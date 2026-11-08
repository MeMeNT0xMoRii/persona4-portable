/* Conservative homogeneous frustum rejection. A shared outside bit for EVERY
   indexed vertex proves that the whole mesh misses one clipping half-space.
   Crossing meshes remain visible. The margin also tolerates GE float24 rounding. */
#ifndef P4_FRUSTUM_H
#define P4_FRUSTUM_H
#include <math.h>

static unsigned char frustum_code(const float m[4][4], const float *p)
{
    float x = p[0]*m[0][0] + p[1]*m[1][0] + p[2]*m[2][0] + m[3][0];
    float y = p[0]*m[0][1] + p[1]*m[1][1] + p[2]*m[2][1] + m[3][1];
    float z = p[0]*m[0][2] + p[1]*m[1][2] + p[2]*m[2][2] + m[3][2];
    float w = p[0]*m[0][3] + p[1]*m[1][3] + p[2]*m[2][3] + m[3][3];
    /* Non-finite inputs never justify rejecting a mesh. */
    if (!isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(w))
        return 0;
    float e = (fabsf(x) + fabsf(y) + fabsf(z) + fabsf(w) + 1) * 0.0001f;
    return (x < -w-e ? 1 : 0) | (x > w+e ? 2 : 0) |
           (y < -w-e ? 4 : 0) | (y > w+e ? 8 : 0) |
           (z < -w-e ? 16 : 0) | (z > w+e ? 32 : 0);
}

static int frustum_mesh_outside(const unsigned char *code, int count,
                                const unsigned short *indices, int n)
{
    unsigned common = 63;
    if (n <= 0 || !indices)
        return 0;
    for (int i = 0; i < n; i++) {
        if (indices[i] >= count)
            return 0;
        common &= code[indices[i]];
        if (!common)
            return 0;
    }
    return common != 0;
}
#endif
