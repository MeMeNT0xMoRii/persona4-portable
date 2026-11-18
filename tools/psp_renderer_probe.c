/* Synthetic command, timing and image A/B for the renderer. No game inputs. */
#include <stdarg.h>
#include <psppower.h>
#define P4_GP 0x09010000u
#define P4_K 0x08ae0000u
#define P4_IMAGE 0x0900da00u
#include P4_PROBE_RENDERER

PSP_MODULE_INFO("P4 perf01 A/B probe", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);

static unsigned commands[65536] __attribute__((aligned(64)));
static unsigned pixels[SCREEN_H][STRIDE] __attribute__((aligned(64)));
static struct {
    unsigned pad[16];
    unsigned rgba[64];
} images[2] __attribute__((aligned(64)));
static Raster rasters[2];
static struct {
    unsigned pad[16];
    unsigned char texels[64];
    unsigned clut[16];
} indexed_image __attribute__((aligned(64)));
static Raster indexed_raster;
static GuVertex geometry[4] __attribute__((aligned(64)));
static unsigned short indices16[6] __attribute__((aligned(64))) = {0,1,2, 2,1,3};
static unsigned char indices8[6] __attribute__((aligned(64))) = {0,1,2, 2,1,3};

void p4_log(const char *format, ...)
{
    (void)format;
}

static void init_gu(void)
{
    scePowerSetClockFrequency(333, 333, 166);
    sceGuInit();
    sceGuStart(GU_DIRECT, commands);
    sceGuDrawBuffer(GU_PSM_8888, NULL, STRIDE);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)(STRIDE * SCREEN_H * 4), STRIDE);
    sceGuDepthBuffer((void *)(STRIDE * SCREEN_H * 8), STRIDE);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthRange(65535, 0);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    for (unsigned i = 0; i < 64; i++) {
        images[0].rgba[i] = 0x90000000 | i * 3 << 16 | i * 4 << 8 | i;
        images[1].rgba[i] = 0xc0000000 | i << 16 | i * 2 << 8 | (255 - i * 3);
    }
    for (int i = 0; i < 2; i++) {
        rasters[i].originalPixels = (unsigned char *)images[i].rgba;
        rasters[i].width = rasters[i].height = 8;
        rasters[i].stride = 32;
        rasters[i].depth = 32;
    }
    for (int i = 0; i < 16; i++) {
        indexed_image.clut[i] = 0x90000011 | (unsigned)(16 * i) << 8;
    }
    for (int i = 0; i < 64; i++)
        indexed_image.texels[i] = (i & 15) * 0x11;
    indexed_raster = (Raster) {
        .width = 8, .height = 8, .depth = 4, .stride = 4,
        .originalPixels = indexed_image.texels, .palette = (unsigned char *)indexed_image.clut,
    };
    sceKernelDcacheWritebackAll();
}

static void ui_quad(Vertex2D out[4], float x, float y, unsigned left_alpha, unsigned right_alpha, float z)
{
    for (int i = 0; i < 4; i++)
        out[i] = (Vertex2D){
            .x = x + (i & 1 ? 93.5f : 0), .y = y + (i & 2 ? 44.75f : 0),
            .z = z, .u = i & 1, .v = !!(i & 2), .r = 208, .g = 240, .b = 192,
            .a = i & 1 ? right_alpha : left_alpha,
        };
}

static void record_draw(const char *name, FILE *report, int prim, const Vertex2D *v, const unsigned short *idx, int count)
{
    const unsigned *start = gu_list->current;
    draw(prim, v, idx, count);
    int n = gu_list->current - start;
    fprintf(report, "list %-18s %d\n",name,n);
}

static void start_picture(void)
{
    sceGuStart(GU_DIRECT, commands);
    list_to_cached();
    in_frame = 1;
    draw_buffer = 0;
    sceGuDepthMask(GU_FALSE);
    sceGuPixelMask(0);
    sceGuClearColor(0xa5204040);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_STENCIL_BUFFER_BIT);
    sceGuDrawBufferList(GU_PSM_8888, NULL, STRIDE);
}

