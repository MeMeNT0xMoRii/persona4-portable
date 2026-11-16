#include <pspkernel.h>
#include <pspgu.h>
#include <pspdisplay.h>
#include <math.h>
#include <stdio.h>

PSP_MODULE_INFO("P4 Effect Probe", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

enum { WIDTH = 480, HEIGHT = 272, STRIDE = 512, PS2_TEST = 200, RS_ZWRITE = 8 };
enum { CAMERA_NEAR = 0x80, CAMERA_FAR = 0x84, GU_2D_FORMAT = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D };
enum { K_ZERO, K_ONE, K_F, K_ONE_MINUS_F };
enum { MASK_X = 16, MASK_W = 256, MASK_H = 8, STRIP_Y = 86, INDEXED_Y = 156, DEPTH_Y = 238, FOLDED_Y = 256 };
typedef struct { float u, v; unsigned colour; float x, y, z; } Vertex3D, GuVertex;
static unsigned commands[32768] __attribute__((aligned(64)));
static unsigned mask_texture[64 * 8] __attribute__((aligned(64)));
static unsigned precision_texture[MASK_W * MASK_H] __attribute__((aligned(64)));
static unsigned blend_unusual[8], blend_unusual_n;
static int p4_blend_fix, render_state[64];
static float camera[40];
static void *current_camera = camera;
static volatile const unsigned *pixels = (volatile const unsigned *)0x44000000;

#include "effect_helpers.h"

static void make_quad(Vertex3D *out, float x, float y, float w, float h, float depth, unsigned colour)
{
    float l = x * depth / WIDTH, r = (x + w) * depth / WIDTH;
    float t = y * depth / HEIGHT, b = (y + h) * depth / HEIGHT;
    out[0] = (Vertex3D){ 0, 0, colour, l, t, depth };
    out[1] = (Vertex3D){ 0, 0, colour, r, t, depth };
    out[2] = (Vertex3D){ 0, 0, colour, r, b, depth };
    out[3] = out[0];
    out[4] = out[2];
    out[5] = (Vertex3D){ 0, 0, colour, l, b, depth };
}

static void quad(const char *ps2, int x, int y, int w, float depth, unsigned colour)
{
    Vertex3D *vertices = sceGuGetMemory(6 * sizeof *vertices);
    make_quad(vertices, x, y, w, 56, depth, colour);
    draw_ptank_quads(ps2, 6, vertices);
}

static void depth_tiles(void)
{
    static const unsigned test[] = { 0, 0x71801, 0x717fb, 0x71801, 0x71801, 0x71801 };
    for (unsigned i = 0; i < sizeof test / sizeof *test; i++) {
        unsigned ptank[PS2_TEST / 4 + 1] = { [PS2_TEST / 4] = test[i] };
        int x = 8 + i * 80;
        if (i == 3) {
            sceGuDepthMask(GU_FALSE);
            quad(NULL, x, 24, 64, 100, 0xff008000); /* opaque foreground still occludes */
        }
        render_state[RS_ZWRITE] = i != 4;
        sceGuDepthMask(!render_state[RS_ZWRITE]);
        sceGuEnable(GU_BLEND);
        set_blend(0x48);
        quad((const char *)ptank, x, 24, 64, 200, 0x80002080);
        if (i == 5) {
            /* The intervening ordinary draw alone must keep the far layer out. */
            quad(NULL, x, 24, 64, 125, 0xff000020);
        }
        quad(NULL, x, 24, 64, 400, 0x80a04000); /* later layer behind the particle */
        sceGuDisable(GU_BLEND);
    }
}

static void blend_tiles(void)
{
    static const unsigned blend[] = { 0x44, 0x48, 0x42, 0x06 };
    const unsigned ptank[PS2_TEST / 4 + 1] = { [PS2_TEST / 4] = 0x71801 };
    render_state[RS_ZWRITE] = 1;
    sceGuEnable(GU_BLEND);
    for (unsigned i = 0; i < 2 * (sizeof blend / sizeof *blend); i++) {
        int x = 8 + i * 60;
        sceGuDepthMask(GU_FALSE);
        set_blend(blend[i / 2]);
        quad(i & 1 ? (const char *)ptank : NULL, x, 184, 44, 200, 0x80604080);
        set_blend(0x48);
        quad(NULL, x, 184, 44, 400, 0x80604000);
    }
    sceGuDisable(GU_BLEND);
}

static GuVertex *ramp_rectangle(int y, unsigned colour)
{
    GuVertex *v = sceGuGetMemory(2 * sizeof *v);
    v[0] = (GuVertex){ 0, 0, colour, 16, y, 0 };
    v[1] = (GuVertex){ 64, 8, colour, 400, y + 40, 0 };
    return v;
}

static void mask_ramp(void)
{
    static const int ramp[] = { 0, 32, 64, 127, 128, 160, 224, 255 };
    for (unsigned i = 0; i < sizeof mask_texture / sizeof *mask_texture; i++)
        mask_texture[i] = (unsigned)ramp[(i % 64) / 8] << 24 | 0xffffff;
    sceKernelDcacheWritebackRange(mask_texture, sizeof mask_texture);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 64, 8, 64, mask_texture);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    draw_effect_mask(GU_SPRITES, 2, ramp_rectangle(108, 0xffffffff));
    sceGuPixelMask(0xff000000); /* retain alpha for readback after the additive pass */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    set_blend(0x58);
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, NULL, ramp_rectangle(108, 0xff0000c0));
    sceGuDisable(GU_BLEND);
    sceGuPixelMask(0);
}

