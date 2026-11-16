/* Host test for port/bone_lod.h (the far skin level's bone collapse). Build with any C compiler:
   cl /I port tools/test_bone_lod.c   or   cc -Iport tools/test_bone_lod.c -lm */
#include <stdio.h>
#include <string.h>
#include "bone_lod.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

/* skinToBone of a joint at (x, y, z) with no rotation: p * M = p - joint */
static void joint(float *m, float x, float y, float z)
{
    memset(m, 0, 16 * sizeof *m);
    m[0] = m[5] = m[10] = 1;
    m[12] = -x, m[13] = -y, m[14] = -z;
}

int main(void)
{
    /* root 0 (hips) -> spine 1 -> head 2 -> six face bones 3..8 (5 units from the head);
       spine 1 -> upper arm 9 -> forearm 10 -> hand 11 -> two fingers 12, 13; hips 0 -> thigh 14 -> shin 15 */
    enum { NB = 16 };
    short parent[NB] = { -1, 0, 1, 2, 2, 2, 2, 2, 2, 1, 9, 10, 11, 11, 0, 14 };
    float s2b[NB * 16], jp[NB][3] = {
        { 0, 100, 0 }, { 0, 130, 0 }, { 0, 165, 0 }, { 2, 168, 4 }, { -2, 168, 4 }, { 2, 162, 4 }, { -2, 162, 4 }, { 0, 165, 5 },
        { 0, 170, 3 }, { 20, 150, 0 }, { 45, 150, 0 }, { 70, 150, 0 }, { 78, 150, 1 }, { 78, 150, -1 }, { 10, 95, 0 }, { 10, 50, 0 } };
    for (int b = 0; b < NB; b++)
        joint(s2b + b * 16, jp[b][0], jp[b][1], jp[b][2]);
    /* 10 vertices a bone, 0 to 20 units from its joint along the bone's direction (face bones: within 1 unit) */
    enum { NV = NB * 10 };
    float pos[NV][3], weight[NV * 4] = { 0 };
    unsigned char bone[NV * 4] = { 0 };
    for (int b = 0; b < NB; b++)
        for (int k = 0; k < 10; k++) {
            int v = b * 10 + k;
            float reach = (b >= 3 && b <= 8) || b == 12 || b == 13 ? 0.1f : 2.0f;
            pos[v][0] = jp[b][0] + reach * k * (b >= 9 && b <= 13 ? 1 : 0);
            pos[v][1] = jp[b][1] - reach * k * (b == 14 || b == 15 ? 1 : 0);
            pos[v][2] = jp[b][2];
            bone[v * 4] = (unsigned char)b, weight[v * 4] = 1;
            if (k == 9 && parent[b] >= 0) /* a blended vertex at the joint */
                bone[v * 4 + 1] = (unsigned char)parent[b], weight[v * 4] = weight[v * 4 + 1] = 0.5f;
        }
    unsigned char map[NB];

    /* keep everything: identity */
    CHECK(bl_collapse(NB, parent, s2b, NV, (const float (*)[3])pos, bone, weight, NB, map) == NB);
    for (int b = 0; b < NB; b++)
        CHECK(map[b] == b);

    /* keep 8: the face bones and fingers (short reach) go first; every map is an ancestor or itself */
    int kept = bl_collapse(NB, parent, s2b, NV, (const float (*)[3])pos, bone, weight, 8, map);
    CHECK(kept == 8);
    int distinct = 0;
    unsigned char seen[NB] = { 0 };
    for (int b = 0; b < NB; b++) {
        int a = b;
        while (a >= 0 && a != map[b])
            a = parent[a];
        CHECK(a == map[b]); /* map[b] is on b's path to the root */
        CHECK(map[map[b]] == map[b]); /* kept bones map to themselves */
        if (!seen[map[b]])
            seen[map[b]] = 1, distinct++;
    }
    CHECK(distinct == 8);
    CHECK(map[0] == 0); /* the root stays */
    for (int b = 3; b <= 8; b++)
        CHECK(map[b] == 2); /* face -> head */
    CHECK(map[12] == 11 || map[12] == 10 || map[12] == 9); /* fingers -> the arm */
    CHECK(map[15] == 15 || map[15] == 14); /* the long shin is not folded into the hips first */

    /* keep 1: only the root remains */
    CHECK(bl_collapse(NB, parent, s2b, NV, (const float (*)[3])pos, bone, weight, 1, map) == 1);
    for (int b = 0; b < NB; b++)
        CHECK(map[b] == 0);

    /* two roots cannot merge: stops at 2 */
    short two[NB];
    memcpy(two, parent, sizeof two);
    two[14] = -1;
    CHECK(bl_collapse(NB, two, s2b, NV, (const float (*)[3])pos, bone, weight, 1, map) == 2);
    CHECK(map[15] == 14 && map[2] == 0);

    /* bad input: a parent after its child */
    short bad[NB];
    memcpy(bad, parent, sizeof bad);
    bad[1] = 5;
    CHECK(bl_collapse(NB, bad, s2b, NV, (const float (*)[3])pos, bone, weight, 8, map) == -1);

    /* parents from RpHAnim node flags: 0 PUSH; 1 (child of 0) PUSH POP leaf; 2 (child of 0) POP leaf */
    unsigned flags[3 * 4] = { 0 };
    flags[0 * 4] = 2, flags[1 * 4] = 3, flags[2 * 4] = 1;
    short p3[3];
    CHECK(bl_parents(3, flags, 4, p3) && p3[0] == -1 && p3[1] == 0 && p3[2] == 0);

    printf(failures ? "%d failures\n" : "bone_lod: all checks passed\n", failures);
    return failures != 0;
}
