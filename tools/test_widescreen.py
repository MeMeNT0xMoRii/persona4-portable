"""Check the actual widescreen font arithmetic at both ends of the 2D screen."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_census import DECOMP, shim


class Widescreen(unittest.TestCase):
    def test_cri_logo_keeps_original_active_rectangle(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        core = Path(__file__).resolve().parents[1] / 'port/rwsky.c'
        source = core.read_text()
        definitions = re.findall(r'^#define SCREEN_[WH] \d+$', source, re.M)
        definitions += re.findall(r'^typedef struct \{ [^\n]+ \} (?:V3|Matrix);$', source, re.M)
        logo = re.search(r'^void p4_draw_cri_logo\(.*?^\}', source, re.M | re.S)[0]
        harness = '\n'.join(definitions) + '\n' + logo + r'''
#include <assert.h>
#include <math.h>

int sflDataDrawTexturedQuad(int x, int y, float z, int width, int height, int rgb,
                          int alpha, int mode, short center_x, short center_y,
                          Matrix *matrix, int texture, float (*uv)[2]) {
    assert(x == 0 && y == 0 && z == 0 && center_x == 0 && center_y == 0);
    assert(width == 640 && height == 448 && rgb == 0xffffff && alpha == 255 && mode == 1);
    assert(texture == 123 && matrix->flags == 0);
    float left = matrix->pos.x * SCREEN_W / width;
    float right = (matrix->pos.x + matrix->right.x * width) * SCREEN_W / width;
    assert(fabsf(right - left - 4.0f * SCREEN_H / 3.0f) < .0001f);
    assert(fabsf(left + right - SCREEN_W) < .0001f);
    assert(matrix->up.y == 1 && matrix->at.z == 1 && matrix->pos.y == 0);
    for (int corner = 0; corner < 4; ++corner) {
        assert(uv[corner][0] * 1024 == ((corner & 1) ? width : 0));
        assert(uv[corner][1] * 512 == ((corner & 2) ? height : 0));
    }
    return 1;
}

int main(void) {
    p4_draw_cri_logo(123);
}
'''
        rel = 'src/promoted/code1_0012.c'
        retail = (DECOMP / rel).read_text()
        adapted = shim(retail, rel)
        self.assertIn('p4_draw_cri_logo(fullImage);', adapted)
        atlus = next(line for line in retail.splitlines() if 'insetImage, 0);' in line)
        self.assertIn(atlus, adapted)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness)
            subprocess.run([gcc, '-std=c99', '-O2', str(path / 'check.c'), '-o', str(path / 'check')], check=True)
            subprocess.run([str(path / 'check')], check=True)

    def test_font_anchor_width_and_advance(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        def source(rel):
            return shim((DECOMP / rel).read_text(), rel)
        font = source('src/frFont.c')
        raster = source('src/frFontRaster.c')
        anchor = re.search(r'x = \(\(\(FrFontTextRun \*\)node\)->x[^;]+;', font)[0]
        append = re.search(r'\(\(FrFontTextRun \*\)list\)->x = \(\(FrFontTextRun \*\)tail\)->x[^;]+;', font)[0]
        origin = re.search(r'originX = [^;]+;', raster)[0]
        width = re.search(r'extentX = [^;]+;', raster)[0]
        harness = r'''
#include <assert.h>
#include <math.h>
typedef float f32;
typedef unsigned u32;
typedef struct { int x, width; } FrFontTextRun;
static float position(int anchor, int advance) {
    FrFontTextRun run = {anchor};
    void *node = &run;
    int x;
    float originX;
''' + anchor + '\nx += advance * 16;\n' + origin + r'''
    return originX;
}
static void append_run(FrFontTextRun *tail, FrFontTextRun *list) {
''' + append + r'''
}
int main(void) {
    for (int x = -100; x <= 640; x += 20)
        assert(fabsf(position(x * 16, 0) - x) < .0001f);
    assert(fabsf(position(640 * 16, 21) - 656) < .0001f);
    for (int x = -100; x <= 100; x += 20) {
        FrFontTextRun first = {x * 16, 57}, second = {0, 93}, third = {0};
        append_run(&first, &second);
        append_run(&second, &third);
        assert(fabsf(position(second.x, 0) - position(first.x, first.width)) < 0.12f);
        assert(fabsf(position(third.x, 0) - position(first.x, first.width + second.width)) < 0.18f);
    }
    unsigned width = 32;
    float extentX;
''' + width + r'''
    assert(extentX == 24);
}
'''
        root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness)
            subprocess.run([gcc, '-std=c99', '-O2', str(path / 'check.c'), '-o', str(path / 'check.exe')], check=True, capture_output=True)
            subprocess.run([str(path / 'check.exe')], check=True)


if __name__ == '__main__':
    unittest.main()
