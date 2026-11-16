#!/usr/bin/env python3
"""Measure memoizing packed bone words within one skin draw on the host."""

import argparse
from pathlib import Path
import subprocess
import tempfile
from textwrap import dedent


HARNESS = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { float x, y, z; } V3;
typedef struct { V3 right; int flags; V3 up; int pad1; V3 at; int pad2; V3 pos; int pad3; } Matrix;
typedef uint32_t AliasWord __attribute__((may_alias));
enum { SLOTS = 8, BONES = 256, WORDS = 12, MAX_BATCHES = 113, SAMPLES = 9, TRIALS = 2000 };
static Matrix bones[BONES];
static unsigned char palette[MAX_BATCHES][SLOTS];
static uint32_t baseline[MAX_BATCHES * SLOTS * (WORDS + 1)], candidate[sizeof baseline / sizeof baseline[0]];
static volatile uint32_t sink;
static const float packing[] = { -1.875f, 6.25f, 1.125f, 0.53125f };

static void pack(const Matrix *bm, uint32_t *out)
{
    const float *c = packing, s = packing[3];
    Matrix folded;
    FOLD_FROM_RENDERER
    const AliasWord *values = (const AliasWord *)&folded;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 3; col++)
            out[row * 3 + col] = 43u << 24 | values[row * 4 + col] >> 8;
}

static size_t __attribute__((noinline)) emit(uint32_t *out, int batches, int slots, int memo, int *folds)
{
    uint32_t packed[BONES][WORDS];
    unsigned char valid[BONES];
    int slot_bone[SLOTS];
    uint32_t *start = out;
    if (memo)
        memset(valid, 0, sizeof valid);
    for (int slot = 0; slot < slots; slot++)
        slot_bone[slot] = -1;
    *folds = 0;
    for (int batch = 0; batch < batches; batch++) {
        for (int slot = 0; slot < slots; slot++) {
            int index = palette[batch][slot];
            if (slot_bone[slot] == index)
                continue;
            slot_bone[slot] = index;
            *out++ = 42u << 24 | (unsigned)(slot * WORDS);
            if (memo) {
                if (!valid[index]) {
                    pack(&bones[index], packed[index]);
                    valid[index] = 1;
                    ++*folds;
                }
                memcpy(out, packed[index], WORDS * sizeof *out);
            } else {
                pack(&bones[index], out);
                ++*folds;
            }
            out += WORDS;
        }
    }
    return out - start;
}

static void make_pose(int pose)
{
    for (int i = 0; i < BONES; i++) {
        float x = (i * 17 % 127 - 64 + pose) / 16.0f;
        bones[i] = (Matrix){ {x, 0, 0.375f}, 0, {0.25f, x, 0}, 0, {-0.5f, 0, x}, 0, {x + pose, -2 * x, 3 * x}, 0 };
    }
}

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static double time_arm(int memo, int batches, int slots)
{
    uint32_t *out = memo ? candidate : baseline;
    uint64_t start = now_ns();
    for (int trial = 0; trial < TRIALS; trial++) {
        int folds;
        size_t words = emit(out, batches, slots, memo, &folds);
        sink ^= out[trial % words];
    }
    return (now_ns() - start) / (double)TRIALS;
}

static int compare_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void run_case(const char *label, int batches, int slots, int unique)
{
    for (int b = 0; b < batches; b++)
        for (int s = 0; s < slots; s++)
            palette[b][s] = unique ? b * slots + s : (3 * b + s) % 48;
    int old_folds, new_folds;
    size_t words = 0;
    for (int pose = 0; pose < 2; pose++) {
        make_pose(pose);
        words = emit(baseline, batches, slots, 0, &old_folds);
        assert(words == emit(candidate, batches, slots, 1, &new_folds));
        assert(memcmp(baseline, candidate, words * sizeof *baseline) == 0);
    }
    double ns[2][SAMPLES];
    for (int sample = -1; sample < SAMPLES; sample++) {
        for (int arm = 0; arm < 2; arm++) {
            int memo = (arm + sample + 1) % 2;
            double t = time_arm(memo, batches, slots);
            if (sample >= 0)
                ns[memo][sample] = t / 1000;
        }
    }
    for (int arm = 0; arm < 2; arm++)
        qsort(ns[arm], SAMPLES, sizeof ns[arm][0], compare_double);
    printf("%s\t%d\t%d\t%zu\t%d\t%d\t%.2f (%.2f-%.2f)\t%.2f (%.2f-%.2f)\n", label, batches, slots,
           words, old_folds, new_folds, ns[0][4], ns[0][0], ns[0][8], ns[1][4], ns[1][0], ns[1][8]);
}

int main(void)
{
    puts("case\tbatches\tslots\tidentical_GE_words\tbaseline_folds\tmemo_folds\tbaseline_host_us\tmemo_host_us");
    run_case("reused_bones", 113, 5, 0);
    run_case("unique_bones", 32, 8, 1);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('renderer', type=Path, nargs='?', default=Path(__file__).resolve().parents[1] / 'port/rwsky.c')
    args = parser.parse_args()
    source = args.renderer.read_text()
    start = source.index('                folded = *bm;', source.index('static int draw_skinned_ge('))
    end = source.index('                bm = &folded;', start)
    fold = dedent(source[start:end]).strip()
    with tempfile.TemporaryDirectory(prefix='p4-skin-pack-') as temp:
        code, program = (Path(temp) / name for name in ('bench.c', 'bench'))
        code.write_text(HARNESS.replace('FOLD_FROM_RENDERER', fold))
        subprocess.run(['gcc', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', str(code), '-o', str(program)], check=True)
        subprocess.run([program], check=True)


if __name__ == '__main__':
    main()
