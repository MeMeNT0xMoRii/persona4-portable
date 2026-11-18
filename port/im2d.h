/* Immediate 2D (moved from rwsky.c, b322; included once, at the same place): Im2D draws in PS2 screen space,
   feedback captures, alpha masks, GS alpha-test modes, TV rings, silhouette tint, the FPS overlay digits. */
/* Immediate 2D. Vertices are the game's 64-byte RwSky2DVertex in PS2 screen space, 640 by 448. */
typedef struct { float x, y, z, camZ, u, v, recipZ, pad, r, g, b, a, nx, ny, nz, pad2; } Vertex2D;
typedef struct { float u, v; unsigned colour; float x, y, z; } GuVertex;
#include "feedback.h"
#include "ui_quad.h"

/* PSPLINK A/B control: keep the original pass available for image comparisons. */
int p4_feedback_sprites = 1;

static int pow2_at_least(int n)
{
    int size = 1;
    while (size < n)
        size <<= 1;
    return size;
}

static float texture_w, texture_h;

static void capture_feedback(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE,
                   (void *)((unsigned)draw_buffer | 0x04000000), 0, 0, 512, feedback_pixels);
    sceGuTexSync();
    /* GS region-clamp stops at 639/447; extend those edge pixels to the GE texture's power-of-two boundary. */
    for (int x = SCREEN_W; x < 512;) {
        int n = x == SCREEN_W ? 1 : x - SCREEN_W;
        if (n > 512 - x) n = 512 - x;
        sceGuCopyImage(GU_PSM_8888, x == SCREEN_W ? SCREEN_W - 1 : SCREEN_W, 0, n, SCREEN_H,
                       512, feedback_pixels, x, 0, 512, feedback_pixels);
        sceGuTexSync();
        x += n;
    }
    for (int y = SCREEN_H; y < 512;) {
        int n = y == SCREEN_H ? 1 : y - SCREEN_H;
        if (n > 512 - y) n = 512 - y;
        sceGuCopyImage(GU_PSM_8888, 0, y == SCREEN_H ? SCREEN_H - 1 : SCREEN_H, 512, n,
                       512, feedback_pixels, 0, y, 512, feedback_pixels);
        sceGuTexSync();
        y += n;
    }
    sceGuTexFlush();
}

/* Half-size screen copy in free VRAM past the depth buffer (0x154000..0x194000). The dream's ~90 fog puffs each sample
   the screen copy: from a 1 MB RAM texture that cost ~50 ms of GE per frame. The puffs are soft, a quarter of the texels
   from VRAM keeps the look. */
enum { HALF_W = SCREEN_W / 2, HALF_H = SCREEN_H / 2, HALF_VRAM = 2 * STRIDE * SCREEN_H * 4 + STRIDE * SCREEN_H * 2 };
/* 0: full-size copy in RAM, as b60; 1: full-size copy in VRAM (same pixels); 2: half size in VRAM */
int p4_feedback_half = 1;

static void capture_feedback_vram(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    void *dst = (void *)(0x04000000 + HALF_VRAM), *src = (void *)((unsigned)draw_buffer | 0x04000000);
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE, src, 0, 0, STRIDE, dst);
    sceGuTexSync();
    /* one column and one row past the picture repeat its edge, for bilinear reads at the border */
    sceGuCopyImage(GU_PSM_8888, SCREEN_W - 1, 0, 1, SCREEN_H, STRIDE, dst, SCREEN_W, 0, STRIDE, dst);
    sceGuTexSync();
    sceGuCopyImage(GU_PSM_8888, 0, SCREEN_H - 1, SCREEN_W + 1, 1, STRIDE, dst, 0, SCREEN_H, STRIDE, dst);
    sceGuTexSync();
    sceGuTexFlush();
}
static int feedback_half_now;

static void capture_feedback_half(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    static const int states[] = { GU_DEPTH_TEST, GU_BLEND, GU_ALPHA_TEST, GU_STENCIL_TEST, GU_FOG, GU_CULL_FACE, GU_LIGHTING };
    int was[sizeof states / sizeof *states];
    for (unsigned i = 0; i < sizeof states / sizeof *states; i++) {
        was[i] = sceGuGetStatus(states[i]);
        sceGuDisable(states[i]);
    }
    sceGuDrawBufferList(GU_PSM_8888, (void *)HALF_VRAM, 256);
    sceGuScissor(0, 0, 256, 256);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 512, STRIDE, (void *)((unsigned)draw_buffer | 0x04000000));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    /* 32-pixel strips for the texture cache, plus one column and one row repeating the edge for bilinear reads */
    typedef struct { float u, v; float x, y, z; } V;
    int strips = HALF_W / 32 + 2;
    V *v = sceGuGetMemory(strips * 2 * sizeof(V));
    int n = 0;
    for (int x = 0; x < HALF_W; x += 32, n += 2) {
        v[n] = (V){ x * 2.0f, 0, x, 0, 0 };
        v[n + 1] = (V){ (x + 32) * 2.0f, SCREEN_H, x + 32, HALF_H, 0 };
    }
    v[n] = (V){ SCREEN_W - 1.0f, 0, HALF_W, 0, 0 };
    v[n + 1] = (V){ SCREEN_W - 1.0f, SCREEN_H, 256, HALF_H, 0 };
    v[n + 2] = (V){ 0, SCREEN_H - 1.0f, 0, HALF_H, 0 };
    v[n + 3] = (V){ SCREEN_W, SCREEN_H - 1.0f, 256, HALF_H + 8, 0 };
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n + 4, 0, v);
    sceGuDrawBufferList(GU_PSM_8888, draw_buffer, STRIDE);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    for (unsigned i = 0; i < sizeof states / sizeof *states; i++)
        if (was[i])
            sceGuEnable(states[i]);
    sceGuTexFlush();
}

/* The bound texture's smallest alpha for draw_tested: 255 without a texture, 0 when not known. */
static unsigned bound_alpha_min, bound_alpha_max = 255;
/* wrap: GU_REPEAT or GU_CLAMP for both axes, or TEX_WRAP_UV(u, v) for one each (PTank textures). */
#define TEX_WRAP_UV(u, v) (2 + (u) + 2 * (v))
static void tex_wrap(int wrap)
{
    if (wrap < 2)
        sceGuTexWrap(wrap, wrap);
    else
        sceGuTexWrap((wrap - 2) & 1, (wrap - 2) >> 1);
}
static void bind_raster_body(Raster *r, int wrap);
/* b374 p4_tex2d_census 1 (poke): the 2D draws' textures by depth and size over the next window (256 frames), to find
   32-bit images (portraits, UI) worth a CLUT8 copy: "tex2d" lines, the biggest 12 by texel bytes x binds. */
