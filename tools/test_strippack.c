/* Host check of port/strippack.h.
     cl /O2 /W4 /I port tools/test_strippack.c   (or gcc -O2 -I port tools/test_strippack.c -o build/test_strippack)
     test_strippack                      the made-up cases and their red controls
     test_strippack p4strips.bin lists   also every batch of a real pack: tools/psp_strips.py --lists writes the
                                         index lists the pack was made from (game data, stays local)
   Made-up cases: a grid's triangles as a strip a row, joined in the four ways sp_join has (the next strip starts
   on the last index or not, on an even or odd position, with the exact number of join indices) and split; each
   must draw the list's triangles.
   Red controls, each must be refused: one triangle turned over, one missing, one drawn twice, an index past the
   vertices, lengths that do not add up. A real pack: every list must be found by its key and its strips must draw
   it, joined and split, in fewer indices than the list. */
#include <stdio.h>
#include "strippack.h"

static int failures;
static void expect(int ok, const char *what)
{
    if (!ok)
        printf("FAIL %s\n", what), failures++;
}

enum { W = 6, H = 4, VERTS = (W + 1) * (H + 1), TRIS = W * H * 2 };
static unsigned short grid_list[TRIS * 3];
static void build_grid(void)
{
    int t = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            unsigned short a = (unsigned short)(y * (W + 1) + x), b = a + 1, c = a + W + 1, d = c + 1;
            unsigned short q[6] = { a, c, b, b, c, d };
            memcpy(grid_list + t * 3, q, sizeof q);
            t += 2;
        }
}
/* one row of the grid as a strip: a0 c0 a1 c1 ... */
static int row_strip(int y, unsigned short *out)
{
    int n = 0;
    for (int x = 0; x <= W; x++) {
        out[n++] = (unsigned short)(y * (W + 1) + x);
        out[n++] = (unsigned short)((y + 1) * (W + 1) + x);
    }
    return n;
}
/* the grid as an entry, a strip a row. lead: every row after the first begins with the index the row before ended on
   and its own first index (two zero-area triangles), so sp_join finds it starting where the last one ended.
   odd: every row ends with a copy of its last index (one zero-area triangle), so the joins land on odd positions. */
static int grid_entry(unsigned short *entry, int lead, int odd)
{
    unsigned short *len = entry + 2, *s = len + H, last = 0;
    int total = 0;
    for (int y = 0; y < H; y++) {
        unsigned short row[2 * (W + 1)];
        int n = row_strip(y, row), from = total;
        if (lead && y)
            s[total++] = last, s[total++] = row[0];
        memcpy(s + total, row, n * sizeof *s);
        total += n;
        if (odd)
            s[total] = s[total - 1], total++;
        last = s[total - 1];
        len[y] = (unsigned short)(total - from);
    }
    entry[0] = H, entry[1] = (unsigned short)total;
    return total;
}

static void made_up(void)
{
    static unsigned short entry[4096], out[4096], lens[64];
    build_grid();
    for (int lead = 0; lead < 2; lead++)
        for (int odd = 0; odd < 2; odd++) {
            char what[64];
            grid_entry(entry, lead, odd);
            int n = sp_join(entry, out, 4096);
            snprintf(what, sizeof what, "join lead %d odd %d", lead, odd);
            expect(n > 0 && sp_same(grid_list, TRIS * 3, out, n, 0, 0, VERTS), what);
            /* a strip that starts where the last ended costs nothing, or one index on an odd position; another two or three */
            int extra = n - entry[1], want = (H - 1) * (lead ? odd : 2 + odd);
            snprintf(what, sizeof what, "join cost lead %d odd %d: %d, expected %d", lead, odd, extra, want);
            expect(extra == want, what);
            int ns = sp_split(entry, out, 4096, lens, 64);
            snprintf(what, sizeof what, "split lead %d odd %d", lead, odd);
            expect(ns == H && sp_same(grid_list, TRIS * 3, out, entry[1], lens, ns, VERTS), what);
        }
    /* red controls on the joined plain entry */
    grid_entry(entry, 0, 0);
    int n = sp_join(entry, out, 4096);
    expect(sp_same(grid_list, TRIS * 3, out, n, 0, 0, VERTS), "control: untouched");
    unsigned short keep = out[0];
    out[0] = out[1], out[1] = keep; /* the first triangle turned over (and the second changed) */
    expect(!sp_same(grid_list, TRIS * 3, out, n, 0, 0, VERTS), "red: a triangle turned over");
    out[1] = out[0], out[0] = keep;
    expect(!sp_same(grid_list, TRIS * 3, out, n - 1, 0, 0, VERTS), "red: a triangle missing");
    out[n] = out[n - 3], out[n + 1] = out[n - 2], out[n + 2] = out[n - 1]; /* ..., a, b, c, a, b, c: the last one again (and more) */
    expect(!sp_same(grid_list, TRIS * 3, out, n + 3, 0, 0, VERTS), "red: a triangle twice");
    expect(!sp_same(grid_list, TRIS * 3, out, n, 0, 0, VERTS - 1), "red: an index past the vertices");
    expect(!sp_same(grid_list, TRIS * 3 - 3, out, n, 0, 0, VERTS), "red: a list one triangle shorter");
    entry[2]++;
    expect(!sp_join(entry, out, 4096) && !sp_split(entry, out, 4096, lens, 64), "red: lengths that do not add up");
    entry[2]--;
    expect(!sp_join(entry, out, entry[1]), "red: no room for the joins");
    expect(!sp_split(entry, out, 4096, lens, H - 1), "red: more strips than room");
    /* a list with a zero-area triangle: it draws nothing and is not asked of the strip */
    static unsigned short list2[TRIS * 3 + 3];
    memcpy(list2, grid_list, sizeof grid_list);
    list2[TRIS * 3] = 1, list2[TRIS * 3 + 1] = 1, list2[TRIS * 3 + 2] = 2;
    n = sp_join(entry, out, 4096);
    expect(sp_same(list2, TRIS * 3 + 3, out, n, 0, 0, VERTS), "zero-area triangle in the list");
    /* the index */
    SpEntry index[5] = { { 5, 9, 100 }, { 7, 6, 200 }, { 7, 12, 300 }, { 9, 6, 400 }, { 0xfffffff0u, 6, 500 } };
    for (int k = 0; k < 5; k++)
        expect(sp_find(index, 5, index[k].key, index[k].corners) == &index[k], "find");
    expect(!sp_find(index, 5, 7, 9) && !sp_find(index, 5, 6, 6) && !sp_find(index, 0, 5, 9), "find: absent");
    unsigned short one[3] = { 0x0201, 0x0403, 0x0605 };
    unsigned h = 0x811C9DC5u;
    for (int k = 1; k <= 6; k++)
        h = (h ^ (unsigned)k) * 0x01000193u;
    expect(sp_key(one, 3) == h, "key: FNV-1a over the bytes");
}

