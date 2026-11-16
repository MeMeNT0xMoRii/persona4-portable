#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <psppower.h>
#include <math.h>
#include <stdio.h>

PSP_MODULE_INFO("P4 Mask Timing", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

enum { WIDTH = 480, HEIGHT = 272, STRIDE = 512, RAMP = 256, RAMP_X = 16, RS_ZWRITE = 8 };
enum { GU_2D_FORMAT = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D };
typedef struct { float u, v; unsigned colour; float x, y, z; } GuVertex;
typedef void (*DrawMask)(int prim, int count, const GuVertex *vertices);
static unsigned commands[32768] __attribute__((aligned(64)));
static unsigned calls[8192] __attribute__((aligned(64)));
static unsigned texture[RAMP * 8] __attribute__((aligned(64)));
static unsigned reference[WIDTH * HEIGHT];
static int render_state[64];
static volatile const unsigned *pixels = (volatile const unsigned *)0x44000000;

#include "mask_arms.h"

static const DrawMask arms[] = {draw_effect_mask_20, draw_effect_mask_21, draw_effect_mask_current};

static void fill_quad(GuVertex *v, int y, int width, int height, unsigned alpha)
{
    unsigned colour = (alpha << 24) | 0xffffff;
    v[0] = (GuVertex){0, 0, colour, RAMP_X, y, 0};
    v[1] = (GuVertex){RAMP, 0, colour, RAMP_X + width, y, 0};
    v[2] = (GuVertex){0, 8, colour, RAMP_X, y + height, 0};
    v[3] = (GuVertex){RAMP, 8, colour, RAMP_X + width, y + height, 0};
}

static void bind_texture(void)
{
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, RAMP, 8, RAMP, texture);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

static void end_list(void)
{
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static void draw_ramp(DrawMask mask)
{
    sceGuStart(GU_DIRECT, commands);
    sceGuClearStencil(0xa5);
    sceGuClearColor(0x00080808);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
    bind_texture();
    end_list();
    for (int y = 0; y < RAMP; y += 32) {
        sceGuStart(GU_DIRECT, commands);
        for (int row = y; row < y + 32; row++) {
            GuVertex *v = sceGuGetMemory(4 * sizeof *v);
            fill_quad(v, row, RAMP, 1, row);
            mask(GU_TRIANGLE_STRIP, 4, v);
        }
        end_list();
    }
    sceGuStart(GU_DIRECT, commands);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    GuVertex *v = sceGuGetMemory(4 * sizeof *v);
    fill_quad(v, RAMP + 1, RAMP, 12, 7);
    v[1].colour = v[3].colour = 0xffffffff;
    v[1].y += 1;
    v[3].y += 1;
    mask(GU_TRIANGLE_STRIP, 4, v);
    end_list();
}

static void blend_ramp(void)
{
    sceGuStart(GU_DIRECT, commands);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_DST_ALPHA, GU_FIX, 0, 0xffffff);
    sceGuPixelMask(0xff000000);
    GuVertex *v = sceGuGetMemory(2 * sizeof *v);
    v[0] = (GuVertex){0, 0, 0xff0000c0, RAMP_X, 0, 0};
    v[1] = (GuVertex){0, 0, 0xff0000c0, RAMP_X + RAMP, HEIGHT, 0};
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, 0, v);
    sceGuDisable(GU_BLEND);
    sceGuPixelMask(0);
    end_list();
}

static unsigned compare_frame(int record)
{
    unsigned mismatches = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            unsigned word = pixels[y * STRIDE + x];
            unsigned *expected = &reference[y * WIDTH + x];
            if (record)
                *expected = word;
            else
                mismatches += *expected != word;
        }
    }
    return mismatches;
}

static unsigned check_ramp(void)
{
    unsigned mismatches = 0;
    for (int y = 0; y < RAMP; y++) {
        for (int x = 0; x < RAMP; x++) {
            unsigned fragment = (y + 1) * x >> 8;
            mismatches += pixels[y * STRIDE + RAMP_X + x] != ((fragment << 24) | 0x080808);
        }
    }
    return mismatches;
}

static void compare_masks(FILE *f)
{
    fputs("arm\tformula_samples\tformula_mismatches\tframe_pixels\tblended_frame_mismatches\n", f);
    for (int arm = 1; arm <= 2; arm++) {
        draw_ramp(arms[arm]);
        unsigned gradient_errors = check_ramp();
        blend_ramp();
        unsigned compare_errors = compare_frame(arm == 1);
        fprintf(f, "%d\t%d\t%u\t%d\t%u\n", arm, RAMP * RAMP, gradient_errors, WIDTH * HEIGHT, compare_errors);
    }
}