static GuVertex *mask_quad(int y, unsigned colour, int indexed)
{
    const GuVertex corner[4] = {
        {0, 0, colour, MASK_X, y, 16000}, {MASK_W, 0, colour, MASK_X + MASK_W, y, 16000},
        {0, MASK_H, colour, MASK_X, y + MASK_H, 16000}, {MASK_W, MASK_H, colour, MASK_X + MASK_W, y + MASK_H, 16000},
    };
    static const int index[] = {0, 2, 3, 0, 3, 1};
    int count = indexed ? 6 : 4;
    GuVertex *out = sceGuGetMemory(count * sizeof *out);
    for (int i = 0; i < count; i++)
        out[i] = corner[indexed ? index[i] : i];
    return out;
}

static void mask_precision(void)
{
    for (unsigned i = 0; i < sizeof precision_texture / sizeof *precision_texture; i++)
        precision_texture[i] = (i % MASK_W) << 24 | 0xffffff;
    sceKernelDcacheWritebackRange(precision_texture, sizeof precision_texture);

    /* Begin with a nonzero alpha so an unwritten low bit cannot pass the test. */
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 0xa5, 0xff);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    sceGuPixelMask(0x00ffffff);
    GuVertex *seed = sceGuGetMemory(6 * sizeof *seed);
    static const int y[] = {STRIP_Y, INDEXED_Y, DEPTH_Y};
    for (int i = 0; i < 3; i++) {
        seed[2 * i] = (GuVertex){0, 0, 0, MASK_X, y[i], 0};
        seed[2 * i + 1] = (GuVertex){0, 0, 0, MASK_X + MASK_W, y[i] + MASK_H, 0};
    }
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 6, NULL, seed);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, MASK_W, MASK_H, MASK_W, precision_texture);
    draw_effect_mask(GU_TRIANGLE_STRIP, 4, mask_quad(STRIP_Y, 0xffffffff, 0));
    draw_effect_mask(GU_TRIANGLES, 6, mask_quad(INDEXED_Y, 0x80ffffff, 1));

    GuVertex *folded = mask_quad(FOLDED_Y, 0xffffffff, 0);
    folded[3] = folded[0];
    folded[3].u = MASK_W;
    draw_effect_mask(GU_TRIANGLE_STRIP, 4, folded);

    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuPixelMask(0xffffffff);
    GuVertex *control = sceGuGetMemory(6 * sizeof *control);
    control[0] = (GuVertex){0, 0, 0, MASK_X + 64, DEPTH_Y, 32000};
    control[1] = (GuVertex){0, 0, 0, MASK_X + 80, DEPTH_Y + MASK_H, 32000};
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, NULL, control);
    sceGuScissor(MASK_X + 32, DEPTH_Y, MASK_W - 64, MASK_H);
    sceGuEnable(GU_TEXTURE_2D);
    render_state[RS_ZWRITE] = 1;
    draw_effect_mask(GU_TRIANGLE_STRIP, 4, mask_quad(DEPTH_Y, 0xffffffff, 0));

    /* The first farther control passes when the mask leaves depth intact.
       It must then write depth and occlude the second, even farther control. */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 0x3c, 0xff);
    control[2] = (GuVertex){0, 0, 0, MASK_X + 128, DEPTH_Y, 10000};
    control[3] = (GuVertex){0, 0, 0, MASK_X + 132, DEPTH_Y + MASK_H, 10000};
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, NULL, control + 2);
    sceGuStencilFunc(GU_ALWAYS, 0xff, 0xff);
    control[4] = control[2];
    control[5] = control[3];
    control[4].z = control[5].z = 5000;
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, NULL, control + 4);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuScissor(0, 0, WIDTH, HEIGHT);
    sceGuPixelMask(0);
}

