"""Compare actual generic/native draw command streams and GE vertices."""
import unittest
from ui_test_support import UiTestCase, function, ROOT


class UiDraw(UiTestCase):

    def test_shared_renderer(self):
        root = ROOT
        new = (root / 'port/im2d.h').read_text()
        code = r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "ui_quad.h"
typedef struct {float x,y,z,camZ,u,v,recipZ,pad,r,g,b,a,nx,ny,nz,pad2;} Vertex2D;
typedef struct {float u,v;unsigned colour;float x,y,z;} GuVertex;
typedef struct {int width,height,fade;} Raster;
typedef struct {int unused;} P4FeedbackRect;
enum {GU_POINTS,GU_LINES,GU_LINE_STRIP,GU_TRIANGLES,GU_TRIANGLE_STRIP,GU_TRIANGLE_FAN};
enum {GU_SPRITES=6,GU_TEXTURE_32BITF=8,GU_COLOR_8888=16,GU_VERTEX_32BITF=32,GU_TRANSFORM_2D=64,
      GU_CLIP_PLANES=100,GU_ALPHA_TEST,GU_GREATER,GU_TRUE,GU_FALSE,PROF_2D,GT_2D,GU_NEVER,GU_ALWAYS,GU_LESS,GU_LEQUAL,GU_EQUAL,GU_GEQUAL,GU_NOTEQUAL,GU_BLEND};
static const int gu_prim[7]={GU_POINTS,GU_LINES,GU_LINE_STRIP,GU_TRIANGLES,GU_TRIANGLE_STRIP,GU_TRIANGLE_FAN,GU_POINTS};
static int skip_frame,p4_probe_flags,gate_closed,rtt_2d;
static Raster *rtt_target;
static int list_available=1;
static int list_room(void){return list_available;}
static int p4_shadow_fade=1;
#define SKIPPING skip_frame
static int p4_probe_2d,p4_2d_nan_drop=1,p4_alpha_pixel,p4_2d_alpha_masks,p4_alpha_write_test;
static int p4_feedback_sprites=1,p4_dst_alpha_many,cache_frame,p4_dst_alpha_passes;
static unsigned nan_2d_dropped,sky_state[8],pixel_mask,dst_small_now,dst_small_last,dst_small_frame,dst_alpha_skipped,gs_alpha_splits;
static long long render_state[32];
static Raster display_raster,texture,target;
static float texture_w,texture_h,target_sx,target_sy;
static int split,mask_early;
static unsigned trace[512],commands,allocs;
static GuVertex emitted[64],pool[128];
static unsigned emitted_count,used;
static void note(unsigned op,unsigned a,unsigned b,unsigned c) {
    assert(commands+4<512);trace[commands++]=op;trace[commands++]=a;trace[commands++]=b;trace[commands++]=c;
}
static void prof_begin(int x){note(1,x,0,0);}static void prof_end(int x){note(2,x,0,0);}
static void gate_poll(void){note(3,0,0,0);}static int ge_time_begin(void){note(4,0,0,0);return 1;}
static void ge_time_end(int a,int b){note(5,a,b,0);}static void translucent_flush(void){note(6,0,0,0);}
static void gu_dirty(void){note(7,0,0,0);}static void begin_frame(void){note(8,0,0,0);}
static void bind_depth(void){note(9,0,0,0);}static void bind_mask_mode(unsigned a,int b){note(10,a,b,0);}
static void bind_texture(void){texture_w=256;texture_h=128;note(11,0,0,0);}
static void bind_blend(void){note(12,0,0,0);}static void sceGuDisable(int a){note(13,a,0,0);}
static void sceGuEnable(int a){note(14,a,0,0);}static void sceGuAlphaFunc(int a,int b,int c){note(15,a,b,c);}
static void sceGuDepthMask(int a){note(16,a,0,0);}static int gs_alpha_fb_only(void){return split;}
static int draw_2d_alpha_mask(int p,int n,GuVertex *v){note(17,p,n,0);return mask_early;}
static void *sceGuGetMemory(unsigned n){void *p=pool+used;used+=n/sizeof(GuVertex);allocs++;assert(used<128);return p;}
static int p4_feedback_rect(GuVertex *v,int n,P4FeedbackRect *r){return 0;}
enum {P4_FEEDBACK_MAX_VERTICES=16};
static int p4_feedback_emit(GuVertex *v,P4FeedbackRect *r){return 0;}
static void sceGuDrawArray(int p,int f,int n,const void *idx,const void *v) {
    note(18,p,f,n);assert(emitted_count+n<=64);memcpy(emitted+emitted_count,v,n*sizeof(GuVertex));emitted_count+=n;
}
static void census2d_note(int p,const Vertex2D *v,const unsigned short *idx,int n){note(19,p,n,0);}
static void p4_log(const char *fmt,...){}
'''
        code += function(new, 'byte_colour(') + function(new, 'put_vertex(')
        code += function(new, 'shadow_fade(') + function(new, 'ui_draw_state(') + function(new, 'ui_draw_finish(')
        code += 'static int draw_body(int,const Vertex2D *,const unsigned short *,int);\n'
        code += function(new, 'draw(') + function(new, 'draw_body(') + function(new, 'p4_ui_quad_draw(')
        code += r'''
