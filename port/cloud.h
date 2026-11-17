/* DistortParticleDrawSky's noise pair owns scratch alpha: its reader immediately consumes its writer.
   Keep the soft mask in texture alpha instead, as P3P's shadow particles do. */
int p4_cloud = 1;
static unsigned cloud_draws, cloud_fallbacks, cloud_builds;
static unsigned cloud_size_refused, cloud_state_refused, cloud_geometry_refused, cloud_busy;
enum { CLOUD_INPUT = 4096 + 8192 + 1024 + 64, CLOUD_IMAGE = 128 * 128 * 4, CLOUD_VARIANTS = 8, CLOUD_SLOTS = 4 };
typedef struct {
    const Raster *mask, *noise;
    unsigned char *data;
    unsigned used, checked, generation;
    int mask_swizzled, noise_swizzled;
} CloudTexture;
static CloudTexture cloud_textures[CLOUD_SLOTS];

static unsigned cloud_texel(const unsigned char *pixels, const unsigned *palette, int x, int y, int depth, int swizzled)
{
    int byte = depth == 4 ? x >> 1 : x;
    unsigned index = pixels[swizzled ? ge_swizzle_offset(byte, y, 64) : y * 64 + byte];
    if (depth == 4) index = index >> ((x & 1) * 4) & 15;
    return palette[index];
}

static unsigned cloud_alpha(const unsigned char *pixels, const unsigned *palette, int x, int y, int swizzled)
{
    /* Sample the 64x64 mask at the centres of the 128x128 noise texture. */
    int fx = x * 128 - 64, fy = y * 128 - 64;
    int x0 = fx >> 8, y0 = fy >> 8, wx = fx - x0 * 256, wy = fy - y0 * 256;
    unsigned sum = 0;
    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
        int sx = x0 + i, sy = y0 + j;
        if (sx < 0) sx = 0; if (sx > 63) sx = 63;
        if (sy < 0) sy = 0; if (sy > 63) sy = 63;
        sum += (cloud_texel(pixels, palette, sx, sy, 8, swizzled) >> 24) *
               (i ? wx : 256 - wx) * (j ? wy : 256 - wy);
    }
    return (sum + 32768) >> 16;
}

static CloudTexture *cloud_texture(const Raster *mask, const Raster *noise)
{
    if (!mask || !noise || mask->width != 64 || mask->height != 64 || mask->depth != 8 || mask->stride != 64 ||
        noise->width != 128 || noise->height != 128 || noise->depth != 4 || noise->stride != 64 ||
        !mask->originalPixels || !noise->originalPixels || !mask->palette || !noise->palette) {
        cloud_size_refused++;
        return 0;
    }
    const SwizzleHead *mh = swizzle_head(mask->originalPixels), *nh = swizzle_head(noise->originalPixels);
    if ((mh && (mh->wbytes != 64 || mh->rows != 64)) || (nh && (nh->wbytes != 64 || nh->rows != 128)))
        return 0;
    CloudTexture *slot = 0;
    for (int i = 0; i < CLOUD_SLOTS; i++) {
        CloudTexture *s = cloud_textures + i;
        if (!s->data || cache_frame - s->used > 4) slot = s;
        if (s->data && s->mask == mask && s->noise == noise && s->mask_swizzled == !!mh && s->noise_swizzled == !!nh) {
            if ((s->checked == cache_frame && s->generation == texture_generation) ||
                (!memcmp(s->data, mask->originalPixels, 4096) && !memcmp(s->data + 4096, noise->originalPixels, 8192) &&
                 !memcmp(s->data + 12288, mask->palette, 1024) && !memcmp(s->data + 13312, noise->palette, 64))) {
                s->used = s->checked = cache_frame; s->generation = texture_generation;
                return s;
            }
        }
    }
    if (!slot) { cloud_busy++; return 0; } /* All slots are still read by queued GE lists. */
    if (!slot->data) slot->data = memalign(64, CLOUD_INPUT + CLOUD_VARIANTS * CLOUD_IMAGE);
    if (!slot->data) return 0;
    unsigned char *input = slot->data;
    memcpy(input, mask->originalPixels, 4096); memcpy(input + 4096, noise->originalPixels, 8192);
    memcpy(input + 12288, mask->palette, 1024); memcpy(input + 13312, noise->palette, 64);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
        unsigned alpha = cloud_alpha(input, (const unsigned *)(input + 12288), x, y, !!mh) << 24;
        for (int flip = 0; flip < CLOUD_VARIANTS; flip++) {
            int nx = flip & 4 ? y : x, ny = flip & 4 ? x : y;
            unsigned rgb = cloud_texel(input + 4096, (const unsigned *)(input + 13312),
                                       flip & 1 ? 127 - nx : nx, flip & 2 ? 127 - ny : ny, 4, !!nh) & 0xffffff;
            *(unsigned *)(input + CLOUD_INPUT + flip * CLOUD_IMAGE + ge_swizzle_offset(x * 4, y, 512)) = rgb | alpha;
        }
    }
    sceKernelDcacheWritebackRange(input + CLOUD_INPUT, CLOUD_VARIANTS * CLOUD_IMAGE);
    slot->mask = mask; slot->noise = noise; slot->mask_swizzled = !!mh; slot->noise_swizzled = !!nh;
    slot->used = slot->checked = cache_frame; slot->generation = texture_generation;
    cloud_builds++;
    return slot;
}

