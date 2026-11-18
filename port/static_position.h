#ifndef P4_STATIC_POSITION_H
#define P4_STATIC_POSITION_H

/* s16 positions on one power-of-two grid per geometry (P3P draws all its 3D this way; the skins since b205).
   bound: the largest |coordinate| of the whole geometry (its sphere's |centre| + radius), so every mesh of the
   geometry rounds on the same grid and a vertex two meshes share stays one point. Rounding moves a vertex by at
   most scale / 65536 (2000 units: about 0.03). head: bytes before the float position (8 unlit: u16 UV + colour;
   12 lit: + s8 normal and pad). The s16 vertex keeps the head and is head + 8 bytes long (16 or 20).
   0: a coordinate is not finite or lies outside the bound (the sphere is wrong): keep floats. */
static float static_position_scale(const void *source, int count, int head, float bound)
{
    const unsigned char *v = source;
    if (!source || count <= 0 || count > 65536 || !(bound > 0 && bound < 1048576)) return 0;
    float scale = 1.0f / 1048576;
    while (scale * 32767 < bound * 32768) scale *= 2; /* bound itself rounds to at most 32767 */
    for (int i = 0; i < count; i++) {
        float p[3];
        __builtin_memcpy(p, v + i * (head + 12) + head, 12);
        for (int k = 0; k < 3; k++)
            if (!(p[k] >= -bound && p[k] <= bound)) return 0;
    }
    return scale;
}

static void static_position_copy(void *target, const void *source, int count, float scale, int head)
{
    unsigned char *out = target;
    const unsigned char *in = source;
    const int from = head + 12, to = head + 8;
    for (int i = 0; i < count; i++) {
        float p[3]; short q[3];
        __builtin_memcpy(p, in + i * from + head, 12);
        for (int k = 0; k < 3; k++) {
            float f = p[k] * (32768 / scale);
            q[k] = (short)(f < 0 ? f - 0.5f : f + 0.5f); /* nearest; |f| <= 32767 by the bound */
        }
        __builtin_memcpy(out + i * to, in + i * from, head);
        __builtin_memcpy(out + i * to + head, q, 6);
        out[i * to + head + 6] = out[i * to + head + 7] = 0;
    }
}

#endif
