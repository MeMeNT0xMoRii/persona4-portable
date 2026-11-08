/* clip_triangle_guard skips planes no corner is beyond: same bytes out as clipping against all five.
   gcc -O2 -I port tools/test_clip_planes.c -o build/test_clip_planes && build/test_clip_planes */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { float u, v; unsigned colour; float x, y, z; } Vertex3D;
#include "clip.h"

static float rnd(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

int main(void)
{
    float near = 1;
    int clipped = 0, skipped_planes = 0;
    for (int run = 0; run < 1000000; run++) {
        ClipVertex tri[3];
        unsigned planes = 0;
        memset(tri, 0, sizeof tri);
        for (int k = 0; k < 3; k++) {
            float w = rnd(-5, 60);
            tri[k].c[0] = rnd(-12, 12) * (w > 1 ? w : 1);
            tri[k].c[1] = rnd(-12, 12) * (w > 1 ? w : 1);
            tri[k].c[2] = rnd(-w, w);
            tri[k].c[3] = w;
            tri[k].v = (Vertex3D){ rnd(0, 4), rnd(0, 4), (unsigned)rand() * 2654435761u, rnd(-99, 99), rnd(-99, 99), rnd(-99, 99) };
            for (int a = 0; a < 3; a++)
                tri[k].n[a] = rnd(-127, 127);
            planes |= guard_outcode(tri[k].c, near);
        }
        if (!planes || (guard_outcode(tri[0].c, near) & guard_outcode(tri[1].c, near) & guard_outcode(tri[2].c, near)))
            continue; /* draw_guarded never clips these */
        Vertex3D all[GUARD_OUT_MAX], some[GUARD_OUT_MAX];
        float na[GUARD_OUT_MAX][3], ns[GUARD_OUT_MAX][3];
        memset(all, 0, sizeof all); memset(some, 0, sizeof some); memset(na, 0, sizeof na); memset(ns, 0, sizeof ns);
        int a = clip_triangle_guard(tri, near, all, na, 0x1f), s = clip_triangle_guard(tri, near, some, ns, planes);
        assert(a == s);
        assert(!memcmp(all, some, sizeof all) && !memcmp(na, ns, sizeof na));
        clipped++;
        skipped_planes += 5 - __builtin_popcount(planes);
    }
    /* mutation check: skipping a plane a corner IS beyond must change the result for some triangle */
    int caught = 0;
    for (int run = 0; run < 10000 && !caught; run++) {
        ClipVertex tri[3];
        memset(tri, 0, sizeof tri);
        unsigned planes = 0;
        for (int k = 0; k < 3; k++) {
            tri[k].c[0] = rnd(-20, 20); tri[k].c[1] = rnd(-1, 1); tri[k].c[3] = 2;
            planes |= guard_outcode(tri[k].c, near);
        }
        Vertex3D all[GUARD_OUT_MAX], some[GUARD_OUT_MAX];
        memset(all, 0, sizeof all); memset(some, 0, sizeof some);
        if ((planes & 6) && clip_triangle_guard(tri, near, all, 0, 0x1f) != clip_triangle_guard(tri, near, some, 0, planes & ~6u))
            caught = 1;
    }
    assert(caught);
    printf("clip planes ok: %d clipped triangles identical, %.2f planes skipped each\n", clipped, skipped_planes / (double)clipped);
    return 0;
}
