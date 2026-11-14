/* Offline side of b211's meshkit: capture/<key>.bin (written by the game with p4_mesh_capture 1) -> override/<key>.bin
   (read with p4_mesh_override 1). Same simplifier as the PSP (port/meshkit.h), one collapse a pass (fresh costs).
     gcc -O2 -I port tools/meshkit_offline.c -o build/meshkit_offline
     build/meshkit_offline <capture dir> <override dir> <percent kept>
   capture: 'P4MK' 1 key nverts nmeshes, float pos[nverts][3], u8 group[nverts], per mesh u32 ntri + u16 tri[ntri][3]
   override: 'P4MO' 1 key nmeshes, per mesh u32 ntri + u16 tri[ntri][3]. Both hold game data: keep them local. */
#include <stdio.h>
#include <dirent.h>
#include "meshkit.h"

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
int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: meshkit_offline <capture dir> <override dir> <percent kept>\n");
        return 2;
    }
    int percent = atoi(argv[3]), files = 0, bad = 0;
    long tin = 0, tout = 0;
    DIR *dir = opendir(argv[1]);
    if (!dir) {
        fprintf(stderr, "no capture dir %s\n", argv[1]);
        return 2;
    }
    for (struct dirent *de; (de = readdir(dir));) {
        if (!strstr(de->d_name, ".bin"))
            continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], de->d_name);
        long size;
        unsigned char *d = load(path, &size), *p = d, *end = d + size;
        unsigned h[5];
        if (!d || size < 20 || (memcpy(h, d, 20), h[0] != 0x4b4d3450u || h[1] != 1) || 20 + (long)h[3] * 13 > size) {
            fprintf(stderr, "%s: not a capture\n", de->d_name), bad++;
            free(d);
            continue;
        }
        int nverts = h[3], nmeshes = h[4];
        const float (*pos)[3] = (const float (*)[3])(d + 20);
        const unsigned char *group = d + 20 + nverts * 12;
        p = d + 20 + nverts * 13;
        unsigned char *out = malloc(size + 16);
        long olen = 0;
        unsigned oh[4] = { 0x4f4d3450u, 1, h[2], (unsigned)nmeshes };
        memcpy(out, oh, 16), olen = 16;
        int ok = 1;
        for (int m = 0; m < nmeshes && ok; m++) {
            unsigned ntri;
            if (p + 4 > end) { ok = 0; break; }
            memcpy(&ntri, p, 4);
            if (p + 4 + ntri * 6 > end) { ok = 0; break; }
            unsigned short *tri = malloc(ntri * 6 + 6);
            memcpy(tri, p + 4, ntri * 6);
            p += 4 + ntri * 6;
            int n = ntri;
            if (ntri >= 8 && percent > 0 && percent < 100) {
                MkStats st;
                n = mk_simplify(pos, nverts, 0, group, tri, ntri, ntri * percent / 100, INFINITY, 1, &st);
            }
            unsigned count = n;
            memcpy(out + olen, &count, 4), olen += 4;
            memcpy(out + olen, tri, n * 6), olen += n * 6;
            tin += ntri, tout += n;
            free(tri);
        }
        if (ok) {
            snprintf(path, sizeof path, "%s/%s", argv[2], de->d_name);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(out, 1, olen, f);
                fclose(f);
                files++;
            } else
                bad++;
        } else
            fprintf(stderr, "%s: truncated\n", de->d_name), bad++;
        free(out), free(d);
    }
    closedir(dir);
    printf("%d geometries, triangles %ld -> %ld (%d%% asked), %d bad\n", files, tin, tout, percent, bad);
    return bad != 0;
}