int p4_tex2d_census = 0;
static int tex2d_now;
enum { TEX2D_SLOTS = 128 };
static struct { const Raster *r; unsigned short w, h; unsigned char depth; unsigned binds; } *tex2d;
static void tex2d_note(const Raster *r)
{
    if (!r || !r->originalPixels || (!tex2d && !(tex2d = calloc(TEX2D_SLOTS, sizeof *tex2d))))
        return;
    for (int i = 0; i < TEX2D_SLOTS; i++)
        if (tex2d[i].r == r || !tex2d[i].r) {
            tex2d[i].r = r, tex2d[i].w = r->width, tex2d[i].h = r->height, tex2d[i].depth = r->depth, tex2d[i].binds++;
            return;
        }
}
static void tex2d_report(void)
{
    if (!tex2d)
        return;
    unsigned by_depth[4] = { 0 }, bytes_depth[4] = { 0 };
    for (int i = 0; i < TEX2D_SLOTS && tex2d[i].r; i++) {
        int d = tex2d[i].depth == 32 ? 3 : tex2d[i].depth == 16 ? 2 : tex2d[i].depth == 8 ? 1 : 0;
        by_depth[d]++, bytes_depth[d] += tex2d[i].w * tex2d[i].h * tex2d[i].depth / 8;
    }
    p4_log("tex2d textures by depth 4/8/16/32: %u %u %u %u, KB %u %u %u %u\n", by_depth[0], by_depth[1], by_depth[2], by_depth[3],
           bytes_depth[0] >> 10, bytes_depth[1] >> 10, bytes_depth[2] >> 10, bytes_depth[3] >> 10);
    for (int k = 0; k < 12; k++) {
        int best = -1;
        unsigned best_cost = 0;
        for (int i = 0; i < TEX2D_SLOTS && tex2d[i].r; i++) {
            unsigned cost = tex2d[i].w * tex2d[i].h * tex2d[i].depth / 8 * (tex2d[i].binds ? 1 : 0);
            if (tex2d[i].binds && cost >= best_cost)
                best = i, best_cost = cost;
        }
        if (best < 0)
            break;
        p4_log("tex2d %dx%dx%d binds %u raster %x\n", tex2d[best].w, tex2d[best].h, tex2d[best].depth, tex2d[best].binds, (unsigned)tex2d[best].r);
        tex2d[best].binds = 0;
    }
    free(tex2d), tex2d = 0;
}
static void bind_raster(Raster *r, int wrap)
{
    if (tex2d_now && p4_tex2d_census)
        tex2d_note(r);
    unsigned kept = kept_textures;
    rtt_sampled += r && r != &display_raster && rtt_find(r); /* b330 */
    if (p4_clut32 && r && r->depth == 32 && r != &display_raster && !rtt_find(r))
        r = clut32_find(r); /* b370 */
    prof_begin(PROF_TBIND);
    bind_raster_body(r, wrap);
    prof_end(PROF_TBIND);
    textures_bound += kept_textures == kept;
}
static void bind_raster_body(Raster *r, int wrap)
{
    if (r != &display_raster && gu_mesh_mode) {
        const void *pixels = r ? r->originalPixels : 0, *palette = r ? r->palette : 0;
        if (gs.tex_known && gs.tex_raster == r && gs.tex_wrap == wrap && gs.tex_pixels == pixels && gs.tex_palette == palette &&
            gs.tex_generation == texture_generation) {
            kept_textures++;
            return;
        } /* bound_alpha_min, texture_w and texture_h are still this raster's */
        gs.tex_known = 1, gs.tex_raster = r, gs.tex_wrap = wrap, gs.tex_pixels = pixels, gs.tex_palette = palette;
        gs.tex_generation = texture_generation;
    }
    bound_alpha_min = 0, bound_alpha_max = 255;
    if (r == &display_raster) {
        gs.tex_known = 0;
        /* Sample this frame's scene before filtering; the front buffer would accumulate the previous frame's blur. */
        if (!feedback_captured) {
            feedback_half_now = p4_feedback_half;
            if (feedback_half_now == 2)
                capture_feedback_half();
            else if (feedback_half_now == 1)
                capture_feedback_vram();
            else
                capture_feedback();
            feedback_captured = 1;
        }
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(GU_PSM_8888, 0, 0, 0);
        /* b323 (from MeMeNT0xMoRii's fork, home23): mdlEffectDrawScreenTexture asks for point filtering on the
           unscaled copies and linear on the zoom layers; the reduced copy keeps its linear upscale. */
        int filter = render_state[RS_TEXTURE_FILTER] == RW_FILTER_NEAREST ? GU_NEAREST : GU_LINEAR;
        if (feedback_half_now == 1) {
            sceGuTexImage(0, 512, 512, STRIDE, (void *)(0x04000000 + HALF_VRAM));
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
            sceGuTexFilter(filter, filter);
            tex_wrap(wrap);
            texture_w = SCREEN_W;
            texture_h = SCREEN_H;
            return;
        }
        if (feedback_half_now == 2) {
            sceGuTexImage(0, 256, 256, 256, (void *)(0x04000000 + HALF_VRAM));
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            tex_wrap(wrap);
            texture_w = HALF_W;
            texture_h = HALF_H;
            return;
        }
        sceGuTexImage(0, 512, 512, 512, feedback_pixels);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
        sceGuTexFilter(filter, filter);
        tex_wrap(wrap);
        texture_w = SCREEN_W;
        texture_h = SCREEN_H;
        return;
    }
    const RttSlot *drawn = rtt_find(r);
    if (drawn) { /* a camera texture: sample its VRAM rectangle, power-of-two sized so 0..1 UVs cover it */
        st_enable(GU_TEXTURE_2D, 1);
        sceGuTexMode(drawn->psm, 0, 0, 0);
        sceGuTexImage(0, drawn->w, drawn->h, drawn->w, (void *)(0x04000000 + drawn->offset));
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
        tex_wrap(wrap);
        texture_w = drawn->w;
        texture_h = drawn->h;
        return;
    }
    if (!r || !r->originalPixels) {
        st_enable(GU_TEXTURE_2D, 0);
        texture_w = texture_h = 1;
        bound_alpha_min = bound_alpha_max = 255;
        return;
    }
    int psm = r->depth == 32 ? GU_PSM_8888 : r->depth == 16 ? GU_PSM_5551 : r->depth == 8 ? GU_PSM_T8 : GU_PSM_T4;
    st_enable(GU_TEXTURE_2D, 1);
    if (p4_ge_raw) { /* b365: the same words libpspgu sends, one TEXFLUSH at the end instead of one per call */
        if (r->depth <= 8) {
            ge_word(0xC5, GU_PSM_8888 | 0xff << 8);                                   /* CMODE */
            ge_word(0xB0, (unsigned)r->palette & 0xffffff);                            /* CBP */
            ge_word(0xB1, (unsigned)r->palette >> 8 & 0xf0000);                       /* CBW */
            ge_word(0xC4, 1 << (r->depth - 1 ? r->depth - 3 : 0));                     /* CLOAD */
        }
        const SwizzleHead *sw = swizzle_head(r->originalPixels);
        bound_alpha_min = sw ? sw->alpha_min : 0;
        bound_alpha_max = sw ? sw->alpha_max : 255;
        int levels = sw && p4_mipmaps ? sw->levels : 1;
        ge_word(0xC2, (levels - 1) << 16 | (sw != 0));                                 /* TMODE */
        ge_word(0xC3, psm);                                                             /* TPSM */
        const unsigned char *level = r->originalPixels;
        for (int l = 0; l < levels; l++) {
            int w = l ? r->width >> l : pow2_at_least(r->width), h = l ? r->height >> l : pow2_at_least(r->height), tbw;
            int size = 0;
            if (l) {
                int wbytes, rows;
                size = mip_level_size(r, l, &wbytes, &rows);
                tbw = wbytes * 8 / r->depth;
            } else
                tbw = sw ? sw->wbytes * 8 / r->depth : r->width;
            int ew = 9, eh = 9;
            while (ew > 0 && !(w >> ew & 1)) ew--;
            while (eh > 0 && !(h >> eh & 1)) eh--;
            ge_word(0xA0 + l, (unsigned)level & 0xffffff);                             /* TBPn */
            ge_word(0xA8 + l, ((unsigned)level >> 8 & 0x0f0000) | tbw);                /* TBWn */
            ge_word(0xB8 + l, eh << 8 | ew);                                            /* TSIZEn */
            level += l ? size : (sw ? sw->wbytes * sw->rows : 0);
        }
        ge_word(0xC9, GU_TCC_RGBA << 8 | GU_TFX_MODULATE);                             /* TFUNC */
        ge_word(0xC6, GU_LINEAR << 8 | (levels > 1 ? GU_LINEAR_MIPMAP_NEAREST : GU_LINEAR)); /* TFILTER */
        ge_word(0xC8, GU_TEXTURE_AUTO);                                                 /* TLEVEL, bias 0 */
        ge_word(0xC7, wrap < 2 ? (wrap << 8 | wrap) : (((wrap - 2) >> 1) << 8 | ((wrap - 2) & 1))); /* TWRAP */
        ge_word(0xCB, 0);                                                               /* TFLUSH */
        texture_w = r->width;
        texture_h = r->height;
        return;
    }
    if (r->depth <= 8) {
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(1 << (r->depth - 1 ? r->depth - 3 : 0), r->palette);
    }
    const SwizzleHead *swizzled = swizzle_head(r->originalPixels);
    bound_alpha_min = swizzled ? swizzled->alpha_min : 0;
    bound_alpha_max = swizzled ? swizzled->alpha_max : 255;
    int levels = swizzled && p4_mipmaps ? swizzled->levels : 1;
    sceGuTexMode(psm, levels - 1, 0, swizzled != 0);
    sceGuTexImage(0, pow2_at_least(r->width), pow2_at_least(r->height), swizzled ? swizzled->wbytes * 8 / r->depth : r->width, r->originalPixels);
    const unsigned char *level = r->originalPixels + (swizzled ? swizzled->wbytes * swizzled->rows : 0);
    for (int l = 1; l < levels; l++) { /* swizzled and power of two: see mip_levels */
        int wbytes, rows, size = mip_level_size(r, l, &wbytes, &rows);
        sceGuTexImage(l, r->width >> l, r->height >> l, wbytes * 8 / r->depth, level);
        level += size;
    }
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(levels > 1 ? GU_LINEAR_MIPMAP_NEAREST : GU_LINEAR, GU_LINEAR);
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
    tex_wrap(wrap);
    texture_w = r->width;
    texture_h = r->height;
}

