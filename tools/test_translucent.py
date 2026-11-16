"""Run the real late-pass implementation and mask binder against a GE recorder."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent.parent


class TranslucentTest(unittest.TestCase):
    def test_masks_order_lifetime_and_state(self):
        renderer=(ROOT/'port/rwsky.c').read_text()
        binder=re.search(r'^static void bind_mask_mode\([^\n]*\)\n\{.*?^\}',renderer,re.M|re.S)[0]
        source=r'''
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "alpha_policy.h"
enum {MAX_STATE=64,RS_ZTEST=6,RS_ZWRITE=8,CAMERA_VIEW=0};
enum {GU_TRIANGLES=3,GU_TRIANGLE_STRIP=4,GU_TRIANGLE_FAN=5,GU_INDEX_BITS=0x1800,GU_INDEX_16BIT=0x1000,VT_3D=0x119f};
enum {GU_VIEW,GU_PROJECTION,GU_LIGHTING,GU_REPEAT,GU_ALWAYS,GU_TRUE,GU_STENCIL_TEST,GU_KEEP,GU_REPLACE};
typedef struct {float x,y,z;} V3;
typedef struct {V3 right;unsigned flags;V3 up;unsigned p1;V3 at;unsigned p2;V3 pos;unsigned p3;} Matrix;
typedef struct {float m[16];} ScePspFMatrix4;
typedef struct {float u,v;unsigned colour;float x,y,z;} Vertex3D;
typedef struct {int dummy;} Raster;
typedef struct {void *texture;unsigned colour;} Material;
static int p4_translucent_late=1,in_frame=1,draw_material_ge,draw_alpha_known=1;
static unsigned cache_frame=1,pixel_mask,sky_state[8],bound_alpha_max=255;
static int render_state[MAX_STATE];
static Matrix translucent_model,camera;
static void *current_camera=&camera;
static unsigned test_hi=153;
static int enabled[16],stencil_ref,stencil_pass,date;
static unsigned recorded_mask;
static int gu_mesh_mode;
static struct {int stencil_known;unsigned stencil_ref;} gs;
static int date_emulated(void){return date;}
static void st_pixel_mask(unsigned m){recorded_mask=m;}
static void st_enable(int s,int on){enabled[s]=on;}
static void sceGuStencilFunc(int fn,int ref,int mask){assert(fn==GU_ALWAYS&&mask==255);stencil_ref=ref;}
static void sceGuStencilOp(int a,int b,int c){assert(a==GU_KEEP&&b==GU_KEEP);stencil_pass=c;}
''' + binder + r'''
static unsigned draw_alpha_reference(unsigned t){unsigned r=((t>>4)&255)*2;return r>255?255:r;}
static int translucent_material(const Material *m,int mod){
    return p4_translucent_late&&mod&&p4_late_rgb_material(sky_state[3],sky_state[2],pixel_mask,m->colour>>24);
}
static int draw_vertex_alpha(int vt,int n,const void *ix,const void *v,unsigned *lo,unsigned *hi){
    (void)vt;(void)n;(void)ix;(void)v;*lo=0;*hi=test_hi;return 1;
}
static unsigned char ge_memory[64*1024];
static unsigned ge_used;
static void *sceGuGetMemory(unsigned n){unsigned at=ge_used;ge_used=(ge_used+n+15)&~15u;assert(ge_used<=sizeof ge_memory);return ge_memory+at;}
static void camera_projection(ScePspFMatrix4 *p){memset(p,0,sizeof *p);p->m[0]=2;}
static void gu_dirty(void){}
static void mesh_state(const Matrix *m,int lit){assert(!lit);translucent_model=*m;}
static float bound_view_pos;
static void set_matrix(int type,const Matrix *m){assert(type==GU_VIEW&&m->at.z==1);bound_view_pos=m->pos.z;}
static void sceGuSetMatrix(int type,const ScePspFMatrix4 *m){assert(type==GU_PROJECTION&&m->m[0]==2);}
static void bind_mask(unsigned a){assert(a==255);}
static void bind_raster(Raster *r,int wrap){(void)r;assert(wrap==GU_REPEAT);}
static void st_alpha_func(int f,int ref){assert(f==GU_ALWAYS&&!ref);}
static void st_depth_mask(int m){assert(m==GU_TRUE);}
static float emitted_z[64];
static int emitted;
static void gu_draw(int p,int vt,int n,const void *ix,const void *v){
    assert(p==GU_TRIANGLES&&n==3&&!ix&&!(vt&GU_INDEX_BITS));
    assert(recorded_mask==0xff000000u&&render_state[RS_ZTEST]);
    assert(sky_state[2]==0x44&&sky_state[3]==0x735fb);
    const Vertex3D *a=v;assert(a[0].colour==0x99ecebe5);
    emitted_z[emitted++]=a[0].z+translucent_model.pos.z+bound_view_pos;
}
static void p4_log(const char *f,...){(void)f;}
#include "translucent_pass.h"
static void reset(void){
    free(late_meshes);late_meshes=0;
    late_count=late_bytes=late_frame=late_queued=late_drawn=late_refused=0;
    late_flushing=0;cache_frame=1;in_frame=draw_alpha_known=p4_translucent_late=1;
    pixel_mask=draw_material_ge=ge_used=emitted=0;bound_alpha_max=255;test_hi=153;
    memset(render_state,0,sizeof render_state);memset(sky_state,0,sizeof sky_state);
    sky_state[2]=0x44;sky_state[3]=0x735fb;render_state[RS_ZTEST]=render_state[RS_ZWRITE]=1;
    camera=(Matrix){.right={1,0,0},.up={0,1,0},.at={0,0,1}};
    translucent_model=camera;current_camera=&camera;
}
int main(void){
    /* A zero-alpha mask needs stencil REPLACE, with RGB protected. */
    pixel_mask=0x00ffffff;bind_mask_mode(0,1);
    assert(enabled[GU_STENCIL_TEST]&&stencil_ref==0&&recorded_mask==0x00ffffff);
    bind_mask_mode(73,0);assert(enabled[GU_STENCIL_TEST]&&stencil_ref==73&&stencil_pass==GU_REPLACE);
    pixel_mask=0;bind_mask_mode(255,1);assert(!enabled[GU_STENCIL_TEST]);
    date=1;recorded_mask=123;pixel_mask=0x00ffffff;bind_mask_mode(0,1);assert(recorded_mask==123);date=0;
    assert(p4_late_rgb_material(0x735fb,0x44,0,153));
    assert(!p4_late_rgb_material(0x717fb,0x44,0,153)); /* FB_ONLY fog */
    assert(!p4_late_rgb_material(0x735fb,0x44,0,255));
    assert(!p4_late_rgb_material(0x735fb,0x58,0,153));
    assert(!p4_late_rgb_material(0x735fb|0x4000,0x44,0,153)); /* DATE */
    assert(p4_alpha_all_fail(0x735fb,190,153));
    assert(!p4_alpha_all_fail(0x735fb,190,190));
    assert(p4_alpha_all_fail(0x735fd,190,190)); /* GREATER: equality fails */
    assert(p4_alpha_product_ceiling(255,153)==153&&p4_alpha_product_ceiling(1,1)==1);
    reset();
    Material m={0,0x99ecebe5};
    Vertex3D v[3]={{0,0,0x99ecebe5,0,0,10},{0,0,0x99ecebe5,1,0,10},{0,0,0x99ecebe5,0,1,10}};
    unsigned short ix[3]={2,0,1};
    assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    v[0].z=v[1].z=v[2].z=30;
    assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    v[0].z=v[1].z=v[2].z=20;
    assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    /* Simulate a later mesh reusing CPU scratch and render state. */
    memset(v,0,sizeof v);sky_state[3]=123;render_state[12]=987;pixel_mask=0x1234;
    translucent_flush();
    assert(emitted==3&&emitted_z[0]==30&&emitted_z[1]==20&&emitted_z[2]==10);
    assert(!late_count&&late_drawn==3&&pixel_mask==0x1234&&recorded_mask==0x1234&&sky_state[3]==123&&render_state[12]==987);
    translucent_flush();assert(emitted==3);
    /* Model/view snapshots survive later camera and object movement. */
    reset();
    for(int i=0;i<3;++i)v[i]=(Vertex3D){0,0,0x99ecebe5,0,0,1};
    translucent_model.pos.z=10;camera.pos.z=2;
    assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    translucent_model.pos.z=30;camera.pos.z=3;
    assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    translucent_model.pos.z=100;camera.pos.z=200;
    translucent_flush();assert(emitted==2&&emitted_z[0]==34&&emitted_z[1]==13);
    assert(translucent_model.pos.z==100&&camera.pos.z==200);
    reset();test_hi=190;assert(!translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    test_hi=153;draw_alpha_known=0;assert(!translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    draw_alpha_known=1;late_frame=cache_frame;late_bytes=LATE_COPY_BYTES;assert(!translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1)&&late_refused==1);
    cache_frame++;assert(translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    late_count=0;late_bytes=0;late_frame=0;
    draw_material_ge=1;assert(!translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    draw_material_ge=0;p4_translucent_late=0;assert(!translucent_defer(GU_TRIANGLES,VT_3D,3,ix,v,&m,1));
    free(late_meshes);return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-alpha-test-') as name:
            folder=Path(name);c=folder/'check.c';exe=folder/'check.exe';c.write_text(source)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:
                command=[cc,'-std=c11','-O2',f'-I{ROOT / "port"}',str(c),'-o',str(exe)]
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'
                compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10'
                versions=sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:self.skipTest('host C compiler unavailable')
                compiler,headers=compilers[-1],versions[-1];vc=compiler.parents[3]
                command=[str(compiler),'/nologo','/std:c11','/O2',f'/I{ROOT / "port"}',f'/I{vc / "include"}',
                         f'/I{headers / "ucrt"}',str(c),f'/Fe:{exe}',f'/Fo:{folder / "check.obj"}','/link',
                         f'/LIBPATH:{vc / "lib/x64"}',f'/LIBPATH:{kits / "Lib" / headers.name / "ucrt/x64"}',
                         f'/LIBPATH:{kits / "Lib" / headers.name / "um/x64"}']
            result=subprocess.run(command,cwd=folder,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
