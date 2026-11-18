"""Exercise packed UVs, renderer alpha reads and prepared-list ownership using production functions."""
from pathlib import Path
import unittest
import ui_test_support

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.rfind('\nstatic ', 0, source.index(name)) + 1
    brace = source.index('{', source.index(name))
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class StaticUV(unittest.TestCase):
    check_c = ui_test_support.UiTestCase.check_c

    def test_temporary_buffers_keep_particle_budget_and_lifetime(self):
        rw = ''.join(p.read_text() for p in [ROOT / 'port/rwsky.c', *sorted((ROOT / 'port').glob('*.h'))])
        group = rw[rw.index('typedef struct PtankSpill'):rw.index('static int ptank_submit(')]
        self.check_c(r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {float u,v;unsigned colour;float x,y,z;} Vertex3D;
static unsigned cache_frame=10,live;
static int fail;
static void *memalign(size_t a,size_t bytes) {if(fail)return 0;void *p=malloc(bytes);if(p)live++;return p;}
static void tracked_free(void *p) {assert(live);live--;free(p);}
#define free tracked_free
'''+group+r'''
#undef free
int main(void) {
    unsigned payload=256*1024-sizeof(PtankSpill);
    unsigned char *uv=static_uv_alloc(payload);
    assert(uv && static_uv_temp_bytes==256*1024 && !ptank_spill_bytes);
    memset(uv,0xac,payload);
    assert(!static_uv_alloc(1));
    assert(ptank_spill_alloc(10) && ptank_spill_bytes);
    cache_frame=14;ptank_spill_collect(0);
    assert(live==2 && uv[payload-1]==0xac);
    cache_frame=15;ptank_spill_collect(0);
    assert(!live && !static_uv_temp_bytes && !ptank_spill_bytes && !ptank_spills);
    assert(ptank_spill_alloc((p4_ptank_spill_budget-sizeof(PtankSpill))/sizeof(Vertex3D)));
    assert(!ptank_spill_alloc(1) && static_uv_alloc(100));
    p4_ptank_spill_budget=0;assert(!ptank_spill_alloc(1));
    assert(!ptank_spill_alloc(-1) && !ptank_spill_alloc(0x7fffffff));
    assert(!static_uv_alloc(~0u));
    fail=1;assert(!static_uv_alloc(100));fail=0;
    ptank_spill_collect(1);assert(!live && !static_uv_temp_bytes && !ptank_spill_bytes);
    puts("UV and particle budgets are independent; buffers survive four frames and retire safely");
    return 0;
}
''')

    def test_packing_alpha_and_fallback(self):
        rw = ''.join(p.read_text() for p in [ROOT / 'port/rwsky.c', *sorted((ROOT / 'port').glob('*.h'))])
        code = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "static_uv.h"
enum {GU_TEXTURE_16BIT=2,GU_TEXTURE_32BITF=3,GU_TEXTURE_BITS=3,GU_COLOR_8888=7<<2,GU_COLOR_BITS=7<<2,
      GU_NORMAL_8BIT=1<<5,GU_NORMAL_BITS=3<<5,GU_VERTEX_32BITF=3<<7,GU_VERTEX_BITS=3<<7,
      GU_WEIGHT_BITS=3<<9,GU_INDEX_16BIT=2<<11,GU_INDEX_BITS=3<<11};
typedef struct {float u,v;unsigned colour;float x,y,z;} Vertex3D;
typedef struct {float u,v;unsigned colour;signed char nx,ny,nz,pad;float x,y,z;} Vertex3DN;
static const int *draw_skin_alpha,*draw_fixed_alpha;
static const void *draw_slot_v;
static int *draw_slot_alpha;
static unsigned tested_scans,draw_material_alpha=255;
static int draw_material_ge;
'''+function(rw, 'draw_vertex_alpha(')+r'''
int main(void) {
    unsigned char source[128*28],packed[128*24+16],saved[sizeof source];
    unsigned seed=9,lo,hi,plo,phi;
    unsigned short ix[]={3,11,37,64,127};
    int lit,i,j;
    float decode[4];
    for(lit=0;lit<2;lit++) for(j=0;j<64;j++) {
        int stride=lit?28:24;
        int type=GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_INDEX_16BIT|(lit?GU_NORMAL_8BIT:0);
        for(i=0;i<128;i++) {
            float uv[2];unsigned color;
            memset(source+i*stride,0x5a,stride);
            seed=seed*1664525u+1013904223u;uv[0]=-4+(seed>>16)*(8.0f/65535);
            seed=seed*1664525u+1013904223u;uv[1]=j%2?1.5f:(seed>>16)*(8.0f/65535);
            color=(unsigned)(i*2)<<24|0x765432;
            memcpy(source+i*stride,uv,8);memcpy(source+i*stride+8,&color,4);
        }
        memcpy(saved,source,sizeof source);memset(packed,0xcc,sizeof packed);
        assert(static_uv_pack(source,128,stride,packed,decode));
        assert(static_uv_same(source,packed,128,stride,decode));
        assert(!memcmp(source,saved,sizeof source));
        assert(packed[128*(stride-4)]==0xcc);
        draw_material_alpha=j%2?137:255;
        assert(draw_vertex_alpha(type,5,ix,source,&lo,&hi));
        assert(draw_vertex_alpha((type&~GU_TEXTURE_BITS)|GU_TEXTURE_16BIT,5,ix,packed,&plo,&phi));
        assert(lo==plo && hi==phi);
        packed[4]^=1;assert(!static_uv_same(source,packed,128,stride,decode));
    }
    {float bad=INFINITY;memcpy(source,&bad,4);memset(packed,0xcc,sizeof packed);
     assert(!static_uv_pack(source,128,24,packed,decode) && packed[0]==0xcc);}
    {float bad=NAN;memcpy(source,&bad,4);assert(!static_uv_pack(source,128,24,packed,decode));}
    memset(source,0,sizeof source);
    {float bad=9;memcpy(source+24,&bad,4);assert(!static_uv_pack(source,128,24,packed,decode));}
    {float tiny=1e-30f;memcpy(source+24,&tiny,4);assert(!static_uv_pack(source,128,24,packed,decode));}
    assert(!static_uv_pack(source,0,24,packed,decode));
    assert(!static_uv_pack(source,128,20,packed,decode));
    puts("16384 packed vertices: attributes, UV error, indexed alpha and fallback checks passed");
    return 0;
}
'''
        self.check_c(code)

    def test_exact_position_conversion(self):
        self.check_c(r'''
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include "static_position.h"
int main(void) {
    unsigned char source[128*20],packed[128*16+1];unsigned seed=17;
    int accepted=0;
    for(int e=-20;e<=19;e++) {
        float bound=ldexpf(1,e)*0.75f,worst=0;
        for(int i=0;i<128;i++) {
            float p[3];
            memset(source+i*20,i,20);
            for(int k=0;k<3;k++) {
                seed=seed*1664525u+1013904223u;
                p[k]=((float)(seed>>8)/(1<<24)*2-1)*bound;
            }
            memcpy(source+i*20+8,p,12);
        }
        float scale=static_position_scale(source,128,8,bound);assert(scale>0 && scale<4*bound);
        memset(packed,0xff,sizeof packed);static_position_copy(packed,source,128,scale,8);
        assert(packed[128*16]==0xff);
        for(int i=0;i<128;i++) {
            float p[3];short q[3];memcpy(p,source+i*20+8,12);memcpy(q,packed+i*16+8,6);
            assert(!memcmp(source+i*20,packed+i*16,8));
            for(int k=0;k<3;k++){float d=fabsf(q[k]*(scale/32768)-p[k]);if(d>worst)worst=d;}
            accepted++;
        }
        assert(worst<=scale/65536*1.001f); /* nearest grid point */
    }
    {/* the bound itself and its negation stay inside s16 */
     float p[3]={1000,-1000,0};memcpy(source+8,p,12);float scale=static_position_scale(source,1,8,1000);
     static_position_copy(packed,source,1,scale,8);short q[3];memcpy(q,packed+8,6);assert(q[0]<=32767&&q[1]>=-32767);}
    {/* two meshes of one geometry: a vertex they share lands on the same s16 */
     unsigned char a[2*20],b[3*20],pa[2*16],pb[3*16];float v[3]={123.456f,-7.89f,0.001f},w[3]={1,2,3};
     memcpy(a+8,v,12);memcpy(a+28,w,12);memcpy(b+8,w,12);memcpy(b+28,w,12);memcpy(b+48,v,12);
     float sa=static_position_scale(a,2,8,200),sb=static_position_scale(b,3,8,200);assert(sa==sb);
     static_position_copy(pa,a,2,sa,8);static_position_copy(pb,b,3,sb,8);assert(!memcmp(pa+8,pb+40,6));}
    {unsigned char lit[64*24],out[64*20+1];float scale;
     for(int i=0;i<64;i++){float p[3]={i*0.25f,-i*0.5f,i};memset(lit+i*24,i+1,12);memcpy(lit+i*24+12,p,12);}
     scale=static_position_scale(lit,64,12,64);assert(scale>0);
     memset(out,0xff,sizeof out);static_position_copy(out,lit,64,scale,12);assert(out[64*20]==0xff);
     for(int i=0;i<64;i++){float p[3];short q[3];memcpy(p,lit+i*24+12,12);memcpy(q,out+i*20+12,6);
      assert(!memcmp(lit+i*24,out+i*20,12)); /* UV, colour, normal and pad unchanged */
      assert(!out[i*20+18]&&!out[i*20+19]);
      for(int k=0;k<3;k++)assert(q[k]*(scale/32768)==p[k]);}}
    {float p[3]={-1,0.1f,0};memcpy(source+8,p,12);
     assert(static_position_scale(source,1,8,1)==2);
     p[0]=1.5f;memcpy(source+8,p,12);assert(!static_position_scale(source,1,8,1)); /* outside the sphere */
     p[0]=NAN;memcpy(source+8,p,12);assert(!static_position_scale(source,1,8,1));
     p[0]=INFINITY;memcpy(source+8,p,12);assert(!static_position_scale(source,1,8,1));
     p[0]=0;memcpy(source+8,p,12);assert(!static_position_scale(source,1,8,0)&&!static_position_scale(source,1,8,NAN)&&
        !static_position_scale(source,1,8,1048576));}
    assert(!static_position_scale(0,1,8,1) && !static_position_scale(source,0,8,1) && !static_position_scale(source,65537,8,1));
    printf("%d rounded positions across 40 binary bounds; shared vertices agree; invalid inputs refused\n",accepted);
    return 0;
}
''')

    def test_list_owns_packed_vertices_and_indices(self):
        rw = ''.join(p.read_text() for p in [ROOT / 'port/rwsky.c', *sorted((ROOT / 'port').glob('*.h'))])
        sl = (ROOT / 'port/static_list.h').read_text()
        v3 = rw.index('typedef struct { float x, y, z; } V3;')
        types = rw[v3:rw.index('} Matrix;', v3) + 10] + \
            rw[rw.index('typedef struct { void *parentGeom;'):rw.index('\nenum { CAMERA_PROJECTION')]
        entry = sl[sl.index('typedef struct {'):sl.index('typedef struct {\n    Geometry geometry;')]
        memo = sl[sl.index('int p4_inst_call ='):sl.index('static unsigned sl_memo_hits')]
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "static_uv.h"
#include "static_position.h"
'''+types+entry+r'''
typedef struct {void *v;unsigned short *idx;int vertices,indices,capacity,vtype;} MeshCache;
static unsigned sl_fog_key(void){return 0;}
enum {SL_MESHES=16,SL_EXTRA=16,GU_VERTEX_16BIT=2<<7,GU_VERTEX_32BITF=3<<7,GU_VERTEX_BITS=3<<7,
      GU_NORMAL_BITS=3<<5,GU_WEIGHT_BITS=3<<9,GU_TEXTURE_BITS=3,GU_TEXTURE_16BIT=2};
static int p4_static_position_pack,sl_gview_set;
static unsigned sl_position_vertices,sl_position_bytes;
static MeshCache *sl_slot[SL_MESHES];
static const void *sl_xp[SL_EXTRA];static unsigned sl_xb[SL_EXTRA];
static int sl_nslots,sl_nextra,sl_ge_uncut,p4_static_list_budget=1<<20;
static unsigned sl_uv_slots,sl_refused[8],sl_size_why[4],sl_size_logged,sl_bytes,sl_checks,sl_check_diffs,sl_records;
static const unsigned *mesh_model_begin,*mesh_model_mark;
static void *memalign(size_t alignment,size_t bytes) { return malloc(bytes); }
static void p4_log(const char *format,...) {}
static void sceKernelDcacheWritebackRange(const void *p,int bytes) {}
static void sl_drop(StaticList *e) { free(e->data);free(e->list);e->data=0;e->list=0; }
'''+function(sl, 'sl_note_uv(')+'\n'+function(sl, 'sl_address(')+'\n'+function(sl, 'sl_position_head(')+'\nstatic unsigned sl_position_why[5];\n'+function(sl, 'sl_position_refuse(')+'\n'+function(sl, 'sl_position_candidate(')+'\n'+function(sl, 'sl_keep(')+r'''
enum {MAX_STATE=64,GU_MODEL=0};
static int p4_static_uv_pack=1,p4_light_prelit,p4_light_unprelit;
static unsigned state_bits(void) {return 7;}
static unsigned cache_frame=1,texture_generation,pixel_mask;
static void *current_camera;
static int render_state[MAX_STATE],shadow_shear_m,rtt_target;
static const void *draw_fixed_alpha,*draw_skin_alpha;
static unsigned sky_state[8],sl_memo_hits,sl_memo_why[8],mesh_draw_calls,draws_since_stall;
static int p4_draw_batch=1000,gu_object_stack_depth;
static struct {unsigned *current;} list_state,*gu_list=&list_state;
static struct {int valid;} replay;
static SceneLight scene_lights(void) {return (SceneLight){0};}
static int sl_inside(const StaticList *e,const Matrix *m,const float *s) {return 1;}
static void begin_frame(void) {} static void gu_mesh_begin(void) {} static void set_camera(void) {}
static Matrix sent_model;
static void set_matrix(int which,const Matrix *m) {if(m)sent_model=*m;} static void sl_forget(void) {} static void stall_flush(void) {}
'''+memo+function(sl,'sl_model(')+'\n'+function(sl,'sl_bits(')+'\n'+function(sl,'sl_memo_set(')+'\n'+function(sl,'sl_memo_try(')+r'''
int main(void) {
    unsigned char cache[24*16+32],packed[20*16],snapshot[sizeof packed];
    float decode[4];int i;
    unsigned short expected[16];
    MeshCache slot={cache,(unsigned short *)(cache+24*16),16,16,sizeof cache};
    StaticList e={0};Geometry geometry={0};
    for(i=0;i<16;i++) {
        float uv[2]={i/15.0f,0.5f};memset(cache+i*24,0x44,24);memcpy(cache+i*24,uv,8);
        slot.idx[i]=expected[i]=(i*7)%16;
    }
    assert(static_uv_pack(cache,16,24,packed,decode));memcpy(snapshot,packed,sizeof packed);
    sl_nslots=1;sl_slot[0]=&slot;
    assert(sl_note_uv(&slot,packed,sizeof packed));
    assert(sl_uv_slots==1 && sl_nextra==1 && sl_xp[0]==packed);
    {unsigned original=(unsigned)(size_t)packed,index=(unsigned)(size_t)slot.idx;
     unsigned words[]={0,0,0,0,0,0,0,0};
     words[1]=0x10000000|(original>>8&0xf0000);words[2]=0x01000000|(original&0xffffff);
     words[3]=0x10000000|(index>>8&0xf0000);words[4]=0x02000000|(index&0xffffff);
     words[5]=0x04000010;
     mesh_model_begin=words;mesh_model_mark=words+1;
     assert(sl_keep(&e,words,words+6,0,&geometry));}
    assert(e.bytes==sizeof packed+sizeof expected+6*4);
    memset(cache,0,sizeof cache);memset(packed,0,sizeof packed);
    assert(!memcmp(e.data,expected,sizeof expected));
    assert(!memcmp((char *)e.data+sizeof expected,snapshot,sizeof snapshot));
    {unsigned v=sl_address(e.list[0],e.list[1]),ix=sl_address(e.list[2],e.list[3]);
     assert(ix==((unsigned)(size_t)e.data&0xfffffff));
     assert(v==(((unsigned)(size_t)e.data+sizeof expected)&0xfffffff));}
    sl_nslots=SL_MESHES+1;assert(!sl_note_uv(&slot,snapshot,sizeof snapshot));
    sl_nslots=1;sl_nextra=SL_EXTRA;assert(!sl_note_uv(&slot,snapshot,sizeof snapshot));
    {unsigned words[16],key=sl_bits();MorphTarget target={0};geometry.morphTarget=&target;
     list_state.current=words;sl_memo_set(&e,&geometry,0);assert(sl_memo_try(&geometry,0,0));
     p4_static_uv_pack=0;assert(sl_bits()!=key && !sl_memo_try(&geometry,0,0));
     p4_static_uv_pack=1;assert(sl_bits()==key && sl_memo_try(&geometry,0,0));}
    sl_drop(&e);
    /* Pack the retained copy, leaving the submitted recording's float vertices intact. */
    sl_nslots=sl_nextra=1;sl_uv_slots=1;sl_slot[0]=&slot;
    sl_xp[0]=packed;sl_xb[0]=sizeof packed;
    slot.vtype=(3<<7)|(7<<2)|3;
    for(i=0;i<16;i++) {
        float p[3]={i-8,i*0.25f,16};
        memset(packed+i*20,0x55,20);memcpy(packed+i*20+8,p,12);
    }
    memcpy(snapshot,packed,sizeof packed);
    {unsigned original=(unsigned)(size_t)packed,index=(unsigned)(size_t)slot.idx;
     unsigned words[]={0,0x12000000|((3<<7)|(7<<2)|2),
        0x10000000|(original>>8&0xf0000),0x01000000|(original&0xffffff),
        0x10000000|(index>>8&0xf0000),0x02000000|(index&0xffffff),0x04000010};
     mesh_model_begin=words;mesh_model_mark=words+1;p4_static_position_pack=1;
     static MorphTarget sphere_target={0,{0,0,0,16}};geometry.morphTarget=&sphere_target;
     assert(sl_position_candidate(words,words+7,&geometry)==32);
     {unsigned draw=words[6];words[6]=0x3b000000;assert(!sl_position_candidate(words,words+7,&geometry));words[6]=draw;}
     {unsigned key=sl_bits();p4_static_position_pack=0;assert(key!=sl_bits());p4_static_position_pack=1;}
     sl_ge_uncut=1;assert(!sl_position_candidate(words,words+7,&geometry));sl_ge_uncut=0;
     sl_gview_set=1;assert(!sl_position_candidate(words,words+7,&geometry));sl_gview_set=0;
     slot.vtype|=1<<5;assert(!sl_position_candidate(words,words+7,&geometry));slot.vtype&=~(1<<5);
     sl_nslots=2;assert(!sl_position_candidate(words,words+7,&geometry));sl_nslots=1;
     words[3]++;assert(!sl_position_candidate(words,words+7,&geometry));words[3]--;
     assert(sl_keep(&e,words,words+7,0,&geometry));
     assert(e.position_scale==32 && e.bytes==16*16+sizeof expected+7*4);
     assert((e.list[0]&GU_VERTEX_BITS)==GU_VERTEX_16BIT);
     assert(words[1]==(0x12000000|((3<<7)|(7<<2)|2)));
     assert(!memcmp(packed,snapshot,sizeof packed));
     for(i=0;i<16;i++) {
         short q[3];float p[3];unsigned char *v=(unsigned char *)e.data+sizeof expected+i*16;
         memcpy(q,v+8,6);memcpy(p,packed+i*20+8,12);
         assert(!memcmp(v,packed+i*20,8));
         for(int k=0;k<3;k++)assert(q[k]*(32.0f/32768)==p[k]);
     }
     /* Both list replay paths use the same decode, preserving instance translation. */
     Matrix model={.right={0,2,0},.up={-3,0,0},.at={0,0,4},.pos={17,18,19}};
     unsigned call[4];list_state.current=call;
     sl_memo_set(&e,&geometry,0);assert(sl_memo_try(&geometry,&model,0));
     assert(sent_model.right.y==64 && sent_model.up.x==-96 && sent_model.at.z==128);
     assert(!memcmp(&sent_model.pos,&model.pos,sizeof model.pos));
     p4_static_position_pack=0;assert(!sl_memo_try(&geometry,&model,0));
     sl_model(&e,0);assert(sent_model.right.x==32 && sent_model.pos.x==0);
    }
    sl_drop(&e);
    puts("prepared list owns and relocates short-UV vertices and original indices");
    return 0;
}
'''
        self.check_c(code)