int p4_cloud_draw(const Vertex2D *mask, const Vertex2D *noise, Raster *raster, unsigned blend)
{
    if (!p4_cloud) return 0;
    if (SKIPPING) return 1;
    if ((p4_probe_flags & 2) || p4_probe_2d || rtt_target || pixel_mask != 0x00ffffff ||
        sky_state[3] != 0x31001 || blend != 0x54 || render_state[RS_ZWRITE]) {
        cloud_state_refused++;
        return 0;
    }
    int swap = noise[1].u == noise[0].u;
    int flip = (noise[0].u == 1) | ((noise[0].v == 1) << 1) | (swap << 2), corners = 0;
    for (int i = 0; i < 4; i++) {
        const Vertex2D *a = mask + i, *b = noise + i;
        if (a->u != (i & 1) || a->v != (i >> 1) ||
            b->u != (flip & 1 ? 1 - (swap ? a->v : a->u) : (swap ? a->v : a->u)) ||
            b->v != (flip & 2 ? 1 - (swap ? a->u : a->v) : (swap ? a->u : a->v)) ||
            !(fabsf(a->x) < 1e6f && fabsf(a->y) < 1e6f && fabsf(a->z) < 1e6f) ||
            a->x != b->x || a->y != b->y || a->z != b->z ||
            a->r != b->r || a->g != b->g || a->b != b->b || a->a != b->a ||
            a->r != mask[0].r || a->g != mask[0].g || a->b != mask[0].b || a->a != mask[0].a) {
            cloud_geometry_refused++;
            return 0;
        }
        corners |= 1 << ((int)a->u + 2 * (int)a->v);
    }
    if (corners != 15) return 0;
    CloudTexture *texture = cloud_texture((Raster *)render_state[1], raster);
    if (!texture) { cloud_fallbacks++; return 0; }
    if (!list_room()) return 1;
    prof_begin(PROF_2D);
    int timed = ge_time_begin();
    translucent_flush(); gu_dirty(); begin_frame(); bind_depth();
    /* shortcut: only DistortParticle mode 0 scratch alpha; audit later alpha readers before extending the hook. */
    sceGuPixelMask(0xff000000);
    sceGuDisable(GU_STENCIL_TEST); sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_CLIP_PLANES);
    sceGuEnable(GU_TEXTURE_2D); sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexMode(GU_PSM_8888, 0, 0, 1);
    sceGuTexImage(0, 128, 128, 128, texture->data + CLOUD_INPUT + flip * CLOUD_IMAGE);
    sceGuTexFlush(); /* An aged cache slot may reuse an address still tagged in the texture cache. */
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA); sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0); sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    GuVertex *v = sceGuGetMemory(4 * sizeof *v);
    texture_w = texture_h = 128;
    for (int i = 0; i < 4; i++) put_vertex(v + i, mask + i);
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, 0, v);
    cloud_draws++;
    ge_time_end(timed, GT_2D); prof_end(PROF_2D);
    return 1;
}

static void cloud_clear(void)
{
    for (int i = 0; i < CLOUD_SLOTS; i++) { free(cloud_textures[i].data); cloud_textures[i] = (CloudTexture){0}; }
}

static void cloud_report(void)
{
    p4_log("cloud %d cumulative: draws %u builds %u fallback %u size/format %u state %u geometry %u busy %u\n",
           p4_cloud, cloud_draws, cloud_builds, cloud_fallbacks, cloud_size_refused, cloud_state_refused,
           cloud_geometry_refused, cloud_busy);
}
