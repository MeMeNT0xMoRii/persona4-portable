"""Compare generated compact sprites with the production PS2 preparation on 32-bit layouts."""
import unittest
import psp_census as c
from ui_test_support import UiTestCase, function


class UiSprite(UiTestCase):

    def test_compact_sprite(self):
        rel = 'src/Kernel/sdkSpr.c'
        source = (c.DECOMP / rel).read_text()
        for old, new in c.FIXES[rel]:
            source = source.replace(old, new)
        native = c.native_ui(source, rel)
        include = (c.DECOMP / 'include').as_posix()
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TYPE_H
typedef unsigned char u8;typedef signed char s8;typedef unsigned short u16;typedef short s16;
typedef unsigned u32;typedef int s32;typedef unsigned long long u64;typedef long long s64;
typedef float f32;typedef double f64;
#include "ui_quad.h"
'''
        for header in ['rw_camera_internal.h', 'member_access.h', 'operand_order_internal.h', 'rw/plcore/barenderstate.h']:
            code += f'#include "{include}/{header}"\n'
        prefix = source[source.index('typedef struct RwV2d'):source.index('/* Advance pack loading')]
        prefix = prefix.replace('#include "rw_types_internal.h"', f'#include "{include}/rw_types_internal.h"')
        prefix = prefix.replace('unsigned __int128 qWords[4];', 'unsigned qWords[4][4];')
        code += prefix
        code += function(source, 'sdkSpriteTableRecord(')
        code += function(source, 'sdkSpriteIndexedRecord(')
        code += r'''
u32 ourGlobals[4096];int p4_ui_font,p4_ui_sprite;
f32 D_008872F8[1]={65535},fGpffff8084=3.14159265f;
f32 fGpffff8054=1.f/120,fGpffff8058=-1.f/5040,fGpffff805c=1.f/362880,fGpffff8060=0;
f32 fGpffff81b0=0,fGpffff81b4=0,fGpffff81b8=0,fGpffff81bc=0;
f32 fGpffff81c0=-1.f/720,fGpffff81c4=1.f/24,fGpffff81c8=0,fGpffff81cc=-1.f/6;
u8 D_007130D8[12];
static RwCamera camera;
static RwMatrix rotation;
static unsigned state_hash,draws;
static unsigned device_values[32],sky_values[4];
static P4UiVertex captured[24];
s32 kwlnGetCamera(void) {return (s32)&camera;}
RwMatrix *func_003e0f80(void) {return &rotation;}
RwMatrix *func_003e0680(RwMatrix *m,const RwV3d *axis,float oneMinus,float sine,RwOpCombineType op) {
    memset(m,0,sizeof *m);m->right.x=1-oneMinus;m->right.y=sine;m->up.x=-sine;m->up.y=1-oneMinus;return m;
}
RwV3d *func_003e42e0(RwV3d *out,const RwV3d *in,int n,const RwMatrix *m) {
    for(int i=0;i<n;i++){out[i].x=in[i].x*m->right.x+in[i].y*m->up.x;out[i].y=in[i].x*m->right.y+in[i].y*m->up.y;out[i].z=in[i].z;}
    return out;
}
s32 func_003e0f40(RwMatrix *m) {return 0;}
void sdkSpriteTransformPoints(u8 *p,u8 *v) {
    SdkSpriteSample *s=(SdkSpriteSample *)p;RwV2d *points=(RwV2d *)v;
    for(int i=0;i<4;i++){points[i].x+=s->x+s->pivotX;points[i].y+=s->y+s->pivotY;}
}
s32 RpSkyRenderStateSet(s32 state,void *v) {sky_values[state]=(unsigned)v;state_hash=state_hash*33+state+(unsigned)v;return 1;}
static s32 state(RwRenderState s,void *v) {device_values[s]=(unsigned)v;state_hash=state_hash*33+s+(unsigned)v;return 1;}
int p4_ui_quad_draw(const P4UiVertex *v) {assert(draws<6);memcpy(captured+draws*4,v,4*sizeof *v);draws++;return 1;}
static s32 submit(int prim,void *vertices,int count) {
    SdkSpriteVertex *v=vertices;P4UiVertex compact[4];assert(prim==4&&count==4);
    for(int i=0;i<4;i++) {
        compact[i].u.els.u=v[i].u.els.u;compact[i].u.els.v=v[i].u.els.v;
        compact[i].u.els.screen.x=v[i].u.els.screen.x;compact[i].u.els.screen.y=v[i].u.els.screen.y;
        compact[i].u.els.screen.z=v[i].u.els.screen.z;
        p4_ui_colour(&compact[i],(u8)v[i].u.els.color.red,(u8)v[i].u.els.color.green,
                     (u8)v[i].u.els.color.blue,(u8)v[i].u.els.color.alpha);
    }
    return p4_ui_quad_draw(compact);
}
'''
        code += function(source, 'sdkSpriteDraw(').replace('sdkSpriteDraw(', 'old_sprite(', 1)
        code += function(native, 'p4_calendar_sprite_state(')
        code += function(native, 'p4_native_sdkSpriteDraw(')
        code += r'''
