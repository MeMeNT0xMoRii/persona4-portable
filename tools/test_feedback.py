"""Compile the actual sprite conversion and check coverage and safe fallbacks."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class FeedbackSprites(unittest.TestCase):
    def test_rectangles_preserve_uv_colour_depth_and_reject_other_geometry(self):
        source = r'''
#include <assert.h>
#include <math.h>
typedef struct { float u,v; unsigned colour; float x,y,z; } GuVertex;
#include "feedback.h"
static GuVertex v[6];
static void rectangle(float left, float right) {
    GuVertex c[4] = {
        {3,7,0x6ecde1ff,left,0,123}, {483,7,0x6ecde1ff,right,0,123},
        {3,279,0x6ecde1ff,left,272,123}, {483,279,0x6ecde1ff,right,272,123}
    };
    const int order[6] = {0,2,3,0,3,1};
    for(int i=0;i<6;i++) v[i]=c[order[i]];
}
int main(void) {
    P4FeedbackRect r;
    struct { GuVertex out[P4_FEEDBACK_MAX_VERTICES]; unsigned guard; } b;
    b.guard=0x12345678;
    rectangle(0,480);
    assert(p4_feedback_rect(v,6,&r));
    int n=p4_feedback_emit(b.out,&r);
    assert(n==30 && b.guard==0x12345678);
    for(int i=0;i<n;i+=2) {
        assert(b.out[i].x==16*i && b.out[i+1].x==16*i+32);
        assert(b.out[i].y==0 && b.out[i+1].y==272);
        assert(b.out[i].u==b.out[i].x+3 && b.out[i+1].u==b.out[i+1].x+3);
        assert(b.out[i].v==7 && b.out[i+1].v==279);
        assert(b.out[i].colour==v[0].colour && b.out[i+1].colour==v[0].colour);
        assert(b.out[i].z==123 && b.out[i+1].z==123);
        if(i) assert(b.out[i-1].x==b.out[i].x && b.out[i-1].u==b.out[i].u);
    }
    rectangle(5.5f,77);
    assert(p4_feedback_rect(v,6,&r)); n=p4_feedback_emit(b.out,&r);
    assert(n==6 && b.out[0].x==5.5f && b.out[5].x==77 && b.out[5].u==483);
    assert(b.guard==0x12345678);
    /* Reversed U and V remain separable and preserve their directions. */
    for(int i=0;i<6;i++) { v[i].u=500-v[i].u; v[i].v=300-v[i].v; }
    assert(p4_feedback_rect(v,6,&r)); n=p4_feedback_emit(b.out,&r);
    assert(b.out[0].u==497 && b.out[n-1].u==17 && b.out[0].v==293 && b.out[1].v==21);
    rectangle(0,480); v[1].colour++; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[1].z++; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[4].u++; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[1].u=5; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[2].x=470; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[0].x=NAN; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[0].u=INFINITY; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,481); assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,0); assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); assert(!p4_feedback_rect(v,4,&r));
    rectangle(0,480); v[1]=v[0]; assert(!p4_feedback_rect(v,6,&r));
    rectangle(0,480); v[3]=v[0]; v[4]=v[2]; v[5]=v[1]; assert(!p4_feedback_rect(v,6,&r));
    /* The other rectangle diagonal is valid too. */
    rectangle(0,480); GuVertex tl=v[0],bl=v[1],br=v[2],tr=v[5];
    v[0]=tl;v[1]=bl;v[2]=tr;v[3]=bl;v[4]=br;v[5]=tr;
    assert(p4_feedback_rect(v,6,&r));
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="p4-feedback-test-") as folder:
            folder = Path(folder)
            c = folder / "check.c"
            exe = folder / "check.exe"
            c.write_text(source)
            gcc = shutil.which("gcc")
            if gcc:
                command = [gcc, "-O2", f"-I{ROOT / 'port'}", str(c), "-o", str(exe)]
            else:
                vs = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
                compilers = sorted(vs.glob("*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe"))
                kits = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Windows Kits/10"
                versions = sorted((kits / "Include").glob("10.*"))
                if not compilers or not versions:
                    self.skipTest("host C compiler is unavailable")
                compiler, headers = compilers[-1], versions[-1]
                vc = compiler.parents[3]
                command = [str(compiler), "/nologo", "/std:c11", "/O2", f"/I{ROOT / 'port'}",
                           f"/I{vc / 'include'}", f"/I{headers / 'ucrt'}", str(c),
                           f"/Fe:{exe}", f"/Fo:{folder / 'check.obj'}", "/link",
                           f"/LIBPATH:{vc / 'lib/x64'}", f"/LIBPATH:{kits / 'Lib' / headers.name / 'ucrt/x64'}",
                           f"/LIBPATH:{kits / 'Lib' / headers.name / 'um/x64'}"]
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
