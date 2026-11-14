/* The hardware half of RenderWare's PS2 driver, on the PSP. A function here named after a retail
   address replaces that address for C and reassembled code alike. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <math.h>
#include "swizzle.h"

#define R32(a) (*(volatile unsigned *)((a) + P4_K))
#define R8(a) (*(volatile unsigned char *)((a) + P4_K))
#define G32(off) (*(volatile int *)(P4_GP + (off)))

#define SCREEN_W 480
#define SCREEN_H 272
#define STRIDE 512

void p4_log(const char *fmt, ...);

/* Allegrex counters per section of the render thread (PSPLINK "profmode t" then "reset" before ldstart; the kernel keeps
   them per thread, sceKernelReferThreadProfiler gives a user-readable block). Words 0x04..0x4C in PSPTEK's order.
   Sections nest and are inclusive; "frame" runs present to present. */
typedef struct KwlnTask KwlnTask;
struct KwlnTask {
    char name[0x18];
    int nameSum;
    unsigned flags;
    int unk20;
    unsigned unk24, unk28;
    unsigned short unk2C, unk2E;
    void *update, *destroy;
    unsigned char *work;
    KwlnTask *listNext;
};
extern KwlnTask *iGpffffba04, *iGpffffba14;
extern char D_00710568[];
void kAssertFail(const void *file, unsigned line);
int kwlnTaskRunUpdate(KwlnTask *task);
#ifdef P4_HW_PROFILE
#include <pspdebug.h>
enum { PROF_FRAME, PROF_ATOMIC, PROF_MESH, PROF_GUARD, PROF_SKIN, PROF_PTANK, PROF_2D, PROF_SYNC, PROF_VBLANK, PROF_SETUP, PROF_SHADE, PROF_EMIT, PROF_SUBMIT, PROF_HIT, PROF_GDRAW, PROF_KEY, PROF_UVSCAN, PROF_LIGHTS, PROF_HEMPTY, PROF_HBIND, PROF_DLIGHT, PROF_DDRAW, PROF_GCLASS, PROF_GLOOP, PROF_GCLIP, PROF_GSUB,
      PROF_MFRUST, PROF_MSLOT, PROF_MCOPY, PROF_ALTM, PROF_ABLACK, PROF_ACOVER, PROF_GESKIN, PROF_SECTOR, PROF_TASKS,
      PROF_SBONES, PROF_SGUARD, PROF_SDRAW, PROF_SMAT, PROF_SBIND, PROF_STEST,
      PROF_QMODEL, PROF_QATTACH, PROF_QSORT, PROF_QEFFECT, PROF_QFLD, PROF_QPRIM,
      PROF_AFRAME, PROF_AATTACH, PROF_ALAYER, PROF_ACTRL, PROF_AFX, PROF_AMOTION,
      PROF_ABLEND, PROF_AREBASE, PROF_APOSE, PROF_KADD, PROF_KSET, PROF_KBLEND, PROF_KMAT, PROF_KANIM, PROF_KFRAME, PROF_KINTERP, PROF_TBIND, PROF_LBIND,
      PROF_DSTATE, PROF_DSKIP, PROF_DPOSE, PROF_DRESTORE, PROF_DCOLOR, PROF_DVIS, PROF_DFX, PROF_DFRAME,
      PROF_RPLAY, PROF_RLIGHT, PROF_RGUARD, PROF_PERSIST, PROF_SECTIONS };
enum { PC_COUNT = 5 };
/* counter words read per section edge: cpu cycles, stall, memory stall, imiss, dmiss (b73 read all 19, which itself
   cost a visible part of the frame at ~800 section edges a frame) */
/* b239: the words are read as prof_regs[1 + index] and PspDebugProfilerRegs starts with `enable`, so index k is the
   k-th counter after it: 0 systemck, 1 cpuck, 2 internal, 3 memory, 4 copz, ... 12 i_miss, 13 d_miss, 14 d_writeback.
   Up to b238 the table was { 1, 2, 4, 13, 14 }: the column printed as "mem_k" was copz, "imiss" was d_miss and
   "dmiss" was d_writeback; instruction-cache misses were never read. prof_bench (boot) prints every counter around a
   walk of 4096 uncached-yet lines to show which word counts data misses. Not const: pokeable. */
unsigned char p4_prof_index[PC_COUNT] = { 1, 2, 3, 12, 13 };
#define prof_index p4_prof_index
static const char *const prof_names[PROF_SECTIONS] = { "frame", "atomic", "mesh", "guard", "skin", "ptank", "2d", "gesync", "vblank", "msetup", "mshade", "memit", "msubmit", "mhit", "mgdraw", "mkey", "muvscan", "mlights", "hempty", "hbind", "dlight", "ddraw", "gclass", "gloop", "gclip", "gsub",
    "mfrust", "mslot", "mcopy", "altm", "ablack", "acover", "geskin", "sector", "tasks", "sbones", "sguard", "sdraw", "smat", "sbind", "stest",
    "qmodel", "qattach", "qsort", "qeffect", "qfld", "qprim",
    "aframe", "aattach", "alayer", "actrl", "afx", "amotion",
    "ablend", "arebase", "apose", "kadd", "kset", "kblend", "kmat", "kanim", "kframe", "kinterp", "tbind", "lbind",
    "dstate", "dskip", "dpose", "drestore", "dcolor", "dvis", "dfx", "dframe", "rplay", "rlight", "rguard", "mpers" };
static const volatile unsigned *prof_regs;
static int prof_state; /* 0 not tried, 1 live, -1 none */
static unsigned prof_at[PROF_SECTIONS][PC_COUNT], prof_depth[PROF_SECTIONS], prof_calls[PROF_SECTIONS];
static unsigned long long prof_sum[PROF_SECTIONS][PC_COUNT];
static unsigned long long prof_mono[PROF_SECTIONS]; /* cycles, never reset: the task table subtracts nested render work */
static unsigned prof_shaded, prof_emitted;

static void prof_read(unsigned out[PC_COUNT])
{
    for (int i = 0; i < PC_COUNT; i++)
        out[i] = prof_regs[1 + prof_index[i]];
}

/* b124: writing GE words cost ~100 cycles each (bone matrices 1600 a matrix) with or without GE fill. Store speed
   into the display list's memory vs a plain buffer, and where libpspgu writes. */
static unsigned prof_store_scratch[1024] __attribute__((aligned(64))); /* b242: 4096 -> 1024 words, the image was 344 bytes over */
static void prof_store_test(void);
static void prof_bench(void);
static int prof_live(void)
{
    if (prof_state == 0) {
        const volatile PspDebugProfilerRegs *r = sceKernelReferThreadProfiler();
        unsigned a = (unsigned)r;
        prof_state = a && !(a & 0x80000000u) ? 1 : -1;
        p4_log("hwprof thread regs %x: %s\n", a, prof_state > 0 ? "live" : "none (PSPLINK profmode t, reset)");
        if (prof_state > 0) {
            prof_regs = (const volatile unsigned *)r;
            unsigned b[PC_COUNT], e[PC_COUNT];
            prof_read(b);
            for (volatile int i = 0; i < 1000000; i++)
                ;
            prof_read(e);
            p4_log("hwprof spin: cpuck %u stall %u mem %u imiss %u dmiss %u\n", e[0] - b[0], e[1] - b[1], e[2] - b[2], e[3] - b[3], e[4] - b[4]);
            prof_read(b);
            for (int i = 0; i < 1000; i++) {
                unsigned t[PC_COUNT];
                prof_read(t);
                prof_read(t);
            }
            prof_read(e);
            p4_log("hwprof one section begin+end costs %u cycles\n", (e[0] - b[0]) / 1000);
            prof_store_test();
            prof_bench();
        }
    }
    return prof_state > 0;
}

static void prof_begin(int s)
{
    if (prof_live() && prof_depth[s]++ == 0)
        prof_read(prof_at[s]);
}

static void prof_end(int s)
{
    if (prof_state <= 0 || !prof_depth[s] || --prof_depth[s])
        return;
    unsigned now[PC_COUNT];
    prof_read(now);
    for (int i = 0; i < PC_COUNT; i++)
        prof_sum[s][i] += now[i] - prof_at[s][i];
    prof_mono[s] += now[0] - prof_at[s][0];
    prof_calls[s]++;
}

static void task_report(unsigned frames);
static int p4_skipping(void);
/* b191: hwtask and hwot keys (function addresses, node types) with this bit are ticks frameskip left undrawn: what
   still runs there for nobody to see (perf_status.md, plan item 5) */
#define SKIP_KEY 0x80000000u
extern unsigned kmat_nodes, kmat_flags_or;
static void ot_report(unsigned frames);
/* per frame: CPU kcycles, stall kcycles (total, memory), cache misses, FPU/VFPU instructions (k) */
static void prof_report(unsigned frames)
{
    if (prof_state <= 0 || !frames)
        return;
    for (int s = 0; s < PROF_SECTIONS; s++) {
        unsigned long long *v = prof_sum[s];
        p4_log("hwprof %s calls %u cpu_k %u stall_k %u mem_k %u imiss %u dmiss %u\n",
               prof_names[s], prof_calls[s] / frames, (unsigned)(v[0] / frames / 1000), (unsigned)(v[1] / frames / 1000),
               (unsigned)(v[2] / frames / 1000), (unsigned)(v[3] / frames), (unsigned)(v[4] / frames));
    }
    prof_store_test();
    p4_log("hwprof vertices shaded %u emitted %u per frame\n", prof_shaded / frames, prof_emitted / frames);
    prof_shaded = prof_emitted = 0;
    task_report(frames);
    ot_report(frames);
    p4_log("hwanim hierarchy nodes %u per frame, flags seen %x\n", kmat_nodes / frames, kmat_flags_or);
    kmat_nodes = 0, kmat_flags_or = 0;
    memset(prof_sum, 0, sizeof prof_sum);
    memset(prof_calls, 0, sizeof prof_calls);
}

/* The game's frame by task: kwlnTaskRunList (src/Kernel/sdkTask.c, FUN_004515B0), the same walk, with each
   kwlnTaskRunUpdate timed and booked to the task's update callback. This definition wins the link (port objects come
   first, --allow-multiple-definition); kwlnTaskRunFrame reaches it through its R_MIPS_26 call and keeps only s
   registers across it. "self" leaves out the renderer sections that ran inside the callback. */
enum { TASK_STATS = 128 };
typedef struct { unsigned fn, calls; unsigned long long cycles, self; char name[0x19]; } TaskStat;
static TaskStat task_stats[TASK_STATS];
static unsigned task_overflow, task_lists;
static unsigned long long task_list_cycles;
static unsigned long long render_mono(void)
{
    return prof_mono[PROF_ATOMIC] + prof_mono[PROF_SECTOR] + prof_mono[PROF_2D] + prof_mono[PROF_SYNC] + prof_mono[PROF_VBLANK];
}
static void task_book(const KwlnTask *task, unsigned fn, unsigned cycles, unsigned long long render)
{
    unsigned h = (fn >> 2) * 2654435761u >> 25;
    for (unsigned probe = 0; probe < TASK_STATS; probe++) {
        TaskStat *e = &task_stats[(h + probe) & (TASK_STATS - 1)];
        if (e->fn != fn && e->fn)
            continue;
        if (!e->fn) {
            e->fn = fn;
            memcpy(e->name, task->name, 0x18);
            e->name[0x18] = 0;
        }
        e->calls++;
        e->cycles += cycles;
        e->self += cycles > render ? cycles - render : 0;
        return;
    }
    task_overflow++;
}
void hanim_scan_after(const char *task, unsigned fn);
static int run_task(KwlnTask *node)
{
    if (prof_state <= 0)
        return kwlnTaskRunUpdate(node);
    unsigned fn = (unsigned)node->update;
    unsigned long long render = render_mono();
    unsigned before = prof_regs[1 + prof_index[0]];
    int r = kwlnTaskRunUpdate(node);
    unsigned cycles = prof_regs[1 + prof_index[0]] - before;
    task_book(node, fn ^ (p4_skipping() ? SKIP_KEY : 0), cycles, render_mono() - render);
    hanim_scan_after(node->name, fn);
    return r;
}
/* the 24 costliest update callbacks per frame: inclusive and self kcycles */
static void task_report(unsigned frames)
{
    p4_log("hwtask lists %u per frame cpu_k %u overflow %u\n", task_lists / frames, (unsigned)(task_list_cycles / frames / 1000), task_overflow);
    for (int k = 0; k < 24; k++) { /* b191: 24, the undrawn ticks take rows too */
        TaskStat *best = 0;
        for (int i = 0; i < TASK_STATS; i++)
            if (task_stats[i].calls && (!best || task_stats[i].cycles > best->cycles))
                best = &task_stats[i];
        if (!best)
            break;
        p4_log("hwtask %2d fn %08x%s %-24s calls %u cpu_k %u self_k %u\n", k, best->fn & ~SKIP_KEY, best->fn & SKIP_KEY ? " SKIP" : "", best->name, best->calls / frames,
               (unsigned)(best->cycles / frames / 1000), (unsigned)(best->self / frames / 1000));
        best->calls = 0;
    }
    memset(task_stats, 0, sizeof task_stats);
    task_overflow = task_lists = 0;
    task_list_cycles = 0;
}

/* sdkOtFlushBuckets (census FIXES): every ordering-table node of the frame, timed by its start callback, its command
   (the node type, 0x109 for clump atomics, or the atomic's render callback) and its end callback. "self" leaves out the
   renderer sections inside, as the task table does: what the game's own draw code costs per kind. */
enum { OT_STATS = 64 };
typedef struct { unsigned key, calls; unsigned long long cycles, self; } OtStat;
static OtStat ot_stats[OT_STATS];
static unsigned long long ot_render_at[8];
static int ot_depth;
unsigned p4_ot_begin(void)
{
    if (prof_state <= 0)
        return 0;
    if (ot_depth < 8)
        ot_render_at[ot_depth] = render_mono();
    ot_depth++;
    return prof_regs[1 + prof_index[0]];
}
void p4_ot_end(unsigned start, unsigned key)
{
    if (prof_state <= 0 || ot_depth <= 0)
        return;
    unsigned cycles = prof_regs[1 + prof_index[0]] - start;
    ot_depth--;
    unsigned long long render = ot_depth < 8 ? render_mono() - ot_render_at[ot_depth] : 0;
    key ^= p4_skipping() ? SKIP_KEY : 0; /* b191: ticks frameskip leaves undrawn booked apart */
    unsigned h = (key >> 2) * 2654435761u >> 26;
    for (unsigned probe = 0; probe < OT_STATS; probe++) {
        OtStat *e = &ot_stats[(h + probe) & (OT_STATS - 1)];
        if (e->key != key && e->calls)
            continue;
        e->key = key;
        e->calls++;
        e->cycles += cycles;
        e->self += cycles > render ? cycles - render : 0;
        return;
    }
}
static void ot_report(unsigned frames)
{
    for (int k = 0; k < 24; k++) { /* b191: 24, the undrawn ticks take rows too */
        OtStat *best = 0;
        for (int i = 0; i < OT_STATS; i++)
            if (ot_stats[i].calls && (!best || ot_stats[i].self > best->self))
                best = &ot_stats[i];
        if (!best)
            break;
        p4_log("hwot %2d key %08x%s calls %u cpu_k %u self_k %u\n", k, best->key & ~SKIP_KEY, best->key & SKIP_KEY ? " SKIP" : "",
               best->calls / frames, (unsigned)(best->cycles / frames / 1000), (unsigned)(best->self / frames / 1000));
        best->calls = 0;
    }
    memset(ot_stats, 0, sizeof ot_stats);
}

/* What resrcMngRenderTask ("draw scene(linkOt)", ~3 Mcycles of its own in Heaven, b137) spends in its callees:
   timed through --wrap in the profiling build only (tools/psp_hw_debug.py). All take integer arguments. */
#define P4_WRAP(ret, name, section, params, args) \
    ret __real_##name params; \
    ret __wrap_##name params { prof_begin(section); ret r = __real_##name args; prof_end(section); return r; }
#define P4_WRAP_VOID(name, section, params, args) \
    void __real_##name params; \
    void __wrap_##name params { prof_begin(section); __real_##name args; prof_end(section); }
P4_WRAP_VOID(mdlQueueModelForDraw, PROF_QMODEL, (void *queue, void *model), (queue, model))
P4_WRAP(int, mdlComputeIndexedAttachMatrix, PROF_QATTACH, (void *model, int index, void *destination), (model, index, destination))
P4_WRAP_VOID(qsort, PROF_QSORT, (void *base, unsigned count, unsigned size, int (*compare)(const void *, const void *)), (base, count, size, compare))
P4_WRAP_VOID(mdlEffectFlushWithTable, PROF_QEFFECT, (void *table, void *effect), (table, effect))
P4_WRAP_VOID(mdlEffectAdvanceAndFlushWithTable, PROF_QEFFECT, (void *table, void *effect), (table, effect))
P4_WRAP_VOID(FldResource_QueueDrawCallbacks, PROF_QFLD, (void *list, void *resource), (list, resource))
P4_WRAP_VOID(FldResource_QueueDrawCallbacksPrimary, PROF_QFLD, (void *list, void *resource), (list, resource))
P4_WRAP_VOID(FldResource_QueueDrawCallbacksSecondary, PROF_QFLD, (void *list, void *resource), (list, resource))
P4_WRAP(void *, PrimAllocQueueNode, PROF_QPRIM, (void), ())
P4_WRAP_VOID(PrimAppendNode, PROF_QPRIM, (void *list, void *node), (list, node))
/* RenderWare animation toolkit called from mdlAdvanceLayer (b141): RtAnimInterpolatorAddAnimTime, SetCurrentTime,
   Blend, SetCurrentAnim and RpHAnimHierarchyUpdateMatrices */
int __real_func_003d5bc0(void *interpolator, float time);
/* b168: AddAnimTime calls the scheme's interpolate callback (per node: slerp of the rotation, lerp of the offset) through
   the interpolator's pointer at +0x44, so it cannot be wrapped by name. For the call the pointer is swapped for a timed
   trampoline: kinterp is the math, kadd minus kinterp the key search and bookkeeping. Seen in PPSSPP: RpHAnim's
   p4r_00415bc0 (slerp + lerp) and the game's mdlKeyFrameInterpolateCopy; both are timed. */
typedef void (*KInterp)(void *out, void *in1, void *in2, float time, void *data);
static KInterp kinterp_now; /* the callback of the AddAnimTime in progress (animation runs on one thread) */
static void kinterp_timed(void *out, void *in1, void *in2, float time, void *data)
{
    prof_begin(PROF_KINTERP);
    kinterp_now(out, in1, in2, time, data);
    prof_end(PROF_KINTERP);
}
KInterp p4_interp_pick(KInterp cb);
int __wrap_func_003d5bc0(void *interpolator, float time)
{
    KInterp *cb = (KInterp *)((char *)interpolator + 0x44), saved = *cb;
    int swap = saved && saved != kinterp_timed;
    if (swap)
        kinterp_now = p4_interp_pick(saved), *cb = kinterp_timed;
    prof_begin(PROF_KADD);
    int r = __real_func_003d5bc0(interpolator, time);
    prof_end(PROF_KADD);
    if (swap)
        *cb = saved;
    return r;
}
int __real_func_003d5e40(void *interpolator, float time);
int __wrap_func_003d5e40(void *interpolator, float time) { prof_begin(PROF_KSET); int r = __real_func_003d5e40(interpolator, time); prof_end(PROF_KSET); return r; }
int __real_func_003d5e90(void *out, void *in1, void *in2, float alpha);
int __wrap_func_003d5e90(void *out, void *in1, void *in2, float alpha) { prof_begin(PROF_KBLEND); int r = __real_func_003d5e90(out, in1, in2, alpha); prof_end(PROF_KBLEND); return r; }
int __real_func_003d5840(void *interpolator, void *animation);
int __wrap_func_003d5840(void *interpolator, void *animation) { prof_begin(PROF_KANIM); int r = __real_func_003d5840(interpolator, animation); prof_end(PROF_KANIM); return r; }
int __real_func_00397c40(void *hierarchy);
unsigned kmat_nodes, kmat_flags_or;
int p4_hanim_dispatch(unsigned *h, int (*original)(void *));
int __wrap_func_00397c40(void *hierarchy)
{
    kmat_nodes += ((int *)hierarchy)[1];
    kmat_flags_or |= ((unsigned *)hierarchy)[0];
    prof_begin(PROF_KMAT);
    int r = p4_hanim_dispatch(hierarchy, __real_func_00397c40);
    prof_end(PROF_KMAT);
    return r;
}
void *__real_func_003e9680(void *frame);
void *__wrap_func_003e9680(void *frame) { prof_begin(PROF_KFRAME); void *r = __real_func_003e9680(frame); prof_end(PROF_KFRAME); return r; }
/* census FIXES hooks in mdlAdvanceModel (b139): id 0..5 = aframe..amotion, 6..8 = ablend, arebase, apose (b141);
   mdlUpdateDrawState (b182): 9 the whole call (dstate, or dskip in a frame frameskip leaves undrawn), 10 dpose,
   11 drestore, 12 dcolor, 13 dvis, 14 dfx, 15 dframe */
static int p4_skipping(void);
void p4_sec(int id, int begin)
{
    if (id >= 9 && id <= 15) {
        static int dstate_skip;
        int s = id != 9 ? PROF_DSTATE + id - 8 : (begin ? (dstate_skip = p4_skipping()) : dstate_skip) ? PROF_DSKIP : PROF_DSTATE;
        if (begin)
            prof_begin(s);
        else
            prof_end(s);
        return;
    }
    if (id >= 0 && id < 9) {
        if (begin)
            prof_begin(PROF_AFRAME + id);
        else
            prof_end(PROF_AFRAME + id);
    }
}
#else
#define prof_begin(s) ((void)0)
#define prof_end(s) ((void)0)
int __real_func_00397c40(void *hierarchy);
int p4_hanim_dispatch(unsigned *h, int (*original)(void *));
int __wrap_func_00397c40(void *hierarchy)
{
    return p4_hanim_dispatch(hierarchy, __real_func_00397c40);
}
unsigned p4_ot_begin(void) { return 0; }
void p4_sec(int id, int begin) { (void)id, (void)begin; }
void p4_ot_end(unsigned start, unsigned key) { (void)start, (void)key; }
#define run_task kwlnTaskRunUpdate
#endif
/* b176: outside the profiler too; release builds need the tick for the far animation LOD */
unsigned p4_anim_tick; /* b171: one per game tick (task list run), for the bone-update decisions */
void kwlnTaskRunList(void)
{
    p4_anim_tick++;
    prof_begin(PROF_TASKS);
#ifdef P4_HW_PROFILE
    unsigned before = prof_state > 0 ? prof_regs[1 + prof_index[0]] : 0;
#endif
    KwlnTask *node = iGpffffba04;
    while (node) {
        iGpffffba14 = node;
        if (run_task(node)) {
            if (node != iGpffffba14) {
                if ((iGpffffba14->flags & 0xF) != 2)
                    kAssertFail(D_00710568, 0x22F);
                node = iGpffffba14 ? iGpffffba14->listNext : iGpffffba04;
            } else {
                if ((node->flags & 0xF) != 2)
                    kAssertFail(D_00710568, 0x236);
                node = node->listNext;
            }
        } else {
            node = iGpffffba14 ? iGpffffba14->listNext : iGpffffba04->listNext;
        }
    }
#ifdef P4_HW_PROFILE
    if (prof_state > 0)
        task_list_cycles += prof_regs[1 + prof_index[0]] - before, task_lists++;
#endif
    prof_end(PROF_TASKS);
}
#ifdef P4_HW_PROFILE
#define PROF_COUNT(var, n) (var += (n))
#else
#define PROF_COUNT(var, n) ((void)0)
#endif
void p4_backtrace(void);

/* DI/EI wrappers: game code runs in threads, nothing here races an interrupt handler yet. */
int func_0042ba20(void) { return 0; }
int func_0042ba70(void) { return 0; }

/* The shadow pass has no vblank interrupt to hang off here. */
int FldShadow_InstallIntcHandler(void) { return 1; }

/* shortcut: omit TV-world noise/vignette; restore when the GE path supports its interpolated alpha mask cheaply. */
int FldNoiseUpdate(void *task) { return 0; }

/* Everything RenderWare reads from a raster, at its retail offsets. */
typedef struct {
    void *parent;
    unsigned char *cpPixels, *palette;
    int width, height, depth, stride;
    short nOffsetX, nOffsetY;
    unsigned char cType, cFlags, privateFlags, cFormat;
    unsigned char *originalPixels;
    int originalWidth, originalHeight, originalStride;
} Raster;

enum { TYPE_ZBUFFER = 1, TYPE_CAMERA = 2, TYPE_MASK = 7, DONT_ALLOCATE = 0x80 };

/* CPU/GE pipeline: the GE draws frame N while the CPU builds frame N+1 in the other list and pool half. Frame N is
   shown once its list is done, so the picture is one frame later than the game state. b72: CPU 59 ms then GE 16 ms
   one after the other. 0: build, finish, wait, show, as b75. Default 1 since b98: b97 on the PSP, Heaven 117 -> 101 ms,
   Dojima's room 74 -> 57 ms, no flicker. */
int p4_pipeline = 1;
static unsigned list_store[2][64 * 1024] __attribute__((aligned(64)));
static unsigned *list = list_store[0];
static void *draw_buffer;
static int in_frame;
/* Frameskip (b147). The game advances a fixed 1/30 s a frame (iGpffff8040 = 0x3D088889): at 15 FPS it ran at half
   speed. When the game falls behind real time by more than half a tick, the next frame runs its logic and draws
   nothing: camera begin and clear, 2D, atomics, world sectors and Im3D return at once, and present neither waits nor
   flips. p4_frameskip: 0 off, else on; p4_frameskip_max: skipped frames in a row. */
int p4_frameskip = 1, p4_frameskip_max = 2; /* b147 Heaven: off 15 FPS at 50% speed; on 11-14 FPS at ~98% speed */
static int skip_frame, skip_run, skip_cap = 2;
static int skip_debt_us;
static unsigned skip_last_us, skipped_frames, skip_ticks;
#define SKIPPING (skip_frame)
static int p4_skipping(void) { return skip_frame; }
/* b198: avoid audited render-only preparation in undrawn ticks. OT callbacks, animation,
 * blend tracks, child/effect progression and queue construction still execute. */
int p4_skip_visual_bits = 3; /* bit 1: material draw-pass walks, bit 2: model material tint */
static unsigned skip_visual_passes, skip_visual_colors;
int p4_skip_visual_prep(int kind)
{
    if (!skip_frame || !(p4_skip_visual_bits & kind)) return 0;
    if (kind == 1) skip_visual_passes++;
    if (kind == 2) skip_visual_colors++;
    return 1;
}
static void skip_visual_report(unsigned frames)
{
    p4_log("skip visual bits %d: passes %u colors %u per %u frames\n",
           p4_skip_visual_bits, skip_visual_passes, skip_visual_colors, frames);
    skip_visual_passes = skip_visual_colors = 0;
}

/* b183: mdlUpdateDrawState (census FIXES) takes its no-shadow path in a frame frameskip leaves undrawn; 0: as b182 */
int p4_shadow_skip_on = 1;
static unsigned shadow_skips;
int p4_shadow_skip(void)
{
    int skip = p4_shadow_skip_on && skip_frame;
    shadow_skips += skip;
    return skip;
}
/* b217 p4_fpu_chop: the EE's FPU rounds toward zero (PCSX2 "Chop"); the PSP's rounds to nearest. Izanagi (the PC port
   of the same decompilation, docs/porting.md "EE float mode") runs all game code in round-toward-zero and names the
   boss fight's 180-degree quaternion among what depends on it. 1: FCR31 rounding mode toward zero on the game thread
   (set once a frame here, the thread that runs the game's ticks); 3: also flush denormals (FS); 0: nearest, as before. */
int p4_fpu_chop = 0;
static void fpu_mode_apply(void)
{
    unsigned fcr, want;
    __asm__ volatile("cfc1 %0, $31" : "=r"(fcr));
    want = (fcr & ~(3u | 1u << 24 | 0x1f << 7)) | (p4_fpu_chop & 1) | (p4_fpu_chop & 2 ? 1u << 24 : 0); /* traps stay off */
    if (want != fcr)
        __asm__ volatile("ctc1 %0, $31" : : "r"(want));
}
static void skip_decide(int cap)
{
    fpu_mode_apply();
    unsigned now = sceKernelGetSystemTimeLow();
    int target = cap * 16683;
    if (skip_last_us) {
        skip_debt_us += (int)(now - skip_last_us) - target;
        if (skip_debt_us > 3 * target) /* a load or a stall: do not race to catch up */
            skip_debt_us = 3 * target;
        if (skip_debt_us < -target)
            skip_debt_us = -target;
    }
    skip_last_us = now;
    skip_ticks++;
    if (p4_frameskip && skip_debt_us > target / 2 && skip_run < p4_frameskip_max) {
        skip_frame = 1;
        skip_run++;
    } else {
        skip_frame = 0;
        skip_run = 0;
    }
}
static int pipe_set, pipe_peak_v, pipe_peak_i;
extern int ge_list_executed[];
/* Frame lists finished by the CPU and reached by the GE. Not sceGeListSync on the list id: a finished list frees its
   id and the next sceGuStart gets it again, so the sync waited on the new, unfinished list (b76: black screen, main
   thread in GELISTSYNC for good in PPSSPP). */
static unsigned ge_queued;
static volatile unsigned ge_finished;
/* b120 Heaven: p4flip polling every 200 us took 12% of the CPU (thinfo RunClocks: 2.56 s of 21 s) and preempted
   user_main ~3500 times a second. Now it sleeps on an event flag: bit 1 is set by the GE finish callback and by a
   frame handed over, bit 2 by a frame shown. The timeouts keep the old behaviour if a set is ever lost. */
int p4_flip_event = 1; /* 0: poll as before b120 */
enum { FLIP_WORK = 1, FLIP_SHOWN = 2 };
static int flip_event = -1;
static volatile int flip_event_error;
static unsigned flip_wakeups;
static void flip_signal(unsigned bits)
{
    if (flip_event >= 0) {
        int r = sceKernelSetEventFlag(flip_event, bits);
        if (r < 0)
            flip_event_error = r;
    }
}
static void on_ge_finish(int arg)
{
    (void)arg;
    ge_finished++;
    flip_signal(FLIP_WORK);
}
static void flip_wait(unsigned bits, SceUInt timeout_us)
{
    if (!p4_flip_event || flip_event < 0) {
        sceKernelDelayThread(bits == FLIP_WORK ? 200 : 100);
        return;
    }
    u32 got;
    SceUInt timeout = timeout_us;
    sceKernelWaitEventFlag(flip_event, bits, PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &got, &timeout);
}

/* Pipeline display (b82). b81 Heaven: CPU 76 ms and GE 72 ms of work, but one after the other 118 ms. With two
   framebuffers the GE may draw frame N+1 only into the buffer that is no longer shown, so:
   - the p4flip thread shows frame N's buffer at the first vblank after its list is done (shown_seq = N);
   - list N+1 is built behind a gate: its stall address stays at its start (libpspgu skips stall updates while
     gu_object_stack_depth is set) until shown_seq reaches N, then the GE catches up while the CPU builds on.
   b72's pipeline drew N+1 into the buffer still on screen: the HUD flickered. */
static volatile unsigned flip_seq, shown_seq;
static void *volatile flip_fb;
static int flip_thread = -1;
static unsigned gate_seq, texture_waits;
static int gate_closed;
extern int gu_object_stack_depth;
static int flip_main(SceSize size, void *arg)
{
    (void)size, (void)arg;
    for (;;) {
        unsigned seq = flip_seq;
        if (seq != shown_seq && (int)(ge_finished - seq) >= 0) {
            void *fb = flip_fb;
            sceDisplayWaitVblankStart();
            sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)fb), 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
            shown_seq = seq;
            flip_signal(FLIP_SHOWN);
        } else {
            flip_wakeups++;
            flip_wait(FLIP_WORK, 20000);
        }
    }
    return 0;
}
static int gate_poll(void);
typedef struct { unsigned *start, *current; int parent_context; } GuList; /* libpspgu's display list record */
extern GuList *gu_list;

/* The display list through the data cache. libpspgu's sceGuStart writes the list through the uncached alias
   (0x40000000): b125 Heaven, an uncached store cost 124 cycles while the GE ran (cached 18), so every GE word did,
   e.g. 1600 cycles a bone matrix. The list is now written cached; every stall move (ours, and libpspgu's own in
   sceGuFinish and sceGuGetMemory) goes through --wrap=sceGeListUpdateStallAddr, which writes the new words back first.
   The GE never reads past the stall, and a word below it never changes again. */
int p4_list_cached = 1; /* 0: uncached list as libpspgu writes it, as b125 */
static unsigned list_flushed, list_lo, list_hi;
static int list_cached_now; /* the list being written goes through the cache */
static unsigned list_writebacks;
int __real_sceGeListUpdateStallAddr(int qid, void *stall);
int __wrap_sceGeListUpdateStallAddr(int qid, void *stall)
{
    unsigned to = (unsigned)stall & ~0x40000000u;
    if (to > list_flushed && to >= list_lo && to <= list_hi) {
        unsigned from = list_flushed & ~63u; /* whole lines */
        sceKernelDcacheWritebackRange((void *)from, ((to + 63) & ~63u) - from);
        list_flushed = to;
        list_writebacks++;
    }
    return __real_sceGeListUpdateStallAddr(qid, (void *)((unsigned)stall | 0x40000000u));
}
/* right after sceGuStart: what it wrote went to RAM uncached; the cache may hold clean stale copies of those lines
   (every cached write was written back at a stall move), so drop them before writing on through the cache */
static void list_to_cached(void)
{
    unsigned start = (unsigned)gu_list->start & ~0x40000000u, current = (unsigned)gu_list->current & ~0x40000000u;
    list_lo = start;
    list_hi = start + sizeof list_store[0];
    list_flushed = current;
    list_cached_now = p4_list_cached;
    if (!p4_list_cached)
        return;
    if (current > start)
        sceKernelDcacheInvalidateRange((void *)start, (current - start + 63) & ~63u);
    gu_list->start = (unsigned *)start;
    gu_list->current = (unsigned *)current;
}
#ifdef P4_HW_PROFILE
static unsigned prof_cycles(void) { return prof_regs[1 + prof_index[0]]; }
static void prof_store_test(void)
{
    volatile unsigned *cached = prof_store_scratch, *uncached = (volatile unsigned *)((unsigned)prof_store_scratch | 0x40000000u);
    unsigned t0 = prof_cycles();
    for (int i = 0; i < 4096; i++)
        cached[i & 1023] = i;
    unsigned t1 = prof_cycles();
    for (int i = 0; i < 4096; i++)
        uncached[i & 1023] = i;
    unsigned t2 = prof_cycles();
    p4_log("hwprof store 4096 words: cached %u uncached %u cycles; list %p current %p\n", t1 - t0, t2 - t1, (void *)list_store,
           gu_list ? (void *)gu_list->current : 0);
}
#endif
static void gate_close(unsigned need)
{
    gate_seq = need;
    gate_closed = 1;
    gu_object_stack_depth = 1;
}
/* A raster's texels or palette are about to change in place: the GE must be done with the previous frame. */
static void pipe_texture_wait(void)
{
    if ((int)(ge_queued - ge_finished) <= 0)
        return;
    texture_waits++;
    while ((int)(ge_queued - ge_finished) > 0)
        sceKernelDelayThread(100);
}
static void begin_frame(void);
static void gu_dirty(void);
extern int p4_probe_2d;
static int census2d_shown;
static int ge_time_begin(void);
static unsigned ge_time_end(int started, int category);
extern int p4_exp_ge_time;
static unsigned ge_time_frames;
static unsigned kept_textures, kept_cameras, kept_lights, mesh_modes; /* the mesh state cache, logged */
static unsigned textures_bound, lights_bound; /* b174: binds that wrote GE state (tbind, lbind: their cost) */
static void cover_report(unsigned frames);
static void other_report(unsigned frames);
static void hanim_report(void);
extern int p4_frameskip_bones;
extern int p4_bone_check;
static unsigned bone_bad_count;
extern int p4_hanim_nan_fix;
static void look_report(void);
static unsigned hanim_nan_fixed;
static unsigned hanim_nan_after_update;
static unsigned skipped_bone_updates;
static void census2d_report(unsigned frames);
int p4_im3d = 1; /* 0: RwIm3D not drawn, as before b134 */
static unsigned im3d_draws, im3d_vertices;
static void frustum_report(unsigned frames);
static void replay_report(unsigned frames);
static void call_report(void);
extern int p4_fog;
static void probe_report(unsigned frames);
static void topology_report(unsigned frames);
/* Temporary A/B controls, all off at boot. Flags: 1 all ordinary atomics,
   2 all 2D, 4 particles, 8 dynamic lighting. Geometry is an exact live pointer;
   keep_every 0 hides all its instances, 2 keeps one in two, etc. */
unsigned p4_probe_flags, p4_probe_geometry;
unsigned p4_probe_geometry2, p4_probe_geometry3, p4_probe_geometry4;
int p4_probe_census;
int p4_probe_keep_every;
static unsigned probe_selected, probe_hidden, probe_select_frame, probe_instance;
static unsigned probe_last_geometry;
extern int p4_exp_scissor;
extern int p4_state_cache, p4_draw_batch;
typedef struct { float u, v; unsigned colour; float x, y, z; } Vertex3D;
/* GE-lit, in the GE's attribute order: s8 normals (the GE divides by |N|), a pad byte keeps the floats aligned. 28 bytes. */
typedef struct { float u, v; unsigned colour; signed char nx, ny, nz, pad; float x, y, z; } Vertex3DN;
_Static_assert(sizeof(Vertex3DN) == 28, "GE vertex layout");
enum { VT_3D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D };
static void churn_report(unsigned frames);
static void churn_top_report(unsigned frames);
static void shadow_report(unsigned frames);
extern int p4_skin_diff, p4_skin_finite;
static unsigned skin_diff_checked, skin_diff_count;
extern int p4_churn_top;
extern int p4_ge_skin;
int p4_skin_alpha = 1; /* 0: skinned batches' alpha unknown, two TEST_1 passes, as b128 */
int p4_bone_direct = 1; /* 0: sceGuBoneMatrix for every slot of every batch, as b123 */
static unsigned bones_sent;
static unsigned skin_ge_draws, skin_ge_batches, skin_ge_fallbacks, skin_ge_builds, skin_ge_bytes, skin_cpu_batches, skin_nonfinite_bones;
static unsigned skin_build_us, skin_build_max_us, skin_build_max_v; /* b212: skin cache build time */
extern int p4_skin_rigid;
static unsigned skin_rigid_batches, skin_rigid_tris; /* b207: one-bone batches built, their triangles */
extern int p4_skin_strip;
static unsigned strip_batches, strip_failed, strip_list_idx, strip_idx; /* b209 */
float p4_skin_lod_depth = 0; /* b245, see skin_geometry */
int p4_skin_lod_keep = 50;
static unsigned skin_lod_draws, skin_full_draws;
int p4_skin_free_wait = 1; /* b241 */
int p4_skin_dummy = 0;
static unsigned skin_free_waits, skin_dummies;
/* b213: p4_strip_why 1 runs b210's whole-mesh diagnosis (two strips over every mesh: on the PSP most of the 7 s of a
   party's strip build); p4_strip_check 1 also runs b209's linear start search and compares the strips */
extern int p4_strip_why, p4_strip_check;
static unsigned strip_checks, strip_check_diffs, strip_us;
extern int p4_skin_simplify, p4_mesh_override, p4_mesh_capture;
static unsigned mk_meshes, mk_tri_in, mk_tri_out, mk_us, mk_override_hits, mk_override_meshes, mk_captured; /* b211 */
/* b210: why the strips are short. Per mesh before batching: the game's own strip (PS2 data), one strip over the whole
   mesh, and one over the mesh with vertices of the same position welded (no UV seams): indices and triangles */
static unsigned sd_src_idx, sd_src_tri, sd_whole_idx, sd_weld_idx, sd_tri;
/* b171: the per-bone guard spheres test against planes built once per skin (p4_skin_guard_fast; check: the old test
   too, compared), and skin-to-bone x pose on the VFPU (p4_skin_vfpu; check: the FPU product too, compared). */
int p4_skin_guard_fast = 1, p4_skin_guard_check = 0, p4_skin_vfpu = 1, p4_skin_vfpu_check = 0; /* b172: vfpu on (b171 class: 440k checks, 0 diffs) */
/* b177: a character with a bone sphere beyond the guard band keeps its other batches on the GE; only the batches that
   use such a bone are skinned on the CPU and cut (b176 battle: the active member behind the camera went wholly to the
   CPU, ~15 meshes rebuilt a frame). 0: the whole character on the CPU, as b176; 2: every batch on the CPU (check). */
int p4_skin_split = 1;
static unsigned skin_guard_checks, skin_guard_diffs, skin_vfpu_unaligned, skin_vfpu_checks, skin_vfpu_diffs;
static float skin_vfpu_max_diff;
int p4_ge_light = 1; /* 1: lit meshes the GE draws whole are lit by the GE; 0: shaded on the CPU, as b75 */
static unsigned work_shaded, work_ge_emitted, work_cpu_emitted, work_ge_draws;
enum { POOL_VERTICES = 125 * 1024 }; /* b193, b202, b232: 1 K vertices of room for .bss each (peak seen ~9.3k of 64k a half) */
static Vertex3D vertex_store[POOL_VERTICES] __attribute__((aligned(64)));
static Vertex3D *vertex_pool = vertex_store;
static int vertex_cap = POOL_VERTICES;
#define MAX_VERTICES vertex_cap
static void *current_camera;
static Raster display_raster = { .width = 640, .height = 448, .depth = 32, .cType = TYPE_CAMERA };
static unsigned *feedback_pixels;
static int feedback_captured;
static int trace_atomics;
static int ptanks_drawn, particles_drawn;
enum { POOL_INDICES = 192 * 1024, MAX_GUARDED = 32 * 1024 }; /* b81 pipeline: 59990 of a 64 K half in Heaven */
static int index_cap = POOL_INDICES;
#define MAX_INDICES index_cap
static int guard_meshes, guard_clipped, guard_dropped, guard_spheres, guard_vertices;
int p4_light_unprelit = 1; /* 0: lit unprelit untextured meshes at full material colour, as b57 */
int p4_decal_bias = 0; /* depth units added to overlay passes; 0 as b57 */
int p4_guard_fast = 2; /* 0: the b72 classify loop, 1: b73 FPU loop, 2: VFPU */
int p4_guard_memo = 1; /* 1: an array classified all-inside with the same matrices last time skips the classify */
int p4_guard_check = 0; /* 1: every 64th VFPU classify is redone on the FPU and differences counted (0 diffs b85-b101) */
int p4_guard_clip_fast = 1; /* 1 (b102): clip_triangle_guard_fast; 0: clip_triangle_guard, as b101 */
int p4_guard_clip_check = 1; /* 1: every 64th clipped triangle is clipped both ways and differences counted */
static unsigned guard_clip_checks, guard_clip_diffs;
static const int *draw_fixed_alpha; /* known alpha bounds (lo, hi) of a superset of the vertices being drawn */
static const int *draw_skin_alpha; /* a GE-skinned batch: bounds of its lit alpha, material included (b129) */
int p4_guard_compact = 1; /* 1 (b99): a guarded mesh uses its cached compact arrays and only its own vertices are
                             classified; 0: the whole array rebuilt per guarded mesh, as b97 */
int p4_guard_gnear = 1; /* 1 (b103): the GE clips the near plane itself (PS2RC: "PPSSPP and the PSP agree"); the CPU
                          only cuts at the four band planes, which pass through the eye and so also cut everything
                          behind it. b101's near_ge sent corners behind the eye uncut: those leave the window. */
#define GUARD_MASK (p4_guard_gnear ? 0x1eu : 0x1fu)
static unsigned guard_offscreen;
int p4_guard_boxes = 1; /* b109 room with the hash fixed: 50.0 ms without, 45.0 ms with (off-screen chunks no longer reach
                          the GE); same picture. 1 (b104): guarded meshes drawn from the mesh cache are classified by the boxes of runs of
                          GUARD_CHUNK triangles (PS2RC's chunks): inside the band -> the GE, off screen -> dropped,
                          only the rest per vertex; the array-level per-vertex classify is skipped (sphere only) */
static unsigned guard_box_whole, guard_box_inside, guard_box_partial, guard_box_culled, guard_box_builds, guard_lazy_vertices;
extern int p4_guard_split, p4_guard_split_grow; /* b232 */
static unsigned split_meshes, split_tris_in, split_tris_out, split_refused, split_us;
int p4_guard_small = 0; /* b199: 0 as b198; 1 small partial chunks drawn uncut; 2 also whole meshes as a mesh-hit CALL.
                           b199 dialogue: no chunk qualifies (its band-crossing triangles are large): off until a split */
static unsigned guard_small_chunks, guard_small_whole, guard_box_same; /* same: guard box rebuilds of an entry that held the mesh */
int p4_guard_box_vfpu = 1, p4_guard_box_check = 0; /* b144: chunk boxes classified on the VFPU; check: also on the FPU, compared */
static unsigned guard_box_checks, guard_box_diffs;
int p4_guard_near_ge = 0; /* 1 (experiment): a triangle with a corner behind the near plane goes to the GE uncut, to see
                             on the device whether the GE clips the near plane itself (GU_CLIP_PLANES) */
static unsigned guard_near_ge;
int p4_guard_full_cache = 1; /* 1: GE-lit arrays of guarded meshes kept between frames; 0: rebuilt per draw, as b90 */
static unsigned guard_full_hits, guard_full_builds;
static void guard_result_report(void);
static void guard_result_clear(void);
static unsigned guard_memo_hits, guard_check_runs, guard_check_diffs, guard_world_checks, guard_world_unsafe, guard_world_stricter;
int p4_guard_world = 1; /* 1: sphere test against the camera's world-space planes; 0: per-atomic model x view x projection, as b88 */
int p4_guard_sphere = 1; /* 0: classify every vertex, for A/B of the sphere shortcut */
int p4_light_prelit = 1; /* RenderWare adds the lights to a lit mesh's prelight; 0: prelight only, as b57 (the parking was darker than the PS2) */
int p4_uv_shift_step = 1; /* the UV rebase takes whole multiples of this many tiles (b105 A/B: 16 changes the shift, and so the
                             cache key, 16 times less often while scrolling) */
int p4_uv_rebase = 1; /* 0: UVs as the game gives them, as b58 */
int p4_uv_memo = 1; /* 0: scan every UV array every draw, as b136 */
static unsigned uv_memo_hits;
static int uv_rebased;
int p4_alpha_write_test = 1; /* 0: alpha-only writes cover the whole quad, as b70 */
int p4_dst_alpha_passes = 0; /* 1: draw small destination-alpha screen-copy passes, as b61 (squares) */
/* b177: an alpha-only write (PS2 FBMSK 0x00ffffff) goes to the GE with the stencil test off and RGB masked, so the alpha
   byte takes each pixel's fragment alpha (Gouraud, texture) as the PS2's does, instead of one stencil value per draw.
   The passes that read it (blend 0x52/54/58) then draw too. Field Shadow (PCSX2 GS dump): a fan writes a radial alpha
   disc, then a screen copy and three smoke layers blend by it; skipped, only the spiky outline was left.
   0: one stencil value per draw and small passes skipped, as b176. Off: in PPSSPP it did not bring the Shadow's body
   back (its draws reach neither the 2D path nor Im3D) and drew a dark band by the minimap (a screen-copy pass). */
int p4_alpha_pixel = 0;
static int dst_alpha_skipped;
static unsigned tested_draws, tested_second, tested_opaque, tested_nodepth, tested_allfail, tested_scans; /* draw_tested */
extern int p4_gs_mesh_alpha;
static const void *draw_slot_v; static int *draw_slot_alpha; /* the cache entry being drawn: its vertices and alpha_lo, alpha_hi */
extern int p4_gs_test, p4_gs_test_skip;
int p4_guard_clip = 1; /* 0: every triangle goes to the GE as before, for A/B on the device (b98 had 0: holes in the
                           floor; b97 room: the guard cost 13.7 ms of CPU, so b99 made it cheap: p4_guard_compact) */
static int index_used;
static unsigned cache_bytes, cache_frame, cache_hits, cache_builds, cache_misses, cache_noslot, guard_mesh_draws;
int p4_mesh_cache = 1; /* 0: build every frame, as b68 */
int p4_lock_memo = 1; /* b167: unlocked geometry reuses its sampled key and UV shift (draw_meshes_body); 0: as b166 */
static unsigned lock_memo_hits;
int p4_cache_audit = 0; /* see audit_inputs */
static unsigned audit_checks, audit_stale[3], audit_stale_vertices;
unsigned p4_cache_budget = 6144 * 1024; /* b72: 1.5 MB was full, 170 mesh draws a frame missed. b113 classroom: 4 MB full,
                                             18 misses a frame; 6 MB: CPU 91.8 -> 71.3 ms (it used 4.45 MB, 1.8 MB heap left) */
enum { CACHE_HEAP_RESERVE = 1024 * 1024, CACHE_BUDGET_MIN = 4096 * 1024, CACHE_BUDGET_MAX = 6144 * 1024 }; /* heap kept free; budget range */
/* On the heap: .bss must end below the retail image at P4_IMAGE. */
static unsigned short *index_pool, *index_store;
static float (*guard_clip)[4];
static unsigned char *guard_code;
static int vertex_used, atomics_drawn, atomics_skipped, sectors_drawn, skinned_drawn, pipeline_other, frustum_tests, frustum_out;

/* rwDEVICESYSTEMOPEN */
int func_003fe700(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    extern int p4_quiet;
    if (!feedback_pixels) {
        feedback_pixels = memalign(64, 512 * 512 * 4);
        if (!feedback_pixels) {
            p4_log("screen feedback allocation failed\n");
            return 0;
        }
        memset(feedback_pixels, 0, 512 * 512 * 4);
        sceKernelDcacheWritebackInvalidateRange(feedback_pixels, 512 * 512 * 4);
    }
    if (!index_pool) {
        index_store = index_pool = memalign(64, POOL_INDICES * sizeof *index_pool);
        guard_clip = memalign(64, MAX_GUARDED * sizeof *guard_clip);
        guard_code = malloc(MAX_GUARDED);
        if (!index_pool || !guard_clip || !guard_code) {
            p4_log("guard band buffers failed, clipping off\n");
            p4_guard_clip = 0;
        }
        if (!index_pool)
            p4_pipeline = 0;
    }
    p4_quiet = 1;
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    list_to_cached();
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, STRIDE);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)(STRIDE * SCREEN_H * 4), STRIDE);
    sceGuDepthBuffer((void *)(2 * STRIDE * SCREEN_H * 4), STRIDE);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthRange(65535, 0);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    flip_event = sceKernelCreateEventFlag("p4flip", PSP_EVENT_WAITMULTIPLE, 0, 0);
    if (flip_event < 0)
        p4_log("flip event failed %x, polling\n", flip_event);
    sceGuSetCallback(GU_CALLBACK_FINISH, on_ge_finish);
    flip_thread = sceKernelCreateThread("p4flip", flip_main, 0x0f, 4096, PSP_THREAD_ATTR_USER, 0);
    if (flip_thread < 0 || sceKernelStartThread(flip_thread, 0, 0) < 0) {
        p4_log("flip thread failed %x, pipeline off\n", flip_thread);
        flip_thread = -1;
    }
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    p4_log("GU up\n");
    return 1;
}

/* rwDEVICESYSTEMUSEMODE: bookkeeping of the retail mode table, 24 bytes per mode. */
void func_003fe470(int mode)
{
    if (G32(0xb958 - 0x10000) || mode >= 0x5a || mode < -1)
        return;
    if (mode != -1) {
        unsigned row = 0x70b840 + mode * 0x18;
        unsigned width = R32(row), height = R32(row + 4), depth = R32(row + 8), flags = R32(row + 12);
        R32(0x88c9bc) = flags;
        R32(0x88c9b0) = width;
        R32(0x88c9b4) = height;
        R32(0x88c9d8) = R32(row + 16);
        R32(0x88c9dc) = R32(row + 20);
        R32(0x88c9c0) = R32(row + 16);
        R32(0x88c9c4) = R32(row + 20);
        R32(0x88c9b8) = depth;
        if (!(flags & 0x100)) {
            R8(0x88c9e0) = 2;
            R8(0x88c9e1) = 2;
            R8(0x88c9e3) = 1;
            R32(0x88c9c8) = width;
            R32(0x88c9cc) = height;
        } else {
            R8(0x88c9e0) = 1;
            R8(0x88c9e1) = 1;
            R32(0x88c9cc) = (int)height >> 1;
            if (G32(0xb920 - 0x10000) == 0)
                G32(0xb920 - 0x10000) = width;
            R8(0x88c9e3) = 0;
            R32(0x88c9c8) = G32(0xb920 - 0x10000);
        }
        R8(0x88c9e2) = 0;
        R32(0x88c9d0) = depth;
        R32(0x88c9d4) = flags;
    }
    G32(0xb960 - 0x10000) = mode;
    G32(0xb95c - 0x10000) = R32(0x88c9b8) == 0x20 ? 0x500 : 0x100;
}

/* rwDEVICESYSTEMFINALIZESTART's GS half */
int func_00401aa0(void) { return 1; }

/* Native textures (read from files, never drawn into) go to the GE swizzled: one 16-byte by 8-row block is one texture
   cache line (PPSSPP docs, GE performance). RenderWare's raster keeps its fields; a 64-byte head in front of the
   pixels says the buffer is swizzled and how it is padded. A lock turns it back into rows. */
int p4_swizzle_textures = 1; /* 0: textures stay in rows, as b76 */
enum { SWIZZLE_HEAD = 64, SWIZZLE_MAGIC = 0x5a1e5a1e };
/* used: the palette entries the texels use (bit per entry); alpha_min: the smallest texel alpha, for draw_tested. */
typedef struct { unsigned magic, check, wbytes, rows; unsigned char used[32]; unsigned alpha_min, levels, alpha_max; } SwizzleHead;
_Static_assert(sizeof(SwizzleHead) <= 64, "swizzle head");
static unsigned swizzled_bytes, swizzled_textures, mip_bytes, mip_textures;

static const SwizzleHead *swizzle_head(const unsigned char *pixels)
{
    const SwizzleHead *h = (const SwizzleHead *)(pixels - SWIZZLE_HEAD);
    return pixels && h->magic == SWIZZLE_MAGIC && h->check == ((unsigned)pixels ^ SWIZZLE_MAGIC) ? h : 0;
}

/* Smallest alpha of the texels as the GE samples them (after the PS2 0x80 -> 0xff unlock): with a palette, of the
   entries the texels use. */
static unsigned raster_alpha_min(const Raster *r, const unsigned char *used, unsigned *max)
{
    unsigned m = 255, big = 0;
    *max = 255;
    if (r->depth <= 8) {
        if (!r->palette)
            return 0;
        const unsigned *entry = (const unsigned *)r->palette;
        for (int i = 0; i < 1 << r->depth; i++)
            if (used[i >> 3] >> (i & 7) & 1) {
                unsigned a = entry[i] >> 24;
                if (a < m) m = a;
                if (a > big) big = a;
            }
        *max = big;
        return m;
    }
    for (int y = 0; y < r->height; y++) {
        const unsigned char *row = r->originalPixels + y * r->stride;
        if (r->depth == 32) {
            for (int x = 0; x < r->width; x++) {
                unsigned a = row[x * 4 + 3];
                if (a < m) m = a;
                if (a > big) big = a;
            }
        } else if (r->depth == 16) {
            for (int x = 0; x < r->width; x++) {
                unsigned a = row[x * 2 + 1] & 0x80 ? 255 : 0;
                if (a < m) m = a;
                if (a > big) big = a;
            }
        } else {
            return 0;
        }
    }
    *max = big;
    return m;
}

static void raster_used_entries(const Raster *r, unsigned char used[32])
{
    memset(used, 0, 32);
    for (int y = 0; y < r->height && r->depth <= 8; y++) {
        const unsigned char *row = r->originalPixels + y * r->stride;
        for (int x = 0; x < r->width; x++) {
            unsigned i = r->depth == 8 ? row[x] : row[x >> 1] >> ((x & 1) * 4) & 15;
            used[i >> 3] |= 1 << (i & 7);
        }
    }
}

/* Mipmaps (PPSSPP GE performance; skill textures-uv): far surfaces read a smaller level, fewer texels per pixel.
   Levels come from level 0 by 2:1 point decimation, so palette indices stay as they are, and stop at one swizzle block
   (16 bytes) wide or 8 rows high. Only power-of-two rasters of at least p4_mip_min texels; levels sit after level 0 in
   the same block. p4_mipmaps is read when a texture is bound: 0 draws level 0 only, as b88 (live A/B). */
int p4_mip_build = 1, p4_mip_min = 64, p4_mipmaps = 1;
enum { MAX_MIPS = 8 };
static int mip_level_size(const Raster *r, int level, int *wbytes, int *rows)
{
    int w = r->width >> level, h = r->height >> level;
    *wbytes = ((w * r->depth + 7) / 8 + 15) & ~15;
    *rows = (h + 7) & ~7;
    return *wbytes * *rows;
}
static int mip_levels(const Raster *r)
{
    if (!p4_mip_build || r->width < p4_mip_min || r->height < p4_mip_min || (r->width & (r->width - 1)) || (r->height & (r->height - 1)))
        return 1;
    int levels = 1;
    while (levels < MAX_MIPS && (r->width >> levels) * r->depth / 8 >= 16 && (r->height >> levels) >= 8)
        levels++;
    return levels;
}
/* level l in rows (stride bytes a row) from level 0's rows: texel (x << l, y << l) */
static void mip_decimate(const Raster *r, int level, unsigned char *out, int stride)
{
    int w = r->width >> level, h = r->height >> level;
    memset(out, 0, stride * h);
    for (int y = 0; y < h; y++) {
        const unsigned char *row = r->originalPixels + (y << level) * r->stride;
        unsigned char *o = out + y * stride;
        for (int x = 0; x < w; x++) {
            int sx = x << level;
            if (r->depth == 32)
                memcpy(o + x * 4, row + sx * 4, 4);
            else if (r->depth == 16)
                memcpy(o + x * 2, row + sx * 2, 2);
            else if (r->depth == 8)
                o[x] = row[sx];
            else
                o[x >> 1] |= (row[sx >> 1] >> ((sx & 1) * 4) & 15) << ((x & 1) * 4);
        }
    }
}

static void swizzle_raster(Raster *r)
{
    if (!p4_swizzle_textures || !r->originalPixels || swizzle_head(r->originalPixels))
        return;
    int wbytes = (r->stride + 15) & ~15, rows = (r->height + 7) & ~7;
    int levels = mip_levels(r), total = wbytes * rows, w, h;
    for (int l = 1; l < levels; l++)
        total += mip_level_size(r, l, &w, &h);
    unsigned char *base = memalign(64, SWIZZLE_HEAD + total), *linear = 0;
    if (base && levels > 1 && !(linear = malloc(r->stride * r->height / 4))) {
        free(base);
        base = 0;
    }
    if (!base) { /* no room for the levels: level 0 only */
        levels = 1;
        total = wbytes * rows;
        base = memalign(64, SWIZZLE_HEAD + total);
    }
    if (!base)
        return;
    unsigned char *pixels = base + SWIZZLE_HEAD;
    ge_swizzle(pixels, r->originalPixels, r->stride, r->height, wbytes, rows, 1);
    unsigned char *level = pixels + wbytes * rows;
    for (int l = 1; l < levels; l++) {
        int size = mip_level_size(r, l, &w, &h), stride = ((r->width >> l) * r->depth + 7) / 8;
        mip_decimate(r, l, linear, stride);
        ge_swizzle(level, linear, stride, r->height >> l, w, h, 1);
        level += size;
    }
    free(linear);
    SwizzleHead *head = (SwizzleHead *)base;
    *head = (SwizzleHead){ SWIZZLE_MAGIC, (unsigned)pixels ^ SWIZZLE_MAGIC, wbytes, rows, {0}, 0, levels };
    raster_used_entries(r, head->used); /* the levels' texels are a subset of level 0's: same entries, alpha */
    head->alpha_min = raster_alpha_min(r, head->used, &head->alpha_max);
    sceKernelDcacheWritebackRange(base, SWIZZLE_HEAD + total);
    free(r->originalPixels);
    r->originalPixels = pixels;
    swizzled_textures++;
    swizzled_bytes += wbytes * rows;
    if (levels > 1)
        mip_textures++, mip_bytes += total - wbytes * rows;
}

static void unswizzle_raster(Raster *r)
{
    const SwizzleHead *h = swizzle_head(r->originalPixels);
    if (!h)
        return;
    unsigned char *linear = memalign(64, r->stride * r->height);
    if (!linear)
        return;
    ge_swizzle(r->originalPixels, linear, r->stride, r->height, h->wbytes, h->rows, 0);
    free(r->originalPixels - SWIZZLE_HEAD);
    r->originalPixels = linear;
}

/* rwSTANDARDRASTERCREATE: retail's validation, then plain memory the GE can read. */
int p4r_003f7a70(void *out, Raster *r, int flags);
/* Render to texture (b165). A camera whose frame buffer is a camera texture (RenderWare type 5) draws into its own
   VRAM rectangle, and binding that raster samples the rectangle directly. Seen: 512x256x16 in a battle skill (PS0C2,
   skill 08C), 128x128x32 in event f007_002. Before this every camera drew onto the screen. VRAM: two frame buffers,
   depth, then the screen copy for feedback effects up to RTT_TAIL; only the tail (~136 KB) is free. The camera texture
   shares the screen's depth buffer: its clear and draws overwrite the top-left of the main scene's depth. */
enum { TYPE_CAMERA_TEXTURE = 5, RTT_SLOTS = 4, VRAM_END = 0x200000,
       RTT_FULL_BASE = 2 * STRIDE * SCREEN_H * 4 + STRIDE * SCREEN_H * 2, /* = HALF_VRAM, the screen copy */
       RTT_TAIL = (RTT_FULL_BASE + STRIDE * (SCREEN_H + 1) * 4 + 0x1fff) & ~0x1fff };
int p4_rtt = 1;       /* 0: camera textures draw onto the screen, as before b165 */
int p4_rtt_scale = 0; /* 0: full size if the free VRAM tail holds it, else half; 1: full size, taking the screen
                         copy's VRAM (feedback effects then copy the screen to RAM, p4_feedback_half 0); 2: half */
extern int p4_feedback_half;
typedef struct { Raster *raster; unsigned offset, bytes; int w, h, psm, scale; } RttSlot;
static RttSlot rtt[RTT_SLOTS];
static RttSlot *rtt_target; /* the camera texture being drawn into; 0: the screen */
static int rtt_restore;     /* a target vanished outside a list: the next list starts by binding the screen */
static unsigned rtt_passes, rtt_clears, rtt_atomics, rtt_2d; /* per 256 frames, for the log */
static int target_w = SCREEN_W, target_h = SCREEN_H;
static float target_sx = SCREEN_W / 640.0f, target_sy = SCREEN_H / 448.0f; /* game 2D coordinates to target pixels */

static RttSlot *rtt_find(const Raster *r)
{
    for (int i = 0; r && i < RTT_SLOTS; i++)
        if (rtt[i].raster == r)
            return &rtt[i];
    return 0;
}
static int rtt_place(unsigned base, unsigned bytes, unsigned *at)
{
    unsigned candidates[RTT_SLOTS + 1];
    int n = 0;
    candidates[n++] = base;
    for (int i = 0; i < RTT_SLOTS; i++)
        if (rtt[i].raster && rtt[i].offset + rtt[i].bytes >= base)
            candidates[n++] = (rtt[i].offset + rtt[i].bytes + 0x1fff) & ~0x1fffu;
    for (int c = 0; c < n; c++) {
        unsigned a = candidates[c];
        int ok = a + bytes <= VRAM_END;
        for (int i = 0; ok && i < RTT_SLOTS; i++)
            if (rtt[i].raster && a < rtt[i].offset + rtt[i].bytes && rtt[i].offset < a + bytes)
                ok = 0;
        if (ok) {
            *at = a;
            return 1;
        }
    }
    return 0;
}
static RttSlot *rtt_get(Raster *r)
{
    RttSlot *s = rtt_find(r), *free_slot = 0;
    if (s || !p4_rtt || !r || (r->cType & TYPE_MASK) != TYPE_CAMERA_TEXTURE)
        return s;
    for (int i = 0; i < RTT_SLOTS && !free_slot; i++)
        if (!rtt[i].raster)
            free_slot = &rtt[i];
    int bpp = r->depth == 16 ? 2 : 4, full_w = 8, full_h = 8;
    while (full_w < r->width && full_w < 512)
        full_w <<= 1;
    while (full_h < r->height && full_h < 512)
        full_h <<= 1;
    for (int scale = p4_rtt_scale == 2 ? 2 : 1; free_slot && scale <= 2; scale++) {
        int w = full_w / scale, h = full_h / scale;
        unsigned bytes = w * h * bpp, base = scale == 1 && p4_rtt_scale == 1 ? RTT_FULL_BASE : RTT_TAIL, at;
        if (!rtt_place(base, bytes, &at))
            continue;
        if (base == RTT_FULL_BASE)
            p4_feedback_half = 0;
        *free_slot = (RttSlot){ r, at, bytes, w, h, bpp == 2 ? GU_PSM_5551 : GU_PSM_8888, scale };
        p4_log("rtt raster %x %dx%dx%d -> %dx%d at vram %x scale %d\n", (unsigned)r, r->width, r->height, r->depth, w, h, at, scale);
        return free_slot;
    }
    static int refused;
    if (refused++ < 4)
        p4_log("rtt raster %x %dx%dx%d: no VRAM, drawn on the screen\n", (unsigned)r, r->width, r->height, r->depth);
    return 0;
}
/* Point the GE at a camera texture (s) or back at the screen (0). Needs an open list. */
static void rtt_bind_target(RttSlot *s)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    if (rtt_target && rtt_target != s)
        sceGuTexFlush(); /* the texture cache may hold the rectangle as it was before this pass */
    rtt_target = s;
    rtt_passes += s != 0;
    target_w = s ? s->w : SCREEN_W;
    target_h = s ? s->h : SCREEN_H;
    target_sx = s ? (float)s->w / s->raster->width : SCREEN_W / 640.0f;
    target_sy = s ? (float)s->h / s->raster->height : SCREEN_H / 448.0f;
    if (s)
        sceGuDrawBufferList(s->psm, (void *)s->offset, s->w);
    else
        sceGuDrawBufferList(GU_PSM_8888, draw_buffer, STRIDE);
    sceGuOffset(2048 - target_w / 2, 2048 - target_h / 2);
    sceGuViewport(2048, 2048, target_w, target_h);
    sceGuScissor(0, 0, target_w, target_h);
}

int func_003f98a0(void *out, Raster *r, int flags)
{
    if (!p4r_003f7a70(out, r, flags)) {
        p4_log("TRACE raster refused %dx%dx%d flags %x\n", r->width, r->height, r->depth, flags);
        p4_backtrace();
        return 0;
    }
    r->cpPixels = r->palette = r->originalPixels = 0;
    if ((r->cFlags & DONT_ALLOCATE) || (r->cType & TYPE_MASK) == TYPE_CAMERA || (r->cType & TYPE_MASK) == TYPE_ZBUFFER)
        return 1;
    r->stride = (r->width * r->depth + 7) / 8;
    r->originalPixels = memalign(64, r->stride * r->height);
    if (r->depth <= 8)
        r->palette = memalign(64, 4 << r->depth);
    if (!r->originalPixels)
        p4_log("TRACE raster no memory %dx%dx%d stride %d flags %x type %x\n", r->width, r->height, r->depth, r->stride, flags, r->cType);
    return r->originalPixels != 0;
}

/* rwSTANDARDRASTERDESTROY */
int func_003fb830(void *out, Raster *r)
{
    RttSlot *slot = rtt_find(r);
    if (slot) {
        if (rtt_target == slot && in_frame)
            rtt_bind_target(0);
        else if (rtt_target == slot)
            rtt_target = 0, rtt_restore = 1; /* begin_frame binds the screen in the next list */
        slot->raster = 0;
    }
    if (!(r->cFlags & DONT_ALLOCATE)) {
        free(swizzle_head(r->originalPixels) ? r->originalPixels - SWIZZLE_HEAD : r->originalPixels);
        free(r->palette);
    }
    return 1;
}

/* GE textures stay in RAM; the PS2's seven-slot VRAM residency cache does not own them. */
int RpSkyTexCacheRasterLock(Raster *raster, int locked) { return 1; }

/* rwSTANDARDCAMERABEGINUPDATE */
int func_003f6a10(void *out, void *camera)
{
    if (SKIPPING) {
        current_camera = camera;
        return 1;
    }
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    current_camera = camera;
    begin_frame();
    if (p4_probe_2d == 3 && census2d_shown < 64)
        census2d_shown++, p4_log("census2d camera begin %x raster %x\n", (unsigned)camera, *(unsigned *)((char *)camera + 0x60));
    {
        static void *seen[16];
        static int seen_n;
        Raster *fb = *(Raster **)((char *)camera + 0x60);
        int known = 0;
        for (int i = 0; i < seen_n; i++)
            known |= seen[i] == (void *)fb;
        if (!known && seen_n < 16) {
            seen[seen_n++] = fb;
            if (fb)
                p4_log("camera %x raster %x %dx%dx%d type %x flags %x parent %x pixels %x\n", (unsigned)camera, (unsigned)fb, fb->width, fb->height,
                       fb->depth, fb->cType, fb->cFlags, (unsigned)fb->parent, (unsigned)fb->originalPixels);
            else
                p4_log("camera %x raster 0\n", (unsigned)camera);
        }
        RttSlot *slot = rtt_get(fb);
        if (slot != rtt_target)
            rtt_bind_target(slot);
    }
    return 1;
}

/* rwSTANDARDCAMERAENDUPDATE: a camera texture pass hands the GE back to the screen. */
int func_003f73d0(void *out, void *camera)
{
    if (rtt_target && in_frame)
        rtt_bind_target(0);
    return 1;
}

/* A clear as 32-pixel-wide sprites: the GE fills one 8888 texture-cache-wide column at a time, a full-screen sprite
   walks across rows (PPSSPP docs, GE performance: "never clear using a single rectangle"). The colour carries the
   stencil in its alpha, as sceGuClear does for 8888. */
int p4_clear_stripes = 1; /* 0: one sceGuClear rectangle, as b76 */
static void clear_striped(int bits, unsigned colour, unsigned short depth)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    typedef struct { unsigned colour; short x, y, z, pad; } ClearVertex;
    enum { STRIPE = 32 };
    const int count = (target_w + STRIPE - 1) / STRIPE * 2;
    ClearVertex *v = sceGuGetMemory(count * sizeof *v);
    for (int i = 0; i < count / 2; i++) {
        int x1 = (i + 1) * STRIPE < target_w ? (i + 1) * STRIPE : target_w;
        v[2 * i] = (ClearVertex){ colour, i * STRIPE, 0, depth, 0 };
        v[2 * i + 1] = (ClearVertex){ colour, x1, target_h, depth, 0 };
    }
    sceGuSendCommandi(211, (bits & 7) << 8 | 1); /* CLEARMODE on, with the buffers to write */
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, count, 0, v);
    sceGuSendCommandi(211, 0);
}

/* rwSTANDARDCAMERACLEAR: colour is RwRGBA, R first like the GE. */
int func_003f7490(void *camera, unsigned *colour, int mask)
{
    if (SKIPPING)
        return 1;
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    rtt_clears += rtt_target != 0;
    if (p4_probe_2d == 3 && census2d_shown < 64)
        census2d_shown++, p4_log("census2d camera clear mask %d colour %08x\n", mask, *colour);
    int bits = (mask & 1 ? GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT : 0) | (mask & 2 ? GU_DEPTH_BUFFER_BIT : 0);
    begin_frame();
    int timed = ge_time_begin();
    sceGuClearColor(*colour);
    sceGuClearStencil(*colour >> 24);
    sceGuClearDepth(0);
    sceGuPixelMask(0);
    if (bits && p4_clear_stripes)
        clear_striped(bits, *colour, 0);
    else if (bits)
        sceGuClear(bits);
    ge_time_end(timed, 3);
    return 1;
}

/* What the game is doing, for the log: sequence and running tasks. */
static void watch(void)
{
    unsigned task = G32(0xb1a8 - 0x10000);
    int *work = task ? *(int **)(task + 0x38) : 0;
    static int last = -2;
    if (work && work[1] != last) {
        last = work[1];
        p4_log("sequence %d previous %d\n", work[1], work[2]);
    }
}

/* Probe: who empties the device table. Slots 0x887300 up to 0x887314: render state set and get, Im2D. */
static void watch_device_table(int frame)
{
    static unsigned saved[6];
    static int reported;
    for (int i = 0; i < 6; i++) {
        unsigned now = R32(0x887300 + i * 4);
        if (now)
            saved[i] = now;
        else if (saved[i]) {
            if (!reported++)
                p4_log("TRACE device slot %d emptied by frame %d\n", i, frame);
            R32(0x887300 + i * 4) = saved[i];
        }
    }
}

static int pipe_last_list = -1; /* the GE list id and end of the frame handed to the flip thread, for the watchdog */
static void *pipe_last_end;
static unsigned pipe_watchdog_gave_up;
int p4_vsync_late = 1; /* 0: a frame slower than its vblank cap flips at once instead of waiting for the next vblank */
/* rwSTANDARDRASTERSHOWRASTER */
int func_003f79b0(void *raster, void *device, int flags)
{
    if (skip_frame && !in_frame) { /* a skipped frame: logic ran, nothing was drawn */
        skipped_frames++;
        skip_decide(skip_cap);
        return 1;
    }
    skip_cap = 2; /* the game clock is 1/30 s a frame everywhere (iGpffff8040); b148 took 60 Hz where the field noise pass was off and ran the game at ~37 frames a second */
    skip_frame = 0;
    skip_decide(skip_cap); /* for the next frame; nothing below draws */
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    static int frames;
    static unsigned last_vblank;
#ifdef P4_HW_PROFILE
    static unsigned present_end, window_start, samples, cpu_us, ge_us, vblank_us;
    unsigned before_sync = sceKernelGetSystemTimeLow();
    if (present_end)
        cpu_us += before_sync - present_end;
#endif
    watch_device_table(frames);
    if (p4_exp_ge_time)
        ge_time_frames++;
    prof_begin(PROF_SYNC);
    int pipe = p4_pipeline && flip_thread >= 0;
    if (vertex_used > pipe_peak_v) pipe_peak_v = vertex_used;
    if (index_used > pipe_peak_i) pipe_peak_i = index_used;
    if (pipe) {
        /* frame N-1 on screen: this list may run and be handed to the flip thread. b115 hung here entering a battle
           (queued 0x704, finished 0x703, shown 0x703, gate closed): the GE never finished the last list. After 250 ms
           log what the GE says about it and move its stall to its end; after 1 s stop waiting. */
        unsigned waited_from = sceKernelGetSystemTimeLow();
        int kicked = 0;
        while ((int)(shown_seq - ge_queued) < 0) {
            flip_wait(FLIP_SHOWN, 2000);
            unsigned waited = sceKernelGetSystemTimeLow() - waited_from;
            if (!kicked && waited > 250000) {
                kicked = 1;
                int state = pipe_last_list >= 0 ? sceGeListSync(pipe_last_list, 1) : -1;
                p4_log("TRACE pipeline watchdog: queued %u finished %u shown %u flip %u gate %d/%u depth %d list %x state %d end %p\n",
                       ge_queued, ge_finished, shown_seq, flip_seq, gate_closed, gate_seq, gu_object_stack_depth,
                       pipe_last_list, state, pipe_last_end);
                /* the last word of each kind the GE executed: which draw it is stuck in */
                p4_log("TRACE watchdog GE base %08x vaddr %08x iaddr %08x prim %08x vtype %08x\n",
                       sceGeGetCmd(0x10), sceGeGetCmd(0x01), sceGeGetCmd(0x02), sceGeGetCmd(0x04), sceGeGetCmd(0x12));
                p4_log("TRACE watchdog GE jump %08x call %08x signal %08x tbp0 %08x tbw0 %08x lists %p\n",
                       sceGeGetCmd(0x08), sceGeGetCmd(0x0A), sceGeGetCmd(0x0E), sceGeGetCmd(0xA0), sceGeGetCmd(0xA8), (void *)list_store);
                if (pipe_last_list >= 0 && pipe_last_end)
                    sceGeListUpdateStallAddr(pipe_last_list, pipe_last_end);
            } else if (kicked == 1 && waited > 1000000) {
                kicked = 2;
                p4_log("TRACE pipeline watchdog: still waiting after 1 s (finished %u shown %u state %d), going on\n",
                       ge_finished, shown_seq, pipe_last_list >= 0 ? sceGeListSync(pipe_last_list, 1) : -1);
                pipe_watchdog_gave_up++;
                break;
            }
        }
        gate_poll();
    } else {
        while (flip_seq != shown_seq) /* a frame the flip thread still has to show */
            sceKernelDelayThread(100);
        if (gate_closed) {
            gate_closed = 0;
            gu_object_stack_depth = 0;
        }
        if (in_frame) {
            sceGuFinish();
            ge_queued++;
            in_frame = 0;
        }
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    }
    prof_end(PROF_SYNC);
#ifdef P4_HW_PROFILE
    unsigned after_sync = sceKernelGetSystemTimeLow();
    ge_us += after_sync - before_sync;
#endif
    vertex_used = index_used = 0;
    cache_frame++;
    feedback_captured = 0;
    /* The game advances a fixed 1/30 s a frame everywhere (PS2: 60 VPS, 30 FPS): never show more than one frame per two vblanks. */
    unsigned elapsed = (unsigned)sceDisplayGetVcount() - last_vblank;
    int wait = frames && elapsed < 2 ? 2 - elapsed : 1;
    if (pipe) /* the flip thread waits for the vblank; here only a frame faster than that waits */
        wait = 2 - (int)elapsed;
    else if (!p4_vsync_late && frames && elapsed >= 2u)
        wait = 0; /* already slower than the cap: show it now (may tear) rather than idle up to a vblank (b86: ~10 ms) */
    prof_begin(PROF_VBLANK);
    while (wait-- > 0)
        sceDisplayWaitVblankStart();
    prof_end(PROF_VBLANK);
    last_vblank = sceDisplayGetVcount();
    void *other = draw_buffer ? (void *)0 : (void *)(STRIDE * SCREEN_H * 4);
    if (pipe) {
        if (in_frame) {
            sceGuFinish();
            pipe_last_list = ge_list_executed[0];
            pipe_last_end = gu_list->current;
            ge_queued++;
            in_frame = 0;
            flip_fb = draw_buffer;
            flip_seq = ge_queued;
            flip_signal(FLIP_WORK);
            draw_buffer = other;
        }
        gate_close(ge_queued); /* the next list draws into the buffer on screen until this one is shown */
        pipe_set ^= 1;
        list = list_store[pipe_set];
        vertex_pool = vertex_store + pipe_set * (POOL_VERTICES / 2);
        index_pool = index_store + pipe_set * (POOL_INDICES / 2);
        vertex_cap = POOL_VERTICES / 2;
        index_cap = POOL_INDICES / 2;
    } else {
        sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)draw_buffer), STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
        draw_buffer = other;
        flip_seq = shown_seq = ge_queued;
        pipe_set = 0;
        list = list_store[0];
        vertex_pool = vertex_store;
        index_pool = index_store;
        vertex_cap = POOL_VERTICES;
        index_cap = POOL_INDICES;
    }
#ifdef P4_HW_PROFILE
    prof_end(PROF_FRAME);
    if (samples == 127)
        prof_report(128);
    prof_begin(PROF_FRAME);
#endif
    /* Debug freeze: hold L + R + SELECT and the frame just shown stays, its display list and vertices untouched, so a
       moment too short to catch can be read over PSPLink (screenshot, savemem of list/vertex_pool). */
    {
        SceCtrlData pad;
        const unsigned combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT;
        if (sceCtrlPeekBufferPositive(&pad, 1) > 0 && (pad.Buttons & combo) == combo) {
            p4_log("FREEZE frame %d list %x\n", frames, (unsigned)list);
            do {
                sceDisplayWaitVblankStart();
            } while (sceCtrlPeekBufferPositive(&pad, 1) > 0 && (pad.Buttons & combo) == combo);
            p4_log("FREEZE end\n");
        }
    }
#ifdef P4_HW_PROFILE
    unsigned after_present = sceKernelGetSystemTimeLow();
    vblank_us += after_present - after_sync;
    if (present_end && ++samples == 128) {
        p4_log("hwperf n=%u ms=%u cpu_us=%u ge_wait_us=%u present_us=%u\n",
               samples, (after_present - window_start) / 1000,
               cpu_us / samples, ge_us / samples, vblank_us / samples);
        samples = cpu_us = ge_us = vblank_us = 0;
        window_start = after_present;
    }
    if (!present_end) {
        window_start = after_present;
        ge_us = vblank_us = 0;
    }
    present_end = after_present;
#endif
    watch();
    if (!(++frames & 255)) {
        extern void p4_memstat_report(void);
        p4_memstat_report();
        p4_log("frames %d atomics %d skipped %d ptanks %d/%d sectors %d skinned %d other %d tests %d out %d af48 %d\n", frames, atomics_drawn, atomics_skipped, ptanks_drawn, particles_drawn, sectors_drawn, skinned_drawn, pipeline_other, frustum_tests, frustum_out, G32(0xaf48 - 0x10000));
        p4_log("guard on %d meshes %d clipped %d dropped %d spheres %d vertices %d uvrebase %d uvmemo %d hits %u dstalpha_skipped %d\n", p4_guard_clip, guard_meshes, guard_clipped, guard_dropped, guard_spheres, guard_vertices, uv_rebased, p4_uv_memo, uv_memo_hits, dst_alpha_skipped);
        uv_memo_hits = 0;
        if (rtt_passes)
            p4_log("rtt passes %u clears %u atomics %u 2d %u\n", rtt_passes, rtt_clears, rtt_atomics, rtt_2d);
        rtt_passes = rtt_clears = rtt_atomics = rtt_2d = 0;
        { extern void p4_blend_report(void); p4_blend_report(); }
        { extern void p4_census3d_report(void); p4_census3d_report(); }
        { extern unsigned p4_gs_alpha_splits(void); unsigned n = p4_gs_alpha_splits(); if (n) p4_log("gs alpha fb_only splits %u\n", n); }
        p4_log("guard near_ge %d triangles %u clip_fast %d checks %u diffs %u gnear %d offscreen %u\n", p4_guard_near_ge, guard_near_ge, p4_guard_clip_fast, guard_clip_checks, guard_clip_diffs, p4_guard_gnear, guard_offscreen);
        p4_log("guard boxes %d meshes whole %u chunks inside %u partial %u culled %u builds %u lazy vertices %u vfpu %d checks %u diffs %u\n", p4_guard_boxes, guard_box_whole, guard_box_inside, guard_box_partial, guard_box_culled, guard_box_builds, guard_lazy_vertices, p4_guard_box_vfpu, guard_box_checks, guard_box_diffs);
        guard_box_checks = guard_box_diffs = 0;
        guard_near_ge = guard_clip_checks = guard_clip_diffs = guard_offscreen = 0;
        p4_log("guard small %d chunks %u whole %u; box rebuilds of the same mesh %u\n", p4_guard_small, guard_small_chunks, guard_small_whole, guard_box_same);
        p4_log("guard split %d grow %d: meshes %u triangles %u -> %u refused %u took %u us\n", p4_guard_split, p4_guard_split_grow, split_meshes,
               split_tris_in, split_tris_out, split_refused, split_us);
        split_meshes = split_tris_in = split_tris_out = split_refused = split_us = 0;
        guard_result_report();
        guard_box_same = 0;
        guard_box_whole = guard_box_inside = guard_box_partial = guard_box_culled = guard_box_builds = guard_lazy_vertices = 0;
        guard_small_chunks = guard_small_whole = 0;
        struct mallinfo heap = mallinfo();
        /* b114 set the budget to what the cache held when the heap was short; during a load that was 0 and the cache
           stayed off for the session (Heaven 38 KB free while loading, 1.3 MB a few seconds later). Now the budget only
           follows the heap between the b72 4 MB and 6 MB, both ways. */
        extern unsigned p4_heap_unclaimed(void);
        unsigned heap_free = heap.fordblks + p4_heap_unclaimed(); /* b166: the arena's free alone missed the untouched heap */
        unsigned room = cache_bytes + (heap_free > CACHE_HEAP_RESERVE ? heap_free - CACHE_HEAP_RESERVE : 0);
        unsigned want = room < CACHE_BUDGET_MIN ? CACHE_BUDGET_MIN : room > CACHE_BUDGET_MAX ? CACHE_BUDGET_MAX : room;
        if (want != p4_cache_budget) {
            p4_log("mesh cache budget %u -> %u: heap free %u, cache %u\n", p4_cache_budget, want, heap_free, cache_bytes);
            p4_cache_budget = want;
        }
        p4_log("mesh cache on %d hits %u builds %u misses %u noslot %u bytes %u budget %u heap_free %u guarded_meshes %u\n", p4_mesh_cache, cache_hits, cache_builds, cache_misses, cache_noslot, cache_bytes, p4_cache_budget, heap_free, guard_mesh_draws);
        p4_log("cache audit %d checks %u stale positions %u uvs %u prelight %u vertices %u\n", p4_cache_audit, audit_checks, audit_stale[0], audit_stale[1], audit_stale[2], audit_stale_vertices);
        audit_checks = audit_stale[0] = audit_stale[1] = audit_stale[2] = audit_stale_vertices = 0;
        p4_log("lock memo %d hits %u\n", p4_lock_memo, lock_memo_hits);
        lock_memo_hits = 0;
        guard_mesh_draws = 0;
        /* Work per frame, the same in PPSSPP and on the PSP (b69 on the PSP: shade_fast 238 cycles a vertex) */
        p4_log("work ge_light %d per frame: cpu_lit %u emitted_ge_lit %u emitted_cpu_lit %u ge_lit_draws %u\n", p4_ge_light, work_shaded / 256, work_ge_emitted / 256, work_cpu_emitted / 256, work_ge_draws / 256);
        work_shaded = work_ge_emitted = work_cpu_emitted = work_ge_draws = 0;
        void p4_vu0_report(unsigned frames);
        p4_vu0_report(256);
        p4_log("textures swizzled %u bytes %u mipmapped %u mip_bytes %u mipmaps %d\n", swizzled_textures, swizzled_bytes, mip_textures, mip_bytes, p4_mipmaps);
        churn_report(256);
        if (p4_churn_top)
            churn_top_report(256);
        shadow_report(256);
        skip_visual_report(256);
        p4_log("skin ge %d draws %u batches %u bones sent %u direct %d fallbacks %u builds %u bytes %u split %d cpu_batches %u\n", p4_ge_skin, skin_ge_draws, skin_ge_batches, bones_sent, p4_bone_direct, skin_ge_fallbacks, skin_ge_builds, skin_ge_bytes, p4_skin_split, skin_cpu_batches);
        if (skin_ge_builds)
            p4_log("skin cache builds %u took %u us, longest %u us (%u vertices)\n", skin_ge_builds, skin_build_us, skin_build_max_us, skin_build_max_v);
        skin_build_us = skin_build_max_us = skin_build_max_v = 0;
        if (p4_skin_diff)
            p4_log("skin diff per 256 frames: checked %u differing %u\n", skin_diff_checked, skin_diff_count), skin_diff_checked = skin_diff_count = 0;
        p4_log("skin finite %d bones out %u per 256 frames\n", p4_skin_finite, skin_nonfinite_bones), skin_nonfinite_bones = 0;
        if (skin_rigid_batches)
            p4_log("skin rigid %d: one-bone batches built %u with %u triangles\n", p4_skin_rigid, skin_rigid_batches, skin_rigid_tris);
        skin_rigid_batches = skin_rigid_tris = 0;
        if (strip_batches || strip_failed)
            p4_log("skin strip %d: batches built as strips %u (list indices %u -> %u), failed checks %u, took %u us; check %d: %u compared, %u differ\n",
                   p4_skin_strip, strip_batches, strip_list_idx, strip_idx, strip_failed, strip_us, p4_strip_check, strip_checks, strip_check_diffs);
        strip_batches = strip_failed = strip_list_idx = strip_idx = strip_us = strip_checks = strip_check_diffs = 0;
        if (sd_tri)
            p4_log("skin strip why, indices a triangle x100: game's own strips %u (%u tri), whole mesh %u, whole mesh welded %u (%u tri)\n",
                   sd_src_tri ? sd_src_idx * 100 / sd_src_tri : 0, sd_src_tri, sd_whole_idx * 100 / sd_tri, sd_weld_idx * 100 / sd_tri, sd_tri);
        sd_src_idx = sd_src_tri = sd_whole_idx = sd_weld_idx = sd_tri = 0;
        if (mk_meshes || mk_override_hits || mk_captured)
            p4_log("mesh reshape: simplify %d%% meshes %u triangles %u -> %u in %u us; override %d geometries %u meshes %u; captured %u\n",
                   p4_skin_simplify, mk_meshes, mk_tri_in, mk_tri_out, mk_us, p4_mesh_override, mk_override_hits, mk_override_meshes, mk_captured);
        mk_meshes = mk_tri_in = mk_tri_out = mk_us = mk_override_hits = mk_override_meshes = mk_captured = 0;
        call_report();
        { extern void p4_batch_stats_report(void); p4_batch_stats_report(); }
        skin_ge_draws =skin_ge_batches = skin_ge_fallbacks = skin_ge_builds = bones_sent = skin_cpu_batches = 0;
        p4_log("guard fast %d memo %d hits %u check runs %u diffs %u world %d checks %u unsafe %u stricter %u full_cache %d hits %u builds %u\n", p4_guard_fast, p4_guard_memo, guard_memo_hits, guard_check_runs, guard_check_diffs, p4_guard_world, guard_world_checks, guard_world_unsafe, guard_world_stricter, p4_guard_full_cache, guard_full_hits, guard_full_builds);
        guard_full_hits = guard_full_builds = 0;
        p4_log("gs test %d skip %d mesh_alpha %d: draws %u second %u opaque %u nodepth %u allfail %u scans %u\n", p4_gs_test, p4_gs_test_skip, p4_gs_mesh_alpha,
               tested_draws, tested_second, tested_opaque, tested_nodepth, tested_allfail, tested_scans);
        tested_draws = tested_second = tested_opaque = tested_nodepth = tested_allfail = tested_scans = 0;
        p4_log("state cache %d batch %d per frame: mesh modes %u kept textures %u cameras %u lights %u; bound textures %u lights %u\n", p4_state_cache, p4_draw_batch,
               mesh_modes / 256, kept_textures / 256, kept_cameras / 256, kept_lights / 256, textures_bound / 256, lights_bound / 256);
        mesh_modes = kept_textures = kept_cameras = kept_lights = textures_bound = lights_bound = 0;
        cover_report(256);
        other_report(256);
        hanim_report();
        census2d_report(256);
        {
            extern unsigned p4_mc_calls[6], p4_mc_bytes;
            if (p4_mc_calls[0] | p4_mc_calls[1] | p4_mc_calls[2] | p4_mc_calls[3] | p4_mc_calls[4] | p4_mc_calls[5])
                p4_log("memcard per 256 frames: info %u fileinfo %u read %u (%u bytes) write %u create %u mkdir %u\n", p4_mc_calls[0], p4_mc_calls[1],
                       p4_mc_calls[2], p4_mc_bytes, p4_mc_calls[3], p4_mc_calls[4], p4_mc_calls[5]);
            memset(p4_mc_calls, 0, sizeof p4_mc_calls);
            p4_mc_bytes = 0;
        }
        p4_log("im3d %d draws %u vertices %u per 256 frames\n", p4_im3d, im3d_draws, im3d_vertices);
        im3d_draws = im3d_vertices = 0;
        frustum_report(256);
        replay_report(256);
        probe_report(256);
        topology_report(256);
        p4_log("pipeline %d peak vertices %d/%d indices %d/%d texture waits %u\n", p4_pipeline, pipe_peak_v, vertex_cap, pipe_peak_i, index_cap, texture_waits);
        p4_log("flip event %d: idle wakeups %u error %x; list cached %d writebacks %u\n", p4_flip_event, flip_wakeups, flip_event_error, p4_list_cached, list_writebacks);
        p4_log("frameskip %d max %d: ticks %u skipped %u debt_us %d cap %d\n", p4_frameskip, p4_frameskip_max, skip_ticks, skipped_frames, skip_debt_us, skip_cap);
        skip_ticks = skipped_frames = 0;
        p4_log("frameskip bones %d: bone updates skipped %u\n", p4_frameskip_bones, skipped_bone_updates);
        p4_log("hanim nan fix %d: frames fixed %u, nan right after update %u; bone check %d: bad bone matrices %u\n", p4_hanim_nan_fix, hanim_nan_fixed, hanim_nan_after_update, p4_bone_check, bone_bad_count);
        hanim_nan_fixed = hanim_nan_after_update = 0;
        bone_bad_count = 0;
        look_report();
        { extern void p4_anim_report(void); p4_anim_report(); }
        { extern void p4_interp_report(void); p4_interp_report(); }
        p4_log("skin guard fast %d checks %u diffs %u; skin vfpu %d unaligned %u checks %u diffs %u max_diff %d/1e6\n", p4_skin_guard_fast, skin_guard_checks, skin_guard_diffs, p4_skin_vfpu, skin_vfpu_unaligned, skin_vfpu_checks, skin_vfpu_diffs, (int)(skin_vfpu_max_diff * 1e6f));
        skin_guard_checks = skin_guard_diffs = skin_vfpu_unaligned = skin_vfpu_checks = skin_vfpu_diffs = 0;
        skin_vfpu_max_diff = 0;
        skipped_bone_updates = 0;
        list_writebacks = 0;
        texture_waits = 0;
        flip_wakeups = 0;
        pipe_peak_v = pipe_peak_i = 0;
        guard_memo_hits = guard_check_diffs = 0;
        cache_hits = cache_builds = cache_misses = cache_noslot = 0;
        guard_meshes = guard_clipped = guard_dropped = guard_spheres = guard_vertices = uv_rebased = dst_alpha_skipped = 0;
        { /* probe: what a battle action waits on */
            extern int btlSoundIsPlaybackActive(void);
            extern unsigned btlCountUnitRecords(void);
            unsigned char *battle = (unsigned char *)G32(0xb3ac - 0x10000);
            /* b127 died here in a battle: a unit pointer 0x099b0001 passed the range test, lhu at +0xec faulted */
#define PROBE_OK(q) (!((unsigned)(q) & 3) && (unsigned)(q) - 0x08800000u < 0x03800000u)
            if (battle && !PROBE_OK(battle))
                battle = 0;
            for (int list = 0; battle && list < 4; list++)
                for (unsigned char *p = *(unsigned char **)(battle + 0x198 + list * 8 + 4); PROBE_OK(p); p = *(unsigned char **)(p + 0x7c))
                    p4_log("TRACE packet list %d type %x phase %d flags %x dep %d/%08x%08x uid %08x%08x action %08x%08x update %x frames %d\n", list,
                           *(int *)(p + 0x40), p[0x45], p[0x47], p[0], *(unsigned *)(p + 0xc), *(unsigned *)(p + 8), *(unsigned *)(p + 0x5c), *(unsigned *)(p + 0x58),
                           *(unsigned *)(p + 0x64), *(unsigned *)(p + 0x60), *(unsigned *)(p + 0x6c), *(unsigned *)(p + 0x50));
            for (int list = 0; battle && list < 4; list++)
                for (unsigned char *p = *(unsigned char **)(battle + 0x198 + list * 8 + 4); PROBE_OK(p); p = *(unsigned char **)(p + 0x7c)) {
                    unsigned char *work = *(unsigned char **)(p + 0x78), *unit = PROBE_OK(work) ? *(unsigned char **)work : 0;
                    if (!PROBE_OK(unit))
                        continue;
                    const float *f = (const float *)unit;
                    p4_log("TRACE unit %x state %d move %x stflags %x pos %d %d %d dest %d %d %d rot %d %d %d %d speed %d stop %d rtarget %d %d %d route %d\n", (unsigned)unit,
                           *(unsigned short *)(work + 0x14), *(unsigned short *)(unit + 0xc8), *(unsigned *)(unit + 0xc4),
                           (int)f[1], (int)f[2], (int)f[3], (int)f[0xdc / 4], (int)f[0xe0 / 4], (int)f[0xe4 / 4],
                           (int)(f[7] * 1000), (int)(f[8] * 1000), (int)(f[9] * 1000), (int)(f[10] * 1000), (int)f[0xcc / 4], (int)f[0xe8 / 4],
                           (int)f[0xd0 / 4], (int)f[0xd4 / 4], (int)f[0xd8 / 4], *(unsigned short *)(unit + 0xec));
                }
            if (battle)
                p4_log("TRACE battle flags %x playback %d a44 %x %x %x units %d\n", *(unsigned *)battle, btlSoundIsPlaybackActive(), *(unsigned *)(battle + 0xA44), *(unsigned *)(battle + 0xA48), *(unsigned *)(battle + 0xA4C), btlCountUnitRecords());
        }
        frustum_tests = frustum_out = 0;
        if (!(frames & 1023))
            trace_atomics = 24;
        atomics_drawn = atomics_skipped = sectors_drawn = skinned_drawn = ptanks_drawn = particles_drawn = 0;
    }
    return 1;
}

/* The game's raw GS words: 1 dither, 2 ALPHA_1, 3 ATEST_1, 4 fog far, 5 mip levels. */
static unsigned sky_state[8];

void RpSkyRenderStateSet(int state, unsigned value)
{
    if (state >= 0 && state < 8)
        sky_state[state] = value;
}

unsigned RpSkyRenderStateGet(int state, unsigned *value)
{
    if (state >= 0 && state < 8 && value)
        *value = sky_state[state];
    return 1;
}

/* fpRenderStateSet / fpRenderStateGet: remembered until Im2D needs them. */
#define MAX_STATE 64
enum { RS_ZTEST = 6, RS_ZWRITE = 8 };
static int render_state[MAX_STATE];
static unsigned pixel_mask;

/* GE state cache for the 3D mesh path. b77 Heaven profile: 200 atomics a frame spent ~19 k cycles each in setup
   (projection, camera, depth, blend, alpha state sent again) and ~14.6 k in each cache hit (texture with its flush,
   CLUT load, GE light, draw with a stall-address syscall). While gu_mesh_mode is on, the setters below send a state
   only when it differs from what this mode last sent. Every other sceGu user (2D, clears, feedback captures,
   particles, present) calls gu_dirty() first, which ends the mode; mesh_state() starts it again with nothing known. */
int p4_state_cache = 1; /* 0: every state sent every time, as b77 */
int p4_draw_batch = 8;  /* mesh draws between stall-address updates; 1: every draw, as b77 */
static int gu_mesh_mode;
static struct {
    unsigned enabled, known; /* one bit per GU state */
    int alpha_known, alpha_func, alpha_ref;
    int depth_func_known, depth_func, depth_mask_known, depth_mask;
    int blend_known; unsigned blend;
    int pixel_known; unsigned pixel;
    int stencil_known; unsigned stencil_ref;
    int tex_known, tex_wrap; const void *tex_raster, *tex_pixels, *tex_palette; unsigned tex_generation;
    int light_known; unsigned light_key[13];
    int camera_known; unsigned camera_key[14], camera_far, camera_pos[3];
    int texscale_known;
} gs;
static unsigned texture_generation; /* bumped when texels or a palette change in place */
static unsigned draws_since_stall;
static int gate_poll(void)
{
    if (gate_closed && (int)(shown_seq - gate_seq) >= 0) {
        gate_closed = 0;
        gu_object_stack_depth = 0;
        if (in_frame)
            sceGeListUpdateStallAddr(ge_list_executed[0], gu_list->current);
    }
    return !gate_closed;
}
/* libpspgu's sceGuGetMemory moves the stall address past every block it hands out (2D vertices, sceGuClear's
   sprites): while the pipeline gate is closed that let the GE draw the next frame into the buffer on screen (b82:
   the screen flickered). Linked with --wrap: the same jump over the block, without the stall update. */
void *__real_sceGuGetMemory(int size);
void *__wrap_sceGuGetMemory(int size)
{
    /* cached list (b127): the real one moves the stall, so the wrapper writes back, before the caller fills the
       block; its vertices then stayed in the cache past list_flushed (b126: 2D glitches in the save menu). Without
       the stall move the next one writes the block back with the draw that reads it. */
    if (!gate_closed && !list_cached_now)
        return __real_sceGuGetMemory(size);
    size = (size + 3) & ~3;
    unsigned *at = gu_list->current;
    unsigned *next = (unsigned *)((unsigned)at + size + 8);
    at[0] = 0x10u << 24 | ((unsigned)next >> 8 & 0xf0000);
    at[1] = 0x08u << 24 | ((unsigned)next & 0xffffff);
    gu_list->current = next;
    return at + 2;
}
static void stall_flush(void)
{
    if (gate_closed && !gate_poll())
        return;
    if (draws_since_stall && !gu_object_stack_depth) {
        draws_since_stall = 0;
        sceGeListUpdateStallAddr(ge_list_executed[0], gu_list->current);
    }
}
int p4_exp_scissor = 0; /* experiment: 1 = 3D meshes drawn into a 16x16 corner (vertex work stays, fill goes) */
static int exp_scissor_on;
static void gu_dirty(void)
{
    stall_flush();
    if (exp_scissor_on) {
        sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
        exp_scissor_on = 0;
    }
    if (gu_mesh_mode && gs.alpha_known && (gs.alpha_func != GU_GREATER || gs.alpha_ref != 8))
        sceGuAlphaFunc(GU_GREATER, 8, 0xff); /* what draw_tested leaves outside mesh mode */
    if (gu_mesh_mode && (gs.known & gs.enabled & 1u << GU_FOG))
        sceGuDisable(GU_FOG); /* fog is the 3D meshes' only */
    gu_mesh_mode = 0;
}
static void gu_mesh_begin(void)
{
    if (p4_exp_scissor && !exp_scissor_on) {
        sceGuScissor(0, 0, 16, 16);
        exp_scissor_on = 1;
    }
    if (gu_mesh_mode || !p4_state_cache)
        return;
    memset(&gs, 0, sizeof gs);
    gu_mesh_mode = 1;
    mesh_modes++;
}
/* TEXSCALE/TEXOFFSET: uvs (u scale, v scale, u offset, v offset) for a packed skin batch's u16 UVs, or 0 for the
   (1, 1, 0, 0) every float-UV draw expects; gs.texscale_known means the default is what the GE holds */
static void bind_texscale(const float *uvs)
{
    if (uvs) {
        sceGuTexScale(uvs[0], uvs[1]);
        sceGuTexOffset(uvs[2], uvs[3]);
        gs.texscale_known = 0;
    } else if (!gu_mesh_mode || !gs.texscale_known) {
        sceGuTexScale(1, 1);
        sceGuTexOffset(0, 0);
        gs.texscale_known = gu_mesh_mode;
    }
}
static void st_enable(int state, int on)
{
    if (gu_mesh_mode) {
        unsigned bit = 1u << state;
        if ((gs.known & bit) && !(gs.enabled & bit) == !on)
            return;
        gs.known |= bit;
        gs.enabled = on ? gs.enabled | bit : gs.enabled & ~bit;
    }
    if (on)
        sceGuEnable(state);
    else
        sceGuDisable(state);
}
static void st_alpha_func(int func, int ref)
{
    if (gu_mesh_mode) {
        if (gs.alpha_known && gs.alpha_func == func && gs.alpha_ref == ref)
            return;
        gs.alpha_known = 1, gs.alpha_func = func, gs.alpha_ref = ref;
    }
    sceGuAlphaFunc(func, ref, 0xff);
}
static void st_depth_func(int func)
{
    if (gu_mesh_mode) {
        if (gs.depth_func_known && gs.depth_func == func)
            return;
        gs.depth_func_known = 1, gs.depth_func = func;
    }
    sceGuDepthFunc(func);
}
static void st_depth_mask(int mask)
{
    if (gu_mesh_mode) {
        if (gs.depth_mask_known && gs.depth_mask == mask)
            return;
        gs.depth_mask_known = 1, gs.depth_mask = mask;
    }
    sceGuDepthMask(mask);
}
static void st_pixel_mask(unsigned mask)
{
    if (gu_mesh_mode) {
        if (gs.pixel_known && gs.pixel == mask)
            return;
        gs.pixel_known = 1, gs.pixel = mask;
    }
    sceGuPixelMask(mask);
}
/* sceGuDrawArray without its stall-address syscall; mesh mode updates the stall every p4_draw_batch draws */
static unsigned mesh_draw_calls, mesh_draw_vertices;
/* b206: what the GE is asked to transform, per atomic in the cover table, so a p4_exp_ge_time run on the PSP can tell
   what a draw's time follows. idx: vertices fetched (the index count); uniq: distinct vertices those indices name (if
   the GE keeps no transformed vertices between indices, idx is what it transforms: unproven); wslots: weights x idx
   (GU_WEIGHTS of the draw). CALL and instance replays are read from their words. On only with the cover table. */
static int ge_work_on, work_vtype;
static unsigned work_idx, work_uniq, work_wslots, work_batches, work_wreal, work_wslotv, work_rigid, work_tris; /* skin: batches, nonzero weights, bones x vertices */
static unsigned index_span(int vtype, int count, const void *indices)
{
    unsigned m = 0;
    if ((vtype & GU_INDEX_BITS) == GU_INDEX_16BIT)
        for (int i = 0; i < count; i++)
            m = ((const unsigned short *)indices)[i] > m ? ((const unsigned short *)indices)[i] : m;
    else if ((vtype & GU_INDEX_BITS) == GU_INDEX_8BIT)
        for (int i = 0; i < count; i++)
            m = ((const unsigned char *)indices)[i] > m ? ((const unsigned char *)indices)[i] : m;
    else
        return count;
    return count ? m + 1 : 0;
}
static void work_draw(int vtype, int count, const void *indices)
{
    int weights = vtype & GU_WEIGHT_BITS ? (vtype >> 14 & 7) + 1 : 0;
    work_idx += count;
    work_wslots += count * weights;
    work_uniq += indices && (vtype & GU_INDEX_BITS) ? index_span(vtype, count, indices) : (unsigned)count;
}
static void work_words(const unsigned *w, const unsigned *end)
{
    unsigned base = 0, iaddr = 0;
    int vtype = work_vtype;
    for (; w < end; w++) {
        unsigned cmd = *w >> 24, arg = *w & 0xffffff;
        if (cmd == 0x10)
            base = arg;
        else if (cmd == 0x12)
            vtype = arg;
        else if (cmd == 0x02)
            iaddr = (base & 0xf0000) << 8 | arg;
        else if (cmd == 0x04)
            work_draw(vtype, arg & 0xffff, iaddr ? (const void *)iaddr : 0);
    }
}
static void gu_draw(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    mesh_draw_calls++;
    mesh_draw_vertices += count;
    if (vtype)
        work_vtype = vtype;
    if (ge_work_on)
        work_draw(work_vtype, count, indices);
    if (!gu_mesh_mode || p4_draw_batch <= 1) {
        sceGuDrawArray(prim, vtype, count, indices, vertices);
        return;
    }
    unsigned *c = gu_list->current;
    if (vtype)
        *c++ = 0x12u << 24 | (vtype & 0xffffff);
    if (indices) {
        *c++ = 0x10u << 24 | ((unsigned)indices >> 8 & 0xf0000);
        *c++ = 0x02u << 24 | ((unsigned)indices & 0xffffff);
    }
    if (vertices) {
        *c++ = 0x10u << 24 | ((unsigned)vertices >> 8 & 0xf0000);
        *c++ = 0x01u << 24 | ((unsigned)vertices & 0xffffff);
    }
    *c++ = 0x04u << 24 | (((unsigned)prim << 16 | count) & 0xffffff);
    gu_list->current = c;
    if (++draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
}

/* Experiment p4_exp_ge_time: the GE's own time for each piece of the frame. Before a piece the GE is run dry; while
   the CPU builds it no stall update lets the GE start (libpspgu skips them inside an object, gu_object_stack_depth);
   then the stall moves past it and the wait until the GE reaches it is that piece's GE time. Serial: slow frames. */
int p4_exp_ge_time = 0;
extern int gu_object_stack_depth;
enum { GT_ATOMIC, GT_2D, GT_PTANK, GT_CLEAR, GT_COUNT };
static unsigned ge_time_us[GT_COUNT];
/* b206: the timer's own cost: once a frame an empty piece (the marker alone) is timed, and the timed pieces counted,
   so a geometry's ge_us can be read net of pieces x empty (b201: 9f01a50, 32 vertices, ~80 pieces a frame, 2.5 ms) */
static unsigned ge_empty_us, ge_empty_n, ge_pieces, ge_cal_frame = ~0u;
/* Until the GE has executed everything up to here. A marker command (0xF9, a register nothing reads) with a
   fresh value goes last; sceGeGetCmd reads it back once the GE got there. Never spins for good: b80 hung the
   PSP polling sceGeListSync for a stalled state it never reported. 100 ms timeout stops the experiment. */
static unsigned ge_marker, ge_timeouts, ge_last_state;
static void ge_run_dry(void)
{
    unsigned value = ++ge_marker & 0xffffff;
    *gu_list->current++ = 0xF9u << 24 | value;
    sceGeListUpdateStallAddr(ge_list_executed[0], gu_list->current);
    draws_since_stall = 0;
    unsigned t0 = sceKernelGetSystemTimeLow();
    while ((sceGeGetCmd(0xF9) & 0xffffff) != value) {
        if (sceKernelGetSystemTimeLow() - t0 > 100000) {
            ge_timeouts++;
            ge_last_state = sceGeListSync(ge_list_executed[0], 1) << 24 | (sceGeGetCmd(0xF9) & 0xffffff);
            p4_exp_ge_time = 0; /* the instrument does not work here: stop it */
            break;
        }
    }
}
static int ge_time_begin(void)
{
    /* b114: switched on in pipeline mode as a battle loaded, the game hung (main thread runnable, log silent): the
       experiment holds the stall the pipeline's gate and texture waits rely on. Serial timing needs the pipeline off. */
    if (!p4_exp_ge_time || p4_pipeline || !in_frame || gu_object_stack_depth)
        return 0;
    ge_run_dry();
    if (ge_cal_frame != ge_time_frames) {
        ge_cal_frame = ge_time_frames;
        unsigned t0 = sceKernelGetSystemTimeLow();
        ge_run_dry();
        ge_empty_us += sceKernelGetSystemTimeLow() - t0, ge_empty_n++;
    }
    gu_object_stack_depth = 1;
    return 1;
}
static unsigned ge_time_end(int started, int category)
{
    if (!started)
        return 0;
    gu_object_stack_depth = 0;
    if (!in_frame)
        return 0;
    unsigned t0 = sceKernelGetSystemTimeLow();
    ge_run_dry();
    unsigned t = sceKernelGetSystemTimeLow() - t0;
    ge_time_us[category] += t;
    ge_pieces++;
    return t;
}

static void bind_depth(void)
{
    if (render_state[RS_ZTEST] || render_state[RS_ZWRITE]) {
        st_enable(GU_DEPTH_TEST, 1);
        st_depth_func(render_state[RS_ZTEST] ? GU_GEQUAL : GU_ALWAYS);
    } else {
        st_enable(GU_DEPTH_TEST, 0);
    }
    st_depth_mask(render_state[RS_ZWRITE] ? GU_FALSE : GU_TRUE);
    /* Overlay passes (depth test on, no depth write) lie on the surface they shade, from other vertices. With 16 bits
       of depth instead of the PS2's 24 they fight it in streaks; pull them toward the camera (65535 is near). */
    static int applied_bias;
    int bias = render_state[RS_ZTEST] && !render_state[RS_ZWRITE] ? p4_decal_bias : 0;
    if (bias != applied_bias) {
        sceGuDepthOffset(bias);
        sceGuDepthRange(65535, 0);
        applied_bias = bias;
    }
}

/* GS destination alpha test (DATE, ATEST_1 bits 14-15) inside camera-texture passes (b165). The PSP's alpha byte is
   the stencil: draws without DATE set it to 0xFF where they pass ("alpha bit 7 set"), DATE draws pass where bit 7
   equals DATM. Field shadows need it: the silhouette marks its pixels, the gradient bands (0x3C803) fill only those. */
int p4_rtt_date = 1;
/* b177: DATE on the screen too. Outside camera textures every draw with the stencil test off writes its fragment alpha
   into the alpha byte (= the stencil), and the PS2's 0x80 is our 0xff, so PS2 bit 7 set reads as 0xfe-0xff there.
   Only DATE draws test it; the others keep writing alpha as before. Battle arena: the rainbow (f244_001_bg_01, blend
   0x54, DATM 0) covered the floor and the hero's head; on the PS2 it shows only where the shadows lowered the alpha. */
int p4_screen_date = 1;
static int date_emulated(void)
{
    unsigned t = sky_state[3];
    if (!rtt_target && p4_screen_date && (t & 1 << 14)) {
        st_pixel_mask(pixel_mask);
        st_enable(GU_STENCIL_TEST, 1);
        gs.stencil_known = 0;
        sceGuStencilFunc(t & 1 << 15 ? GU_EQUAL : GU_NOTEQUAL, 0xfe, 0xfe);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
        return 1;
    }
    if (!rtt_target || !p4_rtt_date)
        return 0;
    st_pixel_mask(pixel_mask);
    st_enable(GU_STENCIL_TEST, 1);
    gs.stencil_known = 0; /* the stencil function below is not the cached pixel-mask one */
    if (t & 1 << 14) {
        sceGuStencilFunc(GU_EQUAL, t & 1 << 15 ? 0x80 : 0, 0x80);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
    } else {
        sceGuStencilFunc(GU_ALWAYS, 0xff, 0xff);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    }
    return 1;
}

static void bind_mask(unsigned alpha)
{
    if (date_emulated())
        return;
    st_pixel_mask(pixel_mask);
    if (p4_alpha_pixel && pixel_mask == 0x00ffffff) { /* alpha-only write: the fragment alpha, per pixel, with no stencil test */
        st_enable(GU_STENCIL_TEST, 0);
        return;
    }
    if (pixel_mask) {
        st_enable(GU_STENCIL_TEST, 1);
        if (gu_mesh_mode && gs.stencil_known && gs.stencil_ref == alpha)
            return;
        if (gu_mesh_mode)
            gs.stencil_known = 1, gs.stencil_ref = alpha;
        sceGuStencilFunc(GU_ALWAYS, alpha, 0xff);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    } else {
        st_enable(GU_STENCIL_TEST, 0);
    }
}

int func_003f5070(int state, int value)
{
    if (state >= 0 && state < MAX_STATE)
        render_state[state] = value;
    return 1;
}

int func_003f5d90(int state, int *value)
{
    *value = state >= 0 && state < MAX_STATE ? render_state[state] : 0;
    return 1;
}

/* Pixels stay in CPU memory until a draw needs them: the PS2 raster formats are byte-for-byte the GE's, RGBA and low nibble first. */

/* rwSTANDARDRASTERLOCK: (&pixels, raster, level << 8 | mode) */
int func_003fc5f0(unsigned char **pixels, Raster *r, int how)
{
    pipe_texture_wait();
    texture_generation++;
    unswizzle_raster(r);
    r->cpPixels = r->originalPixels;
    *pixels = r->cpPixels;
    return r->cpPixels != 0;
}

/* rwSTANDARDRASTERUNLOCK */
int func_003fd230(void *out, Raster *r)
{
    texture_generation++;
    if (r->depth == 32) { /* PS2 alpha tops out at 0x80 */
        unsigned char *p = r->originalPixels + 3;
        for (int i = r->width * r->height; i > 0; i--, p += 4)
            *p = *p >= 0x80 ? 0xff : *p * 2;
    }
    sceKernelDcacheWritebackRange(r->originalPixels, r->stride * r->height);
    r->cpPixels = 0;
    return 1;
}

/* rwSTANDARDRASTERLOCKPALETTE, UNLOCKPALETTE */
int func_003fd460(unsigned char **palette, Raster *r, int how)
{
    pipe_texture_wait();
    texture_generation++;
    *palette = r->palette;
    return r->palette != 0;
}

int func_003fd520(void *out, Raster *r)
{
    texture_generation++;
    unsigned *entry = (unsigned *)r->palette;
    int count = 1 << r->depth;
    if (count == 256) /* PS2 CSM1 order: the middle two groups of every 32 are swapped */
        for (int i = 8; i < 256; i += 32)
            for (int j = 0; j < 8; j++) {
                unsigned t = entry[i + j];
                entry[i + j] = entry[i + 8 + j];
                entry[i + 8 + j] = t;
            }
    for (int i = 0; i < count; i++) {
        unsigned a = entry[i] >> 24;
        entry[i] = (entry[i] & 0xffffff) | (a >= 0x80 ? 0xffu : a * 2) << 24;
    }
    sceKernelDcacheWritebackRange(r->palette, 4 << r->depth);
    SwizzleHead *head = (SwizzleHead *)swizzle_head(r->originalPixels);
    if (head)
        head->alpha_min = raster_alpha_min(r, head->used, &head->alpha_max);
    return 1;
}

/* Native texture stream. Each level and the palette start with GIF packets; TRXREG says how wide the upload was. */
typedef struct {
    int width, height, depth;
    unsigned short format, version;
    unsigned tex0[2], tex1[2], miptbp1[2], miptbp2[2];
    unsigned pixelSize, paletteSize, totalSize, skyMipmapVal;
} RasterInfo;

enum { GIF_HEADER = 0x50, TRXREG_RRW = 0x20, CHUNK_STRUCT = 1, CHUNK_VERSION_MIN = 0x35000, CHUNK_VERSION_MAX = 0x37002 };

int p4r_003df050(void *stream, int type, unsigned *length, unsigned *version);
int p4r_003df360(void *stream, void *data, unsigned length);
unsigned p4r_003e2910(void *stream, void *data, unsigned length);
void *p4r_003ec590(int width, int height, int depth, int flags);
int p4r_003ec330(void *raster);

static int find_struct(void *stream, unsigned *length)
{
    unsigned version;
    return p4r_003df050(stream, CHUNK_STRUCT, length, &version) && version >= CHUNK_VERSION_MIN && version <= CHUNK_VERSION_MAX;
}

static void copy_level(Raster *r, const unsigned char *blob)
{
    const unsigned char *in = blob + GIF_HEADER;
    int rect_w = *(const int *)(blob + TRXREG_RRW);
    if (r->depth == 8) {
        for (int y = 0; y < r->height; y++)
            for (int x = 0; x < r->width; x++)
                r->originalPixels[y * r->stride + x] = in[swizzle8(x, y, rect_w)];
    } else if (r->depth == 4) {
        memset(r->originalPixels, 0, r->stride * r->height);
        for (int y = 0; y < r->height; y++)
            for (int x = 0; x < r->width; x++) {
                int from = swizzle4(x, y, rect_w);
                unsigned pixel = in[from >> 1] >> ((from & 1) * 4) & 15;
                r->originalPixels[y * r->stride + (x >> 1)] |= pixel << ((x & 1) * 4);
            }
    } else {
        memcpy(r->originalPixels, in, r->stride * r->height);
    }
}

/* _rwSkyNativeTextureReadData's raster half. ponytail: only the top mip level is kept, GE mipmaps can come from the same blob. */
void *func_003fd940(void *stream)
{
    RasterInfo info;
    unsigned length;
    if (!find_struct(stream, &length))
        return 0;
    memset(&info, 0, sizeof info);
    if (!p4r_003df360(stream, &info, length))
        return 0;
    int levels = G32(0xabf8 - 0x10000);
    G32(0xabf8 - 0x10000) = (info.tex1[1] >> 2) + 1;
    G32(0xb8ac - 0x10000) = info.version < 2;
    Raster *r = p4r_003ec590(info.width, info.height, info.depth, info.format);
    G32(0xabf8 - 0x10000) = levels;
    G32(0xb8ac - 0x10000) = 0;
    if (!r)
        return 0;
    unsigned char *blob = 0;
    if (find_struct(stream, &length) && (blob = memalign(16, length)) && p4r_003e2910(stream, blob, length) == length) {
        if (info.version < 2) { /* one texture in the game: no GIF packets */
            p4_log("TRACE old texture %dx%dx%d stays blank\n", info.width, info.height, info.depth);
            memset(r->originalPixels, 0, r->stride * r->height);
        } else {
            copy_level(r, blob);
            if (r->palette)
                memcpy(r->palette, blob + info.pixelSize + GIF_HEADER, 4 << r->depth);
        }
        func_003fd230(0, r);
        if (r->palette)
            func_003fd520(0, r);
        swizzle_raster(r);
        free(blob);
        return r;
    }
    free(blob);
    p4r_003ec330(r);
    return 0;
}

void tmxDmaCopy(unsigned destination, unsigned source, unsigned bytes)
{
    pipe_texture_wait();
    memcpy((void *)destination, (void *)source, bytes);
}

/* Immediate 2D. Vertices are the game's 64-byte RwSky2DVertex in PS2 screen space, 640 by 448. */
typedef struct { float x, y, z, camZ, u, v, recipZ, pad, r, g, b, a, nx, ny, nz, pad2; } Vertex2D;
typedef struct { float u, v; unsigned colour; float x, y, z; } GuVertex;
#include "feedback.h"

/* PSPLINK A/B control: keep the original pass available for image comparisons. */
int p4_feedback_sprites = 1;

static int pow2_at_least(int n)
{
    int size = 1;
    while (size < n)
        size <<= 1;
    return size;
}

static float texture_w, texture_h;

static void capture_feedback(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE,
                   (void *)((unsigned)draw_buffer | 0x04000000), 0, 0, 512, feedback_pixels);
    sceGuTexSync();
    /* GS region-clamp stops at 639/447; extend those edge pixels to the GE texture's power-of-two boundary. */
    for (int x = SCREEN_W; x < 512;) {
        int n = x == SCREEN_W ? 1 : x - SCREEN_W;
        if (n > 512 - x) n = 512 - x;
        sceGuCopyImage(GU_PSM_8888, x == SCREEN_W ? SCREEN_W - 1 : SCREEN_W, 0, n, SCREEN_H,
                       512, feedback_pixels, x, 0, 512, feedback_pixels);
        sceGuTexSync();
        x += n;
    }
    for (int y = SCREEN_H; y < 512;) {
        int n = y == SCREEN_H ? 1 : y - SCREEN_H;
        if (n > 512 - y) n = 512 - y;
        sceGuCopyImage(GU_PSM_8888, 0, y == SCREEN_H ? SCREEN_H - 1 : SCREEN_H, 512, n,
                       512, feedback_pixels, 0, y, 512, feedback_pixels);
        sceGuTexSync();
        y += n;
    }
    sceGuTexFlush();
}

/* Half-size screen copy in free VRAM past the depth buffer (0x154000..0x194000). The dream's ~90 fog puffs each sample
   the screen copy: from a 1 MB RAM texture that cost ~50 ms of GE per frame. The puffs are soft, a quarter of the texels
   from VRAM keeps the look. */
enum { HALF_W = SCREEN_W / 2, HALF_H = SCREEN_H / 2, HALF_VRAM = 2 * STRIDE * SCREEN_H * 4 + STRIDE * SCREEN_H * 2 };
/* 0: full-size copy in RAM, as b60; 1: full-size copy in VRAM (same pixels); 2: half size in VRAM */
int p4_feedback_half = 1;

static void capture_feedback_vram(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    void *dst = (void *)(0x04000000 + HALF_VRAM), *src = (void *)((unsigned)draw_buffer | 0x04000000);
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, STRIDE, src, 0, 0, STRIDE, dst);
    sceGuTexSync();
    /* one column and one row past the picture repeat its edge, for bilinear reads at the border */
    sceGuCopyImage(GU_PSM_8888, SCREEN_W - 1, 0, 1, SCREEN_H, STRIDE, dst, SCREEN_W, 0, STRIDE, dst);
    sceGuTexSync();
    sceGuCopyImage(GU_PSM_8888, 0, SCREEN_H - 1, SCREEN_W + 1, 1, STRIDE, dst, 0, SCREEN_H, STRIDE, dst);
    sceGuTexSync();
    sceGuTexFlush();
}
static int feedback_half_now;

static void capture_feedback_half(void)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    static const int states[] = { GU_DEPTH_TEST, GU_BLEND, GU_ALPHA_TEST, GU_STENCIL_TEST, GU_FOG, GU_CULL_FACE, GU_LIGHTING };
    int was[sizeof states / sizeof *states];
    for (unsigned i = 0; i < sizeof states / sizeof *states; i++) {
        was[i] = sceGuGetStatus(states[i]);
        sceGuDisable(states[i]);
    }
    sceGuDrawBufferList(GU_PSM_8888, (void *)HALF_VRAM, 256);
    sceGuScissor(0, 0, 256, 256);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 512, STRIDE, (void *)((unsigned)draw_buffer | 0x04000000));
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    /* 32-pixel strips for the texture cache, plus one column and one row repeating the edge for bilinear reads */
    typedef struct { float u, v; float x, y, z; } V;
    int strips = HALF_W / 32 + 2;
    V *v = sceGuGetMemory(strips * 2 * sizeof(V));
    int n = 0;
    for (int x = 0; x < HALF_W; x += 32, n += 2) {
        v[n] = (V){ x * 2.0f, 0, x, 0, 0 };
        v[n + 1] = (V){ (x + 32) * 2.0f, SCREEN_H, x + 32, HALF_H, 0 };
    }
    v[n] = (V){ SCREEN_W - 1.0f, 0, HALF_W, 0, 0 };
    v[n + 1] = (V){ SCREEN_W - 1.0f, SCREEN_H, 256, HALF_H, 0 };
    v[n + 2] = (V){ 0, SCREEN_H - 1.0f, 0, HALF_H, 0 };
    v[n + 3] = (V){ SCREEN_W, SCREEN_H - 1.0f, 256, HALF_H + 8, 0 };
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n + 4, 0, v);
    sceGuDrawBufferList(GU_PSM_8888, draw_buffer, STRIDE);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    for (unsigned i = 0; i < sizeof states / sizeof *states; i++)
        if (was[i])
            sceGuEnable(states[i]);
    sceGuTexFlush();
}

/* The bound texture's smallest alpha for draw_tested: 255 without a texture, 0 when not known. */
static unsigned bound_alpha_min, bound_alpha_max = 255;
static void bind_raster_body(Raster *r, int wrap);
static void bind_raster(Raster *r, int wrap)
{
    unsigned kept = kept_textures;
    prof_begin(PROF_TBIND);
    bind_raster_body(r, wrap);
    prof_end(PROF_TBIND);
    textures_bound += kept_textures == kept;
}
static void bind_raster_body(Raster *r, int wrap)
{
    if (r != &display_raster && gu_mesh_mode) {
        const void *pixels = r ? r->originalPixels : 0, *palette = r ? r->palette : 0;
        if (gs.tex_known && gs.tex_raster == r && gs.tex_wrap == wrap && gs.tex_pixels == pixels && gs.tex_palette == palette &&
            gs.tex_generation == texture_generation) {
            kept_textures++;
            return;
        } /* bound_alpha_min, texture_w and texture_h are still this raster's */
        gs.tex_known = 1, gs.tex_raster = r, gs.tex_wrap = wrap, gs.tex_pixels = pixels, gs.tex_palette = palette;
        gs.tex_generation = texture_generation;
    }
    bound_alpha_min = 0, bound_alpha_max = 255;
    if (r == &display_raster) {
        gs.tex_known = 0;
        /* Sample this frame's scene before filtering; the front buffer would accumulate the previous frame's blur. */
        if (!feedback_captured) {
            feedback_half_now = p4_feedback_half;
            if (feedback_half_now == 2)
                capture_feedback_half();
            else if (feedback_half_now == 1)
                capture_feedback_vram();
            else
                capture_feedback();
            feedback_captured = 1;
        }
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexMode(GU_PSM_8888, 0, 0, 0);
        if (feedback_half_now == 1) {
            sceGuTexImage(0, 512, 512, STRIDE, (void *)(0x04000000 + HALF_VRAM));
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            sceGuTexWrap(wrap, wrap);
            texture_w = SCREEN_W;
            texture_h = SCREEN_H;
            return;
        }
        if (feedback_half_now == 2) {
            sceGuTexImage(0, 256, 256, 256, (void *)(0x04000000 + HALF_VRAM));
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            sceGuTexWrap(wrap, wrap);
            texture_w = HALF_W;
            texture_h = HALF_H;
            return;
        }
        sceGuTexImage(0, 512, 512, 512, feedback_pixels);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGB);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexWrap(wrap, wrap);
        texture_w = SCREEN_W;
        texture_h = SCREEN_H;
        return;
    }
    const RttSlot *drawn = rtt_find(r);
    if (drawn) { /* a camera texture: sample its VRAM rectangle, power-of-two sized so 0..1 UVs cover it */
        st_enable(GU_TEXTURE_2D, 1);
        sceGuTexMode(drawn->psm, 0, 0, 0);
        sceGuTexImage(0, drawn->w, drawn->h, drawn->w, (void *)(0x04000000 + drawn->offset));
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(GU_LINEAR, GU_LINEAR);
        sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
        sceGuTexWrap(wrap, wrap);
        texture_w = drawn->w;
        texture_h = drawn->h;
        return;
    }
    if (!r || !r->originalPixels) {
        st_enable(GU_TEXTURE_2D, 0);
        texture_w = texture_h = 1;
        bound_alpha_min = bound_alpha_max = 255;
        return;
    }
    int psm = r->depth == 32 ? GU_PSM_8888 : r->depth == 16 ? GU_PSM_5551 : r->depth == 8 ? GU_PSM_T8 : GU_PSM_T4;
    st_enable(GU_TEXTURE_2D, 1);
    if (r->depth <= 8) {
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(1 << (r->depth - 1 ? r->depth - 3 : 0), r->palette);
    }
    const SwizzleHead *swizzled = swizzle_head(r->originalPixels);
    bound_alpha_min = swizzled ? swizzled->alpha_min : 0;
    bound_alpha_max = swizzled ? swizzled->alpha_max : 255;
    int levels = swizzled && p4_mipmaps ? swizzled->levels : 1;
    sceGuTexMode(psm, levels - 1, 0, swizzled != 0);
    sceGuTexImage(0, pow2_at_least(r->width), pow2_at_least(r->height), swizzled ? swizzled->wbytes * 8 / r->depth : r->width, r->originalPixels);
    const unsigned char *level = r->originalPixels + (swizzled ? swizzled->wbytes * swizzled->rows : 0);
    for (int l = 1; l < levels; l++) { /* swizzled and power of two: see mip_levels */
        int wbytes, rows, size = mip_level_size(r, l, &wbytes, &rows);
        sceGuTexImage(l, r->width >> l, r->height >> l, wbytes * 8 / r->depth, level);
        level += size;
    }
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(levels > 1 ? GU_LINEAR_MIPMAP_NEAREST : GU_LINEAR, GU_LINEAR);
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0);
    sceGuTexWrap(wrap, wrap);
    texture_w = r->width;
    texture_h = r->height;
}

static void bind_texture(void) { bind_raster((Raster *)render_state[1], GU_CLAMP); }

/* ALPHA_1 is (A - B) * C + D with 0 source, 1 destination, 2 zero. */
enum { BLEND_NORMAL = 0x44, BLEND_ADD = 0x48, BLEND_SUBTRACT = 0x42, BLEND_REVERSE = 0x06 };

/* Any ALPHA_1 (b165; before, seven known values and the rest drawn as normal alpha, so the field Shadow's dark
   "black hole" drew as an opaque yellow cylinder). A, B, D: 0 source colour, 1 destination, 2 zero; C: 0 source
   alpha, 1 destination alpha, 2 FIX (taken as 1.0: the word kept here has no FIX byte). The result is
   Cs * ks + Cd * kd, each k being (a - b) * F + d, mapped onto the GE's ADD / SUBTRACT / REVERSE_SUBTRACT with
   F, 1 - F, 0 or 1 as factors. Only 1 + F has no GE factor: drawn as 1. */
enum { K_ZERO, K_ONE, K_F, K_ONE_MINUS_F };
int p4_blend_fix = 0; /* ALPHA_1's FIX, GS scale (0x80 = 1.0); 0 as the game's 32-bit state words leave it */
static unsigned blend_unusual[8], blend_unusual_n; /* modes outside the old seven, for the log */
void p4_blend_report(void)
{
    static unsigned reported;
    if (blend_unusual_n > reported) {
        for (unsigned i = reported; i < blend_unusual_n; i++)
            p4_log("blend new ALPHA_1 mode %02x\n", blend_unusual[i]);
        reported = blend_unusual_n;
    }
}
static void blend_factor(int a, int b, int d, int *kind, int *negative)
{
    int f = a - b;
    *negative = 0;
    if (!f)
        *kind = d ? K_ONE : K_ZERO;
    else if (f > 0)
        *kind = d ? K_ONE : K_F; /* 1 + F: no GE factor */
    else if (d)
        *kind = K_ONE_MINUS_F;
    else
        *kind = K_F, *negative = 1;
}
static void set_blend(unsigned alpha)
{
    unsigned m = alpha & 0xff, A = m & 3, B = m >> 2 & 3, C = m >> 4 & 3, D = m >> 6 & 3;
    if (m != 0x44 && m != 0x48 && m != 0x42 && m != 0x06 && m != 0x52 && m != 0x54 && m != 0x58) {
        int seen = 0;
        for (unsigned i = 0; i < blend_unusual_n; i++)
            seen |= blend_unusual[i] == m;
        if (!seen && blend_unusual_n < 8)
            blend_unusual[blend_unusual_n++] = m;
    }
    int ks, kd, ns, nd;
    blend_factor(A == 0, B == 0, D == 0, &ks, &ns);
    blend_factor(A == 1, B == 1, D == 1, &kd, &nd);
    int f = C == 1 ? GU_DST_ALPHA : GU_SRC_ALPHA, omf = C == 1 ? GU_ONE_MINUS_DST_ALPHA : GU_ONE_MINUS_SRC_ALPHA;
    /* C = FIX: a constant, GE GU_FIX colours. The game passes ALPHA_1's low word only (0x64 in mdlDrawSkyPass), so
       FIX, bits 32-39 of the register, is 0 on the PS2: the arena floor's sky pass changes no colour. */
    unsigned fix = p4_blend_fix * 2 > 255 ? 255 : p4_blend_fix * 2, fix_rgb = fix * 0x010101u, omfix_rgb = (255 - fix) * 0x010101u;
    if (C == 2)
        f = omf = GU_FIX;
    int src = ks == K_F ? f : ks == K_ONE_MINUS_F ? omf : GU_FIX, dst = kd == K_F ? f : kd == K_ONE_MINUS_F ? omf : GU_FIX;
    unsigned src_fix = ks == K_ONE ? 0xffffff : C == 2 && ks == K_F ? fix_rgb : C == 2 && ks == K_ONE_MINUS_F ? omfix_rgb : 0;
    unsigned dst_fix = kd == K_ONE ? 0xffffff : C == 2 && kd == K_F ? fix_rgb : C == 2 && kd == K_ONE_MINUS_F ? omfix_rgb : 0;
    int op = ns && !nd ? GU_REVERSE_SUBTRACT : nd && !ns ? GU_SUBTRACT : GU_ADD;
    if (ns && nd) /* both negative: the GS clamps to 0 */
        src = dst = GU_FIX, src_fix = dst_fix = 0;
    sceGuBlendFunc(op, src, dst, src_fix, dst_fix);
}

static void bind_blend(void)
{
    if (!render_state[12] && !sky_state[2]) {
        sceGuDisable(GU_BLEND);
        return;
    }
    sceGuEnable(GU_BLEND);
    set_blend(sky_state[2] ? sky_state[2] : BLEND_NORMAL);
}

static void begin_frame(void)
{
    if (!in_frame) {
        gu_dirty(); /* a new list */
        in_frame = 1;
        sceGuStart(GU_DIRECT, list);
        list_to_cached();
        sceGuDrawBufferList(GU_PSM_8888, draw_buffer, STRIDE); /* buffers alternate here, not through sceGuSwapBuffers */
        if (rtt_restore) {
            rtt_restore = 0;
            rtt_bind_target(0);
        }
    }
}

static unsigned byte_colour(float c)
{
    return c <= 0 ? 0 : c >= 255 ? 255 : (unsigned)c;
}

static void put_vertex(GuVertex *out, const Vertex2D *in)
{
    out->u = in->u * texture_w;
    out->v = in->v * texture_h;
    out->colour = byte_colour(in->r) | byte_colour(in->g) << 8 | byte_colour(in->b) << 16 | byte_colour(in->a) << 24;
    out->x = in->x * target_sx;
    out->y = in->y * target_sy;
    out->z = in->z <= 0 ? 0 : in->z >= 65535 ? 65535 : in->z;
}

static const int gu_prim[7] = { GU_POINTS, GU_LINES, GU_LINE_STRIP, GU_TRIANGLES, GU_TRIANGLE_STRIP, GU_TRIANGLE_FAN, GU_POINTS };

static int draw_body(int prim, const Vertex2D *vertices, const unsigned short *indices, int count);
static int draw(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    if (SKIPPING)
        return 1;
    if (p4_probe_flags & 2)
        return 1;
    prof_begin(PROF_2D);
    if (gate_closed)
        gate_poll();
    int timed = ge_time_begin();
    rtt_2d += rtt_target != 0;
    int r = draw_body(prim, vertices, indices, count);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
    return r;
}

/* b146 census of 2D draws (p4_probe_2d): per texture x blend x pixel mask, draws and the screen area of their bounding
   boxes (a proxy for fill). The title's logo phase: ~36 ms of GE in 2D a frame. */
int p4_probe_2d = 0;
int p4_2d_nan_drop = 1;
float p4_panel_scale = 0.0f; /* b225: the battle panel frame's scale where retail leaves it unset (census FIXES); 0: nothing, as on the PS2 */
static unsigned nan_2d_dropped;
unsigned p4_2d_nan_report(void) { unsigned n = nan_2d_dropped; nan_2d_dropped = 0; return n; }
typedef struct { const void *raster; unsigned blend, mask, prim, calls, w, h; float area; } Census2D;
static Census2D census2d[64]; /* census2d_shown (declared above): single draws logged in this window */
static void census2d_note(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < count; i++) {
        const Vertex2D *v = &vertices[indices ? indices[i] : i];
        if (v->x < x0) x0 = v->x;
        if (v->x > x1) x1 = v->x;
        if (v->y < y0) y0 = v->y;
        if (v->y > y1) y1 = v->y;
    }
    const void *raster = (const void *)render_state[1];
    unsigned blend = sky_state[2] & 0xff, mask = pixel_mask;
    /* b165: single draws for the empty minimap. 2: draws smaller than 1/16 of the screen; 3: every draw. */
    /* b220: 4 = textured draws only, with their first vertices (the battle's attack cursor: stripes on the PSP) */
    if (p4_probe_2d == 4 && census2d_shown < 48 && raster != (const void *)&display_raster &&
        (x1 - x0) * (y1 - y0) >= (float)p4_probe_keep_every) { /* p4_probe_keep_every: the smallest box area (PS2 pixels) logged */
        census2d_shown++;
        const Raster *tr = raster;
        p4_log("census2d tex prim %d n %d box %d,%d-%d,%d raster %p %dx%d blend %02x mask %08x test %08x z %d ztest %d\n", prim, count,
               (int)x0, (int)y0, (int)x1, (int)y1, raster, tr ? tr->width : 0, tr ? tr->height : 0, blend, mask, sky_state[3], (int)vertices[0].z,
               render_state[RS_ZTEST]);
        for (int i = 0; i < count && i < 16; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            p4_log("census2d   v%d xy %d %d (x10) uv %d %d (x1000) rgba %d %d %d %d\n", indices ? indices[i] : i, (int)(v->x * 10),
                   (int)(v->y * 10), (int)(v->u * 1000), (int)(v->v * 1000), (int)v->r, (int)v->g, (int)v->b, (int)v->a);
        }
    }
    if (p4_probe_2d >= 2 && p4_probe_2d <= 3 && census2d_shown < 64 && (p4_probe_2d == 3 || (x1 - x0) * (y1 - y0) < 640 * 448 / 16)) {
        const Vertex2D *v = &vertices[indices ? indices[0] : 0];
        const Raster *tr = raster && raster != (const void *)&display_raster ? raster : 0;
        census2d_shown++;
        p4_log("census2d draw prim %d n %d box %d,%d-%d,%d raster %p %dx%d uv %d,%d blend %02x mask %08x rgba %d %d %d %d z %d ztest %d zwrite %d at %08x\n",
               prim, count, (int)x0, (int)y0, (int)x1, (int)y1, raster, tr ? tr->width : 0, tr ? tr->height : 0,
               (int)(v->u * 1000), (int)(v->v * 1000), blend, mask, (int)v->r, (int)v->g, (int)v->b, (int)v->a, (int)v->z,
               render_state[RS_ZTEST], render_state[RS_ZWRITE], sky_state[3]);
    }
    for (int i = 0; i < 64; i++) {
        Census2D *e = &census2d[i];
        if (e->calls && (e->raster != raster || e->blend != blend || e->mask != mask))
            continue;
        if (!e->calls) {
            e->raster = raster, e->blend = blend, e->mask = mask, e->prim = prim;
            const Raster *r = raster && raster != (const void *)&display_raster ? raster : 0;
            e->w = r ? r->width : raster ? 1 : 0, e->h = r ? r->height : 0;
        }
        e->calls++;
        e->area += (x1 - x0) * (y1 - y0) / (640.0f * 448.0f);
        return;
    }
}
static void census2d_report(unsigned census2d_frames)
{
    if (!p4_probe_2d || !census2d_frames)
        return;
    for (int k = 0; k < 12; k++) {
        Census2D *best = 0;
        for (int i = 0; i < 64; i++)
            if (census2d[i].calls && (!best || census2d[i].area > best->area))
                best = &census2d[i];
        if (!best)
            break;
        p4_log("census2d %2d raster %p %s %ux%u blend %02x mask %08x prim %u: draws %u screens %d.%02d per frame\n", k, best->raster,
               best->raster == (const void *)&display_raster ? "SCREEN" : "", best->w, best->h, best->blend, best->mask, best->prim,
               best->calls / census2d_frames, (int)(best->area / census2d_frames), (int)(best->area * 100 / census2d_frames) % 100);
        best->calls = 0;
    }
    memset(census2d, 0, sizeof census2d);
    census2d_shown = 0;
}

/* GS alpha test with AFAIL = FB_ONLY on a draw that writes depth: the only case where ignoring ATEST changes depth. */
int p4_gs_alpha_test = 1; /* 0: ATEST ignored in 2D, as before b165 */
static unsigned gs_alpha_splits;
unsigned p4_gs_alpha_splits(void) { unsigned n = gs_alpha_splits; gs_alpha_splits = 0; return n; }
static int gs_alpha_fb_only(void)
{
    unsigned at = sky_state[3];
    return p4_gs_alpha_test && (at & 1) && (at >> 1 & 7) != 1 && (at >> 12 & 3) == 1 && render_state[RS_ZWRITE] &&
           !(p4_alpha_write_test && pixel_mask == 0x00ffffff);
}

static int draw_body(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    if (p4_probe_2d && count > 0)
        census2d_note(prim, vertices, indices, count);
    /* b223: 2D primitives with a non-finite position. The battle command panel (btlPanelDrawBackdropGrid, FUN_0020B3A0)
       passes a scale it never set once the panel has finished growing (retail: whatever $f20 held); here that is NaN,
       and the backdrop disc and its arcs went to the GE with NaN corners: the stripes across the attack cursor, PSP
       only (PPSSPP drops such triangles). p4_2d_nan_drop 1 leaves a draw out when any corner is not finite. */
    if (p4_2d_nan_drop) {
        for (int i = 0; i < count; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            if (!(v->x == v->x && v->y == v->y && v->x < 1e6f && v->x > -1e6f && v->y < 1e6f && v->y > -1e6f)) {
                static unsigned nan_logged;
                if (p4_probe_2d == 5 && nan_logged < 24) { /* b224: what these draws are, their corners as float bits */
                    nan_logged++;
                    const Vertex2D *f = &vertices[indices ? indices[0] : 0];
                    p4_log("nan2d prim %d n %d raster %p blend %02x test %08x rgba %d %d %d %d z %d\n", prim, count, (void *)render_state[1],
                           sky_state[2] & 0xff, sky_state[3], (int)f->r, (int)f->g, (int)f->b, (int)f->a, (int)f->z);
                    for (int k = 0; k < count && k < 48; k += 4) {
                        char line[160];
                        int at = 0;
                        for (int j = k; j < count && j < k + 4; j++) {
                            const Vertex2D *w = &vertices[indices ? indices[j] : j];
                            at += snprintf(line + at, sizeof line - at, " %08x,%08x", *(const unsigned *)&w->x, *(const unsigned *)&w->y);
                        }
                        p4_log("nan2d   %d:%s\n", k, line);
                    }
                }
                nan_2d_dropped++;
                return 1;
            }
        }
    }
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    if (count <= 0 || prim < 1 || prim > 6)
        return 1;
    begin_frame();
    bind_depth();
    bind_mask(byte_colour(vertices[indices ? indices[0] : 0].a));
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_ALPHA_TEST);
    bind_texture();
    bind_blend();
    /* An alpha-only write (RGB masked) of a texture: the PS2 stores each pixel's alpha, the PSP's stencil takes one
       value per draw. Keep that value off the pixels the texture leaves transparent, so the title's light blobs
       (added by destination alpha) stay off the letterbox bars instead of covering the whole quad. */
    if (p4_alpha_write_test && !p4_alpha_pixel && pixel_mask == 0x00ffffff && render_state[1] && (Raster *)render_state[1] != &display_raster) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0x40, 0xff);
    }
    /* The PS2 blends these by the alpha the pass before wrote into the frame buffer. On the PSP that byte is the
       stencil, which ordinary draws leave alone, so the dream's screen-copy puffs showed as hard squares. Passes under
       half the screen are skipped; full-screen filters still draw. */
    unsigned blend = sky_state[2] & 0xff;
    int dst_alpha_pass = (blend == 0x52 || blend == 0x54 || blend == 0x58) && (Raster *)render_state[1] == &display_raster;
    /* the alpha-only writes that feed them (RGB masked, a textured puff): the stencil takes one value per draw, which
       is the square; without the pass that reads it they only cost fill */
    int alpha_writer = pixel_mask == 0x00ffffff && render_state[1] && (Raster *)render_state[1] != &display_raster;
    if (!p4_dst_alpha_passes && !p4_alpha_pixel && (dst_alpha_pass || alpha_writer)) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i < count; i++) {
            const Vertex2D *v = &vertices[indices ? indices[i] : i];
            if (v->x < x0) x0 = v->x;
            if (v->x > x1) x1 = v->x;
            if (v->y < y0) y0 = v->y;
            if (v->y > y1) y1 = v->y;
        }
        if ((x1 - x0) * (y1 - y0) < 640 * 448 / 2) { /* the battle's puffs cover 0.27 of the screen, filters all of it */
            dst_alpha_skipped++;
            return 1;
        }
    }
    GuVertex *out = sceGuGetMemory(count * sizeof *out);
    for (int i = 0; i < count; i++)
        put_vertex(&out[i], &vertices[indices ? indices[i] : i]);
    P4FeedbackRect rect;
    if (p4_feedback_sprites && (Raster *)render_state[1] == &display_raster &&
        gu_prim[prim] == GU_TRIANGLES && p4_feedback_rect(out, count, &rect)) {
        /* Small vertical sprites keep the linear framebuffer texture in the
           GE cache; retain the existing capture, filter, blend and mask. */
        GuVertex *strips = sceGuGetMemory(P4_FEEDBACK_MAX_VERTICES * sizeof *strips);
        int n = p4_feedback_emit(strips, &rect);
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n, 0, strips);
    } else if (gs_alpha_fb_only()) {
        /* GS ATEST with AFAIL = FB_ONLY (b165): pixels that fail write colour but no depth. The minimap's frame
           (ATEST_1 0x717FB: alpha >= 0x7F) left its see-through middle free of depth, so the map tiles behind it
           (z 65535 - 60000) drew; written whole it hid them. Pass 1: passing pixels, depth as set; pass 2: the rest,
           colour only. */
        unsigned at = sky_state[3];
        static const unsigned char gu_atst[8] = { GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL };
        static const unsigned char gu_fail[8] = { GU_ALWAYS, GU_NEVER, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_EQUAL };
        int ref = (at >> 4 & 0xff) * 2; /* the GS's alpha 0x80 is 1.0, the GE's 0xff */
        if (ref > 255)
            ref = 255;
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(gu_atst[at >> 1 & 7], ref, 0xff);
        sceGuDrawArray(gu_prim[prim], GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, out);
        sceGuAlphaFunc(gu_fail[at >> 1 & 7], ref, 0xff);
        sceGuDepthMask(GU_TRUE);
        sceGuDrawArray(gu_prim[prim], GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, out);
        sceGuDepthMask(GU_FALSE);
        sceGuDisable(GU_ALPHA_TEST);
        gs_alpha_splits++;
    } else {
        sceGuDrawArray(gu_prim[prim], GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, 0, out);
    }
    return 1;
}

static int im2d_line(Vertex2D *v, int n, int a, int b)
{
    unsigned short pair[2] = { a, b };
    return draw(1, v, pair, 2);
}

static int im2d_triangle(Vertex2D *v, int n, int a, int b, int c)
{
    unsigned short three[3] = { a, b, c };
    return draw(3, v, three, 3);
}

static int im2d_primitive(int prim, Vertex2D *v, int n) { return draw(prim, v, 0, n); }

static int im2d_indexed(int prim, Vertex2D *v, int n, unsigned short *indices, int count)
{
    return draw(prim, v, indices, count);
}

/* The retail device table points at these names. */
int SkyIm2DRenderLine(Vertex2D *v, int n, int a, int b) { return im2d_line(v, n, a, b); }
int SkyIm2DRenderTriangle(Vertex2D *v, int n, int a, int b, int c) { return im2d_triangle(v, n, a, b, c); }
int SkyIm2DRenderPrimitive(int prim, Vertex2D *v, int n) { return im2d_primitive(prim, v, n); }
int SkyIm2DRenderIndexedPrimitive(int prim, Vertex2D *v, int n, unsigned short *indices, int count) { return im2d_indexed(prim, v, n, indices, count); }

/* Atomics, without RenderWare's PS2 pipelines: the game's geometry is never native, its arrays are usable as they are. */
typedef struct { float x, y, z; } V3;
typedef struct { V3 right; unsigned flags; V3 up; unsigned pad1; V3 at; unsigned pad2; V3 pos; unsigned pad3; } Matrix;
typedef struct { void *parentGeom; float sphere[4]; V3 *verts; V3 *normals; } MorphTarget;
typedef struct { void *texture; unsigned colour; void *pipeline; float ambient, specular, diffuse; } Material;
typedef struct { V3 ambient, diffuse, direction; } SceneLight;
typedef struct { unsigned short *indices; int numIndices; Material *material; } Mesh;
typedef struct { unsigned flags; unsigned short numMeshes, serial; unsigned totalIndices, firstMeshOffset; } MeshHeader;
typedef struct {
    unsigned object[2], flags;
    unsigned short locked, refCount;
    int numTriangles, numVertices, numMorphTargets, numTexCoordSets;
    void *matList[3];
    void *triangles;
    unsigned *preLit;
    float (*texCoords[8])[2];
    MeshHeader *mesh;
    void *repEntry;
    MorphTarget *morphTarget;
} Geometry;

enum { CAMERA_VIEW = 0x20, CAMERA_NEAR = 0x80, CAMERA_FAR = 0x84, ATOMIC_FRAME = 4, ATOMIC_GEOMETRY = 0x18 };
enum { GEOMETRY_NATIVE = 0x01000000, MESH_TRISTRIP = 1, MESH_PRIM_MASK = 0xf };

#include "clip.h"
#include "frustum.h"
/* ponytail: every vertex is copied every frame; a cache per geometry when the device shows this in its profile. */


typedef float Mat4[4][4]; /* rows; a row vector times the matrix, as sceGuSetMatrix takes it */
static void mat4_from(Mat4 out, const Matrix *m)
{
    const V3 *row[4] = { &m->right, &m->up, &m->at, &m->pos };
    for (int i = 0; i < 4; i++) {
        out[i][0] = row[i]->x; out[i][1] = row[i]->y; out[i][2] = row[i]->z; out[i][3] = i == 3;
    }
}

static void mat4_mul(Mat4 out, Mat4 a, Mat4 b)
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    memcpy(out, r, sizeof r);
}

static void set_matrix(int which, const Matrix *m)
{
    ScePspFMatrix4 out = {
        { m->right.x, m->right.y, m->right.z, 0 }, { m->up.x, m->up.y, m->up.z, 0 },
        { m->at.x, m->at.y, m->at.z, 0 }, { m->pos.x, m->pos.y, m->pos.z, 1 },
    };
    sceGuSetMatrix(which, &out);
}

/* RenderWare's view gives x/z and y/z from 0 to 1, y going down the screen. */
static void camera_projection(ScePspFMatrix4 *out)
{
    const float near = *(float *)((char *)current_camera + CAMERA_NEAR), far = *(float *)((char *)current_camera + CAMERA_FAR);
    ScePspFMatrix4 projection = {
        { 2, 0, 0, 0 }, { 0, -2, 0, 0 }, { -1, 1, (far + near) / (far - near), 1 }, { 0, 0, -2 * far * near / (far - near), 0 },
    };
    *out = projection;
}

static void set_camera(void)
{
    if (gu_mesh_mode) { /* the same camera, view and planes as the last mesh in this mode: nothing to send */
        unsigned key[14];
        key[0] = (unsigned)current_camera;
        memcpy(key + 1, (char *)current_camera + CAMERA_VIEW, 12 * 4); /* right, up, at rows (each + a flags/pad word) */
        memcpy(&key[13], (char *)current_camera + CAMERA_NEAR, 4);
        if (gs.camera_known && !memcmp(key, gs.camera_key, sizeof key) &&
            *(unsigned *)((char *)current_camera + CAMERA_FAR) == gs.camera_far &&
            !memcmp((char *)current_camera + CAMERA_VIEW + 48, gs.camera_pos, 12)) {
            kept_cameras++;
            return;
        }
        memcpy(gs.camera_key, key, sizeof key);
        gs.camera_far = *(unsigned *)((char *)current_camera + CAMERA_FAR);
        memcpy(gs.camera_pos, (char *)current_camera + CAMERA_VIEW + 48, 12);
        gs.camera_known = 1;
    }
    ScePspFMatrix4 projection;
    camera_projection(&projection);
    sceGuSetMatrix(GU_PROJECTION, &projection);
    set_matrix(GU_VIEW, (const Matrix *)((char *)current_camera + CAMERA_VIEW));
}

/* Clip coordinates and outcodes of an array's vertices; returns the OR of the outcodes (0: the GE takes it all). */
static unsigned guard_classify_body(const V3 *position, int count, const Matrix *model, float near, const float *sphere);
static unsigned guard_classify(const V3 *position, int count, const Matrix *model, float near, const float *sphere)
{
    prof_begin(PROF_GUARD);
    unsigned r = guard_classify_body(position, count, model, near, sphere) & GUARD_MASK;
    prof_end(PROF_GUARD);
    return r;
}

static unsigned hash_words(unsigned h, const void *data, int bytes);

/* Clip coordinates of count vertices into c (16-byte aligned), outcodes into code; returns their OR.
   M (columns m[0..3]) gives x y z w; D gives the four side distances G w -+ x, G w -+ y, so their sign bits are outcode
   bits 1-4; bit 0 compares w with near as integers (near > 0: a negative w has its sign bit, so it is smaller). */
static unsigned guard_vfpu(const V3 *position, int count, const float m[4][4], float near, float (*c)[4], unsigned char *code, int stride)
{
    float mm[16] __attribute__((aligned(16))), dd[16] __attribute__((aligned(16)));
    static const float coef[4][4] = { { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            mm[i * 4 + k] = m[i][k];
            dd[i * 4 + k] = m[i][0] * coef[k][0] + m[i][1] * coef[k][1] + m[i][2] * coef[k][2] + m[i][3] * coef[k][3];
        }
    union { float f; int i; } nb = { near };
    unsigned any = 0;
    if (count <= 0)
        return 0;
    const V3 *pos = position;
    int n = count, t0, t1, t2;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lv.q    C400, 0(%[mm])\n"
        "lv.q    C410, 16(%[mm])\n"
        "lv.q    C420, 32(%[mm])\n"
        "lv.q    C430, 48(%[mm])\n"
        "lv.q    C500, 0(%[dd])\n"
        "lv.q    C510, 16(%[dd])\n"
        "lv.q    C520, 32(%[dd])\n"
        "lv.q    C530, 48(%[dd])\n"
        "vone.s  S613\n"
        "1:\n"
        "lv.s    S610, 0(%[pos])\n"
        "lv.s    S611, 4(%[pos])\n"
        "lv.s    S612, 8(%[pos])\n"
        "vtfm4.q C700, E400, C610\n"
        "vtfm4.q C710, E500, C610\n"
        "sv.q    C700, 0(%[c])\n"
        "mfv     %[t0], S703\n"
        "slt     %[t0], %[t0], %[nb]\n"
        "mfv     %[t1], S710\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 1\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t1], S711\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 2\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t1], S712\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 3\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t2], S713\n"
        "srl     %[t2], %[t2], 31\n"
        "sll     %[t2], %[t2], 4\n"
        "or      %[t0], %[t0], %[t2]\n"
        "sb      %[t0], 0(%[code])\n"
        "or      %[any], %[any], %[t0]\n"
        "addu    %[pos], %[pos], %[st]\n"
        "addiu   %[c], %[c], 16\n"
        "addiu   %[n], %[n], -1\n"
        "bnez    %[n], 1b\n"
        "addiu   %[code], %[code], 1\n"
        ".set pop\n"
        : [pos] "+r"(pos), [c] "+r"(c), [code] "+r"(code), [n] "+r"(n), [any] "+r"(any), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
        : [mm] "r"(mm), [dd] "r"(dd), [nb] "r"(nb.i), [st] "r"(stride)
        : "memory");
    return any;
}

/* Inputs of the last all-inside classify per array: the same arrays, matrices and near plane give the same answer. */
enum { GUARD_MEMO = 1024 };
static struct { const V3 *position; unsigned key; } guard_memo[GUARD_MEMO];

/* model (0: the world) times view times projection: model space to clip space */
static void guard_matrix(Mat4 m, const Matrix *model)
{
    Mat4 view, projection;
    ScePspFMatrix4 p;
    camera_projection(&p);
    memcpy(projection, &p, sizeof projection);
    mat4_from(view, (const Matrix *)((char *)current_camera + CAMERA_VIEW));
    if (model) {
        mat4_from(m, model);
        mat4_mul(m, m, view);
    } else
        memcpy(m, view, sizeof(Mat4)); /* m is a parameter here: sizeof m would be a pointer's 4 bytes */
    mat4_mul(m, m, projection);
}

/* sphere (x y z r in model space) wholly in front of the near plane and inside the guard band */
static int guard_sphere_inside(Mat4 m, const float *sphere, float near)
{
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    float c[4];
    for (int k = 0; k < 4; k++)
        c[k] = sphere[0] * m[0][k] + sphere[1] * m[1][k] + sphere[2] * m[2][k] + m[3][k];
    int inside = 1;
    for (int plane = p4_guard_gnear ? 1 : 0; plane < 5 && inside; plane++) {
        float d = plane ? 0 : -near, g[3] = { 0, 0, 0 };
        for (int k = 0; k < 4; k++) {
            d += coef[plane][k] * c[k];
            for (int i = 0; i < 3; i++)
                g[i] += coef[plane][k] * m[i][k];
        }
        inside = d > sphere[3] * sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    }
    return inside;
}

/* guard_sphere_inside with the planes taken out: plane p as a 4-vector P (a point x is inside when x.P + P[3] > 0,
   the near plane carrying -near) and its normal's length. Built once per skinned draw instead of once per bone. */
static void guard_sphere_planes(Mat4 m, float near, float P[5][4], float len[5])
{
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    for (int p = 0; p < 5; p++) {
        for (int i = 0; i < 4; i++)
            P[p][i] = coef[p][0] * m[i][0] + coef[p][1] * m[i][1] + coef[p][2] * m[i][2] + coef[p][3] * m[i][3];
        if (!p)
            P[p][3] -= near;
        len[p] = sqrtf(P[p][0] * P[p][0] + P[p][1] * P[p][1] + P[p][2] * P[p][2]);
    }
}
static int guard_sphere_inside_planes(float P[5][4], const float len[5], const float *sphere)
{
    for (int p = p4_guard_gnear ? 1 : 0; p < 5; p++)
        if (!(sphere[0] * P[p][0] + sphere[1] * P[p][1] + sphere[2] * P[p][2] + P[p][3] > sphere[3] * len[p]))
            return 0;
    return 1;
}
static void anim_seen_at(const void *hierarchy, float depth, const Matrix *model);

/* The guard planes (near, then the four sides of GUARD x the view) as world-space 4-vectors: a world point p is inside
   plane k when W[k] . (p, 1) > 0 (near: > near). guard_sphere_inside builds model x view x projection for every array
   (b86 Heaven: 200 calls, 10.5 k cycles each); these change only with the camera. */
static struct { unsigned key[18]; float w[5][4], len[5]; Mat4 vp; int known; } guard_world;
static void guard_world_planes(void)
{
    unsigned key[18];
    key[0] = (unsigned)current_camera;
    memcpy(key + 1, (char *)current_camera + CAMERA_VIEW, 15 * 4); /* right, up, at (each + a pad word), pos */
    memcpy(key + 16, (char *)current_camera + CAMERA_NEAR, 4);
    memcpy(key + 17, (char *)current_camera + CAMERA_FAR, 4);
    if (guard_world.known && !memcmp(key, guard_world.key, sizeof key))
        return;
    memcpy(guard_world.key, key, sizeof key);
    guard_world.known = 1;
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    Mat4 vp;
    guard_matrix(vp, 0);
    memcpy(guard_world.vp, vp, sizeof vp);
    for (int k = 0; k < 5; k++) {
        float *w = guard_world.w[k];
        for (int j = 0; j < 4; j++)
            w[j] = vp[j][0] * coef[k][0] + vp[j][1] * coef[k][1] + vp[j][2] * coef[k][2] + vp[j][3] * coef[k][3];
        guard_world.len[k] = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    }
}
/* The sphere (model space) wholly inside every plane. The model's largest row length scales the radius, so a
   non-uniform scale only makes the answer "not inside" more often (the vertices are then looked at). */
static int guard_sphere_world(const Matrix *model, const float *sphere, float near)
{
    float x = sphere[0], y = sphere[1], z = sphere[2], r = sphere[3];
    if (model) {
        const Matrix *m = model;
        float wx = x * m->right.x + y * m->up.x + z * m->at.x + m->pos.x;
        float wy = x * m->right.y + y * m->up.y + z * m->at.y + m->pos.y;
        float wz = x * m->right.z + y * m->up.z + z * m->at.z + m->pos.z;
        float a = m->right.x * m->right.x + m->right.y * m->right.y + m->right.z * m->right.z;
        float b = m->up.x * m->up.x + m->up.y * m->up.y + m->up.z * m->up.z;
        float c = m->at.x * m->at.x + m->at.y * m->at.y + m->at.z * m->at.z;
        float big = a > b ? a : b;
        r *= sqrtf(big > c ? big : c);
        x = wx, y = wy, z = wz;
    }
    for (int k = p4_guard_gnear ? 1 : 0; k < 5; k++) {
        const float *w = guard_world.w[k];
        float d = w[0] * x + w[1] * y + w[2] * z + w[3] - (k ? 0 : near);
        if (!(d > r * guard_world.len[k]))
            return 0;
    }
    return 1;
}

/* guard_matrix with view x projection kept per camera (b97 Heaven: 91 classifies a frame rebuilt it each time). */
static void guard_matrix_cached(Mat4 m, const Matrix *model)
{
    guard_world_planes();
    if (model) {
        mat4_from(m, model);
        mat4_mul(m, m, guard_world.vp);
    } else
        memcpy(m, guard_world.vp, sizeof(Mat4));
}

static unsigned guard_classify_body(const V3 *position, int count, const Matrix *model, float near, const float *sphere)
{
    if (p4_guard_world && sphere && sphere[3] > 0) {
        guard_world_planes();
        int inside = guard_sphere_world(model, sphere, near);
        if (p4_guard_check && !(guard_world_checks++ & 63)) { /* the b88 test on the same sphere */
            Mat4 check;
            guard_matrix(check, model);
            int old = guard_sphere_inside(check, sphere, near);
            guard_world_unsafe += inside && !old;
            guard_world_stricter += !inside && old;
        }
        if (inside) {
            guard_spheres++;
            return 0;
        }
    }
    Mat4 m;
    guard_matrix_cached(m, model);
    unsigned memo_key = 0, memo_slot = 0;
    if (p4_guard_memo && sphere) { /* static arrays only: skinned ones change in place */
        unsigned h = hash_words(2166136261u, m, sizeof m);
        h = hash_words(h, &near, 4);
        h = hash_words(h, &count, 4);
        memo_key = h | 1;
        memo_slot = ((unsigned)position >> 4) * 2654435761u >> 22;
        if (guard_memo[memo_slot].position == position && guard_memo[memo_slot].key == memo_key) {
            guard_memo_hits++;
            return 0;
        }
    }
    if (!p4_guard_world && sphere && sphere[3] > 0) { /* the bounding sphere wholly inside the band: no vertex needs a look */
        if (guard_sphere_inside(m, sphere, near)) {
            guard_spheres++;
            if (memo_key) {
                guard_memo[memo_slot].position = position;
                guard_memo[memo_slot].key = memo_key;
            }
            return 0;
        }
    }
    unsigned any = 0;
    guard_vertices += count;
    if (p4_guard_fast == 2) {
        any = guard_vfpu(position, count, (const float (*)[4])m, near, guard_clip, guard_code, sizeof *position);
        if (p4_guard_check && !(guard_check_runs++ & 63)) {
            for (int i = 0; i < count; i++) {
                const float *c = guard_clip[i];
                float x = position[i].x, y = position[i].y, z = position[i].z, w = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
                float cx = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0], cy = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
                float ref[4] = { cx, cy, 0, w };
                unsigned want = guard_outcode(ref, near);
                float err = fabsf(c[0] - cx) + fabsf(c[1] - cy) + fabsf(c[3] - w);
                if (want != guard_code[i] || err > 1e-3f * (fabsf(w) + fabsf(cx) + fabsf(cy) + 1))
                    guard_check_diffs++;
            }
        }
        if (!any && memo_key) {
            guard_memo[memo_slot].position = position;
            guard_memo[memo_slot].key = memo_key;
        }
        return any;
    }
    if (!p4_guard_fast) {
        for (int i = 0; i < count; i++) {
            float x = position[i].x, y = position[i].y, z = position[i].z, *c = guard_clip[i];
            for (int k = 0; k < 4; k++)
                c[k] = x * m[0][k] + y * m[1][k] + z * m[2][k] + m[3][k];
            any |= guard_code[i] = guard_outcode(c, near);
        }
        return any;
    }
    /* b72: 185 cycles a vertex through the loops and the per-plane switch. Same arithmetic, same order. */
    const float m00 = m[0][0], m01 = m[0][1], m02 = m[0][2], m03 = m[0][3];
    const float m10 = m[1][0], m11 = m[1][1], m12 = m[1][2], m13 = m[1][3];
    const float m20 = m[2][0], m21 = m[2][1], m22 = m[2][2], m23 = m[2][3];
    const float m30 = m[3][0], m31 = m[3][1], m32 = m[3][2], m33 = m[3][3];
    const V3 *pos = position;
    float (*c)[4] = guard_clip;
    unsigned char *code = guard_code;
    for (int i = 0; i < count; i++, pos++, c++) {
        float x = pos->x, y = pos->y, z = pos->z;
        float cx = x * m00 + y * m10 + z * m20 + m30;
        float cy = x * m01 + y * m11 + z * m21 + m31;
        float cz = x * m02 + y * m12 + z * m22 + m32;
        float cw = x * m03 + y * m13 + z * m23 + m33;
        (*c)[0] = cx; (*c)[1] = cy; (*c)[2] = cz; (*c)[3] = cw;
        float g = GUARD * cw;
        unsigned k = (cw - near < 0) | (g - cx < 0) << 1 | (g + cx < 0) << 2 | (g - cy < 0) << 3 | (g + cy < 0) << 4;
        code[i] = k;
        any |= k;
    }
    if (!any && memo_key) {
        guard_memo[memo_slot].position = position;
        guard_memo[memo_slot].key = memo_key;
    }
    return any;
}

/* The game's TEST_1 (RpSkyRenderStateSet 3): alpha test method and reference, and what a failing pixel still writes.
   P4 draws its fog with ATST GEQUAL 0x7F, AFAIL FB_ONLY: translucent texels paint colour but leave the depth alone.
   Drawn with depth writes everywhere, one fog layer hid the next in hard rectangles (first battle). Two passes split
   the pixels by the same test, so the fill stays about one pass. */
int p4_gs_test = 1; /* 0: fixed alpha test GREATER 8 for every 3D mesh, as b64 */
/* The second pass costs a whole second fill even when no pixel can fail: b76 Heaven 151 ms a frame with it, 101 ms
   with p4_gs_test 0 (GE wait 54 -> 9 ms). Skip it when it cannot change a pixel: every pixel passes (smallest
   texture alpha x smallest vertex alpha reaches the reference), or FB_ONLY without depth writes (both passes paint
   the same colour: one pass without the test), or ZB_ONLY without depth writes (pass 2 writes nothing). */
extern int p4_alpha_x2;
int p4_gs_test_raw_prelit = 0; /* 1: raw reference for prelit arrays (experiment, b93 Heaven sky: no visible change) */
int p4_gs_test_skip = 1; /* 0: always two passes, as b76 */
static const unsigned *draw_prelit; /* the mesh's source alpha for draw_tested; draw_alpha_known 0: not known */
static int draw_count, draw_alpha_known;
/* b177 (p4_material_state 2): an unlit compact mesh whose vertices leave the material colour out; draw_mesh_vertices
   sends it as GE state (lighting on, no light: vertex colour x ambient), as the GE-skinned unlit batches do */
static int draw_material_ge;
static int draw_prelit_min = -1;
static unsigned draw_material_alpha = 255;
static inline unsigned double_colour(unsigned c);
static int vertex_alpha_min(void)
{
    if (!draw_alpha_known)
        return 0;
    if (draw_prelit_min < 0) {
        unsigned m = 255;
        if (draw_prelit)
            for (int j = 0; j < draw_count && m; j++) {
                unsigned a = double_colour(draw_prelit[j]) >> 24;
                if (a < m)
                    m = a;
            }
        draw_prelit_min = m;
    }
    unsigned t = draw_prelit_min * draw_material_alpha + 128;
    return (t + (t >> 8)) >> 8; /* modulate_colour */
}
/* TEST_1 by the mesh's own vertices (b96 Heaven: the geometry-wide smallest prelit alpha was 0 for ~200 meshes a
   frame with opaque textures, so each drew twice). A pixel's alpha lies between texture min x vertex min and texture
   max x vertex max (Gouraud and filtering only interpolate): all pass -> one pass; all fail -> only what pass two
   draws. Exact, no picture change. */
int p4_gs_mesh_alpha = 1; /* 0: the geometry-wide prelit minimum only, as b96 */
static int draw_vertex_alpha(int vtype, int count, const void *indices, const void *vertices, unsigned *lo, unsigned *hi)
{
    if (draw_skin_alpha) {
        *lo = draw_skin_alpha[0], *hi = draw_skin_alpha[1];
        return 1;
    }
    if ((vtype & GU_VERTEX_BITS) != GU_VERTEX_32BITF || (vtype & GU_COLOR_BITS) != GU_COLOR_8888 || (vtype & GU_WEIGHT_BITS) ||
        ((vtype & GU_INDEX_BITS) && (vtype & GU_INDEX_BITS) != GU_INDEX_16BIT) || !vertices || count <= 0)
        return 0;
    /* the bounds of the vertex colours alone (b177: kept so in cache entries, whose key no longer has the material) */
    unsigned m = 255, big = 0;
    if (draw_slot_v == vertices && draw_slot_alpha[0] >= 0) {
        m = draw_slot_alpha[0], big = draw_slot_alpha[1];
    } else if (draw_fixed_alpha) { /* a subset (or cut pieces) of a cache entry whose bounds are known: they still bound it */
        m = draw_fixed_alpha[0], big = draw_fixed_alpha[1];
    } else {
        tested_scans++;
        int stride = vtype & GU_NORMAL_BITS ? sizeof(Vertex3DN) : sizeof(Vertex3D);
        const unsigned short *ix = (vtype & GU_INDEX_BITS) ? indices : 0;
        for (int k = 0; k < count; k++) {
            unsigned a = *(const unsigned *)((const char *)vertices + (ix ? ix[k] : k) * stride + 8) >> 24; /* colour after u, v */
            if (a < m) m = a;
            if (a > big) big = a;
        }
        if (draw_slot_v == vertices)
            draw_slot_alpha[0] = m, draw_slot_alpha[1] = big;
    }
    if ((vtype & GU_NORMAL_BITS) || draw_material_ge) { /* GE-lit: the vertex colour is the ambient term, the material alpha comes on top */
        m = m * draw_material_alpha / 255;
        big = (big * draw_material_alpha + 254) / 255;
    }
    *lo = m, *hi = big;
    return 1;
}
static void draw_tested(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    unsigned test = sky_state[3];
    if (!p4_gs_test || !(test & 1)) {
        if (gu_mesh_mode)
            st_alpha_func(GU_GREATER, 8); /* mesh mode leaves the last tested draw's function in place */
        gu_draw(prim, vtype, count, indices, vertices);
        return;
    }
    tested_draws++;
    static const int method[8] = { GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL };
    static const int inverse[8] = { GU_ALWAYS, GU_NEVER, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_EQUAL };
    int atst = test >> 1 & 7, afail = test >> 12 & 3;
    /* The GS's 0x80 is our 0xff in texels and in lit or white vertices. Prelit alpha reaches the GE as the GS got it
       (p4_alpha_x2 0), so there the reference stays: doubled, Heaven's clouds (alpha 0x7f-0xfd) failed, wrote no
       depth, and the night01 dome drawn after them painted them out. */
    int raw = p4_gs_test_raw_prelit && draw_alpha_known && draw_prelit && !p4_alpha_x2;
    int aref = (test >> 4 & 0xff) << !raw;
    if (aref > 255) aref = 255;
    st_enable(GU_ALPHA_TEST, 1);
    st_alpha_func(method[atst], aref);
    /* failing pixels: nothing to add when they are dropped, or are only the alpha-0 ones under a blend */
    int only_clear = (atst == 5 || atst == 6) && aref == 0;
    int second = afail != 0 && atst != 1 && !only_clear;
    if (second && p4_gs_test_skip) {
        int zwrite = render_state[RS_ZWRITE] != 0;
        if ((afail == 1 || afail == 2) && !zwrite) {
            tested_nodepth++;
            second = 0;
            if (afail == 1) { /* both halves painted: the whole mesh, untested */
                st_alpha_func(GU_ALWAYS, 0);
                gu_draw(prim, vtype, count, indices, vertices);
                if (!gu_mesh_mode)
                    sceGuAlphaFunc(GU_GREATER, 8, 0xff);
                return;
            }
        } else if (atst == 5 || atst == 6) {
            /* the geometry-wide prelit minimum scans every vertex of the geometry: only when the mesh's own bounds are
               not known (b122 Heaven: it was computed for every geometry, then replaced by them) */
            unsigned t = bound_alpha_min, v, vhi = 255;
            unsigned lo, hi;
            if (p4_gs_mesh_alpha && draw_alpha_known && draw_vertex_alpha(vtype, count, indices, vertices, &lo, &hi))
                v = lo, vhi = hi;
            else
                v = vertex_alpha_min();
            unsigned lowest = t == 255 && v == 255 ? 255 : t * v >> 8;
            unsigned highest = bound_alpha_max * vhi + 254;
            highest = (highest + (highest >> 8)) >> 8 > 255 ? 255 : (highest + (highest >> 8)) >> 8; /* rounded up */
            if (atst == 5 ? lowest >= (unsigned)aref : lowest > (unsigned)aref) {
                tested_opaque++;
                second = 0;
            } else if (p4_gs_mesh_alpha && afail == 1 && (atst == 5 ? highest < (unsigned)aref : highest <= (unsigned)aref)) {
                tested_allfail++; /* every pixel fails: FB_ONLY paints them all, no depth */
                st_alpha_func(GU_ALWAYS, 0);
                st_depth_mask(GU_TRUE);
                gu_draw(prim, vtype, count, indices, vertices);
                st_depth_mask(render_state[RS_ZWRITE] ? GU_FALSE : GU_TRUE);
                if (!gu_mesh_mode)
                    sceGuAlphaFunc(GU_GREATER, 8, 0xff);
                return;
            }
        }
    }
    gu_draw(prim, vtype, count, indices, vertices);
    if (second) {
        tested_second++;
        st_alpha_func(inverse[atst], aref);
        if (afail == 2) {
            st_pixel_mask(0xffffffff); /* ZB_ONLY: depth, no colour */
        } else {
            st_depth_mask(GU_TRUE); /* FB_ONLY, RGB_ONLY: colour, no depth */
            if (afail == 3)
                st_pixel_mask(pixel_mask | 0xff000000);
        }
        gu_draw(prim, vtype, count, indices, vertices);
        st_pixel_mask(pixel_mask);
        st_depth_mask(render_state[RS_ZWRITE] ? GU_FALSE : GU_TRUE);
    }
    if (!gu_mesh_mode)
        sceGuAlphaFunc(GU_GREATER, 8, 0xff);
}

static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices, const void *vertices, const Material *material, int modulate, const SceneLight *light);
static int census3d_skip(const Material *material, int vtype, int count, int modulate);
static inline signed char normal_s8(float c);

/* A mesh whose array has corners beyond the guard band: inside triangles stay indexed, the rest are cut on the CPU.
   vertices: Vertex3D (lit on the CPU) or, with GU_NORMAL_8BIT in vtype, Vertex3DN that the GE lights; the cut
   corners carry the interpolated normal, so cut triangles are lit by the GE like the rest of the mesh. */
/* Codes computed on demand (only the vertices of partly visible chunks): the FPU form of guard_vfpu. */
typedef struct { float m[4][4]; const char *pos; int stride; } GuardLazy;
static unsigned short *guard_stamp, guard_gen = 1;
static inline void guard_code_lazy(const GuardLazy *z, int i, float near)
{
    if (guard_stamp[i] == guard_gen)
        return;
    guard_stamp[i] = guard_gen;
    guard_lazy_vertices++;
    const float *p = (const float *)(z->pos + i * z->stride);
    float x = p[0], y = p[1], w = p[2], *c = guard_clip[i];
    for (int k = 0; k < 4; k++)
        c[k] = x * z->m[0][k] + y * z->m[1][k] + w * z->m[2][k] + z->m[3][k];
    float g = GUARD * c[3];
    guard_code[i] = (c[3] - near < 0) | (g - c[0] < 0) << 1 | (g + c[0] < 0) << 2 | (g - c[1] < 0) << 3 | (g + c[1] < 0) << 4;
}

/* chunk_class (one byte per run of chunk_tris triangles): 0 inside the band (kept without a look), 1 per triangle,
   2 off screen (dropped); 0 = no chunks: every triangle per vertex codes. lazy: codes not computed yet. */
/* One guarded draw's final geometry, captured before submission (b236). */
typedef struct { const unsigned short *idx; const void *cut; int nkept, ncut, valid; } GuardCapture;
static GuardCapture *guard_capture;
extern int p4_guard_loop2;
static void draw_guarded_chunks(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light,
                                const unsigned char *chunk_class, int chunk_tris, const GuardLazy *lazy);
static void draw_guarded(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light)
{
    draw_guarded_chunks(prim, idx, nidx, vertices, vtype, near, material, modulate, light, 0, 0, 0);
}
extern int p4_guard_loop2;
static void draw_guarded_chunks(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light,
                                const unsigned char *chunk_class, int chunk_tris, const GuardLazy *lazy)
{
    int lit = (vtype & GU_NORMAL_8BIT) != 0, stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    int triangles = prim == GU_TRIANGLE_STRIP ? nidx - 2 : nidx / 3;
    if (triangles <= 0)
        return;
    /* Culled chunks allocate nothing; inside chunks only copy indices. In particular a subdivided mesh can have
       many triangles but very few crossing the band. Reserving cut vertices for all of them rejects valid draws. */
    int kept_max = triangles, cut_max = triangles;
    if (chunk_class) {
        kept_max = cut_max = 0;
        for (int first = 0, j = 0; first < triangles; first += chunk_tris, j++) {
            int n = triangles - first < chunk_tris ? triangles - first : chunk_tris;
            if (chunk_class[j] != 2)
                kept_max += n;
            if (chunk_class[j] == 1)
                cut_max += n;
        }
    }
    int units = (cut_max * GUARD_OUT_MAX * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
    if (index_used + kept_max * 3 > MAX_INDICES || vertex_used + units > MAX_VERTICES) {
        p4_log("TRACE guard pool full\n");
        return;
    }
    unsigned short *kept = index_pool + index_used;
    char *cut = (char *)(vertex_pool + vertex_used);
    int nkept = 0, ncut = 0;
    guard_meshes++;
    prof_begin(PROF_GLOOP);
    /* b143: chunk by chunk (one class lookup, no divide per triangle); a chunk inside the band of a triangle list is
       its own index range, copied whole (p4_guard_loop2). The order of the kept indices is unchanged. */
    int per = chunk_class ? chunk_tris : triangles;
    for (int first = 0; first < triangles; first += per) {
    int last = first + per < triangles ? first + per : triangles;
    int cls = chunk_class ? chunk_class[first / per] : 1;
    if (cls == 2) /* the whole run off screen */
        continue;
    if (cls == 0 && prim != GU_TRIANGLE_STRIP && p4_guard_loop2) {
        memcpy(kept + nkept, idx + first * 3, (last - first) * 3 * sizeof *kept);
        nkept += (last - first) * 3;
        continue;
    }
    for (int t = first; t < last; t++) {
        int a, b, c;
        if (prim == GU_TRIANGLE_STRIP) {
            a = idx[t]; b = idx[t + 1]; c = idx[t + 2];
            if (a == b || b == c || a == c)
                continue;
        } else {
            a = idx[t * 3]; b = idx[t * 3 + 1]; c = idx[t * 3 + 2];
        }
        if (cls == 0) {
            kept[nkept++] = a; kept[nkept++] = b; kept[nkept++] = c;
            continue;
        }
        if (lazy) {
            guard_code_lazy(lazy, a, near);
            guard_code_lazy(lazy, b, near);
            guard_code_lazy(lazy, c, near);
        }
        unsigned ca = guard_code[a], cb = guard_code[b], cc = guard_code[c], mask = GUARD_MASK;
        if (!((ca | cb | cc) & mask)) {
            kept[nkept++] = a; kept[nkept++] = b; kept[nkept++] = c;
            continue;
        }
        if (ca & cb & cc) { /* wholly behind the near plane or beyond one side of the band */
            guard_dropped++;
            continue;
        }
        { /* wholly beyond one edge of the screen (x or y past +-w): invisible, nothing to cut (PS2RC drops these too) */
            const float *pa = guard_clip[a], *pb = guard_clip[b], *pc = guard_clip[c];
            if ((pa[0] > pa[3] && pb[0] > pb[3] && pc[0] > pc[3]) || (pa[0] < -pa[3] && pb[0] < -pb[3] && pc[0] < -pc[3]) ||
                (pa[1] > pa[3] && pb[1] > pb[3] && pc[1] > pc[3]) || (pa[1] < -pa[3] && pb[1] < -pb[3] && pc[1] < -pc[3])) {
                guard_offscreen++;
                continue;
            }
        }
        if (p4_guard_near_ge && ((ca | cb | cc) & 1)) {
            kept[nkept++] = a; kept[nkept++] = b; kept[nkept++] = c;
            guard_near_ge++;
            continue;
        }
        prof_begin(PROF_GCLIP);
        ClipVertex tri[3];
        int corner[3] = { a, b, c };
        for (int k = 0; k < 3; k++) {
            memcpy(tri[k].c, guard_clip[corner[k]], sizeof tri[k].c);
            if (lit) {
                const Vertex3DN *p = (const Vertex3DN *)vertices + corner[k];
                tri[k].v = (Vertex3D){ p->u, p->v, p->colour, p->x, p->y, p->z };
                tri[k].n[0] = p->nx; tri[k].n[1] = p->ny; tri[k].n[2] = p->nz;
            } else {
                tri[k].v = ((const Vertex3D *)vertices)[corner[k]];
                tri[k].n[0] = tri[k].n[1] = tri[k].n[2] = 0;
            }
        }
        Vertex3D out[GUARD_OUT_MAX];
        float normal[GUARD_OUT_MAX][3];
        unsigned planes = (ca | cb | cc) & mask;
        int n = p4_guard_clip_fast ? clip_triangle_guard_fast(tri, near, out, lit ? normal : 0, planes)
                                   : clip_triangle_guard(tri, near, out, lit ? normal : 0, planes);
        if (p4_guard_clip_fast && p4_guard_clip_check && !(guard_clip_checks++ & 63)) {
            Vertex3D ref[GUARD_OUT_MAX];
            float refn[GUARD_OUT_MAX][3];
            int rn = clip_triangle_guard(tri, near, ref, lit ? refn : 0, planes);
            if (rn != n || memcmp(ref, out, n * sizeof *out) || (lit && memcmp(refn, normal, n * sizeof *normal)))
                guard_clip_diffs++;
        }
        for (int k = 0; k < n; k++, ncut++) {
            if (lit) { /* the interpolated normal in s8 units again; the GE divides by its length */
                Vertex3DN *v = (Vertex3DN *)cut + ncut;
                *v = (Vertex3DN){ out[k].u, out[k].v, out[k].colour, normal_s8(normal[k][0] / 127), normal_s8(normal[k][1] / 127), normal_s8(normal[k][2] / 127), 0, out[k].x, out[k].y, out[k].z };
            } else {
                ((Vertex3D *)cut)[ncut] = out[k];
            }
        }
        guard_clipped++;
        prof_end(PROF_GCLIP);
    }
    }
    prof_end(PROF_GLOOP);
    if (guard_capture) {
        guard_capture->idx = kept; guard_capture->cut = cut;
        guard_capture->nkept = nkept; guard_capture->ncut = ncut; guard_capture->valid = 1;
    }
    prof_begin(PROF_GSUB);
    if (nkept) {
        index_used += nkept;
        sceKernelDcacheWritebackRange(kept, nkept * 2);
        draw_mesh_vertices(GU_TRIANGLES, vtype, nkept, kept, vertices, material, modulate, light);
    }
    if (ncut) {
        vertex_used += (ncut * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
        sceKernelDcacheWritebackRange(cut, ncut * stride);
        draw_mesh_vertices(GU_TRIANGLES, vtype & ~GU_INDEX_16BIT, ncut, 0, cut, material, modulate, light);
    }
    prof_end(PROF_GSUB);
}

/* A guarded mesh drawn from its compact arrays (cached or just emitted): classify only the vertices it uses, in their
   own numbering, then draw it whole or cut what crosses the band. Overwrites guard_clip and guard_code. */
enum { GUARD_BOX_SLOTS = 256, GUARD_MAX_CHUNKS = 4096 };
/* triangles a chunk (b143: a switch; 32 before). b142 Heaven per frame: 65 chunks inside, 78 partial, 57 off screen */
int p4_guard_chunk = 8, p4_guard_loop2 = 1; /* b143 Heaven CPU: 32 -> 57.1 ms, 16 -> 56.6, 8 -> 56.1 */
static int guard_chunk_now(void) { return p4_guard_chunk < 8 ? 8 : p4_guard_chunk > 256 ? 256 : p4_guard_chunk; }
/* model-space boxes (min xyz, max xyz) of runs of GUARD_CHUNK triangles of a cache entry, built from the entry's own
   vertices, so they always match what the entry draws; valid while the entry keeps its key and build frame */
typedef struct { const void *slot; unsigned key, built, used; int n, cap, chunk; float *box, *rad; } GuardBoxes;
static unsigned guard_box_tick;
static GuardBoxes *guard_boxes;
static const float *guard_slot_boxes(const void *slot, unsigned key, unsigned built, int prim, const unsigned short *idx, int nidx,
                                     const void *vertices, int vtype, int *count, const float **rad)
{
    int chunk = guard_chunk_now();
    int tris = prim == GU_TRIANGLE_STRIP ? nidx - 2 : nidx / 3, n = (tris + chunk - 1) / chunk;
    if (tris <= 0 || n > GUARD_MAX_CHUNKS)
        return 0;
    if (!guard_boxes && !(guard_boxes = calloc(GUARD_BOX_SLOTS, sizeof *guard_boxes)))
        return 0;
    /* b199: 4-way sets (b169-b198 Heaven/dialogue: exactly 2 rebuilds a frame, maybe two meshes evicting each other
       in the direct-mapped table); a miss takes an empty way of the set, else the least recently used one */
    GuardBoxes *set = &guard_boxes[(((unsigned)slot >> 4) * 2654435761u >> 24) & ~3u], *e = 0;
    *count = n;
    guard_box_tick++;
    for (int w = 0; w < 4 && !e; w++)
        if (set[w].slot == slot)
            e = &set[w];
    if (e && e->key == key && e->built == built && e->n == n && e->chunk == chunk && e->box) {
        e->used = guard_box_tick;
        *rad = e->rad;
        return e->box;
    }
    if (e)
        guard_box_same++;
    else {
        e = set;
        for (int w = 0; w < 4; w++) {
            if (!set[w].slot) {
                e = &set[w];
                break;
            }
            if (set[w].used < e->used)
                e = &set[w];
        }
    }
    e->used = guard_box_tick;
    if (e->cap < n) {
        free(e->box);
        e->box = malloc(n * 7 * sizeof(float)); /* 6 box floats a chunk, then one radius a chunk */
        e->cap = e->box ? n : 0;
        if (!e->box) {
            e->slot = 0;
            return 0;
        }
    }
    e->rad = e->box + n * 6;
    int lit = (vtype & GU_NORMAL_8BIT) != 0, stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    const char *pos = (const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x));
    for (int j = 0; j < n; j++) {
        float *b = e->box + j * 6;
        b[0] = b[1] = b[2] = 1e30f;
        b[3] = b[4] = b[5] = -1e30f;
        int first = j * chunk, last = first + chunk < tris ? first + chunk : tris;
        int from = prim == GU_TRIANGLE_STRIP ? first : first * 3, to = prim == GU_TRIANGLE_STRIP ? last + 2 : last * 3;
        for (int k = from; k < to; k++) {
            const float *p = (const float *)(pos + idx[k] * stride);
            for (int a = 0; a < 3; a++) {
                if (p[a] < b[a]) b[a] = p[a];
                if (p[a] > b[3 + a]) b[3 + a] = p[a];
            }
        }
        /* b199 p4_guard_small: the largest triangle of the chunk, as the radius of a sphere round its centroid */
        float r2 = 0;
        for (int t = first; t < last; t++) {
            int at = prim == GU_TRIANGLE_STRIP ? t : t * 3;
            const float *c[3];
            for (int k = 0; k < 3; k++)
                c[k] = (const float *)(pos + idx[at + k] * stride);
            float m[3];
            for (int a = 0; a < 3; a++)
                m[a] = (c[0][a] + c[1][a] + c[2][a]) * (1.0f / 3);
            for (int k = 0; k < 3; k++) {
                float dx = c[k][0] - m[0], dy = c[k][1] - m[1], dz = c[k][2] - m[2], d = dx * dx + dy * dy + dz * dz;
                if (d > r2)
                    r2 = d;
            }
        }
        e->rad[j] = sqrtf(r2);
    }
    e->slot = slot, e->key = key, e->built = built, e->n = n, e->chunk = chunk;
    guard_box_builds++;
    *rad = e->rad;
    return e->box;
}

/* A box against plane q (clip-space coefficients; q[4] is subtracted): value at the centre +- the radius, with the
   plane taken to model space through m. Returns the classes of the chunks; *partial / *culled count them. */
/* b144: the same tests on the VFPU. M400/M500: screen planes 1-4 and their absolute values (columns = x y z w of the
   four planes, read through E like guard_vfpu); M600/M700 the band planes; C300/C310 the near plane and its absolute
   value. Per box: centre c (w 1) and half size h (w 0), d = Q.c and r = |Q|.h; culled when d + r < 0 for a screen plane
   or the near plane, partial when d - r < 0 for a band plane (or the near plane without gnear). -0 counts as 0. */
static void guard_boxes_fpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled);
static inline int guard_negative(unsigned bits) { return (bits & 0x80000000u) && (bits & 0x7fffffffu); }
static void guard_boxes_vfpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled)
{
    float ms[16] __attribute__((aligned(16))), as[16] __attribute__((aligned(16)));
    float mb[16] __attribute__((aligned(16))), ab[16] __attribute__((aligned(16)));
    float near_q[8] __attribute__((aligned(16)));
    union { float f[12]; unsigned u[12]; } out __attribute__((aligned(16)));
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            ms[i * 4 + k] = sp[k + 1][i];
            mb[i * 4 + k] = bp[k + 1][i];
            as[i * 4 + k] = i < 3 ? fabsf(sp[k + 1][i]) : 0;
            ab[i * 4 + k] = i < 3 ? fabsf(bp[k + 1][i]) : 0;
        }
    for (int i = 0; i < 4; i++) {
        near_q[i] = sp[0][i];
        near_q[4 + i] = i < 3 ? fabsf(sp[0][i]) : 0;
    }
    static const float half = 0.5f;
    __asm__ volatile(
        "lv.q    C400, 0(%[ms])\n lv.q C410, 16(%[ms])\n lv.q C420, 32(%[ms])\n lv.q C430, 48(%[ms])\n"
        "lv.q    C500, 0(%[as])\n lv.q C510, 16(%[as])\n lv.q C520, 32(%[as])\n lv.q C530, 48(%[as])\n"
        "lv.q    C600, 0(%[mb])\n lv.q C610, 16(%[mb])\n lv.q C620, 32(%[mb])\n lv.q C630, 48(%[mb])\n"
        "lv.q    C700, 0(%[ab])\n lv.q C710, 16(%[ab])\n lv.q C720, 32(%[ab])\n lv.q C730, 48(%[ab])\n"
        "lv.q    C300, 0(%[nq])\n lv.q C310, 16(%[nq])\n"
        "lv.s    S320, 0(%[half])\n"
        :
        : [ms] "r"(ms), [as] "r"(as), [mb] "r"(mb), [ab] "r"(ab), [nq] "r"(near_q), [half] "r"(&half)
        : "memory");
    for (int i = 0; i < n; i++, box += 6) {
        __asm__ volatile(
            "lv.s    S200, 0(%[b])\n lv.s S201, 4(%[b])\n lv.s S202, 8(%[b])\n"
            "lv.s    S210, 12(%[b])\n lv.s S211, 16(%[b])\n lv.s S212, 20(%[b])\n"
            "vadd.t  C220, C200, C210\n"
            "vsub.t  C230, C210, C200\n"
            "vscl.t  C220, C220, S320\n"
            "vscl.t  C230, C230, S320\n"
            "vone.s  S223\n"
            "vzero.s S233\n"
            "vtfm4.q C000, E400, C220\n"
            "vtfm4.q C010, E500, C230\n"
            "vadd.q  C020, C000, C010\n"
            "vtfm4.q C030, E600, C220\n"
            "vtfm4.q C100, E700, C230\n"
            "vsub.q  C110, C030, C100\n"
            "vdot.q  S120, C300, C220\n"
            "vdot.q  S121, C310, C230\n"
            "vadd.s  S122, S120, S121\n"
            "vsub.s  S123, S120, S121\n"
            "sv.q    C020, 0(%[o])\n"
            "sv.q    C110, 16(%[o])\n"
            "sv.q    C120, 32(%[o])\n"
            :
            : [b] "r"(box), [o] "r"(out.f)
            : "memory");
        int c = 0;
        if (guard_negative(out.u[0]) || guard_negative(out.u[1]) || guard_negative(out.u[2]) || guard_negative(out.u[3]) ||
            guard_negative(out.u[10]))
            c = 2;
        else if (guard_negative(out.u[4]) || guard_negative(out.u[5]) || guard_negative(out.u[6]) || guard_negative(out.u[7]) ||
                 (!first_band && guard_negative(out.u[11])))
            c = 1;
        cls[i] = c;
        *partial += c == 1;
        *culled += c == 2;
    }
}

static void guard_boxes_classify(const float *box, int n, const float m[4][4], float near, unsigned char *cls, int *partial, int *culled)
{
    static const float band[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    static const float screen[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, -1, 0, 1 }, { 0, 1, 0, 1 } };
    float bp[5][4], sp[5][4];
    for (int k = 0; k < 5; k++)
        for (int j = 0; j < 4; j++) {
            bp[k][j] = m[j][0] * band[k][0] + m[j][1] * band[k][1] + m[j][2] * band[k][2] + m[j][3] * band[k][3];
            sp[k][j] = m[j][0] * screen[k][0] + m[j][1] * screen[k][1] + m[j][2] * screen[k][2] + m[j][3] * screen[k][3];
        }
    bp[0][3] -= near, sp[0][3] -= near;
    int first_band = p4_guard_gnear ? 1 : 0;
    *partial = *culled = 0;
    if (p4_guard_box_vfpu) {
        guard_boxes_vfpu(box, n, bp, sp, first_band, cls, partial, culled);
        if (!p4_guard_box_check)
            return;
        int p2 = 0, c2 = 0;
        static unsigned char ref[GUARD_MAX_CHUNKS];
        guard_boxes_fpu(box, n, bp, sp, first_band, ref, &p2, &c2);
        guard_box_checks += n;
        for (int i = 0; i < n; i++)
            guard_box_diffs += ref[i] != cls[i];
        return;
    }
    guard_boxes_fpu(box, n, bp, sp, first_band, cls, partial, culled);
}

static void guard_boxes_fpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled)
{
    for (int i = 0; i < n; i++, box += 6) {
        float cx = (box[0] + box[3]) * 0.5f, cy = (box[1] + box[4]) * 0.5f, cz = (box[2] + box[5]) * 0.5f;
        float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
        int c = 0;
        for (int k = 0; k < 5; k++) { /* wholly behind the near plane or beyond one screen edge */
            const float *q = sp[k];
            if (q[0] * cx + q[1] * cy + q[2] * cz + q[3] + fabsf(q[0]) * hx + fabsf(q[1]) * hy + fabsf(q[2]) * hz < 0) {
                c = 2;
                break;
            }
        }
        if (!c)
            for (int k = first_band; k < 5; k++) { /* any corner possibly outside a plane the CPU cuts */
                const float *q = bp[k];
                if (q[0] * cx + q[1] * cy + q[2] * cz + q[3] - fabsf(q[0]) * hx - fabsf(q[1]) * hy - fabsf(q[2]) * hz < 0) {
                    c = 1;
                    break;
                }
            }
        cls[i] = c;
        *partial += c == 1;
        *culled += c == 2;
    }
}

/* b199 p4_guard_small: a chunk that crosses the band but whose triangles are all small next to their distance cannot
   hold a triangle that is both on screen and past the band. Band plane k is screen plane k plus (GUARD - 1) w, so a
   point on screen has band value >= (GUARD - 1) w >= (GUARD - 1) w_min, and a corner past the band (value < 0) of a
   triangle of radius r lies within 2 r of it: |grad| 2 r > (GUARD - 1) w_min. When that cannot hold, every triangle
   of the chunk is either inside the band (the GE draws it) or wholly off screen (the GE drops it past its window, or
   draws nothing visible), so the chunk goes to the GE uncut. Needs gnear (the GE clips the near plane) and a box
   wholly in front of the near plane. *front: 1 when every culled chunk is also in front of the near plane (mode 2 may
   then send the whole mesh to the GE as a recorded CALL; the GE transforms the off-screen chunks too). */
static void guard_small_fix(const float *box, const float *rad, int n, const float m[4][4], float near, unsigned char *cls, int *partial,
                            int *front)
{
    static const float band[4][4] = { { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    float g2 = 0, wq[4];
    for (int k = 0; k < 4; k++) {
        float q2 = 0;
        for (int j = 0; j < 3; j++) {
            float q = m[j][0] * band[k][0] + m[j][1] * band[k][1] + m[j][2] * band[k][2] + m[j][3] * band[k][3];
            q2 += q * q;
        }
        if (q2 > g2)
            g2 = q2;
    }
    for (int j = 0; j < 4; j++)
        wq[j] = m[j][3];
    float need = 2 * sqrtf(g2) * 1.01f / (GUARD - 1); /* a chunk is safe when w_min >= need x radius */
    *front = 1;
    for (int i = 0; i < n; i++, box += 6) {
        if (!cls[i])
            continue;
        float cx = (box[0] + box[3]) * 0.5f, cy = (box[1] + box[4]) * 0.5f, cz = (box[2] + box[5]) * 0.5f;
        float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
        float wmin = wq[0] * cx + wq[1] * cy + wq[2] * cz + wq[3] - fabsf(wq[0]) * hx - fabsf(wq[1]) * hy - fabsf(wq[2]) * hz;
        if (cls[i] == 2) {
            if (!(wmin > near))
                *front = 0;
            continue;
        }
        if (wmin > near && wmin > 0 && wmin >= need * rad[i]) {
            cls[i] = 0;
            --*partial;
            guard_small_chunks++;
        }
    }
}

/* the chunk classes of a cached guarded mesh, computed once a draw: by guard_slot_whole at the call site (then
   draw_guarded_compact reuses them) or inside draw_guarded_compact */
static struct { const void *slot; unsigned key, built; const Matrix *model; float near; const float *box; int n, partial, culled, front; GuardLazy lazy; } guard_pre;
static unsigned char guard_cls[GUARD_MAX_CHUNKS];
static const float *guard_classify_slot(const void *slot, unsigned slot_key, unsigned slot_built, int prim, const unsigned short *idx, int nidx,
                                        const void *vertices, int vtype, const Matrix *model, float near)
{
    if (guard_pre.slot == slot && guard_pre.key == slot_key && guard_pre.built == slot_built && guard_pre.model == model && guard_pre.near == near)
        return guard_pre.box;
    prof_begin(PROF_GCLASS);
    const float *rad = 0;
    int n = 0, partial = 0, culled = 0, front = 0;
    const float *box = guard_slot_boxes(slot, slot_key, slot_built, prim, idx, nidx, vertices, vtype, &n, &rad);
    if (box) {
        guard_matrix_cached(guard_pre.lazy.m, model);
        guard_boxes_classify(box, n, (const float (*)[4])guard_pre.lazy.m, near, guard_cls, &partial, &culled);
        if (p4_guard_small && p4_guard_gnear && rad)
            guard_small_fix(box, rad, n, (const float (*)[4])guard_pre.lazy.m, near, guard_cls, &partial, &front);
    }
    prof_end(PROF_GCLASS);
    guard_pre.slot = slot, guard_pre.key = slot_key, guard_pre.built = slot_built, guard_pre.model = model, guard_pre.near = near;
    guard_pre.box = box, guard_pre.n = n, guard_pre.partial = partial, guard_pre.culled = culled, guard_pre.front = front;
    return box;
}

/* mode 2: a cached guarded mesh none of whose chunks needs the CPU: the caller draws it as a plain hit (CALL) */
static int guard_slot_whole(const void *slot, unsigned key, unsigned built, int prim, const unsigned short *idx, int nidx,
                            const void *vertices, int nverts, int vtype, const Matrix *model, float near)
{
    if (p4_guard_small < 2 || !p4_guard_boxes || !p4_guard_gnear || nverts > MAX_GUARDED)
        return 0;
    if (!guard_classify_slot(slot, key, built, prim, idx, nidx, vertices, vtype, model, near))
        return 0;
    if (guard_pre.partial || (guard_pre.culled && !guard_pre.front))
        return 0; /* draw_guarded_compact reuses the classes */
    guard_pre.slot = 0;
    guard_small_whole++;
    return 1;
}

static void draw_guarded_compact_body(int prim, const unsigned short *idx, int nidx, const void *vertices, int nverts, int vtype,
                                 const Matrix *model, float near, const Material *material, int modulate, const SceneLight *light,
                                 const int *alpha, const void *slot, unsigned slot_key, unsigned slot_built)
{
    prof_begin(PROF_GDRAW);
    int lit = (vtype & GU_NORMAL_8BIT) != 0;
    unsigned any = 1;
    if (slot && p4_guard_boxes && nverts <= MAX_GUARDED &&
        (guard_stamp || (guard_stamp = calloc(MAX_GUARDED, sizeof *guard_stamp)))) {
        unsigned char *cls = guard_cls;
        const float *box = guard_classify_slot(slot, slot_key, slot_built, prim, idx, nidx, vertices, vtype, model, near);
        int n = guard_pre.n, partial = guard_pre.partial, culled = guard_pre.culled;
        GuardLazy lazy = guard_pre.lazy;
        guard_pre.slot = 0;
        if (box) {
            guard_mesh_draws++;
            guard_box_inside += n - partial - culled, guard_box_partial += partial, guard_box_culled += culled;
            if (!partial && !culled) { /* the whole mesh inside the band: as an unguarded hit */
                guard_box_whole++;
                draw_slot_v = vertices, draw_slot_alpha = (int *)alpha;
                draw_mesh_vertices(prim, vtype, nidx, idx, vertices, material, modulate, light);
                draw_slot_v = 0;
            } else {
                if (!++guard_gen) { /* the stamps wrapped: forget them all */
                    memset(guard_stamp, 0, MAX_GUARDED * sizeof *guard_stamp);
                    guard_gen = 1;
                }
                lazy.pos = (const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x));
                lazy.stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
                draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0;
                draw_guarded_chunks(prim, idx, nidx, vertices, vtype, near, material, modulate, light, cls, guard_chunk_now(), &lazy);
                draw_fixed_alpha = 0;
            }
            prof_end(PROF_GDRAW);
            return;
        }
    }
    if (nverts <= MAX_GUARDED) {
        Mat4 m;
        guard_matrix_cached(m, model);
        const V3 *position = (const V3 *)((const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x)));
        prof_begin(PROF_GCLASS);
        any = guard_vfpu(position, nverts, (const float (*)[4])m, near, guard_clip, guard_code, lit ? sizeof(Vertex3DN) : sizeof(Vertex3D)) & GUARD_MASK;
        prof_end(PROF_GCLASS);
        guard_vertices += nverts;
    }
    guard_mesh_draws++;
    if (!any || nverts > MAX_GUARDED) /* nothing crosses (or too big to classify: drawn as without the guard) */
        draw_mesh_vertices(prim, vtype, nidx, idx, vertices, material, modulate, light);
    else {
        draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0; /* cut corners interpolate between kept ones */
        draw_guarded(prim, idx, nidx, vertices, vtype, near, material, modulate, light);
        draw_fixed_alpha = 0;
    }
    prof_end(PROF_GDRAW);
}

/* b236: persistent results of guarded cached meshes. Exact camera/model/geometry/options key; material,
   lighting and textures are applied live by draw_mesh_vertices. Admit after the same key is seen in two frames.
   Buffers are CPU-built, GE-read: eviction waits for the list's finish sequence, never just a camera change. */
int p4_guard_result = 1, p4_guard_result_check = 0; /* b242: on by default (user, 2026-11-14); moving scenes still to be checked with the check switch */
enum { GUARD_RESULT_SLOTS = 128, GUARD_RESULT_BUDGET = 256 * 1024 };
typedef struct {
    const void *slot, *v, *idx;
    unsigned key, built;
    int prim, vtype, nv, ni;
    float near, model[12];
    unsigned camera[18];
    int options[8];
} GuardResultKey;
typedef struct {
    GuardResultKey key;
    void *data;
    unsigned bytes, cut_off, seq, used, seen;
    int nkept, ncut, kept_alpha[2], cut_alpha[2];
} GuardResult;
static GuardResult *guard_results;
static unsigned gr_bytes, gr_hits, gr_misses, gr_builds, gr_pending, gr_refused, gr_checks, gr_diffs, gr_evictions;
static int guard_result_busy(const GuardResult *e)
{
    return e->data && e->seq && (int)(ge_finished - e->seq) < 0;
}
static void guard_result_free(GuardResult *e)
{
    gr_bytes -= e->bytes; free(e->data); memset(e, 0, sizeof *e);
}
static void guard_result_clear(void) /* caller has waited for the GE */
{
    if (guard_results) {
        for (int i = 0; i < GUARD_RESULT_SLOTS; i++) guard_result_free(&guard_results[i]);
        free(guard_results); guard_results = 0;
    }
}
static GuardResult *guard_result_set(const GuardResultKey *k)
{
    unsigned h = (((unsigned)k->slot >> 4) * 2654435761u >> 25) & ~3u;
    return guard_results + h;
}
static GuardResult *guard_result_find(const GuardResultKey *k)
{
    GuardResult *set = guard_result_set(k);
    for (int i = 0; i < 4; i++)
        if (set[i].key.slot == k->slot && !memcmp(&set[i].key, k, sizeof *k)) return &set[i];
    return 0;
}
static void guard_result_store(const GuardResultKey *k, const GuardCapture *cap)
{
    GuardResult *e = guard_result_find(k);
    if (!e) {
        GuardResult *set = guard_result_set(k), *take = 0;
        for (int i = 0; i < 4; i++) {
            GuardResult *p = set + i;
            if (guard_result_busy(p)) continue;
            if (!take || !p->key.slot || (!p->data && p->key.slot == k->slot) || p->used < take->used) take = p;
            if (!p->key.slot) break;
        }
        if (!take) { gr_refused++; return; }
        if (take->data) gr_evictions++;
        guard_result_free(take); take->key = *k; take->seen = take->used = cache_frame;
        gr_pending++; return; /* moving cameras incur no buffer allocations/copies */
    }
    e->used = cache_frame;
    if (e->data || e->seen == cache_frame) return;
    if (cap->nkept > 65535 || cap->ncut > 65535) { gr_refused++; return; }
    unsigned stride = k->vtype & GU_NORMAL_8BIT ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    unsigned off = (cap->nkept * 2u + 15u) & ~15u, bytes = off + cap->ncut * stride;
    if (!bytes) bytes = 16; /* valid empty result */
    if (bytes > GUARD_RESULT_BUDGET) { gr_refused++; return; }
    while (gr_bytes + bytes > GUARD_RESULT_BUDGET) {
        GuardResult *old = 0;
        for (int i = 0; i < GUARD_RESULT_SLOTS; i++) {
            GuardResult *p = guard_results + i;
            if (p != e && p->data && !guard_result_busy(p) && (!old || p->used < old->used)) old = p;
        }
        if (!old) { gr_refused++; return; }
        guard_result_free(old); gr_evictions++;
    }
    void *data = memalign(16, bytes);
    if (!data) { gr_refused++; return; }
    if (cap->nkept) memcpy(data, cap->idx, cap->nkept * 2u);
    if (cap->ncut) memcpy((char *)data + off, cap->cut, cap->ncut * stride);
    sceKernelDcacheWritebackRange(data, bytes);
    e->data = data; e->bytes = bytes; e->cut_off = off; e->nkept = cap->nkept; e->ncut = cap->ncut;
    e->kept_alpha[0] = e->cut_alpha[0] = -1; gr_bytes += bytes; gr_builds++;
}
static void draw_guarded_compact(int prim, const unsigned short *idx, int nidx, const void *vertices, int nverts, int vtype,
                                 const Matrix *model, float near, const Material *material, int modulate, const SceneLight *light,
                                 const int *alpha, const void *slot, unsigned slot_key, unsigned slot_built)
{
    if (!p4_guard_result || !slot || !current_camera || nverts > MAX_GUARDED ||
        (!guard_results && !(guard_results = calloc(GUARD_RESULT_SLOTS, sizeof *guard_results)))) {
        draw_guarded_compact_body(prim, idx, nidx, vertices, nverts, vtype, model, near, material, modulate, light, alpha, slot, slot_key, slot_built);
        return;
    }
    prof_begin(PROF_GDRAW);
    GuardResultKey key; memset(&key, 0, sizeof key);
    key.slot = slot; key.v = vertices; key.idx = idx; key.key = slot_key; key.built = slot_built;
    key.prim = prim; key.vtype = vtype; key.nv = nverts; key.ni = nidx; key.near = near;
    if (model) for (int i = 0; i < 4; i++) memcpy(key.model + i * 3, (const char *)model + i * 16, 12);
    else key.model[0] = key.model[4] = key.model[8] = 1;
    guard_world_planes(); memcpy(key.camera, guard_world.key, sizeof key.camera);
    int opts[8] = {p4_guard_boxes, p4_guard_gnear, p4_guard_near_ge, p4_guard_clip_fast,
                   p4_guard_small, p4_guard_chunk, p4_guard_loop2, p4_guard_box_vfpu};
    memcpy(key.options, opts, sizeof opts);
    GuardResult *e = guard_result_find(&key);
    if (e && e->data && !p4_guard_result_check) {
        gr_hits++; guard_mesh_draws++; e->used = cache_frame; e->seq = ge_queued + 1;
        const void *saved_v = draw_slot_v; int *saved_alpha = draw_slot_alpha;
        const int *saved_fixed = draw_fixed_alpha;
        draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0;
        prof_begin(PROF_GSUB);
        if (e->nkept) {
            draw_slot_v = vertices; draw_slot_alpha = e->kept_alpha;
            draw_mesh_vertices(GU_TRIANGLES, vtype, e->nkept, e->data, vertices, material, modulate, light);
        }
        if (e->ncut) {
            const void *cut = (char *)e->data + e->cut_off;
            draw_slot_v = cut; draw_slot_alpha = e->cut_alpha;
            draw_mesh_vertices(GU_TRIANGLES, vtype & ~GU_INDEX_16BIT, e->ncut, 0, cut, material, modulate, light);
        }
        prof_end(PROF_GSUB);
        draw_slot_v = saved_v; draw_slot_alpha = saved_alpha; draw_fixed_alpha = saved_fixed;
        guard_pre.slot = 0;
        prof_end(PROF_GDRAW); return;
    }
    gr_misses++;
    GuardCapture cap = {0}, *saved_capture = guard_capture; guard_capture = &cap;
    draw_guarded_compact_body(prim, idx, nidx, vertices, nverts, vtype, model, near, material, modulate, light, alpha, slot, slot_key, slot_built);
    guard_capture = saved_capture;
    if (e && e->data && p4_guard_result_check) {
        unsigned stride = vtype & GU_NORMAL_8BIT ? sizeof(Vertex3DN) : sizeof(Vertex3D);
        int bad = !cap.valid || cap.nkept != e->nkept || cap.ncut != e->ncut;
        if (!bad && cap.nkept) bad = memcmp(cap.idx, e->data, cap.nkept * 2u) != 0;
        if (!bad && cap.ncut) bad = memcmp(cap.cut, (char *)e->data + e->cut_off, cap.ncut * stride) != 0;
        gr_checks++; gr_diffs += bad;
        if (bad && gr_diffs <= 8) p4_log("TRACE guard result mismatch slot %p kept %d/%d cut %d/%d\n", slot, cap.nkept, e->nkept, cap.ncut, e->ncut);
    } else if (cap.valid) guard_result_store(&key, &cap);
    prof_end(PROF_GDRAW);
}
static void guard_result_report(void)
{
    p4_log("guard result %d check %d hits %u misses %u builds %u pending %u refused %u evictions %u bytes %u checks %u diffs %u\n",
           p4_guard_result, p4_guard_result_check, gr_hits, gr_misses, gr_builds, gr_pending, gr_refused, gr_evictions, gr_bytes, gr_checks, gr_diffs);
    gr_hits = gr_misses = gr_builds = gr_pending = gr_refused = gr_evictions = gr_checks = gr_diffs = 0;
}

/* On the PS2 0x80 is full brightness in a vertex colour. */
/* 1: vertex alpha doubled like RGB (0x80 = opaque); 0: alpha already 0-255, as the dream's fog planes look on the PS2 */
int p4_alpha_x2 = 0;
static inline unsigned double_colour(unsigned c)
{
    unsigned r = (c & 0xff) * 2, g = (c >> 8 & 0xff) * 2, b = (c >> 16 & 0xff) * 2, a = (c >> 24) << p4_alpha_x2;
    return (r > 255 ? 255 : r) | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b) << 16 | (a > 255 ? 255u : a) << 24;
}

static unsigned modulate_colour(unsigned vertex, unsigned material)
{
    if (material == 0xffffffff)
        return vertex;
    unsigned result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        unsigned t = (vertex >> shift & 255) * (material >> shift & 255) + 128; /* round(a * b / 255) */
        result |= ((t + (t >> 8)) >> 8) << shift;
    }
    return result;
}

static inline V3 rotate(const V3 *v, const Matrix *m);
const Matrix *p4r_003e9700(void *frame);

static unsigned shade_colour(unsigned base, const V3 *normal, const Matrix *model, const Material *material, const SceneLight *light)
{
    V3 n = model ? rotate(normal, model) : *normal;
    float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    float diffuse = length > 0 ? -(n.x * light->direction.x + n.y * light->direction.y + n.z * light->direction.z) / length : 0;
    if (diffuse < 0) diffuse = 0;
    if (diffuse > 1) diffuse = 1;
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    unsigned colour = base & 0xff000000;
    for (int channel = 0; channel < 3; channel++) {
        float value = (base >> (channel * 8) & 255) + 255 *
            (material->ambient * ambient[channel] + material->diffuse * diffuse * directional[channel]);
        colour |= byte_colour(value) << (channel * 8);
    }
    return colour;
}

/* shade_colour with the light turned into the model's space once: the model matrices here are rotations with one
   uniform scale, so the normal's length after them is that scale. b68 profile: 367 cycles a vertex, mostly sqrt/div. */
typedef struct { float lx, ly, lz, ambient[3], directional[3]; } ShadeSetup;
static void shade_setup(ShadeSetup *k, const Matrix *model, const Material *material, const SceneLight *light)
{
    const V3 *l = &light->direction;
    float lx = l->x, ly = l->y, lz = l->z;
    if (model) {
        float scale = sqrtf(model->right.x * model->right.x + model->right.y * model->right.y + model->right.z * model->right.z);
        float inv = scale > 0 ? 1.0f / scale : 0;
        lx = (model->right.x * l->x + model->right.y * l->y + model->right.z * l->z) * inv;
        ly = (model->up.x * l->x + model->up.y * l->y + model->up.z * l->z) * inv;
        lz = (model->at.x * l->x + model->at.y * l->y + model->at.z * l->z) * inv;
    }
    k->lx = -lx; k->ly = -ly; k->lz = -lz;
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    for (int c = 0; c < 3; c++) {
        k->ambient[c] = 255 * material->ambient * ambient[c];
        k->directional[c] = 255 * material->diffuse * directional[c];
    }
}

static inline unsigned shade_fast(unsigned base, const V3 *n, const ShadeSetup *k)
{
    float d = n->x * k->lx + n->y * k->ly + n->z * k->lz;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    unsigned colour = base & 0xff000000;
    for (int c = 0; c < 3; c++) {
        float value = (base >> (c * 8) & 255) + k->ambient[c] + d * k->directional[c];
        colour |= byte_colour(value) << (c * 8);
    }
    return colour;
}

/* RenderWare's lighting from GE constants per mesh: the vertex colour (doubled prelight, or black) is the material's
   ambient and the global ambient is the material colour, so prelight x colour; emissive is colour x ka x the ambient
   light; light 0 adds colour x kd x the directional light x N.L. Differs from shade_fast + modulate_colour only where
   a channel saturates: the GE clamps after the material colour, the CPU path before it. */
static void bind_ge_light_body(const Material *material, int modulate, const SceneLight *light);
static void bind_ge_light(const Material *material, int modulate, const SceneLight *light)
{
    unsigned kept = kept_lights;
    prof_begin(PROF_LBIND);
    bind_ge_light_body(material, modulate, light);
    prof_end(PROF_LBIND);
    lights_bound += kept_lights == kept;
}
static void bind_ge_light_body(const Material *material, int modulate, const SceneLight *light)
{
    unsigned c = modulate ? material->colour : 0xffffffff, emissive = 0, diffuse = 0;
    if (gu_mesh_mode) { /* same colour, material terms and light as the last lit draw: only switch lighting on */
        unsigned key[13];
        key[0] = c;
        memcpy(key + 1, &material->ambient, 4);
        memcpy(key + 2, &material->diffuse, 4);
        memcpy(key + 3, light, 9 * 4);
        key[12] = 0;
        if (gs.light_known && !memcmp(key, gs.light_key, sizeof key)) {
            kept_lights++;
            st_enable(GU_LIGHTING, 1);
            return;
        }
        memcpy(gs.light_key, key, sizeof key);
        gs.light_known = 1;
    }
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    for (int k = 0; k < 3; k++) {
        float mc = c >> (k * 8) & 255;
        emissive |= byte_colour(mc * material->ambient * ambient[k] + 0.5f) << (k * 8);
        diffuse |= byte_colour(mc * material->diffuse * directional[k] + 0.5f) << (k * 8);
    }
    const V3 *d = &light->direction;
    st_enable(GU_LIGHTING, 1);
    st_enable(GU_LIGHT1, 0);
    st_enable(GU_LIGHT2, 0);
    st_enable(GU_LIGHT3, 0);
    sceGuColorMaterial(GU_AMBIENT);
    sceGuAmbient(c);
    sceGuModelColor(emissive, 0xffffffff, 0xffffffff, 0);
    if (diffuse && (d->x || d->y || d->z)) { /* a zero direction would normalise to NaN */
        ScePspFVector3 to = { -d->x, -d->y, -d->z };
        sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &to);
        sceGuLightColor(0, GU_DIFFUSE, diffuse);
        sceGuLightColor(0, GU_AMBIENT, 0);
        st_enable(GU_LIGHT0, 1);
    } else {
        st_enable(GU_LIGHT0, 0);
    }
}

/* A unit normal's component as s8 (127 = 1). */
static inline signed char normal_s8(float c)
{
    float s = c * 127.0f;
    return s >= 127 ? 127 : s <= -127 ? -127 : (signed char)(s + (s >= 0 ? 0.5f : -0.5f));
}

/* Vertex j of an array as the GE lights it: doubled prelight (or black) as the material's ambient, unit normal in s8. */
static inline void put_lit_vertex(Vertex3DN *v, const V3 *position, float (*uv)[2], float du, float dv, const unsigned *preLit, const V3 *normals, int j)
{
    const V3 *n = &normals[j];
    float len = sqrtf(n->x * n->x + n->y * n->y + n->z * n->z), inv = len > 0 ? 1.0f / len : 0;
    v->u = uv ? uv[j][0] - du : 0;
    v->v = uv ? uv[j][1] - dv : 0;
    v->colour = preLit ? double_colour(preLit[j]) : 0xff000000;
    v->nx = normal_s8(n->x * inv);
    v->ny = normal_s8(n->y * inv);
    v->nz = normal_s8(n->z * inv);
    v->pad = 0;
    v->x = position[j].x;
    v->y = position[j].y;
    v->z = position[j].z;
}

/* draw_tested of a compact or cached mesh; vtype with normals means GE-lit */
/* b165 census of 3D draws (p4_probe_3d, poke it live): per material, its texture name, blend, colour, flags and the
   words after the material (RenderWare plugin data such as MatFX), to tell the arena floor, chests and Shadows. */
int p4_probe_3d = 0;
unsigned p4_hide_material; /* debug: this material is not drawn (identify a surface by hiding it) */
typedef struct { const Material *material; unsigned blend, test, vtype, calls, triangles; int modulate; unsigned mask; } Census3D;
enum { CENSUS3D = 160 };
static Census3D census3d[CENSUS3D];
static void census3d_note(const Material *material, int vtype, int count, int modulate)
{
    for (int i = 0; i < CENSUS3D; i++) {
        Census3D *e = &census3d[i];
        if (e->calls && (e->material != material || e->blend != (sky_state[2] & 0xff) || e->test != sky_state[3]))
            continue;
        if (!e->calls)
            *e = (Census3D){ material, sky_state[2] & 0xff, sky_state[3], vtype, 0, 0, modulate, pixel_mask };
        e->calls++;
        e->triangles += count / 3;
        return;
    }
}
void p4_census3d_report(void)
{
    if (!p4_probe_3d)
        return;
    for (int i = 0; i < CENSUS3D && census3d[i].calls; i++) {
        const Census3D *e = &census3d[i];
        const unsigned *m = (const unsigned *)e->material;
        const char *texture = m && m[0] ? (const char *)m[0] + 0x10 : "-";
        p4_log("census3d %08x %-20.20s blend %02x test %05x colour %08x mod %d vtype %x draws %u tris %u plugin %08x %08x %08x %08x\n",
               (unsigned)e->material, texture, e->blend, e->test & 0xfffff, m ? m[1] : 0, e->modulate, e->vtype, e->calls, e->triangles,
               m ? m[6] : 0, m ? m[7] : 0, m ? m[8] : 0, m ? m[9] : 0);
    }
    memset(census3d, 0, sizeof census3d);
}

/* Census and the debug hide for every 3D path (material draws, CPU-vertex meshes, skinned batches). */
static int census3d_skip(const Material *material, int vtype, int count, int modulate)
{
    if (p4_probe_3d)
        census3d_note(material, vtype, count, modulate);
    return p4_hide_material && (unsigned)material == p4_hide_material;
}

static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices, const void *vertices, const Material *material, int modulate, const SceneLight *light)
{
    if (census3d_skip(material, vtype, count, modulate))
        return;
    int ge = (vtype & GU_NORMAL_8BIT) || draw_material_ge;
    prof_begin(PROF_DLIGHT);
    if (ge) { /* draw_material_ge: unlit, the light is zero; the material colour comes as the GE's ambient */
        bind_ge_light(material, modulate, light);
        work_ge_draws++;
    }
    prof_end(PROF_DLIGHT);
    draw_material_alpha = modulate ? material->colour >> 24 : 255;
    prof_begin(PROF_DDRAW);
    draw_tested(prim, vtype, count, indices, vertices);
    prof_end(PROF_DDRAW);
    if (ge)
        st_enable(GU_LIGHTING, 0);
}

static SceneLight scene_lights(void)
{
    SceneLight out = {0};
    unsigned char *ambient = (void *)R32(0x764b64), *directional = (void *)R32(0x764b68);
    if (ambient && ambient[1] == 2 && (ambient[2] & 1))
        memcpy(&out.ambient, ambient + 24, sizeof(V3));
    if (directional && directional[1] == 1 && (directional[2] & 1)) {
        void *frame = *(void **)(directional + 4);
        if (frame) {
            memcpy(&out.diffuse, directional + 24, sizeof(V3));
            out.direction = p4r_003e9700(frame)->at;
        }
    }
    return out;
}

/* One vertex-array object: an atomic's geometry, or a world sector with no transform of its own. */
/* Per-mesh compact arrays kept between frames while what they are built from stays the same (same arrays and
   contents, light, model rotation, material). A mesh drawn twice in one frame with other inputs builds the second
   copy in the frame pool: the GE may still be reading the first. Budget-capped; full means build per frame. */
int p4_shade_fast = 1; /* 0: shade_colour per vertex, as b68 */
enum { CACHE_SLOTS = 4096 };
typedef struct { const void *mesh; unsigned key, frame, built; int vertices, indices, capacity, churn, vtype; void *v; unsigned short *idx; int alpha_lo, alpha_hi; } MeshCache;
static MeshCache mesh_cache[CACHE_SLOTS];
/* b232: which copies are split (a bit each: an int in MeshCache pushed .bss into the data image) */
static unsigned mesh_split_bits[CACHE_SLOTS / 32];
#define SLOT_SPLIT(e) ((mesh_split_bits[((e) - mesh_cache) >> 5] >> (((e) - mesh_cache) & 31)) & 1u)
static void mesh_set_split(MeshCache *e, int on)
{
    unsigned slot = e - mesh_cache, bit = 1u << (slot & 31);
    mesh_split_bits[slot >> 5] = (mesh_split_bits[slot >> 5] & ~bit) | (on ? bit : 0);
}
/* b232 p4_guard_split: big triangles of a guarded mesh split when its compact copy is cached. The GE has no X/Y clip:
   a triangle with a corner past the 8x guard band is dropped whole, so a mesh that crosses the band is cut on the CPU
   every frame (b230 dialogue: 27 meshes, 5.3 ms). p4_guard_small lets a chunk go to the GE uncut when its triangles
   are small next to their distance; with big floor and wall triangles none qualified (b200: 0 chunks). Here every
   edge longer than p4_guard_split model units is halved at its midpoint, recursively, longest first. An edge is
   split by its length alone and its midpoint is shared through a map, so both triangles on an edge split it the same
   way: no T-junctions. Corners interpolate position, UV, colour (per byte) and normal. Strips become a list. At most
   p4_guard_split_grow x the triangles, 65535 indices and 32 K vertices, else the mesh stays as it was.
   The per-draw guard_small test must still prove each chunk safe; splitting alone never bypasses clipping. 0: off. */
int p4_guard_split = 0, p4_guard_split_grow = 8;
typedef struct { unsigned key; int v; } SplitEdge;
static int split_mid(SplitEdge *map, int cap, int a, int b, char *verts, int *nv, int maxv, int stride, int lit)
{
    unsigned key = a < b ? (unsigned)a << 16 | b : (unsigned)b << 16 | a;
    unsigned h = key * 2654435761u & (cap - 1);
    while (map[h].key && map[h].key != key + 1)
        h = (h + 1) & (cap - 1);
    if (map[h].key)
        return map[h].v;
    if (*nv >= maxv)
        return -1;
    int m = (*nv)++;
    char *va = verts + a * stride, *vb = verts + b * stride, *vm = verts + m * stride;
    if (lit) {
        const Vertex3DN *x = (const Vertex3DN *)va, *y = (const Vertex3DN *)vb;
        Vertex3DN *o = (Vertex3DN *)vm;
        o->u = (x->u + y->u) * 0.5f, o->v = (x->v + y->v) * 0.5f;
        o->nx = (signed char)((x->nx + y->nx) / 2), o->ny = (signed char)((x->ny + y->ny) / 2), o->nz = (signed char)((x->nz + y->nz) / 2), o->pad = 0;
        o->x = (x->x + y->x) * 0.5f, o->y = (x->y + y->y) * 0.5f, o->z = (x->z + y->z) * 0.5f;
    } else {
        const Vertex3D *x = (const Vertex3D *)va, *y = (const Vertex3D *)vb;
        Vertex3D *o = (Vertex3D *)vm;
        o->u = (x->u + y->u) * 0.5f, o->v = (x->v + y->v) * 0.5f;
        o->x = (x->x + y->x) * 0.5f, o->y = (x->y + y->y) * 0.5f, o->z = (x->z + y->z) * 0.5f;
    }
    unsigned ca = *(const unsigned *)(va + 8), cb = *(const unsigned *)(vb + 8), cm = 0;
    for (int k = 0; k < 32; k += 8)
        cm |= (((ca >> k & 0xff) + (cb >> k & 0xff) + 1) >> 1) << k;
    *(unsigned *)(vm + 8) = cm;
    map[h].key = key + 1, map[h].v = m;
    return m;
}
static float split_len2(const char *verts, int stride, int lit, int a, int b)
{
    const float *p = (const float *)(verts + a * stride + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x)));
    const float *q = (const float *)(verts + b * stride + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x)));
    float dx = p[0] - q[0], dy = p[1] - q[1], dz = p[2] - q[2];
    return dx * dx + dy * dy + dz * dz;
}
/* returns a memalign'd buffer: vertices, then the u16 list indices; 0 when nothing is split or it does not fit */
static void *guard_split_mesh(const void *v, int nv, int stride, int lit, const unsigned short *idx, int nidx, int strip,
                              int *out_nv, int *out_nidx, int *out_bytes)
{
    if (p4_guard_split <= 0 || nv <= 0 || nv > MAX_GUARDED || nidx < 3)
        return 0;
    unsigned t0 = sceKernelGetSystemTimeLow();
    float lim2 = (float)p4_guard_split * (float)p4_guard_split;
    int ntri = strip ? nidx - 2 : nidx / 3;
    if (ntri > 21845)
        return 0;
    int big = 0;
    for (int t = 0; t < ntri && !big; t++) {
        int a = strip ? idx[t] : idx[t * 3], b = strip ? idx[t + 1] : idx[t * 3 + 1], c = strip ? idx[t + 2] : idx[t * 3 + 2];
        if (a == b || b == c || a == c)
            continue;
        big = split_len2(v, stride, lit, a, b) > lim2 || split_len2(v, stride, lit, b, c) > lim2 || split_len2(v, stride, lit, c, a) > lim2;
    }
    if (!big)
        return 0;
    int grow = p4_guard_split_grow < 2 ? 2 : p4_guard_split_grow > 64 ? 64 : p4_guard_split_grow;
    /* GE PRIM has a 16-bit count, including when a cached mesh is submitted whole. */
    int maxt = ntri > 21845 / grow ? 21845 : ntri * grow, maxv = nv + maxt;
    if (maxv > MAX_GUARDED)
        maxv = MAX_GUARDED;
    int cap = 1;
    while (cap < 2 * (maxv - nv) + 16)
        cap <<= 1;
    char *verts = malloc(maxv * stride);
    unsigned short *tris = malloc(maxt * 3 * sizeof *tris);
    SplitEdge *map = calloc(cap, sizeof *map);
    int (*stack)[3] = malloc(64 * sizeof *stack);
    int nverts = nv, nt = 0, ok = verts && tris && map && stack;
    if (ok)
        memcpy(verts, v, nv * stride);
    for (int t = 0; t < ntri && ok; t++) {
        int a, b, c;
        if (strip) {
            a = idx[t], b = idx[t + 1], c = idx[t + 2];
            if (t & 1) { int x = a; a = b; b = x; } /* the GE's alternating winding, as a list */
        } else
            a = idx[t * 3], b = idx[t * 3 + 1], c = idx[t * 3 + 2];
        if (a == b || b == c || a == c)
            continue;
        int sp = 0;
        stack[sp][0] = a, stack[sp][1] = b, stack[sp][2] = c, sp++;
        while (sp && ok) {
            sp--;
            int x = stack[sp][0], y = stack[sp][1], z = stack[sp][2];
            float e0 = split_len2(verts, stride, lit, x, y), e1 = split_len2(verts, stride, lit, y, z), e2 = split_len2(verts, stride, lit, z, x);
            int e = -1;
            float best = lim2;
            if (e0 > best) best = e0, e = 0;
            if (e1 > best) best = e1, e = 1;
            if (e2 > best) best = e2, e = 2;
            if (e < 0) {
                if (nt >= maxt) { ok = 0; break; }
                tris[nt * 3] = x, tris[nt * 3 + 1] = y, tris[nt * 3 + 2] = z, nt++;
                continue;
            }
            if (sp + 2 > 64) { ok = 0; break; }
            int p = e == 0 ? x : e == 1 ? y : z, q = e == 0 ? y : e == 1 ? z : x, r = e == 0 ? z : e == 1 ? x : y; /* edge p-q, opposite r */
            int m = split_mid(map, cap, p, q, verts, &nverts, maxv, stride, lit);
            if (m < 0) { ok = 0; break; }
            stack[sp][0] = p, stack[sp][1] = m, stack[sp][2] = r, sp++;
            stack[sp][0] = m, stack[sp][1] = q, stack[sp][2] = r, sp++;
        }
    }
    void *out = 0;
    if (ok && nt > 0) {
        int bytes = nverts * stride + nt * 3 * 2;
        if ((out = memalign(16, bytes))) {
            memcpy(out, verts, nverts * stride);
            memcpy((char *)out + nverts * stride, tris, nt * 3 * 2);
            *out_nv = nverts, *out_nidx = nt * 3, *out_bytes = bytes;
            split_meshes++, split_tris_in += ntri, split_tris_out += nt;
        }
    } else
        split_refused++;
    free(verts), free(tris), free(map), free(stack);
    split_us += sceKernelGetSystemTimeLow() - t0;
    return out;
}
int p4_material_state = 1; /* b177: 1: GE-lit cache keys without the material; 2: unlit compact meshes too (material
                              colour as GE state); 0: the material in every cache key, as b176 */
int p4_cache_reuse_last = 1; /* 1: a miss may take its mesh's entry drawn last frame (pipeline off); 0: as b75 */
static Geometry *draw_geometry; /* the atomic's geometry while its own arrays are drawn, else 0 */
typedef struct { const Geometry *g; const void *pos, *uv, *pre, *nrm; int count; float du, dv; unsigned nsample, part[4]; } LockMemo;
static LockMemo lock_memo[1024];

/* Why a mesh's cache entry stops matching: the first input whose running hash differs (pointers, positions, UVs,
   prelight, then count + UV shift, then lighting and normals), vertices weighted. Heap: .bss is full. */
static unsigned (*churn_parts)[6];
static unsigned churn_count[7], churn_vertices[7];
static const char *const churn_names[7] = { "pointers", "positions", "uvs", "prelight", "shift", "light/normals", "material" };
static void churn_remember(int index, const unsigned part[6])
{
    if (!churn_parts)
        churn_parts = calloc(CACHE_SLOTS, sizeof *churn_parts);
    if (churn_parts)
        memcpy(churn_parts[index], part, sizeof churn_parts[index]);
}
/* b178: which geometries rebuild for their positions (battle: ~15 meshes, 29k vertices a frame), and what moves them:
   CPU skinning (a new array every frame), a lock (the game rewrote the morph target), or neither (written unlocked) */
int p4_churn_top = 1;
static Geometry *churn_atomic_g; /* the atomic being drawn (also when skinned on the CPU), else 0 */
static int churn_atomic_locked, churn_atomic_skinned;
typedef struct { const void *header; const Geometry *g; unsigned meshes, vertices, locked, skinned, flags, morphs, nverts, frames, last; char tex[24]; } ChurnTop;
static ChurnTop churn_top[32];
static void churn_top_note(const void *header, const Material *material, int vertices)
{
    ChurnTop *e = 0, *low = &churn_top[0];
    for (int i = 0; i < 32; i++) {
        if (churn_top[i].header == header) { e = &churn_top[i]; break; }
        if (churn_top[i].vertices < low->vertices) low = &churn_top[i];
    }
    if (!e) {
        e = low;
        memset(e, 0, sizeof *e);
        e->header = header;
        e->g = churn_atomic_g;
        if (churn_atomic_g)
            e->flags = churn_atomic_g->flags, e->morphs = churn_atomic_g->numMorphTargets, e->nverts = churn_atomic_g->numVertices;
        void **texture = material ? material->texture : 0;
        if (texture && *texture)
            memcpy(e->tex, (const char *)*texture + 0x10, 23);
        e->last = ~0u;
    }
    e->meshes++;
    e->vertices += vertices;
    if (e->last != cache_frame) {
        e->last = cache_frame;
        e->frames++;
        e->locked += churn_atomic_locked != 0;
        e->skinned += churn_atomic_skinned;
    }
}
static void churn_top_report(unsigned frames)
{
    for (int n = 0; n < 12; n++) {
        ChurnTop *best = 0;
        for (int i = 0; i < 32; i++)
            if (churn_top[i].header && churn_top[i].vertices && (!best || churn_top[i].vertices > best->vertices))
                best = &churn_top[i];
        if (!best)
            break;
        p4_log("churn top geometry %p header %p v%u flags %x morphs %u: frames %u/%u vertices %u per frame meshes %u locked %u skinned %u tex %.23s\n",
               best->g, best->header, best->nverts, best->flags, best->morphs, best->frames, frames, best->vertices / frames, best->meshes,
               best->locked, best->skinned, best->tex);
        best->vertices = 0;
    }
    memset(churn_top, 0, sizeof churn_top);
}
static int churn_why(int index, const unsigned part[6], int vertices)
{
    if (!churn_parts)
        return -1;
    int k = 0;
    while (k < 6 && churn_parts[index][k] == part[k])
        k++;
    churn_count[k]++;
    churn_vertices[k] += vertices;
    return k;
}
static void churn_report(unsigned frames)
{
    for (int k = 0; k < 7; k++)
        if (churn_count[k])
            p4_log("churn %s: %u meshes %u vertices per frame (material_state %d)\n", churn_names[k], churn_count[k] / frames, churn_vertices[k] / frames, p4_material_state);
    memset(churn_count, 0, sizeof churn_count);
    memset(churn_vertices, 0, sizeof churn_vertices);
}

/* may_alias: callers hash float arrays and structs (the UV shift, the light, matrices). Read through a plain unsigned *,
   GCC -O2 may load the words before the caller's float stores (strict aliasing): b104-b108 hashed stale stack words, so
   ~100 cache keys a frame changed at random (Dojima's room 50 -> 75 ms, Heaven differing by 18 ms between builds). */
typedef unsigned __attribute__((may_alias)) AliasWord;
static unsigned hash_words(unsigned h, const void *data, int bytes)
{
    const AliasWord *w = data;
    for (int i = 0; i < bytes / 4; i++)
        h = (h ^ w[i]) * 16777619u;
    return h;
}

/* Audit of the sampled key (b105): the key hashes 8 elements per array, so an array rewritten elsewhere in place
   (scrolling UVs of part of a mesh) can hit with old contents. Every 8th hit rehashes the whole arrays and compares
   them with the hashes taken when the entry was built; a difference is a stale hit (drawn with old vertices). */
static unsigned (*audit_hash)[3], audit_tick;
static void audit_inputs(unsigned out[3], const V3 *position, float (*uv)[2], const unsigned *preLit, int count)
{
    out[0] = hash_words(2166136261u, position, count * sizeof *position);
    out[1] = uv ? hash_words(2166136261u, uv, count * 8) : 0;
    out[2] = preLit ? hash_words(2166136261u, preLit, count * 4) : 0;
}

/* One entry per (mesh, inputs): the limousine draws ~30 meshes twice a frame with different inputs (b69: they rebuilt
   every frame). A miss takes an empty entry or one unused for two seconds. */
static MeshCache *cache_slot(const void *mesh, unsigned key, int *hit)
{
    /* entries of one mesh sit together; a miss reuses this mesh's entry that was not drawn this frame (its inputs
       changed), so animated meshes do not fill the budget with dead copies (b71: 1.5 MB full, 38 misses a frame) */
    unsigned i = ((unsigned)mesh >> 2) * 2654435761u >> 20; /* meshes are 12 bytes apart: >> 4 put a geometry's meshes in one 16-entry window (b87 Heaven: 60 rebuilds a frame) */
    MeshCache *same = 0, *spare = 0;
    for (int probe = 0; probe < 16; probe++, i = (i + 1) & (CACHE_SLOTS - 1)) {
        MeshCache *e = &mesh_cache[i];
        if (e->mesh == mesh && e->key == key) {
            *hit = 1;
            return e;
        }
        /* Drawn this frame: the GE has not read it yet. Last frame: only the pipeline still reads it. Excluding last
           frame always (as b75) gave an animated mesh a new entry every frame: ~120 dead copies each, the 4 MB
           budget full in the first dialogue and battle (b76), and churn never saw the same entry come back. */
        int busy = e->frame == cache_frame || ((p4_pipeline || !p4_cache_reuse_last) && e->frame + 1 == cache_frame);
        if (!same && e->mesh == mesh && !busy)
            same = e;
        if (!spare && (!e->mesh || cache_frame - e->frame > 120))
            spare = e;
    }
    *hit = 0;
    return same ? same : spare;
}

/* Contents change in place (scrolling UVs, skinned vertices): a few elements spread over the array stand for it. */
static unsigned hash_sample(unsigned h, const void *data, int count, int bytes)
{
    if (count <= 0)
        return h;
    for (int k = 0; k < 8; k++)
        h = hash_words(h, (const char *)data + (count - 1) * k / 7 * bytes, bytes);
    return h;
}

/* b84: 0 = original path, 1 = count only, 2 = reject offscreen meshes.
   Codes are rebuilt from the current positions, including CPU-skinned positions;
   no cached bounds can become stale when geometry changes in place. GE-skinned
   batches keep their existing path and are not included in these counters. */
int p4_mesh_frustum = 0;
static unsigned char *frustum_codes;
static unsigned frustum_arrays, frustum_vertices, frustum_meshes, frustum_outside, frustum_indices, frustum_us;
static int frustum_prepare(const V3 *position, int count, const Matrix *model)
{
    if (!p4_mesh_frustum || count <= 0 || count > MAX_GUARDED)
        return 0;
    if (!frustum_codes && !(frustum_codes = malloc(MAX_GUARDED)))
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    Mat4 m;
    guard_matrix(m, model);
    for (int i = 0; i < count; i++)
        frustum_codes[i] = frustum_code(m, &position[i].x);
    frustum_arrays++;
    frustum_vertices += count;
    frustum_us += sceKernelGetSystemTimeLow() - start;
    return 1;
}
static int frustum_reject(const Mesh *mesh, int count, int prepared)
{
    if (!prepared)
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    int outside = frustum_mesh_outside(frustum_codes, count, mesh->indices, mesh->numIndices);
    frustum_meshes++;
    if (outside) {
        frustum_outside++;
        frustum_indices += mesh->numIndices;
    }
    frustum_us += sceKernelGetSystemTimeLow() - start;
    return outside && p4_mesh_frustum == 2;
}
static void frustum_report(unsigned frames)
{
    p4_log("mesh frustum mode %d per frame: arrays %u vertices %u meshes %u outside %u outside_indices %u scan_us %u (GE skin excluded)\n",
           p4_mesh_frustum, frustum_arrays / frames, frustum_vertices / frames, frustum_meshes / frames,
           frustum_outside / frames, frustum_indices / frames, frustum_us / frames);
    frustum_arrays = frustum_vertices = frustum_meshes = frustum_outside = frustum_indices = frustum_us = 0;
}

/* Depth experiments use the ACTUAL indexed vertices in the current view, not
   an atomic's enormous sphere. 0 off; 1 count only; 2 hide wholly farther;
   3 hide wholly nearer. Meshes crossing the cut are retained in both modes.
   This diagnostic scan is intentionally identical in count and reject modes.
   It covers draw_meshes, not the separate GE-skinned batch path. */
int p4_probe_depth_mode;
float p4_probe_depth_cut = 2000;
static unsigned probe_depth_bins[6], probe_depth_meshes, probe_depth_hidden, probe_depth_indices, probe_depth_us;
static int probe_depth_reject(const Mesh *mesh, const V3 *position, int count, const Matrix *model)
{
    if (!p4_probe_depth_mode || !mesh->indices || mesh->numIndices <= 0)
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    const Matrix *view = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    float ax = view->right.z, ay = view->up.z, az = view->at.z, offset = view->pos.z;
    if (model) {
        offset += model->pos.x*ax + model->pos.y*ay + model->pos.z*az;
        float x = model->right.x*ax + model->right.y*ay + model->right.z*az;
        float y = model->up.x*ax + model->up.y*ay + model->up.z*az;
        az = model->at.x*ax + model->at.y*ay + model->at.z*az;
        ax = x, ay = y;
    }
    float lo = INFINITY, hi = -INFINITY;
    for (int k = 0; k < mesh->numIndices; k++) {
        unsigned j = mesh->indices[k];
        if (j >= count)
            return 0;
        const V3 *p = &position[j];
        float z = p->x*ax + p->y*ay + p->z*az + offset;
        if (!isfinite(z))
            return 0;
        if (z < lo) lo = z;
        if (z > hi) hi = z;
    }
    int bin = lo < 500 ? 0 : lo < 1000 ? 1 : lo < 2000 ? 2 : lo < 4000 ? 3 : lo < 8000 ? 4 : 5;
    probe_depth_bins[bin]++;
    probe_depth_meshes++;
    int hide = (p4_probe_depth_mode == 2 && lo > p4_probe_depth_cut) ||
               (p4_probe_depth_mode == 3 && hi < p4_probe_depth_cut);
    if (hide) {
        probe_depth_hidden++;
        probe_depth_indices += mesh->numIndices;
    }
    probe_depth_us += sceKernelGetSystemTimeLow() - start;
    return hide;
}
static void probe_report(unsigned frames)
{
    p4_log("probe flags %x geometry %x keep_every %d selected %u hidden %u per frame\n",
           p4_probe_flags, p4_probe_geometry, p4_probe_keep_every, probe_selected / frames, probe_hidden / frames);
    p4_log("probe depth mode %d cut %d per frame: meshes %u hidden %u indices %u scan_us %u bins_minz_500_1000_2000_4000_8000_rest %u %u %u %u %u %u (GE skin excluded)\n",
           p4_probe_depth_mode, (int)p4_probe_depth_cut, probe_depth_meshes / frames, probe_depth_hidden / frames,
           probe_depth_indices / frames, probe_depth_us / frames, probe_depth_bins[0] / frames, probe_depth_bins[1] / frames,
           probe_depth_bins[2] / frames, probe_depth_bins[3] / frames, probe_depth_bins[4] / frames, probe_depth_bins[5] / frames);
    probe_selected = probe_hidden = 0;
    probe_depth_meshes = probe_depth_hidden = probe_depth_indices = probe_depth_us = 0;
    memset(probe_depth_bins, 0, sizeof probe_depth_bins);
}

int p4_mesh_compact = 1; /* 0: every mesh rebuilds the whole array, as b66 */
static unsigned *mesh_lit, *mesh_stamp, mesh_generation;
static unsigned short *mesh_remap;
static int mesh_scratch(int count)
{
    if (!mesh_lit) {
        mesh_lit = malloc(MAX_GUARDED * sizeof *mesh_lit);
        mesh_stamp = calloc(MAX_GUARDED, sizeof *mesh_stamp);
        mesh_remap = malloc(MAX_GUARDED * sizeof *mesh_remap);
        if (!mesh_lit || !mesh_stamp || !mesh_remap) {
            p4_log("mesh scratch failed, compact meshes off\n");
            p4_mesh_compact = 0;
            return 0;
        }
    }
    return count <= MAX_GUARDED;
}

#include "mesh_topology.h"
#include "meshkit.h"
#include <stdio.h>
int p4_topology_cache = 1;
unsigned p4_topology_budget = 192 * 1024;
static P4TopologyCache topology_cache;
static void topology_report(unsigned frames)
{
    p4_log("topology cache %d per frame: hits %u builds %u fallback %u bytes %u budget %u evictions %u\n",
           p4_topology_cache, topology_cache.hits / frames, topology_cache.builds / frames,
           topology_cache.misses / frames, topology_cache.bytes, p4_topology_budget, topology_cache.evictions);
    topology_cache.hits = topology_cache.builds = topology_cache.misses = topology_cache.evictions = 0;
}
static void draw_meshes_body(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere);
/* Instance replay. The game draws copies of one geometry back to back (Heaven: 84 flowers of 32 vertices, same
   arrays, only the matrix differs; b86 ~110 k cycles an atomic, nearly all fixed cost). After a copy is drawn, the
   GE words it wrote after its model matrix are kept; the next copy sends its matrix and those words again when
   nothing was written to the list in between, every input the words depend on is the same, the GE state cache
   ended the copy as it began it (so no state the words skipped can differ), and its sphere needs no guard clip. */
/* b189-b190 GE CALL records (p4_skin_call: GE-skinned batches; p4_mesh_call: mesh cache hits drawn whole). The
   state and draw words of bind_mask, bind_raster, bind_ge_light and draw_tested are recorded once into a ring and
   CALLed in later frames, so the CPU skips those calls. The key hashes every input they read: the drawn arrays,
   material, texture, light, TEST_1, masks, alpha bounds and the state cache gs (its camera fields aside: nothing here
   reads or sets them, and they change whenever the camera moves). A replay leaves gs and the bound texture globals as
   the recording did. Two records per drawn thing: the body and its shadow pass. An entry older than half the ring is
   copied forward before it is CALLed, and one frame writes at most an eighth of the ring, so an entry the GE may still
   read (up to 2 frames late) is never overwritten. GE CALLs nest 2 deep on hardware; this is level 1. Instance replay
   copies these BASE+CALL words like any others. */
typedef struct { unsigned h[2], start, words, draws, used; } GeCall;
int p4_skin_call = 1; /* 0: every skinned batch's state and draw built every frame, as b188 */
int p4_mesh_call = 1; /* 0: every mesh cache hit's state and draw built every frame, as b189 */
enum { CALL_RING_WORDS = 256 * 1024, CALL_FRAME_WORDS = CALL_RING_WORDS / 8, CALL_MAX_WORDS = 512 };
enum { CALL_GS_WORDS = (sizeof gs + 3) / 4, CALL_HEAD = CALL_GS_WORDS + 4 };
static unsigned *call_ring, call_head, call_frame, call_frame_words;
static unsigned call_hits[2], call_records[2], call_misses[2], call_refreshes, call_refused, call_full; /* [0] skin, [1] mesh */
#define CALL_CAM0 offsetof(__typeof__(gs), camera_known)
#define CALL_CAM1 offsetof(__typeof__(gs), texscale_known)
static unsigned *call_alloc(int words, unsigned *start)
{
    if (call_frame != cache_frame)
        call_frame = cache_frame, call_frame_words = 0;
    if (!call_ring && !(call_ring = memalign(64, CALL_RING_WORDS * 4)))
        return 0;
    unsigned at = call_head % CALL_RING_WORDS, skip = at + words > CALL_RING_WORDS ? CALL_RING_WORDS - at : 0;
    if (call_frame_words + skip + words > CALL_FRAME_WORDS) {
        call_full++;
        return 0;
    }
    call_head += skip;
    call_frame_words += skip + words;
    *start = call_head;
    call_head += words;
    return call_ring + *start % CALL_RING_WORDS;
}
static inline void call_mix(unsigned h[2], unsigned w)
{
    h[0] = (h[0] ^ w) * 16777619u;
    h[1] = (h[1] + w) * 2654435761u;
    h[1] ^= h[1] >> 15;
}
/* the inputs every recorded draw shares, into w; -1: not recorded (camera or feedback texture, no mesh mode, a probe
   or an experiment switch) */
static int call_inputs(unsigned *w, const Material *m, int modulate, const SceneLight *light)
{
    if (!gu_mesh_mode || p4_draw_batch <= 1 || p4_gs_test_raw_prelit || p4_probe_3d || p4_hide_material)
        return -1;
    Raster *r = m->texture ? *(Raster **)m->texture : 0;
    if (r && (r == &display_raster || rtt_find(r)))
        return -1;
    int n = 0;
    w[n++] = (unsigned)m, w[n++] = m->colour;
    memcpy(&w[n++], &m->ambient, 4);
    memcpy(&w[n++], &m->diffuse, 4);
    w[n++] = (unsigned)r, w[n++] = r ? (unsigned)r->originalPixels : 0, w[n++] = r ? (unsigned)r->palette : 0, w[n++] = texture_generation;
    w[n++] = modulate;
    memcpy(w + n, light, 36);
    n += 9;
    w[n++] = pixel_mask, w[n++] = sky_state[3], w[n++] = (unsigned)rtt_target, w[n++] = render_state[RS_ZWRITE];
    w[n++] = p4_screen_date | p4_rtt_date << 1 | p4_alpha_pixel << 2 | p4_gs_test << 3 | p4_gs_test_skip << 4 | p4_gs_mesh_alpha << 5 | p4_mipmaps << 6;
    w[n++] = draw_alpha_known, w[n++] = draw_skin_alpha ? draw_skin_alpha[0] : -1, w[n++] = draw_skin_alpha ? draw_skin_alpha[1] : -1;
    w[n++] = (unsigned)draw_fixed_alpha, w[n++] = draw_material_ge;
    return n;
}
static void call_key(const unsigned *w, int n, unsigned h[2])
{
    h[0] = 2166136261u, h[1] = 0x9e3779b9u;
    for (int i = 0; i < n; i++)
        call_mix(h, w[i]);
    const unsigned *s = (const unsigned *)&gs;
    for (unsigned i = 0; i < sizeof gs / 4; i++)
        if (i < CALL_CAM0 / 4 || i >= CALL_CAM1 / 4)
            call_mix(h, s[i]);
}
/* the record with this key, or 0 with *slot the one to record into (empty, else the longest unused) */
static GeCall *call_find(GeCall c[2], const unsigned h[2], GeCall **slot)
{
    GeCall *spare = 0;
    for (int i = 0; i < 2; i++) {
        if (c[i].words && c[i].h[0] == h[0] && c[i].h[1] == h[1])
            return &c[i];
        if (!spare || !c[i].words || (spare->words && c[i].used < spare->used))
            spare = &c[i];
    }
    *slot = spare;
    return 0;
}
static int call_replay(GeCall *c, int kind)
{
    unsigned age = call_head - c->start;
    if (age > CALL_RING_WORDS - CALL_MAX_WORDS) { /* overwritten since: record again */
        c->words = 0;
        return 0;
    }
    if (age > CALL_RING_WORDS / 2) {
        unsigned start, *to = call_alloc(c->words, &start);
        if (!to)
            return 0;
        memcpy(to, call_ring + c->start % CALL_RING_WORDS, c->words * 4);
        sceKernelDcacheWritebackRange(to, c->words * 4);
        c->start = start;
        call_refreshes++;
    }
    const unsigned *e = call_ring + c->start % CALL_RING_WORDS, *ge = e + CALL_HEAD;
    unsigned *l = gu_list->current;
    l[0] = 0x10u << 24 | ((unsigned)ge >> 8 & 0xf0000); /* BASE */
    l[1] = 0x0Au << 24 | ((unsigned)ge & 0xffffff);     /* CALL */
    gu_list->current = l + 2;
    memcpy(&gs, e, CALL_CAM0);
    memcpy((char *)&gs + CALL_CAM1, (const char *)e + CALL_CAM1, sizeof gs - CALL_CAM1);
    bound_alpha_min = e[CALL_GS_WORDS], bound_alpha_max = e[CALL_GS_WORDS + 1];
    memcpy(&texture_w, &e[CALL_GS_WORDS + 2], 4);
    memcpy(&texture_h, &e[CALL_GS_WORDS + 3], 4);
    c->used = cache_frame;
    call_hits[kind]++;
    if (ge_work_on)
        work_words(ge, e + c->words - 1);
    mesh_draw_calls += c->draws;
    draws_since_stall += c->draws;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}
/* the words from..current just went into the list for this draw: keep them as c */
static void call_record(GeCall *c, const unsigned h[2], const unsigned *from, int kind)
{
    int n = gu_list->current - from, draws = 0;
    if (n <= 0 || n > CALL_MAX_WORDS || !gu_mesh_mode) {
        call_refused++;
        return;
    }
    for (int i = 0; i < n; i++) {
        unsigned cmd = from[i] >> 24;
        if (cmd == 0x08 || cmd == 0x0a || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) { /* JUMP, CALL, RET, END, FINISH */
            call_refused++;
            return;
        }
        draws += cmd == 0x04;
    }
    unsigned start, *e = call_alloc(CALL_HEAD + n + 1, &start);
    if (!e)
        return;
    memcpy(e, &gs, sizeof gs);
    e[CALL_GS_WORDS] = bound_alpha_min, e[CALL_GS_WORDS + 1] = bound_alpha_max;
    memcpy(&e[CALL_GS_WORDS + 2], &texture_w, 4);
    memcpy(&e[CALL_GS_WORDS + 3], &texture_h, 4);
    memcpy(e + CALL_HEAD, from, n * 4);
    e[CALL_HEAD + n] = 0x0Bu << 24; /* RET */
    sceKernelDcacheWritebackRange(e, (CALL_HEAD + n + 1) * 4);
    c->h[0] = h[0], c->h[1] = h[1];
    c->start = start, c->words = CALL_HEAD + n + 1, c->draws = draws, c->used = cache_frame;
    call_records[kind]++;
}
static void call_report(void)
{
    p4_log("ge call skin %d mesh %d per 256 frames: skin hits %u records %u misses %u; mesh hits %u records %u misses %u; refreshes %u refused %u full %u ring %u KB\n",
           p4_skin_call, p4_mesh_call, call_hits[0], call_records[0], call_misses[0], call_hits[1], call_records[1], call_misses[1], call_refreshes,
           call_refused, call_full, CALL_RING_WORDS / 256);
    memset(call_hits, 0, sizeof call_hits), memset(call_records, 0, sizeof call_records), memset(call_misses, 0, sizeof call_misses);
    call_refreshes = call_refused = call_full = 0;
}

/* b243 p4_mesh_persist: a whole draw_meshes call kept between frames. b240 on the PSP: 61% of the CPU's cycles are
   memory stall, and a mesh draw that ends as a CALL replay still walks ~80 code lines and ~50 data lines to get there
   (lights, setup, key, cache slot, call key, ring lookup). When every mesh of the call replayed a recorded CALL and
   nothing was cut on the CPU, the words after the model matrix are just BASE + CALL pairs: they are kept with every
   input the body read, and the next frame the call is the state setup (mesh_state), one comparison and a copy.
   Compared exactly, no tolerance: arrays, count, modulate, camera and its view, model, render and sky state, pixel
   mask, materials and their rasters, light, target, the state shadow after mesh_state, the geometry unlocked since,
   each mesh cache slot (key, build stamp, vertices) and the age of the oldest ring record. The slots are stamped as
   used so the cache does not evict what the GE reads. 0 off; check 1: the body runs anyway and its words must be the
   recorded ones. */
int p4_mesh_persist = 0, p4_mesh_persist_check = 0;
enum { PERSIST_SLOTS = 512, PERSIST_WORDS = 48, PERSIST_MESHES = 8 };
static int persist_capture, persist_noted;
static struct { MeshCache *slot; unsigned start; } persist_note_at[PERSIST_MESHES];
static void persist_note(MeshCache *slot, unsigned start)
{
    if (persist_capture) {
        if (persist_noted < PERSIST_MESHES)
            persist_note_at[persist_noted].slot = slot, persist_note_at[persist_noted].start = start;
        persist_noted++;
    }
}
static GeCall (*mesh_calls)[2]; /* heap, one pair per mesh_cache slot: .bss is full */
static void draw_mesh_hit(MeshCache *slot, int prim, const Material *material, int modulate, const SceneLight *light)
{
    unsigned w[48], h[2];
    GeCall *call = 0, *rec = 0;
    int n = p4_mesh_call && (mesh_calls || (mesh_calls = calloc(CACHE_SLOTS, sizeof *mesh_calls))) ? call_inputs(w, material, modulate, light) : -1;
    if (n >= 0) {
        w[n++] = (unsigned)slot, w[n++] = (unsigned)slot->v, w[n++] = (unsigned)slot->idx, w[n++] = slot->vtype, w[n++] = slot->indices;
        w[n++] = slot->key, w[n++] = slot->built, w[n++] = prim, w[n++] = slot->alpha_lo, w[n++] = slot->alpha_hi;
        call_key(w, n, h);
        call = call_find(mesh_calls[slot - mesh_cache], h, &rec);
        if (call && call_replay(call, 1)) {
            persist_note(slot, call->start);
            return;
        }
        if (call) /* found but overwritten or no room to refresh: record it again */
            rec = call;
        else if (rec)
            call_misses[1]++;
    }
    const unsigned *from = gu_list->current;
    void **texture = material->texture;
    prof_begin(PROF_HBIND);
    bind_mask(material->colour >> 24);
    bind_raster(texture ? *texture : 0, GU_REPEAT);
    prof_end(PROF_HBIND);
    draw_slot_v = slot->v, draw_slot_alpha = &slot->alpha_lo;
    draw_mesh_vertices(prim, slot->vtype, slot->indices, slot->idx, slot->v, material, modulate, light);
    draw_slot_v = 0;
    if (rec)
        call_record(rec, h, from, 1);
}

int p4_instance_replay = 1; /* 0: every copy through draw_meshes_body, as b93 */
int p4_replay_census = 0; /* b207: 1 counts what instance replays send (replay_census) */
static unsigned *mesh_model_mark;
enum { REPLAY_MESHES = 8, REPLAY_WORDS = 512 };
static struct {
    int valid, count, modulate, draws;
    unsigned frame, tex_generation, pixel;
    const void *header, *position, *uv, *prelit, *normals, *camera;
    unsigned *from, *end;
    int render[MAX_STATE];
    unsigned sky[8], camera_view[18], material[REPLAY_MESHES][3];
    SceneLight light;
} replay;
static unsigned replay_draws, replay_records, replay_refusals[6];
/* b207: what an instance replay sends, its CALLs followed one level (BASE + CALL into the GeCall ring): words, draws,
   CLUT loads (LOADCLUT 0xC4), texture flushes (0xCB) and texture address words (0xA0). b201 Margaret: 84 flowers a
   frame, 32 vertices each, ~2.5 ms of GE with the timer: are they paying a CLUT load and a flush each? */
static unsigned replay_words, replay_prims, replay_cluts, replay_flushes, replay_texaddr;
static void replay_census(const unsigned *w, const unsigned *end, int depth)
{
    unsigned base = 0;
    for (; w < end; w++) {
        unsigned cmd = *w >> 24;
        replay_words++;
        replay_prims += cmd == 0x04, replay_cluts += cmd == 0xC4, replay_flushes += cmd == 0xCB, replay_texaddr += cmd == 0xA0;
        if (cmd == 0x10)
            base = *w & 0xffffff;
        else if (cmd == 0x0A && depth == 0) { /* CALL: its words up to RET */
            const unsigned *c = (const unsigned *)((base & 0xf0000) << 8 | (*w & 0xffffff)), *stop = c + CALL_MAX_WORDS + 1;
            const unsigned *r = c;
            while (r < stop && *r >> 24 != 0x0B)
                r++;
            replay_census(c, r, 1);
        } else if (cmd == 0x0B)
            return;
    }
}
static unsigned char gs_before[sizeof gs], persist_gs_mark[sizeof gs];
static int replay_inputs(MeshHeader *header, const Matrix *model, unsigned material[REPLAY_MESHES][3], unsigned view[18])
{
    if (!model || header->numMeshes > REPLAY_MESHES || p4_mesh_frustum || p4_probe_depth_mode)
        return 0;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    memset(material, 0, REPLAY_MESHES * 3 * 4);
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        material[i][0] = mesh->material->colour;
        memcpy(&material[i][1], &mesh->material->ambient, 4);
        memcpy(&material[i][2], &mesh->material->diffuse, 4);
    }
    memcpy(view, (char *)current_camera + CAMERA_VIEW, 16 * 4);
    memcpy(view + 16, (char *)current_camera + CAMERA_NEAR, 4);
    memcpy(view + 17, (char *)current_camera + CAMERA_FAR, 4);
    return 1;
}
static int replay_unguarded(const Matrix *model, const float *sphere)
{
    if (!p4_guard_clip || !guard_code)
        return 1;
    if (!p4_guard_world || !sphere || !(sphere[3] > 0))
        return 0;
    guard_world_planes();
    return guard_sphere_world(model, sphere, *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f);
}
static int replay_try(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    if (!replay.valid || replay.frame != cache_frame || gu_list->current != replay.end || !gu_mesh_mode)
        return 0;
    if (replay.header != header || replay.position != position || replay.uv != uv || replay.prelit != preLit || replay.count != count ||
        replay.modulate != modulate || replay.normals != normals || replay.camera != current_camera || replay.tex_generation != texture_generation)
        return 0;
    unsigned material[REPLAY_MESHES][3], view[18];
    if (!replay_inputs(header, model, material, view))
        return 0;
    int why = 0;
    if (memcmp(replay.render, render_state, sizeof replay.render) || memcmp(replay.sky, sky_state, sizeof replay.sky) || replay.pixel != pixel_mask)
        why = 1;
    else if (memcmp(replay.material, material, sizeof material))
        why = 2;
    else if (memcmp(replay.camera_view, view, sizeof view))
        why = 3;
    else if (normals) {
        prof_begin(PROF_RLIGHT);
        SceneLight light = scene_lights();
        prof_end(PROF_RLIGHT);
        if (memcmp(&light, &replay.light, sizeof light))
            why = 4;
    }
    prof_begin(PROF_RGUARD);
    if (!why && !replay_unguarded(model, sphere))
        why = 5;
    prof_end(PROF_RGUARD);
    if (why) {
        replay_refusals[why]++;
        return 0;
    }
    set_matrix(GU_MODEL, model);
    int words = replay.end - replay.from;
    unsigned *to = gu_list->current;
    memcpy(to, replay.from, words * 4);
    gu_list->current = to + words;
    replay.from = to;
    replay.end = to + words;
    if (p4_replay_census)
        replay_census(to, to + words, 0);
    if (ge_work_on)
        work_words(to, to + words);
    mesh_draw_calls += replay.draws;
    replay_draws++;
    draws_since_stall += replay.draws;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}
static void replay_record(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere, unsigned *start)
{
    replay.valid = 0;
    if (!gu_mesh_mode || !mesh_model_mark || mesh_model_mark < start || mesh_model_mark > gu_list->current || memcmp(gs_before, &gs, sizeof gs))
        return;
    int words = gu_list->current - mesh_model_mark, draws = 0;
    if (words <= 0 || words > REPLAY_WORDS)
        return;
    for (int i = 0; i < words; i++) {
        unsigned cmd = mesh_model_mark[i] >> 24;
        if (cmd == 0x08 || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) /* JUMP, RET, END, FINISH: inline data or flow (b190: a CALL to a GeCall ring entry copies fine) */
            return;
        draws += cmd == 0x04;
    }
    if (!replay_inputs(header, model, replay.material, replay.camera_view) || !replay_unguarded(model, sphere))
        return;
    replay.valid = 1;
    replay.frame = cache_frame, replay.tex_generation = texture_generation, replay.pixel = pixel_mask;
    replay.header = header, replay.position = position, replay.uv = uv, replay.prelit = preLit, replay.normals = normals;
    replay.count = count, replay.modulate = modulate, replay.camera = current_camera, replay.draws = draws;
    memcpy(replay.render, render_state, sizeof replay.render);
    memcpy(replay.sky, sky_state, sizeof replay.sky);
    if (normals)
        replay.light = scene_lights();
    replay.from = mesh_model_mark;
    replay.end = gu_list->current;
    replay_records++;
}
typedef struct {
    const void *header, *model_at, *position, *uv, *prelit, *normals, *camera, *geometry, *rtt;
    int count, modulate, words, draws, nslots, churn, valid;
    unsigned tex_generation, pixel, bits, ring_min, skip_until, alpha_min, alpha_max, stamp; /* stamp: last frame used */
    float tw, th, sphere[4], model[12];
    int render[MAX_STATE];
    unsigned sky[8], view[18], material[REPLAY_MESHES][3];
    const void *mat[PERSIST_MESHES][4]; /* material, raster, its pixels, its palette */
    struct { MeshCache *slot; unsigned key, built; const void *v; } slot[PERSIST_MESHES];
    SceneLight light;
    unsigned char gs_mark[sizeof gs], gs_end[sizeof gs];
    unsigned w[PERSIST_WORDS];
} PersistDraw;
static PersistDraw *persist_table;
static unsigned persist_hits, persist_records, persist_miss[10], persist_checks, persist_diffs, persist_backoff;
static SceneLight mesh_state(const Matrix *model, int lit);
static unsigned persist_bits(void)
{
    return p4_screen_date | p4_rtt_date << 1 | p4_alpha_pixel << 2 | p4_gs_test << 3 | p4_gs_test_skip << 4 | p4_gs_mesh_alpha << 5 | p4_mipmaps << 6 |
           p4_mesh_call << 7 | p4_guard_clip << 8 | p4_ge_light << 9 | (p4_fog & 3) << 10 | (p4_material_state & 3) << 12;
}
static void persist_model(float out[12], const Matrix *m)
{
    const float *f = &m->right.x;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            out[r * 3 + c] = f[r * 4 + c];
}
static void persist_materials(MeshHeader *header, const void *mat[PERSIST_MESHES][4])
{
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    memset(mat, 0, PERSIST_MESHES * 4 * sizeof(void *));
    for (int i = 0; i < header->numMeshes && i < PERSIST_MESHES; i++, mesh++) {
        Raster *r = mesh->material->texture ? *(Raster **)mesh->material->texture : 0;
        mat[i][0] = mesh->material, mat[i][1] = r, mat[i][2] = r ? r->originalPixels : 0, mat[i][3] = r ? r->palette : 0;
    }
}
static PersistDraw *persist_entry(const void *header, const void *model)
{
    if (!persist_table && !(persist_table = calloc(PERSIST_SLOTS, sizeof *persist_table)))
        return 0;
    /* b244: the index of b243 (a few middle bits of two products) put ~17 calls a frame on each other's entry in the
       dialogue. Mixed hash, two entries a set: the one that holds this call, else an empty one, else the older. */
    unsigned h = ((unsigned)header >> 4) * 2654435761u ^ ((unsigned)model >> 4) * 2246822519u;
    h ^= h >> 15, h *= 2654435761u, h ^= h >> 13;
    PersistDraw *a = &persist_table[(h >> 8 & (PERSIST_SLOTS / 2 - 1)) * 2], *b = a + 1;
    if (a->header == header && a->model_at == model)
        return a;
    if (b->header == header && b->model_at == model)
        return b;
    return !a->header ? a : !b->header ? b : (int)(a->stamp - b->stamp) <= 0 ? a : b;
}
/* 1: replayed. 2 (check mode): it would have replayed; the caller runs the body and compares. 0: the body. */
static int persist_try(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    PersistDraw *e = persist_entry(header, model);
    int why = 0;
    if (!e || !e->valid || e->header != header || e->model_at != model)
        return 0;
    if (e->position != position || e->uv != uv || e->prelit != preLit || e->normals != normals || e->count != count || e->modulate != modulate ||
        e->camera != current_camera || e->geometry != draw_geometry || !draw_geometry || draw_geometry->locked || e->rtt != rtt_target ||
        draw_fixed_alpha || draw_skin_alpha || !sphere)
        why = 1;
    else if (e->tex_generation != texture_generation || e->pixel != pixel_mask || e->bits != persist_bits() ||
             memcmp(e->render, render_state, sizeof e->render) || memcmp(e->sky, sky_state, sizeof e->sky))
        why = 2;
    else if (call_head - e->ring_min > CALL_RING_WORDS / 2)
        why = 3;
    else {
        float m[12];
        unsigned material[REPLAY_MESHES][3], view[18];
        const void *mat[PERSIST_MESHES][4];
        persist_model(m, model);
        if (memcmp(m, e->model, sizeof m) || memcmp(sphere, e->sphere, sizeof e->sphere))
            why = 4;
        else if (!replay_inputs(header, model, material, view) || memcmp(view, e->view, sizeof view))
            why = 5;
        else if (memcmp(material, e->material, sizeof material) || (persist_materials(header, mat), memcmp(mat, e->mat, sizeof mat)))
            why = 6;
        for (int i = 0; !why && i < e->nslots; i++)
            if (e->slot[i].slot->key != e->slot[i].key || e->slot[i].slot->built != e->slot[i].built || e->slot[i].slot->v != e->slot[i].v)
                why = 7;
    }
    if (!why) {
        begin_frame();
        SceneLight light = mesh_state(model, normals != 0);
        if (memcmp(&gs, e->gs_mark, sizeof gs))
            why = 8;
        else if (normals && memcmp(&light, &e->light, sizeof light))
            why = 9;
    }
    if (why) {
        persist_miss[why]++;
        e->valid = 0;
        if (++e->churn >= 4) /* inputs that move every frame: stop recording it for a while */
            e->churn = 0, e->skip_until = cache_frame + 64, persist_backoff++;
        return 0;
    }
    e->stamp = cache_frame;
    if (p4_mesh_persist_check)
        return 2;
    unsigned *to = gu_list->current;
    memcpy(to, e->w, e->words * 4);
    gu_list->current = to + e->words;
    memcpy(&gs, e->gs_end, sizeof gs);
    bound_alpha_min = e->alpha_min, bound_alpha_max = e->alpha_max;
    texture_w = e->tw, texture_h = e->th;
    for (int i = 0; i < e->nslots; i++)
        e->slot[i].slot->frame = cache_frame;
    if (ge_work_on)
        work_words(to, to + e->words);
    e->churn = 0;
    cache_hits += e->nslots;
    mesh_draw_calls += e->draws;
    draws_since_stall += e->draws;
    persist_hits++;
    replay.valid = 0;
    draw_material_ge = 0;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}
/* after the body, with persist_capture on: keep the call when it was CALL replays only. would: persist_try said 2 */
static void persist_record(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere, unsigned *start, int would)
{
    PersistDraw *e = persist_entry(header, model);
    int words = mesh_model_mark ? gu_list->current - mesh_model_mark : -1, calls = 0;
    if (!e || !gu_mesh_mode || !mesh_model_mark || mesh_model_mark < start || words <= 0 || words > PERSIST_WORDS)
        return;
    if (would) { /* check mode: the body's words against the record */
        persist_checks++;
        if (words != e->words || memcmp(mesh_model_mark, e->w, words * 4) || memcmp(&gs, e->gs_end, sizeof gs)) {
            if (persist_diffs++ < 8)
                p4_log("TRACE mesh persist differs: header %p words %d recorded %d\n", (void *)header, words, e->words);
            e->valid = 0;
        }
        return;
    }
    if ((e->header == header && e->model_at == model && (int)(e->skip_until - cache_frame) > 0) || !sphere || !draw_geometry || draw_geometry->locked ||
        draw_fixed_alpha || draw_skin_alpha || header->numMeshes > PERSIST_MESHES)
        return;
    for (int i = 0; i < words; i++) {
        unsigned cmd = mesh_model_mark[i] >> 24;
        if (cmd != 0x10 && cmd != 0x0A) /* anything but BASE and CALL: built this frame, or it reads the frame pool */
            return;
        calls += cmd == 0x0A;
    }
    if (!calls || calls != persist_noted || calls > PERSIST_MESHES || !replay_unguarded(model, sphere))
        return;
    int churn = e->header == header && e->model_at == model ? e->churn : 0;
    e->valid = 0;
    if (!replay_inputs(header, model, e->material, e->view))
        return;
    e->header = header, e->model_at = model, e->position = position, e->uv = uv, e->prelit = preLit, e->normals = normals;
    e->camera = current_camera, e->geometry = draw_geometry, e->rtt = rtt_target;
    e->count = count, e->modulate = modulate, e->words = words, e->draws = calls, e->nslots = calls, e->churn = churn;
    e->tex_generation = texture_generation, e->pixel = pixel_mask, e->bits = persist_bits();
    e->alpha_min = bound_alpha_min, e->alpha_max = bound_alpha_max, e->tw = texture_w, e->th = texture_h;
    memcpy(e->sphere, sphere, sizeof e->sphere);
    persist_model(e->model, model);
    memcpy(e->render, render_state, sizeof e->render);
    memcpy(e->sky, sky_state, sizeof e->sky);
    persist_materials(header, e->mat);
    e->ring_min = persist_note_at[0].start;
    for (int i = 0; i < calls; i++) {
        MeshCache *slot = persist_note_at[i].slot;
        e->slot[i].slot = slot, e->slot[i].key = slot->key, e->slot[i].built = slot->built, e->slot[i].v = slot->v;
        if ((int)(persist_note_at[i].start - e->ring_min) < 0)
            e->ring_min = persist_note_at[i].start;
    }
    if (normals)
        e->light = scene_lights();
    memcpy(e->gs_mark, persist_gs_mark, sizeof gs);
    memcpy(e->gs_end, &gs, sizeof gs);
    memcpy(e->w, mesh_model_mark, words * 4);
    e->valid = 1, e->stamp = cache_frame;
    persist_records++;
}
static void replay_report(unsigned frames)
{
    p4_log("mesh persist %d check %d per 256 frames: replays %u records %u backoffs %u checks %u diffs %u\n", p4_mesh_persist, p4_mesh_persist_check,
           persist_hits, persist_records, persist_backoff, persist_checks, persist_diffs);
    p4_log("mesh persist misses: inputs %u state %u ring %u model %u camera %u material %u slot %u shadow %u light %u\n", persist_miss[1], persist_miss[2],
           persist_miss[3], persist_miss[4], persist_miss[5], persist_miss[6], persist_miss[7], persist_miss[8], persist_miss[9]);
    persist_hits = persist_records = persist_backoff = persist_checks = persist_diffs = 0;
    memset(persist_miss, 0, sizeof persist_miss);
    p4_log("instance replay %d per frame: draws %u records %u refused state %u material %u camera %u light %u guard %u\n", p4_instance_replay,
           replay_draws / frames, replay_records / frames, replay_refusals[1] / frames, replay_refusals[2] / frames, replay_refusals[3] / frames,
           replay_refusals[4] / frames, replay_refusals[5] / frames);
    if (p4_replay_census && replay_draws)
        p4_log("instance replay sends per replay: words %u draws %u clut loads %u/100 flushes %u/100 texture address %u/100 (%u replays)\n",
               replay_words / replay_draws, replay_prims / replay_draws, replay_cluts * 100 / replay_draws, replay_flushes * 100 / replay_draws,
               replay_texaddr * 100 / replay_draws, replay_draws);
    replay_words = replay_prims = replay_cluts = replay_flushes = replay_texaddr = 0;
    replay_draws = replay_records = 0;
    memset(replay_refusals, 0, sizeof replay_refusals);
}
static void draw_meshes(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    prof_begin(PROF_MESH);
    prof_begin(PROF_RPLAY); /* b230: what a replay costs (b229: ~15k cycles each, 87 a frame in the dialogue) */
    int replayed = p4_instance_replay && model && replay_try(header, position, uv, preLit, count, model, modulate, normals, sphere);
    prof_end(PROF_RPLAY);
    if (replayed) {
        prof_end(PROF_MESH);
        return;
    }
    int persist = 0;
    if (p4_mesh_persist && model && header->numMeshes <= PERSIST_MESHES) {
        prof_begin(PROF_PERSIST);
        persist = persist_try(header, position, uv, preLit, count, model, modulate, normals, sphere);
        prof_end(PROF_PERSIST);
        if (persist == 1) {
            prof_end(PROF_MESH);
            return;
        }
    }
    unsigned *start = gu_list->current;
    int record = p4_instance_replay && model && gu_mesh_mode;
    if (record)
        memcpy(gs_before, &gs, sizeof gs);
    mesh_model_mark = 0;
    persist_capture = p4_mesh_persist && model, persist_noted = 0;
    draw_meshes_body(header, position, uv, preLit, count, model, modulate, normals, sphere);
    draw_material_ge = 0; /* the body sets it for its compact meshes and may return early */
    if (persist_capture) {
        persist_capture = 0;
        persist_record(header, position, uv, preLit, count, model, modulate, normals, sphere, start, persist == 2);
    }
    if (record)
        replay_record(header, position, uv, preLit, count, model, modulate, normals, sphere, start);
    else
        replay.valid = 0;
    prof_end(PROF_MESH);
}

/* RenderWare fog (rwRENDERSTATEFOGENABLE 14, FOGCOLOR 15 as A8R8G8B8, FOGTYPE 16): linear from the camera's fog
   plane to its far plane. The GE fogs per vertex from view depth; RenderWare's view looks down +z and the GE's
   coefficient is (z + fog1) * fog2 with 1 = clear, hence the negated distances. Heaven: on for the clouds, 3000 to
   22870, colour 0. */
int p4_fog = 0; /* 1: the game's fog on 3D meshes (experiment) */
static struct { int known; float start, end; unsigned colour; } fog_sent;
static void bind_fog(void)
{
    int on = p4_fog && render_state[14] && render_state[16] == 1;
    st_enable(GU_FOG, on);
    if (!on)
        return;
    float start = *(float *)((char *)current_camera + 0x88), end = *(float *)((char *)current_camera + CAMERA_FAR);
    unsigned c = render_state[15], colour = (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    if (start >= end)
        start = end - 1;
    if (gu_mesh_mode && fog_sent.known && fog_sent.start == start && fog_sent.end == end && fog_sent.colour == colour)
        return;
    fog_sent.known = gu_mesh_mode, fog_sent.start = start, fog_sent.end = end, fog_sent.colour = colour;
    sceGuFog(-start, -end, colour);
}

/* The state of every 3D mesh draw: depth, alpha test, blend, camera, the model matrix (0: the world). */
static SceneLight mesh_state(const Matrix *model, int lit)
{
    prof_begin(PROF_LIGHTS);
    SceneLight light = lit ? scene_lights() : (SceneLight){0};
    prof_end(PROF_LIGHTS);
    gu_mesh_begin();
    bind_depth();
    st_enable(GU_CLIP_PLANES, 1);
    st_enable(GU_ALPHA_TEST, 1);
    st_alpha_func(GU_GREATER, 8);
    st_enable(GU_BLEND, 1);
    unsigned blend = sky_state[2] ? sky_state[2] : BLEND_NORMAL;
    if (!gu_mesh_mode || !gs.blend_known || gs.blend != (blend & 0xff)) {
        set_blend(blend);
        gs.blend_known = gu_mesh_mode, gs.blend = blend & 0xff;
    }
    bind_texscale(0);
    bind_fog();
    set_camera();
    if (model) {
        set_matrix(GU_MODEL, model);
    } else {
        ScePspFMatrix4 identity;
        gumLoadIdentity(&identity);
        sceGuSetMatrix(GU_MODEL, &identity);
    }
    mesh_model_mark = gu_list->current;
    if (persist_capture)
        memcpy(persist_gs_mark, &gs, sizeof gs);
    return light;
}

static void draw_meshes_body(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    prof_begin(PROF_MFRUST);
    int frustum_ready = frustum_prepare(position, count, model);
    prof_end(PROF_MFRUST);
    prof_begin(PROF_SETUP);
    begin_frame();
    draw_prelit = preLit;
    draw_count = count;
    draw_prelit_min = -1;
    draw_alpha_known = 1;
    Vertex3D *vertices = 0;
    unsigned previous_colour = 0;
    const Material *previous_material = 0;
    SceneLight light = mesh_state(model, normals != 0);
    int prim = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP ? GU_TRIANGLE_STRIP : GU_TRIANGLES;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    /* Scrolled UVs (the station's cloud-shadow pass reached v = -969605) lose their fraction in the GE's float24 and
       sample in speckled streaks. Textures here repeat, so take the whole part of the smallest coordinate off. */
    float du = 0, dv = 0;
    /* b167 (p4_lock_memo): RenderWare on the PS2 re-instances a geometry only when lockedSinceLastInst is set, so the
       game must lock to change its arrays. Unlocked since we last drew it: reuse its sampled hash and UV shift (the
       samples were cache misses, ~0.9 Mcycles a frame in the room). Clearing the flag below is what instancing did. */
    LockMemo *lm = 0;
    int lm_hit = 0;
    if (p4_lock_memo && draw_geometry && p4_mesh_cache && p4_mesh_compact) {
        lm = &lock_memo[((unsigned)draw_geometry >> 4) * 2654435761u >> 22];
        lm_hit = lm->g == draw_geometry && !draw_geometry->locked && lm->pos == position && lm->uv == uv && lm->pre == preLit &&
                 lm->nrm == normals && lm->count == count;
        lock_memo_hits += lm_hit;
    }
    prof_begin(PROF_UVSCAN);
    if (lm_hit)
        du = lm->du, dv = lm->dv;
    else if (uv && p4_uv_rebase && count > 0) {
        /* b137: the shift only has to be whole (textures repeat), not the exact minimum, so it is kept per UV array
           and recomputed when 8 sampled coordinates change (scrolled UVs move all of them). b129 Heaven: the scan
           cost 0.68 Mcycles a frame. */
        static struct { const void *uv; int count; unsigned sample; float du, dv; } uv_memo[1024]; /* b145: 256 slots hit 74 of 140 in the room */
        unsigned slot = ((unsigned)uv >> 3) * 2654435761u >> 22;
        /* b152: only large arrays (where the scan costs): the normal-attack target beam (a few vertices, UVs moved every
           frame) drew striped with a remembered shift; small arrays are scanned every time */
        int memo = p4_uv_memo && count >= 64;
        unsigned sample = memo ? hash_sample(2166136261u, uv, count, 8) : 0;
        if (memo && uv_memo[slot].uv == uv && uv_memo[slot].count == count && uv_memo[slot].sample == sample) {
            du = uv_memo[slot].du, dv = uv_memo[slot].dv;
            uv_memo_hits++;
        } else {
            float mu = uv[0][0], mv = uv[0][1];
            for (int j = 1; j < count; j++) {
                if (uv[j][0] < mu) mu = uv[j][0];
                if (uv[j][1] < mv) mv = uv[j][1];
            }
            float step = p4_uv_shift_step > 1 ? (float)p4_uv_shift_step : 1.0f;
            if (mu < -16 || mu > 16) du = floorf(mu / step) * step;
            if (mv < -16 || mv > 16) dv = floorf(mv / step) * step;
            if (memo)
                uv_memo[slot].uv = uv, uv_memo[slot].count = count, uv_memo[slot].sample = sample, uv_memo[slot].du = du, uv_memo[slot].dv = dv;
        }
        if (du || dv) uv_rebased++;
    }
    prof_end(PROF_UVSCAN);
    prof_end(PROF_SETUP);
    /* with boxes the per-mesh boxes decide: the array is only asked whether its sphere is inside the band */
    int box_mode = p4_guard_boxes && p4_guard_compact && p4_guard_world && p4_mesh_compact && p4_mesh_cache && sphere && sphere[3] > 0 &&
                   header->numMeshes <= 256;
    unsigned guarded = 0;
    if (p4_guard_clip && guard_code && count <= MAX_GUARDED) {
        if (box_mode) {
            prof_begin(PROF_GUARD);
            guard_world_planes();
            guarded = !guard_sphere_world(model, sphere, near);
            guard_spheres += !guarded;
            prof_end(PROF_GUARD);
        } else
            guarded = guard_classify(position, count, model, near, sphere);
    }
    /* Profiled in the limousine (b66): 79% of the frame's CPU was here. A lit geometry re-shaded every vertex for each
       material, and every mesh copied the whole array. Shade each vertex once; give each mesh only the vertices its
       indices use, renumbered. */
    if (p4_mesh_compact && mesh_scratch(count)) {
        const Material *lit_for = 0;
        unsigned shade_generation = 0, full_generation = 0, full_colour = 0;
        Vertex3D *full = 0;
        unsigned geometry_key = 0;
        unsigned part[6] = { 0 }; /* the key after each input, for churn_why */
        int ge_lit = normals && p4_ge_light;
        int ge_colour = !normals && modulate && p4_material_state == 2;
        draw_material_ge = ge_colour;
        prof_begin(PROF_KEY);
        if (p4_mesh_cache) {
            unsigned h, nsample;
            if (lm_hit) {
                memcpy(part, lm->part, sizeof lm->part);
                h = part[3];
                nsample = lm->nsample;
            } else {
                h = 2166136261u;
                const void *pointers[5] = { position, uv, preLit, normals, (void *)modulate };
                h = hash_words(h, pointers, sizeof pointers);
                part[0] = h;
                h = hash_sample(h, position, count, sizeof *position);
                part[1] = h;
                if (uv)
                    h = hash_sample(h, uv, count, 8);
                part[2] = h;
                if (preLit)
                    h = hash_sample(h, preLit, count, 4);
                part[3] = h;
                nsample = normals ? hash_sample(2166136261u, normals, count, sizeof *normals) : 0;
            }
            h = hash_words(h, &count, 4);
            float shift[2] = { du, dv };
            h = hash_words(h, shift, sizeof shift);
            part[4] = h;
            if (normals && ge_lit) {
                h = hash_words(h, &ge_lit, 4); /* the GE lights it: light and rotation are not in the vertices */
                h = hash_words(h, &nsample, 4);
            } else if (normals) {
                h = hash_words(h, &light, sizeof light);
                if (model)
                    h = hash_words(h, model, 12 * 4); /* rotation rows; position does not light */
                h = hash_words(h, &nsample, 4);
            }
            h = hash_words(h, &p4_guard_split, 4); /* b232: a new split length builds new copies */
            h = hash_words(h, &p4_guard_split_grow, 4);
            part[5] = h;
            geometry_key = h | 1;
            if (lm && !lm_hit) {
                *lm = (LockMemo){ draw_geometry, position, uv, preLit, normals, count, du, dv, nsample, { part[0], part[1], part[2], part[3] } };
                draw_geometry->locked = 0;
            }
        }
        prof_end(PROF_KEY);
        /* which meshes touch a vertex beyond the band, decided before draw_guarded_compact overwrites guard_code */
        static unsigned char clip_flags[256];
        int compact_guard = guarded && p4_guard_compact && header->numMeshes <= 256;
        if (compact_guard && box_mode)
            memset(clip_flags, 1, header->numMeshes);
        else if (compact_guard) {
            const Mesh *mm = mesh;
            for (int i = 0; i < header->numMeshes; i++, mm++) {
                unsigned any = 0;
                const unsigned short *ix = mm->indices;
                for (int k = 0; k < mm->numIndices; k++)
                    any |= guard_code[ix[k]];
                clip_flags[i] = (any & GUARD_MASK) != 0;
            }
        }
        for (int i = 0; i < header->numMeshes; i++, mesh++) {
            if (probe_depth_reject(mesh, position, count, model))
                continue;
            if (frustum_reject(mesh, count, frustum_ready))
                continue;
            const Material *material = mesh->material;
            MeshCache *slot = 0;
            unsigned key = 0;
            int clip_this = 0;
            if (compact_guard)
                clip_this = clip_flags[i];
            else if (guarded) {
                unsigned any = 0;
                const unsigned short *ix = mesh->indices;
                for (int k = 0; k < mesh->numIndices; k++)
                    any |= guard_code[ix[k]];
                clip_this = (any & GUARD_MASK) != 0;
            }
            if (geometry_key && (!clip_this || compact_guard)) {
                /* b177: GE-lit vertices do not hold the material (bind_ge_light sends it): a fading or recoloured
                   material keeps its entry */
                unsigned mk[3] = { material->colour, *(unsigned *)&material->ambient, *(unsigned *)&material->diffuse };
                key = (ge_lit && p4_material_state) || ge_colour ? geometry_key : hash_words(geometry_key, mk, sizeof mk) | 1;
                int hit;
                prof_begin(PROF_MSLOT);
                slot = cache_slot(mesh, key, &hit);
                if (!hit && slot && slot->mesh == mesh && churn_why(slot - mesh_cache, part, count) == 1 && p4_churn_top)
                    churn_top_note(header, material, count);
                prof_end(PROF_MSLOT);
                if (!slot) /* every entry of the window drawn this frame: arrays rewritten per instance (Heaven's flowers) */
                    cache_noslot++;
                if (hit) {
                    prof_begin(PROF_HIT);
                    slot->frame = cache_frame;
                    cache_hits++;
                    if (p4_cache_audit && audit_hash && !(audit_tick++ & 7)) {
                        unsigned now[3];
                        audit_inputs(now, position, uv, preLit, count);
                        unsigned *was = audit_hash[slot - mesh_cache];
                        audit_checks++;
                        int stale = 0;
                        for (int a = 0; a < 3; a++)
                            if (now[a] != was[a]) {
                                audit_stale[a]++;
                                stale = 1;
                            }
                        audit_stale_vertices += stale ? count : 0;
                    }
                    prof_begin(PROF_HEMPTY); /* the profiler's own cost per section, to subtract */
                    prof_end(PROF_HEMPTY);
                    int sprim = SLOT_SPLIT(slot) ? GU_TRIANGLES : prim; /* b232: a split copy is a list */
                    if (clip_this && guard_slot_whole(slot, slot->key, slot->built, sprim, slot->idx, slot->indices, slot->v, slot->vertices, slot->vtype, model, near)) {
                        draw_mesh_hit(slot, sprim, material, modulate, &light); /* b199: no chunk needs the CPU */
                    } else if (clip_this) {
                        void **texture = material->texture;
                        prof_begin(PROF_HBIND);
                        bind_mask(material->colour >> 24);
                        bind_raster(texture ? *texture : 0, GU_REPEAT);
                        prof_end(PROF_HBIND);
                        draw_guarded_compact(sprim, slot->idx, slot->indices, slot->v, slot->vertices, slot->vtype, model, near, material, modulate, &light, &slot->alpha_lo, slot, slot->key, slot->built);
                    } else {
                        draw_mesh_hit(slot, sprim, material, modulate, &light);
                    }
                    prof_end(PROF_HIT);
                    continue;
                }
            }
            if (!ge_lit && (!lit_for || (normals && (material->ambient != lit_for->ambient || material->diffuse != lit_for->diffuse)))) {
                prof_begin(PROF_SHADE);
                ShadeSetup k;
                if (normals)
                    shade_setup(&k, model, material, &light);
                for (int j = 0; j < count; j++) {
                    unsigned base = preLit ? double_colour(preLit[j]) : normals ? 0xff000000 : 0xffffffff;
                    mesh_lit[j] = !normals ? base : p4_shade_fast ? shade_fast(base, &normals[j], &k) : shade_colour(base, &normals[j], model, material, &light);
                }
                PROF_COUNT(prof_shaded, count);
                if (normals)
                    work_shaded += count; /* only real lighting: without normals it is a colour copy */
                prof_end(PROF_SHADE);
                lit_for = material;
                shade_generation++;
            }
            if (clip_this && !compact_guard) { /* the whole array in its own numbering, for draw_guarded; shared while colour and shade hold */
                prof_begin(PROF_GDRAW);
                unsigned colour = modulate ? material->colour : 0xffffffff;
                int stride = ge_lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
                /* GE-lit: the array depends only on the geometry's inputs (geometry_key), not on the camera or the
                   material; keep it between frames like a compact mesh (b86 Heaven: 17 rebuilt a frame, sqrt each vertex) */
                MeshCache *full_slot = 0;
                if (!full && ge_lit && geometry_key && p4_guard_full_cache) {
                    int hit;
                    MeshCache *e = cache_slot(header, geometry_key, &hit);
                    if (hit && e->v && e->vertices == count) {
                        e->frame = cache_frame;
                        full = e->v;
                        guard_full_hits++;
                    } else if (e && !(e->mesh == header && e->built + 1 >= cache_frame)) { /* rebuilt last frame too: inputs move */
                        full_slot = e;
                    }
                }
                if (!full || (!ge_lit && (full_generation != shade_generation || full_colour != colour))) { /* GE-lit: one copy for every material */
                    int units = (count * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
                    if (vertex_used + units > MAX_VERTICES) {
                        p4_log("TRACE vertex pool full\n");
                        prof_end(PROF_GDRAW);
                        return;
                    }
                    full = vertex_pool + vertex_used;
                    vertex_used += units;
                    for (int j = 0; j < count; j++) {
                        if (ge_lit) {
                            put_lit_vertex((Vertex3DN *)full + j, position, uv, du, dv, preLit, normals, j);
                            continue;
                        }
                        full[j].u = uv ? uv[j][0] - du : 0;
                        full[j].v = uv ? uv[j][1] - dv : 0;
                        full[j].colour = modulate_colour(mesh_lit[j], colour);
                        full[j].x = position[j].x;
                        full[j].y = position[j].y;
                        full[j].z = position[j].z;
                    }
                    sceKernelDcacheWritebackRange(full, count * stride);
                    full_generation = shade_generation;
                    full_colour = colour;
                    guard_full_builds++;
                    if (full_slot) {
                        int need = count * stride;
                        if (full_slot->v && (full_slot->mesh != header || full_slot->capacity < need)) {
                            cache_bytes -= full_slot->capacity;
                            free(full_slot->v);
                            full_slot->v = 0;
                            full_slot->capacity = 0;
                        }
                        if (!full_slot->v && cache_bytes + need <= p4_cache_budget && (full_slot->v = memalign(16, need))) {
                            full_slot->capacity = need;
                            cache_bytes += need;
                        }
                        if (full_slot->v) {
                            mesh_set_split(full_slot, 0);
                            memcpy(full_slot->v, full, need);
                            sceKernelDcacheWritebackRange(full_slot->v, need);
                            full_slot->mesh = header;
                            full_slot->key = geometry_key;
                            full_slot->vertices = count;
                            full_slot->indices = 0;
                            full_slot->idx = 0;
                            full_slot->churn = 0;
                        } else {
                            full_slot->mesh = header; /* remembered as built, so a moving array is not copied every frame */
                            full_slot->key = 0;
                        }
                        full_slot->frame = full_slot->built = cache_frame;
                        full_slot->alpha_lo = -1;
                    }
                }
                guard_mesh_draws++;
                void **texture = material->texture;
                bind_mask(material->colour >> 24);
                bind_raster(texture ? *texture : 0, GU_REPEAT);
                draw_material_ge = 0; /* this array has the material in its vertices */
                draw_guarded(prim, mesh->indices, mesh->numIndices, full, ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D, near, material, modulate, &light);
                draw_material_ge = ge_colour;
                prof_end(PROF_GDRAW);
                continue;
            }
            int n = mesh->numIndices;
            int stride = ge_lit ? sizeof(Vertex3DN) : sizeof(Vertex3D), vtype = ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D;
            if (vertex_used + (n * stride + 23) / 24 > MAX_VERTICES || index_used + n > MAX_INDICES) {
                p4_log("TRACE vertex pool full\n");
                return;
            }
            unsigned colour = modulate && !ge_colour ? material->colour : 0xffffffff;
            char *out = (char *)(vertex_pool + vertex_used);
            unsigned short *remapped = index_pool + index_used;
            unsigned stamp = ++mesh_generation;
            int used = 0;
            const unsigned short *in = mesh->indices;
            prof_begin(PROF_EMIT);
            P4Topology *topology = p4_topology_cache ? p4_topology_get(&topology_cache, mesh, in, n, count,
                    cache_frame, p4_topology_budget, mesh_remap) : 0;
            const unsigned short *source = topology ? topology->data + n * 2 : 0;
            int iterations = topology ? topology->used : n;
            if (topology)
                memcpy(remapped, topology->data + n, n * 2);
            for (int k = 0; k < iterations; k++) {
                unsigned j = topology ? source[k] : in[k];
                if (topology || mesh_stamp[j] != stamp) {
                    if (!topology) {
                        mesh_stamp[j] = stamp;
                        mesh_remap[j] = used;
                    }
                    if (ge_lit) {
                        put_lit_vertex((Vertex3DN *)out + used, position, uv, du, dv, preLit, normals, j);
                    } else {
                        Vertex3D *v = (Vertex3D *)out + used;
                        v->u = uv ? uv[j][0] - du : 0;
                        v->v = uv ? uv[j][1] - dv : 0;
                        v->colour = modulate_colour(mesh_lit[j], colour);
                        v->x = position[j].x;
                        v->y = position[j].y;
                        v->z = position[j].z;
                    }
                    used++;
                }
                if (!topology)
                    remapped[k] = mesh_remap[j];
            }
            vertex_used += (used * stride + 23) / 24;
            index_used += n;
            PROF_COUNT(prof_emitted, used);
            if (ge_lit)
                work_ge_emitted += used;
            else
                work_cpu_emitted += used;
            prof_end(PROF_EMIT);
            if (slot && slot->mesh == mesh && slot->built + 1 >= cache_frame && ++slot->churn > 2) {
                slot->built = cache_frame; /* inputs change every frame: the copy would never be used */
                churn_remember(slot - mesh_cache, part);
                slot->key = key;
                slot->frame = cache_frame;
                cache_misses++;
                slot = 0;
            } else if (slot && slot->mesh == mesh && slot->built + 1 < cache_frame) {
                slot->churn = 0;
            }
            prof_begin(PROF_MCOPY);
            if (slot) {
                int need = used * stride + n * 2;
                if (slot->mesh != mesh)
                    slot->churn = 0;
                if (slot->mesh != mesh || slot->capacity < need) {
                    if (slot->v) {
                        cache_bytes -= slot->capacity;
                        free(slot->v);
                        slot->v = 0;
                        slot->capacity = 0;
                    }
                    if (cache_bytes + need <= p4_cache_budget && (slot->v = memalign(16, need))) {
                        slot->capacity = need;
                        cache_bytes += need;
                    }
                }
                int split_nv = 0, split_nidx = 0, split_bytes = 0;
                /* Build independently of this camera: an inside mesh may cross the band on a later frame. */
                void *split = slot->v ? guard_split_mesh(out, used, stride, ge_lit, remapped, n, prim == GU_TRIANGLE_STRIP,
                                                                      &split_nv, &split_nidx, &split_bytes) : 0;
                if (split && cache_bytes - slot->capacity + split_bytes > p4_cache_budget) {
                    free(split), split = 0;
                    split_refused++;
                }
                mesh_set_split(slot, 0);
                if (split) {
                    cache_bytes += split_bytes - slot->capacity;
                    free(slot->v);
                    slot->v = split, slot->capacity = split_bytes;
                    slot->idx = (unsigned short *)((char *)split + split_nv * stride);
                    slot->vtype = vtype;
                    slot->alpha_lo = -1;
                    mesh_set_split(slot, 1);
                    sceKernelDcacheWritebackRange(slot->v, split_bytes);
                } else if (slot->v) {
                    memcpy(slot->v, out, used * stride);
                    slot->idx = (unsigned short *)((char *)slot->v + used * stride);
                    slot->vtype = vtype;
                    slot->alpha_lo = -1; /* draw_tested scans it again */
                    memcpy(slot->idx, remapped, n * 2);
                    sceKernelDcacheWritebackRange(slot->v, need);
                }
                if (slot->v) {
                    slot->mesh = mesh;
                    slot->key = key;
                    slot->frame = cache_frame;
                    slot->built = cache_frame;
                    churn_remember(slot - mesh_cache, part);
                    if (p4_cache_audit && (audit_hash || (audit_hash = calloc(CACHE_SLOTS, sizeof *audit_hash))))
                        audit_inputs(audit_hash[slot - mesh_cache], position, uv, preLit, count);
                    slot->vertices = SLOT_SPLIT(slot) ? split_nv : used;
                    slot->indices = SLOT_SPLIT(slot) ? split_nidx : n;
                    cache_builds++;
                } else {
                    slot->mesh = 0;
                    cache_misses++;
                }
            }
            prof_end(PROF_MCOPY);
            prof_begin(PROF_SUBMIT);
            sceKernelDcacheWritebackRange(out, used * stride);
            sceKernelDcacheWritebackRange(remapped, n * 2);
            void **texture = material->texture;
            bind_mask(material->colour >> 24);
            bind_raster(texture ? *texture : 0, GU_REPEAT);
            if (clip_this)
                draw_guarded_compact(prim, remapped, n, out, used, vtype, model, near, material, modulate, &light, 0, 0, 0, 0);
            else
                draw_mesh_vertices(prim, vtype, n, remapped, out, material, modulate, &light);
            prof_end(PROF_SUBMIT);
        }
        return;
    }
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        if (probe_depth_reject(mesh, position, count, model))
            continue;
        if (frustum_reject(mesh, count, frustum_ready))
            continue;
        unsigned colour = modulate ? mesh->material->colour : 0xffffffff;
        if (!vertices || colour != previous_colour || (normals && mesh->material != previous_material)) {
            if (vertex_used + count > MAX_VERTICES) {
                p4_log("TRACE vertex pool full\n");
                return;
            }
            vertices = vertex_pool + vertex_used;
            vertex_used += count;
            for (int j = 0; j < count; j++) {
                vertices[j].u = uv ? uv[j][0] - du : 0;
                vertices[j].v = uv ? uv[j][1] - dv : 0;
                /* lit without prelight: RenderWare starts from black and adds the lights */
                unsigned base = preLit ? double_colour(preLit[j]) : normals ? 0xff000000 : 0xffffffff;
                if (normals) base = shade_colour(base, &normals[j], model, mesh->material, &light);
                vertices[j].colour = modulate_colour(base, colour);
                vertices[j].x = position[j].x;
                vertices[j].y = position[j].y;
                vertices[j].z = position[j].z;
            }
            sceKernelDcacheWritebackRange(vertices, count * sizeof *vertices);
            previous_colour = colour;
            previous_material = mesh->material;
        }
        if (census3d_skip(mesh->material, VT_3D, mesh->numIndices, modulate))
            continue;
        void **texture = mesh->material->texture;
        bind_mask(mesh->material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        if (guarded) {
            draw_guarded(prim, mesh->indices, mesh->numIndices, vertices, VT_3D, near, mesh->material, modulate, &light);
            continue;
        }
        sceKernelDcacheWritebackRange(mesh->indices, mesh->numIndices * 2);
        draw_material_alpha = colour >> 24;
        draw_tested(prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, mesh->numIndices, mesh->indices, vertices);
    }
}

const Matrix *p4r_003e9700(void *frame);

/* RpHAnimHierarchyUpdateMatrices (func_00397c40) in C, read off the reassembled MIPS (b142). Hierarchy: 0 flags,
   4 numNodes, 8 pMatrixArray, 16 pNodeInfo (16 bytes: id, index, flags, pFrame), 20 parentFrame, 24 parentHierarchy,
   28 rootParentOffset, 32 currentAnim. Interpolator: 36 frame size, 60 keyFrameToMatrixCB, 76 first frame (quat at
   +8, translation at +24). Frame: 3 private flags, 4 parent, 8 dirty link, 16 modelling, 80 ltm, 160 root.
   b141 Heaven: 9 calls, 0.64 Mcycles. p4_hanim_c 1 uses this; p4_hanim_check compares it with the original. */
int p4_hanim_c = 1, p4_hanim_check = 0;
/* b188: the bone multiplies of p4_hanim_update on the VFPU (multiply_vfpu, flags as RwMatrixMultiply sets them);
   an unaligned matrix falls back to RwMatrixMultiply and is counted. b142 found this update memory-bound (~3 data
   misses a bone), so the gain is unproven until a device A/B. p4_hanim_c is chosen in hanim_update_now since b188:
   the dispatch called it directly and so skipped the frameskip, far-LOD and NaN checks of func_00397c40. */
int p4_hanim_vfpu = 1;
static unsigned hanim_vfpu_unaligned;
int p4_hanim_update(unsigned *h);
/* b148: on a skipped frame (frameskip) the bone matrices and bone frames are not rebuilt: nothing draws them, and the
   next drawn frame rebuilds them from the animation, whose time still advances (RtAnimInterpolatorAddAnimTime is not
   this). Callers reach the original as p4r_00397c40. */
int p4_frameskip_bones = 1;
int p4_pose_plain = 0; /* b153 experiment: 1 = the look-at pose (mdlUpdateHierarchyPose) replaced by the plain bone update */
/* b154: the hierarchy scale hook (mdlInstallHierarchyScaleHook, layer flag 0x10) scales one node's matrix by a vector
   from the layer. Logged for each new layer and whenever a component is near 0 or huge. */
void p4_scale_hook_note(void *layer, int node, const float *scale, float model_scale, float aspect, int speed_zero)
{
    static void *seen[16];
    static unsigned odd_logged;
    int known = 0;
    for (int i = 0; i < 16; i++)
        known |= seen[i] == layer;
    int odd = 0;
    for (int k = 0; k < 3; k++) {
        float a = scale[k] < 0 ? -scale[k] : scale[k];
        odd |= !(a > 0.05f && a < 20.0f);
    }
    if (known && !(odd && odd_logged < 32))
        return;
    if (!known)
        for (int i = 0; i < 16; i++)
            if (!seen[i]) {
                seen[i] = layer;
                break;
            }
    odd_logged += odd;
    p4_log("TRACE scale hook layer %p node %d scale %d %d %d /1000 model %d aspect %d /1000 speed0 %d%s\n", layer, node,
           (int)(scale[0] * 1000), (int)(scale[1] * 1000), (int)(scale[2] * 1000), (int)(model_scale * 1000), (int)(aspect * 1000),
           speed_zero, odd ? " ODD" : "");
}
int p4r_00397c40(void *hierarchy);
/* b156: the head of a party member drawn as a line in battle, in some rounds. b155 caught it: bones 38-43 of a 93-node
   hierarchy with NaN matrices. RpHAnimKeyFrameInterpolate (p4r_003955a0) divides by t2 - t1; two keys with the same
   time give 0/0. The EE's FPU gives the largest finite value there and huge x 0 = 0, so the result is key 1; the PSP
   gives NaN, and the bone and its children go with it. Each interpolated frame (RpHAnimInterpFrame: keyFrame1,
   keyFrame2, q, t) is checked before the matrices are built; one with a NaN or infinity takes key 1's q and t. */
int p4_hanim_nan_fix = 1;
static unsigned hanim_nan_logged;
/* b158: bones 38-43 bad at skinning but not right after any update: which hierarchy, updated when? */
static struct { const void *h; unsigned frame, count; } hanim_seen[64];
static void hanim_note(const void *h)
{
    unsigned slot = ((unsigned)h >> 4) * 2654435761u >> 26;
    for (int k = 0; k < 64; k++) {
        int i = (slot + k) & 63;
        if (hanim_seen[i].h == h || !hanim_seen[i].h) {
            hanim_seen[i].h = h, hanim_seen[i].frame = cache_frame, hanim_seen[i].count++;
            return;
        }
    }
}
/* b213 p4_head_trace: the headless party members (b155-b181: bones 38-43 of the hero, 34.. of Naoto, beyond 1e7 at
   skinning; b181 took it for a part the game parks). Right after each hierarchy update, the first node whose matrix
   is beyond 1e7: its interpolated frame (q, t), both keys (time, q, t), the interpolator's time, the node's flags and
   whether its local matrix or its parent is the bad one. One line per hierarchy and state change, 24 at most. */
int p4_head_trace = 0;
static unsigned head_trace_logged;
static const void *head_trace_last[16];
static int head_trace_node[16];
static int head_big(const float *f, int n)
{
    for (int k = 0; k < n; k++)
        if (!(f[k] == f[k] && f[k] < 1e7f && f[k] > -1e7f))
            return 1;
    return 0;
}
static void head_trace(unsigned *h)
{
    const float *m = (const float *)h[2];
    char *interp = (char *)h[8];
    int n = (int)h[1];
    if (!m || !interp || n <= 0 || n > 256 || (unsigned)interp - 0x08800000u >= 0x03800000u)
        return;
    int bad = -1;
    for (int i = 0; i < n && bad < 0; i++)
        if (head_big(m + i * 16, 3) || head_big(m + i * 16 + 4, 3) || head_big(m + i * 16 + 8, 3) || head_big(m + i * 16 + 12, 3))
            bad = i;
    int slot = ((unsigned)h >> 4) & 15;
    if (head_trace_last[slot] == h && head_trace_node[slot] == bad)
        return;
    head_trace_last[slot] = h, head_trace_node[slot] = bad;
    if (bad < 0 || head_trace_logged >= 24)
        return;
    head_trace_logged++;
    int stride = *(int *)(interp + 36);
    const char *f = interp + 76 + bad * stride, *k1 = *(const char **)f, *k2 = *(const char **)(f + 4);
    const float *q = (const float *)(f + 8);
    const unsigned *info = (const unsigned *)h[4] + bad * 4;
    #define K1000(x) (int)((x) > 2e6f ? 2e9f : (x) < -2e6f ? -2e9f : (x) * 1000)
    p4_log("TRACE head h %p node %d/%d flags %x anim %p time %d: frame q %d %d %d %d t %d %d %d (x1000)\n", (void *)h, bad, n, info[2],
           *(void **)interp, K1000(*(float *)(interp + 4)), K1000(q[0]), K1000(q[1]), K1000(q[2]), K1000(q[3]), K1000(q[4]), K1000(q[5]), K1000(q[6]));
    for (int j = 0; j < 2; j++) {
        const char *k = j ? k2 : k1;
        if (!k || (unsigned)k - 0x08800000u >= 0x03800000u || ((unsigned)k & 3)) {
            p4_log("TRACE head   key%d %p (not a key)\n", j + 1, (const void *)k);
            continue;
        }
        const float *kq = (const float *)(k + 8);
        p4_log("TRACE head   key%d %p prev %p time %d q %d %d %d %d t %d %d %d\n", j + 1, (const void *)k, *(void **)k, K1000(*(float *)(k + 4)),
               K1000(kq[0]), K1000(kq[1]), K1000(kq[2]), K1000(kq[3]), K1000(kq[4]), K1000(kq[5]), K1000(kq[6]));
    }
    const float *b = m + bad * 16;
    p4_log("TRACE head   matrix right %d %d %d up %d %d %d at %d %d %d pos %d %d %d; node before %s; frame %p\n", K1000(b[0]), K1000(b[1]), K1000(b[2]),
           K1000(b[4]), K1000(b[5]), K1000(b[6]), K1000(b[8]), K1000(b[9]), K1000(b[10]), K1000(b[12]), K1000(b[13]), K1000(b[14]),
           bad ? (head_big(b - 16, 3) || head_big(b - 4, 3) ? "bad" : "fine") : "-", (void *)info[3]);
    #undef K1000
}
/* b214 around mdlUpdateHierarchyPose (census FIXES): the head look-at (node id 0x1389). b213 on the PSP: the bones
   go bad inside mainDraw, after a clean hierarchy update, from bone 38 (the head) down. The look state (MdlLookState
   at controllerState: flags, blend, limits, rotation, limitedRotation, scale, target, offsets) keeps its rotation
   between ticks and slerps it toward the target, so one NaN stays for good: a headless member for the rest of the
   battle. p4_head_trace logs the call where the head goes bad (look state before and after). p4_look_fix 1: a look
   rotation that is not finite before the call is reseeded from the animation (flag 0x200, the game's own reset);
   one made non-finite by the call is put back; a body pose (no parent transform) with a bad head is redone as the
   plain bone update for that tick. */
int p4_look_fix = 1;
static unsigned look_calls, look_bad_before, look_bad_after, look_redone, look_logged;
static unsigned char look_before[0x50] __attribute__((aligned(4)));
static int look_head_node(const unsigned *h)
{
    const unsigned *info = (const unsigned *)h[4];
    int n = (int)h[1];
    if (!info || n <= 0 || n > 256)
        return -1;
    for (int i = 0; i < n; i++)
        if (info[i * 4] == 0x1389)
            return i;
    return -1;
}
static void look_print(const char *when, const unsigned char *c)
{
    const float *f = (const float *)c;
    #define L1000(x) (int)((x) == (x) ? (x) > 2e6f ? 2e9f : (x) < -2e6f ? -2e9f : (x) * 1000 : -1)
    p4_log("TRACE look %s flags %04x blend %d limits %d %d rot %d %d %d %d lim %d %d %d %d scale %d %d %d target %d %d %d off %d %d\n", when,
           *(const unsigned short *)c, L1000(f[1]), L1000(f[2]), L1000(f[3]), L1000(f[4]), L1000(f[5]), L1000(f[6]), L1000(f[7]), L1000(f[8]),
           L1000(f[9]), L1000(f[10]), L1000(f[11]), L1000(f[12]), L1000(f[13]), L1000(f[14]), L1000(f[15]), L1000(f[16]), L1000(f[17]),
           L1000(f[18]), L1000(f[19]));
    #undef L1000
}
/* b218: values inside the look-at call (census FIXES probes 0-7), cleared at its start, printed when it goes bad */
static float look_probe_v[8][12];
static unsigned char look_probe_n[8];
void p4_look_probe(int id, const float *v, int n)
{
    if (id < 0 || id >= 8 || !p4_head_trace)
        return;
    if (n > 12)
        n = 12;
    memcpy(look_probe_v[id], v, n * sizeof *v);
    look_probe_n[id] = n;
}
static void look_probe_print(void)
{
    static const char *const names[8] = { "angles p bp y by look3 wv3", "rot from matrix", "output matrix", "rot after limits",
                                          "slerp cache from to omega flag", "blended limit", "fallback angle q", "local matrix" };
    for (int id = 0; id < 8; id++) {
        if (!look_probe_n[id]) {
            p4_log("TRACE look   %d %s: not reached\n", id, names[id]);
            continue;
        }
        char line[200];
        int at = 0;
        for (int k = 0; k < look_probe_n[id] && at < 180; k++) {
            float x = look_probe_v[id][k];
            at += snprintf(line + at, sizeof line - at, " %d", x != x ? -999999999 : x > 2e6f ? 2000000000 : x < -2e6f ? -2000000000 : (int)(x * 1000));
        }
        p4_log("TRACE look   %d %s:%s\n", id, names[id], line);
    }
}
int func_00397c40(void *hierarchy);
void p4_look_check(void *hierarchy, void *controller, void *parent, int after)
{
    unsigned *h = hierarchy;
    unsigned char *c = controller;
    if (!h || !c)
        return;
    float *rot = (float *)(c + 16);
    if (!after) {
        look_calls++;
        memcpy(look_before, c, sizeof look_before);
        memset(look_probe_n, 0, sizeof look_probe_n);
        if (head_big(rot, 4)) {
            look_bad_before++;
            if (p4_look_fix) {
                rot[0] = rot[1] = rot[2] = 0.0f, rot[3] = 1.0f;
                *(unsigned short *)c |= 0x200;
            }
        }
        return;
    }
    int head = look_head_node(h);
    const float *m = (const float *)h[2];
    int bad_matrix = head >= 0 && m && (head_big(m + head * 16, 3) || head_big(m + head * 16 + 4, 3) || head_big(m + head * 16 + 8, 3) ||
                                        head_big(m + head * 16 + 12, 3));
    int bad_rot = head_big(rot, 4);
    if (!bad_matrix && !bad_rot)
        return;
    look_bad_after++;
    if (p4_head_trace && look_logged < 8) {
        look_logged++;
        p4_log("TRACE look went bad: hierarchy %p head node %d parent %p matrix %s rotation %s\n", (void *)h, head, parent,
               bad_matrix ? "bad" : "fine", bad_rot ? "bad" : "fine");
        look_print("before", look_before);
        look_print("after ", c);
        look_probe_print();
    }
    if (!p4_look_fix)
        return;
    if (bad_rot) {
        memcpy(rot, look_before + 16, 16);
        if (head_big(rot, 4))
            rot[0] = rot[1] = rot[2] = 0.0f, rot[3] = 1.0f;
        *(unsigned short *)c |= 0x200;
    }
    if (bad_matrix && !parent) {
        func_00397c40(h);
        look_redone++;
    }
}
static void look_report(void)
{
    extern unsigned p4_acos_clamped;
    extern unsigned p4_2d_nan_report(void);
    extern unsigned p4_rwmul_vfpu_calls, p4_rwmul_fpu_calls;
    extern int p4_rwmul_vfpu;
    p4_log("rw matrix multiply vfpu %d: vfpu %u fpu %u per 256 frames\n", p4_rwmul_vfpu, p4_rwmul_vfpu_calls, p4_rwmul_fpu_calls);
    p4_rwmul_vfpu_calls = p4_rwmul_fpu_calls = 0;
    p4_log("look fix %d: calls %u bad rotation before %u bad after %u redone %u; acos inputs clamped %u; 2d draws with NaN corners dropped %u\n",
           p4_look_fix, look_calls, look_bad_before, look_bad_after, look_redone, p4_acos_clamped, p4_2d_nan_report());
    look_calls = look_bad_before = look_bad_after = look_redone = p4_acos_clamped = 0;
}
/* b159: after every task, the hierarchies updated in the last 2 frames are checked; the first task after which one has
   a bad bone matrix (it was clean right after its update) is logged: who writes over the bones. */
static unsigned char hanim_bad_seen[64];
static int hanim_matrix_bad(const float *m)
{
    for (int k = 0; k < 16; k++)
        if (k % 4 != 3 && !(m[k] == m[k] && m[k] < 1e8f && m[k] > -1e8f))
            return 1;
    return 0;
}
void hanim_scan_after(const char *task, unsigned fn)
{
    static unsigned logged;
    if (!p4_bone_check || logged >= 16)
        return;
    for (int i = 0; i < 64; i++) {
        const unsigned *h = hanim_seen[i].h;
        if (!h || cache_frame - hanim_seen[i].frame > 2)
            continue;
        const float *m = (const float *)h[2];
        int n = (int)h[1];
        if (!m || n <= 0 || n > 256 || (unsigned)m - 0x08800000u >= 0x03800000u)
            continue;
        int bad = -1;
        for (int b = 0; b < n; b++)
            if (hanim_matrix_bad(m + b * 16)) {
                bad = b;
                break;
            }
        if (bad < 0) {
            hanim_bad_seen[i] = 0;
            continue;
        }
        if (hanim_bad_seen[i])
            continue;
        hanim_bad_seen[i] = 1;
        logged++;
        char name[25];
        memcpy(name, task, 24);
        name[24] = 0;
        p4_log("TRACE bones went bad during task %s fn %08x: hierarchy %p bone %d/%d, updated %d frames ago, matrices at %p\n", name, fn,
               (const void *)h, bad, n, (int)(cache_frame - hanim_seen[i].frame), (const void *)m);
    }
}
int hanim_age(const void *h)
{
    for (int i = 0; i < 64; i++)
        if (hanim_seen[i].h == h)
            return (int)(cache_frame - hanim_seen[i].frame);
    return -1;
}
static inline int hanim_bad(const float *f, int n)
{
    for (int k = 0; k < n; k++)
        if (!(f[k] == f[k] && f[k] < 1e30f && f[k] > -1e30f))
            return 1;
    return 0;
}
static void hanim_sanitize(unsigned *h)
{
    char *interp = (char *)h[8];
    if (!interp || (unsigned)interp - 0x08800000u >= 0x03800000u)
        return;
    int n = (int)h[1], stride = *(int *)(interp + 36);
    if (n <= 0 || n > 256 || stride < 36 || stride > 256)
        return;
    char *frame = interp + 76;
    for (int i = 0; i < n; i++, frame += stride) {
        float *qt = (float *)(frame + 8); /* q x y z w, t x y z */
        if (!hanim_bad(qt, 7))
            continue;
        const char *key1 = *(const char **)frame;
        if (key1 && (unsigned)key1 - 0x08800000u < 0x03800000u && !((unsigned)key1 & 3) && !hanim_bad((const float *)(key1 + 8), 7)) {
            memcpy(qt, key1 + 8, 7 * sizeof(float));
        } else {
            qt[0] = qt[1] = qt[2] = 0, qt[3] = 1; /* no usable key: no rotation, no offset */
            qt[4] = qt[5] = qt[6] = 0;
        }
        hanim_nan_fixed++;
    }
}
/* b171: bone-matrix updates by what the frame draws. A hierarchy whose skin is drawn (skin_bones) is "skinned"; its
   last drawn tick and clip w are kept. p4_anim_cull: a skinned hierarchy not drawn in the last 2 ticks is not updated;
   skin_bones rebuilds its matrices when it is drawn again. p4_anim_far: one last drawn farther than p4_anim_far_depth
   is updated every p4_anim_far_step ticks and drawn with the older matrices in between. Hierarchies never drawn as
   skins (rigid parts on bone frames) always update. The counters run with both off: what each would save. */
int p4_anim_cull = 0, p4_anim_far = 1, p4_anim_far_step = 2; /* b173: far on (b172 class, by eye: fine) */
float p4_anim_far_depth = 500.0f;
enum { ANIM_STATES = 128 };
typedef struct { const void *h, *matrices, *interp; int nodes; unsigned drawn, updated, advanced, moved, used[8]; float depth, pending, at[3]; unsigned char stale, skinned, lod; } AnimState;
static AnimState anim_state[ANIM_STATES];
/* b228: each state's interpolator in an array of its own. AddAnimTime looked an interpolator up by scanning the
   128 states (80 bytes each, a cache line apiece), ~10k cycles a call, ~100 calls a drawn tick, most of them for
   interpolators that are no character's (UV animations): kadd minus its interpolation was ~1.2 Mcycles a frame. */
static const void *anim_interp_of[ANIM_STATES];
static unsigned anim_updates, anim_unskinned, anim_unseen, anim_far_due, anim_culled, anim_far_skipped, anim_lazy, anim_full;
static unsigned anim_draws, anim_depth_bins[6];
static float anim_depth_max;
extern unsigned p4_anim_tick;
static AnimState *anim_find(const void *h)
{
    const unsigned *w = h;
    unsigned slot = ((unsigned)h >> 4) * 2654435761u >> 25;
    for (int k = 0; k < ANIM_STATES; k++) {
        AnimState *s = &anim_state[(slot + k) & (ANIM_STATES - 1)];
        if (s->h && s->h != h)
            continue;
        if (!s->h || s->nodes != (int)w[1] || s->matrices != (const void *)w[2]) { /* new, or another hierarchy here now */
            memset(s, 0, sizeof *s);
            anim_interp_of[s - anim_state] = 0;
            s->h = h, s->nodes = (int)w[1], s->matrices = (const void *)w[2];
        }
        return s;
    }
    anim_full++;
    return 0;
}
/* b175: the bone matrices carry the character's place in the world; held while it walked, the body lagged a step
   behind its shadow on every other tick (b174 Heaven: "my character doubles"). Only a character whose model has not
   moved for 8 ticks is held. */
static unsigned anim_moving_kept;
static int anim_far_hold(const AnimState *s)
{
    if (!p4_anim_far || !(s->depth > p4_anim_far_depth) || p4_anim_tick - s->updated >= (unsigned)p4_anim_far_step)
        return 0;
    if (p4_anim_tick - s->moved < 8) {
        anim_moving_kept++;
        return 0;
    }
    return 1;
}
static int hanim_update_now(void *hierarchy);
static void anim_drawn(const void *hierarchy)
{
    AnimState *s = anim_find(hierarchy);
    if (!s)
        return;
    anim_draws++;
    s->skinned = 1;
    s->drawn = p4_anim_tick;
    if (s->stale && !anim_far_hold(s)) {
        prof_begin(PROF_KMAT);
        hanim_update_now((void *)hierarchy);
        prof_end(PROF_KMAT);
        s->stale = 0, s->updated = p4_anim_tick;
        anim_lazy++;
    }
}
static void anim_seen_at(const void *hierarchy, float depth, const Matrix *model)
{
    AnimState *s = anim_find(hierarchy);
    if (!s)
        return;
    s->depth = depth;
    if (model) {
        const float *at = &model->pos.x;
        float d = 0;
        for (int k = 0; k < 3; k++)
            d += fabsf(at[k] - s->at[k]), s->at[k] = at[k];
        if (d > 0.01f)
            s->moved = p4_anim_tick;
    } else
        s->moved = p4_anim_tick;
    s->interp = (const void *)((const unsigned *)hierarchy)[8]; /* the hierarchy's interpolator (hanim_sanitize) */
    anim_interp_of[s - anim_state] = s->interp;
    static const float edge[5] = { 250, 500, 1000, 2000, 4000 };
    int bin = 0;
    while (bin < 5 && depth >= edge[bin])
        bin++;
    anim_depth_bins[bin]++;
    if (depth > anim_depth_max)
        anim_depth_max = depth;
}
void p4_anim_report(void)
{
    p4_log("anim cull %d far %d depth %d step %d: updates %u unskinned %u unseen %u far_due %u culled %u far_skipped %u moving_kept %u lazy %u draws %u full %u\n",
           p4_anim_cull, p4_anim_far, (int)p4_anim_far_depth, p4_anim_far_step, anim_updates, anim_unskinned, anim_unseen, anim_far_due,
           anim_culled, anim_far_skipped, anim_moving_kept, anim_lazy, anim_draws, anim_full);
    anim_moving_kept = 0;
    p4_log("anim depth of drawn skins <250 %u <500 %u <1000 %u <2000 %u <4000 %u more %u max %d\n", anim_depth_bins[0], anim_depth_bins[1],
           anim_depth_bins[2], anim_depth_bins[3], anim_depth_bins[4], anim_depth_bins[5], (int)anim_depth_max);
    anim_updates = anim_unskinned = anim_unseen = anim_far_due = anim_culled = anim_far_skipped = anim_lazy = anim_draws = anim_full = 0;
    memset(anim_depth_bins, 0, sizeof anim_depth_bins);
    anim_depth_max = 0;
    { void bone_census_report(void); bone_census_report(); }
}
/* b208 p4_bone_census: which nodes of a drawn skinned hierarchy anybody needs. Weighted: some skin drawn with it has a
   vertex weight on the node. Attached: the node's RwFrame holds objects (objectList at +144 not empty: a weapon, an
   atomic). Live: those and their ancestors (RpHAnim node flags: 2 push the parent, 1 pop it). Dead: the rest, which
   neither the interpolation nor the matrix build would need. A count only: the game may still read a dead node's
   matrix (effects on frames, mdlCopyIndexedMatrix), so nothing is skipped. */
int p4_bone_census = 0;
static void bone_census_note(const void *hierarchy, const unsigned *used)
{
    AnimState *st = anim_find(hierarchy);
    if (st)
        for (int k = 0; k < 8; k++)
            st->used[k] |= used[k];
}
void bone_census_report(void)
{
    if (!p4_bone_census)
        return;
    unsigned hier = 0, nodes = 0, weighted = 0, attached = 0, live = 0, shown = 0;
    for (int i = 0; i < ANIM_STATES; i++) {
        AnimState *st = &anim_state[i];
        const unsigned *h = st->h;
        int used_any = 0;
        for (int k = 0; k < 8; k++)
            used_any |= st->used[k] != 0;
        if (!h || !st->skinned || !used_any || p4_anim_tick - st->drawn > 300)
            continue;
        int n = (int)h[1];
        const char *info = (const char *)h[4];
        if (n <= 0 || n > 256 || !info)
            continue;
        short parent[256], stack[256];
        unsigned char keep[256];
        int cur = -1, depth = 0, w = 0, a = 0, l = 0;
        for (int j = 0; j < n; j++) {
            unsigned flags = *(const unsigned *)(info + j * 16 + 8);
            parent[j] = cur;
            if ((flags & 2) && depth < 256)
                stack[depth++] = cur;
            cur = (flags & 1) && depth > 0 ? stack[--depth] : j;
            const char *frame = *(char *const *)(info + j * 16 + 12);
            int weighted_j = st->used[j >> 5] >> (j & 31) & 1;
            int attached_j = frame && (unsigned)frame - 0x08800000u < 0x03800000u && *(char *const *)(frame + 144) != frame + 144;
            keep[j] = weighted_j || attached_j;
            w += weighted_j, a += attached_j && !weighted_j;
        }
        for (int j = n - 1; j >= 0; j--) /* parents come before their children */
            if (keep[j] && parent[j] >= 0)
                keep[parent[j]] = 1;
        for (int j = 0; j < n; j++)
            l += keep[j];
        hier++, nodes += n, weighted += w, attached += a, live += l;
        if (shown++ < 4)
            p4_log("bone census hierarchy %p: nodes %d weighted %d attached only %d live %d dead %d\n", (const void *)h, n, w, a, l, n - l);
        memset(st->used, 0, sizeof st->used);
    }
    if (hier)
        p4_log("bone census: %u drawn skinned hierarchies, nodes %u, weighted %u, attached only %u, live with ancestors %u, dead %u\n", hier, nodes,
               weighted, attached, live, nodes - live);
}
/* b172: RpHAnim's compressed-keyframe interpolate (p4r_00415bc0, the AddAnimTime callback; ~1000 cycles a node on
   the FPU: polynomial acos and three polynomial sines). Read off the MIPS: keys {prev, time, q x y z w, t x y z} with
   16-bit reals (sign, 4-bit exponent from 2^-15, 11-bit mantissa); f = (time - t1) / (t2 - t1); t = (t1 + f (t2 - t1))
   x scale + offset (custom data: offset[3], scale[3]); q2 negated when q1.q2 < 0; slerp when the dot is below 0.999,
   else the plain weights 1 - f, f. p4_interp_fast: the same with the trigonometry on the VFPU; check: the original
   too, compared. */
typedef void (*KInterp)(void *out, void *in1, void *in2, float time, void *data);
int p4_interp_fast = 0, p4_interp_check = 0;
static unsigned interp_calls, interp_slerps, interp_checks, interp_diffs, interp_other;
static float interp_max_diff;
typedef struct { void *prev; float time; unsigned short q[4], t[3]; } CKey;
typedef struct { void *k1, *k2; float q[4], t[3]; } IFrame;
void p4r_00415bc0(void *out, void *in1, void *in2, float time, void *data);
static inline float rw_half(unsigned short h)
{
    union { unsigned u; float f; } v;
    v.u = (h & 0x8000u) << 16;
    if (h & 0x7fff)
        v.u |= (((h & 0x7800u) << 12) + 0x38000000u) | ((h & 0x7ffu) << 12);
    return v.f;
}
static void interp_fast(void *out_, void *in1_, void *in2_, float time, void *data)
{
    const CKey *a = in1_, *b = in2_;
    IFrame *o = out_;
    const float *cd = data;
    float f = (time - a->time) / (b->time - a->time);
    float q1[4], q2[4];
    for (int i = 0; i < 4; i++)
        q1[i] = rw_half(a->q[i]), q2[i] = rw_half(b->q[i]);
    for (int i = 0; i < 3; i++) {
        float t1 = rw_half(a->t[i]), t2 = rw_half(b->t[i]);
        o->t[i] = (t1 + f * (t2 - t1)) * cd[3 + i] + cd[i];
    }
    float dot = q1[1] * q2[1] + q1[0] * q2[0] + q1[2] * q2[2] + q1[3] * q2[3];
    if (dot < 0) {
        for (int i = 0; i < 4; i++)
            q2[i] = -q2[i];
        dot = -dot;
    }
    float w[4] __attribute__((aligned(16))) = { dot, 1 - f, f, 0 };
    if (dot < 0.999f) {
        /* angles in quarter turns: vasin gives asin x 2/pi, vsin takes x pi/2; theta = 1 - asin(dot) */
        __asm__ volatile(
            "lv.q    C000, 0(%[w])\n"
            "vasin.s S010, S000\n"
            "vocp.s  S010, S010\n"
            "vmul.s  S011, S001, S010\n"
            "vmul.s  S012, S002, S010\n"
            "vsin.s  S020, S010\n"
            "vsin.s  S021, S011\n"
            "vsin.s  S022, S012\n"
            "vrcp.s  S020, S020\n"
            "vmul.s  S001, S021, S020\n"
            "vmul.s  S002, S022, S020\n"
            "sv.q    C000, 0(%[w])\n"
            : : [w] "r"(w) : "memory");
        interp_slerps++;
    }
    for (int i = 0; i < 4; i++)
        o->q[i] = w[2] * q2[i] + w[1] * q1[i];
}
/* b239 p4_interp_fast 2: the whole node on the VFPU. The 14 packed reals are unpacked with integer code into an
   aligned block, f is the FPU division retail does, and the lerp of q and t (with the custom scale and offset) is
   ~10 VFPU instructions instead of ~60 FPU ones. Slerp nodes (|dot| < 0.999, ~5% in the dialogue) and a NaN dot
   take the retail function. Reads every input before it writes. p4_interp_check compares with retail as for 1. */
static inline unsigned rw_half_bits(unsigned h)
{
    unsigned v = (h & 0x8000u) << 16;
    if (h & 0x7fff)
        v |= (((h & 0x7800u) << 12) + 0x38000000u) | ((h & 0x7ffu) << 12);
    return v;
}
static unsigned interp_vfpu_nodes, interp_vfpu_slerps;
static void interp_vfpu(void *out_, void *in1_, void *in2_, float time, void *data)
{
    const CKey *a = in1_, *b = in2_;
    unsigned w[16] __attribute__((aligned(16)));
    union { float f; unsigned u; } f, dot;
    f.f = (time - a->time) / (b->time - a->time);
    for (int i = 0; i < 4; i++)
        w[i] = rw_half_bits(a->q[i]), w[4 + i] = rw_half_bits(b->q[i]);
    for (int i = 0; i < 3; i++)
        w[8 + i] = rw_half_bits(a->t[i]), w[12 + i] = rw_half_bits(b->t[i]);
    w[11] = w[15] = 0;
    __asm__ volatile(
        "lv.q    C000, 0(%[w])\n lv.q C010, 16(%[w])\n lv.q C020, 32(%[w])\n lv.q C030, 48(%[w])\n"
        "vdot.q  S100, C000, C010\n"
        "mfv     %[d], S100\n"
        : [d] "=r"(dot.u) : [w] "r"(w) : "memory");
    int neg = dot.f < 0;
    if (!((neg ? -dot.f : dot.f) >= 0.999f)) {
        interp_vfpu_slerps++;
        p4r_00415bc0(out_, in1_, in2_, time, data);
        return;
    }
    unsigned g = f.u ^ (neg ? 0x80000000u : 0); /* the weight of q2: f, negated with q2 */
    __asm__ volatile(
        "mtv     %[f], S101\n mtv %[g], S102\n"
        "vocp.s  S103, S101\n"
        "lv.s    S110, 0(%[cd])\n lv.s S111, 4(%[cd])\n lv.s S112, 8(%[cd])\n"
        "lv.s    S120, 12(%[cd])\n lv.s S121, 16(%[cd])\n lv.s S122, 20(%[cd])\n"
        "vscl.q  C010, C010, S102\n"
        "vscl.q  C000, C000, S103\n"
        "vadd.q  C000, C010, C000\n"
        "vsub.t  C030, C030, C020\n"
        "vscl.t  C030, C030, S101\n"
        "vadd.t  C020, C020, C030\n"
        "vmul.t  C020, C020, C120\n"
        "vadd.t  C020, C020, C110\n"
        "sv.s    S000, 8(%[o])\n sv.s S001, 12(%[o])\n sv.s S002, 16(%[o])\n sv.s S003, 20(%[o])\n"
        "sv.s    S020, 24(%[o])\n sv.s S021, 28(%[o])\n sv.s S022, 32(%[o])\n"
        : : [f] "r"(f.u), [g] "r"(g), [cd] "r"(data), [o] "r"(out_) : "memory");
    interp_vfpu_nodes++;
}
static void interp_checked(void *out, void *in1, void *in2, float time, void *data)
{
    if (p4_interp_fast == 2)
        interp_vfpu(out, in1, in2, time, data);
    else
        interp_fast(out, in1, in2, time, data);
    if (!p4_interp_check)
        return;
    IFrame ref = *(IFrame *)out;
    p4r_00415bc0(&ref, in1, in2, time, data);
    const float *x = ((IFrame *)out)->q, *y = ref.q;
    int differs = 0;
    for (int k = 0; k < 7; k++) {
        float d = fabsf(x[k] - y[k]) / (1 + fabsf(y[k]));
        if (!(d <= 1e-4f))
            differs = 1;
        if (d > interp_max_diff)
            interp_max_diff = d;
    }
    interp_checks++;
    interp_diffs += differs;
}
KInterp p4_interp_pick(KInterp cb)
{
    if (cb == (KInterp)p4r_00415bc0) {
        interp_calls++;
        return p4_interp_fast ? interp_checked : cb;
    }
    interp_other++;
    return cb;
}
/* b234: batch known callbacks without an indirect call/selector per node. Semantics checked against
   Izanagi bfb12b6896955b626cc45254c92e8b76789ae147 overrides/{003955a0,00415bc0}.c and our mdlManager.c.
   The copy hook writes SIX floats (translation.z stays untouched). Uncompressed lerp negates key2 IN PLACE.
   Slerp and unknown callbacks keep retail. Profiling retains a begin/end per node for comparable A/B costs. */
int p4_interp_batch = 0, p4_interp_batch_check = 0;
static unsigned batch_calls, batch_copy, batch_plain, batch_packed, batch_fallback, batch_checks, batch_diffs, batch_key_diffs;
static float batch_max_diff;
extern void mdlKeyFrameInterpolateCopy(void *, void *);
extern void p4r_003955a0(void *, void *, void *, float);

/* b235: decoded compressed-key pairs, dense per interpolator. Validate the complete 18-byte payload
   (time, quaternion, translation), not only pointers: animations may modify or reuse their keys in place.
   Custom translation scale/offset and current time are never cached. No changes to mutable plain keys. */
int p4_interp_pairs = 0, p4_interp_pairs_check = 0;
typedef struct {
    const void *a, *b;
    unsigned raw[10];
    float ta, tb, q1[4], q2[4], t1[3], t2[3];
    int linear;
} InterpPair;
typedef struct { const void *ip; InterpPair *node; unsigned used; int n; } InterpPairs;
enum { INTERP_PAIR_SETS = 64, INTERP_PAIR_BUDGET = 256 * 1024 };
static InterpPairs *interp_pairs;
static unsigned pair_bytes, pair_hits, pair_misses, pair_mutated, pair_fallback, pair_evictions, pair_refused;
static void interp_pair_free(InterpPairs *e)
{
    pair_bytes -= e->n * sizeof(InterpPair);
    free(e->node); memset(e, 0, sizeof *e);
}
static void interp_pair_forget(const void *ip)
{
    if (interp_pairs)
        for (int i = 0; i < INTERP_PAIR_SETS; i++)
            if (interp_pairs[i].ip == ip) interp_pair_free(&interp_pairs[i]);
}
static InterpPair *interp_pair_nodes(const void *ip, int n)
{
    if (n <= 0 || n > 256) return 0;
    if (!interp_pairs && !(interp_pairs = calloc(INTERP_PAIR_SETS, sizeof *interp_pairs))) return 0;
    InterpPairs *take = 0;
    for (int i = 0; i < INTERP_PAIR_SETS; i++) {
        InterpPairs *e = &interp_pairs[i];
        if (e->ip == ip) {
            if (e->n == n) { e->used = p4_anim_tick; return e->node; }
            interp_pair_free(e); take = e; break;
        }
        if (!take || (!e->ip && take->ip) || (e->ip && take->ip && e->used < take->used)) take = e;
    }
    if (take->ip) { interp_pair_free(take); pair_evictions++; }
    unsigned bytes = n * sizeof(InterpPair);
    while (pair_bytes + bytes > INTERP_PAIR_BUDGET) {
        InterpPairs *old = 0;
        for (int i = 0; i < INTERP_PAIR_SETS; i++) {
            InterpPairs *e = &interp_pairs[i];
            if (e->ip && (!old || e->used < old->used)) old = e;
        }
        if (!old) { pair_refused++; return 0; }
        interp_pair_free(old); pair_evictions++;
    }
    InterpPair *nodes = calloc(n, sizeof *nodes);
    if (!nodes) { pair_refused++; return 0; }
    take->ip = ip; take->n = n; take->node = nodes; take->used = p4_anim_tick; pair_bytes += bytes;
    return nodes;
}
static void interp_pair_decode(InterpPair *e, const CKey *a, const CKey *b, const unsigned *raw)
{
    if (e->a == a && e->b == b) pair_mutated++;
    e->a = a; e->b = b; memcpy(e->raw, raw, sizeof e->raw);
    e->ta = a->time; e->tb = b->time;
    for (int k = 0; k < 4; k++) e->q1[k] = rw_half(a->q[k]), e->q2[k] = rw_half(b->q[k]);
    for (int k = 0; k < 3; k++) e->t1[k] = rw_half(a->t[k]), e->t2[k] = rw_half(b->t[k]);
    float dot = e->q1[1] * e->q2[1] + e->q1[0] * e->q2[0] + e->q1[2] * e->q2[2] + e->q1[3] * e->q2[3];
    e->linear = fabsf(dot) >= 0.999f;
    if (dot < 0) for (int k = 0; k < 4; k++) e->q2[k] = -e->q2[k];
    pair_misses++;
}
static inline __attribute__((always_inline)) void interp_pair_node(InterpPair *e, KInterp cb, IFrame *out,
                                                                  const CKey *a, const CKey *b, float time, void *custom)
{
    const char *pa = (const char *)a, *pb = (const char *)b;
    unsigned raw[10];
#define PAIR_WORDS(p, at) \
    memcpy(&raw[at], (p) + 4, 16); \
    raw[(at) + 4] = *(const unsigned short *)((p) + 20)
    PAIR_WORDS(pa, 0); PAIR_WORDS(pb, 5);
#undef PAIR_WORDS
    unsigned d = (e->raw[0] ^ raw[0]) | (e->raw[1] ^ raw[1]) | (e->raw[2] ^ raw[2]) | (e->raw[3] ^ raw[3]) |
                 (e->raw[4] ^ raw[4]) | (e->raw[5] ^ raw[5]) | (e->raw[6] ^ raw[6]) | (e->raw[7] ^ raw[7]) |
                 (e->raw[8] ^ raw[8]) | (e->raw[9] ^ raw[9]);
    if (e->a != a || e->b != b || d) interp_pair_decode(e, a, b, raw);
    else pair_hits++;
    if (!e->linear || (void *)out == (const void *)a || (void *)out == (const void *)b) {
        pair_fallback++; cb(out, (void *)a, (void *)b, time, custom); return;
    }
    float f = (time - e->ta) / (e->tb - e->ta), g = 1.0f - f;
    const float *cd = custom;
    for (int k = 0; k < 3; k++) out->t[k] = (e->t1[k] + f * (e->t2[k] - e->t1[k])) * cd[3 + k] + cd[k];
    for (int k = 0; k < 4; k++) out->q[k] = f * e->q2[k] + g * e->q1[k];
}
static inline __attribute__((always_inline)) void interp_batch_node(int mode, KInterp cb, IFrame *out,
                                                                   void *a_, void *b_, float time, void *custom)
{
    if (mode == 1) {
        const IFrame *a = a_;
        for (int k = 0; k < 4; k++) out->q[k] = a->q[k];
        out->t[0] = a->t[0]; out->t[1] = a->t[1];
        return;
    }
    float q1[4], q2[4], t1[3], t2[3], f, dot;
    if (mode == 2) {
        const IFrame *a = a_, *b = b_;
        f = (time - *(float *)((char *)a + 4)) / (*(float *)((char *)b + 4) - *(float *)((char *)a + 4));
        for (int k = 0; k < 4; k++) q1[k] = a->q[k], q2[k] = b->q[k];
        for (int k = 0; k < 3; k++) t1[k] = a->t[k], t2[k] = b->t[k];
    } else {
        const CKey *a = a_, *b = b_;
        f = (time - a->time) / (b->time - a->time);
        for (int k = 0; k < 4; k++) q1[k] = rw_half(a->q[k]), q2[k] = rw_half(b->q[k]);
        for (int k = 0; k < 3; k++) t1[k] = rw_half(a->t[k]), t2[k] = rw_half(b->t[k]);
    }
    dot = q1[1] * q2[1] + q1[0] * q2[0] + q1[2] * q2[2] + q1[3] * q2[3];
    if (!(fabsf(dot) >= 0.999f) || out == a_ || out == b_) {
        batch_fallback++;
        cb(out, a_, b_, time, custom);
        return;
    }
    for (int k = 0; k < 3; k++) {
        float t = t1[k] + f * (t2[k] - t1[k]);
        out->t[k] = mode == 3 ? t * ((float *)custom)[3 + k] + ((float *)custom)[k] : t;
    }
    if (dot < 0) {
        for (int k = 0; k < 4; k++) {
            q2[k] = -q2[k];
            if (mode == 2) ((IFrame *)b_)->q[k] = q2[k];
        }
    }
    for (int k = 0; k < 4; k++) out->q[k] = f * q2[k] + (1.0f - f) * q1[k];
}
static void interp_batch_checked(int mode, KInterp cb, IFrame *out, void *a, void *b, float time, void *custom, InterpPair *pair)
{
    /* Preserve input keys before the fast path, including the sign mutation of uncompressed key2. */
    union { float f[12]; unsigned u[12]; char b[48]; } ca, cbkey, ref;
    int bytes = mode == 1 ? 32 : mode == 2 ? 36 : 22, count = mode == 1 ? 6 : 7;
    if (out == a || out == b) { cb(out, a, b, time, custom); return; }
    memset(&ca, 0, sizeof ca); memset(&cbkey, 0, sizeof cbkey); memset(&ref, 0, sizeof ref);
    memcpy(ca.b, a, bytes); memcpy(cbkey.b, b, bytes); memcpy(ref.b, out, mode == 1 ? 32 : 36);
    if (pair) interp_pair_node(pair, cb, out, a, b, time, custom);
    else interp_batch_node(mode, cb, out, a, b, time, custom);
    cb(ref.b, ca.b, cbkey.b, time, custom);
    int bad = 0;
    for (int k = 0; k < count; k++) {
        float x = ((float *)out)[k + 2], y = ref.f[k + 2];
        unsigned u; memcpy(&u, &x, 4);
        if (u == ref.u[k + 2]) continue; /* identical NaNs/infinities too; the existing sanitizer follows */
        float d = fabsf(x - y) / (1 + fabsf(y));
        if (!(d <= 1e-5f)) bad = 1;
        if (d > batch_max_diff) batch_max_diff = d;
    }
    int key_bad = mode == 2 && memcmp(b, cbkey.b, bytes);
    batch_checks++; batch_diffs += bad; batch_key_diffs += key_bad != 0;
    if ((bad || key_bad) && batch_diffs + batch_key_diffs <= 8)
        p4_log("TRACE interp batch mismatch mode %d out %d key %d\n", mode, bad, key_bad != 0);
}
/* AddAnimTime (RtAnimInterpolatorAddAnimTime). The interpolate callback is swapped for the call (a pointer at +0x44);
   p4_anim_far_kadd: the interpolator of a hierarchy held by p4_anim_far (or culled by p4_anim_cull) keeps its time
   step for later and is advanced by the sum when it is due. */
int p4_anim_far_kadd = 1;
/* b201 p4_anim_skip_kadd: in a tick frameskip leaves undrawn the bones are not updated (p4_frameskip_bones), so the
   interpolated pose of a drawn skinned character is not used either: its time step is kept and added with the drawn
   tick's (at most a second kept back), as p4_anim_far_kadd does for far characters. Layer time and the animation's
   callbacks then move on the drawn tick, at most 2 ticks late. */
int p4_anim_skip_kadd = 1;
static unsigned kadd_calls, kadd_mapped, kadd_held, kadd_skip_held;
int p4r_003d5bc0(void *interpolator, float time);
static AnimState *anim_by_interp(const void *interp)
{
    if (!interp)
        return 0;
    for (int i = 0; i < ANIM_STATES; i++)
        if (anim_interp_of[i] == interp && anim_state[i].h && anim_state[i].interp == interp)
            return &anim_state[i];
    return 0;
}
/* b202 p4_anim_lazy: in a tick frameskip leaves undrawn a layer's own interpolators (its hierarchy's currentAnim and
   its rebase input, inside mdlAdvanceLayer) only move their clock. RtAnim's AddAnimTime adds the step, walks the key
   stream up to the new time and interpolates every node; walk and interpolation depend on the final time alone, so
   both wait for the first reader. A step that crosses the callback time or loops takes the retail call. The layer
   blend and the rebase of such a tick read the frames, so they are recorded and replayed after the interpolation.
   Readers that materialise: the bone update (hanim_update_now), look-at, a blend or rebase outside a deferral and the
   RtAnim calls SubAnimTime, SetCurrentAnim, Blend, Destroy. A retail AddAnimTime catches the walk up by itself.
   0 off, 1 undrawn ticks, 2 every tick (PPSSPP test: same picture). p4_anim_lazy_check: a copy of each deferred
   interpolator runs the retail calls and is compared when materialised (with p4_interp_fast 0). */
int p4_anim_lazy = 1, p4_anim_lazy_check = 0;
int p4_anim_native = 1; /* b204: plain AddAnimTime steps in C (func_003d5bc0) */
static unsigned native_adds, retail_adds;
enum { LAZY_SLOTS = 128 }; /* b203: material UV interpolators too */
typedef struct { char *ip, *first, *layer, *input, *rotation, *clone; unsigned stamp; float blend; } LazyAnim;
static LazyAnim lazy_slot[LAZY_SLOTS];
static const void *lazy_ip[LAZY_SLOTS]; /* b228: lazy_slot[i].ip, scanned instead of the 32-byte slots */
static int lazy_used, lazy_paused;
static char *lazy_cur, *lazy_input, *lazy_replay_input;
static unsigned lazy_deferred, lazy_refused, lazy_blends, lazy_rebases, lazy_materialized, lazy_obsolete, lazy_full, lazy_checks, lazy_diffs;
int func_003d5bc0(void *interpolator, float time);
unsigned p4_mdlBlendAnimInterpolators(void *destination, void *first, void *second, float blend);
void p4_mdlRebaseLayerPose(void *layer);
static LazyAnim *lazy_find(const void *ip)
{
    if (!lazy_used || !ip)
        return 0;
    for (int i = 0; i < LAZY_SLOTS; i++)
        if (lazy_ip[i] == ip)
            return &lazy_slot[i];
    return 0;
}
static void lazy_drop(LazyAnim *e)
{
    free(e->clone);
    memset(e, 0, sizeof *e);
    lazy_ip[e - lazy_slot] = 0;
    lazy_used--;
}
/* b204: the node of every key, once per animation. SetCurrentAnim (3d5840) gives node i keys i and i + n, so key k < 2n
   belongs to node k mod n, and a later key to the node of its previous key. The walk checks each answer (the node's
   second key must be the previous key) and searches as retail does when it is wrong, so a stale map costs time only. */
typedef struct { const char *anim, *frames; int count, size, nodes; unsigned short *node; } KeyMap;
enum { KEY_MAPS = 128 };
static KeyMap key_map[KEY_MAPS];
static unsigned key_maps_built, key_map_misses, key_walked;
int p4_anim_keymap = 1;
static const unsigned short *key_nodes(const char *ip)
{
    const char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10);
    int count = *(int *)(anim + 4), size = *(int *)(ip + 0x28), n = *(int *)(ip + 0x2c);
    KeyMap *m = &key_map[((unsigned)anim >> 4) * 2654435761u >> 25];
    if (m->anim == anim && m->frames == frames && m->count == count && m->size == size && m->nodes == n)
        return m->node;
    free(m->node);
    memset(m, 0, sizeof *m);
    if (!p4_anim_keymap || count <= 0 || n <= 0 || n > 0xffff || size <= 0)
        return 0;
    unsigned short *node = malloc(count * sizeof *node);
    if (!node)
        return 0;
    for (int k = 0; k < count; k++) {
        if (k < 2 * n) {
            node[k] = k % n;
            continue;
        }
        const char *prev = *(char **)(frames + k * size);
        int at = prev - frames;
        if (at < 0 || at % size || at / size >= k) { /* not this layout: no map */
            free(node);
            return 0;
        }
        node[k] = node[at / size];
    }
    m->anim = anim, m->frames = frames, m->count = count, m->size = size, m->nodes = n, m->node = node;
    key_maps_built++;
    return node;
}
/* retail AddAnimTime's key walk (func_003d5bc0 at 3d5cac): every key whose previous key is due goes to the node
   whose second key that is, searched from where the last one was found */
static void lazy_walk(char *ip)
{
    char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10), *end = frames + *(int *)(anim + 4) * *(int *)(ip + 0x28);
    int nodes = *(int *)(ip + 0x2c), a = 0;
    const unsigned short *map = 0;
    while (*(char **)(ip + 8) < end) {
        char *next = *(char **)(ip + 8), *prev = *(char **)next, *f;
        if (!(*(float *)(prev + 4) <= *(float *)(ip + 4)))
            break;
        if (!map && p4_anim_keymap)
            map = key_nodes(ip);
        key_walked++;
        if (map && (f = ip + 0x4c + map[(next - frames) / *(int *)(ip + 0x28)] * *(int *)(ip + 0x24), *(char **)(f + 4) == prev)) {
            a = map[(next - frames) / *(int *)(ip + 0x28)] + 1; /* where retail's search would go on */
            if (a >= nodes)
                a = 0;
        } else {
            if (map)
                key_map_misses++;
            do {
                f = ip + 0x4c + a * *(int *)(ip + 0x24);
                if (++a >= nodes)
                    a = 0;
            } while (*(char **)(f + 4) != prev);
        }
        *(char **)f = prev;
        *(char **)(f + 4) = next;
        *(char **)(ip + 8) = next + *(int *)(ip + 0x28);
    }
}
/* and its interpolation (3d5dd0): every node from its two keys at the current time */
static void lazy_interpolate(char *ip)
{
    char *f = ip + 0x4c;
    KInterp cb = *(KInterp *)(ip + 0x44), actual = cb;
    int timed = 0;
#ifdef P4_HW_PROFILE
    if (cb == kinterp_timed) actual = kinterp_now, timed = 1;
#endif
    int mode = 0;
    if (p4_interp_batch || p4_interp_pairs) {
        if (p4_interp_pairs && actual == (KInterp)p4r_00415bc0 && !p4_interp_fast) mode = 3;
        else if (p4_interp_batch && actual == (KInterp)mdlKeyFrameInterpolateCopy) mode = 1;
        else if (p4_interp_batch && actual == (KInterp)p4r_003955a0) mode = 2;
        else if (p4_interp_batch && actual == (KInterp)p4r_00415bc0 && !p4_interp_fast) mode = 3;
    }
    InterpPair *pairs = mode == 3 && p4_interp_pairs && *(int *)(ip + 0x28) >= 22 ?
                        interp_pair_nodes(ip, *(int *)(ip + 0x2c)) : 0;
    if (p4_interp_pairs && !p4_interp_batch && !pairs) mode = 0;
    if (!mode || *(int *)(ip + 0x24) < (mode == 1 ? 32 : 36)) {
        for (int i = 0; i < *(int *)(ip + 0x2c); i++, f += *(int *)(ip + 0x24)) {
            cb = *(KInterp *)(ip + 0x44);
            p4_interp_pick(cb)(f, *(void **)f, *(void **)(f + 4), *(float *)(ip + 4), *(void **)(*(char **)ip + 0x14));
        }
        return;
    }
    int n = *(int *)(ip + 0x2c), stride = *(int *)(ip + 0x24);
    float time = *(float *)(ip + 4);
    void *custom = *(void **)(*(char **)ip + 0x14);
    batch_calls++;
    if (mode == 1) batch_copy += n;
    else if (mode == 2) batch_plain += n;
    else batch_packed += n;
    /* Separate loops let GCC inline and fold the callback format out of each node. */
#define INTERP_BATCH_LOOP(kind) \
    for (int i = 0; i < n; i++, f += stride) { \
        void *a = *(void **)f, *b = *(void **)(f + 4); \
        if (timed) prof_begin(PROF_KINTERP); \
        InterpPair *pair = (kind) == 3 && pairs ? pairs + i : 0; \
        if (p4_interp_batch_check || (pair && p4_interp_pairs_check)) interp_batch_checked(kind, actual, (IFrame *)f, a, b, time, custom, pair); \
        else if (pair) interp_pair_node(pair, actual, (IFrame *)f, a, b, time, custom); \
        else interp_batch_node(kind, actual, (IFrame *)f, a, b, time, custom); \
        if (timed) prof_end(PROF_KINTERP); \
    }
    if (mode == 1) { INTERP_BATCH_LOOP(1) }
    else if (mode == 2) { INTERP_BATCH_LOOP(2) }
    else { INTERP_BATCH_LOOP(3) }
#undef INTERP_BATCH_LOOP
}
static int lazy_bytes(const char *ip) { return 0x4c + *(int *)(ip + 0x2c) * *(int *)(ip + 0x24); }
static int lazy_compare(const char *ip, const char *clone);
void p4_anim_materialize(void *interp)
{
    LazyAnim *e = lazy_find(interp), copy;
    if (!e)
        return;
    copy = *e;
    e->clone = 0;
    lazy_drop(e); /* first: the replays below may reach this interpolator again */
    lazy_materialized++;
    lazy_walk(copy.ip);
    lazy_interpolate(copy.ip);
    if (copy.clone) { /* the header's callbacks may hold the profiler's trampoline: lazy_compare skips them */
        lazy_compare(copy.ip, copy.clone);
        free(copy.clone);
    }
    if (copy.first) {
        p4_anim_materialize(copy.first);
        p4_mdlBlendAnimInterpolators(copy.ip, copy.first, copy.ip, copy.blend);
    }
    if (copy.layer) {
        p4_anim_materialize(copy.input);
        p4_anim_materialize(copy.rotation);
        lazy_replay_input = copy.input; /* the rebase's own AddAnimTime of its input happened when it was recorded */
        p4_mdlRebaseLayerPose(copy.layer);
        lazy_replay_input = 0;
    }
}
/* the frames are rebuilt from the keys by the caller: what was waiting is obsolete */
static void lazy_obsolete_now(const void *ip)
{
    LazyAnim *e = lazy_find(ip);
    if (e)
        lazy_obsolete++, lazy_drop(e);
}
/* census FIXES around mdlAdvanceLayer in mdlAdvanceModel: this layer's interpolators, or 0 after it */
void p4_anim_lazy_scope(void *cur, void *input)
{
    int on = cur && (p4_anim_lazy == 2 || (p4_anim_lazy == 1 && skip_frame && p4_frameskip_bones));
    lazy_cur = on ? cur : 0;
    lazy_input = on ? input : 0;
}
/* b203: around mdlAdvanceAnimControl (census FIXES) every interpolator it steps is the material UV animation's own;
   in an undrawn tick their apply (func_003d7cf0, the material's UV matrices) is left out too: nothing draws them, and
   the drawn tick steps and applies again. 2 = the test mode: the apply runs and materialises. */
static int lazy_all;
void p4_anim_lazy_materials(int begin)
{
    lazy_all = !begin ? 0 : p4_anim_lazy == 2 ? 2 : p4_anim_lazy == 1 && skip_frame && p4_frameskip_bones;
}
static int lazy_owned(const void *ip) { return !lazy_paused && ip && (lazy_all || ip == lazy_cur || ip == lazy_input); }
static unsigned lazy_uv_skipped;
void *func_003d8130(void *material, int slot); /* the material's UV interpolator, slot 0 or 1 */
int p4r_003d7cf0(void *material);
int func_003d7cf0(void *material) /* RpMaterialUVAnimApplyUpdate: UV matrices from both slots' frames */
{
    if (lazy_all == 1) {
        lazy_uv_skipped++;
        return 1;
    }
    p4_anim_materialize(func_003d8130(material, 0));
    p4_anim_materialize(func_003d8130(material, 1));
    return p4r_003d7cf0(material);
}
/* an AddAnimTime step where retail fires no callback (3d5bc0 start), does not loop (3d5c30) and has a next key:
   then it is the clock, the walk and the interpolation only; *t is the new time */
static int add_plain(char *ip, float time, float *t)
{
    char *anim = *(char **)ip;
    if (!(time > 0.0f) || !anim || !*(char **)(ip + 8))
        return 0;
    float now = *(float *)(ip + 4), at = *(float *)(ip + 0x14);
    *t = now + time;
    return *t <= *(float *)(anim + 0xc) && !(now < at && !(*t < at));
}
static int lazy_compare(const char *ip, const char *clone) /* time, next key and the frames */
{
    int at = -1, n = lazy_bytes(ip);
    for (int k = 4; k < n && at < 0; k += 4)
        if ((k < 0xc || k >= 0x4c) && *(unsigned *)(ip + k) != *(unsigned *)(clone + k))
            at = k;
    lazy_checks++;
    if (at >= 0 && lazy_diffs++ < 8)
        p4_log("TRACE anim check: interpolator %p offset %x: %08x, retail copy %08x (time %d/1000, nodes %d, frame %d)\n", (void *)ip, at,
               *(unsigned *)(ip + at), *(unsigned *)(clone + at), (int)(*(float *)(ip + 4) * 1000), *(int *)(ip + 0x2c), *(int *)(ip + 0x24));
    return at < 0;
}
static int lazy_defer(char *ip, float time)
{
    float t;
    if (!add_plain(ip, time, &t))
        return 0;
    LazyAnim *e = lazy_find(ip);
    if (!e) {
        for (int i = 0; i < LAZY_SLOTS && !e; i++)
            if (!lazy_ip[i])
                e = &lazy_slot[i];
        if (!e) {
            lazy_full++;
            return 0;
        }
        lazy_used++;
        e->ip = ip;
        lazy_ip[e - lazy_slot] = ip;
        if (p4_anim_lazy_check && (e->clone = malloc(lazy_bytes(ip))))
            memcpy(e->clone, ip, lazy_bytes(ip));
    }
    if (e->clone)
        p4r_003d5bc0(e->clone, time);
    e->first = e->layer = 0; /* this tick interpolates afresh: an older blend or rebase is overwritten */
    e->stamp = p4_anim_tick;
    *(float *)(ip + 4) = t;
    lazy_deferred++;
    return 1;
}
static LazyAnim *lazy_fresh(const void *ip)
{
    LazyAnim *e = lazy_find(ip);
    return e && e->stamp == p4_anim_tick ? e : 0;
}
/* mdlBlendAnimInterpolators (census FIXES): 1 = recorded, the caller returns */
int p4_anim_lazy_blend(void *destination, void *first, void *second, float blend)
{
    LazyAnim *e;
    if (lazy_owned(destination) && destination == second && (e = lazy_fresh(destination)) && !e->first && !e->layer) {
        e->first = first, e->blend = blend;
        lazy_blends++;
        return 1;
    }
    p4_anim_materialize(first);
    p4_anim_materialize(second);
    p4_anim_materialize(destination);
    return 0;
}
/* mdlRebaseLayerPose (census FIXES); active: it has an input and flag 0x80. 1 = recorded, the caller returns */
int p4_anim_lazy_rebase(void *layer, void *cur, void *input, void *rotation, int active, float step)
{
    LazyAnim *e;
    if (lazy_replay_input || !active)
        return 0;
    if (lazy_owned(cur) && input == lazy_input && (e = lazy_fresh(cur)) && !e->layer) {
        func_003d5bc0(input, step);
        e->layer = layer, e->input = input, e->rotation = rotation;
        lazy_rebases++;
        return 1;
    }
    p4_anim_materialize(cur);
    p4_anim_materialize(input);
    p4_anim_materialize(rotation);
    return 0;
}
/* mdlUpdateHierarchyPose (census FIXES) reads the frames and runs RtAnim calls of its own */
void p4_anim_lazy_pause(void *cur, int pause)
{
    if (pause)
        p4_anim_materialize(cur);
    lazy_paused += pause ? 1 : -1;
}
int p4r_003d59d0(void *interpolator, float time);
int func_003d59d0(void *interpolator, float time) /* SubAnimTime: walks back from the keys, then interpolates all */
{
    LazyAnim *e = lazy_find(interpolator);
    if (e) {
        lazy_walk(interpolator);
        lazy_obsolete++, lazy_drop(e);
    }
    return p4r_003d59d0(interpolator, time);
}
int p4r_003d5840(void *interpolator, void *animation);
int func_003d5840(void *interpolator, void *animation) /* SetCurrentAnim */
{
    lazy_obsolete_now(interpolator);
    return p4r_003d5840(interpolator, animation);
}
int p4r_003d5e90(void *out, void *in1, void *in2, float alpha);
int func_003d5e90(void *out, void *in1, void *in2, float alpha) /* Blend: every node of out from in1 and in2 */
{
    p4_anim_materialize(in1);
    p4_anim_materialize(in2);
    lazy_obsolete_now(out);
    return p4r_003d5e90(out, in1, in2, alpha);
}
void p4r_003d5830(void *interpolator);
void func_003d5830(void *interpolator) /* Destroy */
{
    interp_pair_forget(interpolator);
    lazy_obsolete_now(interpolator);
    for (int i = 0; i < LAZY_SLOTS; i++) { /* a recorded blend or rebase that reads it cannot be replayed */
        LazyAnim *e = &lazy_slot[i];
        if (e->ip && e->first == interpolator)
            e->first = 0;
        if (e->ip && (e->input == interpolator || e->rotation == interpolator))
            e->layer = 0;
    }
    p4r_003d5830(interpolator);
}
int func_003d5bc0(void *interpolator, float time)
{
    kadd_calls++;
    if (interpolator == lazy_replay_input)
        return 1;
    int owned = lazy_owned(interpolator);
    if (owned) {
        if (lazy_defer(interpolator, time))
            return 1;
        lazy_refused++;
    }
    if ((p4_anim_far_kadd && (p4_anim_far || p4_anim_cull)) || (p4_anim_skip_kadd && skip_frame && p4_frameskip_bones && !owned)) {
        AnimState *s = anim_by_interp(interpolator);
        if (s && s->skinned) {
            kadd_mapped++;
            int unseen = p4_anim_tick - s->drawn > 2;
            int hold = p4_anim_far_kadd && ((p4_anim_cull && unseen) ||
                       (p4_anim_far && !unseen && s->depth > p4_anim_far_depth && p4_anim_tick - s->advanced < (unsigned)p4_anim_far_step &&
                        p4_anim_tick - s->moved >= 8));
            int skip = !hold && p4_anim_skip_kadd && skip_frame && p4_frameskip_bones && !owned;
            if ((hold || skip) && s->pending < 1.0f) { /* at most a second kept back */
                s->pending += time;
                kadd_held += hold;
                kadd_skip_held += skip;
                return 1;
            }
            time += s->pending;
            s->pending = 0;
            s->advanced = p4_anim_tick;
        }
    }
    /* b204 p4_anim_native: a plain step (no callback, no loop) in C: clock, keyed walk, interpolation. Exactly
       retail's work in retail's order; p4_anim_lazy_check runs retail on a copy and compares. */
    float t;
    if (p4_anim_native && add_plain(interpolator, time, &t)) {
        char *ip = interpolator, *clone = 0;
        if (p4_anim_lazy_check && (clone = malloc(lazy_bytes(ip))))
            memcpy(clone, ip, lazy_bytes(ip));
        lazy_obsolete_now(ip); /* the walk catches up and every node is interpolated */
        *(float *)(ip + 4) = t;
        lazy_walk(ip);
        lazy_interpolate(ip);
        native_adds++;
        if (clone) {
            p4r_003d5bc0(clone, time);
            lazy_compare(ip, clone);
            free(clone);
        }
        return 1;
    }
    KInterp *cb = (KInterp *)((char *)interpolator + 0x44), saved = *cb;
    KInterp pick = saved ? p4_interp_pick(saved) : saved;
    if (pick != saved)
        *cb = pick;
    retail_adds++;
    int r = p4r_003d5bc0(interpolator, time);
    if (pick != saved)
        *cb = saved;
    if (time > 0.0f) /* walked and interpolated: nothing waits any more */
        lazy_obsolete_now(interpolator);
    return r;
}
void p4_interp_report(void)
{
    p4_log("interp fast %d calls %u other %u slerps %u checks %u diffs %u max_diff %d/1e6; kadd far %d calls %u mapped %u held %u skip %d held %u\n",
           p4_interp_fast, interp_calls, interp_other, interp_slerps, interp_checks, interp_diffs, (int)(interp_max_diff * 1e6f),
           p4_anim_far_kadd, kadd_calls, kadd_mapped, kadd_held, p4_anim_skip_kadd, kadd_skip_held);
    p4_log("anim lazy %d: deferred %u refused %u blends %u rebases %u materialized %u obsolete %u full %u waiting %d check %u diffs %u uv skipped %u\n",
           p4_anim_lazy, lazy_deferred, lazy_refused, lazy_blends, lazy_rebases, lazy_materialized, lazy_obsolete, lazy_full, lazy_used,
           lazy_checks, lazy_diffs, lazy_uv_skipped);
    p4_log("anim native %d: adds %u retail %u; keymap %d: keys walked %u maps built %u misses %u\n", p4_anim_native, native_adds, retail_adds,
           p4_anim_keymap, key_walked, key_maps_built, key_map_misses);
    p4_log("interp batch %d calls %u copy %u plain %u packed %u fallback %u check %d checked %u diffs %u key_diffs %u max %d/1e6\n",
           p4_interp_batch, batch_calls, batch_copy, batch_plain, batch_packed, batch_fallback, p4_interp_batch_check,
           batch_checks, batch_diffs, batch_key_diffs, (int)(batch_max_diff * 1e6f));
    batch_calls = batch_copy = batch_plain = batch_packed = batch_fallback = batch_checks = batch_diffs = batch_key_diffs = 0;
    batch_max_diff = 0;
    p4_log("interp pairs %d check %d: hits %u misses %u mutated %u fallback %u bytes %u evictions %u refused %u\n",
           p4_interp_pairs, p4_interp_pairs_check, pair_hits, pair_misses, pair_mutated, pair_fallback, pair_bytes, pair_evictions, pair_refused);
    pair_hits = pair_misses = pair_mutated = pair_fallback = pair_evictions = pair_refused = 0;
    native_adds = retail_adds = key_walked = key_maps_built = key_map_misses = 0;
    lazy_deferred = lazy_refused = lazy_blends = lazy_rebases = lazy_materialized = lazy_obsolete = lazy_full = lazy_checks = lazy_diffs = 0;
    lazy_uv_skipped = 0;
    interp_calls = interp_other = interp_slerps = interp_checks = interp_diffs = kadd_calls = kadd_mapped = kadd_held = kadd_skip_held = 0;
    interp_max_diff = 0;
}
int func_00397c40(void *hierarchy)
{
    if (skip_frame && p4_frameskip_bones) {
        skipped_bone_updates++;
        return 1;
    }
    AnimState *s = anim_find(hierarchy);
    if (s && s->skinned) {
        int unseen = p4_anim_tick - s->drawn > 2;
        anim_unseen += unseen;
        if (!unseen && s->depth > p4_anim_far_depth)
            anim_far_due++;
        if (p4_anim_cull && unseen) {
            s->stale = 1;
            anim_culled++;
            return 1;
        }
        if (!unseen && anim_far_hold(s)) {
            s->stale = 1;
            anim_far_skipped++;
            return 1;
        }
    } else
        anim_unskinned++;
    anim_updates++;
    if (s)
        s->stale = 0, s->updated = p4_anim_tick;
    return hanim_update_now(hierarchy);
}
/* b240 p4_hanim_time (profiling build): cycles of the hierarchy update by part, read from the cycle counter around
   each part (4 reads a node, ~15 cycles each): waiting frames interpolated, the NaN scan, the prologue (parent LTM,
   dirty list), per node the matrix (callback or conversion, multiply) and the RwFrame writes; and the ordinary runs
   of the look-at pose (p4_pose_runs 1) so that apose minus them is the head and the tail. */
int p4_hanim_time = 1;
static unsigned long long hanim_t_wait, hanim_t_scan, hanim_t_pre, hanim_t_math, hanim_t_frame, pose_t_runs;
static unsigned hanim_t_nodes, hanim_t_framed, hanim_t_callback, hanim_t_calls;
#ifdef P4_HW_PROFILE
#define HANIM_NOW() (p4_hanim_time && prof_state > 0 ? prof_cycles() : 0)
#else
#define HANIM_NOW() 0u
#endif
static int hanim_update_now(void *hierarchy)
{
    unsigned t_w = HANIM_NOW();
    p4_anim_materialize((void *)((unsigned *)hierarchy)[8]); /* b202: currentAnim's frames, if they wait */
    unsigned t_s = HANIM_NOW();
    if (p4_hanim_nan_fix)
        hanim_sanitize(hierarchy);
    hanim_t_wait += t_s - t_w, hanim_t_scan += HANIM_NOW() - t_s;
    int r = p4_hanim_c ? p4_hanim_update(hierarchy) : p4r_00397c40(hierarchy);
    hanim_note(hierarchy);
    if (p4_head_trace)
        head_trace(hierarchy);
    if (p4_bone_check) { /* b157: are the matrices already bad right after the update, or made bad later? */
        unsigned *h = hierarchy;
        const float *m = (const float *)h[2];
        int n = (int)h[1];
        for (int i = 0; m && i < n && i < 256; i++, m += 16)
            if (hanim_bad(m, 3) || hanim_bad(m + 4, 3) || hanim_bad(m + 8, 3) || hanim_bad(m + 12, 3)) {
                hanim_nan_after_update++;
                if (hanim_nan_logged < 12) {
                    hanim_nan_logged++;
                    const char *frame = (const char *)h[8] + 76 + i * *(int *)((char *)h[8] + 36);
                    const float *qt = (const float *)(frame + 8);
                    p4_log("TRACE nan after update hierarchy %p node %d/%d: frame q %d %d %d %d t %d %d %d (x1000)\n", (void *)h, i, n,
                           (int)(qt[0] * 1000), (int)(qt[1] * 1000), (int)(qt[2] * 1000), (int)(qt[3] * 1000), (int)qt[4], (int)qt[5], (int)qt[6]);
                }
            }
    }
    return r;
}
static unsigned hanim_checks, hanim_diffs, hanim_calls;
static float hanim_max_diff;
extern char func_003954b0[];
int p4r_00397480(void *hierarchy);
int func_003e9240(void *frame);
void *func_003e9680(void *frame);
void *RwMatrixMultiply(void *out, const void *a, const void *b);

static void hanim_identity(Matrix *m)
{
    memset(m, 0, sizeof *m);
    m->right.x = m->up.y = m->at.z = 1;
    m->flags = 0x20003;
}

/* the inline RpHAnimKeyFrameToMatrix, same operations in the same order */
static void hanim_key_to_matrix(Matrix *m, const char *key)
{
    const float *q = (const float *)(key + 8), *t = (const float *)(key + 24);
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float xx = x * x, zz = z * z, yz = y * z, zx = z * x, xy = x * y, wx = w * x, yy = y * y, wy = w * y, wz = w * z;
    float one = 0.0f + 1.0f;
    m->right.x = one - 2.0f * (yy + zz);
    m->right.y = 2.0f * (xy + wz);
    m->right.z = 2.0f * (zx - wy);
    m->flags = 3;
    m->up.x = 2.0f * (xy - wz);
    m->up.y = one - 2.0f * (xx + zz);
    m->up.z = 2.0f * (yz + wx);
    m->at.x = 2.0f * (zx + wy);
    m->at.y = 2.0f * (yz - wx);
    m->at.z = one - 2.0f * (xx + yy);
    m->pos.x = t[0], m->pos.y = t[1], m->pos.z = t[2];
}

static int multiply_vfpu(Matrix *out, const Matrix *a, const Matrix *b);
static void hanim_mul(void *out, const void *a, const void *b)
{
    if (p4_hanim_vfpu && multiply_vfpu(out, a, b)) {
        ((Matrix *)out)->flags = ((const Matrix *)a)->flags & ((const Matrix *)b)->flags;
        return;
    }
    hanim_vfpu_unaligned += p4_hanim_vfpu;
    RwMatrixMultiply(out, a, b);
}

/* b239 p4_hanim_vfpu 2: one bone on the VFPU: the unit quaternion to the local matrix (the values of
   RpHAnimKeyFrameToMatrix: 1 - (2yy + 2zz) and so on) built in M600, where multiply_vfpu wants the rows of a, then
   through the parent as multiply_vfpu does. No FPU work and no call per bone. local and out get the rows; flags as
   the C path sets them. out, local and parent must be 16-byte aligned. */
static unsigned hanim_fused_nodes, hanim_hook_nodes;
static inline void hanim_node_vfpu(Matrix *out, Matrix *local, const char *key, const Matrix *parent)
{
    __asm__ volatile(
        "lv.s    S000, 8(%[k])\n lv.s S001, 12(%[k])\n lv.s S002, 16(%[k])\n lv.s S003, 20(%[k])\n"
        "lv.s    S630, 24(%[k])\n lv.s S631, 28(%[k])\n lv.s S632, 32(%[k])\n vone.s S633\n"
        "lv.q    C400, 0(%[b])\n lv.q C410, 16(%[b])\n lv.q C420, 32(%[b])\n lv.q C430, 48(%[b])\n"
        "vadd.q  C010, C000, C000\n"       /* 2x 2y 2z 2w */
        "vscl.t  C020, C010, S000\n"       /* 2xx 2xy 2xz */
        "vscl.t  C030, C010, S001\n"       /* 2xy 2yy 2yz */
        "vmul.s  S100, S002, S012\n"       /* 2zz */
        "vscl.t  C110, C010, S003\n"       /* 2wx 2wy 2wz */
        "vadd.s  S600, S031, S100\n vocp.s S600, S600\n"
        "vadd.s  S601, S021, S112\n"
        "vsub.s  S602, S022, S111\n"
        "vsub.s  S610, S021, S112\n"
        "vadd.s  S611, S020, S100\n vocp.s S611, S611\n"
        "vadd.s  S612, S032, S110\n"
        "vadd.s  S620, S022, S111\n"
        "vsub.s  S621, S032, S110\n"
        "vadd.s  S622, S020, S031\n vocp.s S622, S622\n"
        "vzero.s S603\n vzero.s S613\n vzero.s S623\n"
        "vtfm4.q C700, E400, C600\n"
        "vtfm4.q C710, E400, C610\n"
        "vtfm4.q C720, E400, C620\n"
        "vtfm4.q C730, E400, C630\n"
        "sv.q    C600, 0(%[l])\n sv.q C610, 16(%[l])\n sv.q C620, 32(%[l])\n sv.q C630, 48(%[l])\n"
        "sv.q    C700, 0(%[o])\n sv.q C710, 16(%[o])\n sv.q C720, 32(%[o])\n sv.q C730, 48(%[o])\n"
        : : [o] "r"(out), [l] "r"(local), [k] "r"(key), [b] "r"(parent) : "memory");
    local->flags = 3;
    out->flags = 3 & parent->flags;
}
/* The per-node hook of the game (mdlHierarchyScaleHook, installed for layers with flag 0x10) scales the key
   translation of one node, calls the saved callback and scales the matrix of one node: for every other node it is
   the saved callback. 1 when that is the stock conversion; *ka and *kb are the two keys the hook treats specially. */
void mdlHierarchyScaleHook(unsigned char *matrix, int key);
/* b241: the apply callback the characters really carry is the compressed-key scheme's p4r_00415ad0 (read from a live
   interpolator on the PSP, b240: +0x3c = 0x0889513c), not RpHAnim's func_003954b0. Its code is the same conversion
   (1 - 2(yy + zz) ..., pos = t, flags 3), so the fused node may stand in for either. */
void p4r_00415ad0(void *matrix, void *key);
static int hanim_convertible(const void *cb) { return cb == (const void *)func_003954b0 || cb == (const void *)p4r_00415ad0; }
static int hanim_hook_stock(const void *cb, const char **ka, const char **kb)
{
    if (cb != (const void *)mdlHierarchyScaleHook || !hanim_convertible((const void *)R32(0x922ba0)))
        return 0;
    *ka = (const char *)R32(0x922ba4), *kb = (const char *)R32(0x922ba8);
    return 1;
}
static inline void copy64(void *d, const void *s) /* 4-byte aligned words, no call */
{
    unsigned *x = d;
    const unsigned *y = s;
    for (int k = 0; k < 16; k++)
        x[k] = y[k];
}

/* b237/b238: ordinary runs of the special look-at hierarchy. Head/controller code
   stays in mdlUpdateHierarchyPose. Custom apply callbacks run exactly once, in the
   original order, since the scale hook can mutate keyframes and global state. */
int p4_pose_runs = 0;
int p4_pose_runs_check = 0;
static unsigned pose_runs, pose_nodes, pose_custom_nodes, pose_checks, pose_diffs, pose_flags_diffs, pose_exact_diffs;
static float pose_max_diff;
static int pose_compare(const Matrix *a, const Matrix *b)
{
    int bad = a->flags != b->flags;
    pose_flags_diffs += bad;
    const float *x = (const float *)a, *y = (const float *)b;
    for (int k = 0; k < 16; k++) {
        if ((k & 3) == 3) continue; /* unused matrix padding is not initialized */
        float d = fabsf(x[k] - y[k]);
        float scale = fabsf(x[k]) > fabsf(y[k]) ? fabsf(x[k]) : fabsf(y[k]);
        pose_exact_diffs += !(x[k] == y[k]);
        if (!(d <= 0.00001f * (1.0f + scale))) bad = 1;
        if (d > pose_max_diff) pose_max_diff = d;
    }
    pose_checks++;
    pose_diffs += bad;
    return bad;
}
/* Keep diagnostics out of the hot loop's stack/register allocation. */
static __attribute__((noinline)) void pose_check_bone(Matrix *local, Matrix *out, const Matrix *parent, const char *key, int stock)
{
    Matrix reference __attribute__((aligned(16))), expected __attribute__((aligned(16)));
    if (stock) ((void (*)(void *, const void *))func_003954b0)(&reference, key);
    else reference = *local; /* preserve the custom callback's single invocation */
    RwMatrixMultiply(&expected, &reference, parent);
    int bad = stock ? pose_compare(local, &reference) : 0;
    bad |= pose_compare(out, &expected);
    if (bad) { *local = reference; *out = expected; }
}
static __attribute__((noinline)) void pose_check_ltm(Matrix *out, const Matrix *m, const Matrix *base)
{
    Matrix expected __attribute__((aligned(16)));
    RwMatrixMultiply(&expected, m, base);
    if (pose_compare(out, &expected)) memcpy(out, &expected, sizeof expected);
}
int p4_pose_run(unsigned *h, int first, const char *key, Matrix *m, unsigned *info,
                const Matrix **parent_io, const Matrix ***stack_io, const Matrix *base, void (*apply)(void *, const void *))
{
    const Matrix *parent = *parent_io;
    const Matrix **stack = *stack_io;
    const int stride = *(int *)((char *)h[8] + 36), flags = h[0];
    unsigned t_run = HANIM_NOW();
    int count = 0, stock = (const void *)apply == (const void *)func_003954b0;
    const char *hook_a = 0, *hook_b = 0;
    int fused = p4_hanim_vfpu >= 2 && !((unsigned)m & 15) && (hanim_convertible(apply) || hanim_hook_stock(apply, &hook_a, &hook_b));
    for (int i = first; i < (int)h[1] && info[0] != 0x1389; i++, count++, m++, key += stride, info += 4) {
        Matrix local __attribute__((aligned(16)));
        if (fused && key != hook_a && key != hook_b && !((unsigned)parent & 15)) {
            hanim_node_vfpu(m, &local, key, parent);
            hanim_fused_nodes++, hanim_hook_nodes += !stock;
        } else {
            if (stock) hanim_key_to_matrix(&local, key);
            else { apply(&local, key); pose_custom_nodes++; }
            hanim_mul(m, &local, parent);
        }
        /* The original head/controller slerp still advances only once. */
        if (p4_pose_runs_check) pose_check_bone(&local, m, parent, key, stock);
        unsigned char *frame = (unsigned char *)info[3];
        if (frame) {
            if (flags & 0x1000) {
                memcpy(frame + 16, &local, sizeof local);
                if (!(flags & 0x2000)) func_003e9680(frame);
            }
            if (flags & 0x2000) {
                if (flags & 0x4000) {
                    hanim_mul(frame + 80, m, base);
                    if (p4_pose_runs_check) pose_check_ltm((Matrix *)(frame + 80), m, base);
                } else memcpy(frame + 80, m, sizeof *m);
                frame[3] = (frame[3] & ~4) | 8;
            }
        }
        switch (info[2] & 3) {
        case 0: parent = m; break;
        case 1: parent = *--stack; break;
        case 2: *stack++ = parent; parent = m; break;
        default: break;
        }
    }
    *parent_io = parent;
    *stack_io = stack;
    if (count) { pose_runs++; pose_nodes += count; }
    pose_t_runs += HANIM_NOW() - t_run;
    return count;
}

int p4_hanim_update(unsigned *h)
{
    unsigned flags = h[0];
    unsigned t_in = HANIM_NOW();
    Matrix ltm_parent __attribute__((aligned(16))), local_root __attribute__((aligned(16)));
    const Matrix *parent = &ltm_parent;
    char *frame = 0;
    int have_frame = 0;
    if (flags & 2)
        return p4r_00397480(h);
    if ((flags & 1) && (int)h[7] != -1) {
        unsigned *ph = (unsigned *)h[6];
        parent = (const Matrix *)(ph[2] + (h[7] << 6));
        if ((flags & 0x2000) && (flags & 0x4000))
            frame = (char *)ph[5], have_frame = 1;
    } else if (flags & 0x4000) {
        hanim_identity(&local_root);
        parent = &local_root;
        if (flags & 0x2000)
            frame = (char *)h[5], have_frame = 1;
    } else {
        frame = (char *)h[5], have_frame = 1;
    }
    if (have_frame) {
        char *above = frame ? *(char **)(frame + 4) : 0;
        if (!above) {
            hanim_identity(&ltm_parent);
        } else if (!func_003e9240(above)) {
            ltm_parent = *(const Matrix *)p4r_003e9700(above);
        } else {
            ltm_parent = *(const Matrix *)(above + 16);
            for (char *p = *(char **)(above + 4); p; p = *(char **)(p + 4)) {
                Matrix t = ltm_parent;
                RwMatrixMultiply(&ltm_parent, &t, p + 16);
            }
        }
    }
    int update_ltm = flags & 0x2000;
    if (update_ltm) {
        unsigned char *root = *(unsigned char **)((char *)h[5] + 160);
        if (!(root[3] & 3)) { /* rwLinkListAddLLLink(dirty frames, &root->inDirtyListLink) */
            unsigned *head = (unsigned *)(0x88739C + P4_K);
            *(unsigned *)(root + 8) = head[0];
            *(unsigned *)(root + 12) = (unsigned)head;
            ((unsigned *)head[0])[1] = (unsigned)(root + 8);
            head[0] = (unsigned)(root + 8);
            root[3] |= 2;
        }
    }
    char *interp = (char *)h[8];
    int n = (int)h[1], stride = *(int *)(interp + 36);
    void (*key_to_matrix)(Matrix *, const char *) = *(void (**)(Matrix *, const char *))(interp + 60);
    const char *key = interp + 76;
    Matrix *m = (Matrix *)h[2];
    unsigned *info = (unsigned *)h[4];
    const Matrix *stack[64];
    int depth = 0;
    /* b239: stock conversion, or the scale hook over it (every node but its two special keys), on the VFPU */
    const char *hook_a = 0, *hook_b = 0;
    int stock = (char *)key_to_matrix == func_003954b0;
    int fused = p4_hanim_vfpu >= 2 && !((unsigned)m & 15) && (hanim_convertible(key_to_matrix) || hanim_hook_stock(key_to_matrix, &hook_a, &hook_b));
    hanim_t_pre += HANIM_NOW() - t_in;
    hanim_t_calls++, hanim_t_nodes += n, hanim_t_callback += stock ? 0 : n;
    for (int i = 0; i < n; i++, m++, key += stride, info += 4) {
        Matrix local __attribute__((aligned(16)));
        unsigned t_a = HANIM_NOW();
        if (fused && key != hook_a && key != hook_b && !((unsigned)parent & 15)) {
            hanim_node_vfpu(m, &local, key, parent);
            hanim_fused_nodes++, hanim_hook_nodes += !stock;
        } else {
            if (stock)
                hanim_key_to_matrix(&local, key);
            else
                key_to_matrix(&local, key);
            hanim_mul(m, &local, parent);
        }
        unsigned t_b = HANIM_NOW();
        hanim_t_math += t_b - t_a;
        unsigned char *f = (unsigned char *)info[3];
        hanim_t_framed += f != 0;
        if (f) {
            if (flags & 0x1000) {
                if (fused && !((unsigned)f & 3))
                    copy64(f + 16, &local);
                else
                    memcpy(f + 16, &local, sizeof local);
                if (!update_ltm)
                    func_003e9680(f);
            }
            if (update_ltm) {
                if (flags & 0x4000)
                    hanim_mul(f + 80, m, &ltm_parent);
                else if (fused && !((unsigned)f & 3))
                    copy64(f + 80, m);
                else
                    memcpy(f + 80, m, sizeof *m);
                f[3] = (f[3] & ~4) | 8;
            }
        }
        hanim_t_frame += HANIM_NOW() - t_b;
        switch (info[2] & 3) {
        case 0: parent = m; break;
        case 1: if (depth > 0) parent = stack[--depth]; break;
        case 2: if (depth < 64) stack[depth++] = parent; parent = m; break;
        default: break;
        }
    }
    return 1;
}

/* every 8th call with p4_hanim_check: the original, then this one on the same hierarchy; the bone matrices compared */
int p4_hanim_dispatch(unsigned *h, int (*original)(void *))
{
    hanim_calls++;
    if (p4_hanim_check && !(hanim_calls & 7) && !(h[0] & 2) && (int)h[1] > 0 && (int)h[1] <= 256) {
        static Matrix saved[256];
        int n = (int)h[1];
        p4r_00397c40(h); /* the retail update, not original: that is func_00397c40, which may take the C path now */
        memcpy(saved, (void *)h[2], n * sizeof(Matrix));
        p4_hanim_update(h);
        const Matrix *now = (const Matrix *)h[2];
        hanim_checks++;
        for (int i = 0; i < n; i++) {
            const float *a = &saved[i].right.x, *b = &now[i].right.x;
            for (int k = 0; k < 16; k++) {
                if (k % 4 == 3)
                    continue; /* flags and pads */
                float d = a[k] - b[k];
                if (d < 0) d = -d;
                if (d > 0) hanim_diffs++;
                if (d > hanim_max_diff) hanim_max_diff = d;
            }
        }
        return 1;
    }
    return original(h);
}
static void hanim_report(void)
{
    p4_log("hanim c %d vfpu %d check %d: calls %u checks %u differing floats %u max diff %d/1000000 unaligned %u\n", p4_hanim_c,
           p4_hanim_vfpu, p4_hanim_check, hanim_calls, hanim_checks, hanim_diffs, (int)(hanim_max_diff * 1000000), hanim_vfpu_unaligned);
    p4_log("pose runs %d check %d: runs %u nodes %u custom %u matrices checked %u diffs %u flags %u exact floats %u max %d/1e6\n",
           p4_pose_runs, p4_pose_runs_check, pose_runs, pose_nodes, pose_custom_nodes, pose_checks, pose_diffs, pose_flags_diffs, pose_exact_diffs,
           (int)(pose_max_diff * 1000000));
    p4_log("hanim fused nodes %u of them not through func_003954b0 %u; interp vfpu nodes %u slerps to retail %u\n", hanim_fused_nodes, hanim_hook_nodes,
           interp_vfpu_nodes, interp_vfpu_slerps);
    p4_log("hanim time %d kcycles: calls %u nodes %u framed %u callback %u; waiting frames %u nan scan %u prologue %u matrices %u frame writes %u; pose runs %u\n",
           p4_hanim_time, hanim_t_calls, hanim_t_nodes, hanim_t_framed, hanim_t_callback, (unsigned)(hanim_t_wait / 1000), (unsigned)(hanim_t_scan / 1000),
           (unsigned)(hanim_t_pre / 1000), (unsigned)(hanim_t_math / 1000), (unsigned)(hanim_t_frame / 1000), (unsigned)(pose_t_runs / 1000));
    hanim_t_wait = hanim_t_scan = hanim_t_pre = hanim_t_math = hanim_t_frame = pose_t_runs = 0;
    hanim_t_nodes = hanim_t_framed = hanim_t_callback = hanim_t_calls = 0;
    p4_log("skin free waits %u (wait %d); skin dummy words %u (dummy %d)\n", skin_free_waits, p4_skin_free_wait, skin_dummies, p4_skin_dummy);
    p4_log("skin lod depth %d keep %d: draws simplified %u full %u\n", (int)p4_skin_lod_depth, p4_skin_lod_keep, skin_lod_draws, skin_full_draws);
    skin_lod_draws = skin_full_draws = 0;
    skin_free_waits = skin_dummies = 0;
    hanim_fused_nodes = hanim_hook_nodes = interp_vfpu_nodes = interp_vfpu_slerps = 0;
    pose_runs = pose_nodes = pose_checks = pose_diffs = pose_flags_diffs = pose_exact_diffs = 0;
    pose_max_diff = 0; pose_custom_nodes = 0;
    hanim_calls = hanim_checks = hanim_diffs = hanim_vfpu_unaligned = 0;
    hanim_max_diff = 0;
}

#ifdef P4_HW_PROFILE
/* b239: what the animation arithmetic costs by itself, once at boot when the thread profiler is live (no scene
   needed). (1) every counter word around a read of 4096 lines never touched before and around 4096 reads of 64 warm
   lines: which word counts data misses, and what a miss costs. (2) the compressed-key interpolate, retail and
   interp_vfpu, on 32 warm key pairs and on 1024 pairs two cold lines each. (3) one bone, FPU conversion +
   multiply_vfpu against hanim_node_vfpu, warm. The results of both paths are compared. */
static unsigned bench_half(float v)
{
    union { float f; unsigned u; } x = { v };
    int e = (int)((x.u >> 23) & 0xff) - 112;
    if (!(x.u & 0x7fffffff) || e <= 0)
        return 0;
    if (e > 15)
        e = 15;
    return ((x.u >> 16) & 0x8000) | (e << 11) | ((x.u >> 12) & 0x7ff);
}
static void bench_key(CKey *k, float time, int i, float turn)
{
    float q[4] = { 0.1f + 0.01f * (i & 31) + turn, 0.2f, 0.3f - turn, 0.9f }, n = 0;
    for (int c = 0; c < 4; c++)
        n += q[c] * q[c];
    n = 1.0f / sqrtf(n);
    k->prev = 0, k->time = time;
    for (int c = 0; c < 4; c++)
        k->q[c] = bench_half(q[c] * n);
    for (int c = 0; c < 3; c++)
        k->t[c] = bench_half(0.5f + 0.125f * c + turn);
}
static void prof_bench(void)
{
    enum { LINES = 4096, PAIRS = 32, COLD = 1024 };
    char *raw = malloc(LINES * 64 + 16384 + 64);
    if (!raw) {
        p4_log("hwbench: no memory\n");
        return;
    }
    char *p = (char *)(((unsigned)raw + 63) & ~63u), *small = p + LINES * 64;
    unsigned b[20], e[20], sum = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 20; i++)
            b[i] = prof_regs[i];
        for (int i = 0; i < LINES; i++)
            sum += *(volatile unsigned *)(pass ? small + (i & 63) * 64 : p + i * 64);
        for (int i = 0; i < 20; i++)
            e[i] = prof_regs[i];
        p4_log("hwbench %s lines, words 1..19: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", pass ? "4096 reads of 64 warm" : "4096 cold",
               e[1] - b[1], e[2] - b[2], e[3] - b[3], e[4] - b[4], e[5] - b[5], e[6] - b[6], e[7] - b[7], e[8] - b[8], e[9] - b[9], e[10] - b[10],
               e[11] - b[11], e[12] - b[12], e[13] - b[13], e[14] - b[14], e[15] - b[15], e[16] - b[16], e[17] - b[17], e[18] - b[18], e[19] - b[19]);
    }
    /* interpolation */
    static const float custom[6] = { 0.25f, -0.5f, 1.0f, 2.0f, 3.0f, 0.5f };
    CKey *wa = (CKey *)small, *wb = wa + PAIRS;
    IFrame *fr = (IFrame *)(small + 4096), *fv = fr + PAIRS;
    Matrix *bm = (Matrix *)(small + 8192), *bv = bm + PAIRS; /* 64-byte aligned */
    for (int i = 0; i < PAIRS; i++)
        bench_key(wa + i, 0.0f, i, 0.0f), bench_key(wb + i, 1.0f, i, 0.004f);
    for (int i = 0; i < COLD; i++)
        bench_key((CKey *)(p + i * 128), 0.0f, i, 0.0f), bench_key((CKey *)(p + 131072 + i * 128), 1.0f, i, 0.004f);
    unsigned t[5];
    KInterp fn[2] = { (KInterp)p4r_00415bc0, interp_vfpu };
    for (int k = 0; k < 2; k++) {
        fn[k](k ? fv : fr, wa, wb, 0.4f, (void *)custom); /* code warm */
        t[0] = prof_cycles();
        for (int r = 0; r < 64; r++)
            for (int i = 0; i < PAIRS; i++)
                fn[k]((k ? fv : fr) + i, wa + i, wb + i, 0.4f, (void *)custom);
        t[1 + k * 2] = prof_cycles() - t[0];
        for (int i = 0; i < 512; i++) /* 32 KB of other lines: the cold keys leave the cache */
            sum += *(volatile unsigned *)(p + 196608 + i * 64 + 32);
        t[0] = prof_cycles();
        for (int i = 0; i < COLD; i++)
            fn[k]((k ? fv : fr) + (i & 31), p + i * 128, p + 131072 + i * 128, 0.4f, (void *)custom);
        t[2 + k * 2] = prof_cycles() - t[0];
    }
    for (int i = 0; i < PAIRS; i++) { /* the cold loop left other pairs in the frames */
        p4r_00415bc0(fr + i, wa + i, wb + i, 0.4f, (void *)custom);
        interp_vfpu(fv + i, wa + i, wb + i, 0.4f, (void *)custom);
    }
    float worst = 0;
    for (int i = 0; i < PAIRS; i++)
        for (int c = 0; c < 7; c++) {
            float d = fabsf(fr[i].q[c] - fv[i].q[c]);
            if (!(d <= worst))
                worst = d;
        }
    p4_log("hwbench interp cycles a node: retail warm %u cold %u; vfpu warm %u cold %u; max diff %d/1e6, slerps %u, q0 %d t0 %d /1000\n", t[1] / (64 * PAIRS),
           t[2] / COLD, t[3] / (64 * PAIRS), t[4] / COLD, (int)(worst * 1e6f), interp_vfpu_slerps, (int)(fr[0].q[0] * 1000), (int)(fr[0].t[0] * 1000));
    /* one bone */
    Matrix parent __attribute__((aligned(16)));
    hanim_identity(&parent);
    parent.right.x = 0.6f, parent.right.y = 0.8f, parent.up.x = -0.8f, parent.up.y = 0.6f, parent.pos.x = 3, parent.pos.y = -2, parent.pos.z = 5;
    t[0] = prof_cycles();
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < PAIRS; i++) {
            Matrix local __attribute__((aligned(16)));
            hanim_key_to_matrix(&local, (const char *)(fr + i));
            hanim_mul(bm + i, &local, &parent);
        }
    t[1] = prof_cycles() - t[0];
    t[0] = prof_cycles();
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < PAIRS; i++) {
            Matrix local __attribute__((aligned(16)));
            hanim_node_vfpu(bv + i, &local, (const char *)(fr + i), &parent);
        }
    t[2] = prof_cycles() - t[0];
    worst = 0;
    int flags = 0;
    for (int i = 0; i < PAIRS; i++) {
        flags += bm[i].flags != bv[i].flags;
        for (int c = 0; c < 16; c++) {
            float d = fabsf((&bm[i].right.x)[c] - (&bv[i].right.x)[c]);
            if ((c & 3) != 3 && !(d <= worst))
                worst = d;
        }
    }
    p4_log("hwbench bone cycles a node: fpu + multiply_vfpu %u; fused vfpu %u; max diff %d/1e6, flags differing %d (sum %u)\n", t[1] / (64 * PAIRS),
           t[2] / (64 * PAIRS), (int)(worst * 1e6f), flags, sum);
    interp_vfpu_nodes = interp_vfpu_slerps = hanim_fused_nodes = 0;
    free(raw);
}
#endif

/* RpSkin and RpHAnimHierarchy prefixes */
typedef struct {
    int numBones, numBoneIds;
    unsigned char *boneIds;
    Matrix *skinToBone;
    int maxWeights;
    unsigned char (*indices)[4];
    float (*weights)[4];
} Skin;
typedef struct { int flags, numNodes; Matrix *matrices; } Hierarchy;

enum { HIERARCHY_NO_MATRICES = 2, HIERARCHY_LOCAL_SPACE = 0x4000, MAX_BONES = 256, MAX_SKINNED = 32 * 1024 };
#define SKIN_OF_GEOMETRY(g) (*(Skin **)((char *)(g) + R32(0x886498)))
#define HIERARCHY_OF_ATOMIC(a) (*(Hierarchy **)((char *)(a) + R32(0x886494)))

static inline V3 rotate(const V3 *v, const Matrix *m)
{
    V3 out = {
        v->x * m->right.x + v->y * m->up.x + v->z * m->at.x,
        v->x * m->right.y + v->y * m->up.y + v->z * m->at.y,
        v->x * m->right.z + v->y * m->up.z + v->z * m->at.z,
    };
    return out;
}

static inline V3 transform(const V3 *v, const Matrix *m)
{
    V3 out = rotate(v, m);
    out.x += m->pos.x;
    out.y += m->pos.y;
    out.z += m->pos.z;
    return out;
}

/* a, then b */
static void multiply(Matrix *out, const Matrix *a, const Matrix *b);
/* b182: does the shadow pass's flattened pose equal the normal pose times the shear it builds (mdlUpdateDrawState's
   identity: up row = -light.xz / light.y, 0.01)? phase 0 before its pose update (snapshot), 1 after it (plain update;
   11 the look-at pose), 2 after the pose is restored, 3 a shadow at alpha 0 (pose only). Every 16th frame. If 1 holds,
   the GE can draw the shadow from the body's bones with the shear as its world matrix. */
int p4_shadow_probe = 0; /* b182-b185 diagnostic: 1 checks the shadow pose every 16th frame */
/* b186: b185 on the PSP, every plain shadow pose (no look-at) was the body pose times the shear, to 1e-3. With
   p4_shadow_shear the shadow pass (census FIXES in mdlUpdateDrawState) poses the skeleton once, for the body, and
   draws the shadow from it: atomic models and world-space bones go through the shear (p4_shadow_shear_set). Look-at
   poses keep the game's own way (b187: blend tracks no longer do). 0: as b185. */
int p4_shadow_shear = 1;
static Matrix shadow_shear_copy __attribute__((aligned(16)));
static const Matrix *shadow_shear_m;
static unsigned shadow_shear_draws;
int p4_shadow_capsule = 1; /* b194: 0 original projected model, 1 projected capsule mask */
static unsigned capsule_draws, capsule_segments, capsule_fallbacks, capsule_rigid;
static unsigned capsule_alpha = 128;
static void capsule_report(unsigned frames);
static void capsule_begin(void);
static void capsule_end(void);
int p4_shadow_shear_on(void) { return p4_shadow_shear || p4_shadow_capsule; }
/* b192: look-at models drawn from the body pose too (census FIXES). mdlUpdateHierarchyPose slerps the stored look-at
   rotation toward its target by blend and keeps it; the game called it twice a tick (the shadow with the shear as
   parent, then the body). The one body call takes 1 - (1 - blend)^2: two steps, so a head turns as fast; once turned,
   the head points at the target without the shadow call's sheared one. 0: look-at shadows as b191. */
int p4_shadow_lookat = 1;
int p4_shadow_lookat_on(void) { return p4_shadow_lookat || p4_shadow_capsule; }
void p4_shadow_shear_set(const void *m)
{
    if (m) {
        memcpy(&shadow_shear_copy, m, sizeof shadow_shear_copy);
        shadow_shear_m = &shadow_shear_copy;
        capsule_begin();
        shadow_shear_draws++;
    } else {
        if (shadow_shear_m) capsule_end();
        shadow_shear_m = 0;
    }
}
void p4_shadow_shear_set_color(const void *m, unsigned alpha)
{
    capsule_alpha = alpha;
    p4_shadow_shear_set(m);
}
static Matrix *shadow_snap; /* heap, 8 KB: .bss must end below the data image */
static int shadow_snap_n, shadow_snap_ok;
static Matrix *shadow_pose, shadow_shear; /* b184: the shadow pose and its shear, checked against the restored pose */
static unsigned shadow_post_checked, shadow_post_match, shadow_post_bad;
static float shadow_post_worst;
static unsigned shadow_calls[4], shadow_checked[3], shadow_match[3], shadow_logged;
static float shadow_worst[3];
static float matrix_diff(const Matrix *a, const Matrix *b)
{
    const float *x = &a->right.x, *y = &b->right.x;
    float worst = 0;
    for (int k = 0; k < 16; k++) {
        if ((k & 3) == 3)
            continue;
        float d = fabsf(x[k] - y[k]) / (1 + fabsf(y[k]));
        if (!(d <= worst))
            worst = d == d ? d : 1e30f;
    }
    return worst;
}
void p4_shadow_check(void *hierarchy, const void *shear, int phase)
{
    const Hierarchy *h = hierarchy;
    int p = phase == 11 ? 1 : phase;
    if (p >= 0 && p < 4)
        shadow_calls[p]++;
    if (!p4_shadow_probe || !h || !h->matrices || (cache_frame & 15))
        return;
    int n = h->numNodes < 128 ? h->numNodes : 128;
    if (!shadow_snap && !(shadow_snap = memalign(16, 128 * sizeof *shadow_snap)))
        return;
    if (p == 0) {
        memcpy(shadow_snap, h->matrices, n * sizeof *shadow_snap);
        shadow_snap_n = n, shadow_snap_ok = 1;
        return;
    }
    if (p == 3 || !shadow_snap_ok || shadow_snap_n != n || !shear)
        return;
    if (!shadow_pose && !(shadow_pose = memalign(16, 128 * sizeof *shadow_pose)))
        return;
    if (p == 1) {
        memcpy(shadow_pose, h->matrices, n * sizeof *shadow_pose);
        shadow_shear = *(const Matrix *)shear;
    } else if (p == 2) {
        float post = 0;
        int bad = 0;
        for (int i = 0; i < n; i++) {
            Matrix want;
            multiply(&want, &h->matrices[i], &shadow_shear);
            float d = matrix_diff(&shadow_pose[i], &want);
            bad += !(d < 1e-3f);
            if (d > post)
                post = d;
        }
        shadow_post_checked++;
        shadow_post_match += post < 1e-3f;
        shadow_post_bad += bad;
        if (post > shadow_post_worst)
            shadow_post_worst = post;
    }
    float worst = 0;
    int worst_bone = 0, bad = 0, parked = 0;
    Matrix worst_want;
    for (int i = 0; i < n; i++) {
        Matrix want;
        if (p == 1)
            multiply(&want, &shadow_snap[i], (const Matrix *)shear);
        else
            want = shadow_snap[i];
        float d = matrix_diff(&h->matrices[i], &want);
        bad += !(d < 1e-3f);
        parked += !(fabsf(shadow_snap[i].pos.x) < 1e6f && fabsf(shadow_snap[i].pos.y) < 1e6f && fabsf(shadow_snap[i].pos.z) < 1e6f);
        if (d > worst)
            worst = d, worst_bone = i, worst_want = want;
    }
    int k = p == 1 ? (phase == 11 ? 2 : 0) : 1; /* 0 plain shadow pose, 1 restored, 2 look-at shadow pose */
    shadow_checked[k]++;
    shadow_match[k] += worst < 1e-3f;
    if (worst > shadow_worst[k])
        shadow_worst[k] = worst;
    if (worst >= 1e-3f && shadow_logged < 40) {
        shadow_logged++;
        /* b184: which bones differ, and how: the worst one's now / expected / snapshot (pos, then up x1000) */
        const Matrix *a = &h->matrices[worst_bone], *w = &worst_want, *b = &shadow_snap[worst_bone];
        p4_log("TRACE shadow check ph %d h %p n %d bad %d parked %d worst %d/1e6 bone %d now %d %d %d u %d %d %d want %d %d %d u %d %d %d snap %d %d %d\n",
               phase, hierarchy, n, bad, parked, (int)(worst > 1e3f ? 1e9f : worst * 1e6f), worst_bone, (int)a->pos.x, (int)a->pos.y, (int)a->pos.z,
               (int)(a->up.x * 1000), (int)(a->up.y * 1000), (int)(a->up.z * 1000), (int)w->pos.x, (int)w->pos.y, (int)w->pos.z, (int)(w->up.x * 1000),
               (int)(w->up.y * 1000), (int)(w->up.z * 1000), (int)b->pos.x, (int)b->pos.y, (int)b->pos.z);
    }
    if (p == 2)
        shadow_snap_ok = 0;
}
static void shadow_report(unsigned frames)
{
    capsule_report(frames);
    p4_log("shadow skip %d: %u calls per %u frames; shear %d: %u shadows\n", p4_shadow_skip_on, shadow_skips, frames, p4_shadow_shear,
           shadow_shear_draws), shadow_skips = 0, shadow_shear_draws = 0;
    p4_log("shadow pose vs restored x shear: checked %u match %u bad bones %u worst %d/1e6\n", shadow_post_checked, shadow_post_match,
           shadow_post_bad, (int)(shadow_post_worst > 1e3f ? 1e9f : shadow_post_worst * 1e6f));
    shadow_post_checked = shadow_post_match = shadow_post_bad = 0, shadow_post_worst = 0;
    p4_log("shadow probe %d per %u frames: calls before %u after %u restored %u invisible %u; checked/match/worst(1e-6) plain %u %u %d restored %u %u %d lookat %u %u %d\n",
           p4_shadow_probe, frames, shadow_calls[0], shadow_calls[1], shadow_calls[2], shadow_calls[3], shadow_checked[0], shadow_match[0],
           (int)(shadow_worst[0] > 1e3f ? 1e9f : shadow_worst[0] * 1e6f), shadow_checked[1], shadow_match[1],
           (int)(shadow_worst[1] > 1e3f ? 1e9f : shadow_worst[1] * 1e6f), shadow_checked[2], shadow_match[2],
           (int)(shadow_worst[2] > 1e3f ? 1e9f : shadow_worst[2] * 1e6f));
    memset(shadow_calls, 0, sizeof shadow_calls), memset(shadow_checked, 0, sizeof shadow_checked), memset(shadow_match, 0, sizeof shadow_match);
    memset(shadow_worst, 0, sizeof shadow_worst);
}
static void multiply(Matrix *out, const Matrix *a, const Matrix *b)
{
    out->right = rotate(&a->right, b);
    out->up = rotate(&a->up, b);
    out->at = rotate(&a->at, b);
    out->pos = transform(&a->pos, b);
}

/* ponytail: skinned on the CPU, every vertex every frame; the GE's 8 bone matrices need meshes split, when the device asks for it. */
static const V3 *skin_vertices_body(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals);
static const V3 *skin_vertices(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals)
{
    prof_begin(PROF_SKIN);
    const V3 *r = skin_vertices_body(g, skin, hierarchy, normals);
    prof_end(PROF_SKIN);
    return r;
}

static Matrix bones[MAX_BONES] __attribute__((aligned(16)));

/* bones[i] = skin-to-bone, then the bone's pose; returns how many, -1 when there are too many */
static int hierarchy_nodes_seen; /* the hierarchy of the geometry being built, for its log */
/* b155: a party member's head drawn as a line in battle, in some rounds (b150-b154: not the GE skin, the look-at, the
   scale hook, frameskip). Every bone matrix checked each frame: not finite, or a row length outside 0.01..100, is logged
   with the hierarchy's own matrix (first 24 cases). */
int p4_bone_check = 0; /* b169: off by default. With the profiler on, hanim_scan_after re-scanned every recent bone matrix after
   every task: a dungeon corridor 108 ms -> 33 ms a frame (b163 on the PSP, frameskip on). */
static unsigned bone_bad_logged;
static int bone_row_ok(const V3 *r)
{
    float l = r->x * r->x + r->y * r->y + r->z * r->z;
    return l == l && l > 1e-4f && l < 1e4f;
}
static int bone_finite(const Matrix *m)
{
    const float *f = &m->right.x;
    for (int k = 0; k < 16; k++)
        if (k % 4 != 3 && !(f[k] == f[k] && f[k] < 1e8f && f[k] > -1e8f))
            return 0;
    return 1;
}
static void bone_check(const void *skin_, const void *hierarchy_, int count);
static const void *bone_check_geometry;
int hanim_age(const void *h);
/* out = a x b (RenderWare rows: a's rows through b) on the VFPU: b's rows in M400, a's rows with w 0, 0, 0, 1 in
   M600, each row through E400 as guard_vfpu transforms points. 0 when a matrix is not 16-byte aligned. */
static int multiply_vfpu(Matrix *out, const Matrix *a, const Matrix *b)
{
    if (((unsigned)out | (unsigned)a | (unsigned)b) & 15)
        return 0;
    __asm__ volatile(
        "lv.q    C400, 0(%[b])\n lv.q C410, 16(%[b])\n lv.q C420, 32(%[b])\n lv.q C430, 48(%[b])\n"
        "lv.q    C600, 0(%[a])\n lv.q C610, 16(%[a])\n lv.q C620, 32(%[a])\n lv.q C630, 48(%[a])\n"
        "vzero.s S603\n vzero.s S613\n vzero.s S623\n vone.s S633\n"
        "vtfm4.q C700, E400, C600\n"
        "vtfm4.q C710, E400, C610\n"
        "vtfm4.q C720, E400, C620\n"
        "vtfm4.q C730, E400, C630\n"
        "sv.q    C700, 0(%[o])\n sv.q C710, 16(%[o])\n sv.q C720, 32(%[o])\n sv.q C730, 48(%[o])\n"
        : : [o] "r"(out), [a] "r"(a), [b] "r"(b) : "memory");
    return 1;
}
static void anim_drawn(const void *hierarchy);
static int skin_bones(const Skin *skin, const Hierarchy *hierarchy)
{
    anim_drawn(hierarchy);
    hierarchy_nodes_seen = hierarchy->numNodes;
    int count = skin->numBones < hierarchy->numNodes ? skin->numBones : hierarchy->numNodes;
    if (count > MAX_BONES)
        return -1;
    for (int i = 0; i < count; i++) {
        if (!p4_skin_vfpu || !multiply_vfpu(&bones[i], &skin->skinToBone[i], &hierarchy->matrices[i])) {
            skin_vfpu_unaligned += p4_skin_vfpu;
            multiply(&bones[i], &skin->skinToBone[i], &hierarchy->matrices[i]);
            continue;
        }
        if (p4_skin_vfpu_check) {
            Matrix ref;
            multiply(&ref, &skin->skinToBone[i], &hierarchy->matrices[i]);
            const float *x = &bones[i].right.x, *y = &ref.right.x;
            int differs = 0;
            for (int k = 0; k < 16; k++) {
                if ((k & 3) == 3)
                    continue;
                float d = fabsf(x[k] - y[k]) / (1 + fabsf(y[k]));
                if (!(d <= 1e-4f))
                    differs = 1;
                if (d > skin_vfpu_max_diff)
                    skin_vfpu_max_diff = d;
            }
            skin_vfpu_checks++;
            skin_vfpu_diffs += differs;
        }
    }
    if (p4_bone_check)
        bone_check(skin, hierarchy, count);
    return count;
}

static void bone_check(const void *skin_, const void *hierarchy_, int count)
{
    const Skin *skin = skin_;
    const Hierarchy *hierarchy = hierarchy_;
    for (int i = 0; i < count; i++) {
        const Matrix *b = &bones[i], *h = &hierarchy->matrices[i];
        if (bone_finite(b) && bone_row_ok(&b->right) && bone_row_ok(&b->up) && bone_row_ok(&b->at))
            continue;
        bone_bad_count++;
        if (bone_bad_logged >= 24)
            continue;
        bone_bad_logged++;
        p4_log("TRACE bad bone geometry %p skin %p bones %d hierarchy %p age %d flags %x nodes %d bone %d/%d: hier right %d %d %d up %d %d %d at %d %d %d pos %d %d %d (x1000)\n",
               bone_check_geometry, (void *)skin, skin->numBones, (void *)hierarchy, hanim_age(hierarchy), hierarchy->flags, hierarchy->numNodes, i, count,
               (int)(h->right.x * 1000), (int)(h->right.y * 1000), (int)(h->right.z * 1000), (int)(h->up.x * 1000), (int)(h->up.y * 1000),
               (int)(h->up.z * 1000), (int)(h->at.x * 1000), (int)(h->at.y * 1000), (int)(h->at.z * 1000), (int)h->pos.x, (int)h->pos.y,
               (int)h->pos.z);
    }
}

static const V3 *skin_vertices_body(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals)
{
    static V3 (*skinned)[MAX_SKINNED]; /* heap, 768 KB: .bss must end below the data image */
    if (!skinned && !(skinned = malloc(2 * sizeof *skinned)))
        return 0;
    bone_check_geometry = g;
    if (g->numVertices > MAX_SKINNED || skin_bones(skin, hierarchy) < 0)
        return 0;
    const V3 *in = normals ? g->morphTarget->normals : g->morphTarget->verts;
    for (int i = 0; i < g->numVertices; i++) {
        if (skin->maxWeights == 1) { /* RenderWare leaves the single weight out of the stream */
            skinned[normals][i] = normals ? rotate(&in[i], &bones[skin->indices[i][0]]) : transform(&in[i], &bones[skin->indices[i][0]]);
            continue;
        }
        V3 sum = { 0, 0, 0 };
        for (int w = 0; w < skin->maxWeights && w < 4 && skin->weights[i][w] > 0; w++) {
            V3 moved = normals ? rotate(&in[i], &bones[skin->indices[i][w]]) : transform(&in[i], &bones[skin->indices[i][w]]);
            float weight = skin->weights[i][w];
            sum.x += moved.x * weight;
            sum.y += moved.y * weight;
            sum.z += moved.z * weight;
        }
        skinned[normals][i] = sum;
    }
    return skinned[normals];
}

/* Skinning on the GE. Built once per geometry: each mesh's triangles in batches of at most 8 bones (the GE's bone
   matrices), every vertex with its weights for that batch's palette as u8 / 128 summing to 128 (the GE does not
   normalise them), UV, prelight, s8 normal when lit and the bind-pose position. Per frame only the bone matrices
   go to the GE, which skins positions and normals and lights them (bind_ge_light; unlit meshes get the material
   colour through the same path with no light). A skeleton whose sphere may leave the guard band stays on the CPU. */
int p4_ge_skin = 1; /* 0: every skinned atomic on the CPU, as b76 */
typedef struct { int mesh, bones, vtype, indices, vertices, wreal, rigid, strip_count; unsigned char palette[8]; unsigned char *v; unsigned short *idx, *src, *strip; int alpha[2]; GeCall call[2]; float uvs[4]; } SkinBatch;
typedef struct { const Geometry *g; const V3 *verts; int vertices, lit, batches, nradius, packed; unsigned sample, frame, bytes, used[8]; SkinBatch *batch; float *radius; unsigned short regroup[8]; float pack[4]; } SkinGeometry;
enum { SKIN_GEOMETRIES = 64, SKIN_PALETTE = 8 };
static SkinGeometry *skin_cache; /* heap: .bss is full */

/* b205 P3P-shaped skin vertices (P3P's battle frame: s16 positions and u16 UVs on every 3D draw). Position: s16 over
   the geometry's bind-pose box, pack = centre xyz and half extent s, the GE reading q / 32768; draw_skinned_ge folds
   p = centre + s q / 32768 into each bone matrix it sends (rotation x s, translation + rotation . centre). The
   skinned normal grows by s too: the GE divides its lighting dot products by the normal's length (PPSSPP's
   software GE, from hardware probes), so a uniform s does not change the light. UV: u16 over the batch's range,
   TEXSCALE/TEXOFFSET (batch uvs) mapping it back; the offset loses its whole part (skins bind GU_REPEAT), as a
   float24 offset of 100.3 would move texels. 8 bones lit: 36 -> 28 bytes a vertex, unlit 32 -> 24. */
int p4_skin_pack = 1; /* 0: float positions and UVs, as b204 */
/* b207 p4_skin_rigid: a mesh's triangles whose corners all hang from one bone alone go to a batch of that one bone,
   drawn with 1 weight instead of the whole palette (b206 Margaret/battle: ~1.1 weights used a vertex, ~6.2 blended,
   45-64% of triangles rigid), when that bone has at least p4_skin_rigid of them in the mesh (each batch is a draw and
   a bone upload). Opaque meshes only (skin_mesh_regroup), as the triangle order changes. 0 off; in the cache key. */
int p4_skin_rigid = 0;
/* b208 p4_skin_palette: at most this many bones a batch (1-8; the GE blends that many weights a vertex). A first
   triangle with more bones than the cap still starts a batch of its own. In the cache key. */
int p4_skin_palette = 8;
typedef struct { int uv, colour, normal, position, stride; } SkinLayout;
static SkinLayout skin_layout(int bones, int lit, int packed)
{
    SkinLayout l;
    if (packed) { /* GE order u8 weights, u16 UV, 8888 colour, s8 normal, s16 position; each at its own size, stride x4 */
        l.uv = (bones + 1) & ~1;
        l.colour = (l.uv + 4 + 3) & ~3;
        l.normal = l.colour + 4;
        l.position = l.normal + (lit ? 4 : 0);
        l.stride = (l.position + 6 + 3) & ~3;
    } else {
        l.uv = (bones + 3) & ~3;
        l.colour = l.uv + 8;
        l.normal = l.colour + 4;
        l.position = l.normal + (lit ? 4 : 0);
        l.stride = l.position + 12;
    }
    return l;
}
/* a batch vertex's bind position and UV as floats (pack 0: stored as floats) */
static void skin_vertex_read(const unsigned char *in, const SkinLayout *l, const float *pack, const float *uvs, V3 *bind, float uv[2])
{
    if (!pack) {
        memcpy(bind, in + l->position, 12);
        memcpy(uv, in + l->uv, 8);
        return;
    }
    short q[3];
    unsigned short t[2];
    memcpy(q, in + l->position, 6);
    memcpy(t, in + l->uv, 4);
    float k = pack[3] * (1.0f / 32768);
    *bind = (V3){ pack[0] + q[0] * k, pack[1] + q[1] * k, pack[2] + q[2] * k };
    uv[0] = t[0] * (1.0f / 32768) * uvs[0] + uvs[2];
    uv[1] = t[1] * (1.0f / 32768) * uvs[1] + uvs[3];
}
static inline int clamp_round(float x, int lo, int hi)
{
    if (!(x > lo)) return lo; /* NaN too */
    if (!(x < hi)) return hi;
    return (int)lrintf(x);
}

/* the bones a vertex hangs from, with their weights (RenderWare leaves a single weight out of the stream) */
static int vertex_bones(const Skin *skin, int nbones, int i, unsigned char bone[4], float weight[4]);
static void mk_tri_bones(const Skin *skin, int nbones, const int *c, unsigned char *set)
{
    set[0] = 0;
    for (int k = 0; k < 3; k++) {
        unsigned char bone[4];
        float weight[4];
        int n = vertex_bones(skin, nbones, c[k], bone, weight);
        for (int w = 0; w < n; w++) {
            int have = 0;
            for (int s = 1; s <= set[0] && !have; s++)
                have = set[s] == bone[w];
            if (!have && set[0] < 12)
                set[++set[0]] = bone[w];
        }
    }
}
static int vertex_bones(const Skin *skin, int nbones, int i, unsigned char bone[4], float weight[4])
{
    if (skin->maxWeights == 1) {
        bone[0] = skin->indices[i][0];
        weight[0] = 1;
        return bone[0] < nbones;
    }
    int n = 0;
    for (int k = 0; k < skin->maxWeights && k < 4; k++)
        if (skin->weights[i][k] > 0 && skin->indices[i][k] < nbones) {
            bone[n] = skin->indices[i][k];
            weight[n++] = skin->weights[i][k];
        }
    return n;
}

/* b241 p4_skin_dummy: the word 0xFF000000 after each GE-skinned PRIM (PS2RC does this in its skinned sub-lists and
   calls it "the dummy word the GE's skinning needs"; PSPSDK's sceGuDrawArray does not). A lead for the strip hang,
   not an established cause: 0 by default. */
static void skin_free(SkinGeometry *e)
{
    for (int b = 0; b < e->batches; b++)
        free(e->batch[b].v), free(e->batch[b].src), free(e->batch[b].strip);
    free(e->batch);
    free(e->radius);
    skin_ge_bytes -= e->bytes;
    memset(e, 0, sizeof *e);
}

/* one batch: the triangles in tri (corner indices), their own vertex numbering */
/* b209 p4_skin_strip: each GE batch's triangle list also as one triangle strip (greedy: from a triangle, on through the
   neighbour across the last edge while its winding fits the strip's alternation), strips joined by zero-area
   triangles (repeat the last index and the next first one, plus one more when the next must start on an even
   index). The GE then reads ~1 index a triangle, not 3 (b206: indices ~2.9x the vertices); whether it also
   transforms fewer vertices is the device's question. The strip is unrolled and compared with the list (same
   triangles, same winding) before it is kept; idx stays the list for the CPU paths and checks. In the cache key. */
int p4_skin_strip = 1; /* b243: on again. b227-b229 hung at the battle start (flicker, black scene); b241 put the strip address and count into the recorded CALL key (a rebuilt geometry replayed a record pointing at a freed strip) and b242 went through the boss battle with strips on. One pass: repeat before calling it closed. */
int p4_strip_why = 0, p4_strip_check = 0;
/* b211 meshkit (port/meshkit.h), one simplifier for both ways of reshaping a skinned geometry's triangles:
   p4_skin_simplify N (percent of triangles kept, 0 off): online, at skin cache build, many collapses a pass.
   p4_mesh_capture 1: every geometry built writes capture/<key>.bin (positions, dominant bone per vertex, each mesh's
   triangles) next to the EBOOT; tools/meshkit_offline.c simplifies those on the PC (one collapse a pass) into
   override/<key>.bin, triangle lists only. p4_mesh_override 1: a geometry with such a file takes its lists.
   Keys: a hash of the bind positions, vertex and mesh counts. Files hold game data: they stay on the user's card.
   Vertices are never made or moved (half-edge collapse), so UVs, normals and weights stay as they are. */
int p4_skin_simplify = 0, p4_mesh_override = 0, p4_mesh_capture = 0;
typedef struct { unsigned char *data; int size, cap; } MkBuf;
static void mk_put(MkBuf *b, const void *p, int n)
{
    if (!b->data && b->cap < 0)
        return;
    if (b->size + n > b->cap) {
        int cap = (b->size + n) * 2 + 256;
        unsigned char *grown = realloc(b->data, cap);
        if (!grown) {
            free(b->data), b->data = 0, b->cap = -1;
            return;
        }
        b->data = grown, b->cap = cap;
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
}
static void mk_write_file(const char *dir, unsigned key, const MkBuf *b)
{
    char path[48];
    if (!b->data)
        return;
    sceIoMkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/%08x.bin", dir, key);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, b->data, b->size);
    sceIoClose(fd);
}
/* override/<key>.bin: 'P4MO' 1 key meshes, then per mesh ntri and ntri x 3 u16; 0 if missing or not this geometry */
static unsigned char *mk_read_override(unsigned key, int meshes, int *size_out)
{
    char path[48];
    snprintf(path, sizeof path, "override/%08x.bin", key);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    unsigned char *d = size >= 16 ? malloc(size) : 0;
    if (d && sceIoRead(fd, d, size) != size)
        free(d), d = 0;
    sceIoClose(fd);
    unsigned *h = (unsigned *)d;
    if (d && (h[0] != 0x4f4d3450u || h[1] != 1 || h[2] != key || (int)h[3] != meshes))
        free(d), d = 0;
    *size_out = d ? size : 0;
    return d;
}
/* the bone set of one triangle (set[0] the count), as the batcher wants it */
static void mk_tri_bones(const Skin *skin, int nbones, const int *c, unsigned char *set);
typedef struct { unsigned key; int tri; } StripEdge;
static int strip_edge_cmp(const void *a, const void *b)
{
    unsigned x = ((const StripEdge *)a)->key, y = ((const StripEdge *)b)->key;
    return x < y ? -1 : x > y;
}
static unsigned strip_key(int a, int b) { return a < b ? (unsigned)a << 16 | b : (unsigned)b << 16 | a; }
/* a triangle by its corners, rotated so the smallest comes first (keeps the winding) */
static unsigned long long strip_canon(int a, int b, int c)
{
    if (b < a && b < c) { int t = a; a = b; b = c; c = t; }
    else if (c < a && c < b) { int t = c; c = b; b = a; a = t; }
    return (unsigned long long)a << 32 | (unsigned long long)b << 16 | (unsigned)c;
}
static int strip_ull_cmp(const void *a, const void *b)
{
    unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
    return x < y ? -1 : x > y;
}
typedef struct { int *t, n, cap; } StripHeap; /* b213: min-heap of triangle numbers */
static int strip_heap_push(StripHeap *h, int t)
{
    if (h->n == h->cap) {
        int cap = h->cap * 2 + 64, *grown = realloc(h->t, cap * sizeof *grown);
        if (!grown)
            return 0;
        h->t = grown, h->cap = cap;
    }
    int i = h->n++;
    for (; i && h->t[(i - 1) / 2] > t; i = (i - 1) / 2)
        h->t[i] = h->t[(i - 1) / 2];
    h->t[i] = t;
    return 1;
}
static void strip_heap_pop(StripHeap *h)
{
    int last = h->t[--h->n], i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n)
            break;
        if (c + 1 < h->n && h->t[c + 1] < h->t[c])
            c++;
        if (h->t[c] >= last)
            break;
        h->t[i] = h->t[c], i = c;
    }
    if (h->n)
        h->t[i] = last;
}
/* b213: triangle t is used: each unused neighbour has one unused neighbour fewer per shared edge */
static int strip_take(int t, const StripEdge *edges, const int *range, const unsigned char *used, int *count, StripHeap *heap)
{
    for (int k = 0; k < 3; k++)
        for (int e = range[t * 6 + k * 2]; e < range[t * 6 + k * 2 + 1]; e++) {
            int u = edges[e].tri;
            if (u == t || used[u])
                continue;
            if (--count[u] < 4 && !strip_heap_push(&heap[count[u]], u))
                return 0;
        }
    return 1;
}
/* out: strip indices (room for 6 n + 6: b229, a strip of one triangle and its join take 6; 5 n + 4 overran the heap on
   batches of lone triangles and crashed the PSP in free() as a battle loaded); returns their count, or 0 when it does not check out. slow: b209's linear
   start search (the reference for p4_strip_check); else (b213) per-triangle counts of unused neighbours in buckets
   0..3 give the same choice (fewest, then the lowest triangle number) without rescanning every triangle per strip:
   b212 on the PSP, 10 characters' strips 6.7-7.9 s. */
static int skin_stripify(const unsigned short *list, int corners, unsigned short *out, int slow)
{
    int n = corners / 3, m = 0, len = 0;
    StripEdge *edges = malloc(n * 3 * sizeof *edges);
    unsigned char *used = calloc(n ? n : 1, 1);
    unsigned long long *want = malloc(n * sizeof *want), *got = malloc(n * sizeof *got);
    if (!edges || !used || !want || !got) {
        free(edges), free(used), free(want), free(got);
        return 0;
    }
    for (int t = 0; t < n; t++) {
        const unsigned short *c = list + t * 3;
        if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]) { /* draws nothing: left out */
            used[t] = 1;
            continue;
        }
        want[m++] = strip_canon(c[0], c[1], c[2]);
        for (int k = 0; k < 3; k++)
            edges[(t * 3 + k)] = (StripEdge){ strip_key(c[k], c[(k + 1) % 3]), t };
    }
    int ne = 0;
    for (int t = 0; t < n; t++)
        if (!used[t])
            for (int k = 0; k < 3; k++)
                edges[ne++] = edges[t * 3 + k];
    qsort(edges, ne, sizeof *edges, strip_edge_cmp);
    /* b213: each triangle's edges as ranges of the sorted edges, its unused-neighbour count, buckets by count */
    int *range = slow ? 0 : malloc((n ? n : 1) * 6 * sizeof *range), *count = slow ? 0 : malloc((n ? n : 1) * sizeof *count);
    StripHeap heap[4] = { { 0 } };
    if (!slow && (!range || !count))
        slow = 1;
    for (int t = 0; t < n && !slow; t++) {
        count[t] = 0;
        if (used[t])
            continue;
        for (int k = 0; k < 3; k++) {
            const unsigned short *c = list + t * 3;
            StripEdge key = { strip_key(c[k], c[(k + 1) % 3]), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            range[t * 6 + k * 2] = hit ? hit - edges : 0;
            for (; hit && hit < edges + ne && hit->key == key.key; hit++)
                count[t] += hit->tri != t;
            range[t * 6 + k * 2 + 1] = hit ? hit - edges : 0;
        }
        if (count[t] < 4 && !strip_heap_push(&heap[count[t]], t))
            slow = 1;
    }
    for (;;) {
        /* start where the fewest unused neighbours are (an edge of what is left), so strips do not cut it in two */
        int start = -1, fewest = 4;
        for (int b = 0; !slow && b < 4 && start < 0; b++) { /* stale entries (used, or moved to a lower bucket) go */
            while (heap[b].n && (used[heap[b].t[0]] || count[heap[b].t[0]] != b))
                strip_heap_pop(&heap[b]);
            if (heap[b].n)
                start = heap[b].t[0];
        }
        for (int t = 0; slow && t < n && fewest > 0; t++) {
            if (used[t])
                continue;
            int free_n = 0;
            for (int k = 0; k < 3; k++) {
                const unsigned short *c = list + t * 3;
                StripEdge key = { strip_key(c[k], c[(k + 1) % 3]), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
                while (hit && hit > edges && hit[-1].key == key.key)
                    hit--;
                for (; hit && hit < edges + ne && hit->key == key.key; hit++)
                    free_n += hit->tri != t && !used[hit->tri];
            }
            if (free_n < fewest)
                fewest = free_n, start = t;
        }
        if (start < 0)
            break;
        const unsigned short *c = list + start * 3;
        int v[3] = { c[0], c[1], c[2] }, best = 0;
        for (int r = 0; r < 3 && !best; r++) { /* a rotation whose last edge leads on */
            int p = c[(r + 1) % 3], q = c[(r + 2) % 3];
            StripEdge key = { strip_key(p, q), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            for (; hit && hit < edges + ne && hit->key == key.key && !best; hit++)
                if (hit->tri != start && !used[hit->tri])
                    best = 1, v[0] = c[r], v[1] = p, v[2] = q;
        }
        used[start] = 1;
        if (!slow && !strip_take(start, edges, range, used, count, heap))
            break; /* out of memory: the check below fails and the batch stays a list */
        if (len) { /* join: zero-area triangles, the next strip starting on an even index */
            out[len] = out[len - 1], len++;
            out[len++] = v[0];
            if (len & 1)
                out[len++] = v[0];
        }
        int first = len;
        out[len++] = v[0], out[len++] = v[1], out[len++] = v[2];
        for (;;) {
            int p = out[len - 2], q = out[len - 1], odd = (len - 2 - first) & 1, next = -1, r = 0;
            StripEdge key = { strip_key(p, q), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            for (; hit && hit < edges + ne && hit->key == key.key && next < 0; hit++) {
                if (used[hit->tri])
                    continue;
                const unsigned short *u = list + hit->tri * 3;
                int third = u[0] != p && u[0] != q ? u[0] : u[1] != p && u[1] != q ? u[1] : u[2];
                /* triangle number len - 2 - first of the strip: even (p, q, r), odd (q, p, r) */
                unsigned long long need = odd ? strip_canon(q, p, third) : strip_canon(p, q, third);
                if (need == strip_canon(u[0], u[1], u[2]))
                    next = hit->tri, r = third;
            }
            if (next < 0)
                break;
            used[next] = 1;
            out[len++] = r;
            if (!slow && !strip_take(next, edges, range, used, count, heap))
                break;
        }
    }
    /* unroll and compare */
    int k = 0, ok = 1;
    for (int i = 0; i + 2 < len && ok; i++) {
        int a = out[i], b = out[i + 1], c = out[i + 2];
        if (a == b || b == c || a == c)
            continue;
        if (k >= m)
            ok = 0;
        else
            got[k++] = i & 1 ? strip_canon(b, a, c) : strip_canon(a, b, c);
    }
    if (ok && k == m) {
        qsort(want, m, sizeof *want, strip_ull_cmp);
        qsort(got, k, sizeof *got, strip_ull_cmp);
        ok = !memcmp(want, got, m * sizeof *want);
    } else
        ok = 0;
    free(edges), free(used), free(want), free(got), free(range), free(count);
    for (int b = 0; b < 4; b++)
        free(heap[b].t);
    if (!ok)
        strip_failed++;
    return ok ? len : 0;
}

static const V3 *weld_verts;
static int weld_cmp(const void *a, const void *b) /* by position bits, then index (stable: the first is the lowest) */
{
    int i = *(const int *)a, j = *(const int *)b, c = memcmp(&weld_verts[i], &weld_verts[j], sizeof *weld_verts);
    return c ? c : i - j;
}

static int skin_batch(SkinGeometry *e, int mesh, const unsigned char *palette, int bones, const int *tri, int corners,
                      const Geometry *g, const Skin *skin, int nbones)
{
    unsigned stamp = ++mesh_generation;
    int used = 0;
    for (int k = 0; k < corners; k++)
        if (mesh_stamp[tri[k]] != stamp) {
            mesh_stamp[tri[k]] = stamp;
            mesh_remap[tri[k]] = used++;
        }
    SkinLayout l = skin_layout(bones, e->lit, e->packed);
    int stride = l.stride, bytes = stride * used + corners * 2;
    unsigned char *v = memalign(16, bytes);
    SkinBatch *grown = realloc(e->batch, (e->batches + 1) * sizeof *grown);
    if (!v || !grown) {
        free(v);
        if (grown)
            e->batch = grown;
        return 0;
    }
    e->batch = grown;
    SkinBatch *b = &e->batch[e->batches++];
    memset(b, 0, sizeof *b);
    b->mesh = mesh;
    b->bones = bones;
    memcpy(b->palette, palette, bones);
    b->vtype = GU_WEIGHTS(bones) | GU_WEIGHT_8BIT | (e->packed ? GU_TEXTURE_16BIT | GU_VERTEX_16BIT : GU_TEXTURE_32BITF | GU_VERTEX_32BITF) | GU_COLOR_8888 |
               (e->lit ? GU_NORMAL_8BIT : 0) | GU_INDEX_16BIT | GU_TRANSFORM_3D;
    b->v = v;
    b->idx = (unsigned short *)(v + stride * used);
    b->src = malloc(used * 2); /* b178: the geometry vertex of each batch vertex, for p4_skin_diff */
    b->indices = corners;
    b->vertices = used;
    const V3 *verts = g->morphTarget->verts, *normals = g->morphTarget->normals;
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    float lo[2] = { 0, 0 }, range[2] = { 0, 0 }; /* packed: the batch's UV box */
    if (e->packed && uv) {
        float hi[2] = { uv[tri[0]][0], uv[tri[0]][1] };
        lo[0] = hi[0], lo[1] = hi[1];
        for (int k = 1; k < corners; k++)
            for (int c = 0; c < 2; c++) {
                float x = uv[tri[k]][c];
                if (x < lo[c]) lo[c] = x;
                if (x > hi[c]) hi[c] = x;
            }
        for (int c = 0; c < 2; c++) {
            range[c] = hi[c] - lo[c];
            if (!(range[c] < 1e6f) || !(lo[c] > -1e6f)) /* not finite: every vertex at 0 */
                lo[c] = range[c] = 0;
            b->uvs[c] = range[c] * (32768.0f / 65535);
            b->uvs[2 + c] = lo[c] - floorf(lo[c]);
        }
    }
    b->alpha[0] = 255, b->alpha[1] = 0; /* the vertex colours' alpha; the material's comes on top when drawn */
    stamp = ++mesh_generation;
    for (int k = 0; k < corners; k++) {
        int j = tri[k];
        b->idx[k] = mesh_remap[j];
        if (mesh_stamp[j] == stamp)
            continue;
        mesh_stamp[j] = stamp;
        if (b->src)
            b->src[mesh_remap[j]] = j;
        unsigned char *out = v + stride * mesh_remap[j];
        memset(out, 0, stride);
        unsigned char bone[4];
        float weight[4], slot[SKIN_PALETTE] = { 0 };
        int n = vertex_bones(skin, nbones, j, bone, weight);
        for (int w = 0; w < n; w++)
            for (int s = 0; s < bones; s++)
                if (palette[s] == bone[w])
                    slot[s] += weight[w];
        int sum = 0, largest = 0;
        for (int s = 0; s < bones; s++) {
            int q = (int)(slot[s] * 128 + 0.5f);
            out[s] = q > 128 ? 128 : q;
            sum += out[s];
            if (slot[s] > slot[largest])
                largest = s;
        }
        out[largest] += 128 - sum; /* exactly 128: the GE sums weight x bone as given */
        for (int s = 0; s < bones; s++)
            b->wreal += out[s] != 0;
        if (e->packed) {
            unsigned short t[2];
            for (int c = 0; c < 2; c++)
                t[c] = uv && range[c] > 0 ? clamp_round((uv[j][c] - lo[c]) / range[c] * 65535, 0, 65535) : 0;
            memcpy(out + l.uv, t, 4);
            float k = 32768 / e->pack[3];
            short q[3] = { clamp_round((verts[j].x - e->pack[0]) * k, -32768, 32767), clamp_round((verts[j].y - e->pack[1]) * k, -32768, 32767),
                           clamp_round((verts[j].z - e->pack[2]) * k, -32768, 32767) };
            memcpy(out + l.position, q, 6);
        } else {
            float f[2] = { uv ? uv[j][0] : 0, uv ? uv[j][1] : 0 };
            memcpy(out + l.uv, f, 8);
            memcpy(out + l.position, &verts[j], 12);
        }
        unsigned base = g->preLit ? double_colour(g->preLit[j]) : e->lit ? 0xff000000 : 0xffffffff;
        memcpy(out + l.colour, &base, 4);
        if ((int)(base >> 24) < b->alpha[0]) b->alpha[0] = base >> 24;
        if ((int)(base >> 24) > b->alpha[1]) b->alpha[1] = base >> 24;
        if (e->lit) {
            const V3 *nv = &normals[j];
            float len = sqrtf(nv->x * nv->x + nv->y * nv->y + nv->z * nv->z), inv = len > 0 ? 1.0f / len : 0;
            out[l.normal] = (unsigned char)normal_s8(nv->x * inv);
            out[l.normal + 1] = (unsigned char)normal_s8(nv->y * inv);
            out[l.normal + 2] = (unsigned char)normal_s8(nv->z * inv);
        }
    }
    for (int t = 0; t + 2 < corners; t += 3) { /* b206: rigid triangles, every corner on one and the same bone */
        int slot = -1, rigid = 1;
        for (int c = 0; c < 3 && rigid; c++) {
            const unsigned char *w = v + stride * b->idx[t + c];
            int only = -1;
            for (int k = 0; k < bones; k++)
                if (w[k])
                    only = only < 0 ? k : -2;
            rigid = only >= 0 && (slot < 0 || only == slot);
            slot = only;
        }
        b->rigid += rigid;
    }
    if (p4_skin_strip && corners >= 6) {
        unsigned short *strip = malloc((6 * (corners / 3) + 6) * sizeof *strip);
        unsigned t0 = sceKernelGetSystemTimeLow();
        int n = strip ? skin_stripify(b->idx, corners, strip, 0) : 0;
        strip_us += sceKernelGetSystemTimeLow() - t0;
        static unsigned overlong_logged;
        if (n > 5 * (corners / 3) + 4 && overlong_logged++ < 4) /* b229: what overran the old 5 n + 4 buffer */
            p4_log("TRACE skin strip longer than 5n+4: %d indices for %d triangles (mesh %d)\n", n, corners / 3, mesh);
        if (n && p4_strip_check) { /* b213: the linear search must give the very same strip */
            unsigned short *ref = malloc((6 * (corners / 3) + 6) * sizeof *ref);
            if (ref) {
                int rn = skin_stripify(b->idx, corners, ref, 1);
                strip_checks++;
                strip_check_diffs += rn != n || memcmp(ref, strip, n * sizeof *ref);
                free(ref);
            }
        }
        if (n && n < corners) {
            b->strip = strip, b->strip_count = n;
            sceKernelDcacheWritebackRange(b->strip, n * sizeof *b->strip);
            strip_batches++, strip_list_idx += corners, strip_idx += n;
            bytes += n * sizeof *b->strip;
        } else
            free(strip);
    }
    sceKernelDcacheWritebackRange(v, bytes - b->strip_count * (int)sizeof *b->strip);
    e->bytes += bytes;
    skin_ge_bytes += bytes;
    return 1;
}

/* b199 PSP-shaped skin batches. p4_skin_sticky: a batch puts a bone the GE already holds (from the batch before)
   back in the same slot, so draw_skinned_ge does not send its matrix again. p4_skin_regroup: an opaque mesh's
   triangles are batched first-fit by bone set (b193 counts: battle body 152 -> 99 batches, dialogue 228 -> 157);
   a mesh that may blend keeps the PS2 triangle order. 1: no texture or a texture with no translucent texel, opaque
   vertex and material alpha when built; 2: every mesh (to compare). Both act when a geometry's cache is built. */
int p4_skin_sticky = 1, p4_skin_regroup = 1;
static unsigned skin_regrouped;
static int raster_opaque(const Raster *r)
{
    if (!r || !r->originalPixels)
        return 1;
    const SwizzleHead *h = swizzle_head(r->originalPixels);
    if (h)
        return h->alpha_min >= 255;
    unsigned char used[32];
    unsigned max;
    raster_used_entries(r, used);
    return raster_alpha_min(r, used, &max) >= 255;
}
static int skin_mesh_regroup(const Mesh *mesh, const Geometry *g, const int *corner, int ntri)
{
    if (p4_skin_regroup >= 2)
        return 1;
    if (!p4_skin_regroup || ntri < 2 || mesh->material->colour >> 24 != 255)
        return 0;
    void **texture = mesh->material->texture;
    if (!raster_opaque(texture ? (const Raster *)*texture : 0))
        return 0;
    for (int k = 0; k < ntri * 3 && g->preLit; k++)
        if (double_colour(g->preLit[corner[k]]) >> 24 != 255)
            return 0;
    return 1;
}

/* the cached GE form of a skinned geometry, built on first use; 0: it cannot go to the GE */
/* b193 p4_batch_stats: what other batchings would give, counted once per built geometry (regroup[]) and summed per
   draw and pass (body / shadow) in p4_batch_stats_report. First-fit: a batch takes any later triangle of its group
   whose bones still fit, not only the next one. Groups: each mesh; meshes sharing a texture (the shadow pass draws
   one colour); all meshes. Alpha order and blend/depth state are not told apart, so these are lower bounds. */
int p4_batch_stats = 1;
enum { BS_DRAWS, BS_BATCHES, BS_MESHES, BS_FF_MESH, BS_FF_TEX, BS_FF_ALL, BS_PAL, BS_PAL_MESH, BS_PAL_TEX, BS_PAL_ALL, BS_SENT, BS_N };
static unsigned *batch_stats; /* [2][BS_N], heap: .bss is full */

static void first_fit(const unsigned char (*set)[13], const int *group, int n, unsigned char *done, unsigned short *batches, unsigned short *palettes)
{
    memset(done, 0, n);
    *batches = *palettes = 0;
    for (int start = 0; start < n; start++) {
        if (done[start])
            continue;
        unsigned char palette[SKIN_PALETTE];
        int np = 0;
        for (int t = start; t < n; t++) {
            if (done[t] || group[t] != group[start])
                continue;
            unsigned char extra[12];
            int add = 0;
            for (int k = 1; k <= set[t][0]; k++) {
                int have = 0;
                for (int s = 0; s < np && !have; s++)
                    have = palette[s] == set[t][k];
                if (!have)
                    extra[add++] = set[t][k];
            }
            if (np + add > SKIN_PALETTE)
                continue;
            memcpy(palette + np, extra, add);
            np += add;
            done[t] = 1;
        }
        ++*batches;
        *palettes += np;
    }
}

static void batch_regroup(SkinGeometry *e, const Geometry *g, const Skin *skin, int nbones)
{
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, n = 0, meshes = 0;
    for (int m = 0; m < header->numMeshes; m++)
        n += strip ? (mesh[m].numIndices > 2 ? mesh[m].numIndices - 2 : 0) : mesh[m].numIndices / 3;
    unsigned char (*set)[13] = malloc((n ? n : 1) * sizeof *set), *done = malloc(n ? n : 1);
    int *group = malloc(3 * (n ? n : 1) * sizeof *group);
    if (!set || !done || !group)
        goto out;
    n = 0;
    for (int m = 0; m < header->numMeshes; m++) {
        const unsigned short *idx = mesh[m].indices;
        int triangles = strip ? mesh[m].numIndices - 2 : mesh[m].numIndices / 3, used = 0;
        for (int t = 0; t < triangles; t++) {
            int c[3] = { strip ? idx[t] : idx[t * 3], strip ? idx[t + 1] : idx[t * 3 + 1], strip ? idx[t + 2] : idx[t * 3 + 2] };
            if (strip && (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]))
                continue;
            set[n][0] = 0;
            for (int k = 0; k < 3; k++) {
                unsigned char bone[4];
                float weight[4];
                int nb = vertex_bones(skin, nbones, c[k], bone, weight);
                for (int w = 0; w < nb; w++) {
                    int have = 0;
                    for (int s = 1; s <= set[n][0] && !have; s++)
                        have = set[n][s] == bone[w];
                    if (!have)
                        set[n][++set[n][0]] = bone[w];
                }
            }
            group[n++] = m;
            used = 1;
        }
        meshes += used;
    }
    /* group ids per variant: mesh (group), first mesh with the same texture (by_tex), one group (all) */
    int *by_tex = group + n, *all = group + 2 * n;
    for (int t = 0; t < n; t++) {
        int gm = group[t], tex = gm;
        for (int o = 0; o < gm; o++)
            if (mesh[o].material->texture == mesh[gm].material->texture) {
                tex = o;
                break;
            }
        by_tex[t] = tex;
        all[t] = 0;
    }
    e->regroup[0] = meshes;
    first_fit(set, group, n, done, &e->regroup[1], &e->regroup[5]);
    first_fit(set, by_tex, n, done, &e->regroup[2], &e->regroup[6]);
    first_fit(set, all, n, done, &e->regroup[3], &e->regroup[7]);
    e->regroup[4] = 0;
    for (int b = 0; b < e->batches; b++)
        e->regroup[4] += e->batch[b].bones;
out:
    free(set), free(done), free(group);
}

static void batch_stats_add(const SkinGeometry *e, int shadow, unsigned sent)
{
    if (!batch_stats && !(batch_stats = calloc(2 * BS_N, sizeof *batch_stats)))
        return;
    unsigned *s = batch_stats + (shadow ? BS_N : 0);
    s[BS_DRAWS]++;
    s[BS_BATCHES] += e->batches;
    s[BS_MESHES] += e->regroup[0];
    s[BS_FF_MESH] += e->regroup[1];
    s[BS_FF_TEX] += e->regroup[2];
    s[BS_FF_ALL] += e->regroup[3];
    s[BS_PAL] += e->regroup[4];
    s[BS_PAL_MESH] += e->regroup[5];
    s[BS_PAL_TEX] += e->regroup[6];
    s[BS_PAL_ALL] += e->regroup[7];
    s[BS_SENT] += sent;
}

void p4_batch_stats_report(void)
{
    p4_log("skin batches sticky %d regroup %d regrouped batches built %u\n", p4_skin_sticky, p4_skin_regroup, skin_regrouped);
    skin_regrouped = 0;
    if (!p4_batch_stats || !batch_stats)
        return;
    for (int p = 0; p < 2; p++) {
        unsigned *s = batch_stats + p * BS_N;
        p4_log("batch stats %s per 256 frames: draws %u batches %u meshes %u first-fit mesh %u texture %u all %u\n", p ? "shadow" : "body",
               s[BS_DRAWS], s[BS_BATCHES], s[BS_MESHES], s[BS_FF_MESH], s[BS_FF_TEX], s[BS_FF_ALL]);
        p4_log("batch stats %s palette bones now %u mesh %u texture %u all %u; bones sent %u\n", p ? "shadow" : "body", s[BS_PAL],
               s[BS_PAL_MESH], s[BS_PAL_TEX], s[BS_PAL_ALL], s[BS_SENT]); /* one line ran past p4_log's buffer */
    }
    memset(batch_stats, 0, 2 * BS_N * sizeof *batch_stats);
}

/* b245 p4_skin_lod_depth D (clip w, 0 off), p4_skin_lod_keep K (percent of triangles): a skinned character whose root
   was farther than D in the last frame is drawn from a second cache entry simplified to K% (meshkit, as
   p4_skin_simplify, built once when it first goes far: 0.2-0.5 s for a party). It comes back to the full mesh below
   0.9 D, so it does not flip at the edge. The user (2026-11-14): far characters and scenery may lose detail the PSP
   screen does not show. In the dialogue the party stands at 650-1300; b211 found 60% visibly damaged up close. */
static int skin_simplify_now; /* the percent this skin_geometry call builds and keys with */
static SkinGeometry *skin_geometry(const Geometry *g, const Skin *skin, int nbones, int lit)
{
    if (!skin_cache && !(skin_cache = calloc(SKIN_GEOMETRIES, sizeof *skin_cache)))
        return 0;
    const V3 *verts = g->morphTarget->verts;
    unsigned sample = hash_sample(2166136261u, verts, g->numVertices, sizeof *verts);
    sample ^= (unsigned)(p4_skin_sticky | p4_skin_regroup << 1 | (p4_skin_pack != 0) << 3 | (unsigned)p4_skin_rigid << 4 | (unsigned)p4_skin_palette << 12 | (unsigned)p4_skin_strip << 16 | (unsigned)skin_simplify_now << 18 | (p4_mesh_override != 0) << 25) * 0x9e3779b1u; /* b199: a switch poke builds the new shape */
    SkinGeometry *spare = 0;
    for (int i = 0; i < SKIN_GEOMETRIES; i++) {
        SkinGeometry *e = &skin_cache[i];
        if (e->g == g && e->verts == verts && e->vertices == g->numVertices && e->lit == lit && e->sample == sample) {
            e->frame = cache_frame;
            return e->batches ? e : 0;
        }
        if (e->frame != cache_frame && (!spare || !e->g || (spare->g && e->frame < spare->frame)))
            spare = e; /* empty, or the longest unused; never one the GE still reads this frame */
    }
    if (!spare || g->numVertices > MAX_GUARDED || !mesh_scratch(g->numVertices))
        return 0;
    /* b241: with the pipeline the GE may still be drawing the previous frame, which read this entry's buffers */
    if (p4_skin_free_wait && spare->g && cache_frame - spare->frame <= 1) {
        pipe_texture_wait();
        skin_free_waits++;
    }
    skin_free(spare);
    SkinGeometry *e = spare;
    e->g = g;
    e->verts = verts;
    e->vertices = g->numVertices;
    e->lit = lit;
    e->sample = sample;
    e->frame = cache_frame;
    skin_ge_builds++;
    unsigned build_t0 = sceKernelGetSystemTimeLow(); /* b212: what a build costs (a hitch when a character appears) */
    if (p4_skin_pack && g->numVertices > 0) { /* the bind-pose box for s16 positions; not finite: floats */
        float lo[3], hi[3];
        memcpy(lo, &verts[0], 12), memcpy(hi, &verts[0], 12);
        for (int j = 1; j < g->numVertices; j++) {
            const float *p = &verts[j].x;
            for (int c = 0; c < 3; c++) {
                if (p[c] < lo[c]) lo[c] = p[c];
                if (p[c] > hi[c]) hi[c] = p[c];
            }
        }
        float half = 0;
        for (int c = 0; c < 3; c++) {
            e->pack[c] = (lo[c] + hi[c]) * 0.5f;
            if ((hi[c] - lo[c]) * 0.5f > half)
                half = (hi[c] - lo[c]) * 0.5f;
        }
        e->pack[3] = half > 0 ? half : 1;
        e->packed = half < 1e6f && lo[0] > -1e6f && lo[1] > -1e6f && lo[2] > -1e6f;
    }
    /* b151: vertices whose bones fall outside the hierarchy (index >= nbones) lose those weights in vertex_bones; with
       none left a GE batch gives the whole weight to palette slot 0 and the vertex flies to that bone (a battle: a long
       line from one party member, in some poses only). Logged once per geometry. */
    {
        int lost = 0, partly = 0, top = 0;
        for (int j = 0; j < g->numVertices; j++) {
            int k_max = skin->maxWeights < 4 ? skin->maxWeights : 4, dropped = 0, kept = 0;
            for (int k = 0; k < k_max; k++) {
                int b = skin->indices[j][k];
                float w = skin->maxWeights == 1 ? 1 : skin->weights[j][k];
                if (w <= 0)
                    continue;
                if (b > top) top = b;
                if (b < nbones) kept++; else dropped++;
            }
            lost += dropped && !kept;
            partly += dropped && kept;
        }
        if (lost || partly || skin->numBones != hierarchy_nodes_seen)
            p4_log("TRACE skin geometry %x v%d bones %d ids %d nodes %d used %d maxw %d top index %d: vertices lost %d partly %d\n", (unsigned)g,
                   g->numVertices, skin->numBones, skin->numBoneIds, hierarchy_nodes_seen, nbones, skin->maxWeights, top, lost, partly);
    }
    /* how far from its joint each bone carries a vertex (bone space): the per-frame guard test */
    if ((e->radius = calloc(nbones, sizeof *e->radius)))
        e->nradius = nbones;
    for (int j = 0; j < g->numVertices && e->radius; j++) {
        unsigned char bone[4];
        float weight[4];
        int n = vertex_bones(skin, nbones, j, bone, weight);
        for (int w = 0; w < n; w++) {
            e->used[bone[w] >> 5] |= 1u << (bone[w] & 31); /* b208 bone census */
            V3 local = transform(&verts[j], &skin->skinToBone[bone[w]]);
            float d = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
            if (!(d <= 1e30f))
                d = INFINITY; /* b180: NaN was skipped by the compare below and left radius 0, which skips the guard test */
            if (d > e->radius[bone[w]])
                e->radius[bone[w]] = d;
        }
    }
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, ok = 1;
    /* b199: the GE bone slots as the batches built so far leave them (draw_skinned_ge keeps a slot whose bone is
       already there); a new batch puts a bone it shares with them back in the same slot (p4_skin_sticky) */
    unsigned char held[SKIN_PALETTE];
    memset(held, 0xff, sizeof held);
    int *weld = 0; /* b210: each vertex -> the first vertex at the same position */
    if (p4_skin_strip && p4_strip_why && g->numVertices > 0 && g->numVertices <= 65535 && (weld = malloc(g->numVertices * sizeof *weld))) {
        int *order = malloc(g->numVertices * sizeof *order);
        if (order) {
            for (int j = 0; j < g->numVertices; j++)
                order[j] = j;
            weld_verts = verts;
            qsort(order, g->numVertices, sizeof *order, weld_cmp);
            for (int j = 0; j < g->numVertices; j++)
                weld[order[j]] = j && !memcmp(&verts[order[j]], &verts[order[j - 1]], sizeof *verts) ? weld[order[j - 1]] : order[j];
            free(order);
        } else
            free(weld), weld = 0;
    }
    /* b211 meshkit: the geometry's key, dominant bones, an override file, a capture */
    unsigned mk_key_g = hash_sample(2166136261u, verts, g->numVertices, sizeof *verts) ^ (unsigned)g->numVertices * 0x9e3779b1u ^ (unsigned)header->numMeshes;
    int reshape = (skin_simplify_now > 0 || p4_mesh_override || p4_mesh_capture) && g->numVertices > 0 && g->numVertices <= 65535;
    unsigned char *mk_group = reshape ? malloc(g->numVertices) : 0, *mk_over = 0, *mk_next = 0;
    int mk_over_size = 0;
    MkBuf mk_cap = { 0, 0, 0 };
    if (mk_group) {
        for (int j = 0; j < g->numVertices; j++) {
            unsigned char bone[4];
            float weight[4];
            int n = vertex_bones(skin, nbones, j, bone, weight), best = 0;
            for (int w = 1; w < n; w++)
                if (weight[w] > weight[best])
                    best = w;
            mk_group[j] = n ? bone[best] : 0xff;
        }
        if (p4_mesh_override && (mk_over = mk_read_override(mk_key_g, header->numMeshes, &mk_over_size)))
            mk_next = mk_over + 16, mk_override_hits++;
        if (p4_mesh_capture) {
            unsigned head[5] = { 0x4b4d3450u, 1, mk_key_g, (unsigned)g->numVertices, (unsigned)header->numMeshes }; /* 'P4MK' */
            mk_put(&mk_cap, head, sizeof head);
            mk_put(&mk_cap, verts, g->numVertices * sizeof *verts);
            mk_put(&mk_cap, mk_group, g->numVertices);
        }
    } else
        reshape = 0;
    for (int m = 0; m < header->numMeshes && ok; m++) {
        const unsigned short *idx = mesh[m].indices;
        int triangles = strip ? mesh[m].numIndices - 2 : mesh[m].numIndices / 3;
        if (triangles < 0)
            triangles = 0;
        int cap = triangles > 0 ? triangles : 1, corners = 0, bones = 0, ntri = 0;
        int *tri = malloc(cap * 3 * sizeof *tri), *corner = malloc(cap * 3 * sizeof *corner);
        unsigned char (*set)[13] = malloc(cap * sizeof *set), *done = malloc(cap);
        unsigned char palette[SKIN_PALETTE];
        if (!tri || !corner || !set || !done) {
            free(tri), free(corner), free(set), free(done);
            ok = 0;
            break;
        }
        /* the mesh's triangles (strips unrolled, degenerate ones left out) and the bones of each */
        for (int t = 0; t < triangles; t++) {
            int *c = corner + ntri * 3;
            if (strip) { /* odd strip triangles are wound the other way */
                c[0] = idx[t + (t & 1)]; c[1] = idx[t + 1 - (t & 1)]; c[2] = idx[t + 2];
                if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                    continue;
            } else {
                c[0] = idx[t * 3]; c[1] = idx[t * 3 + 1]; c[2] = idx[t * 3 + 2];
            }
            set[ntri][0] = 0;
            for (int k = 0; k < 3; k++) {
                unsigned char bone[4];
                float weight[4];
                int n = vertex_bones(skin, nbones, c[k], bone, weight);
                for (int w = 0; w < n; w++) {
                    int have = 0;
                    for (int s = 1; s <= set[ntri][0] && !have; s++)
                        have = set[ntri][s] == bone[w];
                    if (!have)
                        set[ntri][++set[ntri][0]] = bone[w];
                }
            }
            if (set[ntri][0] > SKIN_PALETTE) /* one triangle with more bones than the GE has: the CPU keeps it */
                ok = 0;
            ntri++;
        }
        if (reshape && ok) { /* b211: capture, then the override's or the simplified triangles */
            unsigned short *list = malloc((ntri ? ntri : 1) * 3 * sizeof *list);
            int n = ntri;
            for (int k = 0; list && k < ntri * 3; k++)
                list[k] = corner[k];
            if (list && mk_cap.cap >= 0 && p4_mesh_capture) {
                unsigned count = ntri;
                mk_put(&mk_cap, &count, 4);
                mk_put(&mk_cap, list, ntri * 3 * sizeof *list);
            }
            if (list && mk_next) {
                unsigned count = 0, fits;
                if (mk_next + 4 <= mk_over + mk_over_size)
                    memcpy(&count, mk_next, 4);
                fits = count <= (unsigned)ntri && mk_next + 4 + count * 6 <= mk_over + mk_over_size;
                for (unsigned k = 0; fits && k < count * 3; k++) {
                    unsigned short x;
                    memcpy(&x, mk_next + 4 + k * 2, 2);
                    fits = x < g->numVertices;
                }
                if (fits) {
                    memcpy(list, mk_next + 4, count * 6);
                    n = count, mk_override_meshes++;
                    mk_next += 4 + count * 6;
                } else
                    mk_next = 0; /* not this geometry's file after all: the rest of it is ignored */
            } else if (list && skin_simplify_now > 0 && skin_simplify_now < 100 && ntri >= 8) {
                unsigned t0 = sceKernelGetSystemTimeLow();
                n = mk_simplify((const float (*)[3])verts, g->numVertices, 0, mk_group, list, ntri, ntri * skin_simplify_now / 100, INFINITY, 1 << 30, 0);
                mk_us += sceKernelGetSystemTimeLow() - t0;
                mk_meshes++, mk_tri_in += ntri, mk_tri_out += n;
            }
            if (list && n != ntri) {
                for (int k = 0; k < n * 3; k++)
                    corner[k] = list[k];
                ntri = n;
                for (int t = 0; t < ntri; t++) {
                    mk_tri_bones(skin, nbones, corner + t * 3, set[t]);
                    if (set[t][0] > SKIN_PALETTE)
                        ok = 0;
                }
            }
            free(list);
        }
        int regroup = ok && skin_mesh_regroup(&mesh[m], g, corner, ntri);
        if (ok && p4_skin_strip && p4_strip_why && ntri >= 2 && weld) { /* b210 strip diagnosis (b213: on request) */
            unsigned short *list = malloc(ntri * 3 * sizeof *list), *out = malloc((6 * ntri + 6) * sizeof *out);
            if (list && out) {
                if (strip)
                    sd_src_idx += mesh[m].numIndices, sd_src_tri += ntri;
                for (int k = 0; k < ntri * 3; k++)
                    list[k] = corner[k];
                sd_whole_idx += skin_stripify(list, ntri * 3, out, 0);
                for (int k = 0; k < ntri * 3; k++)
                    list[k] = weld[corner[k]];
                sd_weld_idx += skin_stripify(list, ntri * 3, out, 0);
                sd_tri += ntri;
            }
            free(list), free(out);
        }
        memset(done, 0, cap);
        if (regroup && p4_skin_rigid > 0) { /* b207: one-bone batches first */
            unsigned short count[256] = { 0 };
            for (int t = 0; t < ntri; t++)
                if (set[t][0] == 1)
                    count[set[t][1]]++;
            for (int t = 0; t < ntri && ok; t++) {
                if (done[t] || set[t][0] != 1 || count[set[t][1]] < p4_skin_rigid)
                    continue;
                int bone = set[t][1];
                count[bone] = 0;
                corners = 0;
                for (int u = t; u < ntri; u++)
                    if (!done[u] && set[u][0] == 1 && set[u][1] == bone) {
                        done[u] = 1;
                        memcpy(tri + corners, corner + u * 3, 3 * sizeof *tri);
                        corners += 3;
                    }
                palette[0] = bone;
                if (!skin_batch(e, m, palette, 1, tri, corners, g, skin, nbones))
                    ok = 0;
                held[0] = bone;
                skin_rigid_batches++, skin_rigid_tris += corners / 3;
            }
        }
        /* a batch takes the triangles in order while their bones fit; with regroup (first-fit) it also takes any later
           triangle of the mesh whose bones still fit, so the triangle order changes (opaque meshes only) */
        for (int first = 0; first < ntri && ok; first++) {
            if (done[first])
                continue;
            bones = corners = 0;
            for (int t = first; t < ntri; t++) {
                if (done[t])
                    continue;
                unsigned char extra[12];
                int add = 0;
                for (int k = 1; k <= set[t][0]; k++) {
                    int have = 0;
                    for (int s = 0; s < bones && !have; s++)
                        have = palette[s] == set[t][k];
                    for (int s = 0; s < add && !have; s++)
                        have = extra[s] == set[t][k];
                    if (!have)
                        extra[add++] = set[t][k];
                }
                if (bones + add > SKIN_PALETTE || (bones > 0 && bones + add > p4_skin_palette)) {
                    if (regroup)
                        continue;
                    break;
                }
                memcpy(palette + bones, extra, add);
                bones += add;
                done[t] = 1;
                memcpy(tri + corners, corner + t * 3, 3 * sizeof *tri);
                corners += 3;
            }
            if (p4_skin_sticky && bones > 1) { /* bones already held in a slot below the batch's count keep it */
                unsigned char sorted[SKIN_PALETTE], taken[SKIN_PALETTE] = { 0 }, placed[SKIN_PALETTE] = { 0 };
                for (int k = 0; k < bones; k++)
                    for (int s = 0; s < bones; s++)
                        if (held[s] == palette[k] && !taken[s]) {
                            sorted[s] = palette[k];
                            taken[s] = placed[k] = 1;
                            break;
                        }
                for (int k = 0, s = 0; k < bones; k++) {
                    if (placed[k])
                        continue;
                    while (taken[s])
                        s++;
                    sorted[s] = palette[k];
                    taken[s] = 1;
                }
                memcpy(palette, sorted, bones);
            }
            if (corners && !skin_batch(e, m, palette, bones > 0 ? bones : 1, tri, corners, g, skin, nbones))
                ok = 0;
            memcpy(held, palette, bones);
            skin_regrouped += regroup;
        }
        free(tri), free(corner), free(set), free(done);
    }
    free(weld);
    if (mk_cap.data && p4_mesh_capture) {
        mk_write_file("capture", mk_key_g, &mk_cap);
        mk_captured++;
    }
    free(mk_cap.data), free(mk_over), free(mk_group);
    if (!ok) /* packed stays 0: no batches */
        skin_free(e), e->g = g, e->verts = verts, e->vertices = g->numVertices, e->lit = lit, e->sample = sample, e->frame = cache_frame;
    else if (p4_batch_stats)
        batch_regroup(e, g, skin, nbones);
    unsigned build_us = sceKernelGetSystemTimeLow() - build_t0;
    skin_build_us += build_us;
    if (build_us > skin_build_max_us)
        skin_build_max_us = build_us, skin_build_max_v = g->numVertices;
    return e->batches ? e : 0; /* a failed build stays as an empty entry: the CPU path, without retrying every frame */
}

/* One batch skinned on the CPU from its own GE vertices (u8 weights / 128 over the palette, bind pose), into the
   frame pool in the space the GE batches use, then cut against the guard band like a static mesh. Unlit: the
   material colour goes into the vertices, as on the CPU path; lit: the GE lights it (draw_mesh_vertices). The batch's
   alpha bounds (draw_skin_alpha) still hold: skinning moves vertices, not colours. */
static unsigned char bone_parked[MAX_BONES]; /* b180: this frame's bones with an insane matrix (draw_skinned_ge) */
static int bones_parked;
static void skin_batch_cpu(const SkinBatch *b, int lit, const float *pack, const Material *material, int modulate, const SceneLight *light, const Matrix *space, float near)
{
    SkinLayout l = skin_layout(b->bones, lit, pack != 0);
    int stride = l.stride, n = b->vertices, out_stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    int units = (n * out_stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
    if (n > MAX_GUARDED || vertex_used + units > MAX_VERTICES) {
        p4_log("TRACE vertex pool full\n");
        return;
    }
    char *out = (char *)(vertex_pool + vertex_used);
    vertex_used += units;
    unsigned c = modulate ? material->colour : 0xffffffff;
    for (int j = 0; j < n; j++) {
        const unsigned char *in = b->v + stride * j;
        V3 bind, normal = { 0, 0, 0 }, p = { 0, 0, 0 }, q = { 0, 0, 0 };
        float uv[2];
        unsigned colour;
        skin_vertex_read(in, &l, pack, b->uvs, &bind, uv);
        memcpy(&colour, in + l.colour, 4);
        if (lit)
            normal = (V3){ (signed char)in[l.normal], (signed char)in[l.normal + 1], (signed char)in[l.normal + 2] };
        for (int s = 0; s < b->bones; s++) {
            if (!in[s])
                continue;
            float w = in[s] * (1.0f / 128);
            const Matrix *m = &bones[b->palette[s]];
            V3 t = transform(&bind, m);
            p.x += t.x * w, p.y += t.y * w, p.z += t.z * w;
            if (lit) {
                V3 r = rotate(&normal, m);
                q.x += r.x * w, q.y += r.y * w, q.z += r.z * w;
            }
        }
        if (lit) {
            float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z), inv = len > 0 ? 1.0f / len : 0;
            ((Vertex3DN *)out)[j] = (Vertex3DN){ uv[0], uv[1], colour, normal_s8(q.x * inv), normal_s8(q.y * inv), normal_s8(q.z * inv), 0, p.x, p.y, p.z };
        } else {
            ((Vertex3D *)out)[j] = (Vertex3D){ uv[0], uv[1], modulate_colour(colour, c), p.x, p.y, p.z };
        }
    }
    sceKernelDcacheWritebackRange(out, n * out_stride);
    if (!lit)
        st_enable(GU_LIGHTING, 0); /* the GE batches before left it on */
    skin_cpu_batches++;
    const unsigned short *idx = b->idx;
    int indices = b->indices;
    if (bones_parked) { /* leave out the triangles with a corner on a parked bone */
        int parked = 0;
        for (int s = 0; s < b->bones; s++)
            parked |= bone_parked[b->palette[s]];
        if (parked && index_used + b->indices <= MAX_INDICES) {
            unsigned short *kept = index_pool + index_used; /* the GE reads it after this returns: the frame's pool */
            int k = 0;
            for (int t = 0; t + 2 < b->indices; t += 3) {
                int drop = 0;
                for (int c = 0; c < 3 && !drop; c++) {
                    const unsigned char *in = b->v + stride * b->idx[t + c];
                    for (int s = 0; s < b->bones; s++)
                        drop |= in[s] && bone_parked[b->palette[s]];
                }
                if (!drop)
                    kept[k] = b->idx[t], kept[k + 1] = b->idx[t + 1], kept[k + 2] = b->idx[t + 2], k += 3;
            }
            index_used += k;
            sceKernelDcacheWritebackRange(kept, k * 2);
            idx = kept, indices = k;
        }
    }
    if (!indices)
        return;
    draw_guarded_compact(GU_TRIANGLES, idx, indices, out, n, lit ? VT_3D | GU_NORMAL_8BIT : VT_3D, space, near, material, modulate, light, 0, 0, 0, 0);
}

/* 1: drawn on the GE; 0: the caller skins on the CPU */
/* b178: a battle shows a large flat triangle from Chie's hand with GE skinning (also with p4_skin_split 2, the batch
   data skinned on the CPU), none with p4_ge_skin 0. Every 32 frames each batch vertex is skinned from its u8 weights
   and palette and compared with the CPU path's sum over the RenderWare weights; differences over 4 units logged. */
int p4_skin_diff = 0; /* 1: compare GE batch vertices with the CPU sum every 128 frames (b179 diagnostic) */
static unsigned skin_diff_logged, skin_diff_count, skin_diff_checked;
static void skin_diff(const Geometry *g, const Skin *skin, const SkinGeometry *e, int nbones)
{
    const V3 *verts = g->morphTarget->verts;
    for (int bi = 0; bi < e->batches; bi++) {
        const SkinBatch *b = &e->batch[bi];
        if (!b->src)
            continue;
        SkinLayout l = skin_layout(b->bones, e->lit, e->packed);
        float step = e->packed ? e->pack[3] * (1.0f / 32768) : 0, moved = 3 * step * step + 1e-6f; /* s16 rounding */
        for (int vi = 0; vi < b->vertices; vi++) {
            const unsigned char *in = b->v + l.stride * vi;
            int j = b->src[vi];
            V3 bind;
            float uv[2];
            skin_vertex_read(in, &l, e->packed ? e->pack : 0, b->uvs, &bind, uv);
            V3 ge = { 0, 0, 0 }, ref = { 0, 0, 0 };
            for (int s = 0; s < b->bones; s++) {
                if (!in[s])
                    continue;
                V3 p = transform(&bind, &bones[b->palette[s]]);
                float w = in[s] / 128.0f;
                ge.x += p.x * w, ge.y += p.y * w, ge.z += p.z * w;
            }
            if (skin->maxWeights == 1)
                ref = transform(&verts[j], &bones[skin->indices[j][0]]);
            else
                for (int w = 0; w < skin->maxWeights && w < 4 && skin->weights[j][w] > 0; w++) {
                    V3 p = transform(&verts[j], &bones[skin->indices[j][w]]);
                    float k = skin->weights[j][w];
                    ref.x += p.x * k, ref.y += p.y * k, ref.z += p.z * k;
                }
            skin_diff_checked++;
            float dx = ge.x - ref.x, dy = ge.y - ref.y, dz = ge.z - ref.z, bx = bind.x - verts[j].x, by = bind.y - verts[j].y, bz = bind.z - verts[j].z;
            float d2 = dx * dx + dy * dy + dz * dz, b2 = bx * bx + by * by + bz * bz;
            if (!(d2 <= 16) || !(b2 <= moved)) {
                skin_diff_count++;
                if (skin_diff_logged < 40) {
                    skin_diff_logged++;
                    const unsigned char *ix = skin->indices[j];
                    const float *wt = skin->weights[j];
                    p4_log("TRACE skin diff geometry %p v%d j %d batch %d bones %d idx %d w %d u8 %d %d ge %d %d %d ref %d %d %d bind_moved %d locked %x\n", g, g->numVertices, j, bi, b->bones, skin->indices[j][0], (int)(skin->weights[j][0] * 1000), in[0], b->bones > 1 ? in[1] : 0, (int)ge.x, (int)ge.y, (int)ge.z, (int)ref.x, (int)ref.y, (int)ref.z, !(b2 <= moved), g->locked);
                }
            }
        }
    }
}
/* b180: in a battle bone 38 of Chie's 1829-vertex model (vertices 583-716, weight 1) skinned to coordinates past 2^31
   on the CPU and the GE alike (p4_skin_diff: both INT_MAX) - by its look the game parks a hidden part of the model
   there. The CPU path's guard clipper drops those triangles; the GE drew them as large flat triangles from her hand
   and head. A bone whose matrix is not finite or is beyond 1e7 sends its batches through skin_batch_cpu. */
int p4_skin_finite = 1; /* 0: as b179 */
static int matrix_sane(const Matrix *m)
{
    const float *f = &m->right.x;
    for (int k = 0; k < 15; k++) {
        if ((k & 3) == 3)
            continue;
        if (!(f[k] < 1e7f && f[k] > -1e7f))
            return 0;
    }
    return 1;
}
static int skin_call_key(const SkinBatch *b, const Material *m, int modulate, const SceneLight *light, unsigned h[2])
{
    unsigned w[48];
    int n = p4_skin_call ? call_inputs(w, m, modulate, light) : -1;
    if (n < 0)
        return 0;
    w[n++] = (unsigned)b, w[n++] = (unsigned)b->v, w[n++] = (unsigned)b->idx, w[n++] = b->vtype, w[n++] = b->indices;
    /* b241: a recorded draw holds the strip's address and count. Without them in the key, a geometry rebuilt at the
       same batch, vertex and list addresses (free then malloc of the same sizes, as when a battle loads) replayed
       the old record with the address of a strip that had been freed. */
    w[n++] = (unsigned)b->strip, w[n++] = b->strip_count;
    call_key(w, n, h);
    return 1;
}

static int draw_skinned_ge(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, const Matrix *model, int lit, int modulate)
{
    prof_begin(PROF_SBONES);
    bone_check_geometry = g;
    int nbones = skin_bones(skin, hierarchy);
    skin_simplify_now = p4_skin_simplify;
    if (p4_skin_lod_depth > 0 && p4_skin_lod_keep > 0 && p4_skin_lod_keep < 100) {
        AnimState *as = anim_find(hierarchy); /* its depth is the last frame's (anim_seen_at, below) */
        if (as) {
            as->lod = as->depth > p4_skin_lod_depth || (as->lod && as->depth > p4_skin_lod_depth * 0.9f);
            if (as->lod)
                skin_simplify_now = p4_skin_lod_keep;
            skin_lod_draws += as->lod, skin_full_draws += !as->lod;
        }
    }
    SkinGeometry *e = nbones > 0 ? skin_geometry(g, skin, nbones, lit) : 0;
    prof_end(PROF_SBONES);
    if (!e || e->nradius < nbones)
        return 0;
    if (p4_bone_census)
        bone_census_note(hierarchy, e->used);
    /* A skinned vertex is a weighted mean of points within radius[i] of joint i; the guard band is convex, so it is
       inside when every joint's sphere is (one sphere round the whole skeleton crossed the near plane: b76 r 520). */
    const Matrix *space = (hierarchy->flags & HIERARCHY_LOCAL_SPACE) ? model : shadow_shear_m; /* world bones; b186 shadow: sheared */
    { /* b177 probe: the battle's character shadows (ATEST 0x7F06B) flatten the clump's frame before the hierarchy update */
        static unsigned shown;
        unsigned t = sky_state[3] & 0xfffff;
        if ((t == 0x7f06b || t == 0x717fb) && shown < 48 && !(cache_frame & 31)) {
            shown++;
            const Matrix *h0 = &hierarchy->matrices[0];
            p4_log("TRACE %s h %p flags %x bone0 up %d %d %d pos %d %d %d space %p up %d %d %d colour %08x\n", t == 0x7f06b ? "shadowpass" : "mainpass", hierarchy,
                   hierarchy->flags, (int)(h0->up.x * 1000), (int)(h0->up.y * 1000), (int)(h0->up.z * 1000), (int)h0->pos.x,
                   (int)h0->pos.y, (int)h0->pos.z, space, space ? (int)(space->up.x * 1000) : 0, space ? (int)(space->up.y * 1000) : 0,
                   space ? (int)(space->up.z * 1000) : 0, ((const Material *)0) ? 0 : 0);
        }
    }
    if (p4_skin_diff && !(cache_frame & 127))
        skin_diff(g, skin, e, nbones);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    Mat4 m;
    prof_begin(PROF_SGUARD);
    guard_matrix(m, space);
    {
        const Matrix *root = &hierarchy->matrices[0]; /* its clip w: how far the character is, for p4_anim_far */
        anim_seen_at(hierarchy, root->pos.x * m[0][3] + root->pos.y * m[1][3] + root->pos.z * m[2][3] + m[3][3], model);
    }
    float planes[5][4], plane_len[5];
    if (p4_skin_guard_fast)
        guard_sphere_planes(m, near, planes, plane_len);
    static unsigned char bone_out[MAX_BONES];
    int any_out = 0;
    bones_parked = 0;
    for (int i = 0; i < nbones; i++) {
        bone_out[i] = 0;
        bone_parked[i] = 0;
        if (p4_skin_finite && !matrix_sane(&bones[i])) {
            bone_parked[i] = 1;
            bones_parked = 1;
            bone_out[i] = 1; /* its vertices reach no sane place: the CPU batch path drops them, as the PS2's clipper did */
            any_out = 1;
            skin_nonfinite_bones++;
            continue;
        }
        if (e->radius[i] <= 0)
            continue;
        const Matrix *pose = &hierarchy->matrices[i];
        float scale = sqrtf(pose->right.x * pose->right.x + pose->right.y * pose->right.y + pose->right.z * pose->right.z);
        float sphere[4] = { pose->pos.x, pose->pos.y, pose->pos.z, e->radius[i] * (scale > 1 ? scale : 1) };
        int inside = p4_skin_guard_fast ? guard_sphere_inside_planes(planes, plane_len, sphere) : guard_sphere_inside(m, sphere, near);
        if (p4_skin_guard_fast && p4_skin_guard_check) {
            skin_guard_checks++;
            skin_guard_diffs += inside != guard_sphere_inside(m, sphere, near);
        }
        if (!inside && !p4_skin_split) {
            skin_ge_fallbacks++;
            prof_end(PROF_SGUARD);
            return 0;
        }
        bone_out[i] = !inside || p4_skin_split == 2; /* 2: every batch through skin_batch_cpu, to check it against the GE */
        any_out |= bone_out[i];
    }
    skin_ge_fallbacks += any_out;
    prof_end(PROF_SGUARD);
    prof_begin(PROF_SDRAW);
    begin_frame();
    SceneLight light = mesh_state(space, lit);
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int slot_bone[SKIN_PALETTE]; /* bones[] index in each GE bone slot; bones[] is this character's */
    for (int k = 0; k < SKIN_PALETTE; k++)
        slot_bone[k] = -1;
    unsigned sent_before = bones_sent;
    for (int b = 0; b < e->batches; b++) {
        SkinBatch *batch = &e->batch[b];
        const Material *material = mesh[batch->mesh].material;
        int cpu = 0;
        for (int k = 0; k < batch->bones && any_out; k++)
            cpu |= bone_out[batch->palette[k]];
        prof_begin(PROF_SMAT);
        for (int k = 0; k < batch->bones && !cpu; k++) {
            const Matrix *bm = &bones[batch->palette[k]];
            if (p4_bone_direct && slot_bone[k] == batch->palette[k])
                continue;
            Matrix folded;
            if (e->packed) { /* p = centre + s q / 32768 (skin_layout): rotation x s, translation + rotation . centre */
                const float *c = e->pack, s = e->pack[3];
                folded = *bm;
                folded.right = (V3){ bm->right.x * s, bm->right.y * s, bm->right.z * s };
                folded.up = (V3){ bm->up.x * s, bm->up.y * s, bm->up.z * s };
                folded.at = (V3){ bm->at.x * s, bm->at.y * s, bm->at.z * s };
                folded.pos = (V3){ bm->pos.x + c[0] * bm->right.x + c[1] * bm->up.x + c[2] * bm->at.x,
                                   bm->pos.y + c[0] * bm->right.y + c[1] * bm->up.y + c[2] * bm->at.y,
                                   bm->pos.z + c[0] * bm->right.z + c[1] * bm->up.z + c[2] * bm->at.z };
                bm = &folded;
            }
            if (p4_bone_direct) {
                /* BONE_MATRIX_NUMBER then 12 x BONE_MATRIX_DATA (float >> 8), as sceGuBoneMatrix sends them; b123
                   Heaven: sceGuBoneMatrix cost ~1450 cycles a matrix, 1.78 Mcycles a frame. A slot that already holds
                   this bone (same character, earlier batch) keeps it. */
                slot_bone[k] = batch->palette[k];
                const AliasWord *f = (const AliasWord *)bm;
                unsigned *c = gu_list->current;
                c[0] = 42u << 24 | (unsigned)(k * 12);
                for (int row = 0; row < 4; row++)
                    for (int col = 0; col < 3; col++)
                        c[1 + row * 3 + col] = 43u << 24 | f[row * 4 + col] >> 8;
                gu_list->current = c + 13;
                bones_sent++;
                continue;
            }
            ScePspFMatrix4 out = {
                { bm->right.x, bm->right.y, bm->right.z, 0 }, { bm->up.x, bm->up.y, bm->up.z, 0 },
                { bm->at.x, bm->at.y, bm->at.z, 0 }, { bm->pos.x, bm->pos.y, bm->pos.z, 1 },
            };
            sceGuBoneMatrix(k, &out);
            bones_sent++;
        }
        prof_end(PROF_SMAT);
        /* lighting is on for every batch: the lit alpha is the vertex alpha x the material's (bind_ge_light's ambient).
           Bounded like a mesh's, draw_tested skips the second TEST_1 pass when every pixel passes (b128 Heaven: all
           153 batches a frame drew twice). */
        int bounds[2];
        if (p4_skin_alpha && batch->alpha[1] >= batch->alpha[0]) {
            unsigned ma = modulate ? material->colour >> 24 : 255;
            bounds[0] = batch->alpha[0] * ma / 255;
            bounds[1] = (batch->alpha[1] * ma + 254) / 255;
            draw_alpha_known = 1;
            draw_skin_alpha = bounds;
        } else {
            draw_alpha_known = 0; /* the batch's colours are not scanned */
        }
        if (census3d_skip(material, batch->vtype, batch->indices, modulate))
            continue;
        prof_begin(PROF_SBIND);
        unsigned key[2];
        if (ge_work_on && !cpu)
            work_batches++, work_wreal += batch->wreal, work_wslotv += batch->bones * batch->vertices, work_rigid += batch->rigid, work_tris += batch->indices / 3;
        GeCall *slot = 0, *call = !cpu && skin_call_key(batch, material, modulate, &light, key) ? call_find(batch->call, key, &slot) : 0;
        if (call && call_replay(call, 0)) {
            prof_end(PROF_SBIND);
            draw_skin_alpha = 0;
            continue;
        }
        if (call) /* found but overwritten or no room to refresh: record it again */
            slot = call;
        else if (slot)
            call_misses[0]++;
        const unsigned *from = gu_list->current;
        void **texture = material->texture;
        bind_mask(material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        if (!cpu)
            bind_ge_light(material, modulate, &light);
        bind_texscale(e->packed && !cpu ? batch->uvs : 0); /* the CPU path draws float UVs */
        prof_end(PROF_SBIND);
        prof_begin(PROF_STEST);
        if (cpu)
            skin_batch_cpu(batch, e->lit, e->packed ? e->pack : 0, material, modulate, &light, space, near);
        else if (batch->strip)
            draw_tested(GU_TRIANGLE_STRIP, batch->vtype, batch->strip_count, batch->strip, batch->v);
        else
            draw_tested(GU_TRIANGLES, batch->vtype, batch->indices, batch->idx, batch->v);
        if (p4_skin_dummy && !cpu)
            *gu_list->current++ = 0xff000000u, skin_dummies++;
        if (slot)
            call_record(slot, key, from, 0);
        prof_end(PROF_STEST);
        draw_skin_alpha = 0;
    }
    bind_texscale(0); /* draws outside mesh mode take (1, 1, 0, 0) for granted */
    st_enable(GU_LIGHTING, 0);
    prof_end(PROF_SDRAW);
    if (p4_batch_stats) /* the shadow pass: sheared (b186/b192) or the battle shadow's ATEST */
        batch_stats_add(e, shadow_shear_m || (sky_state[3] & 0xfffff) == 0x7f06b, bones_sent - sent_before);
    skin_ge_draws++;
    skin_ge_batches += e->batches;
    return 1;
}

float __sqrt(float x)
{
    float root;
    __asm__ volatile("sqrt.s %0, %1" : "=f"(root) : "f"(x));
    return root;
}

/* Probe: RwCameraFrustumTestSphere, counted */
int func_003e8200(char *camera, const float *sphere)
{
    const float *plane = (const float *)(camera + 0x94);
    int inside = 2;
    frustum_tests++;
    for (int i = 0; i < 6; i++, plane += 5) {
        float distance = sphere[0] * plane[0] + sphere[1] * plane[1] + sphere[2] * plane[2] - plane[3];
        if (sphere[3] < distance) {
            frustum_out++;
            return 0;
        }
        if (-sphere[3] < distance)
            inside = 1;
    }
    return inside;
}

/* RpPTank: the atomic's plugin record (offset at gp b610) holds RpPTankData at +0x50; layout read off func_003a2770. */
static inline char *PTANK_OF_ATOMIC(void *atomic)
{
    int offset = G32(0xb610 - 0x10000);
    return offset > 0 ? *(char **)((char *)atomic + offset) : 0;
}
enum {
    PT_ACTIVE = 4, PT_CLUSTERS = 0x54, PT_USERDATA = 0x9c, PT_DATAFLAGS = 0xa8, PT_SRCBLEND = 0xac, PT_DSTBLEND = 0xb0,
    PT_CSIZE = 0xc0, PT_CROTATE = 0xc8, PT_CCOLOR = 0xcc, PT_CUV = 0xe0, PT_INSTFLAGS = 0x40,
    PD_POSITION = 1, PD_COLOR = 2, PD_SIZE = 4, PD_ROTATE = 0x20, PD_UV2 = 0x80, PD_CNSROTATE = 0x20000,
    /* cluster slots follow RPPTANKSIZE*, not the flag bits */
    PC_POSITION = 0, PC_SIZE = 3, PC_COLOR = 4, PC_ROTATE = 6, PC_UV2 = 7,
    PI_ALPHABLENDING = 0x10000000, PS2_ALPHA = 184, PS2_TEST = 200,
};
typedef struct { unsigned char *data; int stride; } Cluster;

/* RpPTank's PS2 register setter: which 1 writes ALPHA_1 and takes over from srcBlend/dstBlend, 2 the TEST low half. */
int func_003a2950(void *atomic, int which, unsigned value)
{
    char *tank = PTANK_OF_ATOMIC(atomic);
    char *ps2 = tank ? *(char **)(tank + PT_USERDATA) : 0;
    if (!ps2)
        return which == 1 || which == 2;
    if (which == 1) {
        *(unsigned *)(ps2 + PS2_ALPHA) = value & 0xff;
        *(unsigned *)(ps2 + PS2_ALPHA + 4) = value >> 24 & 0xff;
        *(unsigned *)(tank + PT_INSTFLAGS) &= ~PI_ALPHABLENDING;
        return 1;
    }
    if (which == 2) {
        unsigned *test = (unsigned *)(ps2 + PS2_TEST);
        *test = (*test & 0xffff0000) | (value & 0xffff);
        return 1;
    }
    return 0;
}

/* RwBlendFunction to a GE factor; in either slot 0/1 mean the other side's colour, as RenderWare's SRCCOLOR/DESTCOLOR do. */
static inline int in_ram(const void *p) { return (unsigned)p - 0x08800000u < 0x03800000u; }

static int gu_factor(int rw, unsigned *fix)
{
    switch (rw) {
    case 1: *fix = 0; return GU_FIX;
    case 2: *fix = 0xffffff; return GU_FIX;
    case 3: case 9: return GU_SRC_COLOR;
    case 4: case 10: return GU_ONE_MINUS_SRC_COLOR;
    case 6: return GU_ONE_MINUS_SRC_ALPHA;
    case 7: return GU_DST_ALPHA;
    case 8: return GU_ONE_MINUS_DST_ALPHA;
    default: return GU_SRC_ALPHA;
    }
}

/* ponytail: camera-facing quads built on the CPU, per-vertex colour/UV and matrix particles ignored; add them when a tank asks (the TRACE line says). */
static void draw_ptank_body(void *atomic, char *tank, Geometry *g, void *frame);
static void draw_ptank(void *atomic, char *tank, Geometry *g, void *frame)
{
    if (p4_probe_flags & 4)
        return;
    prof_begin(PROF_PTANK);
    int timed = ge_time_begin();
    draw_ptank_body(atomic, tank, g, frame);
    ge_time_end(timed, GT_PTANK);
    prof_end(PROF_PTANK);
}

static void draw_ptank_body(void *atomic, char *tank, Geometry *g, void *frame)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    int count = *(int *)(tank + PT_ACTIVE);
    unsigned flags = *(unsigned *)(tank + PT_DATAFLAGS);
    const Cluster *cluster = (const Cluster *)(tank + PT_CLUSTERS);
    static unsigned traced[8];
    for (int i = 0; i < 8; i++) {
        if (traced[i] == flags)
            break;
        if (!traced[i]) {
            traced[i] = flags;
            p4_log("TRACE ptank flags %x n%d blend %d/%d inst %x\n", flags, count, *(int *)(tank + PT_SRCBLEND), *(int *)(tank + PT_DSTBLEND), *(unsigned *)(tank + PT_INSTFLAGS));
            break;
        }
    }
    if (count <= 0 || !(flags & PD_POSITION) || !cluster[0].data || !in_ram(frame) || *(unsigned char *)frame != 0 /* rwFRAME */)
        return;
    if (vertex_used + count * CLIP_MAX_OUT > MAX_VERTICES) {
        p4_log("TRACE vertex pool full\n");
        return;
    }
    begin_frame();
    bind_depth();
    sceGuEnable(GU_CLIP_PLANES);
    Material **materials = g ? g->matList[0] : 0;
    Material *material = materials && (int)g->matList[1] > 0 ? materials[0] : 0;
    unsigned material_colour = material ? material->colour : 0xffffffff;
    bind_mask(material_colour >> 24); /* a stale stencil test from the last draw would hide every particle */
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_BLEND);
    char *ps2 = *(char **)(tank + PT_USERDATA);
    if (ps2 && !(*(unsigned *)(tank + PT_INSTFLAGS) & PI_ALPHABLENDING) && *(unsigned *)(ps2 + PS2_ALPHA))
        set_blend(*(unsigned *)(ps2 + PS2_ALPHA)); /* the game wrote ALPHA_1 itself, see func_003a2950 */
    else {
        unsigned src_fix = 0, dst_fix = 0;
        int src = gu_factor(*(int *)(tank + PT_SRCBLEND), &src_fix), dst = gu_factor(*(int *)(tank + PT_DSTBLEND), &dst_fix);
        sceGuBlendFunc(GU_ADD, src, dst, src_fix, dst_fix);
    }
    sceGuTexScale(1, 1);
    sceGuTexOffset(0, 0);
    set_camera();
    ScePspFMatrix4 identity;
    gumLoadIdentity(&identity);
    sceGuSetMatrix(GU_MODEL, &identity);
    void **texture = material ? material->texture : 0;
    bind_raster(texture ? *texture : 0, GU_CLAMP);

    /* World axes of the view's x and of screen up (the view's y runs down the screen). */
    const Matrix *view = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    V3 right = { view->right.x, view->up.x, view->at.x }, up = { -view->right.y, -view->up.y, -view->at.y };
    /* The view matrix carries the view window's scale; particle sizes are in world units. */
    float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z), ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
    if (rl > 0) { right.x /= rl; right.y /= rl; right.z /= rl; }
    if (ul > 0) { up.x /= ul; up.y /= ul; up.z /= ul; }
    const Matrix *model = p4r_003e9700(frame);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    static const float whole[4] = { 0, 0, 1, 1 };
    const float *cuv = (const float *)(tank + PT_CUV);
    if (!cuv[0] && !cuv[1] && !cuv[2] && !cuv[3])
        cuv = whole;
    Vertex3D *out = vertex_pool + vertex_used;
    for (int i = 0; i < count; i++) {
        V3 centre = transform((const V3 *)(cluster[PC_POSITION].data + i * cluster[PC_POSITION].stride), model);
        const float *size = flags & PD_SIZE ? (const float *)(cluster[PC_SIZE].data + i * cluster[PC_SIZE].stride) : (const float *)(tank + PT_CSIZE);
        unsigned colour = flags & PD_COLOR ? *(unsigned *)(cluster[PC_COLOR].data + i * cluster[PC_COLOR].stride) : *(unsigned *)(tank + PT_CCOLOR);
        float angle = flags & PD_ROTATE ? *(float *)(cluster[PC_ROTATE].data + i * cluster[PC_ROTATE].stride) : flags & PD_CNSROTATE ? *(float *)(tank + PT_CROTATE) : 0;
        const float *uv = flags & PD_UV2 ? (const float *)(cluster[PC_UV2].data + i * cluster[PC_UV2].stride) : cuv;
        unsigned tint = modulate_colour(colour, material_colour);
        float c = cosf(angle), s = sinf(angle), w = size[0] * 0.5f, h = size[1] * 0.5f;
        V3 x = { (right.x * c + up.x * s) * w, (right.y * c + up.y * s) * w, (right.z * c + up.z * s) * w };
        V3 y = { (up.x * c - right.x * s) * h, (up.y * c - right.y * s) * h, (up.z * c - right.z * s) * h };
        static const signed char corner[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } }; /* around the edge */
        Vertex3D quad[4];
        float depth[4];
        for (int k = 0; k < 4; k++) {
            int sx = corner[k][0], sy = corner[k][1];
            quad[k].u = sx < 0 ? uv[0] : uv[2];
            quad[k].v = sy > 0 ? uv[1] : uv[3];
            quad[k].colour = tint;
            quad[k].x = centre.x + x.x * sx + y.x * sy;
            quad[k].y = centre.y + x.y * sx + y.y * sy;
            quad[k].z = centre.z + x.z * sx + y.z * sy;
            depth[k] = quad[k].x * view->right.z + quad[k].y * view->up.z + quad[k].z * view->at.z + view->pos.z;
        }
        out += clip_quad_near(quad, depth, near, out);
    }
    Vertex3D *vertices = vertex_pool + vertex_used;
    int emitted = out - vertices;
    vertex_used += emitted;
    if (!emitted)
        return;
    sceKernelDcacheWritebackRange(vertices, emitted * sizeof *vertices);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, emitted, 0, vertices);
    ptanks_drawn++;
    particles_drawn += count;
}

/* Experiment: which atomics cover the most screen. cover = the bounding sphere's projected disc as a fraction of
   the view (1 = all of it); weighted by the draws the atomic issued, it ranks fill. Top 8 logged every 256 frames. */
typedef struct { const void *g; float cover; unsigned calls, vertices, frames, flags, nverts, meshes, ge_us, blend, test, tex, work[8]; char texture_name[32]; } CoverEntry;
static CoverEntry *cover_table; /* heap on first use (b206: 30 KB of .bss for a diagnostic) */
int p4_cover_stats = 0; /* 1: rank atomics by screen cover ("cover top"); p4_exp_ge_time and p4_probe_census turn it on too */
static float cover_total, cover_weighted;
/* Distance cull (experiment, 0 = off): atomics of at least p4_cull_min_vertices whose bounding sphere lies wholly
   beyond p4_cull_far in view depth are not drawn. The game's own frustum test keeps all it sees (b85: 0 of 190 meshes
   outside), so this trades distant detail (Heaven's 13 statues, v1180) for time. */
int p4_cull_far = 0, p4_cull_min_vertices = 500;
static unsigned cull_far_atomics;
static int atomic_far(const Geometry *g, const Matrix *model)
{
    const float *s = g->morphTarget->sphere;
    const Matrix *v = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    float wx = s[0] * model->right.x + s[1] * model->up.x + s[2] * model->at.x + model->pos.x;
    float wy = s[0] * model->right.y + s[1] * model->up.y + s[2] * model->at.y + model->pos.y;
    float wz = s[0] * model->right.z + s[1] * model->up.z + s[2] * model->at.z + model->pos.z;
    float z = wx * v->right.z + wy * v->up.z + wz * v->at.z + v->pos.z;
    float scale = sqrtf(model->right.x * model->right.x + model->right.y * model->right.y + model->right.z * model->right.z);
    return z - s[3] * scale > p4_cull_far;
}

int p4_cull_cover = 300; /* b247: on at 0.3% of the view on trial (user, 2026-11-14: Margaret in Heaven appearing from the middle of the bridge is fine; other scenes are being looked at). Drops skinned models too. 0 if any scene loses something the player must see. */
static unsigned cull_cover_atomics;
static float atomic_cover(const Geometry *g, const Matrix *model)
{
    const float *s = g->morphTarget->sphere;
    const Matrix *v = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    float wx = s[0] * model->right.x + s[1] * model->up.x + s[2] * model->at.x + model->pos.x;
    float wy = s[0] * model->right.y + s[1] * model->up.y + s[2] * model->at.y + model->pos.y;
    float wz = s[0] * model->right.z + s[1] * model->up.z + s[2] * model->at.z + model->pos.z;
    float z = wx * v->right.z + wy * v->up.z + wz * v->at.z + v->pos.z;
    float scale = sqrtf(model->right.x * model->right.x + model->right.y * model->right.y + model->right.z * model->right.z);
    float sx = sqrtf(v->right.x * v->right.x + v->up.x * v->up.x + v->at.x * v->at.x);
    float r = s[3] * scale;
    if (z - r <= 1)
        return 1;
    float q = r * sx / z;
    float c = 3.14159f * q * q;
    return c > 1 ? 1 : c;
}
static void cover_note(const Geometry *g, float cover, unsigned calls, unsigned vertices, unsigned ge_us, const unsigned *work)
{
    if (!cover_table && !(cover_table = calloc(256, sizeof *cover_table)))
        return;
    cover_total += cover;
    cover_weighted += cover * calls;
    unsigned bucket = ((unsigned)g >> 4) * 2654435761u >> 24;
    CoverEntry *e = 0;
    for (unsigned probe = 0; probe < 256; probe++) {
        CoverEntry *candidate = &cover_table[(bucket + probe) & 255];
        if (!candidate->g || candidate->g == g) {
            e = candidate;
            break;
        }
    }
    if (!e)
        return;
    if (e->g != g) {
        memset(e, 0, sizeof *e);
        e->g = g;
        e->flags = g->flags, e->nverts = g->numVertices, e->meshes = g->mesh->numMeshes;
        Mesh *mesh = (Mesh *)((char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
        void **texture = mesh->material->texture;
        Raster *r = texture ? *texture : 0;
        e->tex = r ? r->width << 20 | r->height << 8 | r->depth : 0;
        if (texture) {
            memcpy(e->texture_name, (const char *)texture + 0x10, 31);
            e->texture_name[31] = 0;
        }
    }
    e->blend = sky_state[2], e->test = sky_state[3];
    e->cover += cover, e->calls += calls, e->vertices += vertices, e->frames++, e->ge_us += ge_us;
    for (int k = 0; k < 8; k++)
        e->work[k] += work[k];
}
static void cover_report(unsigned frames)
{
    static CoverEntry none[1];
    CoverEntry *table = cover_table ? cover_table : none;
    int entries = cover_table ? 256 : 1;
    if (p4_probe_census) {
        for (int i = 0; i < entries; i++) {
            const CoverEntry *e = &table[i];
            if (e->g && e->frames)
                p4_log("census geometry %x v%u flags %x meshes %u calls %u vertices %u tex %s %ux%ux%u\n",
                       (unsigned)e->g, e->nverts, e->flags, e->meshes, e->frames / frames, e->vertices / frames,
                       e->texture_name, e->tex >> 20, e->tex >> 8 & 0xfff, e->tex & 0xff);
        }
        p4_probe_census = 0;
    }
    p4_log("cull far %d min_vertices %d: %u atomics per frame\n", p4_cull_far, p4_cull_min_vertices, cull_far_atomics / frames);
    p4_log("cull cover %d: %u atomics per frame\n", p4_cull_cover, cull_cover_atomics / frames);
    cull_cover_atomics = 0;
    cull_far_atomics = 0;
    p4_log("cover per frame: atomics' sphere cover %d%% draw-weighted %d%% mesh draws %u vertices %u exp_scissor %d\n",
           (int)(cover_total * 100 / frames), (int)(cover_weighted * 100 / frames), mesh_draw_calls / frames, mesh_draw_vertices / frames, p4_exp_scissor);
    for (int k = 0; k < 8; k++) {
        CoverEntry *best = 0;
        for (int i = 0; i < entries; i++)
            if (table[i].g && table[i].frames && (!best || (p4_exp_ge_time ? table[i].ge_us > best->ge_us :
                table[i].cover * table[i].calls / table[i].frames > best->cover * best->calls / best->frames)))
                best = &table[i];
        if (!best)
            break;
        p4_log("cover top %d: geometry %x v%u f%x m%u drawn %u/%u frames cover %d%% draws %u vertices %u ge_us %u per frame tex %ux%ux%u blend %x test %x\n", k, (unsigned)best->g, best->nverts, best->flags,
               best->meshes, best->frames, frames, (int)(best->cover * 100 / best->frames), best->calls / best->frames, best->vertices / best->frames,
               best->ge_us / (ge_time_frames ? ge_time_frames : frames), best->tex >> 20, best->tex >> 8 & 0xfff, best->tex & 0xff, best->blend, best->test);
        const unsigned *w = best->work, n = best->frames;
        p4_log("cover top %d work per draw of it: idx %u uniq %u wslots %u; skin batches %u weights used %u of %u blended, rigid triangles %u of %u\n", k,
               w[0] / n, w[1] / n, w[2] / n, w[3] / n, w[4] / n, w[5] / n, w[6] / n, w[7] / n);
        best->frames = 0;
    }
    if (ge_time_frames)
        p4_log("ge time per frame us: atomics %u 2d %u ptank %u clear %u (%u frames)\n", ge_time_us[GT_ATOMIC] / ge_time_frames, ge_time_us[GT_2D] / ge_time_frames,
               ge_time_us[GT_PTANK] / ge_time_frames, ge_time_us[GT_CLEAR] / ge_time_frames, ge_time_frames);
    if (ge_time_frames && ge_empty_n)
        p4_log("ge time empty piece %u us, %u pieces per frame: ~%u us a frame is the timer\n", ge_empty_us / ge_empty_n,
               ge_pieces / ge_time_frames, ge_empty_us / ge_empty_n * (ge_pieces / ge_time_frames));
    ge_empty_us = ge_empty_n = ge_pieces = 0;
    if (ge_timeouts)
        p4_log("ge time TIMEOUTS %u last state %x marker %x\n", ge_timeouts, ge_last_state, ge_marker & 0xffffff);
    memset(ge_time_us, 0, sizeof ge_time_us);
    ge_time_frames = 0;
    if (cover_table)
        memset(cover_table, 0, 256 * sizeof *cover_table);
    cover_total = cover_weighted = 0;
    mesh_draw_calls = mesh_draw_vertices = 0;
}

/* Independent proxy pass, after the geometry/skin helpers it shares. */
#include "shadow_capsules.h"

/* AtomicDefaultRenderCallBack */
static void *atomic_render_body(void *atomic);
static void *atomic_draw(void *atomic, Geometry *g, char *frame, const Matrix *model, const V3 *normals);
void *func_003be9f0(void *atomic)
{
    if (SKIPPING)
        return atomic;
    prof_begin(PROF_ATOMIC);
    void *r = atomic_render_body(atomic);
    prof_end(PROF_ATOMIC);
    return r;
}

static void *atomic_render_body(void *atomic)
{
    Geometry *g = *(Geometry **)((char *)atomic + ATOMIC_GEOMETRY);
    char *frame = *(char **)((char *)atomic + ATOMIC_FRAME);
    char *tank = PTANK_OF_ATOMIC(atomic);
    rtt_atomics += rtt_target != 0;
    if (tank && frame && current_camera) {
        draw_ptank(atomic, tank, g, frame);
        return atomic;
    }
    if (!g || !frame || !current_camera || (g->flags & GEOMETRY_NATIVE) || !g->mesh || !g->numVertices || !g->morphTarget || !g->morphTarget->verts) {
        atomics_skipped++;
        return atomic;
    }
    if (p4_shadow_capsule && shadow_shear_m && capsule_shadow(atomic, g, frame))
        return atomic;
    if ((p4_probe_flags & 1) || (unsigned)g == p4_probe_geometry || (unsigned)g == p4_probe_geometry2 ||
        (unsigned)g == p4_probe_geometry3 || (unsigned)g == p4_probe_geometry4) {
        if (probe_select_frame != cache_frame || probe_last_geometry != p4_probe_geometry) {
            probe_select_frame = cache_frame;
            probe_last_geometry = p4_probe_geometry;
            probe_instance = 0;
        }
        probe_selected++;
        unsigned instance = probe_instance++;
        if ((p4_probe_flags & 1) || p4_probe_keep_every <= 0 || instance % p4_probe_keep_every) {
            probe_hidden++;
            return atomic;
        }
    }
    if ((p4_probe_flags & 0x10) && p4_probe_keep_every > 1) { /* every atomic, not one geometry: 2 keeps half */
        static unsigned thin_frame, thin_count;
        if (thin_frame != cache_frame)
            thin_frame = cache_frame, thin_count = 0;
        if (thin_count++ % p4_probe_keep_every) {
            probe_hidden++;
            return atomic;
        }
    }
    atomics_drawn++;
    if (trace_atomics > 0) {
        const Matrix *m = p4r_003e9700(frame);
        const Skin *sk = SKIN_OF_GEOMETRY(g);
        const Hierarchy *hi = HIERARCHY_OF_ATOMIC(atomic);
        p4_log("TRACE atom v%d f%x m%d morph%d skin%d/%d w%d hier%d/%x pos %d %d %d cb %x\n", g->numVertices, g->flags, g->mesh->numMeshes, g->numMorphTargets,
               sk ? sk->numBones : -1, sk ? sk->numBoneIds : -1, sk ? sk->maxWeights : -1, hi ? hi->numNodes : -1, hi ? hi->flags : 0, (int)m->pos.x, (int)m->pos.y, (int)m->pos.z, *(unsigned *)((char *)atomic + 0x48));
        trace_atomics--;
    }
    const V3 *normals = 0;
    /* shortcut: fill missing lighting on zero-prelit meshes; match other dynamic pipelines before changing their baked colours. */
    if ((g->flags & 0x20) && g->preLit && g->morphTarget->normals) {
        prof_begin(PROF_ABLACK);
        int black = 1;
        for (int i = 0; i < g->numVertices; i++)
            if (g->preLit[i] & 0xffffff) { black = 0; break; }
        if (black || p4_light_prelit) normals = g->morphTarget->normals;
        prof_end(PROF_ABLACK);
    }
    /* Lit, untextured, no prelight (the limousine floor, f73): shown at full material colour it was a bright cyan
       panel. Textured unprelit meshes (characters, f77) stay as they are until their own pipeline is matched. */
    if (p4_light_unprelit && (g->flags & 0x2c) == 0x20 && g->morphTarget->normals)
        normals = g->morphTarget->normals;
    if (p4_probe_flags & 8)
        normals = 0;
    prof_begin(PROF_ALTM);
    const Matrix *model = p4r_003e9700(frame); /* RwFrameGetLTM: the matrix in the frame is out of date after a move */
    static Matrix sheared __attribute__((aligned(16)));
    if (shadow_shear_m) { /* b186: a shadow drawn from the body's frame */
        multiply(&sheared, model, shadow_shear_m);
        model = &sheared;
    }
    prof_end(PROF_ALTM);
    if (p4_cull_far > 0 && g->numVertices >= p4_cull_min_vertices && atomic_far(g, model)) {
        cull_far_atomics++;
        return atomic;
    }
    /* b246 p4_cull_cover N (0 off): an atomic of at least p4_cull_min_vertices whose bounding sphere covers less than
       N / 100000 of the view is not drawn. By size on the screen, not by depth, so far backdrops that are large stay.
       b245 Heaven: dropping 16 far atomics by depth (three statues a few pixels tall behind the gate among them)
       took the frame from 66.7 to 50.0 ms. */
    if (p4_cull_cover > 0 && g->numVertices >= p4_cull_min_vertices && atomic_cover(g, model) * 100000.0f < (float)p4_cull_cover) {
        cull_cover_atomics++;
        return atomic;
    }
    /* the cover ranking is a diagnostic (b121 Heaven: 0.42 Mcycles a frame): only for the experiments that print it */
    int cover_on = p4_cover_stats || p4_exp_ge_time || p4_probe_census;
    prof_begin(PROF_ACOVER);
    float cover = cover_on ? atomic_cover(g, model) : 0;
    prof_end(PROF_ACOVER);
    unsigned calls_before = mesh_draw_calls, verts_before = mesh_draw_vertices;
    unsigned work_before[8] = { work_idx, work_uniq, work_wslots, work_batches, work_wreal, work_wslotv, work_rigid, work_tris };
    ge_work_on = cover_on;
    int timed = ge_time_begin();
    void *r = atomic_draw(atomic, g, frame, model, normals);
    unsigned ge_us = ge_time_end(timed, GT_ATOMIC);
    ge_work_on = 0;
    prof_begin(PROF_ACOVER);
    unsigned work[8] = { work_idx - work_before[0], work_uniq - work_before[1], work_wslots - work_before[2], work_batches - work_before[3],
                         work_wreal - work_before[4], work_wslotv - work_before[5], work_rigid - work_before[6], work_tris - work_before[7] };
    if (cover_on)
        cover_note(g, cover, mesh_draw_calls - calls_before, mesh_draw_vertices - verts_before, ge_us, work);
    prof_end(PROF_ACOVER);
    return r;
}

static void *atomic_draw(void *atomic, Geometry *g, char *frame, const Matrix *model, const V3 *normals)
{
    const V3 *positions = g->morphTarget->verts;
    churn_atomic_g = g, churn_atomic_locked = g->locked, churn_atomic_skinned = 0;
    const Skin *skin = SKIN_OF_GEOMETRY(g);
    const Hierarchy *hierarchy = HIERARCHY_OF_ATOMIC(atomic);
    if (skin && skin->skinToBone && hierarchy && hierarchy->matrices && !(hierarchy->flags & HIERARCHY_NO_MATRICES)) {
        if (p4_ge_skin && (!normals || g->morphTarget->normals)) {
            prof_begin(PROF_GESKIN);
            int done = draw_skinned_ge(g, skin, hierarchy, model, normals != 0, g->flags & 0x40);
            prof_end(PROF_GESKIN);
            if (done) {
                skinned_drawn++;
                return atomic;
            }
        }
        const V3 *skinned = skin_vertices(g, skin, hierarchy, 0);
        if (skinned) {
            positions = skinned;
            churn_atomic_skinned = 1;
            if (normals) normals = skin_vertices(g, skin, hierarchy, 1);
            skinned_drawn++;
            if (!(hierarchy->flags & HIERARCHY_LOCAL_SPACE))
                model = shadow_shear_m; /* the bones already stand in the world (b186: a shadow shears them) */
        }
    }
    /* the morph target's sphere is in the geometry's space; skinned vertices are not */
    draw_geometry = positions == g->morphTarget->verts ? g : 0; /* CPU-skinned arrays change without a lock */
    draw_meshes(g->mesh, positions, g->numTexCoordSets ? g->texCoords[0] : 0, g->preLit, g->numVertices, model, g->flags & 0x40, normals,
                positions == g->morphTarget->verts && p4_guard_sphere ? g->morphTarget->sphere : 0);
    draw_geometry = 0;
    churn_atomic_g = 0, churn_atomic_skinned = 0;
    return atomic;
}

/* RxPipelineExecute: the game's own atomic callbacks, such as toon outlines, come here after setting up their state. */
enum { OBJECT_ATOMIC = 1 };
/* b133: what the pipelines other than atomics are (Shuffle Time: 0 atomics, ~810 others a frame, no cards) */
typedef struct { const void *pipeline, *caller; unsigned calls, word0, word1; } OtherEntry;
static OtherEntry other_table[16];
static void other_note(const void *pipeline, const void *object, const void *caller)
{
    for (int i = 0; i < 16; i++) {
        OtherEntry *e = &other_table[i];
        if (!e->pipeline || (e->pipeline == pipeline && e->caller == caller)) {
            if (!e->pipeline)
                e->pipeline = pipeline, e->caller = caller, e->word0 = ((const unsigned *)object)[0], e->word1 = ((const unsigned *)object)[1];
            e->calls++;
            return;
        }
    }
}
static void other_report(unsigned frames)
{
    for (int i = 0; i < 16 && other_table[i].pipeline; i++)
        p4_log("other pipeline %p from %p calls %u per frame object %08x %08x\n", other_table[i].pipeline, other_table[i].caller,
               other_table[i].calls / frames, other_table[i].word0, other_table[i].word1);
    memset(other_table, 0, sizeof other_table);
}
/* RwIm3D: p4r_00410420 and p4r_00410520 (RwIm3DRenderPrimitive / RwIm3DRenderIndexedPrimitive) execute the render
   pipeline on the pool's _rwIm3DPoolStash (rw/p2/baim3d.h): flags, ltm, numVerts, objVerts, camVerts, devVerts,
   meshState, renderState, pipeline, primType, indices, numIndices. The vertices are sky2 RxObjSpace3DVertex: position,
   RGBA, normal, u, v (36 bytes). b133 Shuffle Time: 36 draws a frame, nothing drawn, the cards missing. */
extern char p4r_00410420[], p4r_004104d0[], p4r_00410520[], p4r_004106a0[];
static int im3d_caller(const char *ra)
{
    return (ra >= p4r_00410420 && ra < p4r_004104d0) || (ra >= p4r_00410520 && ra < p4r_004106a0);
}
typedef struct { float x, y, z; unsigned colour; float nx, ny, nz, u, v; } Im3DVertex;
/* RwIm3DTransform executes the transform pipeline on the pool (rwIm3DPool: u16 numElements, u16 pad, elements, stride,
   then the stash). That pipeline would fill stash.objVerts/numVerts; the port has none, so b135 drew nothing: the render
   call's stash had numVerts 0 (Shuffle Time: 4 vertices, stride 36, 6 indices a card). Keep what the transform got. */
static const unsigned char *im3d_elements;
static unsigned im3d_count, im3d_stride;
static void im3d_transform(const unsigned *pool)
{
    im3d_count = pool[0] & 0xffff;
    im3d_elements = (const unsigned char *)pool[1];
    im3d_stride = pool[2];
}
static void im3d_draw(const unsigned *stash)
{
    static const int prims[7] = { -1, GU_LINES, GU_LINE_STRIP, GU_TRIANGLES, GU_TRIANGLE_STRIP, GU_TRIANGLE_FAN, GU_POINTS };
    unsigned flags = stash[0], type = stash[9], nidx = stash[11], count = im3d_count;
    const Matrix *ltm = (const Matrix *)stash[1];
    const unsigned short *indices = (const unsigned short *)stash[10];
    if (!im3d_elements || !count || im3d_stride < sizeof(Im3DVertex) || type >= 7 || prims[type] < 0 || !current_camera) {
        static unsigned logged;
        if (logged++ < 8)
            p4_log("TRACE im3d skip camera %p count %u stride %u stash %08x %08x %08x %08x %08x %08x\n", current_camera, count,
                   im3d_stride, stash[0], stash[1], stash[2], stash[9], stash[10], stash[11]);
        return;
    }
    int n = indices && nidx ? (int)nidx : (int)count;
    begin_frame();
    mesh_state(ltm, 0);
    Raster *r = (flags & 1) ? (Raster *)render_state[1] : 0;
    bind_mask(255);
    bind_raster(r, GU_REPEAT);
    Vertex3D *out = sceGuGetMemory(count * sizeof *out);
    for (unsigned i = 0; i < count; i++) {
        const Im3DVertex *in = (const Im3DVertex *)(im3d_elements + i * im3d_stride);
        out[i].u = in->u, out[i].v = in->v;
        out[i].colour = in->colour;
        out[i].x = in->x, out[i].y = in->y, out[i].z = in->z;
    }
    unsigned short *idx = 0;
    if (indices && nidx) {
        idx = sceGuGetMemory((nidx * 2 + 3) & ~3u);
        memcpy(idx, indices, nidx * 2);
    }
    draw_prelit = 0, draw_alpha_known = 0, draw_material_alpha = 255;
    draw_tested(prims[type], idx ? VT_3D : VT_3D & ~GU_INDEX_BITS, n, idx, out);
    im3d_draws++;
    im3d_vertices += count;
}

void *func_004114d0(void *pipeline, void *object, int force)
{
    if (*(unsigned char *)object == OBJECT_ATOMIC)
        return func_003be9f0(object);
    if (p4_im3d && im3d_caller(__builtin_return_address(0))) {
        if (SKIPPING)
            return object;
        if ((const char *)__builtin_return_address(0) < p4r_004104d0)
            im3d_transform(object);
        else
            im3d_draw(object);
        return object;
    }
    pipeline_other++;
    other_note(pipeline, object, __builtin_return_address(0));
    return object;
}

typedef struct {
    int type;
    void *triangles;
    V3 *vertices;
    void *normals;
    float (*texCoords[8])[2];
    unsigned *preLit;
    void *repEntry;
    void *lists[4];
    float boxes[12];
    MeshHeader *mesh;
    void *pipeline;
    unsigned short windowBase, numVertices, numTriangles, pad;
} WorldSector;

/* _rpSectorDefaultRenderCallBack */
void *func_003c9640(WorldSector *sector)
{
    if (SKIPPING)
        return sector;
    sectors_drawn++;
    prof_begin(PROF_SECTOR);
    if (sector->numTriangles && sector->mesh && current_camera)
        draw_meshes(sector->mesh, sector->vertices, sector->texCoords[0], sector->preLit, sector->numVertices, 0, 0, 0, 0);
    prof_end(PROF_SECTOR);
    return sector;
}

/* rwDEVICESYSTEMSTART */
int func_003ff5b0(void)
{
    G32(0xb8a0 - 0x10000) = 0x88c9b0 + P4_K; /* the current mode, read by raster validation */
    return 1;
}

/* The display as a raster: effects read its size. */
void *RpSkyGetDisplayBufferRaster(void) { return &display_raster; }

/* Same walk as RenderWare's, but a constructor that fails says who it was. */
typedef struct Entry { int offset, size; unsigned id; void *cb[5]; void *(*construct)(void *, int, int); void *(*destruct)(void *, int, int); void *cb2[2]; struct Entry *next, *prev; } Entry;
void *func_003e3b70(void **registry, void *object)
{
    for (Entry *e = (Entry *)registry[4]; e; e = e->next)
        if (!e->construct(object, e->offset, e->size)) {
            p4_log("plugin %x at %x failed from %x\n", e->id, e->offset, (unsigned)e->construct);
            for (e = e->prev; e; e = e->prev)
                e->destruct(object, e->offset, e->size);
            return 0;
        }
    return registry;
}

/* _rwDMAOpenVIFPkt: packets for the GS that nothing here sends. Their writers go on filling this and nothing reads it. */
static unsigned char dma_sink[16 * 1024] __attribute__((aligned(16)));

int func_003f3eb0(unsigned mode, int quads)
{
    G32(0xb884 - 0x10000) = (int)dma_sink;
    return 1;
}

/* GS FRAME masks RGB for the title silhouettes; only their alpha and depth are written. */
void mdlEffectEmitMaskedTagPair(void) { pixel_mask = 0x00ffffff; }
void mdlEffectEmitTagPair(void) { pixel_mask = 0; }

/* Glyphs: the game writes them in PSMT4 swizzled order for the GS. The GE reads 4-bit rasters linearly, and the font
   bitmap already is: 32 rows of 16 bytes. */
enum { GLYPH_PALETTE = 8, GLYPH_TEXTURE = 0x14 }; /* offsets in the game's glyph cache entry */
unsigned char *func_003ec6a0(void *raster, int level, int mode);
void *func_003ec2a0(void *raster);
unsigned char *func_003ec3d0(void *raster, int mode);
void *func_003ec2e0(void *raster);

void frFontRasterUploadGlyph(unsigned char *entry, unsigned char *bitmap, int palette)
{
    void *raster = *(void **)(entry + GLYPH_TEXTURE);
    memcpy(func_003ec6a0(raster, 0, 9), bitmap, 16 * 32);
    func_003ec2a0(raster);
    memcpy(func_003ec3d0(raster, 1), (char *)(0x63B5F0 + P4_K) + palette * 0x40, 0x40);
    func_003ec2e0(raster);
    *(short *)(entry + GLYPH_PALETTE) = palette;
}

/* Full-screen movies own VRAM only after the last GE list has been presented.
 * Cache entries are disposable, but must not be freed while that list reads them. */
int p4_video_renderer_begin(void)
{
    if (in_frame) func_003f79b0(NULL, NULL, 0);
    unsigned begin = sceKernelGetSystemTimeLow();
    while (flip_thread >= 0 && (int)(shown_seq - ge_queued) < 0) {
        flip_wait(FLIP_SHOWN, 2000);
        if (sceKernelGetSystemTimeLow() - begin > 2000000) return -1;
    }
    gate_poll();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    guard_result_clear();
    unsigned released = cache_bytes + skin_ge_bytes;
    for (int i = 0; i < CACHE_SLOTS; i++) free(mesh_cache[i].v);
    memset(mesh_cache, 0, sizeof mesh_cache);
    memset(mesh_split_bits, 0, sizeof mesh_split_bits);
    cache_bytes = 0;
    if (skin_cache)
        for (int i = 0; i < SKIN_GEOMETRIES; i++) skin_free(&skin_cache[i]);
    p4_log("video renderer idle, released %u bytes of geometry caches\n", released);
    return 0;
}

void p4_video_renderer_end(void)
{
    skip_frame = skip_run = 0;
    skip_debt_us = 0;
    skip_last_us = sceKernelGetSystemTimeLow();
    feedback_captured = 0;
    texture_generation++;
    gu_dirty();
}
