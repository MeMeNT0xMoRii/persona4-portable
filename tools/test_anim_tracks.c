/* Host test for port/anim_tracks.h: per-node tracks against retail's AddAnimTime walk and compressed-key
   interpolation (as overrides/003d5bc0.c and 00415bc0.c of Izanagi bfb12b6 describe them), on synthetic animations.
   32-bit build (the RtAnim layouts hold 4-byte pointers): see the .cmd in docs or run
   vcvars32 + cl /O2 /I port tools/test_anim_tracks.c. Exit 0 when every compared word matches. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if UINTPTR_MAX > 0xffffffffu || defined(_WIN64) || defined(__LP64__)
#error "build 32-bit: the interpolator layout holds 4-byte pointers"
#endif

typedef void (*KInterp)(void *out, void *in1, void *in2, float time, void *data);
static unsigned p4_anim_tick;
static int p4_anim_lazy_check = 1;
static void p4_log(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}
#define memalign(a, n) malloc(n)
typedef int SceUID;
static SceUID sceKernelGetThreadId(void) { return 1; }
struct mallinfo { unsigned fordblks; };
static struct mallinfo mallinfo(void) { struct mallinfo m = { 64u << 20 }; return m; }
unsigned p4_heap_unclaimed(void) { return 0; }

static float half(unsigned short h)
{
    union { unsigned u; float f; } v;
    v.u = (h & 0x8000u) << 16;
    if (h & 0x7fff)
        v.u |= (((h & 0x7800u) << 12) + 0x38000000u) | ((h & 0x7ffu) << 12);
    return v.f;
}
static unsigned short to_half(float x)
{
    union { float f; unsigned u; } v = { x };
    unsigned e = (v.u >> 23) & 0xff, s = (v.u >> 16) & 0x8000;
    if (e < 0x71)
        return (unsigned short)s;
    if (e > 0x7f)
        return (unsigned short)(s | 0x7fff);
    return (unsigned short)(s | (e - 0x70) << 11 | ((v.u >> 12) & 0x7ff));
}

/* the reference interpolate: retail order for f, dot and translation; slerp by libm (both sides call this one for a
   slerp node, so only the linear case is compared against independent arithmetic) */
static unsigned ref_calls;
void p4r_00415bc0(void *out_, void *in1_, void *in2_, float time, void *data)
{
    unsigned char *out = out_, *in1 = in1_, *in2 = in2_;
    float *custom = data, q1[4], q2[4], t1[3], t2[3];
    for (int i = 0; i < 4; i++)
        q1[i] = half(*(unsigned short *)(in1 + 8 + i * 2)), q2[i] = half(*(unsigned short *)(in2 + 8 + i * 2));
    for (int i = 0; i < 3; i++)
        t1[i] = half(*(unsigned short *)(in1 + 16 + i * 2)), t2[i] = half(*(unsigned short *)(in2 + 16 + i * 2));
    float f = (time - *(float *)(in1 + 4)) / (*(float *)(in2 + 4) - *(float *)(in1 + 4));
    float dot = q1[1] * q2[1] + q1[0] * q2[0] + q1[2] * q2[2] + q1[3] * q2[3];
    for (int i = 0; i < 3; i++)
        *(float *)(out + 0x18 + i * 4) = (f * (t2[i] - t1[i]) + t1[i]) * custom[3 + i] + custom[i];
    if (dot < 0.0f) {
        for (int i = 0; i < 4; i++)
            q2[i] = -q2[i];
        dot = -dot;
    }
    float s0 = 1.0f - f, s1 = f;
    union { unsigned u; float f; } lim = { 0x3f7fbe77u };
    if (dot < lim.f) {
        float omega = acosf(dot), inv = 1.0f / sinf(omega);
        s0 = sinf(s0 * omega) * inv, s1 = sinf(s1 * omega) * inv;
    }
    for (int i = 0; i < 4; i++)
        *(float *)(out + 8 + i * 4) = s1 * q2[i] + s0 * q1[i];
    ref_calls++;
}

#include "anim_tracks.h"

