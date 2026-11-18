"""Inject allocation failures at the RenderWare driver's create and lock entry points."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class RasterMemory(unittest.TestCase):
    def test_generated_mip_chain_requires_source_flag(self):
        cc = shutil.which("gcc")
        if not cc:
            self.skipTest("C compiler required")
        root = Path(__file__).resolve().parents[1]
        source = (root / "port/rwsky.c").read_text()
        body = re.search(r"^static int mip_levels\([^\n]+\)\n\{.*?^\}", source, re.M | re.S)[0]
        program = """
#include <assert.h>
typedef struct { int width, height, depth, cFormat; } Raster;
enum { MAX_MIPS = 8 };
static int p4_mip_build = 1, p4_mip_min = 64;
""" + body + """
int main(void) {
    Raster r = {128, 128, 8, 0x25};
    assert(mip_levels(&r) == 1);
    r.cFormat |= 0x80;
    assert(mip_levels(&r) == 4);
    r.width = 127;
    assert(mip_levels(&r) == 1);
    r.width = 32;
    assert(mip_levels(&r) == 1);
    r.width = 128;
    p4_mip_build = 0;
    assert(mip_levels(&r) == 1);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "mip-test"
            subprocess.run([cc, "-O2", "-x", "c", "-", "-o", exe], input=program, text=True, check=True)
            subprocess.run([exe], check=True)

    def test_partial_create_and_failed_cpu_lock(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("C compiler required")
        root = Path(__file__).resolve().parents[1]
        source = (root / "port/rwsky.c").read_text()
        names = ("func_003f98a0", "unswizzle_raster", "func_003fc5f0")
        bodies = [re.search(rf"^(?:static )?int {name}\([^\n]+\)\n\{{.*?^\}}",
                            source, re.S | re.M)[0] for name in names]
        stub = r"""
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include "swizzle.h"
typedef struct { unsigned char *originalPixels, *cpPixels, *palette;
                 int width, height, depth, stride, cType, cFlags, privateFlags; } Raster;
typedef struct { int wbytes, rows; } SwizzleHead;
enum { SWIZZLE_HEAD=64, TYPE_MASK=7, TYPE_CAMERA=2, TYPE_ZBUFFER=1, DONT_ALLOCATE=128,
       RASTER_LOCK_READ=2, RASTER_LOCK_WRITE=1, PIXELS_LOCKED_READ=2, PIXELS_LOCKED_WRITE=4 };
static unsigned texture_generation;
static int calls, fail, live, waits;
static unsigned char *marked;
static SwizzleHead head = {32, 8};
static const SwizzleHead *swizzle_head(const unsigned char *p) { return p && p==marked ? &head : NULL; }
static void *allocate(int align, size_t n) {
    if (++calls == fail) return NULL;
    void *p = calloc(1, n);
    assert(p); live++; return p;
}
static void release(void *p) { if (p) { live--; free(p); } }
#define free release
#define memalign allocate
#define p4_log(...) ((void)0)
#define p4_backtrace() ((void)0)
#define p4r_003f7a70(o,r,f) 1
static void pipe_texture_wait(void) { waits++; }
"""
        checks = r"""
int main(void) {
    Raster r = {.width=8, .height=8, .depth=8, .cType=4};
    for (fail=1; fail<=2; fail++) {
        calls=0;
        assert(!func_003f98a0(NULL,&r,0));
        assert(!r.originalPixels && !r.palette && !r.cpPixels && !live && calls==fail);
    }
    calls=fail=0;
    assert(func_003f98a0(NULL,&r,0) && r.palette && live==2);
    free(r.originalPixels); free(r.palette);
    r.depth=32;
    assert(func_003f98a0(NULL,&r,0) && !r.palette && live==1);
    free(r.originalPixels);
    unsigned char rows[256];
    for (int i=0;i<256;i++) rows[i]=(unsigned char)(i*17);
    marked=(unsigned char *)allocate(64,64+256)+64;
    r.originalPixels=marked;
    ge_swizzle(marked,rows,32,8,32,8,1);
    fail=calls+1;
    unsigned char *out=rows;
    assert(!func_003fc5f0(&out,&r,RASTER_LOCK_WRITE));
    assert(!out && !r.cpPixels && !r.privateFlags && r.originalPixels==marked && live==1 && !texture_generation);
    assert(func_003fc5f0(&out,&r,RASTER_LOCK_READ));
    assert(out==r.originalPixels && out!=marked && !memcmp(out,rows,256));
    assert(live==1 && r.privateFlags==PIXELS_LOCKED_READ && waits==2 && texture_generation==1);
    free(out);
    assert(live==0);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "raster"
            subprocess.run([cc, "-O2", "-I", str(root / "port"), "-x", "c", "-", "-o", exe],
                           input=stub + "\n".join(bodies) + checks, text=True, check=True)
            subprocess.run([exe], check=True)
