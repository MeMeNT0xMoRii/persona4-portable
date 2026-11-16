"""Compare production calendar state and subsequent visible sprite preparation across skipped ticks."""
import unittest
import psp_census as c
from ui_test_support import UiTestCase, function


class CalendarSkip(UiTestCase):
    def test_transitions(self):
        rel = 'src/datCalendar_inferred_001228c0.c'
        original = (c.DECOMP / rel).read_text()
        reference = original.replace('signedScaleX = 0xFF6;', 'signedScaleX = 0xBF6;')
        candidate = original
        for old, new in c.FIXES[rel]:
            self.assertIn(old, candidate)
            candidate = candidate.replace(old, new)
        include = (c.DECOMP / 'include').as_posix()
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define TYPE_H
typedef unsigned char u8;typedef signed char s8;typedef unsigned short u16;typedef short s16;
typedef unsigned u32;typedef int s32;typedef unsigned long long u64;typedef long long s64;typedef float f32;typedef double f64;
'''
        code += f'#include "{include}/rw/plcore/barenderstate.h"\n'
        radar = (c.DECOMP / 'include/radar_menu_internal.h').read_text()
        code += radar[radar.index('typedef union MenuAnimValue'):radar.index('typedef union RadarSprite')]
        code += original[original.index('typedef struct SdkSpriteSample'):original.index('extern u8 *iGpffffb1dc;')]
        code += r'''
static int skip_frame;
'''
        rw = (c.ROOT / 'port/rwsky.c').read_text()
        code += rw[rw.index('int p4_font_skip ='):rw.index('/* b278')]
        code += r'''
f32 D_00761184=1.57079632679f;
static unsigned draws,allocations,live;
static int device[32],sky[4],environment;
static SdkSpriteSample pool,output[24];
static s32 set(RwRenderState state,void *v) {device[state]=(s32)v;return 1;}
static s32 get(RwRenderState state,void *v) {*(int *)v=device[state];return 1;}
s32 (*D_00887300[])(RwRenderState,void *)={set};
s32 (*D_00887304[])(RwRenderState,void *)={get};
s32 RpSkyRenderStateSet(s32 state,void *v) {sky[state]=(s32)v;return 1;}
u8 *sdkSpriteNewNode(u32 pack,u32 record) {
    assert(!live);allocations++;live++;memset(&pool,0,sizeof pool);
    pool.packAddress=pack;pool.recordIndex=record;pool.scaleX=pool.scaleY=4096;return (u8 *)&pool;
}
void sdkSpriteFreeNode(void *node){assert(live && node==&pool);live--;}
void p4_calendar_sprite_state(void *pack,unsigned record) {
    device[6]=device[8]=1;device[1]=5678+record%3;sky[2]=record%2?0x44:0x48;sky[3]=0x717FB;
}
void sdkSpriteDraw(u8 *sample,s32 states) {
    p4_calendar_sprite_state((void *)((SdkSpriteSample *)sample)->packAddress,((SdkSpriteSample *)sample)->recordIndex);
    if(!skip_frame){assert(draws<24);output[draws++]=*(SdkSpriteSample *)sample;}
}
f32 sdkSpriteScaledHeight(u8 *s){return 32.0f*((SdkSpriteSample *)s)->scaleY/4096.f;}
void sdkSpriteDrawOnceAux(s32 parent,s32 pack,s32 record,f32 x,f32 y,u8 subtract,u8 fade,f32 z,s32 states) {
    u8 *p=sdkSpriteNewNode(pack,record);pool.x=x;pool.y=y;pool.depth=z;pool.alphaSubtract=subtract;pool.fade=fade;
    sdkSpriteDraw(p,states);sdkSpriteFreeNode(p);
}
void sdkSpriteDrawOnceTinted(s32 parent,s32 pack,s32 record,f32 x,f32 y,u8 subtract,u8 r,u8 g,u8 b,f32 z,s32 states) {
    u8 *p=sdkSpriteNewNode(pack,record);pool.x=x;pool.y=y;pool.depth=z;pool.alphaSubtract=subtract;pool.red=r;pool.green=g;pool.blue=b;
    sdkSpriteDraw(p,states);sdkSpriteFreeNode(p);
}
void calSplitDayOfYear(s32 date,s32 *month,s32 *day){*month=date/30+1;*day=date%30+1;}
s32 calGetWeekdayOfDay(s32 date){return date%7;}
s32 calHasDayEntry(s32 date){return environment==2;}
s32 FldStepIndex(void){return environment==1;}
s32 evtHasTaskHandle(void){return environment==2;}
#include "calendar_skip.h"
'''
        motion = (c.DECOMP / 'src/Kernel/h_malloc_inferred_001400f0.c').read_text()
        code += function(motion, 'radarAnimateValue(')
        code += function(reference, 'calendarDrawBackdrop(').replace('calendarDrawBackdrop(', 'old_calendar(', 1)
        code += function(candidate, 'calendarDrawBackdrop(').replace('calendarDrawBackdrop(', 'new_calendar(', 1)
        code += r'''