/* retail AddAnimTime's walk and interpolation for a plain step (no loop, no callback) */
static void ref_step(char *ip)
{
    char *anim = *(char **)ip, *end = *(char **)(anim + 0x10) + *(int *)(anim + 4) * *(int *)(ip + 0x28);
    int nodes = *(int *)(ip + 0x2c), idx = 0, stride = *(int *)(ip + 0x24);
    while (*(char **)(ip + 8) < end && *(float *)(**(char ***)(ip + 8) + 4) <= *(float *)(ip + 4)) {
        char *next = *(char **)(ip + 8), *f;
        do {
            f = ip + 0x4c + idx * stride;
            if (++idx >= nodes)
                idx = 0;
        } while (*(char **)(f + 4) != *(char **)next);
        *(char **)f = *(char **)(f + 4);
        *(char **)(f + 4) = next;
        *(char **)(ip + 8) = next + *(int *)(ip + 0x28);
    }
    for (int i = 0; i < nodes; i++) {
        char *f = ip + 0x4c + i * stride;
        p4r_00415bc0(f, *(void **)f, *(void **)(f + 4), *(float *)(ip + 4), *(void **)(anim + 0x14));
    }
}

/* retail SubAnimTime (overrides/003d59d0.c) without wrap or callback: each pair steps back, every node interpolated,
   next-key pointer 0 */
static void ref_sub(char *ip, float dt)
{
    char *anim = *(char **)ip;
    int nodes = *(int *)(ip + 0x2c), stride = *(int *)(ip + 0x24);
    float *w = (float *)ip;
    w[1] = w[1] - dt;
    for (int i = 0; i < nodes; i++) {
        char *f = ip + 0x4c + i * stride, *prev = *(char **)f;
        while (w[1] < *(float *)(prev + 4)) {
            *(char **)(f + 4) = prev;
            *(char **)f = *(char **)prev;
            prev = *(char **)f;
        }
        p4r_00415bc0(f, prev, *(char **)(f + 4), w[1], *(void **)(anim + 0x14));
    }
    *(char **)(ip + 8) = 0;
}
/* retail AddAnimTime with the next-key pointer 0: SetCurrentAnim (pairs i, n + i), the time kept, then the walk */
static void ref_restart(char *ip)
{
    char *frames = *(char **)(*(char **)ip + 0x10);
    int nodes = *(int *)(ip + 0x2c), stride = *(int *)(ip + 0x24);
    for (int i = 0; i < nodes; i++)
        *(char **)(ip + 0x4c + i * stride) = frames + i * 24, *(char **)(ip + 0x4c + i * stride + 4) = frames + (nodes + i) * 24;
    *(char **)(ip + 8) = frames + 2 * nodes * 24;
    ref_step(ip);
}

static unsigned seed = 12345;
static float rnd(void) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; }

typedef struct { int node; float time; float q[4], t[3]; int index; } GenKey;

/* one synthetic animation: n nodes, keys at node-specific times, stream ordered like RtAnim's (first two keys of each
   node, then by the previous key's time); scramble 1 breaks that order */
