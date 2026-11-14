/* VU0 and the EE's 128-bit registers as plain C, for the code tools/psp_vu0.py generates from inline assembly.
   The state is shared: the game loads a register in one asm statement and uses it in the next.
   ponytail: a function call per instruction, VFPU when effects show up in a device profile. */
#ifndef P4_VU0_H
#define P4_VU0_H

typedef union {
    unsigned w[4];
    int s[4];
    float f[4];
    unsigned short h[8];
    unsigned char b[16];
    unsigned long long d[2];
} __attribute__((aligned(16))) P4Reg;

extern struct P4Vu { P4Reg vf[32], acc, r[32]; float q; } p4vu;

/* Count translated work by site in profiling builds or when enabled by the debugger. */
extern int p4_vu0_profile;
void p4_vu0_site(unsigned short *id, const char *file, int line, int ops);
#define P4_VU0_COUNT(n) do { \
    if (p4_vu0_profile) { \
        static unsigned short p4_vu0_id_; \
        p4_vu0_site(&p4_vu0_id_, __FILE__, __LINE__, (n)); \
    } \
} while (0)


static __inline__ void p4_load(P4Reg *r, const void *from, unsigned size)
{
    __builtin_memset(r, 0, 16);
    __builtin_memcpy(r, from, size > 16 ? 16 : size);
}

/* kind: 0 add, 1 sub, 2 mul, 3 ACC + s * t, 4 ACC - s * t. lane: -1 per lane, 0-3 broadcast, -2 the Q register. */
static __inline__ void p4_vop(int kind, int mask, P4Reg *d, const P4Reg *s, const P4Reg *t, int lane)
{
    float out[4];
    int i;
    for (i = 0; i < 4; i++) {
        float a = s->f[i], b = lane == -2 ? p4vu.q : t->f[lane < 0 ? i : lane];
        out[i] = kind == 0 ? a + b : kind == 1 ? a - b : kind == 2 ? a * b : kind == 3 ? p4vu.acc.f[i] + a * b : p4vu.acc.f[i] - a * b;
    }
    for (i = 0; i < 4; i++)
        if (mask >> i & 1)
            d->f[i] = out[i];
}

static __inline__ void p4_vmove(int mask, P4Reg *d, const P4Reg *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (mask >> i & 1)
            d->w[i] = s->w[i];
}

static __inline__ void p4_vabs(int mask, P4Reg *d, const P4Reg *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (mask >> i & 1)
            d->w[i] = s->w[i] & 0x7FFFFFFFu;
}

static __inline__ void p4_vitof(int mask, P4Reg *d, const P4Reg *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (mask >> i & 1)
            d->f[i] = (float)s->s[i];
}

static __inline__ void p4_vftoi(int mask, P4Reg *d, const P4Reg *s)
{
    int i;
    for (i = 0; i < 4; i++)
        if (mask >> i & 1) {
            float f = s->f[i];
            d->s[i] = f >= 2147483520.0f ? 0x7FFFFFFF : f <= -2147483648.0f ? (int)0x80000000 : (int)f;
        }
}

static __inline__ void p4_vopmula(const P4Reg *s, const P4Reg *t)
{
    p4vu.acc.f[0] = s->f[1] * t->f[2];
    p4vu.acc.f[1] = s->f[2] * t->f[0];
    p4vu.acc.f[2] = s->f[0] * t->f[1];
}

static __inline__ void p4_vopmsub(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    float x = p4vu.acc.f[0] - s->f[1] * t->f[2];
    float y = p4vu.acc.f[1] - s->f[2] * t->f[0];
    float z = p4vu.acc.f[2] - s->f[0] * t->f[1];
    d->f[0] = x;
    d->f[1] = y;
    d->f[2] = z;
}

/* A VU divide by zero gives the largest float, never a NaN or a trap. */
static __inline__ float p4_divide(float a, float b)
{
    return b != 0.0f ? a / b : 3.4028235e38f;
}

