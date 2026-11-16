"""Exercise the actual particle renderer across fixed-pool/spill boundaries."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parent.parent

def definition(s,name):
    m=re.search(r'^static [^\n;]*\b'+name+r'\([^;]*?\)\n\{',s,re.M)
    if not m:raise ValueError(name)
    a=m.start();i=s.index('{',a)+1;depth=1
    while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[a:i]

class PtankStreaming(unittest.TestCase):
    def test_actual_quads_stream_in_order_and_retire_after_ge(self):
        s=(ROOT/'port/rwsky.c').read_text();types=s[s.index('typedef struct { float x, y, z; } V3;'):s.index('\nenum { CAMERA_VIEW')]
        enums=s[s.index('enum {\n    PT_ACTIVE'):s.index('/* RpPTank\'s PS2 register setter')]
        spill=s[s.index('typedef struct PtankSpill'):s.index('static void ptank_spill_collect(int all)\n{')]
        c=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)
#define memalign(a,b) malloc(b)
typedef struct {float u,v;unsigned colour;float x,y,z;} Vertex3D;
'''+types+enums+r'''
enum {GU_TRIANGLES,GU_TEXTURE_32BITF,GU_COLOR_8888,GU_VERTEX_32BITF,GU_TRANSFORM_3D,GU_CLIP_PLANES,GU_ALPHA_TEST,GU_GREATER,GU_BLEND,GU_ADD,GU_CLAMP,GU_MODEL};
enum {CAMERA_VIEW=0x20,CAMERA_NEAR=0x80};
static unsigned cache_frame=100;static int cap=12,vertex_used,ptanks_drawn,particles_drawn;
static Vertex3D pool[64],*vertex_pool=pool;
#define MAX_VERTICES cap
static char camera[512];static void *current_camera=camera;
static Matrix identity;
static int total,batches;
void p4_log(const char *fmt,...) {}
void sceKernelDcacheWritebackRange(const void *p,int n) {}
void sceGuDrawArray(int prim,int vt,int count,const void *idx,const void *verts) {
    const Vertex3D *v=verts;assert(count%6==0);
    for(int i=0;i<count;i+=6) {float x=0;for(int j=0;j<6;j++)x+=v[i+j].x;assert(fabsf(x/6-total)<.001f);total++;}
    batches++;
}
void gu_dirty(void){}int in_ram(const void *p){return 1;}void begin_frame(void){}void bind_depth(void){}
void sceGuEnable(int x){}void bind_mask(unsigned x){}void sceGuAlphaFunc(int a,int b,int c){}void set_blend(unsigned x){}
int gu_factor(int a,unsigned *b){return 0;}void sceGuBlendFunc(int a,int b,int c,unsigned d,unsigned e){}
void sceGuTexScale(float x,float y){}void sceGuTexOffset(float x,float y){}void set_camera(void){}
typedef Matrix ScePspFMatrix4;void gumLoadIdentity(Matrix *m){*m=identity;}void sceGuSetMatrix(int t,const Matrix *m){}
Matrix *p4r_003e9700(const void *p){return &identity;}void bind_raster(void *p,int wrap){}
V3 transform(const V3 *v,const Matrix *m){return *v;}unsigned modulate_colour(unsigned a,unsigned b){return a;}
#include "clip.h"
'''+spill+'\n'.join(definition(s,n) for n in ['ptank_spill_collect','ptank_spill_alloc','ptank_submit','draw_ptank_body'])+r'''
int main(void) {
    identity.right.x=identity.up.y=identity.at.z=1;memcpy(camera+CAMERA_VIEW,&identity,sizeof identity);*(float*)(camera+CAMERA_NEAR)=1;
    char tank[512]={0},frame[64]={0};int count=1200;
    V3 *pos=malloc(5000*sizeof *pos);for(int i=0;i<5000;i++)pos[i]=(V3){i,0,10};
    *(int*)(tank+PT_ACTIVE)=count;*(unsigned*)(tank+PT_DATAFLAGS)=PD_POSITION;
    Cluster *clusters=(Cluster*)(tank+PT_CLUSTERS);clusters[0]=(Cluster){(unsigned char*)pos,sizeof *pos};
    ((float*)(tank+PT_CSIZE))[0]=((float*)(tank+PT_CSIZE))[1]=1;*(unsigned*)(tank+PT_CCOLOR)=0xffffffff;
    draw_ptank_body(0,tank,0,frame);
    assert(total==count && batches>1 && vertex_used==12 && ptank_spill_batches>0 && !ptank_spill_refused);
    unsigned bytes=ptank_spill_bytes;assert(bytes>0);
    cache_frame=104;ptank_spill_collect(0);assert(ptank_spill_bytes==bytes);
    cache_frame=105;ptank_spill_collect(0);assert(!ptank_spills && !ptank_spill_bytes);
    total=batches=vertex_used=0;cap=12;p4_ptank_spill_budget=0;
    draw_ptank_body(0,tank,0,frame);assert(total==2 && ptank_spill_refused==1 && !ptank_spills);
    total=batches=vertex_used=0;cap=0;p4_ptank_spill_budget=2<<20;
    *(int*)(tank+PT_ACTIVE)=5000;for(int i=0;i<5000;i++)pos[i].z=-10;
    draw_ptank_body(0,tank,0,frame);assert(!total && !batches && !ptank_spills);
    free(pos);puts("particle streaming, output order, true capacity, spill budget and GE-safe retirement OK");
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-ptank-test-') as name:
            folder=Path(name);src=folder/'check.c';exe=folder/'check.exe';src.write_text(c)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:cmd=[cc,'-std=c11','-O2',f'-I{ROOT/"port"}',str(src),'-o',str(exe),'-lm']
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio';compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10';versions=sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:self.skipTest('host compiler unavailable')
                compiler,headers=compilers[-1],versions[-1];vc=compiler.parents[3]
                cmd=[str(compiler),'/nologo','/std:c11','/O2',f'/I{ROOT/"port"}',f'/I{vc/"include"}',f'/I{headers/"ucrt"}',str(src),f'/Fe:{exe}',f'/Fo:{folder/"check.obj"}','/link',f'/LIBPATH:{vc/"lib/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"ucrt/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"um/x64"}']
            r=subprocess.run(cmd,cwd=folder,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