static char *make_anim(int n, int per_node_max, float duration, int scramble, float custom[6], int *out_count)
{
    int cap = n * (per_node_max + 2);
    GenKey *k = calloc(cap, sizeof *k);
    int *len = calloc(n, sizeof *len), count = 0;
    int **list = calloc(n, sizeof *list);
    for (int i = 0; i < n; i++) {
        int m = 2 + (int)(rnd() * per_node_max);
        list[i] = calloc(m, sizeof **list);
        float q[4] = { rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f };
        int big = i % 3 == 0; /* every third node turns fast: slerp segments */
        for (int j = 0; j < m; j++) {
            GenKey *g = &k[count];
            g->node = i, g->time = j == 0 ? 0.0f : j == m - 1 ? duration : duration * (j + rnd() * 0.5f) / (m - 1);
            for (int c = 0; c < 4; c++)
                q[c] += (rnd() - 0.5f) * (big ? 0.9f : 0.02f);
            float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            float sgn = big && rnd() < 0.3f ? -1.0f : 1.0f; /* a negated quaternion: negative dot */
            for (int c = 0; c < 4; c++)
                g->q[c] = sgn * q[c] / l * 0.99995f;
            for (int c = 0; c < 3; c++)
                g->t[c] = rnd() - 0.5f;
            list[i][j] = count++;
        }
        len[i] = m;
    }
    /* stream: node i key 0 at i, key 1 at n + i, then the rest ordered by their previous key's time */
    int *order = malloc(count * sizeof *order), *prev = malloc(count * sizeof *prev), o = 0;
    for (int i = 0; i < n; i++)
        order[o++] = list[i][0];
    for (int i = 0; i < n; i++)
        order[o++] = list[i][1];
    int rest = o;
    for (int i = 0; i < n; i++)
        for (int j = 2; j < len[i]; j++)
            order[o++] = list[i][j], prev[list[i][j]] = list[i][j - 1];
    for (int a = rest + 1; a < count; a++) { /* insertion sort, stable */
        int x = order[a], b = a - 1;
        while (b >= rest && k[prev[order[b]]].time > k[prev[x]].time)
            order[b + 1] = order[b], b--;
        order[b + 1] = x;
    }
    if (scramble) /* two neighbours of different nodes swapped: still a stream retail can walk, out of time order */
        for (int a = rest; a + 1 < count; a++)
            if (k[order[a]].node != k[order[a + 1]].node && k[prev[order[a]]].time < k[prev[order[a + 1]]].time) {
                int x = order[a];
                order[a] = order[a + 1], order[a + 1] = x;
                break;
            }
    char *anim = calloc(1, 0x20), *frames = calloc(count, 24);
    int *at = malloc(count * sizeof *at);
    for (int s = 0; s < count; s++)
        at[order[s]] = s;
    for (int s = 0; s < count; s++) {
        GenKey *g = &k[order[s]];
        char *d = frames + s * 24;
        int pos_in_node = -1;
        for (int j = 0; j < len[g->node]; j++)
            if (list[g->node][j] == order[s])
                pos_in_node = j;
        *(char **)d = pos_in_node >= 1 ? frames + at[list[g->node][pos_in_node - 1]] * 24 : 0;
        *(float *)(d + 4) = g->time;
        for (int c = 0; c < 4; c++)
            *(unsigned short *)(d + 8 + c * 2) = to_half(g->q[c]);
        for (int c = 0; c < 3; c++)
            *(unsigned short *)(d + 16 + c * 2) = to_half(g->t[c]);
    }
    *(int *)(anim + 4) = count;
    *(float *)(anim + 0xc) = duration;
    *(char **)(anim + 0x10) = frames;
    *(float **)(anim + 0x14) = custom;
    *out_count = count;
    free(k), free(len), free(order), free(prev), free(at);
    for (int i = 0; i < n; i++)
        free(list[i]);
    free(list);
    return anim;
}
/* SetCurrentAnim: time 0, node i on keys i and n + i, next stream key 2n */
static char *make_interp(char *anim, int n, int stride)
{
    char *ip = calloc(1, 0x4c + n * stride), *frames = *(char **)(anim + 0x10);
    *(char **)ip = anim;
    *(char **)(ip + 8) = frames + 2 * n * 24;
    *(int *)(ip + 0x24) = stride, *(int *)(ip + 0x28) = 24, *(int *)(ip + 0x2c) = n;
    *(KInterp *)(ip + 0x44) = (KInterp)p4r_00415bc0;
    for (int i = 0; i < n; i++)
        *(char **)(ip + 0x4c + i * stride) = frames + i * 24, *(char **)(ip + 0x4c + i * stride + 4) = frames + (n + i) * 24;
    return ip;
}
static int compare(const char *a, const char *b, int n, int stride, const char *what, int step)
{
    for (int w = 4; w < 0x4c + n * stride; w += 4) {
        if (w >= 0xc && w < 0x4c)
            continue;
        if (*(const unsigned *)(a + w) != *(const unsigned *)(b + w)) {
            printf("FAIL %s step %d: offset %x tracks %08x retail %08x\n", what, step, w, *(const unsigned *)(a + w), *(const unsigned *)(b + w));
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    int failures = 0;
    struct { int n, keys, stride, scramble; float duration; } cases[] = {
        { 1, 3, 36, 0, 1.0f }, { 7, 12, 36, 0, 2.0f }, { 40, 30, 36, 0, 5.0f }, { 120, 8, 40, 0, 3.0f }, { 30, 20, 36, 1, 4.0f },
    };
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
        float custom[6] = { 0.25f, -1.5f, 2.0f, 3.0f, 0.5f, 1.25f };
        int count;
        char *anim = make_anim(cases[c].n, cases[c].keys, cases[c].duration, cases[c].scramble, custom, &count);
        char *a = make_interp(anim, cases[c].n, cases[c].stride), *b = make_interp(anim, cases[c].n, cases[c].stride);
        unsigned steps_before = track_steps, fallbacks_before = track_refused;
        float t = 0;
        int handled = 0, ok = 1, back_handled = 0, restarts_handled = 0;
        for (int step = 0; ok && t < cases[c].duration - 0.05f; step++) {
            float dt = step % 7 == 3 ? 0.4f : 0.0167f + rnd() * 0.02f; /* some steps cross several keys */
            if (t + dt > cases[c].duration)
                dt = cases[c].duration - t;
            t += dt;
            *(float *)(a + 4) = t, *(float *)(b + 4) = t;
            p4_anim_tick++;
            if (anim_track_step(a))
                handled++;
            else
                ref_step(a);
            ref_step(b);
            ok = compare(a, b, cases[c].n, cases[c].stride, cases[c].scramble ? "scrambled" : "ordered", step);
            /* mdlAdvanceLayer's dispatch layers: SetCurrentTime(t - back) then SetCurrentTime(t): a step back, then a
               step forward from next-key pointer 0 (retail restarts) */
            float back = step % 3 == 0 ? 1.0f : 0.05f + rnd() * 0.3f;
            if (ok && !cases[c].scramble && t - back >= 0.0f) {
                float ta = *(float *)(a + 4) - back;
                *(float *)(a + 4) = ta;
                int by_tracks = anim_track_sample(a, 1);
                if (!by_tracks) {
                    *(float *)(a + 4) += back;
                    ref_sub(a, back);
                }
                ref_sub(b, back);
                back_handled += by_tracks;
                ok = compare(a, b, cases[c].n, cases[c].stride, "step back", step);
                *(float *)(a + 4) = *(float *)(b + 4) = t;
                if (ok && anim_track_sample(a, 0))
                    restarts_handled++;
                else
                    ref_restart(a);
                ref_restart(b);
                ok = ok && compare(a, b, cases[c].n, cases[c].stride, "restart", step);
            }
        }
        int expect_tracks = !cases[c].scramble;
        if (expect_tracks && !handled)
            printf("FAIL case %u: tracks never used (refused %u)\n", c, track_refused - fallbacks_before), ok = 0;
        if (!expect_tracks && handled)
            printf("FAIL case %u: a stream out of order took the tracks\n", c), ok = 0;
        if (expect_tracks && cases[c].duration > 1.5f && (!back_handled || !restarts_handled))
            printf("FAIL case %u: steps back %d, restarts %d by tracks\n", c, back_handled, restarts_handled), ok = 0;
        printf("case %u: %d nodes %d keys, %s, %d steps by tracks (%u total, %d back, %d restarts), slerps %u: %s\n", c, cases[c].n,
               count, cases[c].scramble ? "scrambled stream" : "ordered stream", handled, track_steps - steps_before, back_handled,
               restarts_handled, track_slerps, ok ? "ok" : "FAILED");
        failures += !ok;
        /* keys rewritten after the build: the check rebuilds instead of using stale floats */
        if (expect_tracks && c == 2) {
            char *frames = *(char **)(anim + 0x10);
            *(unsigned short *)(frames + 8) ^= 0x0400;
            *(float *)(a + 4) = *(float *)(b + 4) = t;
            if (anim_track_step(a))
                printf("FAIL: changed keys used without a rebuild\n"), failures++;
        }
        free(a), free(b), free(*(char **)(anim + 0x10)), free(anim);
    }
    anim_track_report();
    printf(failures ? "anim tracks: %d FAILED\n" : "anim tracks: all ok\n", failures);
    return failures != 0;
}