static void bind_texture(void) { tex2d_now = 1; bind_raster((Raster *)render_state[1], GU_CLAMP); tex2d_now = 0; }

/* ALPHA_1 is (A - B) * C + D with 0 source, 1 destination, 2 zero. */
enum { BLEND_NORMAL = 0x44, BLEND_ADD = 0x48, BLEND_SUBTRACT = 0x42, BLEND_REVERSE = 0x06 };

/* Any ALPHA_1 (b165; before, seven known values and the rest drawn as normal alpha, so the field Shadow's dark
   "black hole" drew as an opaque yellow cylinder). A, B, D: 0 source colour, 1 destination, 2 zero; C: 0 source
   alpha, 1 destination alpha, 2 FIX (taken as 1.0: the word kept here has no FIX byte). The result is
   Cs * ks + Cd * kd, each k being (a - b) * F + d, mapped onto the GE's ADD / SUBTRACT / REVERSE_SUBTRACT with
   F, 1 - F, 0 or 1 as factors. Only 1 + F has no GE factor: drawn as 1. */
enum { K_ZERO, K_ONE, K_F, K_ONE_MINUS_F };
int p4_blend_fix = 0; /* ALPHA_1's FIX, GS scale (0x80 = 1.0); 0 as the game's 32-bit state words leave it */
static unsigned blend_unusual[8], blend_unusual_n; /* modes outside the old seven, for the log */
void p4_blend_report(void)
{
    static unsigned reported;
    if (blend_unusual_n > reported) {
        for (unsigned i = reported; i < blend_unusual_n; i++)
            p4_log("blend new ALPHA_1 mode %02x\n", blend_unusual[i]);
        reported = blend_unusual_n;
    }
}
static void blend_factor(int a, int b, int d, int *kind, int *negative)
{
    int f = a - b;
    *negative = 0;
    if (!f)
        *kind = d ? K_ONE : K_ZERO;
    else if (f > 0)
        *kind = d ? K_ONE : K_F; /* 1 + F: no GE factor */
    else if (d)
        *kind = K_ONE_MINUS_F;
    else
        *kind = K_F, *negative = 1;
}
static void set_blend(unsigned alpha)
{
    unsigned m = alpha & 0xff, A = m & 3, B = m >> 2 & 3, C = m >> 4 & 3, D = m >> 6 & 3;
    if (m != 0x44 && m != 0x48 && m != 0x42 && m != 0x06 && m != 0x52 && m != 0x54 && m != 0x58) {
        int seen = 0;
        for (unsigned i = 0; i < blend_unusual_n; i++)
            seen |= blend_unusual[i] == m;
        if (!seen && blend_unusual_n < 8)
            blend_unusual[blend_unusual_n++] = m;
    }
    int ks, kd, ns, nd;
    blend_factor(A == 0, B == 0, D == 0, &ks, &ns);
    blend_factor(A == 1, B == 1, D == 1, &kd, &nd);
    int f = C == 1 ? GU_DST_ALPHA : GU_SRC_ALPHA, omf = C == 1 ? GU_ONE_MINUS_DST_ALPHA : GU_ONE_MINUS_SRC_ALPHA;
    /* C = FIX: a constant, GE GU_FIX colours. The game passes ALPHA_1's low word only (0x64 in mdlDrawSkyPass), so
       FIX, bits 32-39 of the register, is 0 on the PS2: the arena floor's sky pass changes no colour. */
    unsigned fix = p4_blend_fix * 2 > 255 ? 255 : p4_blend_fix * 2, fix_rgb = fix * 0x010101u, omfix_rgb = (255 - fix) * 0x010101u;
    if (C == 2)
        f = omf = GU_FIX;
    int src = ks == K_F ? f : ks == K_ONE_MINUS_F ? omf : GU_FIX, dst = kd == K_F ? f : kd == K_ONE_MINUS_F ? omf : GU_FIX;
    unsigned src_fix = ks == K_ONE ? 0xffffff : C == 2 && ks == K_F ? fix_rgb : C == 2 && ks == K_ONE_MINUS_F ? omfix_rgb : 0;
    unsigned dst_fix = kd == K_ONE ? 0xffffff : C == 2 && kd == K_F ? fix_rgb : C == 2 && kd == K_ONE_MINUS_F ? omfix_rgb : 0;
    int op = ns && !nd ? GU_REVERSE_SUBTRACT : nd && !ns ? GU_SUBTRACT : GU_ADD;
    if (ns && nd) /* both negative: the GS clamps to 0 */
        src = dst = GU_FIX, src_fix = dst_fix = 0;
    if (p4_ge_raw) { /* b366: ALPHA (op, src, dst), SFIX, DFIX as libpspgu writes them */
        ge_word(0xDF, op << 8 | dst << 4 | src);
        ge_word(0xE0, src_fix);
        ge_word(0xE1, dst_fix);
    } else
        sceGuBlendFunc(op, src, dst, src_fix, dst_fix);
}

