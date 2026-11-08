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
#ifdef P4_HW_PROFILE
#include <pspdebug.h>
enum { PROF_FRAME, PROF_ATOMIC, PROF_MESH, PROF_GUARD, PROF_SKIN, PROF_PTANK, PROF_2D, PROF_SYNC, PROF_VBLANK, PROF_SETUP, PROF_SHADE, PROF_EMIT, PROF_SUBMIT, PROF_HIT, PROF_GDRAW, PROF_KEY, PROF_UVSCAN, PROF_LIGHTS, PROF_HEMPTY, PROF_HBIND, PROF_DLIGHT, PROF_DDRAW, PROF_GCLASS, PROF_GLOOP, PROF_GCLIP, PROF_GSUB,
      PROF_MFRUST, PROF_MSLOT, PROF_MCOPY, PROF_ALTM, PROF_ABLACK, PROF_ACOVER, PROF_GESKIN, PROF_SECTOR, PROF_TASKS,
      PROF_SBONES, PROF_SGUARD, PROF_SDRAW, PROF_SMAT, PROF_SBIND, PROF_STEST,
      PROF_QMODEL, PROF_QATTACH, PROF_QSORT, PROF_QEFFECT, PROF_QFLD, PROF_QPRIM,
      PROF_AFRAME, PROF_AATTACH, PROF_ALAYER, PROF_ACTRL, PROF_AFX, PROF_AMOTION,
      PROF_ABLEND, PROF_AREBASE, PROF_APOSE, PROF_KADD, PROF_KSET, PROF_KBLEND, PROF_KMAT, PROF_KANIM, PROF_KFRAME, PROF_SECTIONS };
enum { PC_COUNT = 5 };
/* counter words read per section edge: cpu cycles, stall, memory stall, imiss, dmiss (b73 read all 19, which itself
   cost a visible part of the frame at ~800 section edges a frame) */