static void ui_scene(FILE *report)
{
    Vertex2D vertices[4];
    render_state[1] = (int)&rasters[0];
    render_state[12] = 1;
    sky_state[2] = BLEND_NORMAL;
    sky_state[3] = 0x70000;
    ui_quad(vertices, -9, 24, 120, 200, 3200);
    record_draw("first", report, 4, vertices, 0, 4);
    ui_quad(vertices, 63.25f, 24, 130, 95, 3200);
    record_draw("adjacent", report, 4, vertices, 0, 4);
    render_state[1] = (int)&indexed_raster;
    ui_quad(vertices, 128.5f, 24, 175, 176, 3200);
    record_draw("clut_switch", report, 4, vertices, 0, 4);
    render_state[1] = (int)&rasters[1];
    ui_quad(vertices, 198, 24, 255, 161, 3200);
    record_draw("raster_switch", report, 4, vertices, 0, 4);
    sky_state[2] = 0;
    render_state[12] = 0;
    ui_quad(vertices, 282, 24, 225, 90, 3200);
    record_draw("blend_off", report, 4, vertices, 0, 4);
    ui_quad(vertices, 350, 24, 225, 90, 3200);
    record_draw("opaque_adjacent", report, 4, vertices, 0, 4);
    sky_state[2] = BLEND_NORMAL;
    sky_state[3] = 0x31001; /* NEVER, FB_ONLY. */
    ui_quad(vertices, 435, 24, 255, 175, 3200);
    record_draw("blend_on", report, 4, vertices, 0, 4);
    ui_quad(vertices, 525, 24, 170, 220, 3200);
    record_draw("fb_only_quad", report, 4, vertices, 0, 4);
    sceGuScissor(20, 0, SCREEN_W - 20, SCREEN_H); /* interleaved GE command */
    ui_quad(vertices, -10, 79, 140, 160, 3200);
    record_draw("scissor_boundary", report, 4, vertices, 0, 4);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);

    sky_state[3] = 0x71001;
    render_state[RS_ZTEST] = render_state[RS_ZWRITE] = 1;
    ui_quad(vertices, 125, 91, 220, 220, 40000);
    record_draw("depth_write", report, 4, vertices, 0, 4);
    ui_quad(vertices, 138, 95, 200, 240, 33000);
    record_draw("occluded_quad", report, 4, vertices, 0, 4);
    render_state[RS_ZWRITE] = 0;

    unsigned left[] = {5, 9, 34, 63, 127};
    unsigned right[] = {7, 27, 60, 127, 128};
    sky_state[3] = 0x31001;
    pixel_mask = 0x00ffffff;
    for (int i = 0; i < 5; i++) {
        ui_quad(vertices, 3 + 110 * i, 105, left[i], right[i], 33000);
        char name[20];
        snprintf(name, sizeof name, "gradient_%u_%u", left[i], right[i]);
        record_draw(name, report, 4, vertices, 0, 4);
    }
    ui_quad(vertices, 537, 158, 5, 27, 33000);
    vertices[3] = vertices[0];
    vertices[3].u = 1.0f;
    record_draw("folded_mask", report, 4, vertices, 0, 4); /* conservative legacy fallback */
    pixel_mask = 0;

    render_state[RS_ZTEST] = 0;
    sky_state[3] = 0x70000;
    sky_state[2] = BLEND_ADD;
    ui_quad(vertices, 410, 195, 175, 175, 3000);
    record_draw("additive", report, 4, vertices, 0, 4);
    sky_state[2] = BLEND_NORMAL;
    system_cursor_mask = 2;
    ui_quad(vertices, 210, 188, 180, 195, 3200);
    record_draw("cursor_mask", report, 4, vertices, 0, 4);
    system_cursor_mask = 0;
    calendar_depth_pass = 1;
    calendar_date_clip = 1;
    ui_quad(vertices, 80, 205, 190, 190, 3200);
    record_draw("calendar_clip", report, 4, vertices, 0, 4);
    calendar_depth_pass = calendar_date_clip = 0;
    ui_quad(vertices, 16, 220, 205, 175, 3200);
    record_draw("after_special", report, 4, vertices, 0, 4);
    ui_quad(vertices, 108, 220, 255, 82, 3200);
    record_draw("reuse_after_reset", report, 4, vertices, 0, 4);
    /* Exercise the screen-copy path after an ordinary strip. */
    render_state[1] = (int)&display_raster;
    sky_state[2] = BLEND_NORMAL;
    sky_state[3] = 0x70000;
    ui_quad(vertices, 190, 269, 190, 175, 3200);
    record_draw("framebuffer_copy", report, 4, vertices, 0, 4);
    render_state[1] = (int)&rasters[0];
}

