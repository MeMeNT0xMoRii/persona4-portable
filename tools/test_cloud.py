"""Exercise the cloud texture, cache lifetime and guarded single-draw path on the host."""
import unittest
from ui_test_support import UiTestCase


class Cloud(UiTestCase):
    def test_texture_and_draw(self):
        self.check_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "swizzle.h"
typedef struct {unsigned char *originalPixels,*palette;int width,height,depth,stride;} Raster;
typedef struct {unsigned wbytes,rows;} SwizzleHead;
typedef struct {float x,y,z,camZ,u,v,recipZ,pad,r,g,b,a,nx,ny,nz,pad2;} Vertex2D;
typedef struct {float u,v;unsigned colour;float x,y,z;} GuVertex;
enum {RS_ZWRITE, GU_STENCIL_TEST,GU_ALPHA_TEST,GU_CLIP_PLANES,GU_TEXTURE_2D,GU_BLEND,GU_ADD,GU_SRC_ALPHA,
      GU_ONE_MINUS_SRC_ALPHA,GU_PSM_8888,GU_TFX_MODULATE,GU_TCC_RGBA,GU_LINEAR,GU_CLAMP,GU_TEXTURE_AUTO,
      GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF,GU_COLOR_8888,GU_VERTEX_32BITF,GU_TRANSFORM_2D,PROF_2D,GT_2D};
