/* meshkit: triangle-list simplification shared by the PSP build (rwsky.c, at skin cache build) and host tools
   (tools/meshkit_offline.c, tools/test_meshkit.c). Plain C, no PSP calls, malloc only.

   Half-edge collapse with Garland-Heckbert quadrics: a vertex a moves onto a neighbour b that already exists, so no
   vertex is made and every attribute (UV, normal, colour, bone weights) stays valid; only the index list changes.
   Never moved: vertices on an open edge (one triangle), vertices that share their position with another vertex (UV
   and normal seams), vertices the caller locks. With groups (the dominant bone of a skinned vertex), a vertex only
   moves onto one of its own group, so a joint keeps its shape. A collapse that turns a remaining triangle over
   (normal against the old one) or makes it near zero area is refused.

   per_pass: collapses a pass may take before costs are evaluated again. Large (the PSP, load time): many independent
   collapses a pass. 1 (offline): always the cheapest collapse with fresh costs, slower and better. */
#ifndef MESHKIT_H
#define MESHKIT_H
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { int passes, collapses, refused_flip, triangles_in, triangles_out; float worst; } MkStats;
typedef struct { float cost; int from, to; } MkCand;
typedef struct { unsigned key; int tri; } MkEdge;

static const float (*mk_sort_pos)[3];
static int mk_pos_cmp(const void *a, const void *b)
{
    int i = *(const int *)a, j = *(const int *)b, c = memcmp(mk_sort_pos[i], mk_sort_pos[j], sizeof *mk_sort_pos);
    return c ? c : i - j;
}
static int mk_cand_cmp(const void *a, const void *b)
{
    float x = ((const MkCand *)a)->cost, y = ((const MkCand *)b)->cost;
    return x < y ? -1 : x > y;
}
static int mk_edge_cmp(const void *a, const void *b)
{
    unsigned x = ((const MkEdge *)a)->key, y = ((const MkEdge *)b)->key;
    return x < y ? -1 : x > y;
}
static unsigned mk_key(int a, int b) { return a < b ? (unsigned)a << 16 | (unsigned)b : (unsigned)b << 16 | (unsigned)a; }

/* lock[v] |= 1 for every vertex whose position another vertex has too (seams) */
static void mk_lock_seams(const float (*pos)[3], int nverts, unsigned char *lock)
{
    int *order = malloc(nverts * sizeof *order);
    if (!order)
        return;
    for (int i = 0; i < nverts; i++)
        order[i] = i;
    mk_sort_pos = pos;
    qsort(order, nverts, sizeof *order, mk_pos_cmp);
    for (int i = 1; i < nverts; i++)
        if (!memcmp(pos[order[i]], pos[order[i - 1]], sizeof *pos))
            lock[order[i]] = lock[order[i - 1]] = 1;
    free(order);
}