static __inline__ void p4_vdiv(float a, float b) { p4vu.q = p4_divide(a, b); }
static __inline__ void p4_vrsqrt(float a, float b) { p4vu.q = p4_divide(a, __builtin_sqrtf(b < 0 ? -b : b)); }
static __inline__ void p4_vsqrt(float b) { p4vu.q = __builtin_sqrtf(b < 0 ? -b : b); }

static __inline__ void p4_float_to_reg(P4Reg *d, float f)
{
    __builtin_memcpy(d, &f, 4);
    d->s[1] = d->s[0] >> 31;
}

static __inline__ void p4_lw(P4Reg *d, const char *from)
{
    __builtin_memcpy(d, from, 4);
    d->s[1] = d->s[0] >> 31;
}

static __inline__ void p4_lwu(P4Reg *d, const char *from)
{
    __builtin_memcpy(d, from, 4);
    d->w[1] = 0;
}

static __inline__ void p4_pextlb(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    P4Reg r;
    int i;
    for (i = 0; i < 8; i++) {
        r.b[2 * i] = t->b[i];
        r.b[2 * i + 1] = s->b[i];
    }
    *d = r;
}

static __inline__ void p4_pextlh(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    P4Reg r;
    int i;
    for (i = 0; i < 4; i++) {
        r.h[2 * i] = t->h[i];
        r.h[2 * i + 1] = s->h[i];
    }
    *d = r;
}

static __inline__ void p4_pextuw(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    P4Reg r;
    r.w[0] = t->w[2];
    r.w[1] = s->w[2];
    r.w[2] = t->w[3];
    r.w[3] = s->w[3];
    *d = r;
}

/* PEXEW rd, rt: words 0 and 2 change places (effectVuGetZ10 takes VF10.z this way). */
static __inline__ void p4_pexew(P4Reg *d, const P4Reg *t)
{
    P4Reg r = *t;
    r.w[0] = t->w[2];
    r.w[2] = t->w[0];
    *d = r;
}

static __inline__ void p4_ppach(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    P4Reg r;
    int i;
    for (i = 0; i < 4; i++) {
        r.h[i] = t->h[2 * i];
        r.h[4 + i] = s->h[2 * i];
    }
    *d = r;
}

static __inline__ void p4_ppacb(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    P4Reg r;
    int i;
    for (i = 0; i < 8; i++) {
        r.b[i] = t->b[2 * i];
        r.b[8 + i] = s->b[2 * i];
    }
    *d = r;
}

static __inline__ void p4_pcpyld(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    unsigned long long low = t->d[0], high = s->d[0];
    d->d[0] = low;
    d->d[1] = high;
}

static __inline__ void p4_pcpyud(P4Reg *d, const P4Reg *s, const P4Reg *t)
{
    unsigned long long low = s->d[1], high = t->d[1];
    d->d[0] = low;
    d->d[1] = high;
}

static __inline__ void p4_prot3w(P4Reg *d, const P4Reg *t)
{
    P4Reg r;
    r.w[0] = t->w[1];
    r.w[1] = t->w[2];
    r.w[2] = t->w[0];
    r.w[3] = t->w[3];
    *d = r;
}

static __inline__ void p4_or(P4Reg *d, const P4Reg *s, const P4Reg *t) { d->d[0] = s->d[0] | t->d[0]; }
static __inline__ void p4_and(P4Reg *d, const P4Reg *s, const P4Reg *t) { d->d[0] = s->d[0] & t->d[0]; }
static __inline__ void p4_daddu(P4Reg *d, const P4Reg *s, const P4Reg *t) { d->d[0] = s->d[0] + t->d[0]; }
static __inline__ void p4_dsll32(P4Reg *d, const P4Reg *t) { d->d[0] = t->d[0] << 32; }
static __inline__ void p4_dsrl32(P4Reg *d, const P4Reg *t) { d->d[0] = t->d[0] >> 32; }

#endif
