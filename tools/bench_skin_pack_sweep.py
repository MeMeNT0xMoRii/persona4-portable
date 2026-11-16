"""Compare the home21 packed-bone math with a per-skin-draw memo."""
import argparse
import os
from pathlib import Path
import subprocess
from textwrap import dedent


HARNESS = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __psp__
#include <pspkernel.h>
#include <psppower.h>
PSP_MODULE_INFO("P4 Bone Sweep", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
#else
#include <time.h>
#endif

typedef struct { float x, y, z; } V3;
typedef struct { V3 right; int flags; V3 up; int pad1; V3 at; int pad2; V3 pos; int pad3; } Matrix;
typedef uint32_t AliasWord __attribute__((may_alias));
enum { ACTORS = 5, POSES = 4, BATCHES = 24, SLOTS = 8, BONES = 256, WORDS = 12, SAMPLES = 7 };
#ifdef __psp__
enum { TRIALS = 256 };
#else
enum { TRIALS = 1024 };
#endif
typedef struct { unsigned char count, bone[SLOTS]; } Palette;
static const int actor_batches[ACTORS] = {24, 22, 21, 22, 22};
static Palette palette[ACTORS][BATCHES];
static Matrix pose[POSES][ACTORS][BONES];
static float packing[POSES][ACTORS][4];
static uint32_t memo[BONES][WORDS], original[BATCHES * SLOTS * (WORDS + 1)], comparison[sizeof original / sizeof original[0]];
static volatile uint32_t sink;

static void pack(const Matrix *bm, const float *c, uint32_t *words)
{
    Matrix folded;
    float s = c[3];
    FOLD_FROM_RENDERER
    const AliasWord *f = (const AliasWord *)&folded;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 3; col++)
            words[row * 3 + col] = 43u << 24 | f[row * 4 + col] >> 8;
}

static size_t __attribute__((noinline)) emit(uint32_t *out, int actor, int phase, int use_memo, int clipped, int *folds)
{
    uint32_t valid[BONES / 32];
    int slot_bone[SLOTS];
    uint32_t *start = out;
    if (use_memo)
        memset(valid, 0, sizeof valid);
    for (int k = 0; k < SLOTS; k++)
        slot_bone[k] = -1;
    *folds = 0;
    for (int b = 0; b < actor_batches[actor]; b++) {
        if (clipped && (b + actor) % 3 == 0)
            continue;
        const Palette *p = &palette[actor][b];
        for (int slot = 0; slot < p->count; slot++) {
            unsigned index = p->bone[slot];
            if (slot_bone[slot] == (int)index)
                continue;
            slot_bone[slot] = (int)index;
            *out++ = 42u << 24 | (unsigned)(slot * WORDS);
            const Matrix *bm = &pose[phase][actor][index];
            const float *c = packing[phase][actor];
            if (use_memo) {
                uint32_t bit = 1u << (index & 31);
                if (!(valid[index >> 5] & bit)) {
                    pack(bm, c, memo[index]);
                    valid[index >> 5] |= bit;
                    ++*folds;
                }
                memcpy(out, memo[index], WORDS * sizeof *out);
            } else {
                pack(bm, c, out);
                ++*folds;
            }
            out += WORDS;
        }
    }
    return out - start;
}

static void init_pose(void)
{
    for (int phase = 0; phase < POSES; phase++) {
        for (int actor = 0; actor < ACTORS; actor++) {
            float *c = packing[phase][actor];
            c[0] = -1.25f + actor;
            c[1] = 5.75f - phase;
            c[2] = -1.125f * phase;
            c[3] = 0.4375f + (actor + phase) / 32.0f;
            for (int j = 0; j < BONES; j++) {
                float x = ((j * 17 + actor * 31 + phase * 7) % 127 - 64) / 16.0f;
                pose[phase][actor][j] = (Matrix){ {x, 0, 0.375f}, 0, {0.25f, x, 0}, 0, {-0.5f, 0, x}, 0,
                                                  {x + phase, -2 * x, 3 * x + actor}, 0 };
            }
        }
    }
}

static void init_palettes(int budget, int sticky)
{
    int total_batches = 0;
    for (int actor = 0; actor < ACTORS; actor++) {
        int i = 0;
        for (int b = 0; b < actor_batches[actor]; b++) {
            Palette *p = &palette[actor][b];
            p->count = sticky ? 6 + (total_batches < 42) : 4 + (b & 1);
            for (int k = 0; k < p->count; k++) {
                if (sticky && k < 2) {
                    /* Match the reported 708 palette entries and 498 uploads.
                       One early replacement costs two of the 212 held slots. */
                    int replace = k == 1 && actor == ACTORS - 1 && b == actor_batches[actor] - 2;
                    p->bone[k] = 255 - k - replace;
                } else {
                    p->bone[k] = i++ % budget;
                }
            }
            ++total_batches;
        }
    }
}