static void mk_normal(const float *a, const float *b, const float *c, float *n)
{
    float u[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, v[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
    n[0] = u[1] * v[2] - u[2] * v[1], n[1] = u[2] * v[0] - u[0] * v[2], n[2] = u[0] * v[1] - u[1] * v[0];
}
/* quadric (symmetric 4x4 as 10 floats) of a plane n.x + d = 0, weighted */
static void mk_quadric_add(float *q, const float *n, float d, float w)
{
    float a = n[0], b = n[1], c = n[2];
    q[0] += w * a * a, q[1] += w * a * b, q[2] += w * a * c, q[3] += w * a * d;
    q[4] += w * b * b, q[5] += w * b * c, q[6] += w * b * d;
    q[7] += w * c * c, q[8] += w * c * d, q[9] += w * d * d;
}
static float mk_quadric_eval(const float *q, const float *p)
{
    float x = p[0], y = p[1], z = p[2];
    return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y +
           q[7] * z * z + 2 * q[8] * z + q[9];
}

/* tri: ntri x 3 indices, simplified in place; returns the triangles left. target: stop at or below this many;
   max_error: refuse collapses costing more (squared distance units of pos); lock/group may be 0. */
static int mk_simplify(const float (*pos)[3], int nverts, const unsigned char *lock_in, const unsigned char *group, unsigned short *tri, int ntri,
                       int target, float max_error, int per_pass, MkStats *st)
{
    MkStats local;
    if (!st)
        st = &local;
    memset(st, 0, sizeof *st);
    st->triangles_in = ntri;
    float *q = calloc(nverts * 10, sizeof *q);
    unsigned char *lock = calloc(nverts, 1), *touched = malloc(nverts);
    MkEdge *edges = malloc(ntri * 3 * sizeof *edges);
    MkCand *cand = malloc(ntri * 3 * 2 * sizeof *cand);
    int *start = malloc((nverts + 1) * sizeof *start), *vtri = malloc(ntri * 3 * sizeof *vtri), alive = 0;
    if (!q || !lock || !touched || !edges || !cand || !start || !vtri)
        goto done;
    if (lock_in)
        memcpy(lock, lock_in, nverts);
    mk_lock_seams(pos, nverts, lock);
    for (int t = 0; t < ntri; t++) { /* quadrics, and degenerate triangles out */
        unsigned short *c = tri + t * 3;
        float n[3];
        mk_normal(pos[c[0]], pos[c[1]], pos[c[2]], n);
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2] || !(len > 0)) {
            c[0] = c[1] = c[2] = 0xffff;
            continue;
        }
        alive++;
        n[0] /= len, n[1] /= len, n[2] /= len;
        float d = -(n[0] * pos[c[0]][0] + n[1] * pos[c[0]][1] + n[2] * pos[c[0]][2]);
        for (int k = 0; k < 3; k++)
            mk_quadric_add(q + c[k] * 10, n, d, len * 0.5f);
    }
    { /* open edges (one triangle) lock their vertices */
        int ne = 0;
        for (int t = 0; t < ntri; t++)
            if (tri[t * 3] != 0xffff)
                for (int k = 0; k < 3; k++)
                    edges[ne++] = (MkEdge){ mk_key(tri[t * 3 + k], tri[t * 3 + (k + 1) % 3]), t };
        qsort(edges, ne, sizeof *edges, mk_edge_cmp);
        for (int i = 0; i < ne;) {
            int j = i;
            while (j < ne && edges[j].key == edges[i].key)
                j++;
            if (j - i == 1)
                lock[edges[i].key >> 16] = lock[edges[i].key & 0xffff] = 1;
            i = j;
        }
    }
    while (alive > target) {
        st->passes++;
        /* vertex -> triangles */
        memset(start, 0, (nverts + 1) * sizeof *start);
        for (int t = 0; t < ntri; t++)
            if (tri[t * 3] != 0xffff)
                for (int k = 0; k < 3; k++)
                    start[tri[t * 3 + k] + 1]++;
        for (int v = 0; v < nverts; v++)
            start[v + 1] += start[v];
        {
            int *fill = malloc(nverts * sizeof *fill);
            if (!fill)
                break;
            memcpy(fill, start, nverts * sizeof *fill);
            for (int t = 0; t < ntri; t++)
                if (tri[t * 3] != 0xffff)
                    for (int k = 0; k < 3; k++)
                        vtri[fill[tri[t * 3 + k]]++] = t;
            free(fill);
        }
        /* candidates: each edge, the cheaper allowed direction */
        int nc = 0, ne = 0;
        for (int t = 0; t < ntri; t++)
            if (tri[t * 3] != 0xffff)
                for (int k = 0; k < 3; k++)
                    edges[ne++] = (MkEdge){ mk_key(tri[t * 3 + k], tri[t * 3 + (k + 1) % 3]), t };
        qsort(edges, ne, sizeof *edges, mk_edge_cmp);
        for (int i = 0; i < ne; i++) {
            if (i && edges[i].key == edges[i - 1].key)
                continue;
            int a = edges[i].key >> 16, b = edges[i].key & 0xffff;
            float qs[10];
            for (int k = 0; k < 10; k++)
                qs[k] = q[a * 10 + k] + q[b * 10 + k];
            int same = !group || group[a] == group[b];
            float ab = !lock[a] && same ? mk_quadric_eval(qs, pos[b]) : INFINITY; /* a moves onto b */
            float ba = !lock[b] && same ? mk_quadric_eval(qs, pos[a]) : INFINITY;
            float cost = ab < ba ? ab : ba;
            if (cost <= max_error)
                cand[nc++] = ab < ba ? (MkCand){ cost, a, b } : (MkCand){ cost, b, a };
        }
        if (!nc)
            break;
        qsort(cand, nc, sizeof *cand, mk_cand_cmp);
        memset(touched, 0, nverts);
        int taken = 0;
        for (int i = 0; i < nc && taken < per_pass && alive > target; i++) {
            int a = cand[i].from, b = cand[i].to, ok = 1;
            if (touched[a] || touched[b])
                continue;
            for (int s = start[a]; s < start[a + 1] && ok; s++) { /* the triangles a keeps after the move */
                const unsigned short *c = tri + vtri[s] * 3;
                if (c[0] == 0xffff || c[0] == b || c[1] == b || c[2] == b)
                    continue;
                float before[3], after[3], p[3][3];
                mk_normal(pos[c[0]], pos[c[1]], pos[c[2]], before);
                for (int k = 0; k < 3; k++)
                    memcpy(p[k], pos[c[k] == a ? b : c[k]], sizeof p[k]);
                mk_normal(p[0], p[1], p[2], after);
                float lb = sqrtf(before[0] * before[0] + before[1] * before[1] + before[2] * before[2]);
                float la = sqrtf(after[0] * after[0] + after[1] * after[1] + after[2] * after[2]);
                float dot = before[0] * after[0] + before[1] * after[1] + before[2] * after[2];
                if (!(la > lb * 0.02f) || !(dot > 0.2f * la * lb))
                    ok = 0;
            }
            if (!ok) {
                st->refused_flip++;
                continue;
            }
            for (int s = start[a]; s < start[a + 1]; s++) {
                unsigned short *c = tri + vtri[s] * 3;
                if (c[0] == 0xffff)
                    continue;
                for (int k = 0; k < 3; k++) {
                    touched[c[k]] = 1;
                    if (c[k] == a)
                        c[k] = b;
                }
                if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                    c[0] = c[1] = c[2] = 0xffff, alive--;
            }
            touched[a] = touched[b] = 1;
            for (int k = 0; k < 10; k++)
                q[b * 10 + k] += q[a * 10 + k];
            if (cand[i].cost > st->worst)
                st->worst = cand[i].cost;
            st->collapses++, taken++;
        }
        if (!taken)
            break;
    }
done:
    { /* compact */
        int n = 0;
        for (int t = 0; t < ntri; t++)
            if (tri[t * 3] != 0xffff) {
                memmove(tri + n * 3, tri + t * 3, 3 * sizeof *tri);
                n++;
            }
        alive = n;
    }
    free(q), free(lock), free(touched), free(edges), free(cand), free(start), free(vtri);
    st->triangles_out = alive;
    return alive;
}
#endif
