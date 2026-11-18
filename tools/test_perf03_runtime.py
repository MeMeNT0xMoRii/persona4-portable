"""Exercise the port's retail index reader and the animation-frame validity predicate."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeRegression(unittest.TestCase):
    def compile_run(self, body):
        cc = shutil.which("gcc") or shutil.which("clang")
        if not cc:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp / "pspkernel.h").touch()
            (tmp / "check.c").write_text(body)
            exe = tmp / "check.exe"
            subprocess.run([cc, "-O2", "-I", str(tmp), "-I", str(ROOT / "port"),
                            str(tmp / "check.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_disc_reads(self):
        self.compile_run(r"""
#include <assert.h>
#include <string.h>
#include <stdlib.h>
typedef int SceUID;
enum { PSP_O_RDONLY, PSP_SEEK_SET, PSP_SEEK_END };
static int truncated, missing, opens, part, length = 12;
static unsigned record[3];
char p4_game_dir[160];
static int sceIoOpen(const char *path, int flags, int perm) {
    if (!strcmp(path, "p4files.idx")) return 9;
    opens++;
    if (!strcmp(path,"DATA.0")) return 0;
    if (!strcmp(path,"DATA.1") && !missing) return 1;
    return -1;
}
static int sceIoLseek(int fd, int offset, int whence) {
    return whence == PSP_SEEK_END ? length : offset;
}
static int sceIoRead(int fd, void *p, int size) {
    if(fd==9) {memcpy(p,record,size);return truncated ? size-1 : size;}
    memset(p,fd+1,size);return size;
}
static int sceIoClose(int fd){return 0;}
static int sceKernelCreateSema(const char*n,int a,int b,int c,void*d){return 1;}
static int sceKernelWaitSema(int a,int b,void*c){return 0;}
static int sceKernelSignalSema(int a,int b){return 0;}
static void sceKernelDcacheWritebackRange(void*p,int n){}
void p4_log(const char *fmt,...){}
void p4_backtrace(void){}
unsigned char *__real_sdkSpriteRequestLoad(const char*n){return 0;}
#include "files.c"
int main(void){
    record[0]=path_hash("","FILE"); record[1]=((PART/SECTOR-1)<<3);record[2]=4096;
    length=11;assert(!func_004c85a0("FILE",0) && !rows);
    length=12;truncated=1;assert(!func_004c85a0("FILE",0) && !rows);
    truncated=0;Handle*h=func_004c85a0("FILE",0);assert(h && opens==0);
    char data[4096];missing=1;assert(!func_004c9010(h,2,data) && h->stat==4);
    missing=0;assert(func_004c9010(h,2,data)==2 && h->stat==3 && opens==3);
    assert(data[0]==1 && data[2047]==1 && data[2048]==2 && data[4095]==2);
    assert(func_004c9010(h,2,data)==2 && opens==3);
    rows[0].where |= 7;assert(!func_004c85a0("FILE",0));
}
""")

    def test_pose_bounds(self):
        src = (ROOT / "port/rwsky.c").read_text()
        start = src.index("static inline int hanim_bad(")
        function = src[start:src.index("\n}", start) + 2]
        self.compile_run("#include <stdint.h>\n#include <string.h>\n#include <assert.h>\n" + function + r"""
int main(void) {
    uint32_t x=1, cases[]={0,0x80000000,1,0x7149f2c9,0x7149f2ca,0xf149f2c9,
      0xf149f2ca,0x7f800000,0xff800000,0x7fc00123,0xff800001};
    for(int i=0;i<100000;i++) {
        x=x*1664525u+1013904223u;
        uint32_t bits=i<sizeof(cases)/sizeof(*cases)?cases[i]:x;
        float f[7]={0}; memcpy(&f[i%7],&bits,4);
        float v=f[i%7];
        assert(hanim_bad(f,7)==!(v<1e30f && v>-1e30f));
    }
}
""")