static uint64_t now_ns(void)
{
#ifdef __psp__
    return sceKernelGetSystemTimeWide() * UINT64_C(1000);
#else
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
#endif
}

static double elapsed(int candidate, int phase_seed)
{
    uint32_t *out = candidate ? comparison : original;
    uint64_t before = now_ns();
    for (int trial = 0; trial < TRIALS; trial++) {
        for (int actor = 0; actor < ACTORS; actor++) {
            int folds;
            size_t words = emit(out, actor, (trial + phase_seed) % POSES, candidate, 0, &folds);
            sink ^= out[words - 1];
        }
    }
    return (now_ns() - before) / (1000.0 * TRIALS);
}

static int order(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void run(FILE *f, const char *label, int budget, int sticky)
{
    init_palettes(budget, sticky);
    size_t words = 0;
    int old_folds = 0, new_folds = 0;
    for (int phase = 0; phase < POSES; phase++) {
        for (int actor = 0; actor < ACTORS; actor++) {
            int old, next;
            int clipped = phase == 2;
            size_t length = emit(original, actor, phase, 0, clipped, &old);
            assert(length == emit(comparison, actor, phase, 1, clipped, &next));
            assert(memcmp(original, comparison, length * sizeof *original) == 0);
            if (phase == 0) {
                words += length;
                old_folds += old;
                new_folds += next;
            }
        }
    }
    double us[2][SAMPLES];
    for (int sample = -1; sample < SAMPLES; sample++) {
        for (int arm = 0; arm < 2; arm++) {
            int candidate = (arm + sample + 1) % 2;
            double duration = elapsed(candidate, sample + 1);
            if (sample >= 0)
                us[candidate][sample] = duration;
        }
    }
    for (int arm = 0; arm < 2; arm++)
        qsort(us[arm], SAMPLES, sizeof us[arm][0], order);
    fprintf(f, "%s\t%zu\t%d\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n", label, words, old_folds, new_folds,
            us[0][3], us[1][3], us[0][0], us[0][6], us[1][0], us[1][6]);
}

int main(void)
{
#ifdef __psp__
    scePowerSetClockFrequency(333, 333, 166);
    FILE *f = fopen("ms0:/p4-bone-sweep.tsv", "w");
    if (!f)
        return 1;
    fprintf(f, "# guest_clock_mhz\t%d\n", scePowerGetCpuClockFrequencyInt());
#else
    FILE *f = stdout;
#endif
    init_pose();
    fputs("case\tidentical_GE_words\tfolds_before\tfolds_after\tbaseline_us\tmemo_us\tbaseline_min\tbaseline_max\tmemo_min\tmemo_max\n", f);
    run(f, "unique", 256, 0);
    run(f, "quarter_reused", 75, 0);
    run(f, "half_reused", 49, 0);
    run(f, "high_reuse", 22, 0);
    run(f, "tv_entrance_volume", 37, 1);
#ifdef __psp__
    fclose(f);
    sceKernelSleepThread();
#endif
    return 0;
}
'''


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('renderer', type=Path)
    cli.add_argument('output', type=Path)
    cli.add_argument('--psp', action='store_true')
    args = cli.parse_args()
    src = args.renderer.read_text()
    start = src.index('                folded = *bm;', src.index('static int draw_skinned_ge('))
    end = src.index('                bm = &folded;', start)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'probe.c').write_text(HARNESS.replace('FOLD_FROM_RENDERER', dedent(src[start:end]).strip()))

    def run(*command):
        subprocess.run(command, cwd=out, check=True)

    if not args.psp:
        run('gcc', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', 'probe.c', '-o', 'probe')
        run(out / 'probe')
        return
    psp = Path(os.environ['PSPDEV'])
    sdk = psp / 'psp/sdk'
    run(psp / 'bin/psp-gcc', '-G0', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', f'-I{sdk}/include',
        f'-L{sdk}/lib', f'-specs={sdk}/lib/prxspecs', f'-Wl,-q,-T{sdk}/lib/linkfile.prx', 'probe.c',
        sdk / 'lib/prxexports.o', '-lpsppower', '-o', 'probe.elf')
    run(psp / 'bin/psp-fixup-imports', 'probe.elf')
    run(psp / 'bin/psp-prxgen', 'probe.elf', 'probe.prx')
    run(psp / 'bin/mksfoex', 'P4 Bone Sweep', 'PARAM.SFO')
    run(psp / 'bin/pack-pbp', 'EBOOT.PBP', 'PARAM.SFO', 'NULL', 'NULL', 'NULL', 'NULL', 'NULL', 'probe.prx', 'NULL')


if __name__ == '__main__':
    main()
