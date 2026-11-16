/* The VU0 math of RenderWare and the game, in plain C. VFPU when a profile on the device says so. */
#include "vu0.h"

typedef struct { float x, y, z; unsigned flags; } Row;
typedef struct { Row right, up, at, pos; } Matrix; /* 64 bytes, rows like RwMatrix */

#define ROTATE(out, m, in) do { \
    (out).x = (m).x * (in)->right.x + (m).y * (in)->up.x + (m).z * (in)->at.x; \
    (out).y = (m).x * (in)->right.y + (m).y * (in)->up.y + (m).z * (in)->at.y; \
    (out).z = (m).x * (in)->right.z + (m).y * (in)->up.z + (m).z * (in)->at.z; \
} while (0)

/* out = a * b. RenderWare keeps two words of flags: the result has lost identity and may have lost orthonormality. */
unsigned char *RwMatrixMultiply(unsigned char *out_, unsigned char *a_, unsigned char *b_)
{
    Matrix *out = (Matrix *)out_, *a = (Matrix *)a_, *b = (Matrix *)b_;
    /* The PS2 loads both inputs before it writes: out may be a or b (fix from MeMeNT0xMoRii's fork). */
    unsigned flags = a->right.flags & b->right.flags;
    Matrix r;
    ROTATE(r.right, a->right, b);
    ROTATE(r.up, a->up, b);
    ROTATE(r.at, a->at, b);
    ROTATE(r.pos, a->pos, b);
    r.pos.x += b->pos.x;
    r.pos.y += b->pos.y;
    r.pos.z += b->pos.z;
    out->right.x = r.right.x; out->right.y = r.right.y; out->right.z = r.right.z;
    out->up.x = r.up.x; out->up.y = r.up.y; out->up.z = r.up.z;
    out->at.x = r.at.x; out->at.y = r.at.y; out->at.z = r.at.z;
    out->pos.x = r.pos.x; out->pos.y = r.pos.y; out->pos.z = r.pos.z;
    out->right.flags = flags;
    return out_;
}

/* The three in-register forms the sky driver keeps in rw/baskytran.c. */
void matrixASMMult(unsigned char *out_, unsigned char *a_, unsigned char *b_)
{
    Matrix *out = (Matrix *)out_, *a = (Matrix *)a_, *b = (Matrix *)b_;
    unsigned flags = out->right.flags;
    Matrix r;
    ROTATE(r.right, a->right, b);
    ROTATE(r.up, a->up, b);
    ROTATE(r.at, a->at, b);
    ROTATE(r.pos, a->pos, b);
    r.pos.x += b->pos.x;
    r.pos.y += b->pos.y;
    r.pos.z += b->pos.z;
    out->right.x = r.right.x; out->right.y = r.right.y; out->right.z = r.right.z;
    out->up.x = r.up.x; out->up.y = r.up.y; out->up.z = r.up.z;
    out->at.x = r.at.x; out->at.y = r.at.y; out->at.z = r.at.z;
    out->pos.x = r.pos.x; out->pos.y = r.pos.y; out->pos.z = r.pos.z;
    out->right.flags = flags;
}

typedef struct { float x, y, z; } V3;

static V3 rotate(const V3 *v, const Matrix *m)
{
    V3 out;
    ROTATE(out, *v, m);
    return out;
}

unsigned char *vectorASMMultVectorSingle(unsigned char *out, unsigned char *in, unsigned char *m)
{
    *(V3 *)out = rotate((V3 *)in, (Matrix *)m);
    return out;
}

unsigned char *vectorASMMultPoints(unsigned char *out_, unsigned char *in_, int count, unsigned char *m_)
{
    V3 *out = (V3 *)out_, *in = (V3 *)in_;
    Matrix *m = (Matrix *)m_;
    for (int i = 0; i < count; i++) {
        V3 v = rotate(&in[i], m);
        out[i].x = v.x + m->pos.x;
        out[i].y = v.y + m->pos.y;
        out[i].z = v.z + m->pos.z;
    }
    return out_;
}

unsigned char *vectorASMMultPointSingle(unsigned char *out, unsigned char *in, unsigned char *m)
{
    return vectorASMMultPoints(out, in, 1, m);
}

/* VF0 always reads as (0, 0, 0, 1). */
struct P4Vu p4vu = { .vf = { { .f = { 0, 0, 0, 1 } } } };

/* The EE's soft-float doubles: game code only ever wraps a float, calls one of these and unwraps it, so "double" here
   is a float's bits in the one register the callers pass. */
#include <math.h>
/* Head-look controllers call the EE atan2f directly; a missing-call stub cannot return a float. */
/* y + 0.0f turns -0 into +0: a heading straight behind must read +180 both from a target and from a quaternion-rotated
   forward vector, or btlUnit's turn sees a 360-degree gap, wraps it to a zero step and the battle waits forever. */