int main(void) {
    SdkSpritePack pack={0};SdkSpriteRecord records[2]={0};SdkSpriteSample sample={0};RwRaster raster={0};
    P4UiVertex reference[24];unsigned expected_draws,expected_state,cases=0;
    unsigned expected_device[32],expected_sky[4];
    SdkSpriteGlobalsPrefix *globals=(void *)ourGlobals;
    globals->device.setState=state;globals->device.renderPrimitive=submit;
    camera.nearPlane=0.125f;raster.width=256;raster.height=128;
    pack.recordsAddress=(u32)records;pack.rasters[0]=(u32)&raster;sample.packAddress=(u32)&pack;
    for(int index=0;index<2;index++) for(int flags=0;flags<16;flags++) for(int borders=0;borders<16;borders++)
    for(int transform=0;transform<4;transform++) for(int extend=0;extend<3;extend++) {
        SdkSpriteRecord *r=&records[index];sample.recordIndex=index;
        r->flags=flags;r->renderFlags=flags&3;r->originX=-7;r->originY=3;
        r->left=9;r->top=7;r->right=71;r->bottom=63;
        for(int b=0;b<4;b++)r->borders[b]=(borders>>b&1)?b+2:0;
        r->colors[0]=0x0f89ff80;r->colors[1]=0xfeff00ff;r->colors[2]=0x12345600;r->colors[3]=0x99887760;
        r->widthOverride=index?35:0;r->heightOverride=index?28:0;
        sample.x=17.25f;sample.y=-9.5f;sample.depth=300;
        sample.pivotX=6;sample.pivotY=-3;sample.angle=transform==0?0:transform==1?-90:transform==2?45:180;
        sample.scaleX=transform?3072:0;sample.scaleY=transform?6144:0;
        sample.extendX=extend==1?13:0;sample.extendY=extend==2?11:0;
        sample.red=234;sample.green=177;sample.blue=111;sample.fade=transform*70;sample.alphaSubtract=transform*10;
        draws=state_hash=0;memset(captured,0,sizeof captured);memset(device_values,0,sizeof device_values);memset(sky_values,0,sizeof sky_values);
        old_sprite((u8 *)&sample,index);expected_draws=draws;expected_state=state_hash;memcpy(reference,captured,sizeof captured);
        memcpy(expected_device,device_values,sizeof expected_device);memcpy(expected_sky,sky_values,sizeof expected_sky);
        draws=state_hash=0;memset(captured,0,sizeof captured);memset(device_values,0,sizeof device_values);memset(sky_values,0,sizeof sky_values);
        p4_native_sdkSpriteDraw((u8 *)&sample,index);
        assert(expected_draws==draws && expected_state==state_hash && !memcmp(reference,captured,sizeof captured));cases++;
        if(index) {
            draws=0;memset(device_values,0,sizeof device_values);memset(sky_values,0,sizeof sky_values);
            p4_calendar_sprite_state(&pack,index);
            assert(!draws && !memcmp(expected_device,device_values,sizeof expected_device) && !memcmp(expected_sky,sky_values,sizeof expected_sky));
        }
    }
    printf("%u sprites: UV flips, rotation, scale, tint/fade, borders, extensions and device states match\n",cases);
}
'''
        self.check_32(code)


if __name__ == '__main__': unittest.main()