static unsigned cache_frame=10,texture_generation,sky_state[8]={0,0,0,0x31001},pixel_mask=0x00ffffff;
static uintptr_t render_state[32];
static int skipped,p4_probe_flags,p4_probe_2d,rtt_target,room=1,alloc_fail,draws,uploads;
static float texture_w,texture_h;
static GuVertex vertices[4];
static const void *bound;
#define SKIPPING skipped
static unsigned char smp[4096],snp[8192];
static const SwizzleHead mh={64,64},nh={64,128};
static const SwizzleHead *swizzle_head(const unsigned char *p){return p==smp?&mh:p==snp?&nh:0;}
static void *memalign(unsigned alignment,unsigned size){return alloc_fail?0:malloc(size);}
#define sceKernelDcacheWritebackRange(...) ((void)0)
#define sceGuPixelMask(...) ((void)0)
#define sceGuDisable(...) ((void)0)
#define sceGuEnable(...) ((void)0)
#define sceGuTexMode(...) ((void)0)
#define sceGuTexFunc(...) ((void)0)
#define sceGuTexFilter(...) ((void)0)
#define sceGuTexWrap(...) ((void)0)
#define sceGuTexLevelMode(...) ((void)0)
#define sceGuTexScale(...) ((void)0)
#define sceGuTexOffset(...) ((void)0)
#define sceGuTexFlush(...) ((void)0)
#define prof_begin(...) ((void)0)
#define prof_end(...) ((void)0)
#define p4_log(...) ((void)0)
#define ge_time_begin() 0
#define ge_time_end(...) ((void)0)
#define translucent_flush() ((void)0)
#define gu_dirty() ((void)0)
#define begin_frame() ((void)0)
#define bind_depth() ((void)0)
static int list_room(void){return room;}
static void sceGuBlendFunc(int op,int src,int dst,int a,int b){
    assert(op==GU_ADD&&src==GU_SRC_ALPHA&&dst==GU_ONE_MINUS_SRC_ALPHA);
}
static void sceGuTexImage(int level,int w,int h,int stride,const void *p){
    assert(!level&&w==128&&h==128&&stride==128);bound=p;uploads++;
}
static void *sceGuGetMemory(unsigned n){assert(n==sizeof vertices);return vertices;}
static void sceGuDrawArray(int prim,int type,int count,const void *indices,const void *p){
    assert(prim==GU_TRIANGLE_STRIP&&count==4&&!indices&&p==vertices);draws++;
}
static void put_vertex(GuVertex *v,const Vertex2D *a){
    *v=(GuVertex){a->u*texture_w,a->v*texture_h,((unsigned)a->a)<<24,a->x,a->y,a->z};
}
#include "cloud.h"
int main(void){
    unsigned char mp[4096],np[8192];unsigned mc[256],nc[16];
    for(int y=0;y<64;y++)for(int x=0;x<64;x++)mp[y*64+x]=x;
    for(int i=0;i<256;i++)mc[i]=0x00ffffff|((unsigned)(i*4&255)<<24);
    for(int i=0;i<16;i++)nc[i]=0x11000000|i*0x10203;
    for(int y=0;y<128;y++)for(int x=0;x<64;x++)np[y*64+x]=((x+y)&15)|(((x+y+1)&15)<<4);
    Raster m={mp,(void *)mc,64,64,8,64},n={np,(void *)nc,128,128,4,64};
    render_state[1]=(uintptr_t)&m;
    Vertex2D a[4]={{0}},b[4];
    for(int i=0;i<4;i++){
        a[i].u=i&1;a[i].v=i>>1;a[i].x=(i&1)*150;a[i].y=(i>>1)*150;a[i].z=123;
        a[i].r=255;a[i].g=100;a[i].b=50;a[i].a=127;
    }
    for(int flip=0;flip<8;flip++){
        memcpy(b,a,sizeof b);
        for(int i=0;i<4;i++){
            if(flip&4){b[i].u=a[i].v;b[i].v=a[i].u;}
            if(flip&1)b[i].u=1-b[i].u;if(flip&2)b[i].v=1-b[i].v;
        }
        draws=0;assert(p4_cloud_draw(a,b,&n,0x54)&&draws==1);
        const unsigned char *image=bound;
        for(int y=0;y<128;y++)for(int x=0;x<128;x++){
            unsigned got=*(const unsigned *)(image+ge_swizzle_offset(x*4,y,512));
            int alpha=x*2-1;if(alpha<0)alpha=0;if(alpha>252)alpha=252;
            int nx=flip&4?y:x,ny=flip&4?x:y;
            unsigned rgb=cloud_texel(np,nc,flip&1?127-nx:nx,flip&2?127-ny:ny,4,0)&0xffffff;
            assert(got==(rgb|((unsigned)alpha<<24)));
        }
        assert(vertices[3].u==128&&vertices[3].v==128&&vertices[3].colour==0x7f000000);
    }
    assert(cloud_builds==1);
    /* Exact input comparison detects mutation; queued lists retain their old texture. */
    const void *old=bound;texture_generation++;nc[1]^=0x80;
    assert(p4_cloud_draw(a,b,&n,0x54)&&bound!=old&&cloud_builds==2);
    draws=0;b[0].u=.5;assert(!p4_cloud_draw(a,b,&n,0x54)&&!draws);memcpy(b,a,sizeof b);
    b[0].x++;assert(!p4_cloud_draw(a,b,&n,0x54));memcpy(b,a,sizeof b);
    render_state[RS_ZWRITE]=1;assert(!p4_cloud_draw(a,b,&n,0x54));render_state[RS_ZWRITE]=0;
    assert(!p4_cloud_draw(a,b,&n,0x44));
    m.depth=4;assert(!p4_cloud_draw(a,b,&n,0x54));m.depth=8;
    for(int i=0;i<2;i++){texture_generation++;nc[1]++;assert(p4_cloud_draw(a,b,&n,0x54));}
    texture_generation++;nc[1]++;assert(!p4_cloud_draw(a,b,&n,0x54)); /* four live slots */
    cache_frame+=5;assert(p4_cloud_draw(a,b,&n,0x54));
    cloud_clear();alloc_fail=1;assert(!p4_cloud_draw(a,b,&n,0x54));alloc_fail=0;
    skipped=1;assert(p4_cloud_draw(a,b,&n,0x54));skipped=0;
    assert(cloud_draws==12 && cloud_size_refused==1 && cloud_geometry_refused==2 && cloud_busy==1);cloud_clear();
    ge_swizzle(smp,mp,64,64,64,64,1);ge_swizzle(snp,np,64,128,64,128,1);
    m.originalPixels=smp;n.originalPixels=snp;
    assert(p4_cloud_draw(a,b,&n,0x54));
    for(int y=0;y<128;y++)for(int x=0;x<128;x++){
        unsigned got=*(const unsigned *)((const unsigned char *)bound+ge_swizzle_offset(x*4,y,512));
        int alpha=x*2-1;if(alpha<0)alpha=0;if(alpha>252)alpha=252;
        assert(got==((cloud_texel(np,nc,x,y,4,0)&0xffffff)|((unsigned)alpha<<24)));
    }
    cloud_clear();
}
''')


if __name__ == '__main__':
    unittest.main()
