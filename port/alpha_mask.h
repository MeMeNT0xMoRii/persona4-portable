/* PSP writes framebuffer alpha through stencil, never fragment output.
 * Reconstruct each bit of textured/interpolated alpha with the GE alpha test.
 * Restricted to main-screen masks without depth writes/DATE or ATEST rejection. */

/* Lowest alpha bit reconstructed: crowded frames keep only the high bits (fewer passes, coarser soft edges). */
static unsigned alpha_mask_low = 1;

/* Clear the covered pixels' alpha, then write each bit up to limit. The first draw selects the vertex format. */
static void alpha_mask_bits(int prim, int type, int count, const GuVertex *v, unsigned limit)
{
    sceGuPixelMask(0x00ffffff);
    sceGuAlphaFunc(GU_ALWAYS, 0, 0xff);
    sceGuStencilFunc(GU_ALWAYS, 0, 0xff);
    sceGuDrawArray(prim, type, count, 0, v);
    for (unsigned bit = alpha_mask_low; bit <= limit && bit <= 128; bit <<= 1) {
        sceGuPixelMask(0x00ffffff | ((~bit & 0xffu) << 24));
        sceGuAlphaFunc(GU_EQUAL, bit, bit);
        sceGuStencilFunc(GU_ALWAYS, bit, 0xff);
        sceGuDrawArray(prim, 0, count, 0, v);
    }
}

/* Sign of point against the line a-b, 0 when too close to call after GE subpixel rounding. */
static int alpha_mask_side(const GuVertex *a, const GuVertex *b, const GuVertex *p)
{
    float dx = b->x - a->x, dy = b->y - a->y, x = p->x - a->x, y = p->y - a->y;
    float area = dx * y - dy * x, margin = (fabsf(dx) + fabsf(dy) + fabsf(x) + fabsf(y) + 1.0f) / 8;
    return area > margin ? 1 : area < -margin ? -1 : 0;
}

/* Two triangles on opposite sides of a shared diagonal: a convex quad, each pixel covered once. */
static int alpha_mask_quad(int prim, int count, const GuVertex *v)
{
    if ((prim != GU_TRIANGLE_STRIP || count != 4) && (prim != GU_TRIANGLES || count != 6))
        return 0;
    for (int i = 0; i < count; i++)
        if (!(fabsf(v[i].x) < 1024 && fabsf(v[i].y) < 1024))
            return 0;
    if (count == 4)
        return alpha_mask_side(v + 1, v + 2, v) * alpha_mask_side(v + 1, v + 2, v + 3) == -1;
    if (v[0].x == v[3].x && v[0].y == v[3].y && v[2].x == v[4].x && v[2].y == v[4].y)
        return alpha_mask_side(v, v + 2, v + 1) * alpha_mask_side(v, v + 2, v + 5) == -1;
    return 0;
}

static int draw_2d_alpha_mask(int prim, int count, const GuVertex *v)
{
    if (count <= 0) return 0;
    unsigned test = sky_state[3], alpha = v[0].colour >> 24;
    if (!p4_2d_alpha_masks || pixel_mask != 0x00ffffff || rtt_target ||
        render_state[RS_ZWRITE] || (test & (1u << 14)) ||
        ((test & 1) && ((test >> 1) & 7) != 1 && ((test >> 12) & 3) != 1) ||
        (prim != GU_TRIANGLES && prim != GU_TRIANGLE_STRIP && prim != GU_TRIANGLE_FAN))
        return 0;
    int uniform = !render_state[1] || (bound_alpha_min == 255 && bound_alpha_max == 255) || !alpha;
    for (int i = 1; i < count && uniform; ++i)
        uniform = (v[i].colour >> 24) == alpha;
    const int type = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    if (uniform) {
        sceGuDisable(GU_ALPHA_TEST);
        sceGuStencilFunc(GU_ALWAYS, alpha, 0xff);
        sceGuDrawArray(prim, type, count, 0, v);
    } else {
        /* MODULATE cannot raise alpha: bits above the product's ceiling stay 0 (cleared). */
        unsigned vmax = 0;
        for (int i = 0; i < count; ++i)
            if ((v[i].colour >> 24) > vmax) vmax = v[i].colour >> 24;
        unsigned limit = render_state[1] ? p4_alpha_product_ceiling(bound_alpha_max, vmax) : vmax;
        sceGuEnable(GU_ALPHA_TEST);
        if (alpha_mask_quad(prim, count, v)) {
            /* One fragment per pixel: the whole quad at once (from the persona4-portable fork, home21-22). */
            alpha_mask_bits(prim, type, count, v, limit);
        } else {
            /* Finish each triangle before the next: overlapping triangles must
             * overwrite alpha, rather than OR their separate bit planes. */
            int triangles = prim == GU_TRIANGLES ? count / 3 : count - 2;
            for (int i = 0; i < triangles; ++i) {
                const GuVertex *tri = v + i * 3;
                if (prim != GU_TRIANGLES) {
                    GuVertex *copy = sceGuGetMemory(3 * sizeof *copy);
                    if (prim == GU_TRIANGLE_FAN) {
                        copy[0] = v[0]; copy[1] = v[i + 1]; copy[2] = v[i + 2];
                    } else {
                        copy[0] = v[i + (i & 1)]; copy[1] = v[i + !(i & 1)]; copy[2] = v[i + 2];
                    }
                    tri = copy;
                }
                alpha_mask_bits(GU_TRIANGLES, type, 3, tri, limit);
            }
        }
        sceGuDisable(GU_ALPHA_TEST);
        sceGuPixelMask(pixel_mask);
        sceGuStencilFunc(GU_ALWAYS, alpha, 0xff);
    }
    return 1;
}
