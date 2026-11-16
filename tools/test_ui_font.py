"""Compare real 32-bit glyph/list state with skipped geometry enabled and disabled."""
import unittest
import psp_census as census
from ui_test_support import UiTestCase, function


class NativeFont(UiTestCase):

    def test_cache_vertices_and_text_progression(self):
        source = (census.DECOMP / 'src/frFontRaster.c').read_text()
        patched = source
        for old, new in census.FIXES['src/frFontRaster.c']:
            self.assertIn(old, patched)
            patched = patched.replace(old, new)
        original = source.replace('originX = (f32)x / 16.0f;', 'originX = (f32)x / 21.0f;').replace(
            'extentX = (f32)(u32)width;', 'extentX = (f32)((width * 12) >> 4);')
        include = (census.DECOMP / 'include').as_posix()
        code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define TYPE_H
typedef unsigned char u8; typedef signed char s8;
typedef unsigned short u16; typedef short s16;
typedef unsigned u32; typedef int s32;
typedef unsigned long long u64; typedef long long s64;
typedef float f32; typedef double f64;
'''
        for header in ['fr_font_objects.h', 'rw_camera_internal.h', 'member_access.h']:
            code += f'#include "{include}/{header}"\n'
        code += '#include "ui_quad.h"\n'
        code += source[source.index('typedef struct FrFontRasterEntry'):source.index('/* Defined below')]
        code += r'''
static RwCamera camera;
static u8 D_0063BC88[1];
static float uvs[8]={0,0,1,0,0,1,1,1};
#define D_0063BCC0 ((u8 *)uvs)
static float D_008872F8[1]={65535};
static unsigned DAT_00881750_abs[1];
static int skip_frame, cameras, binds, submits, frees, acquires, sounds;
static unsigned last_texture;
static float captured[64];
static FrFontGlyphData glyph;
static FrFontGlyphRaster rasters[2];
static FrFontRasterEntry entries[2];
static int kwlnGetCamera(void) {cameras++;return (int)&camera;}
static void kAssertFail(const void *p,unsigned line) {assert(0);}
static s32 frFontFreeListInsert(u8 *slot) {frees++;return 0;}
static u8 *frFontAcquireGlyph(u8 *g, unsigned key) {
    acquires++; rasters[1].references++; entries[1].references++;
    return (u8 *)&rasters[1];
}
static void state(unsigned s,unsigned v) {binds++;last_texture=v;}
static int submit(int p,void *v,int n) {
    assert(p==4 && n==4);
    if (!skip_frame) {
        submits++;memcpy(captured,v,sizeof captured);
        for(int i=0;i<4;i++) captured[i*16+6]=0; /* reciprocal depth is not consumed by this renderer */
    }
    return 1;
}
static void (*D_00887300[])(unsigned,unsigned)={state};
static int (*D_00887310[])(int,void *,int)={submit};
int p4_ui_font, p4_ui_sprite;
int p4_ui_quad_draw(const P4UiVertex *v) {
    float ps2[64]={0};
    for(int i=0;i<4;i++) {
        float *p=ps2+i*16;unsigned c=v[i].u.els.colour;
        p[0]=v[i].u.els.screen.x;p[1]=v[i].u.els.screen.y;p[2]=v[i].u.els.screen.z;
        p[4]=v[i].u.els.u;p[5]=v[i].u.els.v;
        p[8]=c&255;p[9]=c>>8&255;p[10]=c>>16&255;p[11]=c>>24;
    }
    return submit(4,ps2,4);
}
'''
        rw = (census.ROOT / 'port/rwsky.c').read_text()
        code += rw[rw.index('int p4_font_skip ='):rw.index('int p4_calendar_skip_prepare')]
        code += function(original, 'frFontRasterDrawGlyph').replace('frFontRasterDrawGlyph(', 'old_glyph(')
        code += function(patched, 'frFontRasterDrawGlyph').replace('frFontRasterDrawGlyph(', 'new_glyph(')
        native = census.native_ui(patched, 'src/frFontRaster.c')
        code += function(native, 'p4_native_frFontRasterDrawGlyph(')
        code += r'''
static int candidate;
static int frFontRasterDrawGlyph(int x,int y,float z,u8 *g,int p,unsigned w,unsigned h,unsigned c) {
    if(candidate==2) return p4_native_frFontRasterDrawGlyph(x,y,z,g,p,w,h,c);
    return candidate ? new_glyph(x,y,z,g,p,w,h,c) : old_glyph(x,y,z,g,p,w,h,c);
}
static int D_00763810=0x40,D_0076459C;
static unsigned D_00887300_abs[1];
static void RpSkyRenderStateSet(int a,int b) {}
static int sdkSndIsChannelPlaying(int channel) {sounds++;return 0;}
static u8 *frFontReadyNode(u8 *p) {return ((FrFontTextRun *)p)->duration ? p : 0;}
'''
        font = (census.DECOMP / 'src/frFont.c').read_text()
        code += function(font, 'frFontStepNodeIntensity')
        code += function(font, 'frFontDrawList(')
        code += r'''