typedef struct {CalendarState state;unsigned draws;SdkSpriteSample output[24];int device[32],sky[4];} Snapshot;
int main(void) {
    CalendarState cal;Snapshot expected[240];unsigned pairs=0;
    assert(sizeof(void *)==4 && sizeof(CalendarRecord)==48 && sizeof(CalendarState)==0x114);
    for(int flags=0;flags<8;flags++)for(int env=0;env<5;env++)for(int pass=0;pass<2;pass++) {
        environment=env;p4_calendar_skip=pass;calendar_ticks_skipped=0;
        memset(&cal,0,sizeof cal);memset(device,0,sizeof device);memset(sky,0,sizeof sky);live=0;
        cal.field_0C=flags;cal.field_14=1;cal.field_110=1234;cal.daysSinceApr5=120+env;
        cal.period=env?3:4;cal.field_08=env+1;cal.field_18=env==1||env>=3;cal.field_1C=env==2||env==3?1:env==4?2:0;
        for(int i=0;i<5;i++) {
            MenuSlotAnimation *a=(void *)&cal.records[i];a->startX.f=-5-i;a->targetX.f=6+i;
            a->startY.f=2+i;a->targetY.f=10-i;a->targetAlpha=255;a->targetScaleX=a->targetScaleY=4096;
            a->startFrame.s=i;a->endFrame.s=i+20;
        }
        cal.records[0].field_2C=cal.records[0].field_28; /* zero-duration animator, not a backdrop divisor */
        for(int tick=0;tick<240;tick++) {
            if(tick==80)cal.field_14=0;
            if(tick==160){cal.field_0C=flags;cal.field_14=1;}
            cal.period=(tick/30)%7;
            skip_frame=tick%3!=0;device[rwRENDERSTATEFOGENABLE]=tick&1;
            draws=allocations=0;memset(output,0,sizeof output);
            if(pass)new_calendar(0,&cal);else old_calendar(0,&cal);
            assert(!live && device[rwRENDERSTATEFOGENABLE]==(tick&1));
            if(!pass){expected[tick].state=cal;expected[tick].draws=draws;memcpy(expected[tick].output,output,sizeof output);
                memcpy(expected[tick].device,device,sizeof device);memcpy(expected[tick].sky,sky,sizeof sky);}
            else {
                assert(!memcmp(&cal,&expected[tick].state,sizeof cal));
                assert(draws==expected[tick].draws && !memcmp(output,expected[tick].output,sizeof output));
                assert(!memcmp(device,expected[tick].device,sizeof device) && !memcmp(sky,expected[tick].sky,sizeof sky));
                if(skip_frame)assert(!allocations);
                pairs++;
            }
        }
        if(pass)assert(calendar_ticks_skipped==160);
    }
    printf("%u paired calendar ticks: animation, visibility, device state and drawn sprites match\n",pairs);
}
'''
        self.check_32(code)


if __name__ == '__main__': unittest.main()