static void bind_blend(void)
{
    if (!render_state[12] && !sky_state[2]) {
        st_enable(GU_BLEND, 0);
        return;
    }
    st_enable(GU_BLEND, 1);
    set_blend(sky_state[2] ? sky_state[2] : BLEND_NORMAL);
}

/* b379 p4_keep_previous: a frame whose screen colour the game does not clear before it draws starts from the frame
   shown before it, as on the PS2, where the one draw buffer still holds it. The Velvet Room's new persona (fusion):
   only depth cleared (camera clear mask 2), the old picture darkened by 0x90-alpha tiles and screen-copy glow layers
   added; with our two alternating buffers each one kept its own history and the glow flickered at half the frame
   rate. The copy is one VRAM to VRAM sceGuCopyImage, in the list before the frame's first draw. 0: as before. */
int p4_keep_previous = 0; /* b384: 0 again - the fusion scene clears its colour every frame (mask 3; the mask 2 clear is a
   later camera), so the copy fixed nothing there and only cost GE time in Heaven (87 of 128 frames) */
static void keep_previous_copy(void)
{
    frame_colour_pending = 0;
    gu_dirty();
    void *shown = (void *)(0x04000000 | (unsigned)(draw_buffer ? (void *)0 : (void *)(STRIDE * SCREEN_H * 4)));
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE, shown, 0, 0, STRIDE, (void *)(0x04000000 | (unsigned)draw_buffer));
    sceGuTexSync();
    keep_previous_copies++;
}

static void begin_frame(void)
{
    if (!in_frame) {
        frame_colour_pending = 1;
        gu_dirty(); /* a new list */
        in_frame = 1;
        sceGuStart(GU_DIRECT, list);
        list_to_cached();
        sceGuDrawBufferList(GU_PSM_8888, draw_buffer, STRIDE); /* buffers alternate here, not through sceGuSwapBuffers */
        if (rtt_restore) {
            rtt_restore = 0;
            rtt_bind_target(0);
        }
    }
    if (frame_colour_pending && !in_camera_clear) {
        if (p4_keep_previous)
            keep_previous_copy();
        else
            frame_colour_pending = 0;
    }
}

static unsigned byte_colour(float c)
{
    return c <= 0 ? 0 : c >= 255 ? 255 : (unsigned)c;
}

static void fps_draw(void)
{
    static const unsigned short glyphs[] = {
        0x7b6f, 0x2492, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf,
        0x0002, 0x79e4,
    };
    enum { FIRST_X = 4, FIRST_Y = SCREEN_H - 14 };
    unsigned value = fps_tenths;
    if (value > 999) value = 999;
    unsigned glyph[5] = {11, value / 100, value / 10 % 10, 10, value % 10};
    typedef struct { unsigned colour; short x, y, z, pad; } FpsVertex;
    const int format = GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D;
    FpsVertex *vertices = sceGuGetMemory((1 + 5 * 15) * 2 * sizeof *vertices);
    vertices[0] = (FpsVertex){0xff202020, FIRST_X, FIRST_Y, 0, 0};
    vertices[1] = (FpsVertex){0xff202020, FIRST_X + 44, FIRST_Y + 12, 0, 0};
    unsigned count = 2;
    for (unsigned i = 0; i < 5; i++) {
        if (i == 1 && !glyph[i]) continue;
        unsigned bits = glyphs[glyph[i]];
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++) {
                if (!(bits & (1u << (14 - row * 3 - col)))) continue;
                int x = FIRST_X + 2 + i * 8 + col * 2;
                int y = FIRST_Y + 1 + row * 2;
                vertices[count++] = (FpsVertex){0xffffffff, x, y, 0, 0};
                vertices[count++] = (FpsVertex){0xffffffff, x + 2, y + 2, 0, 0};
            }
    }
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDepthMask(GU_TRUE);
    sceGuPixelMask(0xff000000);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuDrawArray(GU_SPRITES, format, count, 0, vertices);
}

static void put_vertex(GuVertex *out, const Vertex2D *in)
{
    out->u = in->u * texture_w;
    out->v = in->v * texture_h;
    out->colour = byte_colour(in->r) | byte_colour(in->g) << 8 | byte_colour(in->b) << 16 | byte_colour(in->a) << 24;
    out->x = in->x * target_sx;
    out->y = in->y * target_sy;
    out->z = in->z <= 0 ? 0 : in->z >= 65535 ? 65535 : in->z;
}

/* b332: a field shadow's fade. In a camera texture the GS draws the silhouette black, then an untextured fan with DATE
   writes an alpha gradient over it (Junes: 0 -> 0x80, rows 48-128). The GE writes no per-pixel alpha here (the alpha
   byte is the stencil that carries DATE), so the fan writes its alpha into RGB instead, unblended; the floor quad that
   samples the texture then darkens by RGB (rwsky.c im3d_draw). 0: an even black shadow, as b331. */
int p4_shadow_fade = 1;
static void shadow_fade(GuVertex *out, int count)
{
    if (!p4_shadow_fade || !rtt_target || !(sky_state[3] & 1 << 14) || render_state[1])
        return;
    for (int i = 0; i < count; i++)
        if (out[i].colour & 0xffffff)
            return;
    for (int i = 0; i < count; i++) {
        unsigned a = out[i].colour >> 24;
        out[i].colour = a << 24 | a << 16 | a << 8 | a;
    }
    sceGuDisable(GU_BLEND);
    rtt_target->fade = 1;
}