static const unsigned char prof_index[PC_COUNT] = { 1, 2, 4, 13, 14 };
static const char *const prof_names[PROF_SECTIONS] = { "frame", "atomic", "mesh", "guard", "skin", "ptank", "2d", "gesync", "vblank", "msetup", "mshade", "memit", "msubmit", "mhit", "mgdraw", "mkey", "muvscan", "mlights", "hempty", "hbind", "dlight", "ddraw", "gclass", "gloop", "gclip", "gsub",
    "mfrust", "mslot", "mcopy", "altm", "ablack", "acover", "geskin", "sector", "tasks", "sbones", "sguard", "sdraw", "smat", "sbind", "stest",
    "qmodel", "qattach", "qsort", "qeffect", "qfld", "qprim",
    "aframe", "aattach", "alayer", "actrl", "afx", "amotion",
    "ablend", "arebase", "apose", "kadd", "kset", "kblend", "kmat", "kanim", "kframe" };
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
static unsigned prof_store_scratch[4096] __attribute__((aligned(64)));
static void prof_store_test(void);
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
    task_book(node, fn, cycles, render_mono() - render);
    hanim_scan_after(node->name, fn);
    return r;
}
void kwlnTaskRunList(void)
{
    prof_begin(PROF_TASKS);
    unsigned before = prof_state > 0 ? prof_regs[1 + prof_index[0]] : 0;
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
    if (prof_state > 0)
        task_list_cycles += prof_regs[1 + prof_index[0]] - before, task_lists++;
    prof_end(PROF_TASKS);
}
/* the 16 costliest update callbacks per frame: inclusive and self kcycles */
static void task_report(unsigned frames)
{
    p4_log("hwtask lists %u per frame cpu_k %u overflow %u\n", task_lists / frames, (unsigned)(task_list_cycles / frames / 1000), task_overflow);
    for (int k = 0; k < 16; k++) {
        TaskStat *best = 0;
        for (int i = 0; i < TASK_STATS; i++)
            if (task_stats[i].calls && (!best || task_stats[i].cycles > best->cycles))
                best = &task_stats[i];
        if (!best)
            break;
        p4_log("hwtask %2d fn %08x %-24s calls %u cpu_k %u self_k %u\n", k, best->fn, best->name, best->calls / frames,
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
    for (int k = 0; k < 16; k++) {
        OtStat *best = 0;
        for (int i = 0; i < OT_STATS; i++)
            if (ot_stats[i].calls && (!best || ot_stats[i].self > best->self))
                best = &ot_stats[i];
        if (!best)
            break;
        p4_log("hwot %2d key %08x calls %u cpu_k %u self_k %u\n", k, best->key, best->calls / frames,
               (unsigned)(best->cycles / frames / 1000), (unsigned)(best->self / frames / 1000));
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
int __wrap_func_003d5bc0(void *interpolator, float time) { prof_begin(PROF_KADD); int r = __real_func_003d5bc0(interpolator, time); prof_end(PROF_KADD); return r; }
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
/* census FIXES hooks in mdlAdvanceModel (b139): id 0..5 = aframe..amotion, 6..8 = ablend, arebase, apose (b141) */
void p4_sec(int id, int begin)
{
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
#endif
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
static int field_noise_active;
int FldNoiseUpdate(void *task) { field_noise_active = 1; return 0; }

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
static void skip_decide(int cap)
{
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
        cached[i] = i;
    unsigned t1 = prof_cycles();
    for (int i = 0; i < 4096; i++)
        uncached[i] = i;
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
static int ge_time_begin(void);
static unsigned ge_time_end(int started, int category);
extern int p4_exp_ge_time;
static unsigned ge_time_frames;
static unsigned kept_textures, kept_cameras, kept_lights, mesh_modes; /* the mesh state cache, logged */
static void cover_report(unsigned frames);
static void other_report(unsigned frames);
static void hanim_report(void);
extern int p4_frameskip_bones;
extern int p4_bone_check;
static unsigned bone_bad_count;
extern int p4_hanim_nan_fix;
static unsigned hanim_nan_fixed;
static unsigned hanim_nan_after_update;
static unsigned skipped_bone_updates;
static void census2d_report(unsigned frames);
int p4_im3d = 1; /* 0: RwIm3D not drawn, as before b134 */
static unsigned im3d_draws, im3d_vertices;
static void frustum_report(unsigned frames);
static void replay_report(unsigned frames);
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
extern int p4_ge_skin;
int p4_skin_alpha = 1; /* 0: skinned batches' alpha unknown, two TEST_1 passes, as b128 */
int p4_bone_direct = 1; /* 0: sceGuBoneMatrix for every slot of every batch, as b123 */
static unsigned bones_sent;
static unsigned skin_ge_draws, skin_ge_batches, skin_ge_fallbacks, skin_ge_builds, skin_ge_bytes;
int p4_ge_light = 1; /* 1: lit meshes the GE draws whole are lit by the GE; 0: shaded on the CPU, as b75 */
static unsigned work_shaded, work_ge_emitted, work_cpu_emitted, work_ge_draws;
enum { POOL_VERTICES = 128 * 1024 };
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
int p4_guard_box_vfpu = 1, p4_guard_box_check = 0; /* b144: chunk boxes classified on the VFPU; check: also on the FPU, compared */
static unsigned guard_box_checks, guard_box_diffs;
int p4_guard_near_ge = 0; /* 1 (experiment): a triangle with a corner behind the near plane goes to the GE uncut, to see
                             on the device whether the GE clips the near plane itself (GU_CLIP_PLANES) */
static unsigned guard_near_ge;
int p4_guard_full_cache = 1; /* 1: GE-lit arrays of guarded meshes kept between frames; 0: rebuilt per draw, as b90 */
static unsigned guard_full_hits, guard_full_builds;
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
    }
    return 1;
}

/* rwSTANDARDCAMERAENDUPDATE */
int func_003f73d0(void *out, void *camera) { return 1; }

/* A clear as 32-pixel-wide sprites: the GE fills one 8888 texture-cache-wide column at a time, a full-screen sprite
   walks across rows (PPSSPP docs, GE performance: "never clear using a single rectangle"). The colour carries the
   stencil in its alpha, as sceGuClear does for 8888. */
int p4_clear_stripes = 1; /* 0: one sceGuClear rectangle, as b76 */
static void clear_striped(int bits, unsigned colour, unsigned short depth)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    typedef struct { unsigned colour; short x, y, z, pad; } ClearVertex;
    enum { STRIPE = 32, COUNT = (SCREEN_W + STRIPE - 1) / STRIPE * 2 };
    ClearVertex *v = sceGuGetMemory(COUNT * sizeof *v);
    for (int i = 0; i < COUNT / 2; i++) {
        int x1 = (i + 1) * STRIPE < SCREEN_W ? (i + 1) * STRIPE : SCREEN_W;
        v[2 * i] = (ClearVertex){ colour, i * STRIPE, 0, depth, 0 };
        v[2 * i + 1] = (ClearVertex){ colour, x1, SCREEN_H, depth, 0 };
    }
    sceGuSendCommandi(211, (bits & 7) << 8 | 1); /* CLEARMODE on, with the buffers to write */
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, COUNT, 0, v);
    sceGuSendCommandi(211, 0);
}

/* rwSTANDARDCAMERACLEAR: colour is RwRGBA, R first like the GE. */
int func_003f7490(void *camera, unsigned *colour, int mask)
{
    if (SKIPPING)
        return 1;
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
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
    /* Removing the noise pass must not double the field's frame-based game logic. */
    unsigned elapsed = (unsigned)sceDisplayGetVcount() - last_vblank;
    int wait = field_noise_active && frames && elapsed < 2 ? 2 - elapsed : 1;
    if (pipe) /* the flip thread waits for the vblank; here only a frame faster than that waits */
        wait = (field_noise_active ? 2 : 1) - (int)elapsed;
    else if (!p4_vsync_late && frames && elapsed >= (field_noise_active ? 2u : 1u))
        wait = 0; /* already slower than the cap: show it now (may tear) rather than idle up to a vblank (b86: ~10 ms) */
    prof_begin(PROF_VBLANK);
    while (wait-- > 0)
        sceDisplayWaitVblankStart();
    prof_end(PROF_VBLANK);
    last_vblank = sceDisplayGetVcount();
    field_noise_active = 0;
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
        p4_log("frames %d atomics %d skipped %d ptanks %d/%d sectors %d skinned %d other %d tests %d out %d af48 %d\n", frames, atomics_drawn, atomics_skipped, ptanks_drawn, particles_drawn, sectors_drawn, skinned_drawn, pipeline_other, frustum_tests, frustum_out, G32(0xaf48 - 0x10000));
        p4_log("guard on %d meshes %d clipped %d dropped %d spheres %d vertices %d uvrebase %d uvmemo %d hits %u dstalpha_skipped %d\n", p4_guard_clip, guard_meshes, guard_clipped, guard_dropped, guard_spheres, guard_vertices, uv_rebased, p4_uv_memo, uv_memo_hits, dst_alpha_skipped);
        uv_memo_hits = 0;
        p4_log("guard near_ge %d triangles %u clip_fast %d checks %u diffs %u gnear %d offscreen %u\n", p4_guard_near_ge, guard_near_ge, p4_guard_clip_fast, guard_clip_checks, guard_clip_diffs, p4_guard_gnear, guard_offscreen);
        p4_log("guard boxes %d meshes whole %u chunks inside %u partial %u culled %u builds %u lazy vertices %u vfpu %d checks %u diffs %u\n", p4_guard_boxes, guard_box_whole, guard_box_inside, guard_box_partial, guard_box_culled, guard_box_builds, guard_lazy_vertices, p4_guard_box_vfpu, guard_box_checks, guard_box_diffs);
        guard_box_checks = guard_box_diffs = 0;
        guard_near_ge = guard_clip_checks = guard_clip_diffs = guard_offscreen = 0;
        guard_box_whole = guard_box_inside = guard_box_partial = guard_box_culled = guard_box_builds = guard_lazy_vertices = 0;
        struct mallinfo heap = mallinfo();
        /* b114 set the budget to what the cache held when the heap was short; during a load that was 0 and the cache
           stayed off for the session (Heaven 38 KB free while loading, 1.3 MB a few seconds later). Now the budget only
           follows the heap between the b72 4 MB and 6 MB, both ways. */
        unsigned room = cache_bytes + (heap.fordblks > CACHE_HEAP_RESERVE ? heap.fordblks - CACHE_HEAP_RESERVE : 0);
        unsigned want = room < CACHE_BUDGET_MIN ? CACHE_BUDGET_MIN : room > CACHE_BUDGET_MAX ? CACHE_BUDGET_MAX : room;
        if (want != p4_cache_budget) {
            p4_log("mesh cache budget %u -> %u: heap free %d, cache %u\n", p4_cache_budget, want, heap.fordblks, cache_bytes);
            p4_cache_budget = want;
        }
        p4_log("mesh cache on %d hits %u builds %u misses %u noslot %u bytes %u budget %u heap_free %d guarded_meshes %u\n", p4_mesh_cache, cache_hits, cache_builds, cache_misses, cache_noslot, cache_bytes, p4_cache_budget, heap.fordblks, guard_mesh_draws);
        p4_log("cache audit %d checks %u stale positions %u uvs %u prelight %u vertices %u\n", p4_cache_audit, audit_checks, audit_stale[0], audit_stale[1], audit_stale[2], audit_stale_vertices);
        audit_checks = audit_stale[0] = audit_stale[1] = audit_stale[2] = audit_stale_vertices = 0;
        guard_mesh_draws = 0;
        /* Work per frame, the same in PPSSPP and on the PSP (b69 on the PSP: shade_fast 238 cycles a vertex) */
        p4_log("work ge_light %d per frame: cpu_lit %u emitted_ge_lit %u emitted_cpu_lit %u ge_lit_draws %u\n", p4_ge_light, work_shaded / 256, work_ge_emitted / 256, work_cpu_emitted / 256, work_ge_draws / 256);
        work_shaded = work_ge_emitted = work_cpu_emitted = work_ge_draws = 0;
        void p4_vu0_report(unsigned frames);
        p4_vu0_report(256);
        p4_log("textures swizzled %u bytes %u mipmapped %u mip_bytes %u mipmaps %d\n", swizzled_textures, swizzled_bytes, mip_textures, mip_bytes, p4_mipmaps);
        churn_report(256);
        p4_log("skin ge %d draws %u batches %u bones sent %u direct %d fallbacks %u builds %u bytes %u\n", p4_ge_skin, skin_ge_draws, skin_ge_batches, bones_sent, p4_bone_direct, skin_ge_fallbacks, skin_ge_builds, skin_ge_bytes);
        skin_ge_draws = skin_ge_batches = skin_ge_fallbacks = skin_ge_builds = bones_sent = 0;
        p4_log("guard fast %d memo %d hits %u check runs %u diffs %u world %d checks %u unsafe %u stricter %u full_cache %d hits %u builds %u\n", p4_guard_fast, p4_guard_memo, guard_memo_hits, guard_check_runs, guard_check_diffs, p4_guard_world, guard_world_checks, guard_world_unsafe, guard_world_stricter, p4_guard_full_cache, guard_full_hits, guard_full_builds);
        guard_full_hits = guard_full_builds = 0;
        p4_log("gs test %d skip %d mesh_alpha %d: draws %u second %u opaque %u nodepth %u allfail %u scans %u\n", p4_gs_test, p4_gs_test_skip, p4_gs_mesh_alpha,
               tested_draws, tested_second, tested_opaque, tested_nodepth, tested_allfail, tested_scans);
        tested_draws = tested_second = tested_opaque = tested_nodepth = tested_allfail = tested_scans = 0;
        p4_log("state cache %d batch %d per frame: mesh modes %u kept textures %u cameras %u lights %u\n", p4_state_cache, p4_draw_batch,
               mesh_modes / 256, kept_textures / 256, kept_cameras / 256, kept_lights / 256);
        mesh_modes = kept_textures = kept_cameras = kept_lights = 0;
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
static void gu_draw(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    mesh_draw_calls++;
    mesh_draw_vertices += count;
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

static void bind_mask(unsigned alpha)
{
    st_pixel_mask(pixel_mask);
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
static void bind_raster(Raster *r, int wrap)
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

static void set_blend(unsigned alpha)
{
    switch (alpha & 0xff) {
    case BLEND_ADD: /* Cs * As + Cd */
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        break;
    case BLEND_SUBTRACT: /* Cd - Cs * As */
        sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0xffffff);
        break;
    case BLEND_REVERSE: /* Cs - Cd * As */
        sceGuBlendFunc(GU_SUBTRACT, GU_FIX, GU_SRC_ALPHA, 0xffffff, 0);
        break;
    case 0x52: /* Cd - Cs * Ad */
        sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_DST_ALPHA, GU_FIX, 0, 0xffffff);
        break;
    case 0x54: /* Cs * Ad + Cd * (1 - Ad) */
        sceGuBlendFunc(GU_ADD, GU_DST_ALPHA, GU_ONE_MINUS_DST_ALPHA, 0, 0);
        break;
    case 0x58: /* Cs * Ad + Cd */
        sceGuBlendFunc(GU_ADD, GU_DST_ALPHA, GU_FIX, 0, 0xffffff);
        break;
    default:
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    }
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
    out->x = in->x * (SCREEN_W / 640.0f);
    out->y = in->y * (SCREEN_H / 448.0f);
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
    int r = draw_body(prim, vertices, indices, count);
    ge_time_end(timed, GT_2D);
    prof_end(PROF_2D);
    return r;
}

/* b146 census of 2D draws (p4_probe_2d): per texture x blend x pixel mask, draws and the screen area of their bounding
   boxes (a proxy for fill). The title's logo phase: ~36 ms of GE in 2D a frame. */
int p4_probe_2d = 0;
typedef struct { const void *raster; unsigned blend, mask, prim, calls, w, h; float area; } Census2D;
static Census2D census2d[64];
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
}

static int draw_body(int prim, const Vertex2D *vertices, const unsigned short *indices, int count)
{
    if (p4_probe_2d && count > 0)
        census2d_note(prim, vertices, indices, count);
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
    if (p4_alpha_write_test && pixel_mask == 0x00ffffff && render_state[1] && (Raster *)render_state[1] != &display_raster) {
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
    if (!p4_dst_alpha_passes && (dst_alpha_pass || alpha_writer)) {
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
    if (draw_slot_v == vertices && draw_slot_alpha[0] >= 0) {
        *lo = draw_slot_alpha[0], *hi = draw_slot_alpha[1];
        return 1;
    }
    if (draw_fixed_alpha) { /* a subset (or cut pieces) of a cache entry whose bounds are known: they still bound it */
        *lo = draw_fixed_alpha[0], *hi = draw_fixed_alpha[1];
        return 1;
    }
    tested_scans++;
    int stride = vtype & GU_NORMAL_BITS ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    const unsigned short *ix = (vtype & GU_INDEX_BITS) ? indices : 0;
    unsigned m = 255, big = 0;
    for (int k = 0; k < count; k++) {
        unsigned a = *(const unsigned *)((const char *)vertices + (ix ? ix[k] : k) * stride + 8) >> 24; /* colour after u, v */
        if (a < m) m = a;
        if (a > big) big = a;
    }
    if (vtype & GU_NORMAL_BITS) { /* GE-lit: the vertex colour is the ambient term, the material alpha comes on top */
        m = m * draw_material_alpha / 255;
        big = (big * draw_material_alpha + 254) / 255;
    }
    if (draw_slot_v == vertices)
        draw_slot_alpha[0] = m, draw_slot_alpha[1] = big;
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
    int units = (triangles * GUARD_OUT_MAX * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
    if (index_used + triangles * 3 > MAX_INDICES || vertex_used + units > MAX_VERTICES) {
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
typedef struct { const void *slot; unsigned key, built; int n, cap, chunk; float *box; } GuardBoxes;
static GuardBoxes *guard_boxes;
static const float *guard_slot_boxes(const void *slot, unsigned key, unsigned built, int prim, const unsigned short *idx, int nidx,
                                     const void *vertices, int vtype, int *count)
{
    int chunk = guard_chunk_now();
    int tris = prim == GU_TRIANGLE_STRIP ? nidx - 2 : nidx / 3, n = (tris + chunk - 1) / chunk;
    if (tris <= 0 || n > GUARD_MAX_CHUNKS)
        return 0;
    if (!guard_boxes && !(guard_boxes = calloc(GUARD_BOX_SLOTS, sizeof *guard_boxes)))
        return 0;
    GuardBoxes *e = &guard_boxes[((unsigned)slot >> 4) * 2654435761u >> 24];
    *count = n;
    if (e->slot == slot && e->key == key && e->built == built && e->n == n && e->chunk == chunk && e->box)
        return e->box;
    if (e->cap < n) {
        free(e->box);
        e->box = malloc(n * 6 * sizeof(float));
        e->cap = e->box ? n : 0;
        if (!e->box) {
            e->slot = 0;
            return 0;
        }
    }
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
    }
    e->slot = slot, e->key = key, e->built = built, e->n = n, e->chunk = chunk;
    guard_box_builds++;
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

static void draw_guarded_compact(int prim, const unsigned short *idx, int nidx, const void *vertices, int nverts, int vtype,
                                 const Matrix *model, float near, const Material *material, int modulate, const SceneLight *light,
                                 const int *alpha, const void *slot, unsigned slot_key, unsigned slot_built)
{
    prof_begin(PROF_GDRAW);
    int lit = (vtype & GU_NORMAL_8BIT) != 0;
    unsigned any = 1;
    if (slot && p4_guard_boxes && nverts <= MAX_GUARDED &&
        (guard_stamp || (guard_stamp = calloc(MAX_GUARDED, sizeof *guard_stamp)))) {
        static unsigned char cls[GUARD_MAX_CHUNKS];
        int n = 0, partial = 0, culled = 0;
        prof_begin(PROF_GCLASS);
        const float *box = guard_slot_boxes(slot, slot_key, slot_built, prim, idx, nidx, vertices, vtype, &n);
        GuardLazy lazy;
        if (box) {
            guard_matrix_cached(lazy.m, model);
            guard_boxes_classify(box, n, (const float (*)[4])lazy.m, near, cls, &partial, &culled);
        }
        prof_end(PROF_GCLASS);
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
static void bind_ge_light(const Material *material, int modulate, const SceneLight *light)
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
static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices, const void *vertices, const Material *material, int modulate, const SceneLight *light)
{
    prof_begin(PROF_DLIGHT);
    if (vtype & GU_NORMAL_8BIT) {
        bind_ge_light(material, modulate, light);
        work_ge_draws++;
    }
    prof_end(PROF_DLIGHT);
    draw_material_alpha = modulate ? material->colour >> 24 : 255;
    prof_begin(PROF_DDRAW);
    draw_tested(prim, vtype, count, indices, vertices);
    prof_end(PROF_DDRAW);
    if (vtype & GU_NORMAL_8BIT)
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
int p4_cache_reuse_last = 1; /* 1: a miss may take its mesh's entry drawn last frame (pipeline off); 0: as b75 */

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
static void churn_why(int index, const unsigned part[6], int vertices)
{
    if (!churn_parts)
        return;
    int k = 0;
    while (k < 6 && churn_parts[index][k] == part[k])
        k++;
    churn_count[k]++;
    churn_vertices[k] += vertices;
}
static void churn_report(unsigned frames)
{
    for (int k = 0; k < 7; k++)
        if (churn_count[k])
            p4_log("churn %s: %u meshes %u vertices per frame\n", churn_names[k], churn_count[k] / frames, churn_vertices[k] / frames);
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
int p4_instance_replay = 1; /* 0: every copy through draw_meshes_body, as b93 */
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
static unsigned char gs_before[sizeof gs];
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
        SceneLight light = scene_lights();
        if (memcmp(&light, &replay.light, sizeof light))
            why = 4;
    }
    if (!why && !replay_unguarded(model, sphere))
        why = 5;
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
        if (cmd == 0x08 || cmd == 0x0a || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) /* JUMP, CALL, RET, END, FINISH: inline data or flow */
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
static void replay_report(unsigned frames)
{
    p4_log("instance replay %d per frame: draws %u records %u refused state %u material %u camera %u light %u guard %u\n", p4_instance_replay,
           replay_draws / frames, replay_records / frames, replay_refusals[1] / frames, replay_refusals[2] / frames, replay_refusals[3] / frames,
           replay_refusals[4] / frames, replay_refusals[5] / frames);
    replay_draws = replay_records = 0;
    memset(replay_refusals, 0, sizeof replay_refusals);
}
static void draw_meshes(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    prof_begin(PROF_MESH);
    if (p4_instance_replay && model && replay_try(header, position, uv, preLit, count, model, modulate, normals, sphere)) {
        prof_end(PROF_MESH);
        return;
    }
    unsigned *start = gu_list->current;
    int record = p4_instance_replay && model && gu_mesh_mode;
    if (record)
        memcpy(gs_before, &gs, sizeof gs);
    mesh_model_mark = 0;
    draw_meshes_body(header, position, uv, preLit, count, model, modulate, normals, sphere);
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
    if (!gu_mesh_mode || !gs.texscale_known) {
        sceGuTexScale(1, 1);
        sceGuTexOffset(0, 0);
        gs.texscale_known = gu_mesh_mode;
    }
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
    prof_begin(PROF_UVSCAN);
    if (uv && p4_uv_rebase && count > 0) {
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
        prof_begin(PROF_KEY);
        if (p4_mesh_cache) {
            unsigned h = 2166136261u;
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
            h = hash_words(h, &count, 4);
            float shift[2] = { du, dv };
            h = hash_words(h, shift, sizeof shift);
            part[4] = h;
            if (normals && ge_lit) {
                h = hash_words(h, &ge_lit, 4); /* the GE lights it: light and rotation are not in the vertices */
                h = hash_sample(h, normals, count, sizeof *normals);
            } else if (normals) {
                h = hash_words(h, &light, sizeof light);
                if (model)
                    h = hash_words(h, model, 12 * 4); /* rotation rows; position does not light */
                h = hash_sample(h, normals, count, sizeof *normals);
            }
            part[5] = h;
            geometry_key = h | 1;
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
                unsigned mk[3] = { material->colour, *(unsigned *)&material->ambient, *(unsigned *)&material->diffuse };
                key = hash_words(geometry_key, mk, sizeof mk) | 1;
                int hit;
                prof_begin(PROF_MSLOT);
                slot = cache_slot(mesh, key, &hit);
                if (!hit && slot && slot->mesh == mesh)
                    churn_why(slot - mesh_cache, part, count);
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
                    void **texture = material->texture;
                    prof_begin(PROF_HEMPTY); /* the profiler's own cost per section, to subtract */
                    prof_end(PROF_HEMPTY);
                    prof_begin(PROF_HBIND);
                    bind_mask(material->colour >> 24);
                    bind_raster(texture ? *texture : 0, GU_REPEAT);
                    prof_end(PROF_HBIND);
                    if (clip_this) {
                        draw_guarded_compact(prim, slot->idx, slot->indices, slot->v, slot->vertices, slot->vtype, model, near, material, modulate, &light, &slot->alpha_lo, slot, slot->key, slot->built);
                    } else {
                        draw_slot_v = slot->v, draw_slot_alpha = &slot->alpha_lo;
                        draw_mesh_vertices(prim, slot->vtype, slot->indices, slot->idx, slot->v, material, modulate, &light);
                        draw_slot_v = 0;
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
                draw_guarded(prim, mesh->indices, mesh->numIndices, full, ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D, near, material, modulate, &light);
                prof_end(PROF_GDRAW);
                continue;
            }
            int n = mesh->numIndices;
            int stride = ge_lit ? sizeof(Vertex3DN) : sizeof(Vertex3D), vtype = ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D;
            if (vertex_used + (n * stride + 23) / 24 > MAX_VERTICES || index_used + n > MAX_INDICES) {
                p4_log("TRACE vertex pool full\n");
                return;
            }
            unsigned colour = modulate ? material->colour : 0xffffffff;
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
                if (slot->v) {
                    memcpy(slot->v, out, used * stride);
                    slot->idx = (unsigned short *)((char *)slot->v + used * stride);
                    slot->vtype = vtype;
                    slot->alpha_lo = -1; /* draw_tested scans it again */
                    memcpy(slot->idx, remapped, n * 2);
                    sceKernelDcacheWritebackRange(slot->v, need);
                    slot->mesh = mesh;
                    slot->key = key;
                    slot->frame = cache_frame;
                    slot->built = cache_frame;
                    churn_remember(slot - mesh_cache, part);
                    if (p4_cache_audit && (audit_hash || (audit_hash = calloc(CACHE_SLOTS, sizeof *audit_hash))))
                        audit_inputs(audit_hash[slot - mesh_cache], position, uv, preLit, count);
                    slot->vertices = used;
                    slot->indices = n;
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
int p4_hanim_c = 0, p4_hanim_check = 0;
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
int func_00397c40(void *hierarchy)
{
    if (skip_frame && p4_frameskip_bones) {
        skipped_bone_updates++;
        return 1;
    }
    if (p4_hanim_nan_fix)
        hanim_sanitize(hierarchy);
    int r = p4r_00397c40(hierarchy);
    hanim_note(hierarchy);
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

int p4_hanim_update(unsigned *h)
{
    unsigned flags = h[0];
    Matrix ltm_parent, local_root;
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
    for (int i = 0; i < n; i++, m++, key += stride, info += 4) {
        Matrix local;
        if ((char *)key_to_matrix == func_003954b0)
            hanim_key_to_matrix(&local, key);
        else
            key_to_matrix(&local, key);
        RwMatrixMultiply(m, &local, parent);
        unsigned char *f = (unsigned char *)info[3];
        if (f) {
            if (flags & 0x1000) {
                memcpy(f + 16, &local, sizeof local);
                if (!update_ltm)
                    func_003e9680(f);
            }
            if (update_ltm) {
                if (flags & 0x4000)
                    RwMatrixMultiply(f + 80, m, &ltm_parent);
                else
                    memcpy(f + 80, m, sizeof *m);
                f[3] = (f[3] & ~4) | 8;
            }
        }
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
        original(h);
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
    return p4_hanim_c ? p4_hanim_update(h) : original(h);
}
static void hanim_report(void)
{
    p4_log("hanim c %d check %d: calls %u checks %u differing floats %u max diff %d/1000000\n", p4_hanim_c, p4_hanim_check, hanim_calls,
           hanim_checks, hanim_diffs, (int)(hanim_max_diff * 1000000));
    hanim_calls = hanim_checks = hanim_diffs = 0;
    hanim_max_diff = 0;
}

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
int p4_bone_check = 1;
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
static int skin_bones(const Skin *skin, const Hierarchy *hierarchy)
{
    hierarchy_nodes_seen = hierarchy->numNodes;
    int count = skin->numBones < hierarchy->numNodes ? skin->numBones : hierarchy->numNodes;
    if (count > MAX_BONES)
        return -1;
    for (int i = 0; i < count; i++)
        multiply(&bones[i], &skin->skinToBone[i], &hierarchy->matrices[i]);
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
typedef struct { int mesh, bones, vtype, indices; unsigned char palette[8]; unsigned char *v; unsigned short *idx; int alpha[2]; } SkinBatch;
typedef struct { const Geometry *g; const V3 *verts; int vertices, lit, batches, nradius; unsigned sample, frame, bytes; SkinBatch *batch; float *radius; } SkinGeometry;
enum { SKIN_GEOMETRIES = 64, SKIN_PALETTE = 8 };
static SkinGeometry *skin_cache; /* heap: .bss is full */

/* the bones a vertex hangs from, with their weights (RenderWare leaves a single weight out of the stream) */
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

static void skin_free(SkinGeometry *e)
{
    for (int b = 0; b < e->batches; b++)
        free(e->batch[b].v);
    free(e->batch);
    free(e->radius);
    skin_ge_bytes -= e->bytes;
    memset(e, 0, sizeof *e);
}

/* one batch: the triangles in tri (corner indices), their own vertex numbering */
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
    int uv_at = (bones + 3) & ~3, colour_at = uv_at + 8, normal_at = colour_at + 4, position_at = normal_at + (e->lit ? 4 : 0);
    int stride = position_at + 12, bytes = stride * used + corners * 2;
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
    b->vtype = GU_WEIGHTS(bones) | GU_WEIGHT_8BIT | GU_TEXTURE_32BITF | GU_COLOR_8888 | (e->lit ? GU_NORMAL_8BIT : 0) | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D;
    b->v = v;
    b->idx = (unsigned short *)(v + stride * used);
    b->indices = corners;
    const V3 *verts = g->morphTarget->verts, *normals = g->morphTarget->normals;
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    b->alpha[0] = 255, b->alpha[1] = 0; /* the vertex colours' alpha; the material's comes on top when drawn */
    stamp = ++mesh_generation;
    for (int k = 0; k < corners; k++) {
        int j = tri[k];
        b->idx[k] = mesh_remap[j];
        if (mesh_stamp[j] == stamp)
            continue;
        mesh_stamp[j] = stamp;
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
        float *f = (float *)(out + uv_at);
        f[0] = uv ? uv[j][0] : 0;
        f[1] = uv ? uv[j][1] : 0;
        unsigned base = g->preLit ? double_colour(g->preLit[j]) : e->lit ? 0xff000000 : 0xffffffff;
        memcpy(out + colour_at, &base, 4);
        if ((int)(base >> 24) < b->alpha[0]) b->alpha[0] = base >> 24;
        if ((int)(base >> 24) > b->alpha[1]) b->alpha[1] = base >> 24;
        if (e->lit) {
            const V3 *nv = &normals[j];
            float len = sqrtf(nv->x * nv->x + nv->y * nv->y + nv->z * nv->z), inv = len > 0 ? 1.0f / len : 0;
            out[normal_at] = (unsigned char)normal_s8(nv->x * inv);
            out[normal_at + 1] = (unsigned char)normal_s8(nv->y * inv);
            out[normal_at + 2] = (unsigned char)normal_s8(nv->z * inv);
        }
        memcpy(out + position_at, &verts[j], 12);
    }
    sceKernelDcacheWritebackRange(v, bytes);
    e->bytes += bytes;
    skin_ge_bytes += bytes;
    return 1;
}

/* the cached GE form of a skinned geometry, built on first use; 0: it cannot go to the GE */
static SkinGeometry *skin_geometry(const Geometry *g, const Skin *skin, int nbones, int lit)
{
    if (!skin_cache && !(skin_cache = calloc(SKIN_GEOMETRIES, sizeof *skin_cache)))
        return 0;
    const V3 *verts = g->morphTarget->verts;
    unsigned sample = hash_sample(2166136261u, verts, g->numVertices, sizeof *verts);
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
    skin_free(spare);
    SkinGeometry *e = spare;
    e->g = g;
    e->verts = verts;
    e->vertices = g->numVertices;
    e->lit = lit;
    e->sample = sample;
    e->frame = cache_frame;
    skin_ge_builds++;
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
            V3 local = transform(&verts[j], &skin->skinToBone[bone[w]]);
            float d = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
            if (d > e->radius[bone[w]])
                e->radius[bone[w]] = d;
        }
    }
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, ok = 1;
    for (int m = 0; m < header->numMeshes && ok; m++) {
        const unsigned short *idx = mesh[m].indices;
        int triangles = strip ? mesh[m].numIndices - 2 : mesh[m].numIndices / 3;
        int *tri = malloc((triangles > 0 ? triangles : 1) * 3 * sizeof *tri), corners = 0, bones = 0;
        unsigned char palette[SKIN_PALETTE];
        if (!tri) {
            ok = 0;
            break;
        }
        for (int t = 0; t < triangles && ok; t++) {
            int c[3];
            if (strip) { /* odd strip triangles are wound the other way */
                c[0] = idx[t + (t & 1)]; c[1] = idx[t + 1 - (t & 1)]; c[2] = idx[t + 2];
                if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                    continue;
            } else {
                c[0] = idx[t * 3]; c[1] = idx[t * 3 + 1]; c[2] = idx[t * 3 + 2];
            }
            unsigned char need[12];
            int needed = 0;
            for (int k = 0; k < 3; k++) {
                unsigned char bone[4];
                float weight[4];
                int n = vertex_bones(skin, nbones, c[k], bone, weight);
                for (int w = 0; w < n; w++) {
                    int have = 0;
                    for (int s = 0; s < bones && !have; s++)
                        have = palette[s] == bone[w];
                    for (int s = 0; s < needed && !have; s++)
                        have = need[s] == bone[w];
                    if (!have)
                        need[needed++] = bone[w];
                }
            }
            if (bones + needed > SKIN_PALETTE) { /* this batch is full: draw it, start the next */
                if (corners && !skin_batch(e, m, palette, bones, tri, corners, g, skin, nbones))
                    ok = 0;
                corners = bones = 0;
                needed = 0;
                for (int k = 0; k < 3; k++) {
                    unsigned char bone[4];
                    float weight[4];
                    int n = vertex_bones(skin, nbones, c[k], bone, weight);
                    for (int w = 0; w < n; w++) {
                        int have = 0;
                        for (int s = 0; s < needed && !have; s++)
                            have = need[s] == bone[w];
                        if (!have)
                            need[needed++] = bone[w];
                    }
                }
                if (needed > SKIN_PALETTE) /* one triangle with more bones than the GE has: the CPU keeps it */
                    ok = 0;
            }
            for (int s = 0; s < needed && ok; s++)
                palette[bones++] = need[s];
            tri[corners++] = c[0];
            tri[corners++] = c[1];
            tri[corners++] = c[2];
        }
        if (ok && corners && !skin_batch(e, m, palette, bones > 0 ? bones : 1, tri, corners, g, skin, nbones))
            ok = 0;
        free(tri);
    }
    if (!ok)
        skin_free(e), e->g = g, e->verts = verts, e->vertices = g->numVertices, e->lit = lit, e->sample = sample, e->frame = cache_frame;
    return e->batches ? e : 0; /* a failed build stays as an empty entry: the CPU path, without retrying every frame */
}

/* 1: drawn on the GE; 0: the caller skins on the CPU */
static int draw_skinned_ge(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, const Matrix *model, int lit, int modulate)
{
    prof_begin(PROF_SBONES);
    bone_check_geometry = g;
    int nbones = skin_bones(skin, hierarchy);
    SkinGeometry *e = nbones > 0 ? skin_geometry(g, skin, nbones, lit) : 0;
    prof_end(PROF_SBONES);
    if (!e || e->nradius < nbones)
        return 0;
    /* A skinned vertex is a weighted mean of points within radius[i] of joint i; the guard band is convex, so it is
       inside when every joint's sphere is (one sphere round the whole skeleton crossed the near plane: b76 r 520). */
    const Matrix *space = (hierarchy->flags & HIERARCHY_LOCAL_SPACE) ? model : 0;
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    Mat4 m;
    prof_begin(PROF_SGUARD);
    guard_matrix(m, space);
    for (int i = 0; i < nbones; i++) {
        if (e->radius[i] <= 0)
            continue;
        const Matrix *pose = &hierarchy->matrices[i];
        float scale = sqrtf(pose->right.x * pose->right.x + pose->right.y * pose->right.y + pose->right.z * pose->right.z);
        float sphere[4] = { pose->pos.x, pose->pos.y, pose->pos.z, e->radius[i] * (scale > 1 ? scale : 1) };
        if (!guard_sphere_inside(m, sphere, near)) {
            skin_ge_fallbacks++;
            prof_end(PROF_SGUARD);
            return 0;
        }
    }
    prof_end(PROF_SGUARD);
    prof_begin(PROF_SDRAW);
    begin_frame();
    SceneLight light = mesh_state(space, lit);
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int slot_bone[SKIN_PALETTE]; /* bones[] index in each GE bone slot; bones[] is this character's */
    for (int k = 0; k < SKIN_PALETTE; k++)
        slot_bone[k] = -1;
    for (int b = 0; b < e->batches; b++) {
        const SkinBatch *batch = &e->batch[b];
        const Material *material = mesh[batch->mesh].material;
        prof_begin(PROF_SMAT);
        for (int k = 0; k < batch->bones; k++) {
            const Matrix *bm = &bones[batch->palette[k]];
            if (p4_bone_direct) {
                /* BONE_MATRIX_NUMBER then 12 x BONE_MATRIX_DATA (float >> 8), as sceGuBoneMatrix sends them; b123
                   Heaven: sceGuBoneMatrix cost ~1450 cycles a matrix, 1.78 Mcycles a frame. A slot that already holds
                   this bone (same character, earlier batch) keeps it. */
                if (slot_bone[k] == batch->palette[k])
                    continue;
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
        void **texture = material->texture;
        prof_begin(PROF_SBIND);
        bind_mask(material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        bind_ge_light(material, modulate, &light);
        prof_end(PROF_SBIND);
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
        prof_begin(PROF_STEST);
        draw_tested(GU_TRIANGLES, batch->vtype, batch->indices, batch->idx, batch->v);
        prof_end(PROF_STEST);
        draw_skin_alpha = 0;
    }
    st_enable(GU_LIGHTING, 0);
    prof_end(PROF_SDRAW);
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
typedef struct { const void *g; float cover; unsigned calls, vertices, frames, flags, nverts, meshes, ge_us, blend, test, tex; char texture_name[32]; } CoverEntry;
static CoverEntry cover_table[256];
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
static void cover_note(const Geometry *g, float cover, unsigned calls, unsigned vertices, unsigned ge_us)
{
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
}
static void cover_report(unsigned frames)
{
    if (p4_probe_census) {
        for (int i = 0; i < 256; i++) {
            const CoverEntry *e = &cover_table[i];
            if (e->g && e->frames)
                p4_log("census geometry %x v%u flags %x meshes %u calls %u vertices %u tex %s %ux%ux%u\n",
                       (unsigned)e->g, e->nverts, e->flags, e->meshes, e->frames / frames, e->vertices / frames,
                       e->texture_name, e->tex >> 20, e->tex >> 8 & 0xfff, e->tex & 0xff);
        }
        p4_probe_census = 0;
    }
    p4_log("cull far %d min_vertices %d: %u atomics per frame\n", p4_cull_far, p4_cull_min_vertices, cull_far_atomics / frames);
    cull_far_atomics = 0;
    p4_log("cover per frame: atomics' sphere cover %d%% draw-weighted %d%% mesh draws %u vertices %u exp_scissor %d\n",
           (int)(cover_total * 100 / frames), (int)(cover_weighted * 100 / frames), mesh_draw_calls / frames, mesh_draw_vertices / frames, p4_exp_scissor);
    for (int k = 0; k < 8; k++) {
        CoverEntry *best = 0;
        for (int i = 0; i < 256; i++)
            if (cover_table[i].g && cover_table[i].frames && (!best || (p4_exp_ge_time ? cover_table[i].ge_us > best->ge_us :
                cover_table[i].cover * cover_table[i].calls / cover_table[i].frames > best->cover * best->calls / best->frames)))
                best = &cover_table[i];
        if (!best)
            break;
        p4_log("cover top %d: geometry %x v%u f%x m%u drawn %u/%u frames cover %d%% draws %u vertices %u ge_us %u per frame tex %ux%ux%u blend %x test %x\n", k, (unsigned)best->g, best->nverts, best->flags,
               best->meshes, best->frames, frames, (int)(best->cover * 100 / best->frames), best->calls / best->frames, best->vertices / best->frames,
               best->ge_us / (ge_time_frames ? ge_time_frames : frames), best->tex >> 20, best->tex >> 8 & 0xfff, best->tex & 0xff, best->blend, best->test);
        best->frames = 0;
    }
    if (ge_time_frames)
        p4_log("ge time per frame us: atomics %u 2d %u ptank %u clear %u (%u frames)\n", ge_time_us[GT_ATOMIC] / ge_time_frames, ge_time_us[GT_2D] / ge_time_frames,
               ge_time_us[GT_PTANK] / ge_time_frames, ge_time_us[GT_CLEAR] / ge_time_frames, ge_time_frames);
    if (ge_timeouts)
        p4_log("ge time TIMEOUTS %u last state %x marker %x\n", ge_timeouts, ge_last_state, ge_marker & 0xffffff);
    memset(ge_time_us, 0, sizeof ge_time_us);
    ge_time_frames = 0;
    memset(cover_table, 0, sizeof cover_table);
    cover_total = cover_weighted = 0;
    mesh_draw_calls = mesh_draw_vertices = 0;
}

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
    if (tank && frame && current_camera) {
        draw_ptank(atomic, tank, g, frame);
        return atomic;
    }
    if (!g || !frame || !current_camera || (g->flags & GEOMETRY_NATIVE) || !g->mesh || !g->numVertices || !g->morphTarget || !g->morphTarget->verts) {
        atomics_skipped++;
        return atomic;
    }
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
    prof_end(PROF_ALTM);
    if (p4_cull_far > 0 && g->numVertices >= p4_cull_min_vertices && atomic_far(g, model)) {
        cull_far_atomics++;
        return atomic;
    }
    /* the cover ranking is a diagnostic (b121 Heaven: 0.42 Mcycles a frame): only for the experiments that print it */
    int cover_on = p4_cover_stats || p4_exp_ge_time || p4_probe_census;
    prof_begin(PROF_ACOVER);
    float cover = cover_on ? atomic_cover(g, model) : 0;
    prof_end(PROF_ACOVER);
    unsigned calls_before = mesh_draw_calls, verts_before = mesh_draw_vertices;
    int timed = ge_time_begin();
    void *r = atomic_draw(atomic, g, frame, model, normals);
    unsigned ge_us = ge_time_end(timed, GT_ATOMIC);
    prof_begin(PROF_ACOVER);
    if (cover_on)
        cover_note(g, cover, mesh_draw_calls - calls_before, mesh_draw_vertices - verts_before, ge_us);
    prof_end(PROF_ACOVER);
    return r;
}

static void *atomic_draw(void *atomic, Geometry *g, char *frame, const Matrix *model, const V3 *normals)
{
    const V3 *positions = g->morphTarget->verts;
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
            if (normals) normals = skin_vertices(g, skin, hierarchy, 1);
            skinned_drawn++;
            if (!(hierarchy->flags & HIERARCHY_LOCAL_SPACE))
                model = 0; /* the bones already stand in the world */
        }
    }
    /* the morph target's sphere is in the geometry's space; skinned vertices are not */
    draw_meshes(g->mesh, positions, g->numTexCoordSets ? g->texCoords[0] : 0, g->preLit, g->numVertices, model, g->flags & 0x40, normals,
                positions == g->morphTarget->verts && p4_guard_sphere ? g->morphTarget->sphere : 0);
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
    unsigned released = cache_bytes + skin_ge_bytes;
    for (int i = 0; i < CACHE_SLOTS; i++) free(mesh_cache[i].v);
    memset(mesh_cache, 0, sizeof mesh_cache);
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