static void reset(void) {
    commands=emitted_count=used=allocs=0;nan_2d_dropped=dst_small_now=dst_small_last=dst_alpha_skipped=gs_alpha_splits=0;
    dst_small_frame=cache_frame;rtt_2d=0;target.fade=0;memset(trace,0,sizeof trace);memset(emitted,0,sizeof emitted);memset(pool,0,sizeof pool);
}
typedef struct {unsigned trace[512],commands,count,allocs,nan,skips,splits;int rtt,fade;GuVertex v[64];} Snapshot;
static Snapshot snapshot(void){Snapshot s={0};memcpy(s.trace,trace,sizeof trace);memcpy(s.v,emitted,sizeof emitted);
    s.commands=commands;s.count=emitted_count;s.allocs=allocs;s.nan=nan_2d_dropped;s.skips=dst_alpha_skipped;s.splits=gs_alpha_splits;s.rtt=rtt_2d;s.fade=target.fade;return s;}
int main(void) {
    Vertex2D v[4]={{0}};P4UiVertex compact[4];unsigned cases=0;
    for(int config=0;config<8192;config++) {
        skip_frame=config&1;p4_probe_flags=config&2;p4_probe_2d=config&4?1:0;
        pixel_mask=config&8?0x00ffffff:0;render_state[1]=(long long)(config&16?&display_raster:&texture);
        split=(config>>5)&1;mask_early=(config>>6)&1;gate_closed=(config>>7)&1;rtt_target=config&256?&target:0;
        list_available=!(config&2048);
        sky_state[2]=0x54;sky_state[3]=0x717fb;target_sx=0.75f;target_sy=272.f/448;
        for(int i=0;i<4;i++) {
            v[i].x=(i&1)?123.5f:-10.25f;v[i].y=(i&2)?230.f:0.25f;v[i].z=(i-1)*65536.f;
            v[i].u=(i&1)?0.875f:-0.125f;v[i].v=(i&2)?0.7f:0.1f;v[i].r=23;v[i].g=90;v[i].b=175;v[i].a=i*85;
            compact[i].u.els.u=v[i].u;compact[i].u.els.v=v[i].v;
            compact[i].u.els.screen.x=v[i].x;compact[i].u.els.screen.y=v[i].y;compact[i].u.els.screen.z=v[i].z;
            p4_ui_colour(&compact[i],23,90,175,i*85);
        }
        if(config&512) v[0].x=compact[0].u.els.screen.x=NAN;
        else if(config&1024) v[0].y=compact[0].u.els.screen.y=1e6f;
        if(config&4096) {
            render_state[1]=0;sky_state[3]|=1<<14;
            for(int i=0;i<4;i++){v[i].r=v[i].g=v[i].b=0;p4_ui_colour(&compact[i],0,0,0,i*85);}
        }
        reset();draw(4,v,0,4);Snapshot reference=snapshot();
        reset();p4_ui_quad_draw(compact);Snapshot direct=snapshot();assert(!memcmp(&reference,&direct,sizeof reference));cases++;
    }
    puts("8192 draw configurations: generic/native GE vertices, states, guards, list capacity and shadow fades match");
}
'''
        self.check_c(code)


if __name__ == '__main__': unittest.main()