static const int gu_prim[7] = { GU_POINTS, GU_LINES, GU_LINE_STRIP, GU_TRIANGLES, GU_TRIANGLE_STRIP, GU_TRIANGLE_FAN, GU_POINTS };
#include "alpha_mask.h"

static int draw_body(int prim, const Vertex2D *vertices, const unsigned short *indices, int count);
static int draw(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    if (SKIPPING)
        return 1;
    if (p4_probe_flags & 2)
        return 1;
    prof_begin(PROF_2D);
    if (gate_closed)
        gate_poll();
    int timed = ge_time_begin();
    rtt_2d += rtt_target != 0;
    int r = draw_body(prim, vertices, indices, count);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
    return r;
}

/* b146 census of 2D draws (p4_probe_2d): per texture x blend x pixel mask, draws and the screen area of their bounding
   boxes (a proxy for fill). The title's logo phase: ~36 ms of GE in 2D a frame. */
int p4_probe_2d = 0;
int p4_2d_nan_drop = 1;
float p4_panel_scale = 0.0f; /* b225: the battle panel frame's scale where retail leaves it unset (census FIXES); 0: nothing, as on the PS2 */
static unsigned nan_2d_dropped;
unsigned p4_2d_nan_report(void) { unsigned n = nan_2d_dropped; nan_2d_dropped = 0; return n; }
typedef struct { const void *raster; unsigned blend, mask, prim, calls, w, h; float area; } Census2D;
static Census2D *census2d; /* b260: heap on first use (.bss is full); census2d_shown (declared above): single draws logged in this window */
static void census2d_note(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < count; i++) {
        const Vertex2D *v = &vertices[indices ? indices[i] : i];
        if (v->x < x0) x0 = v->x;
        if (v->x > x1) x1 = v->x;
        if (v->y < y0) y0 = v->y;
        if (v->y > y1) y1 = v->y;
    }
    const void *raster = (const void *)render_state[1];
    unsigned blend = sky_state[2] & 0xff, mask = pixel_mask;
    /* b165: single draws for the empty minimap. 2: draws smaller than 1/16 of the screen; 3: every draw. */
    /* b220: 4 = textured draws only, with their first vertices (the battle's attack cursor: stripes on the PSP) */
    if (p4_probe_2d == 4 && census2d_shown < 48 && raster != (const void *)&display_raster &&
        (x1 - x0) * (y1 - y0) >= (float)p4_probe_keep_every) { /* p4_probe_keep_every: the smallest box area (PS2 pixels) logged */
        census2d_shown++;
        const Raster *tr = raster;
        p4_log("census2d tex prim %d n %d box %d,%d-%d,%d raster %p %dx%d blend %02x mask %08x test %08x z %d ztest %d\n", prim, count,
               (int)x0, (int)y0, (int)x1, (int)y1, raster, tr ? tr->width : 0, tr ? tr->height : 0, blend, mask, sky_state[3], (int)vertices[0].z,
               render_state[RS_ZTEST]);
        for (int i = 0; i < count && i < 16; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            p4_log("census2d   v%d xy %d %d (x10) uv %d %d (x1000) rgba %d %d %d %d\n", indices ? indices[i] : i, (int)(v->x * 10),
                   (int)(v->y * 10), (int)(v->u * 1000), (int)(v->v * 1000), (int)v->r, (int)v->g, (int)v->b, (int)v->a);
        }
    }
    if (p4_probe_2d >= 2 && p4_probe_2d <= 3 && census2d_shown < 64 && (p4_probe_2d == 3 || (x1 - x0) * (y1 - y0) < 640 * 448 / 16)) {
        const Vertex2D *v = &vertices[indices ? indices[0] : 0];
        const Raster *tr = raster && raster != (const void *)&display_raster ? raster : 0;
        census2d_shown++;
        p4_log("census2d draw prim %d n %d box %d,%d-%d,%d raster %p %dx%d uv %d,%d blend %02x mask %08x rgba %d %d %d %d z %d ztest %d zwrite %d at %08x\n",
               prim, count, (int)x0, (int)y0, (int)x1, (int)y1, raster, tr ? tr->width : 0, tr ? tr->height : 0,
               (int)(v->u * 1000), (int)(v->v * 1000), blend, mask, (int)v->r, (int)v->g, (int)v->b, (int)v->a, (int)v->z,
               render_state[RS_ZTEST], render_state[RS_ZWRITE], sky_state[3]);
    }
    if (!census2d && !(census2d = calloc(64, sizeof *census2d)))
        return;
    for (int i = 0; i < 64; i++) {
        Census2D *e = &census2d[i];
        if (e->calls && (e->raster != raster || e->blend != blend || e->mask != mask))
            continue;
        if (!e->calls) {
            e->raster = raster, e->blend = blend, e->mask = mask, e->prim = prim;
            const Raster *r = raster && raster != (const void *)&display_raster ? raster : 0;
            e->w = r ? r->width : raster ? 1 : 0, e->h = r ? r->height : 0;
        }
        e->calls++;
        e->area += (x1 - x0) * (y1 - y0) / (640.0f * 448.0f);
        return;
    }
}
static void census2d_report(unsigned census2d_frames)
{
    if (!p4_probe_2d || !census2d_frames || !census2d)
        return;
    for (int k = 0; k < 12; k++) {
        Census2D *best = 0;
        for (int i = 0; i < 64; i++)
            if (census2d[i].calls && (!best || census2d[i].area > best->area))
                best = &census2d[i];
        if (!best)
            break;
        p4_log("census2d %2d raster %p %s %ux%u blend %02x mask %08x prim %u: draws %u screens %d.%02d per frame\n", k, best->raster,
               best->raster == (const void *)&display_raster ? "SCREEN" : "", best->w, best->h, best->blend, best->mask, best->prim,
               best->calls / census2d_frames, (int)(best->area / census2d_frames), (int)(best->area * 100 / census2d_frames) % 100);
        best->calls = 0;
    }
    memset(census2d, 0, 64 * sizeof *census2d);
    census2d_shown = 0;
}

/* GS alpha test with AFAIL = FB_ONLY on a draw that writes depth: the only case where ignoring ATEST changes depth. */
int p4_gs_alpha_test = 1; /* 0: ATEST ignored in 2D, as before b165 */
static unsigned gs_alpha_splits;
unsigned p4_gs_alpha_splits(void) { unsigned n = gs_alpha_splits; gs_alpha_splits = 0; return n; }
static int gs_alpha_fb_only(void)
{
    unsigned at = sky_state[3];
    return p4_gs_alpha_test && (at & 1) && (at >> 1 & 7) != 1 && (at >> 12 & 3) == 1 && render_state[RS_ZWRITE] &&
           !(p4_alpha_write_test && pixel_mask == 0x00ffffff);
}