/* The same at a hair off the axis: a quaternion turned 180 degrees by the PSP's cosf (w = -4.4e-8) rotates forward to
   x = -8.7e-8, atan2 says -180 against the target's +180 and the first battle's unit never finishes its turn. */
float func_004494b8(float y, float x)
{
    if (fabsf(y) <= fabsf(x) * 1e-6f)
        y = 0.0f;
    return atan2f(y + 0.0f, x);
}
/* b215 acosf (the EE libm's, func_00448d10, reached through func_0044b920). The EE's FPU rounds toward zero, so a
   normalised vector never has a component above 1; the PSP rounds to nearest and gives 1.0000001 now and then, and
   acosf answers NaN ((x - x) / (x - x); on the EE that is the largest float, and the look-at's angle loops would never
   end, so the EE never gets there). The head look-at (mdlUpdateHierarchyPose) took acos of such a component: the
   head's rotation went NaN and stayed, a headless party member in battle (b155-b214). Inputs are clamped to [-1, 1];
   inside the range the retail body runs as before. */
unsigned p4_acos_clamped;
float p4r_00448d10(float x);
float func_00448d10(float x)
{
    if (x > 1.0f)
        x = 1.0f, p4_acos_clamped++;
    else if (x < -1.0f)
        x = -1.0f, p4_acos_clamped++;
    return p4r_00448d10(x);
}
/* RwV3dTransformVectors' VU0 body: rotate count packed vectors by the matrix's right/up/at rows, no translation. */
float *func_00402570(float *out, const float *in, int count, const float *m)
{
    float *result = out;
    for (; count > 0; count--, in += 3, out += 3) {
        float x = in[0], y = in[1], z = in[2];
        out[0] = x * m[0] + y * m[4] + z * m[8];
        out[1] = x * m[1] + y * m[5] + z * m[9];
        out[2] = x * m[2] + y * m[6] + z * m[10];
    }
    return result;
}
typedef union { float f; int bits; } Soft;
static inline float unwrap(int bits) { Soft s; s.bits = bits; return s.f; }
static inline int wrap(float f) { Soft s; s.f = f; return s.bits; }

int fptodp(float f) { return wrap(f); }
float dptofp(int bits) { return unwrap(bits); }
int func_0044b310(int x) { return x & 0x7fffffff; }                        /* fabs */
int func_004470f0(int x) { return wrap(acosf(unwrap(x))); }                /* acos */
int func_00447b88(int x) { return wrap(log10f(unwrap(x))); }               /* log10 */
int func_00447d00(int x, int y) { return wrap(powf(unwrap(x), unwrap(y))); } /* pow */
int func_00448a00(int x) { return wrap(sqrtf(unwrap(x))); }                /* sqrt */
int func_0044e0a8(int x, int y) { return wrap(unwrap(x) + unwrap(y)); }    /* adddf3 */
int func_0044e100(int x, int y) { return wrap(unwrap(x) - unwrap(y)); }    /* subdf3 */
int func_0044e168(int x, int y) { return wrap(unwrap(x) * unwrap(y)); }    /* muldf3 */
int func_0044e3b8(int x, int y) { return wrap(unwrap(x) / unwrap(y)); }    /* divdf3 */

#include "particle_vfpu.h"
int p4_particle_vfpu = 1, p4_particle_vfpu_check = 0;
static unsigned particle_native, particle_reference, particle_checks, particle_diffs;
void p4_log(const char *fmt, ...);
void p4_particle_matrix(const void *matrix, const void *point)
{
    if (!p4_particle_vfpu || (((unsigned long)matrix | (unsigned long)point) & 15) || !particle_matrix_regular(matrix, point)) {
        particle_matrix_reference(matrix, point);
        particle_reference++;
        return;
    }
    particle_matrix_native(matrix, point);
    if (p4_particle_vfpu_check && !(particle_native & 63)) {
        P4Reg fast = p4vu.vf[10], acc = p4vu.acc;
        particle_matrix_reference(matrix, point);
        int same = 1;
        for (int i = 0; i < 4; i++) {
            float a = fast.f[i] - p4vu.vf[10].f[i], b = acc.f[i] - p4vu.acc.f[i];
            if (!(fabsf(a) <= 1e-4f * (1 + fabsf(p4vu.vf[10].f[i]))) || !(fabsf(b) <= 1e-4f * (1 + fabsf(p4vu.acc.f[i]))))
                same = 0;
        }
        particle_checks++;
        if (!same) {
            if (particle_diffs++ < 8) p4_log("TRACE particle VFPU differs: matrix %p point %p\n", matrix, point);
            p4_particle_vfpu = 0; /* retain reference registers and fall back until explicitly re-enabled */
        } else {
            p4vu.vf[10] = fast;
            p4vu.acc = acc;
        }
    }
    particle_native++;
}

