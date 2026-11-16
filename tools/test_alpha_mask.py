"""Execute the mask renderer with GE alpha/stencil semantics, not GL alpha writes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


class AlphaMaskTest(unittest.TestCase):
    def test_alpha_bytes_coverage_overlap_and_fallback(self):
        source = r'''
#include <assert.h>
#include <string.h>
enum {GU_TRIANGLES,GU_TRIANGLE_STRIP,GU_TRIANGLE_FAN,GU_STENCIL_TEST,GU_ALPHA_TEST,
      GU_KEEP,GU_REPLACE,GU_ALWAYS,GU_EQUAL};
enum {GU_TEXTURE_32BITF=1,GU_COLOR_8888=2,GU_VERTEX_32BITF=4,GU_TRANSFORM_2D=8,
      RS_ZTEST=6,RS_ZWRITE=8};
typedef struct {float u,v;unsigned colour;float x,y,z;} GuVertex;
static int p4_2d_alpha_masks=1,rtt_target,render_state[64],enabled[16];
static unsigned sky_state[8],pixel_mask=0x00ffffff,bound_alpha_min,bound_alpha_max=255;
static unsigned mask=0x00ffffff,ref,afunc,aref,amask;
static unsigned framebuffer[256],calls,used;
static GuVertex memory[64];
static void sceGuEnable(int s){enabled[s]=1;}
static void sceGuDisable(int s){enabled[s]=0;}
static void sceGuStencilOp(int a,int b,int c){assert(a==GU_KEEP&&b==GU_KEEP&&c==GU_REPLACE);}
static void sceGuStencilFunc(int f,unsigned r,int m){assert(f==GU_ALWAYS&&m==255);ref=r;}
static void sceGuAlphaFunc(unsigned f,unsigned r,unsigned m){afunc=f;aref=r;amask=m;}
static void sceGuPixelMask(unsigned m){mask=m;}
static void *sceGuGetMemory(unsigned n){void *p=memory+used;used+=n/sizeof(GuVertex);assert(used<=64);return p;}
static void sceGuDrawArray(int prim,int type,int n,const void *ix,const void *pv){
    (void)prim;(void)type;assert(!ix&&n>=3);const GuVertex *v=pv;calls++;
    /* The second triangle covers only even pixels, with opposite alpha.
     * Fragment alpha is deliberately independent of stencil REPLACE. */
    for(unsigned x=0;x<256;x++){
        unsigned tri=(unsigned)v[0].x;
        if(tri && (x&1))continue;
        unsigned a=render_state[1] ? (tri?255-x:x) : v[0].colour>>24;
        if(enabled[GU_ALPHA_TEST]&&afunc==GU_EQUAL&&(a&amask)!=(aref&amask))continue;
        assert((mask&0xffffff)==0xffffff); /* RGB stays byte-identical. */
        /* Actual GE: alpha changes ONLY through stencil, not fragment output. */
        if(enabled[GU_STENCIL_TEST]){
            unsigned write=(~mask)>>24;
            unsigned old=framebuffer[x]>>24;
            framebuffer[x]=(framebuffer[x]&0xffffff)|(((old&~write)|(ref&write))<<24);
        }
    }
}
#include "alpha_mask.h"
int main(void){
    GuVertex v[6];memset(v,0,sizeof v);for(int i=0;i<6;i++)v[i].colour=0xffffffff;
    v[3].x=v[4].x=v[5].x=1;
    render_state[1]=1;sky_state[3]=0x717fb;
    for(unsigned x=0;x<256;x++)framebuffer[x]=0xa5123456;
    assert(draw_2d_alpha_mask(GU_TRIANGLES,6,v)&&calls==18);
    for(unsigned x=0;x<256;x++)assert(framebuffer[x]==(((x&1?x:255-x)<<24)|0x123456));
    assert(mask==pixel_mask&&!enabled[GU_ALPHA_TEST]);
    calls=0;for(int i=0;i<6;i++)v[i].colour=0;
    assert(draw_2d_alpha_mask(GU_TRIANGLES,3,v)&&calls==1);
    for(int x=0;x<256;x++)assert(framebuffer[x]==0x123456); /* alpha zero really clears */
    render_state[1]=0;calls=0;for(int i=0;i<6;i++)v[i].colour=0x73000000;
    assert(draw_2d_alpha_mask(GU_TRIANGLES,3,v)&&calls==1);
    for(int x=0;x<256;x++)assert(framebuffer[x]==0x73123456);
    unsigned before=calls;
    render_state[RS_ZTEST]=1;assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));render_state[RS_ZTEST]=0;
    render_state[RS_ZWRITE]=1;assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));render_state[RS_ZWRITE]=0;
    sky_state[3]|=0x4000;assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));sky_state[3]=0x735fb;
    assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));sky_state[3]=0x717fb;
    rtt_target=1;assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));rtt_target=0;
    p4_2d_alpha_masks=0;assert(!draw_2d_alpha_mask(GU_TRIANGLES,3,v));p4_2d_alpha_masks=1;
    assert(calls==before);
    render_state[1]=1;for(int i=0;i<6;i++)v[i].colour=0xffffffff;
    calls=used=0;assert(draw_2d_alpha_mask(GU_TRIANGLE_STRIP,4,v)&&calls==18&&used==6);
    calls=used=0;assert(draw_2d_alpha_mask(GU_TRIANGLE_FAN,4,v)&&calls==18&&used==6);
    return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-mask-test-') as name:
            folder = Path(name)
            c, exe = folder/'check.c', folder/'check.exe'
            c.write_text(source)
            cc = shutil.which('gcc') or shutil.which('clang')
            if cc:
                command = [cc, '-std=c11', '-O2', f'-I{ROOT / "port"}', str(c), '-o', str(exe)]
            else:
                vs = Path(os.environ.get('ProgramFiles', 'C:/Program Files'))/'Microsoft Visual Studio'
                compilers = sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)'))/'Windows Kits/10'
                versions = sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:
                    self.skipTest('host C compiler unavailable')
                compiler, headers = compilers[-1], versions[-1]
                vc = compiler.parents[3]
                command = [str(compiler), '/nologo', '/std:c11', '/O2', f'/I{ROOT / "port"}',
                           f'/I{vc / "include"}', f'/I{headers / "ucrt"}', str(c),
                           f'/Fe:{exe}', f'/Fo:{folder / "check.obj"}', '/link',
                           f'/LIBPATH:{vc / "lib/x64"}',
                           f'/LIBPATH:{kits / "Lib" / headers.name / "ucrt/x64"}',
                           f'/LIBPATH:{kits / "Lib" / headers.name / "um/x64"}']
            result = subprocess.run(command, cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == '__main__':
    unittest.main()