/* b312 (from MeMeNT0xMoRii's fork): the TV-world entry rings drawn as the retail's two fans through the stencil
   (p4_tv_ring), and a silhouette tinted by its texture alpha (p4_texture_tint, shdMisc.c via census FIXES).
   0: the game's own paths, as b311. */
int p4_tv_ring = 1;
int p4_texture_tint = 1;
static float contour_clip_distance(const GuVertex *vertex, int plane)
{
    switch (plane) {
    case 0: return vertex->x;
    case 1: return target_w - vertex->x;
    case 2: return vertex->y;
    default: return target_h - vertex->y;
    }
}

enum { TV_RING_POINTS = 64, GU_2D_FORMAT = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D };

static void submit_tv_fan(int count, const GuVertex *vertices)
{
    /* Oversized final TV fans can extend past the GE's 2D coordinate range.
       Clip each triangle before it becomes a mask or is drawn through it. */
    int inside = 1;
    for (int i = 0; i < count; i++)
        inside &= vertices[i].x >= 0 && vertices[i].x <= target_w &&
                  vertices[i].y >= 0 && vertices[i].y <= target_h;
    if (inside) {
        GuVertex *out = sceGuGetMemory(count * sizeof *out);
        memcpy(out, vertices, count * sizeof *out);
        sceGuDrawArray(GU_TRIANGLE_FAN, GU_2D_FORMAT, count, 0, out);
        return;
    }

    GuVertex clipped[(TV_RING_POINTS - 2) * 15];
    int total = 0;
    for (int i = 1; i + 1 < count; i++) {
        GuVertex polygon[2][8] = { { vertices[0], vertices[i], vertices[i + 1] } };
        int length = 3;
        for (int plane = 0; plane < 4 && length >= 3; plane++) {
            const GuVertex *source = polygon[plane & 1];
            GuVertex *target = polygon[(plane + 1) & 1];
            int next = 0;
            for (int j = 0; j < length; j++) {
                const GuVertex *a = &source[j], *b = &source[j + 1 == length ? 0 : j + 1];
                float d = contour_clip_distance(a, plane), e = contour_clip_distance(b, plane);
                if (d >= 0)
                    target[next++] = *a;
                if ((d < 0 && e > 0) || (d > 0 && e < 0)) {
                    float t = d / (d - e);
                    target[next++] = (GuVertex){0, 0, a->colour, a->x + (b->x - a->x) * t,
                                                a->y + (b->y - a->y) * t, a->z};
                }
            }
            length = next;
        }
        for (int j = 1; j + 1 < length; j++) {
            clipped[total++] = polygon[0][0];
            clipped[total++] = polygon[0][j];
            clipped[total++] = polygon[0][j + 1];
        }
    }
    if (total) {
        size_t bytes = total * sizeof *clipped;
        GuVertex *out = sceGuGetMemory(bytes);
        memcpy(out, clipped, bytes);
        sceGuDrawArray(GU_TRIANGLES, GU_2D_FORMAT, total, 0, out);
    }
}

static void tv_ring_points(GuVertex *out, const float (*points)[2], int count, float x, float y,
                           float rotation, float sx, float sy, unsigned colour)
{
    float sine = sinf(rotation), cosine = cosf(rotation);
    for (int i = 0; i < count; i++) {
        float px = (points[i][0] - points[0][0]) * sx;
        float py = (points[i][1] - points[0][1]) * sy;
        out[i] = (GuVertex){0, 0, colour, (x + px * cosine + py * sine) * target_sx,
                            (y - px * sine + py * cosine) * target_sy, 0};
    }
}

/* Render the ring with the retail's two fans. The four colour layers share
   one opening, while the outer edges keep their original colour offsets. */
int p4_draw_tv_ring(const float (*outer)[2], int outer_count, const float (*inner)[2], int inner_count,
                    float x, float y, int ox, int oy, float rotation, float sx, float sy, unsigned rgb, unsigned alpha)
{
    if (!p4_tv_ring || outer_count < 3 || inner_count < 3 || outer_count > TV_RING_POINTS || inner_count > TV_RING_POINTS)
        return 0;
    if (!alpha || SKIPPING || (p4_probe_flags & 2))
        return 1;

    prof_begin(PROF_2D);
    if (gate_closed)
        gate_poll();
    int timed = ge_time_begin();
    gu_dirty();
    begin_frame();
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_TRUE);
    sceGuEnable(GU_STENCIL_TEST);
    sceGuPixelMask(0x7fffffff);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    sceGuStencilFunc(GU_ALWAYS, 0, 0x80);
    GuVertex *clear = sceGuGetMemory(2 * sizeof *clear);
    clear[0] = (GuVertex){0, 0, 0, 0, 0, 0};
    clear[1] = (GuVertex){0, 0, 0, target_w, target_h, 0};
    sceGuDrawArray(GU_SPRITES, GU_2D_FORMAT, 2, 0, clear);

    const float degrees = 0.0174532925199433f;
    float fringe = rgb == 0x76bf48 ? 4.0f : rgb == 0x4883bf ? 2.0f : rgb == 0xbf4848 ? -2.0f : 0.0f;
    GuVertex fan[TV_RING_POINTS];
    tv_ring_points(fan, inner, inner_count, x + ox, y + oy, (rotation - fringe) * degrees, sx, sy, 0);
    sceGuStencilFunc(GU_ALWAYS, 0x80, 0x80);
    submit_tv_fan(inner_count, fan);

    unsigned colour = (rgb & 255) << 16 | (rgb & 0xff00) | (rgb >> 16 & 255) | alpha << 24;
    tv_ring_points(fan, outer, outer_count, x + ox, y + oy, rotation * degrees, sx, sy, colour);
    sceGuPixelMask(0);
    sceGuStencilFunc(GU_EQUAL, 0, 0x80);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
    sceGuEnable(GU_BLEND);
    set_blend(BLEND_NORMAL);
    submit_tv_fan(outer_count, fan);
    sceGuDisable(GU_STENCIL_TEST);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
    return 1;
}


static void tint_sprite(GuVertex *out, float x, float y, float w, float h, unsigned colour)
{
    out[0] = (GuVertex){0, 0, colour, x * target_sx, y * target_sy, 0};
    out[1] = (GuVertex){texture_w, texture_h, colour, (x + w) * target_sx, (y + h) * target_sy, 0};
}

/* The GS stores the silhouette's texture alpha in the framebuffer before applying a flat tint.
   Use that texture alpha directly on the GE, where framebuffer alpha is the stencil. */