static void reset(int references,int palette,unsigned rgba) {
    memset(&glyph,0,sizeof glyph);memset(rasters,0,sizeof rasters);memset(entries,0,sizeof entries);
    for(int i=0;i<2;i++) {rasters[i].cacheEntry=(int)&entries[i];rasters[i].slot=123+i;entries[i].texture=456+i;}
    rasters[0].references=references; entries[0].references=references;entries[0].palette=palette;
    entries[0].key=66;entries[0].flags=0x80;
    entries[1].palette=1;entries[1].flags=0x40;
    glyph.raster=(int)&rasters[0];glyph.width=23;glyph.height=27;glyph.advance=23;glyph.color=rgba;
    camera.nearPlane=0.125f;DAT_00881750_abs[0]=9;
    cameras=binds=submits=frees=acquires=sounds=0;last_texture=0;D_0076459C=0;
    memset(captured,0,sizeof captured);font_glyphs_skipped=0;
}
typedef struct {
    FrFontGlyphData glyph;FrFontGlyphRaster rasters[2];FrFontRasterEntry entries[2];
    unsigned live,texture; int binds,submits,frees,acquires,sounds,result,delay;
    FrFontTextRun run;float vertices[64];
} Snapshot;
static Snapshot snap(int result,FrFontTextRun *run) {
    Snapshot s={0};s.glyph=glyph;memcpy(s.rasters,rasters,sizeof rasters);memcpy(s.entries,entries,sizeof entries);
    s.live=DAT_00881750_abs[0];s.texture=last_texture;s.binds=binds;s.submits=submits;
    s.frees=frees;s.acquires=acquires;s.sounds=sounds;s.result=result;s.delay=D_0076459C;
    if(run)s.run=*run;memcpy(s.vertices,captured,sizeof captured);return s;
}
int main(void) {
    Snapshot reference,actual,steps[80];FrFontTextRun run;
    unsigned cases=0,colors[]={0,0x12345601,0xABCDEF80,0xffffffff};
    assert(sizeof(void*)==4 && sizeof(FrFontGlyphRaster)==16 && sizeof(FrFontRasterEntry)==0x21c);
    for(int enabled=0;enabled<2;enabled++) for(int skip=0;skip<2;skip++)
    for(int palette=0;palette<2;palette++) for(int refs=1;refs<=2;refs++) for(int color=0;color<4;color++) {
        skip_frame=skip;p4_font_skip=enabled;
        reset(refs,palette,colors[color]);
        int result=old_glyph(155,77,15,(u8 *)&glyph,1,23,27,colors[color]);reference=snap(result,0);
        reset(refs,palette,colors[color]);
        result=new_glyph(155,77,15,(u8 *)&glyph,1,23,27,colors[color]);actual=snap(result,0);
        assert(!memcmp(&reference,&actual,sizeof actual));
        assert(cameras==(!(enabled&&skip)) && font_glyphs_skipped==(enabled&&skip));cases++;
        reset(refs,palette,colors[color]);
        result=p4_native_frFontRasterDrawGlyph(155,77,15,(u8 *)&glyph,1,23,27,colors[color]);actual=snap(result,0);
        assert(!memcmp(&reference,&actual,sizeof actual) && !cameras);
    }
    p4_font_skip=1;
    for(int pass=0;pass<3;pass++) {
        candidate=pass;reset(1,0,0x12345600);memset(&run,0,sizeof run);
        run.root=(int)&run;run.firstGlyph=(int)&glyph;run.glyphCount=1;run.intensity=3;
        run.depth.scale=12;run.duration=12;run.command=0xF222;glyph.character='M';glyph.effect=1;
        for(int tick=0;tick<80;tick++) {
            skip_frame=tick%3!=0;
            if(tick==10)glyph.palette=1;
            if(tick==40){run.duration=-1;run.command=0xF223;}
            if(tick==55)D_0076459C=3;
            int result=frFontDrawList(&run,0,0);Snapshot s=snap(result,&run);
            if(!pass)steps[tick]=s;else assert(!memcmp(&steps[tick],&s,sizeof s));
        }
    }
    assert(font_glyphs_skipped>0 && glyph.frame>0 && sounds>0);
    printf("%u glyph cases and 80 text ticks: old, skip and native cache/vertices/text progression match\n",cases);
}
'''
        self.check_32(code)


if __name__ == '__main__':
    unittest.main()