static unsigned char *load(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(*size ? *size : 1);
    if (d && fread(d, 1, *size, f) != (size_t)*size)
        free(d), d = 0;
    fclose(f);
    return d;
}

/* lists: 'P4SL' count, then per batch u32 corners and corners x u16 (padded to 4 bytes) */
static void real_pack(const char *pack_path, const char *lists_path)
{
    long psize, lsize;
    unsigned char *pack = load(pack_path, &psize), *lists = load(lists_path, &lsize);
    if (!pack || !lists || psize < 16 || lsize < 8) {
        printf("FAIL cannot read %s or %s\n", pack_path, lists_path), failures++;
        return;
    }
    unsigned head[4], lhead[2];
    memcpy(head, pack, 16), memcpy(lhead, lists, 8);
    expect(head[0] == SP_MAGIC && head[1] == SP_VERSION && head[3] == 16 + head[2] * sizeof(SpEntry) && (long)head[3] <= psize, "pack header");
    expect(lhead[0] == 0x4c533450u && lhead[1] == head[2], "lists header: one list a pack entry");
    if (failures)
        return;
    const SpEntry *index = (const SpEntry *)(pack + 16);
    int count = head[2];
    for (int k = 1; k < count; k++)
        if (index[k - 1].key > index[k].key || (index[k - 1].key == index[k].key && index[k - 1].corners >= index[k].corners)) {
            expect(0, "index sorted by key then corners");
            break;
        }
    long at = 8, list_idx = 0, joined_idx = 0, split_idx = 0, strips = 0, bad = 0, biggest = 0;
    for (unsigned b = 0; b < lhead[1]; b++) {
        unsigned corners;
        memcpy(&corners, lists + at, 4);
        const unsigned short *list = (const unsigned short *)(lists + at + 4);
        at += 4 + ((corners * 2 + 3) & ~3u);
        if (at > lsize) {
            expect(0, "lists file cut short");
            break;
        }
        int vertices = 0, ok = 0;
        for (unsigned k = 0; k < corners; k++)
            if (list[k] >= vertices)
                vertices = list[k] + 1;
        const SpEntry *e = sp_find(index, count, sp_key(list, corners), corners);
        if (e && !(e->offset & 3) && (long)e->offset + 4 <= psize) {
            const unsigned short *entry = (const unsigned short *)(pack + e->offset);
            int words = sp_entry_words(entry), cap = 6 * (corners / 3) + 6;
            if (words && (long)e->offset + words * 2 <= psize) {
                unsigned short *out = malloc(cap * sizeof *out), *lens = malloc(entry[0] * sizeof *lens);
                int n = sp_join(entry, out, cap);
                ok = n && n < (int)corners && sp_same(list, corners, out, n, 0, 0, vertices);
                int ns = sp_split(entry, out, cap, lens, entry[0]);
                ok = ok && ns == entry[0] && sp_same(list, corners, out, entry[1], lens, ns, vertices);
                list_idx += corners, joined_idx += n, split_idx += entry[1], strips += ns;
                if (words * 2 > biggest)
                    biggest = words * 2;
                free(out), free(lens);
            }
        }
        bad += !ok;
    }
    expect(!bad, "every list found, its strips drawing it, joined and split");
    printf("%s: %u batches, %ld refused; indices: lists %ld, joined %ld (%.3f a triangle), split %ld (%.3f a triangle, %.1f strips a batch); biggest entry %ld bytes\n",
           pack_path, lhead[1], bad, list_idx, joined_idx, joined_idx * 3.0 / (list_idx ? list_idx : 1), split_idx, split_idx * 3.0 / (list_idx ? list_idx : 1),
           (double)strips / (lhead[1] ? lhead[1] : 1), biggest);
    free(pack), free(lists);
}

int main(int argc, char **argv)
{
    made_up();
    if (argc >= 3)
        real_pack(argv[1], argv[2]);
    printf(failures ? "FAILED: %d\n" : "PASS\n", failures);
    return failures != 0;
}