void p4_draw_texture_tint(unsigned rgba, float x, float y, float mask_x, float mask_y,
                          float w, float h, Raster *raster, int cutout, int additive)
{
    render_state[7] = 2;
    render_state[RS_ZTEST] = 0;
    render_state[RS_ZWRITE] = 0;
    render_state[14] = 0;
    render_state[9] = 2;
    render_state[12] = 1;
    render_state[1] = 0;
    pixel_mask = 0;
    sky_state[2] = additive ? 0x58 : 0x54;
    sky_state[3] = cutout ? 0x35801 : 0x31801;
    unsigned a = rgba & 255;
    if (!a || !raster || w <= 0 || h <= 0 || SKIPPING || (p4_probe_flags & 2))
        return;

    prof_begin(PROF_2D);
    if (gate_closed)
        gate_poll();
    int timed = ge_time_begin();
    gu_dirty();
    begin_frame();
    bind_depth();
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuPixelMask(0);

    int format = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;
    if (cutout) {
        /* DATE selects pixels outside the opaque part of the displaced silhouette. */
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_BLEND);
        sceGuEnable(GU_STENCIL_TEST);
        sceGuPixelMask(0x7fffffff);
        sceGuStencilFunc(GU_ALWAYS, 0, 0x80);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
        GuVertex *mask = sceGuGetMemory(4 * sizeof *mask);
        tint_sprite(mask, x, y, w, h, 0);
        sceGuDrawArray(GU_SPRITES, format, 2, 0, mask);

        tex2d_now = 1, bind_raster(raster, GU_CLAMP), tex2d_now = 0;
        tint_sprite(mask + 2, mask_x, mask_y, w, h, 0xffffffff);
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_EQUAL, 255, 0xff);
        sceGuStencilFunc(GU_ALWAYS, 0x80, 0x80);
        sceGuDrawArray(GU_SPRITES, format, 2, 0, mask + 2);

        sceGuDisable(GU_ALPHA_TEST);
        sceGuPixelMask(0);
        sceGuStencilFunc(GU_EQUAL, 0, 0x80);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
    } else {
        tex2d_now = 1, bind_raster(raster, GU_CLAMP), tex2d_now = 0;
    }

    unsigned colour = rgba >> 24 | (rgba >> 8 & 0xff00) | (rgba << 8 & 0xff0000) | a << 24;
    /* Equal vertex and environment RGB make the combiner constant across all texels. */
    sceGuTexEnvColor(colour);
    sceGuTexFunc(GU_TFX_BLEND, GU_TCC_RGBA);
    sceGuEnable(GU_BLEND);
    set_blend(additive ? BLEND_ADD : BLEND_NORMAL);
    GuVertex *out = sceGuGetMemory(2 * sizeof *out);
    tint_sprite(out, x, y, w, h, colour);
    sceGuDrawArray(GU_SPRITES, format, 2, 0, out);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
}


static void ui_draw_state(unsigned alpha)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    begin_frame();
    bind_depth();
    bind_mask_mode(alpha, p4_alpha_pixel || p4_2d_alpha_masks);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_ALPHA_TEST);
    bind_texture();
    bind_blend();
    /* An alpha-only write (RGB masked) of a texture: the PS2 stores each pixel's alpha, the PSP's stencil takes one
       value per draw. Keep that value off the pixels the texture leaves transparent, so the title's light blobs
       (added by destination alpha) stay off the letterbox bars instead of covering the whole quad. */
    if (p4_alpha_write_test && !p4_alpha_pixel && !p4_2d_alpha_masks && pixel_mask == 0x00ffffff && render_state[1] && (Raster *)render_state[1] != &display_raster) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0x40, 0xff);
    }
}

static int ui_draw_finish(int prim, int count, GuVertex *out)
{
    if (draw_2d_alpha_mask(prim, count, out))
        return 1;
    shadow_fade(out, count);
    P4FeedbackRect rect;
    if (p4_feedback_sprites && (Raster *)render_state[1] == &display_raster &&
        prim == GU_TRIANGLES && p4_feedback_rect(out, count, &rect)) {
        /* Small vertical sprites keep the linear framebuffer texture in the
           GE cache; retain the existing capture, filter, blend and mask. */
        GuVertex *strips = sceGuGetMemory(P4_FEEDBACK_MAX_VERTICES * sizeof *strips);
        int n = p4_feedback_emit(strips, &rect);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, strips);
    } else if (gs_alpha_fb_only()) {
        /* GS ATEST with AFAIL = FB_ONLY (b165): pixels that fail write colour but no depth. The minimap's frame
           (ATEST_1 0x717FB: alpha >= 0x7F) left its see-through middle free of depth, so the map tiles behind it
           (z 65535 - 60000) drew; written whole it hid them. Pass 1: passing pixels, depth as set; pass 2: the rest,
           colour only. */
        unsigned at = sky_state[3];
        static const unsigned char gu_atst[8] = { GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL };
        static const unsigned char gu_fail[8] = { GU_ALWAYS, GU_NEVER, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_EQUAL };
        int ref = (at >> 4 & 0xff) * 2; /* the GS's alpha 0x80 is 1.0, the GE's 0xff */
        if (ref > 255)
            ref = 255;
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(gu_atst[at >> 1 & 7], ref, 0xff);
        sceGuDrawArray(prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, out);
        sceGuAlphaFunc(gu_fail[at >> 1 & 7], ref, 0xff);
        sceGuDepthMask(GU_TRUE);
        sceGuDrawArray(prim, 0, count, 0, out); /* format set by pass 1 */
        sceGuDepthMask(GU_FALSE);
        sceGuDisable(GU_ALPHA_TEST);
        gs_alpha_splits++;
    } else {
        sceGuDrawArray(prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, out);
    }
    return 1;
}

