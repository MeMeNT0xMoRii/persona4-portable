"""Exercise the screen-buffer decisions with synthetic PSP display state."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class DisplayHandoff(unittest.TestCase):
    def test_movie_return_and_empty_serial_frames(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("host gcc is unavailable")
        renderer = (Path(__file__).resolve().parents[1] / "port/rwsky.c").read_text()
        resume = re.search(r"^void p4_video_renderer_end\(void\)\n\{.*?^\}", renderer, re.M | re.S)[0]
        serial = renderer.split("index_cap = POOL_INDICES / 2;\n    } else {\n", 1)[1].split("        flip_seq = shown_seq", 1)[0]
        harness = r'''
#include <assert.h>
#include <stdint.h>

enum { STRIDE = 512, SCREEN_H = 272, PSP_DISPLAY_PIXEL_FORMAT_8888 = 3, PSP_DISPLAY_SETBUF_IMMEDIATE = 0 };
enum { VRAM = 0x04000000, BANK_SIZE = STRIDE * SCREEN_H * 4 };
static unsigned fps_window_valid, fps_frames, fps_tenths, skip_frame, skip_run;
static unsigned skip_debt_us, skip_last_us, feedback_captured, texture_generation, dirties;
static void *draw_buffer;
static uintptr_t front_address;
static int front_stride = STRIDE, front_format = PSP_DISPLAY_PIXEL_FORMAT_8888;
static int query_error, present_error, presentations, fps_markers;

static int sceDisplayGetFrameBuf(void **front, int *stride, int *format, int sync) {
    assert(sync == PSP_DISPLAY_SETBUF_IMMEDIATE);
    *front = (void *)front_address;
    *stride = front_stride;
    *format = front_format;
    return query_error;
}
static int sceDisplaySetFrameBuf(void *front, int stride, int format, int sync) {
    assert(stride == STRIDE && format == PSP_DISPLAY_PIXEL_FORMAT_8888);
    assert(sync == PSP_DISPLAY_SETBUF_IMMEDIATE);
    presentations++;
    if (!present_error) front_address = (uintptr_t)front;
    return present_error;
}
static void fps_presented(void) { fps_markers++; }
static void gu_dirty(void) { dirties++; }
static unsigned sceKernelGetSystemTimeLow(void) { return 10; }
''' + resume + r'''
static void present_serial(int present_new_frame) {
    void *other = draw_buffer ? (void *)0 : (void *)BANK_SIZE;
''' + serial + r'''
}
int main(void) {
    for (int shown = 0; shown < 6; shown++) {
        for (int prior_bank = 0; prior_bank < 2; prior_bank++) {
            int movie_bank = shown & 1;
            front_address = VRAM + movie_bank * BANK_SIZE;
            draw_buffer = (void *)(uintptr_t)(prior_bank * BANK_SIZE);
            fps_tenths = skip_frame = feedback_captured = 1;
            p4_video_renderer_end();
            assert((uintptr_t)draw_buffer == (1 - movie_bank) * BANK_SIZE);
            assert(!fps_tenths && !skip_frame && !feedback_captured && skip_last_us == 10);
        }
    }
    assert(texture_generation == 12 && dirties == 12 && presentations == 0);
    front_address = VRAM | 0x40000000u;
    p4_video_renderer_end();
    assert((uintptr_t)draw_buffer == BANK_SIZE);
    front_address = VRAM + BANK_SIZE;
    query_error = -1;
    p4_video_renderer_end();
    assert((uintptr_t)draw_buffer == BANK_SIZE);
    query_error = 0;
    front_stride = 256;
    p4_video_renderer_end();
    assert((uintptr_t)draw_buffer == BANK_SIZE);
    front_stride = STRIDE;
    front_address = VRAM;
    present_serial(0);
    assert(presentations == 0 && !fps_markers && (uintptr_t)draw_buffer == BANK_SIZE);
    present_error = -1;
    present_serial(1);
    assert(presentations == 1 && !fps_markers && (uintptr_t)draw_buffer == BANK_SIZE);
    present_error = 0;
    present_serial(1);
    assert(presentations == 2 && fps_markers == 1 && !draw_buffer && front_address == VRAM + BANK_SIZE);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "check.c"
            binary = source.with_suffix("")
            source.write_text(harness)
            subprocess.run([gcc, "-O2", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