void p4_particle_selftest(void)
{
    static int done;
    if (done++) return;
    extern unsigned sceKernelGetSystemTimeLow(void);
    struct P4Vu saved = p4vu;
    float m[16] __attribute__((aligned(16))), p[4] __attribute__((aligned(16)));
    unsigned seed=17, bad=0;
    for (int test=0;test<512;test++) {
        for (int j=0;j<16;j++) { seed=seed*1664525u+1013904223u; m[j]=((int)(seed>>16)-32768)*(j>=12 ? 0.125f : 0.0001f); }
        for (int j=0;j<4;j++) { seed=seed*1664525u+1013904223u; p[j]=((int)(seed>>16)-32768)*0.0625f; }
        particle_matrix_native(m,p);
        P4Reg native=p4vu.vf[10], acc=p4vu.acc;
        particle_matrix_reference(m,p);
        for (int j=0;j<4;j++)
            if (!(fabsf(native.f[j]-p4vu.vf[10].f[j])<=1e-4f*(1+fabsf(p4vu.vf[10].f[j]))) ||
                !(fabsf(acc.f[j]-p4vu.acc.f[j])<=1e-4f*(1+fabsf(p4vu.acc.f[j])))) bad++;
    }
    int on=p4_particle_vfpu, check=p4_particle_vfpu_check;
    p4_particle_vfpu_check=0;
    unsigned us[2];
    for (int mode=0;mode<2;mode++) {
        p4_particle_vfpu=mode;
        unsigned start=sceKernelGetSystemTimeLow();
        for (int k=0;k<8192;k++) p4_particle_matrix(m,p);
        us[mode]=sceKernelGetSystemTimeLow()-start;
    }
    p4vu=saved;
    p4_particle_vfpu=bad ? 0 : on; p4_particle_vfpu_check=check;
    particle_native=particle_reference=particle_checks=particle_diffs=0;
    p4_log("particle VFPU selftest: 512 vectors bad %u; 8192 transforms reference %u us native %u us; enabled %d\n",bad,us[0],us[1],p4_particle_vfpu);
}

/* VU0 work per call site (P4_VU0_COUNT in vu0.h): instructions the game runs through the C model of VU0. */
#ifdef P4_HW_PROFILE
int p4_vu0_profile = 1;
#else
int p4_vu0_profile;
#endif
enum { VU0_SITES = 2048 };
static struct Vu0Site { const char *file; int line; unsigned ops, hits; } *vu0_site; /* heap: .bss must end below the data image */
static int vu0_sites;
static unsigned vu0_ops, vu0_blocks;
void p4_log(const char *fmt, ...);
void *calloc(unsigned long count, unsigned long size);

void p4_vu0_site(unsigned short *id, const char *file, int line, int ops)
{
    if (!*id) {
        if (!vu0_site && !(vu0_site = calloc(VU0_SITES, sizeof *vu0_site)))
            return;
        if (vu0_sites + 1 >= VU0_SITES)
            return;
        *id = ++vu0_sites;
        vu0_site[*id].file = file;
        vu0_site[*id].line = line;
    }
    vu0_site[*id].ops += ops;
    vu0_site[*id].hits++;
    vu0_ops += ops;
    vu0_blocks++;
}

void p4_vu0_report(unsigned frames)
{
    p4_log("particle VFPU %d check %d: native %u reference %u checks %u diffs %u per %u frames\n",
           p4_particle_vfpu, p4_particle_vfpu_check, particle_native, particle_reference, particle_checks, particle_diffs, frames);
    particle_native = particle_reference = particle_checks = particle_diffs = 0;
    if (!vu0_site)
        return;
    p4_log("vu0 per frame: instructions %u blocks %u sites %d\n", vu0_ops / frames, vu0_blocks / frames, vu0_sites);
    for (int rank = 0; rank < 8; rank++) {
        int best = 0;
        for (int i = 1; i <= vu0_sites; i++)
            if (vu0_site[i].ops > vu0_site[best].ops)
                best = i;
        if (!best || !vu0_site[best].ops)
            break;
        p4_log("vu0 top %d: %s:%d %u instructions %u blocks per frame\n", rank + 1, vu0_site[best].file, vu0_site[best].line, vu0_site[best].ops / frames, vu0_site[best].hits / frames);
        vu0_site[best].ops = 0; /* taken; the rest are cleared below */
    }
    for (int i = 1; i <= vu0_sites; i++)
        vu0_site[i].ops = vu0_site[i].hits = 0;
    vu0_ops = vu0_blocks = 0;
}
