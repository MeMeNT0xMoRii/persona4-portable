/* Bone LOD for far skinned characters, shared by the PSP (rwsky.c, at skin cache build) and the host test
   (tools/test_bone_lod.c). Plain C, no PSP calls, malloc only.

   Games drop bones on their far LODs (fingers, face, hair, twist bones): a dropped bone's vertices follow its
   nearest kept ancestor rigidly. Here the kept set is chosen greedily: while more than `keep` weighted bones remain,
   the bone whose removal moves the least is merged into its nearest kept ancestor. The cost of removing bone b is
   sum(weight * distance from b's joint) over the vertex weights b carries (its own and those merged into it before):
   a vertex at distance d from a joint that turns by angle t moves by about d * t. A bone with no kept ancestor (a
   root) is never removed, so the result can stay above `keep` when a skin has several roots.

   Positions are the geometry's bind-pose vertices; s2b[b] is the bone's skinToBone matrix (RenderWare layout: right,
   flags, up, pad, at, pad, pos, pad as 16 floats; p * M = bone space), so |p * s2b[b]| is p's distance from joint b.
   parent[b] < 0 for a root. Output: map[b] = the bone b's weights go to (b itself when kept). Returns the number of
   weighted bones kept, or -1 (no memory or bad input: the caller keeps every bone). */
#ifndef BONE_LOD_H
#define BONE_LOD_H
#include <stdlib.h>
#include <math.h>

enum { BL_MAX_BONES = 256 };

static float bl_dist(const float *p, const float *m)
{
    float x = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    float y = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    float z = p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14];
    float d = sqrtf(x * x + y * y + z * z);
    return d <= 1e30f ? d : 1e30f; /* NaN or huge: a bone that must not be dropped cheaply */
}

/* bone[v * 4 + k], weight[v * 4 + k]: up to 4 influences a vertex (weight <= 0: none) */
static int bl_collapse(int nbones, const short *parent, const float *s2b, int nverts, const float (*pos)[3],
                       const unsigned char *bone, const float *weight, int keep, unsigned char *map)
{
    if (nbones <= 0 || nbones > BL_MAX_BONES || nverts < 0 || keep < 1)
        return -1;
    for (int b = 0; b < nbones; b++)
        if (parent[b] >= nbones || parent[b] >= b) /* RenderWare lists a parent before its children */
            return -1;
    unsigned char *owner = malloc(nverts * 4 > 0 ? nverts * 4 : 1);
    float cost[BL_MAX_BONES] = { 0 };
    unsigned char kept[BL_MAX_BONES] = { 0 };
    if (!owner)
        return -1;
    int count = 0;
    for (int i = 0; i < nverts * 4; i++) {
        owner[i] = bone[i];
        if (weight[i] > 0 && bone[i] < nbones) {
            cost[bone[i]] += weight[i] * bl_dist(pos[i >> 2], s2b + bone[i] * 16);
            count += !kept[bone[i]];
            kept[bone[i]] = 1;
        }
    }
    while (count > keep) {
        int best = -1, to = -1;
        for (int b = 0; b < nbones; b++) {
            if (!kept[b] || (best >= 0 && cost[b] >= cost[best]))
                continue;
            int a = parent[b];
            while (a >= 0 && !kept[a])
                a = parent[a];
            if (a >= 0)
                best = b, to = a;
        }
        if (best < 0)
            break;
        for (int i = 0; i < nverts * 4; i++)
            if (owner[i] == best && weight[i] > 0) {
                owner[i] = (unsigned char)to;
                cost[to] += weight[i] * bl_dist(pos[i >> 2], s2b + to * 16);
            }
        kept[best] = 0;
        count--;
    }
    for (int b = 0; b < nbones; b++) {
        int a = b;
        while (a >= 0 && !kept[a])
            a = parent[a];
        map[b] = (unsigned char)(a >= 0 ? a : b); /* unweighted bones with no kept ancestor carry nothing */
    }
    free(owner);
    return count;
}

/* parents from RpHAnimHierarchy node flags (PUSH 2: the parent stays for the next sibling; POP 1: the last child) */
static int bl_parents(int n, const unsigned *flags, int stride_words, short *parent)
{
    short stack[BL_MAX_BONES];
    int cur = -1, depth = 0;
    if (n <= 0 || n > BL_MAX_BONES)
        return 0;
    for (int j = 0; j < n; j++) {
        unsigned f = flags[j * stride_words];
        parent[j] = (short)cur;
        if ((f & 2) && depth < BL_MAX_BONES)
            stack[depth++] = (short)cur;
        cur = (f & 1) && depth > 0 ? stack[--depth] : j;
    }
    return 1;
}
#endif
