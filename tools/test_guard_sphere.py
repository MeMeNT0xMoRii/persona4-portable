"""Exercise the PSP guard-band shortcut with transformed model-space bounds."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class GuardSphereTest(unittest.TestCase):
    def test_sheared_sphere_with_a_visible_triangle_requires_clipping(self):
        cc = shutil.which("gcc")
        if not cc:
            self.skipTest("host gcc is unavailable")

        root = Path(__file__).resolve().parents[1]
        source = (root / "port/rwsky.c").read_text()
        types = re.findall(r"^typedef struct \{[^\n]+} (?:V3|Matrix);", source, re.M)
        guard = re.search(r"^static int guard_sphere_world\([^;]*?\n\{.*?^\}", source, re.M | re.S)[0]
        harness = r'''
#include <assert.h>
#include <math.h>
#define GUARD 8.0f
static int p4_guard_gnear = 1;
static struct { float w[5][4], len[5]; } guard_world;
'''
        cases = r'''
int main(void)
{
    /* Projection fixture with clip W = world Z. */
    const float planes[5][4] = {
        {0, 0, 1, 0}, {-1, 0, GUARD, 0}, {1, 0, GUARD, 0},
        {0, -1, GUARD, 0}, {0, 1, GUARD, 0},
    };
    for (int i = 0; i < 5; ++i) {
        const float *p = planes[i];
        for (int j = 0; j < 4; ++j)
            guard_world.w[i][j] = p[j];
        guard_world.len[i] = sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
    }

    const float sphere[4] = {0, 0, 0, 1};
    const Matrix model = {
        .right = {.2f, 0, 1}, .up = {0, .2f, 1}, .at = {0, 0, 1},
        .pos = {0, 0, 1.7f},
    };
    const V3 corner = {-.576f, -.576f, -.54f};
    assert(corner.x*corner.x + corner.y*corner.y + corner.z*corner.z < 1);
    const float clip_x = corner.x * model.right.x;
    const float clip_w = corner.x + corner.y + corner.z + model.pos.z;
    /* A triangle can connect this corner to inside vertices (0,0,0) and
       (.5,0,0). It intersects the screen, but the corner exceeds the
       hardware raster window: 2048 px from the viewport center. */
    assert(clip_w > .001f && -clip_x / clip_w * 240 > 2048);
    assert(!guard_sphere_world(&model, sphere, .001f));

    Matrix distant = model;
    distant.pos.z = 20;
    assert(guard_sphere_world(&distant, sphere, .001f));

    const Matrix orthogonal = {
        .right = {0, .5f, 0}, .up = {-1, 0, 0}, .at = {0, 0, .75f},
        .pos = {0, 0, 2},
    };
    assert(guard_sphere_world(&orthogonal, sphere, .001f));
    const float world_sphere[4] = {0, 0, 2, 1};
    assert(guard_sphere_world(0, world_sphere, .001f));
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c_file = Path(tmp) / "guard_sphere.c"
            exe = Path(tmp) / "guard_sphere"
            c_file.write_text("\n".join([*types, harness, guard, cases]))
            subprocess.run([cc, "-std=c99", "-O2", str(c_file), "-lm", "-o", str(exe)], check=True)
            subprocess.run([str(exe)], cwd=tmp, check=True)


if __name__ == "__main__":
    unittest.main()
