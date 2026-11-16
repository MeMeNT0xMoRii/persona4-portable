/* The profiler (moved from rwsky.c, b322; included once, at the same place): sections read off the CPU
   counters (profmode t), the game task table (hwtask) and the ordering-table node table (hwot), the wraps that
   time RenderWare calls, p4_clock_prof, and kwlnTaskRunList with the tick count. */
#ifdef P4_HW_PROFILE
#include <pspdebug.h>
enum { PROF_FRAME, PROF_ATOMIC, PROF_MESH, PROF_GUARD, PROF_SKIN, PROF_PTANK, PROF_2D, PROF_SYNC, PROF_VBLANK, PROF_SETUP, PROF_SHADE, PROF_EMIT, PROF_SUBMIT, PROF_HIT, PROF_GDRAW, PROF_KEY, PROF_UVSCAN, PROF_LIGHTS, PROF_HEMPTY, PROF_HBIND, PROF_DLIGHT, PROF_DDRAW, PROF_GCLASS, PROF_GLOOP, PROF_GCLIP, PROF_GSUB,
      PROF_MFRUST, PROF_MSLOT, PROF_MCOPY, PROF_ALTM, PROF_ABLACK, PROF_ACOVER, PROF_GESKIN, PROF_SECTOR, PROF_TASKS,
      PROF_SBONES, PROF_SGUARD, PROF_SDRAW, PROF_SMAT, PROF_SBIND, PROF_STEST,
      PROF_QMODEL, PROF_QATTACH, PROF_QSORT, PROF_QEFFECT, PROF_QFLD, PROF_QPRIM,
      PROF_AFRAME, PROF_AATTACH, PROF_ALAYER, PROF_ACTRL, PROF_AFX, PROF_AMOTION,
      PROF_ABLEND, PROF_AREBASE, PROF_APOSE, PROF_KADD, PROF_KSET, PROF_KBLEND, PROF_KMAT, PROF_KANIM, PROF_KFRAME, PROF_KINTERP, PROF_TBIND, PROF_LBIND,
      PROF_DSTATE, PROF_DSKIP, PROF_DPOSE, PROF_DRESTORE, PROF_DCOLOR, PROF_DVIS, PROF_DFX, PROF_DFRAME,
      PROF_RPLAY, PROF_RLIGHT, PROF_RGUARD, PROF_FSYNC, PROF_FTRANS, PROF_ASCALE, PROF_AHANDLE, PROF_ACHILD, PROF_AADV, PROF_QFX, PROF_AENV, PROF_AIB, PROF_AFP, PROF_ASL, PROF_SECTIONS };
enum { PC_COUNT = 5 };
/* counter words read per section edge: cpu cycles, stall, memory stall, imiss, dmiss (b73 read all 19, which itself
   cost a visible part of the frame at ~800 section edges a frame) */
/* b239: the words are read as prof_regs[1 + index] and PspDebugProfilerRegs starts with `enable`, so index k is the
   k-th counter after it: 0 systemck, 1 cpuck, 2 internal, 3 memory, 4 copz, ... 12 i_miss, 13 d_miss, 14 d_writeback.
   Up to b238 the table was { 1, 2, 4, 13, 14 }: the column printed as "mem_k" was copz, "imiss" was d_miss and
   "dmiss" was d_writeback; instruction-cache misses were never read. b239 checked it on a walk of 4096 uncached
   lines. Not const: pokeable. */