static int draw_body(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    if (!list_room()) /* b325: the frame list is nearly full */
        return 1;
    translucent_flush();
    if (p4_probe_2d && count > 0)
        census2d_note(prim, vertices, indices, count);
    /* b223: 2D primitives with a non-finite position. The battle command panel (btlPanelDrawBackdropGrid, FUN_0020B3A0)
       passes a scale it never set once the panel has finished growing (retail: whatever $f20 held); here that is NaN,
       and the backdrop disc and its arcs went to the GE with NaN corners: the stripes across the attack cursor, PSP
       only (PPSSPP drops such triangles). p4_2d_nan_drop 1 leaves a draw out when any corner is not finite. */
    if (p4_2d_nan_drop) {
        for (int i = 0; i < count; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            if (!(v->x == v->x && v->y == v->y && v->x < 1e6f && v->x > -1e6f && v->y < 1e6f && v->y > -1e6f)) {
                static unsigned nan_logged;
                if (p4_probe_2d == 5 && nan_logged < 24) { /* b224: what these draws are, their corners as float bits */
                    nan_logged++;
                    const Vertex2D *f = &vertices[indices ? indices[0] : 0];
                    p4_log("nan2d prim %d n %d raster %p blend %02x test %08x rgba %d %d %d %d z %d\n", prim, count, (void *)render_state[1],
                           sky_state[2] & 0xff, sky_state[3], (int)f->r, (int)f->g, (int)f->b, (int)f->a, (int)f->z);
                    for (int k = 0; k < count && k < 48; k += 4) {
                        char line[160];
                        int at = 0;
                        for (int j = k; j < count && j < k + 4; j++) {
                            const Vertex2D *w = &vertices[indices ? indices[j] : j];
                            at += snprintf(line + at, sizeof line - at, " %08x,%08x", *(const unsigned *)&w->x, *(const unsigned *)&w->y);
                        }
                        p4_log("nan2d   %d:%s\n", k, line);
                    }
                }
                nan_2d_dropped++;
                return 1;
            }
        }
    }
    if (count <= 0 || prim < 1 || prim > 6) {
        gu_dirty();
        return 1;
    }
    ui_draw_state(byte_colour(vertices[indices ? indices[0] : 0].a));
    /* The PS2 blends these by the alpha the pass before wrote into the frame buffer. On the PSP that byte is the
       stencil, which ordinary draws leave alone, so the dream's screen-copy puffs showed as hard squares. Passes under
       half the screen are skipped; full-screen filters still draw. */
    unsigned blend = sky_state[2] & 0xff;
    int dst_alpha_pass = (blend == 0x52 || blend == 0x54 || blend == 0x58) && (Raster *)render_state[1] == &display_raster;
    /* the alpha-only writes that feed them (RGB masked, a textured puff): the stencil takes one value per draw, which
       is the square; without the pass that reads it they only cost fill */
    int alpha_writer = pixel_mask == 0x00ffffff && render_state[1] && (Raster *)render_state[1] != &display_raster;
    if (dst_small_frame != cache_frame)
        dst_small_last = dst_small_now, dst_small_now = 0, dst_small_frame = cache_frame;
    int many = p4_dst_alpha_many > 0 && dst_small_last > (unsigned)p4_dst_alpha_many;
    alpha_mask_low = many ? p4_alpha_mask_coarse : 1;
    if (!p4_alpha_pixel && (dst_alpha_pass || alpha_writer)) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i < count; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            if (v->x < x0) x0 = v->x;
            if (v->x > x1) x1 = v->x;
            if (v->y < y0) y0 = v->y;
            if (v->y > y1) y1 = v->y;
        }
        if ((x1 - x0) * (y1 - y0) < 640 * 448 / 2) { /* the battle's puffs cover 0.27 of the screen, filters all of it */
            dst_small_now++; /* b284: counted always; skipped when passes are off or the last frame had many */
            /* Noise textures also read these masks: keep them even when screen-feedback passes are skipped. */
            if (!p4_dst_alpha_passes ? (dst_alpha_pass || (alpha_writer && !p4_2d_alpha_masks)) :
                (many && (!alpha_writer || !p4_2d_alpha_masks))) {
                dst_alpha_skipped++;
                return 1;
            }
        }
    }
    GuVertex *out = sceGuGetMemory(count * sizeof *out);
    for (int i = 0; i < count; i++)
        put_vertex(&out[i], &vertices[indices ? indices[i] : i]);
    return ui_draw_finish(gu_prim[prim], count, out);
}

int p4_ui_font = 1, p4_ui_sprite = 1; /* b339: on by default (PSP b1332: +0.6..1.2 fps, about -2 ms CPU in the Margaret dialogue) */
int p4_ui_quad_draw(const P4UiVertex *vertices)
{
    if (SKIPPING || (p4_probe_flags & 2))
        return 1;
    /* Special passes and diagnostics retain all original bounds/probe behaviour. */
    if (p4_probe_2d || (Raster *)render_state[1] == &display_raster || pixel_mask == 0x00ffffff) {
        Vertex2D fallback[4] = {0};
        for (int i = 0; i < 4; i++) {
            const P4UiVertex *v = &vertices[i];
            fallback[i].x = v->u.els.screen.x;
            fallback[i].y = v->u.els.screen.y;
            fallback[i].z = v->u.els.screen.z;
            fallback[i].u = v->u.els.u;
            fallback[i].v = v->u.els.v;
            unsigned c = v->u.els.colour;
            fallback[i].r = c & 255;
            fallback[i].g = c >> 8 & 255;
            fallback[i].b = c >> 16 & 255;
            fallback[i].a = c >> 24;
        }
        return draw(4, fallback, 0, 4);
    }
    prof_begin(PROF_2D);
    if (gate_closed)
        gate_poll();
    int timed = ge_time_begin();
    rtt_2d += rtt_target != 0;
    if (!list_room()) {
        ge_time_end(timed, GT_2D);
        prof_end(PROF_2D);
        return 1;
    }
    translucent_flush();
    if (p4_2d_nan_drop) {
        for (int i = 0; i < 4; i++) {
            float x = vertices[i].u.els.screen.x, y = vertices[i].u.els.screen.y;
            if (!(x == x && y == y && x < 1e6f && x > -1e6f && y < 1e6f && y > -1e6f)) {
                nan_2d_dropped++;
                ge_time_end(timed, GT_2D);
                prof_end(PROF_2D);
                return 1;
            }
        }
    }
    ui_draw_state(vertices[0].u.els.colour >> 24);
    GuVertex *out = sceGuGetMemory(4 * sizeof *out);
    for (int i = 0; i < 4; i++) {
        const P4UiVertex *v = &vertices[i];
        out[i].u = v->u.els.u * texture_w;
        out[i].v = v->u.els.v * texture_h;
        out[i].colour = v->u.els.colour;
        out[i].x = v->u.els.screen.x * target_sx;
        out[i].y = v->u.els.screen.y * target_sy;
        float z = v->u.els.screen.z;
        out[i].z = z <= 0 ? 0 : z >= 65535 ? 65535 : z;
    }
    int result = ui_draw_finish(GU_TRIANGLE_STRIP, 4, out);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
    return result;
}

#include "cloud.h"

static int im2d_line(Vertex2D *v, int n, int a, int b)
{
    unsigned short pair[2] = { a, b };
    return draw(1, v, pair, 2);
}

static int im2d_triangle(Vertex2D *v, int n, int a, int b, int c)
{
    unsigned short three[3] = { a, b, c };
    return draw(3, v, three, 3);
}

static int im2d_primitive(int prim, Vertex2D *v, int n) { return draw(prim, v, 0, n); }

static int im2d_indexed(int prim, Vertex2D *v, int n, unsigned short *indices, int count)
{
    return draw(prim, v, indices, count);
}

/* The retail device table points at these names. */
int SkyIm2DRenderLine(Vertex2D *v, int n, int a, int b) { return im2d_line(v, n, a, b); }
int SkyIm2DRenderTriangle(Vertex2D *v, int n, int a, int b, int c) { return im2d_triangle(v, n, a, b, c); }
int SkyIm2DRenderPrimitive(int prim, Vertex2D *v, int n) { return im2d_primitive(prim, v, n); }
int SkyIm2DRenderIndexedPrimitive(int prim, Vertex2D *v, int n, unsigned short *indices, int count) { return im2d_indexed(prim, v, n, indices, count); }

