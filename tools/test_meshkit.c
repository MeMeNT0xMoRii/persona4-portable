/* Host check of port/meshkit.h. gcc -O2 -I port tools/test_meshkit.c -o build/test_meshkit && build/test_meshkit
   A curved grid with an open border and a UV seam (duplicated vertices), and a sphere, simplified online-style (many
   collapses a pass) and offline-style (one a pass). Every result must keep border and seam vertices, stay inside
   its groups, have no degenerate or flipped triangle, and reach the target. Red control: a result with one triangle
   turned over must fail the same checks. */
#include <stdio.h>
#include "meshkit.h"

enum { GRID = 24 };
static float pos[(GRID + 1) * (GRID + 2) + 2000][3];
static unsigned char group[(GRID + 1) * (GRID + 2) + 2000], keep[(GRID + 1) * (GRID + 2) + 2000];

static int build_grid(unsigned short *tri, int *nverts)
{
    /* (GRID+1)^2 vertices on z = 0.2 sin(x) cos(y), plus a seam column: column GRID/2 duplicated for the right half */
    int n = 0, seam = (GRID + 1) * (GRID + 1);
    for (int y = 0; y <= GRID; y++)
        for (int x = 0; x <= GRID; x++, n++) {
            pos[n][0] = x * 0.1f, pos[n][1] = y * 0.1f, pos[n][2] = 0.2f * sinf(x * 0.25f) * cosf(y * 0.2f);
            group[n] = x < GRID / 2 ? 1 : 2;
        }
    for (int y = 0; y <= GRID; y++) {
        memcpy(pos[seam + y], pos[y * (GRID + 1) + GRID / 2], sizeof pos[0]);
        group[seam + y] = 2;
    }
    int t = 0;
    for (int y = 0; y < GRID; y++)
        for (int x = 0; x < GRID; x++) {
            int a = y * (GRID + 1) + x, b = a + 1, c = a + GRID + 1, d = c + 1;
            if (x == GRID / 2) /* right of the seam: the duplicates */
                a = seam + y, c = seam + y + 1;
            unsigned short q[6] = { a, b, d, a, d, c };
            memcpy(tri + t * 3, q, sizeof q);
            t += 2;
        }
    *nverts = seam + GRID + 1;
    return t;
}
static int build_sphere(unsigned short *tri, int *nverts)
{
    enum { R = 16, S = 24 };
    int n = 0, t = 0;
    for (int i = 0; i <= R; i++)
        for (int j = 0; j < S; j++, n++) {
            float th = 3.14159265f * i / R, ph = 6.2831853f * j / S;
            pos[n][0] = sinf(th) * cosf(ph), pos[n][1] = cosf(th), pos[n][2] = sinf(th) * sinf(ph);
            if (i == 0 || i == R) /* poles: one vertex each */
                pos[n][0] = 0, pos[n][2] = 0;
            group[n] = 0;
        }
    for (int i = 0; i < R; i++)
        for (int j = 0; j < S; j++) {
            int a = i * S + j, b = i * S + (j + 1) % S, c = a + S, d = b + S;
            if (i > 0) { unsigned short q[3] = { a, b, d }; memcpy(tri + t++ * 3, q, sizeof q); }
            if (i < R - 1) { unsigned short q[3] = { a, d, c }; memcpy(tri + t++ * 3, q, sizeof q); }
        }
    *nverts = n;
    return t;
}
/* 0 when the result is sound */
static int check(const char *name, const unsigned short *orig, int norig, const unsigned short *out, int nout, int nverts, int target,
                 const MkStats *st)
{
    int bad = 0;
    memset(keep, 0, sizeof keep);
    for (int t = 0; t < nout; t++) {
        const unsigned short *c = out + t * 3;
        for (int k = 0; k < 3; k++) {
            if (c[k] >= nverts) { printf("%s: index out of range\n", name); return 1; }
            keep[c[k]] = 1;
        }
        float n[3];
        mk_normal(pos[c[0]], pos[c[1]], pos[c[2]], n);
        if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2] || !(n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 0))
            bad++, printf("%s: degenerate triangle %d\n", name, t);
        /* turned over: against the mean normal of the original triangles at its corners */
        float m[3] = { 0, 0, 0 };
        for (int s = 0; s < norig; s++) {
            const unsigned short *o = orig + s * 3;
            for (int k = 0; k < 3; k++)
                if (o[0] == c[k] || o[1] == c[k] || o[2] == c[k]) {
                    float on[3];
                    mk_normal(pos[o[0]], pos[o[1]], pos[o[2]], on);
                    m[0] += on[0], m[1] += on[1], m[2] += on[2];
                    break;
                }
        }
        if (n[0] * m[0] + n[1] * m[1] + n[2] * m[2] <= 0)
            bad++, printf("%s: triangle %d turned over\n", name, t);
        if (group[c[0]] != group[c[1]] && group[c[0]] != group[c[2]] && group[c[1]] != group[c[2]])
            bad++, printf("%s: triangle %d spans three groups\n", name, t);
    }
    /* border and seam vertices of the original stay */
    unsigned char lock[sizeof keep] = { 0 };
    mk_lock_seams(pos, nverts, lock);
    for (int s = 0; s < norig; s++) /* open edges: an edge no other triangle has */
        for (int k = 0; k < 3; k++) {
            int a = orig[s * 3 + k], b = orig[s * 3 + (k + 1) % 3], shared = 0;
            for (int u = 0; u < norig && !shared; u++)
                for (int m2 = 0; m2 < 3 && u != s; m2++)
                    shared |= orig[u * 3 + m2] == b && orig[u * 3 + (m2 + 1) % 3] == a;
            if (!shared)
                lock[a] = lock[b] = 1;
        }
    for (int v = 0; v < nverts; v++)
        if (lock[v] && !keep[v]) {
            int used = 0;
            for (int i = 0; i < norig * 3 && !used; i++)
                used = orig[i] == v;
            if (used)
                bad++, printf("%s: seam vertex %d lost\n", name, v);
        }
    if (nout > target + target / 10)
        bad++, printf("%s: %d triangles left, target %d\n", name, nout, target);
    printf("%s: %d -> %d triangles (target %d), %d passes, %d collapses, %d refused, worst %.6f%s\n", name, norig, nout, target, st->passes,
           st->collapses, st->refused_flip, st->worst, bad ? "  BAD" : "");
    return bad;
}
int main(void)
{
    static unsigned short orig[20000], work[20000];
    int fails = 0, nverts;
    for (int shape = 0; shape < 2; shape++)
        for (int offline = 0; offline < 2; offline++) {
            int n = shape ? build_sphere(orig, &nverts) : build_grid(orig, &nverts), target = n / 2;
            memcpy(work, orig, n * 3 * sizeof *orig);
            MkStats st;
            int left = mk_simplify((const float (*)[3])pos, nverts, 0, shape ? 0 : group, work, n, target, 1e9f, offline ? 1 : 1 << 30, &st);
            char name[32];
            snprintf(name, sizeof name, "%s %s", shape ? "sphere" : "grid", offline ? "offline" : "online");
            fails += check(name, orig, n, work, left, nverts, target, &st) != 0;
            if (shape == 1 && offline == 1) { /* red control: one triangle turned over must be caught */
                unsigned short t = work[0];
                work[0] = work[1], work[1] = t;
                int caught = check("red control", orig, n, work, left, nverts, target, &st) != 0;
                printf("red control %s\n", caught ? "caught (good)" : "NOT caught");
                fails += !caught;
            }
        }
    puts(fails ? "FAIL" : "PASS");
    return fails != 0;
}
