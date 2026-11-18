#ifndef P4_STATIC_UV_H
#define P4_STATIC_UV_H

#include <math.h>

/* Float UVs become u16 in GE units; colour, normals and positions remain byte-identical. */
static int static_uv_pack(const void *source, int count, int stride, void *target, float decode[4])
{
    const unsigned char *in = source;
    unsigned char *out = target;
    float lo[2] = {64,64}, hi[2] = {-64,-64};
    int i, k;
    if (!source || !target || count <= 0 || count > 65536 || (stride != 24 && stride != 28)) return 0;
    for (i = 0; i < count; i++) {
        float uv[2];
        __builtin_memcpy(uv, in + i * stride, sizeof uv);
        for (k = 0; k < 2; k++) {
            if (!(uv[k] >= -64 && uv[k] <= 64)) return 0;
            if (uv[k] < lo[k]) lo[k] = uv[k];
            if (uv[k] > hi[k]) hi[k] = uv[k];
        }
    }
    for (k = 0; k < 2; k++) {
        float span = hi[k] - lo[k];
        if (!(span <= 8)) return 0; /* about 1/16 texel quantization error at a 512-texel texture */
        if (span > 0 && span < 1e-6f) return 0;
        decode[k] = span > 0 ? span * (32768.0f / 32767.0f) : 1;
        decode[k + 2] = lo[k];
    }
    for (i = 0; i < count; i++) {
        float uv[2];
        unsigned short packed[2];
        __builtin_memcpy(uv, in + i * stride, sizeof uv);
        for (k = 0; k < 2; k++) {
            float q = (uv[k] - lo[k]) * (32768.0f / decode[k]);
            int rounded = (int)(q + 0.5f);
            packed[k] = rounded < 0 ? 0 : rounded > 32767 ? 32767 : rounded;
        }
        __builtin_memcpy(out + i * (stride - 4), packed, sizeof packed);
        __builtin_memcpy(out + i * (stride - 4) + 4, in + i * stride + 8, stride - 8);
    }
    return 1;
}

static int static_uv_same(const void *source, const void *packed, int count, int stride, const float decode[4])
{
    const unsigned char *a = source, *b = packed;
    int i, k;
    for (i = 0; i < count; i++) {
        float uv[2];
        unsigned short q[2];
        __builtin_memcpy(uv, a + i * stride, sizeof uv);
        __builtin_memcpy(q, b + i * (stride - 4), sizeof q);
        if (__builtin_memcmp(a + i * stride + 8, b + i * (stride - 4) + 4, stride - 8)) return 0;
        for (k = 0; k < 2; k++) {
            float restored = q[k] * (1.0f / 32768) * decode[k] + decode[k + 2];
            float tolerance = decode[k] * (0.5f / 32768) + 2e-6f * (1 + fabsf(uv[k]));
            if (!(fabsf(restored - uv[k]) <= tolerance)) return 0;
        }
    }
    return 1;
}

#endif
