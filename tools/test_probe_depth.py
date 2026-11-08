"""Host checks of the actual depth-probe function; run in an MSVC dev shell."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class ProbeDepth(unittest.TestCase):
    def test_current_indexed_depth_and_crossing_meshes(self):
        compiler = shutil.which('cl')
        if not compiler:
            self.skipTest('MSVC developer shell is required')
        source = (Path(__file__).resolve().parents[1] / 'port/rwsky.c').read_text()
        function = re.search(r'^static int probe_depth_reject\([^\n]+\)\n\{.*?^\}', source, re.M | re.S)[0]
        harness = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef struct { float x,y,z; } V3;
typedef struct { V3 right; unsigned flags; V3 up; unsigned pad1; V3 at; unsigned pad2; V3 pos; unsigned pad3; } Matrix;
typedef struct { unsigned short *indices; int numIndices; void *material; } Mesh;
enum { CAMERA_VIEW = 0 };
static void *current_camera;
static int p4_probe_depth_mode;
static float p4_probe_depth_cut = 2000;
static unsigned probe_depth_bins[6], probe_depth_meshes, probe_depth_hidden, probe_depth_indices, probe_depth_us;
static unsigned sceKernelGetSystemTimeLow(void) { static unsigned t; return ++t; }
'''
        checks = r'''
int main(void) {
    Matrix view = {0}, model = {0};
    view.right.x=view.up.y=view.at.z=1;
    model.right.x=model.up.y=model.at.z=1;
    current_camera=&view;
    V3 p[3] = {{0,0,3000},{0,0,4000},{0,0,5000}};
    unsigned short ix[3]={0,1,2}; Mesh mesh={ix,3,0};
    p4_probe_depth_mode=0;
    assert(!probe_depth_reject(&mesh,p,3,0) && probe_depth_meshes==0);
    p4_probe_depth_mode=1;
    assert(!probe_depth_reject(&mesh,p,3,0));
    assert(probe_depth_bins[3]==1 && probe_depth_hidden==0);
    p4_probe_depth_mode=2;
    assert(probe_depth_reject(&mesh,p,3,0));
    p[0].z=1000; /* a crossing mesh must remain in both modes */
    assert(!probe_depth_reject(&mesh,p,3,0));
    p4_probe_depth_mode=3;
    assert(!probe_depth_reject(&mesh,p,3,0));
    p[1].z=500; p[2].z=1500;
    assert(probe_depth_reject(&mesh,p,3,0));
    model.pos.z=10000; /* transformed vertices are far despite local positions */
    assert(!probe_depth_reject(&mesh,p,3,&model));
    p4_probe_depth_mode=2;
    assert(probe_depth_reject(&mesh,p,3,&model));
    view.pos.z=-10000; /* camera translation cancels the model translation */
    assert(!probe_depth_reject(&mesh,p,3,&model));
    view.pos.z=0;
    /* Rotate local X to view Z, with nonuniform scale. */
    model.pos.z=0; model.right.x=0; model.right.z=2; model.at.z=0;
    p[0].x=p[1].x=p[2].x=1500;
    assert(probe_depth_reject(&mesh,p,3,&model));
    p[0].x=NAN;
    assert(!probe_depth_reject(&mesh,p,3,&model));
    p[0].x=1500; ix[2]=3;
    assert(!probe_depth_reject(&mesh,p,3,&model));
    mesh.numIndices=0;
    assert(!probe_depth_reject(&mesh,p,3,&model));
    puts("depth probe checks passed");
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='p3f-depth-test-') as temporary:
            directory = Path(temporary)
            code = directory / 'check.c'
            code.write_text(harness + function + checks)
            exe = directory / 'check.exe'
            result = subprocess.run([compiler, '/nologo', '/O2', str(code), '/Fe:' + str(exe),
                                     '/Fo:' + str(directory / 'check.obj')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