static void indexed_scene(FILE *report)
{
    static const unsigned colours[] = {0xff310ad0, 0xffcc3f00, 0xff22e011, 0xffa05070};
    gu_dirty();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuPixelMask(0);
    for (int i = 0; i < 4; i++)
        geometry[i] = (GuVertex){.colour=colours[i], .x=228+(i&1)*70, .y=204+(i>>1)*45, .z=0};
    sceKernelDcacheWritebackRange(geometry, sizeof geometry);
    gu_mesh_begin();
    int format = GU_2D_FORMAT;
    const unsigned *start = gu_list->current;
    gu_draw(GU_TRIANGLES, format | GU_INDEX_16BIT, 6, indices16, geometry);
    fprintf(report, "indexed16 %td\n", gu_list->current - start);
    start = gu_list->current;
    gu_draw(GU_TRIANGLES, format | GU_INDEX_8BIT, 6, indices8, geometry);
    fprintf(report, "indexed8 %td\n", gu_list->current - start);
    gu_dirty();
}

static void finish_picture(void)
{
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE, (void *)0x04000000, 0, 0, STRIDE, pixels);
    sceGuTexSync();
    sceGuFinish();
    in_frame = 0;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceKernelDcacheInvalidateRange(pixels, sizeof pixels);
}

/* The reference draws each triangle in retail order. A GE equality test for
   each alpha value is independent of the renderer's bit-plane algorithm. */
static void mask_picture(int reference)
{
    start_picture();
    gu_dirty();
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuClearDepth(20000);
    sceGuDepthMask(GU_FALSE);
    sceGuClear(GU_DEPTH_BUFFER_BIT);
    render_state[RS_ZWRITE] = 0;
    for (int tile = 0; tile < 7; tile++) {
        int prim = tile == 0 || tile == 3 ? GU_TRIANGLES :
                   tile == 1 || tile == 4 ? GU_TRIANGLE_STRIP : GU_TRIANGLE_FAN;
        int count = tile == 0 ? 3 : prim == GU_TRIANGLES ? 6 : 4;
        GuVertex *v = sceGuGetMemory(count * sizeof *v);
        static const GuVertex shape[6] = {
            {0,0,0x177f9781,4,4,9000}, {1,0,0xa6718652,49,8,28000},
            {0,1,0xef628ba4,10,58,50000}, {1,1,0x40435465,15,15,37000},
            {1,0,0x6d48596a,24,33,44000}, {0,1,0x82394a5b,37,14,41000},
        };
        for (int i = 0; i < count; i++) {
            v[i] = shape[i];
            v[i].x += 64 * tile;
            if (tile == 0 || tile == 4 || tile == 6)
                v[i].colour = (v[i].colour & 0xffffff) | (unsigned)(7 - i * 2) << 24;
        }
        if (tile & 1)
            bind_raster(&rasters[0], GU_CLAMP);
        else
            sceGuDisable(GU_TEXTURE_2D);
        if (!reference) {
            draw_effect_mask(prim, count, v);
            continue;
        }
        sceGuDepthMask(GU_TRUE);
        sceGuEnable(GU_ALPHA_TEST);
        sceGuEnable(GU_STENCIL_TEST);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
        sceGuPixelMask(0x00ffffff);
        for (int t = 0; t < (count == 3 ? 1 : 2); t++) {
            GuVertex *tri = sceGuGetMemory(3 * sizeof *tri);
            tri[0] = v[t == 0 ? 0 : prim == GU_TRIANGLES ? 3 : prim == GU_TRIANGLE_STRIP ? 2 : 0];
            tri[1] = v[t == 0 ? 1 : prim == GU_TRIANGLES ? 4 : prim == GU_TRIANGLE_STRIP ? 1 : 2];
            tri[2] = v[t == 0 ? 2 : prim == GU_TRIANGLES ? 5 : 3];
            for (int a = 0; a < 256; a++) {
                sceGuAlphaFunc(GU_EQUAL, a, 0xff);
                int stored = tile == 0 || tile == 4 || tile == 6 ? a : (a + 42) / 85 * 85;
                sceGuStencilFunc(GU_ALWAYS, stored, 0xff);
                sceGuDrawArray(GU_TRIANGLES, GU_2D_FORMAT, 3, NULL, tri);
            }
        }
    }
    finish_picture();
}

