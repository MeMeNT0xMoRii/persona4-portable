"""Compile the actual port helpers against a small GE recorder on the host."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class RenderState(unittest.TestCase):
    def test_2d_alpha_fail_outputs(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        root = Path(__file__).resolve().parent.parent
        source = (root / 'port/rwsky.c').read_text()
        tables = re.findall(r'^static const int gs_alpha_.*?;', source, re.M)
        draw = re.search(r'^static void draw_2d_tested\(.*?^\}', source, re.M | re.S)[0]
        harness = r'''
#include <assert.h>
enum { GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL };
enum { GU_ALPHA_TEST, GU_TEXTURE_32BITF, GU_COLOR_8888, GU_VERTEX_32BITF, GU_TRANSFORM_2D };
enum { RS_ZWRITE, GU_TRUE = 1 };
typedef struct { unsigned colour; } GuVertex;
static int p4_gs_alpha_test = 1, render_state[1];
static unsigned sky_state[4], pixel_mask, gs_alpha_splits;
static unsigned current_mask;
static int depth_mask, test_enabled, current_method, current_ref, draws;
static struct { unsigned mask; int test, ref, depth; } pass[2];
void sceGuEnable(int x) { assert(x == GU_ALPHA_TEST); test_enabled = 1; }
void sceGuDisable(int x) { assert(x == GU_ALPHA_TEST); test_enabled = 0; }
void sceGuAlphaFunc(int fn, int ref, int mask) { assert(mask == 0xff); current_method = fn; current_ref = ref; }
void sceGuDepthMask(int mask) { depth_mask = mask; }
void sceGuPixelMask(unsigned mask) { current_mask = mask; }
void sceGuDrawArray(int prim, int vtype, int count, int ix, const GuVertex *v) {
    assert(draws < 2);
    pass[draws].mask = current_mask;
    pass[draws].test = test_enabled ? current_method : GU_ALWAYS;
    pass[draws].ref = current_ref;
    pass[draws++].depth = !depth_mask;
}
'''
        checks = r'''
static void test(unsigned at, int zwrite, int masked_writer) {
    sky_state[3] = at;
    render_state[RS_ZWRITE] = zwrite;
    depth_mask = !zwrite;
    current_mask = pixel_mask;
    test_enabled = masked_writer;
    current_method = GU_GREATER;
    current_ref = 0x40;
    draws = 0;
    draw_2d_tested(4, 4, 0, masked_writer);
    assert(depth_mask == !zwrite && current_mask == pixel_mask);
}
int main(void) {
    test(0x32801, 1, 0);
    assert(draws == 1 && pass[0].mask == 0xffffffff && pass[0].depth);
    test(0x32801, 0, 0);
    assert(draws == 0);
    test(0x7000d, 1, 0);
    assert(draws == 1 && pass[0].test == GU_GREATER && pass[0].ref == 0);
    test(0x717fb, 1, 0);
    assert(draws == 2 && pass[0].test == GU_GEQUAL && pass[0].depth);
    assert(pass[1].test == GU_LESS && pass[1].ref == 254 && !pass[1].depth && pass[1].mask == 0);
    test(0x717fb, 0, 0);
    assert(draws == 1 && pass[0].test == GU_ALWAYS);
    pixel_mask = 0x000000ff;
    test(0x737fb, 1, 0);
    assert(draws == 2 && pass[1].mask == 0xff0000ff && !pass[1].depth && !test_enabled);
    pixel_mask = 0x00ffffff;
    test(0x717fb, 1, 1);
    assert(draws == 1 && pass[0].ref == 0x40 && pass[0].mask == 0x00ffffff);
}
'''
        with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness + '\n'.join(tables) + '\n' + draw + checks)
            subprocess.run([gcc, '-O2', str(path / 'check.c'), '-o', str(path / 'check.exe')], check=True, capture_output=True)
            subprocess.run([str(path / 'check.exe')], check=True)

    def test_field_noise_removal_keeps_thirty_hz_pacing(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        root = Path(__file__).resolve().parent.parent
        source = (root / 'port/rwsky.c').read_text()
        present = re.search(r'^int func_003f79b0\([^\n]*\)\n\{.*?^\}', source, re.M | re.S)[0]
        noise = re.search(r'^int FldNoiseUpdate\([^\n]*', source, re.M)[0]
        harness = r'''
#include <assert.h>
enum { GU_SYNC_FINISH, GU_SYNC_WHAT_DONE };
static int field_noise_active, in_frame, vertex_used, feedback_captured, waited;
static unsigned vblank;
static void *draw_buffer;
static int atomics_drawn, atomics_skipped, sectors_drawn, skinned_drawn, pipeline_other, frustum_tests, frustum_out, trace_atomics;
#define G32(x) 0
void watch_device_table(int f) {}
void watch(void) {}
void sceGuFinish(void) {}
void sceGuSync(int a,int b) {}
unsigned sceDisplayGetVcount(void) { return vblank; }
void sceDisplayWaitVblankStart(void) { waited++; vblank++; }
void *sceGuSwapBuffers(void) { return 0; }
void p4_log(const char *fmt,...) {}
'''
        checks = r'''
int main(void) {
    vblank=100;
    assert(FldNoiseUpdate(0)==0); waited=0; func_003f79b0(0,0,0); assert(waited==1 && !field_noise_active);
    FldNoiseUpdate(0); waited=0; func_003f79b0(0,0,0); assert(waited==2);
    vblank++; FldNoiseUpdate(0); waited=0; func_003f79b0(0,0,0); assert(waited==1);
    vblank+=3; FldNoiseUpdate(0); waited=0; func_003f79b0(0,0,0); assert(waited==1);
    waited=0; func_003f79b0(0,0,0); assert(waited==1);
    vblank=0xfffffffeu; FldNoiseUpdate(0); waited=0; func_003f79b0(0,0,0); assert(vblank==0xffffffffu);
    vblank=0; FldNoiseUpdate(0); waited=0; func_003f79b0(0,0,0); assert(waited==1 && vblank==1);
}
'''
        with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness + noise + '\n' + present + checks)
            subprocess.run([gcc, '-O2', str(path / 'check.c'), '-o', str(path / 'check.exe')], check=True, capture_output=True)
            subprocess.run([str(path / 'check.exe')], check=True)

    def test_render_state_and_frame_feedback(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("host gcc is unavailable")
        root = Path(__file__).resolve().parent.parent
        source = (root / "port/rwsky.c").read_text()
        functions = []
        for name in ("bind_depth", "bind_mask", "byte_colour", "put_vertex", "set_blend", "modulate_colour", "capture_feedback", "bind_raster", "rotate", "shade_colour"):
            functions.append(re.search(rf"^static (?:inline )?(?:void|unsigned|V3) {name}\([^\n]*\)\n\{{.*?^\}}", source, re.M | re.S)[0])
        functions.append(re.search(r"^int func_003f7490\([^\n]*\)\n\{.*?^\}", source, re.M | re.S)[0])
        types = re.findall(r"^typedef struct \{[^\n]+\} (?:Vertex2D|GuVertex|V3|Matrix|Material|SceneLight);", source, re.M)
        harness = r'''
#include <assert.h>
#include <math.h>
enum { GU_COLOR_BUFFER_BIT = 1, GU_DEPTH_BUFFER_BIT = 2, GU_STENCIL_BUFFER_BIT = 4, GU_DIRECT = 0 };
static int in_frame, list[1], starts, clears, clear_bits;
void sceGuStart(int mode, void *buffer) { starts++; }
void sceGuClearColor(unsigned color) { assert(color == 0x12345678); }
void sceGuClearDepth(int depth) { assert(depth == 0); }
void sceGuClearStencil(unsigned stencil) { assert(stencil == 0x12); }
void sceGuClear(int bits) { clears++; clear_bits = bits; }
enum { GU_DEPTH_TEST, GU_STENCIL_TEST, GU_TEXTURE_2D, GU_GEQUAL, GU_ALWAYS, GU_FALSE, GU_TRUE, GU_KEEP, GU_REPLACE };
enum { RS_ZTEST = 6, RS_ZWRITE = 8, SCREEN_W = 480, SCREEN_H = 272 };
enum { BLEND_NORMAL = 0x44, BLEND_ADD = 0x48, BLEND_SUBTRACT = 0x42, BLEND_REVERSE = 0x06 };
enum { GU_ADD, GU_REVERSE_SUBTRACT, GU_SUBTRACT, GU_SRC_ALPHA, GU_FIX, GU_DST_ALPHA, GU_ONE_MINUS_DST_ALPHA, GU_ONE_MINUS_SRC_ALPHA };
static int blend_op, blend_src, blend_dst;
static unsigned blend_fixed_dst;
void sceGuBlendFunc(int op, int src, int dst, unsigned a, unsigned b) { blend_op = op; blend_src = src; blend_dst = dst; blend_fixed_dst = b; }
static int render_state[64], enabled[3], depth_func, depth_mask, stencil_ref, stencil_pass;
static unsigned pixel_mask, recorded_mask;
static float texture_w = 1, texture_h = 1;
void sceGuEnable(int x) { enabled[x] = 1; }
void sceGuDisable(int x) { enabled[x] = 0; }
void sceGuDepthFunc(int x) { depth_func = x; }
void sceGuDepthMask(int x) { depth_mask = x; }
void sceGuPixelMask(unsigned x) { recorded_mask = x; }
void sceGuStencilFunc(int fn, int ref, int mask) { assert(fn == GU_ALWAYS && mask == 255); stencil_ref = ref; }
void sceGuStencilOp(int fail, int zfail, int pass) { assert(fail == GU_KEEP && zfail == GU_KEEP); stencil_pass = pass; }
'''
        harness += r'''
typedef struct { unsigned char *originalPixels, *palette; int depth, width, height; } Raster;
static Raster display_raster;
static void *draw_buffer = (void *)0x88000;
static unsigned feedback_pixels[512 * 512];
enum { STRIDE = 512, GU_PSM_8888, GU_PSM_5551, GU_PSM_T8, GU_PSM_T4, GU_TFX_MODULATE, GU_TCC_RGBA, GU_TCC_RGB, GU_LINEAR };
static int copies, snapshots, synced, flushed, feedback_captured;
static unsigned expected_source = 0x4088000;
void sceGuCopyImage(int psm, int x, int y, int w, int h, int stride, void *src, int dx, int dy, int ds, void *dst) {
    assert(stride == 512 && ds == 512 && dst == feedback_pixels);
    assert(dx >= 0 && dy >= 0 && dx + w <= 512 && dy + h <= 512);
    if (src != feedback_pixels) {
        assert(src == (void *)expected_source && w == 480 && h == 272);
        snapshots++;
        for (int row = 0; row < h; row++) for (int col = 0; col < w; col++)
            feedback_pixels[row * 512 + col] = 0x1000000 | (row << 12) | col;
    } else {
        assert(x >= 0 && y >= 0 && x + w <= 512 && y + h <= 512);
        for (int row = 0; row < h; row++) for (int col = 0; col < w; col++)
            feedback_pixels[(dy + row) * 512 + dx + col] = feedback_pixels[(y + row) * 512 + x + col];
    }
    copies++;
}
void sceGuTexSync(void) { assert(copies); synced++; }
void sceGuTexFlush(void) { assert(synced); flushed++; }
void sceGuTexMode(int a, int b, int c, int d) {}
void sceGuTexImage(int a, int b, int c, int d, void *pixels) { assert(flushed && pixels == feedback_pixels); }
void sceGuTexFunc(int a, int b) { assert(b == GU_TCC_RGB); }
void sceGuTexFilter(int a, int b) {}
void sceGuTexWrap(int a, int b) {}
void sceGuClutMode(int a, int b, int c, int d) {}
void sceGuClutLoad(int a, void *b) {}
static int pow2_at_least(int n) { int p = 1; while (p < n) p *= 2; return p; }
'''
        checks = r'''
int main(void) {
    Material material = {0}; material.ambient = material.diffuse = .7f;
    SceneLight light = {{.6f,.6f,.6f}, {.4f,.4f,.4f}, {0,0,1}};
    V3 normal = {0,0,-1}, zero_normal = {0};
    assert(shade_colour(0xff000000, &normal, 0, &material, &light) == 0xffb2b2b2);
    normal.z = 1;
    assert(shade_colour(0x80000000, &normal, 0, &material, &light) == 0x806b6b6b);
    assert(shade_colour(0xff000000, &zero_normal, 0, &material, &light) == 0xff6b6b6b);
    Matrix model = {0}; model.at.z = -1;
    assert(shade_colour(0xff000000, &normal, &model, &material, &light) == 0xffb2b2b2);
    bind_raster(&display_raster, 0);
    assert(snapshots == 1 && synced == copies && flushed == 1 && enabled[GU_TEXTURE_2D]);
    assert(texture_w == 480 && texture_h == 272);
    for (int y = 0; y < 512; y++) for (int x = 0; x < 512; x++)
        assert(feedback_pixels[y * 512 + x] == (0x1000000u | ((y < 272 ? y : 271) << 12) | (x < 480 ? x : 479)));
    bind_raster(&display_raster, 0);
    assert(snapshots == 1 && synced == copies && flushed == 1);
    feedback_captured = 0;
    draw_buffer = 0;
    expected_source = 0x4000000;
    bind_raster(&display_raster, 0);
    assert(snapshots == 2 && synced == copies && flushed == 2);
    bind_raster(0, 0);
    assert(snapshots == 2 && !enabled[GU_TEXTURE_2D]);
    unsigned clear_colour = 0x12345678;
    func_003f7490(0, &clear_colour, 3);
    assert(in_frame && starts == 1 && clears == 1 && clear_bits == 7 && recorded_mask == 0);
    func_003f7490(0, &clear_colour, 2);
    assert(starts == 1 && clears == 2 && clear_bits == 2);
    assert(modulate_colour(0xffffffff, 0xff05c7ff) == 0xff05c7ff);
    assert(modulate_colour(0x80402000, 0xffffffff) == 0x80402000);
    assert(modulate_colour(0x80808080, 0x80808080) == 0x40404040);
    set_blend(0x52);
    assert(blend_op == GU_REVERSE_SUBTRACT && blend_src == GU_DST_ALPHA && blend_dst == GU_FIX && blend_fixed_dst == 0xffffff);
    for (int test = 0; test < 2; test++) for (int write = 0; write < 2; write++) {
        render_state[RS_ZTEST] = test; render_state[RS_ZWRITE] = write; bind_depth();
        assert(enabled[GU_DEPTH_TEST] == (test || write));
        assert(depth_mask == (write ? GU_FALSE : GU_TRUE));
        if (test || write) assert(depth_func == (test ? GU_GEQUAL : GU_ALWAYS));
    }
    pixel_mask = 0x00ffffff; bind_mask(128);
    assert(recorded_mask == pixel_mask && enabled[GU_STENCIL_TEST] && stencil_ref == 128 && stencil_pass == GU_REPLACE);
    pixel_mask = 0; bind_mask(0); assert(recorded_mask == 0 && !enabled[GU_STENCIL_TEST]);
    Vertex2D in = {0}; GuVertex out;
    in.x = 640; in.y = 448; in.z = 65534; put_vertex(&out, &in);
    assert(out.x == 480 && out.y == 272 && out.z == 65534);
    in.z = 65535; put_vertex(&out, &in); assert(out.z == 65535);
    in.z = -1; put_vertex(&out, &in); assert(out.z == 0);
    in.z = 65536; put_vertex(&out, &in); assert(out.z == 65535);
}
'''
        with tempfile.TemporaryDirectory(dir=root / "build") as temporary:
            path = Path(temporary)
            (path / "check.c").write_text(harness + "\n".join(types + functions) + checks)
            subprocess.run([gcc, "-O2", str(path / "check.c"), "-o", str(path / "check.exe"), "-lm"], check=True, capture_output=True)
            subprocess.run([str(path / "check.exe")], check=True)


if __name__ == "__main__":
    unittest.main()
