/* b250 strip pack: triangle strips made on the PC (tools/psp_strips.py, from the player's own ISO) for the GE skin
   batches, looked up by a batch's own index list. Pure C, no PSP calls: port/rwsky.c reads the file, the host check
   is tools/test_strippack.c.

   File: 'P4SP' 1 count data_offset, count x SpEntry sorted by key then corners, then per batch
   u16 strips, u16 total, u16 length[strips], u16 index[total] (padded to 4 bytes).
   A strip: triangle k is (i[k], i[k+1], i[k+2]), wound the other way for odd k; equal corners draw nothing.
   Nothing here is trusted: sp_same compares what the strips draw with the batch's list before one is used. */
#ifndef P4_STRIPPACK_H
#define P4_STRIPPACK_H
#include <stdlib.h>
#include <string.h>

enum { SP_MAGIC = 0x50533450, SP_VERSION = 1 };
typedef struct { unsigned key, corners, offset; } SpEntry;

/* FNV-1a over the list's bytes (little-endian u16), as fnv1a in tools/psp_strips.py */
static unsigned sp_key(const unsigned short *list, int corners)
{
    unsigned h = 0x811C9DC5u;
    for (int k = 0; k < corners; k++) {
        h = (h ^ (list[k] & 0xff)) * 0x01000193u;
        h = (h ^ (list[k] >> 8)) * 0x01000193u;
    }
    return h;
}

static const SpEntry *sp_find(const SpEntry *index, int count, unsigned key, unsigned corners)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const SpEntry *e = &index[mid];
        if (e->key == key && e->corners == corners)
            return e;
        if (e->key < key || (e->key == key && e->corners < corners))
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* the words of an entry that starts with these two (strips, total); 0: not an entry */
static int sp_entry_words(const unsigned short *head)
{
    return head[0] && head[1] >= 3 ? 2 + head[0] + head[1] : 0;
}

/* One strip from the entry's strips, as join in tools/psp_strips.py: a strip that starts with the index the one
   before ended on follows it directly, otherwise the last index and the next first one are repeated; one more copy
   of the first index when the strip would start on an odd position (its first triangle must keep its winding).
   Returns the count, 0 when the entry is malformed or does not fit cap. */
static int sp_join(const unsigned short *entry, unsigned short *out, int cap)
{
    int strips = entry[0], total = entry[1], n = 0, sum = 0;
    const unsigned short *len = entry + 2, *s = len + strips;
    for (int k = 0; k < strips; k++) {
        int m = len[k];
        sum += m;
        if (m < 3 || sum > total || n + m + 3 > cap)
            return 0;
        if (n && out[n - 1] != s[0])
            out[n] = out[n - 1], out[n + 1] = s[0], n += 2;
        if (n & 1)
            out[n++] = s[0];
        memcpy(out + n, s, m * sizeof *out);
        n += m, s += m;
    }
    return sum == total ? n : 0;
}

/* The entry's strips one after the other, unjoined (each its own PRIM): out gets the indices, lens the lengths.
   Returns the strip count, 0 when malformed or over cap / max_strips. */
static int sp_split(const unsigned short *entry, unsigned short *out, int cap, unsigned short *lens, int max_strips)
{
    int strips = entry[0], total = entry[1], sum = 0;
    if (strips > max_strips || total > cap)
        return 0;
    for (int k = 0; k < strips; k++) {
        lens[k] = entry[2 + k];
        sum += lens[k];
        if (lens[k] < 3)
            return 0;
    }
    if (sum != total)
        return 0;
    memcpy(out, entry + 2 + strips, total * sizeof *out);
    return strips;
}

/* a triangle by its corners, rotated so the smallest comes first (keeps the winding) */
static unsigned long long sp_canon(unsigned a, unsigned b, unsigned c)
{
    if (b < a && b < c) { unsigned t = a; a = b; b = c; c = t; }
    else if (c < a && c < b) { unsigned t = c; c = b; b = a; a = t; }
    return (unsigned long long)a << 32 | (unsigned long long)b << 16 | c;
}
static int sp_ull_cmp(const void *a, const void *b)
{
    unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
    return x < y ? -1 : x > y;
}

/* 1 when the strips (indices idx, nstrips lengths in lens; lens 0: one strip of n indices) draw exactly the list's
   triangles: the same corners with the same winding, each as often as the list has it, every index below vertices.
   Triangles of the list with two equal corners draw nothing and are not counted. 0 also when out of memory. */
static int sp_same(const unsigned short *list, int corners, const unsigned short *idx, int n, const unsigned short *lens, int nstrips, int vertices)
{
    int tris = corners / 3, want_n = 0, got_n = 0, ok = 1;
    unsigned long long *want = malloc((tris ? tris : 1) * sizeof *want), *got = malloc((tris ? tris : 1) * sizeof *got);
    if (!want || !got) {
        free(want), free(got);
        return 0;
    }
    for (int t = 0; t < tris; t++) {
        const unsigned short *c = list + t * 3;
        if (c[0] != c[1] && c[1] != c[2] && c[0] != c[2])
            want[want_n++] = sp_canon(c[0], c[1], c[2]);
    }
    for (int k = 0; k < n && ok; k++)
        ok = idx[k] < vertices;
    unsigned short whole = (unsigned short)n;
    if (!lens)
        lens = &whole, nstrips = 1, ok = ok && n < 65536;
    int at = 0;
    for (int s = 0; s < nstrips && ok; s++) {
        const unsigned short *p = idx + at;
        int m = lens[s];
        at += m;
        if (at > n) {
            ok = 0;
            break;
        }
        for (int i = 0; i + 2 < m && ok; i++) {
            unsigned a = p[i], b = p[i + 1], c = p[i + 2];
            if (a == b || b == c || a == c)
                continue;
            if (got_n >= want_n)
                ok = 0;
            else
                got[got_n++] = i & 1 ? sp_canon(b, a, c) : sp_canon(a, b, c);
        }
    }
    if (ok && at == n && got_n == want_n) {
        qsort(want, want_n, sizeof *want, sp_ull_cmp);
        qsort(got, got_n, sizeof *got, sp_ull_cmp);
        ok = !memcmp(want, got, want_n * sizeof *want);
    } else
        ok = 0;
    free(want), free(got);
    return ok;
}
#endif