static void check_mask_order(FILE *report)
{
    enum { WIDTH = 448, HEIGHT = 64 };
    static unsigned expected[HEIGHT][WIDTH];
    mask_picture(1);
    for (int y = 0; y < HEIGHT; y++)
        memcpy(expected[y], pixels[y], WIDTH * sizeof(unsigned));
    mask_picture(0);
    int differences = 0, covered = 0, alpha_values = 0;
    unsigned char seen[256] = {0};
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            unsigned value = expected[y][x];
            differences += pixels[y][x] != value;
            covered += value != expected[0][0];
            seen[value >> 24] = 1;
        }
    }
    for (int a = 0; a < 256; a++)
        alpha_values += seen[a];
    fprintf(report, "ordered-masks pixels %d covered %d alphas %d differences %d\n",
            WIDTH * HEIGHT, covered, alpha_values, differences);
}

static void measure_submission(FILE *report)
{
    enum { SAMPLES = 9, LISTS = 12, DRAWS = 120 };
    Vertex2D v[4];
    ui_quad(v, 0, 0, 190, 180, 0);
    render_state[1] = (int)&rasters[0];
    render_state[RS_ZTEST] = render_state[RS_ZWRITE] = 0;
    render_state[12] = 1;
    sky_state[2] = BLEND_NORMAL;
    sky_state[3] = 0x70000;
    list_cached_now = 0;
    for (int j = -1; j < SAMPLES; j++) {
        unsigned total = 0;
        int words = 0;
        for (int i = 0; i < LISTS; i++) {
            sceGuStart(GU_CALL, commands);
            gu_dirty();
            in_frame = 1;
            const unsigned *start = gu_list->current;
            unsigned time = sceKernelGetSystemTimeLow();
            for (int k = 0; k < DRAWS; k++) draw(4, v, 0, 4);
            total += sceKernelGetSystemTimeLow() - time;
            words = gu_list->current - start;
            sceGuFinish();
            in_frame = 0;
        }
        if (j >= 0)
            fprintf(report, "synthetic-ui %d %d %d %u\n", LISTS, DRAWS, words, total);
    }
}

static void measure_call_key(FILE *report)
{
    enum { SAMPLES = 9, KEYS = 16384 };
    unsigned inputs[34], h[2];
    for (unsigned k = 0; k < 34; k++)
        inputs[k] = 0x0b056432u * (k + 1);
    memset(&gs, 0, sizeof gs);
    for (int s = -1; s < SAMPLES; s++) {
        unsigned check0 = 0, check1 = 0;
        unsigned start = sceKernelGetSystemTimeLow();
        for (unsigned k = 0; k < KEYS; k++) {
            inputs[0] = k * 0x9e3779b1u;
            gs.enabled = k * 0x27d4eb2fu;
            gs.texscale_known = k & 1;
            call_key(inputs, sizeof inputs / sizeof *inputs, h);
            check0 ^= h[0];
            check1 += h[1];
        }
        unsigned elapsed = sceKernelGetSystemTimeLow() - start;
        if (s >= 0)
            fprintf(report, "synthetic-key %u %u %08x %08x\n", KEYS, elapsed, check0, check1);
    }
}

int main(void)
{
    FILE *report = fopen("ms0:/p4-perf01-probe.txt", "w");
    if (!report)
        return 1;
    init_gu();
    start_picture();
    ui_scene(report);
    indexed_scene(report);
    finish_picture();
    FILE *raw = fopen("ms0:/p4-perf01-pixels.rgba", "wb");
    if (!raw) {
        fclose(report);
        return 2;
    }
    for (int y = 0; y < SCREEN_H; y++)
        fwrite(pixels[y], sizeof(unsigned), SCREEN_W, raw);
    fclose(raw);
    check_mask_order(report);
    measure_submission(report);
    measure_call_key(report);
    fputs("done\n", report);
    fclose(report);
    sceKernelSleepThread();
    return 0;
}