unsigned char p4_prof_index[PC_COUNT] = { 1, 2, 3, 12, 13 };
#define prof_index p4_prof_index
static const char *const prof_names[PROF_SECTIONS] = { "frame", "atomic", "mesh", "guard", "skin", "ptank", "2d", "gesync", "vblank", "msetup", "mshade", "memit", "msubmit", "mhit", "mgdraw", "mkey", "muvscan", "mlights", "hempty", "hbind", "dlight", "ddraw", "gclass", "gloop", "gclip", "gsub",
    "mfrust", "mslot", "mcopy", "altm", "ablack", "acover", "geskin", "sector", "tasks", "sbones", "sguard", "sdraw", "smat", "sbind", "stest",
    "qmodel", "qattach", "qsort", "qeffect", "qfld", "qprim",
    "aframe", "aattach", "alayer", "actrl", "afx", "amotion",
    "ablend", "arebase", "apose", "kadd", "kset", "kblend", "kmat", "kanim", "kframe", "kinterp", "tbind", "lbind",
    "dstate", "dskip", "dpose", "drestore", "dcolor", "dvis", "dfx", "dframe", "rplay", "rlight", "rguard", "fsync", "ftrans", "ascale", "ahandle", "achild", "aadv", "qfx", "aenv", "aib", "afp", "asl" };
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
static unsigned *prof_store_scratch; /* diagnostic-only heap storage: keep space below fixed .p4image */
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
static TaskStat *task_stats; /* b258: heap on first use (.bss is full) */
static unsigned task_overflow, task_lists;
static unsigned long long task_list_cycles;
static unsigned long long render_mono(void)
{
    return prof_mono[PROF_ATOMIC] + prof_mono[PROF_SECTOR] + prof_mono[PROF_2D] + prof_mono[PROF_SYNC] + prof_mono[PROF_VBLANK];
}
static void task_book(const KwlnTask *task, unsigned fn, unsigned cycles, unsigned long long render)
{
    unsigned h = (fn >> 2) * 2654435761u >> 25;
    if (!task_stats && !(task_stats = calloc(TASK_STATS, sizeof *task_stats))) {
        task_overflow++;
        return;
    }
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
/* p4_clock_prof 1 (poke it live, no profmode t or reset needed): without the thread profiler, the hwtask and hwot
   tables are timed with the system clock (microseconds x 333, so "cpu_k" stays roughly kcycles at 333 MHz) and logged
   every 128 frames. Inclusive only: the render sections are not timed then. Each timed call costs a system call, so
   the totals run a little high. 0: off, as before. */
int p4_clock_prof = 0;
static inline unsigned clock_cycles(void) { return sceKernelGetSystemTimeLow() * 333u; }
static int run_task(KwlnTask *node)
{
    if (prof_state <= 0 && !p4_clock_prof)
        return kwlnTaskRunUpdate(node);
    unsigned fn = (unsigned)node->update;
    unsigned long long render = render_mono();
    unsigned before = prof_state > 0 ? prof_regs[1 + prof_index[0]] : clock_cycles();
    int r = kwlnTaskRunUpdate(node);
    unsigned cycles = (prof_state > 0 ? prof_regs[1 + prof_index[0]] : clock_cycles()) - before;
    task_book(node, fn ^ (p4_skipping() ? SKIP_KEY : 0), cycles, render_mono() - render);
    hanim_scan_after(node->name, fn);
    return r;
}
/* the 24 costliest update callbacks per frame: inclusive and self kcycles */
static void task_report(unsigned frames)
{
    p4_log("hwtask lists %u per frame cpu_k %u overflow %u\n", task_lists / frames, (unsigned)(task_list_cycles / frames / 1000), task_overflow);
    for (int k = 0; k < 24 && task_stats; k++) { /* b191: 24, the undrawn ticks take rows too */
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
    if (task_stats)
        memset(task_stats, 0, TASK_STATS * sizeof *task_stats);
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
    if (prof_state <= 0 && !p4_clock_prof)
        return 0;
    if (ot_depth < 8)
        ot_render_at[ot_depth] = render_mono();
    ot_depth++;
    return prof_state > 0 ? prof_regs[1 + prof_index[0]] : clock_cycles();
}
void p4_ot_end(unsigned start, unsigned key)
{
    if ((prof_state <= 0 && !p4_clock_prof) || ot_depth <= 0)
        return;
    unsigned cycles = (prof_state > 0 ? prof_regs[1 + prof_index[0]] : clock_cycles()) - start;
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
P4_WRAP_VOID(mdlEffectListInvokeEach, PROF_QFX, (int context, void *list), (context, list)) /* b279: the effects a queued model runs */
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
/* b263: RenderWare's frame sync (_rwFrameSyncDirty, called by RwCameraBeginUpdate: every dirty root's whole frame
   tree gets its LTM again) and RwFrameTransform (mdlAdvanceModel moves each model's root frame every tick) */
void __real_p4r_003ed7e0(void);
void __wrap_p4r_003ed7e0(void) { prof_begin(PROF_FSYNC); __real_p4r_003ed7e0(); prof_end(PROF_FSYNC); }
void *__real_func_003e9cb0(void *frame, const void *matrix, int combine);
void *__wrap_func_003e9cb0(void *frame, const void *matrix, int combine) { prof_begin(PROF_FTRANS); void *r = __real_func_003e9cb0(frame, matrix, combine); prof_end(PROF_FTRANS); return r; }
void *__real_func_003e9680(void *frame);
void *__wrap_func_003e9680(void *frame) { prof_begin(PROF_KFRAME); void *r = __real_func_003e9680(frame); prof_end(PROF_KFRAME); return r; }
/* census FIXES hooks in mdlAdvanceModel (b139): id 0..5 = aframe..amotion, 6..8 = ablend, arebase, apose (b141);
   mdlUpdateDrawState (b182): 9 the whole call (dstate, or dskip in a frame frameskip leaves undrawn), 10 dpose,
   11 drestore, 12 dcolor, 13 dvis, 14 dfx, 15 dframe; mdlAdvanceModel again (b269): 16 ascale, 17 ahandle, 18 achild */
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
    if (id >= 16 && id <= 19) { /* b279: 19 aadv, mdlAdvanceModel as mdlQueueModelForDraw calls it (inclusive) */
        if (begin)
            prof_begin(PROF_ASCALE + id - 16);
        else
            prof_end(PROF_ASCALE + id - 16);
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