static void event(FILE *f, int alpha, int sample, int arm, const char *part)
{
    if (f) {
        fprintf(f, "%d\t%d\t%d\t%s\n", alpha, sample, arm, part);
        fflush(f);
    }
}

static void time_mask(FILE *f, FILE *events, int alpha, int sample, int arm)
{
    enum { BUILDS = 8, MASKS = 8, EXECUTIONS = 8 };
    static GuVertex quad[4] __attribute__((aligned(64)));
    fill_quad(quad, 32, 240, 144, alpha);
    sceKernelDcacheWritebackRange(quad, sizeof quad);
    unsigned start = sceKernelGetSystemTimeLow();
    unsigned bytes = 0;
    for (int i = 0; i < BUILDS; i++) {
        sceGuStart(GU_CALL, calls);
        for (int m = 0; m < MASKS; m++)
            arms[arm](GU_TRIANGLE_STRIP, 4, quad);
        bytes = sceGuFinish();
    }
    unsigned submitted = sceKernelGetSystemTimeLow() - start;
    const volatile unsigned *words = (const volatile unsigned *)((unsigned)calls | 0x40000000u);
    unsigned prims = 0;
    for (unsigned i = 0; i < bytes / sizeof *calls; i++)
        prims += words[i] >> 24 == 4; /* GE PRIM */

    sceGuStart(GU_DIRECT, commands);
    sceGuClearStencil(0xa5);
    sceGuClear(GU_STENCIL_BUFFER_BIT);
    bind_texture();
    end_list();
    event(events, alpha, sample, arm, "begin");
    start = sceKernelGetSystemTimeLow();
    for (int i = 0; i < EXECUTIONS; i++) {
        sceGuStart(GU_DIRECT, commands);
        sceGuCallList(calls);
        end_list();
    }
    unsigned executed = sceKernelGetSystemTimeLow() - start;
    unsigned alpha_sample = pixels[64 * STRIDE + RAMP_X + 220] >> 24;
    event(events, alpha, sample, arm, "end");
    if (sample >= 0)
        fprintf(f, "%d\t%d\t%d\t%d\t%u\t%u\t%u\t%u\t%u\n", alpha, sample, arm, MASKS, prims, bytes,
                submitted, executed, alpha_sample);
}

int main(void)
{
    scePowerSetClockFrequency(333, 333, 166);
    for (int i = 0; i < RAMP * 8; i++)
        texture[i] = ((unsigned)(i % RAMP) << 24) | 0xffffff;
    sceKernelDcacheWritebackRange(texture, sizeof texture);
    sceGuInit();
    sceGuStart(GU_DIRECT, commands);
    sceGuDrawBuffer(GU_PSM_8888, NULL, STRIDE);
    sceGuDispBuffer(WIDTH, HEIGHT, (void *)(STRIDE * HEIGHT * 4), STRIDE);
    sceGuDepthBuffer((void *)(STRIDE * HEIGHT * 8), STRIDE);
    sceGuViewport(2048, 2048, WIDTH, HEIGHT);
    sceGuOffset(2048 - WIDTH / 2, 2048 - HEIGHT / 2);
    sceGuScissor(0, 0, WIDTH, HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    end_list();
    sceGuDisplay(GU_TRUE);

    FILE *parity = fopen("ms0:/p4-mask-parity.tsv", "w");
    if (parity) {
        compare_masks(parity);
        fclose(parity);
    }
    FILE *timing = fopen("ms0:/p4-mask-timing.tsv", "w");
    FILE *events = fopen("ms0:/p4-mask-events.tsv", "w");
    if (timing) {
        static const int alphas[] = {0, 32, 64, 127, 128, 255};
        fprintf(timing, "# guest_clock_mhz\t%d\n", scePowerGetCpuClockFrequencyInt());
        fputs("alpha\tsample\tarm\tmasks_per_list\tprims_per_list\tbytes_per_list\t8_builds_us\t8_executions_us\talpha_sample\n", timing);
        for (unsigned j = 0; j < sizeof alphas / sizeof *alphas; j++) {
            for (int sample = -1; sample < 5; sample++) {
                for (int arm = 0; arm < 3; arm++)
                    time_mask(timing, events, alphas[j], sample, (arm + sample + 1) % 3);
            }
        }
        fclose(timing);
    }
    if (events)
        fclose(events);
    FILE *done = fopen("ms0:/p4-mask-done.txt", "w");
    if (done) {
        fputs("done\n", done);
        fclose(done);
    }
    sceKernelSleepThread();
    return 0;
}