static void report_mask_precision(FILE *result)
{
    static const int y[] = {STRIP_Y, INDEXED_Y, DEPTH_Y, FOLDED_Y};
    for (unsigned row = 0; row < sizeof y / sizeof *y; row++) {
        for (int x = 0; x < MASK_W; x++)
            fprintf(result, "%08x%c", pixels[(y[row] + 2) * STRIDE + MASK_X + x], x == MASK_W - 1 ? '\n' : ' ');
    }
}

static void init_gu(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, commands);
    sceGuDrawBuffer(GU_PSM_8888, NULL, STRIDE);
    sceGuDispBuffer(WIDTH, HEIGHT, (void *)(STRIDE * HEIGHT * 4), STRIDE);
    sceGuDepthBuffer((void *)(STRIDE * HEIGHT * 8), STRIDE);
    sceGuViewport(2048, 2048, WIDTH, HEIGHT);
    sceGuOffset(2048 - WIDTH / 2, 2048 - HEIGHT / 2);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, WIDTH, HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuClearColor(0x00080808);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    camera[CAMERA_NEAR / 4] = 20;
    camera[CAMERA_FAR / 4] = 1500;
    ScePspFMatrix4 projection, identity = { {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1} };
    camera_projection(&projection);
    sceGuSetMatrix(GU_PROJECTION, &projection);
    sceGuSetMatrix(GU_MODEL, &identity);
    sceGuSetMatrix(GU_VIEW, &identity);
}

static void time_layers(FILE *result)
{
    enum { SAMPLES = 5, LISTS = 4, DRAWS = 24, CASES = 3 };
    static Vertex3D vertices[2][6] __attribute__((aligned(64)));
    static const char *const names[] = { "quarter_axes_add", "full_add", "full_opaque" };
    make_quad(vertices[0], 0, 0, WIDTH / 4, HEIGHT / 4, 200, 0x200b0703);
    make_quad(vertices[1], 0, 0, WIDTH, HEIGHT, 200, 0x200b0703);
    sceKernelDcacheWritebackRange(vertices, sizeof vertices);
    fputs("case\tlists\tdraws_per_list\tguest_submit_us\tguest_complete_us\n", result);
    for (int sample = -1; sample < SAMPLES; sample++) {
        for (int index = 0; index < CASES; index++) {
            int mode = (index + sample + 1) % CASES;
            unsigned submit = 0, complete = 0;
            for (int list = 0; list < LISTS; list++) {
                unsigned start = sceKernelGetSystemTimeLow();
                sceGuStart(GU_DIRECT, commands);
                if (mode == 2) {
                    sceGuDisable(GU_BLEND);
                } else {
                    sceGuEnable(GU_BLEND);
                    set_blend(0x48);
                }
                for (int layer = 0; layer < DRAWS; layer++)
                    draw_ptank_quads(NULL, 6, vertices[mode != 0]);
                sceGuFinish();
                submit += sceKernelGetSystemTimeLow() - start;
                sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
                complete += sceKernelGetSystemTimeLow() - start;
            }
            if (sample >= 0)
                fprintf(result, "%s\t%d\t%d\t%u\t%u\n", names[mode], LISTS, DRAWS, submit, complete);
        }
    }
}

int main(void)
{
    init_gu();
    depth_tiles();
    blend_tiles();
    mask_ramp();
    mask_precision();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplaySetFrameBuf((void *)0x04000000, STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
    sceGuDisplay(GU_TRUE);
    FILE *result = fopen("ms0:/p4-effect-probe.txt", "w");
    if (result) {
        for (int i = 0; i < 6; i++)
            fprintf(result, "%08x%c", pixels[44 * STRIDE + i * 80 + 36], i == 5 ? '\n' : ' ');
        for (int i = 0; i < 8; i++)
            fprintf(result, "%08x%c", pixels[127 * STRIDE + i * 48 + 40], i == 7 ? '\n' : ' ');
        for (int i = 0; i < 8; i++)
            fprintf(result, "%08x%c", pixels[204 * STRIDE + i * 60 + 30], i == 7 ? '\n' : ' ');
        fclose(result);
    }
    result = fopen("ms0:/p4-mask-precision.txt", "w");
    if (result) {
        report_mask_precision(result);
        fclose(result);
    }
    result = fopen("ms0:/p4-effect-timing.tsv", "w");
    if (result) {
        time_layers(result);
        fclose(result);
    }
    sceKernelSleepThread();
    return 0;
}
