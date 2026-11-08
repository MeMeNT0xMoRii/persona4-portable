/* Host check for port/clip.h: gcc -I port tools/test_clip.c -o build/test_clip && build/test_clip */
#include <assert.h>
#include <math.h>
#include <stdio.h>
typedef struct { float u, v; unsigned colour; float x, y, z; } Vertex3D;
#include "clip.h"

static Vertex3D corner(float x, float z, float u) { Vertex3D v = { u, 0, 7, x, 0, z }; return v; }

int main(void)
{
    Vertex3D quad[4] = { corner(0, 1, 0), corner(1, 1, 1), corner(1, 3, 1), corner(0, 3, 0) }, out[CLIP_MAX_OUT];
    float in_front[4] = { 5, 5, 5, 5 }, behind[4] = { -1, -1, -1, -1 }, half[4] = { 0, 0, 2, 2 }, one[4] = { 0, 2, 2, 2 };
    assert(clip_quad_near(quad, in_front, 1, out) == 6);
    assert(clip_quad_near(quad, behind, 1, out) == 0);
    int n = clip_quad_near(quad, half, 1, out); /* two corners behind: still a quad, cut halfway */
    assert(n == 6);
    for (int i = 0; i < n; i++)
        assert(out[i].colour == 7 && (out[i].z == 3 || fabsf(out[i].z - 2) < 1e-6f));
    assert(clip_quad_near(quad, one, 1, out) == 9); /* one corner behind: a pentagon */
    puts("clip ok");
    return 0;
}
