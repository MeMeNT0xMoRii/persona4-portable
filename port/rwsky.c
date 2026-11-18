/* The hardware half of RenderWare's PS2 driver, on the PSP. A function here named after a retail
   address replaces that address for C and reassembled code alike. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspgum.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
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
/* p4_shuffle_force_bonus (census FIXES in btlShuffleBonusRoll): 1 = Shuffle Time's Arcana Chance roll always passes
   once the game's flags allow it. 0: the retail roll. */
int p4_shuffle_force_bonus = 0;
#include "prof.h" /* the profiler and the task runner */
#include "arena.h" /* b376: the caches' own memory */
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
/* b325: two frame lists of LIST_WORDS each, on the heap (allocated in func_003fe700). They were 64 K words in .bss
   with nothing bounding a frame: battles with fog wrote up to 67927 words and ran over the following memory (crashes in
   btlUnitColorPacketUpdate and sdkOtFlushBuckets with garbage pointers). Draws are now refused near the end
   (list_room). */
enum { LIST_WORDS = 128 * 1024, LIST_RESERVE = 4096 };
static unsigned (*list_store)[LIST_WORDS];
static unsigned *list;
static unsigned list_refused_draws;
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
/* Skip preparation only after the shared tick has selected an undrawn frame. */
int p4_font_skip = 0, p4_calendar_skip = 0;
static unsigned font_glyphs_skipped, calendar_ticks_skipped;
int p4_font_skip_prepare(void)
{
    if (!p4_font_skip || !skip_frame) return 0;
    font_glyphs_skipped++;
    return 1;
}
int p4_calendar_skip_prepare(void)
{
    if (!p4_calendar_skip || !skip_frame) return 0;
    calendar_ticks_skipped++;
    return 1;
}

/* b278 (census FIXES in mdlAdvanceModel): in an undrawn tick skip what the next drawn tick rebuilds from scratch.
   Bits: 1 root frame + attached model matrices, 2 effect transforms, 4 child models' indexed matrices. 0: as b277. */
int p4_skip_model_lite = 7; /* b279 default: Heaven -3..4 ms CPU (b278 A/B), picture unchanged */
int p4_tick_lite(int bit) { return (p4_skip_model_lite & bit) && skip_frame; }
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
/* b287 (census FIXES in mdlEffectFlushTreeFrame): an effect child's draw callback (kind table +0x0C) left out in an
   undrawn tick. Shuffle Time runs its effect tree three times a presented frame (docs/shuffle_time_perf.md).
   0: as b286. */
int p4_fx_skip_draw = 0;
static unsigned fx_draws_skipped;
int p4_fx_draw_skipped(void)
{
    if (!skip_frame || !p4_fx_skip_draw) return 0;
    fx_draws_skipped++;
    return 1;
}
static void skip_visual_report(unsigned frames)
{
    if (p4_font_skip || p4_calendar_skip || font_glyphs_skipped || calendar_ticks_skipped)
        p4_log("UI skip font %d glyphs %u calendar %d ticks %u per %u drawn frames\n",
               p4_font_skip, font_glyphs_skipped, p4_calendar_skip, calendar_ticks_skipped, frames);
    font_glyphs_skipped = calendar_ticks_skipped = 0;

    p4_log("skip visual bits %d: passes %u colors %u per %u frames; fx skip draw %d: %u\n",
           p4_skip_visual_bits, skip_visual_passes, skip_visual_colors, frames, p4_fx_skip_draw, fx_draws_skipped);
    skip_visual_passes = skip_visual_colors = fx_draws_skipped = 0;
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
/* b363 p4_timeline N (poke it): the next N presented frames log where their time goes, in microseconds from the
   frame's start (the end of the previous present): the first draw handed to the GE, the present (CPU done), the end
   of the wait for the frame before to be shown, the gate opening (the GE may draw this list), the GE finishing it,
   and the vblank flip that shows it. b360 PSP, Margaret: CPU 47.8 ms and the GE alone 26 ms, yet 67 ms a frame with
   19 ms waited at the present: is the GE idle (late draws) or slow (sharing the bus)? Frame k is logged when frame
   k + 3 is presented. 0: off. */
int p4_timeline = 0;
typedef struct { unsigned start, first, present, resumed, gate, gedone, shown; } TlFrame;
static TlFrame tl_frame[8];
static inline unsigned tl_now(void) { return sceKernelGetSystemTimeLow(); }
static void on_ge_finish(int arg)
{
    (void)arg;
    ge_finished++;
    if (p4_timeline)
        tl_frame[ge_finished & 7].gedone = tl_now();
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
/* b321 (from MeMeNT0xMoRii's fork): an FPS file next to the EBOOT shows the presented frame rate (tenths) in the
   bottom-left corner, counted at the flip. */
int p4_fps_overlay;
static unsigned fps_started, fps_frames, fps_window_valid;
static volatile unsigned fps_tenths;
static void fps_draw(void);

static void fps_presented(void)
{
    if (!p4_fps_overlay)
        return;
    unsigned now = sceKernelGetSystemTimeLow();
    if (!fps_window_valid) {
        fps_started = now;
        fps_frames = 0;
        fps_window_valid = 1;
        return;
    }
    unsigned elapsed = now - fps_started;
    ++fps_frames;
    if (elapsed >= 1000000) {
        fps_tenths = (unsigned)((fps_frames * 10000000ULL + elapsed / 2) / elapsed);
        fps_started = now;
        fps_frames = 0;
    }
}


static volatile unsigned flip_seq, shown_seq;
static void *volatile flip_fb;
static int flip_thread = -1;
static unsigned gate_seq, texture_waits;
static int gate_closed;
int p4_list_double = 0; /* b364, see gate_poll */
static int gate_at_present;
extern int gu_object_stack_depth;
static int flip_main(SceSize size, void *arg)
{
    (void)size, (void)arg;
    for (;;) {
        unsigned seq = flip_seq;
        if (seq != shown_seq && (int)(ge_finished - seq) >= 0) {
            void *fb = flip_fb;
            sceDisplayWaitVblankStart();
            if (sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)fb), STRIDE,
                                     PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE) >= 0)
                fps_presented();
            shown_seq = seq;
            if (p4_timeline)
                tl_frame[seq & 7].shown = tl_now();
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
/* 0: the frame list has less than LIST_RESERVE words left; the draw must be left out */
static int list_room(void)
{
    unsigned used = (((unsigned)gu_list->current & ~0x40000000u) - ((unsigned)list & ~0x40000000u)) / 4;
    if (used < LIST_WORDS - LIST_RESERVE)
        return 1;
    list_refused_draws++;
    return 0;
}

/* The display list through the data cache. libpspgu's sceGuStart writes the list through the uncached alias
   (0x40000000): b125 Heaven, an uncached store cost 124 cycles while the GE ran (cached 18), so every GE word did,
   e.g. 1600 cycles a bone matrix. The list is now written cached; every stall move (ours, and libpspgu's own in
   sceGuFinish and sceGuGetMemory) goes through --wrap=sceGeListUpdateStallAddr, which writes the new words back first.
   The GE never reads past the stall, and a word below it never changes again. */
int p4_list_cached = 1; /* 0: uncached list as libpspgu writes it, as b125 */
static unsigned list_flushed, list_lo, list_hi;
static unsigned list_peak_words, list_full_logged; /* b325 */
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
#include "ge_dump.h"
#ifdef P4_HW_PROFILE
static unsigned prof_cycles(void) { return prof_regs ? prof_regs[1 + prof_index[0]] : sceKernelGetSystemTimeLow() * 333u; } /* b356: clock mode */
static void prof_store_test(void)
{
    if (!prof_store_scratch && !(prof_store_scratch = memalign(64, 4096))) return;
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
static void translucent_flush(void);
static void translucent_report(void);
static void pipe_texture_wait(void)
{
    translucent_flush();
    if ((int)(ge_queued - ge_finished) <= 0)
        return;
    texture_waits++;
    while ((int)(ge_queued - ge_finished) > 0)
        sceKernelDelayThread(100);
}
static void begin_frame(void);
static int frame_colour_pending, in_camera_clear; /* b379, im2d.h p4_keep_previous */
static unsigned keep_previous_copies;
extern int p4_cull_back; /* b381, bind_depth */
static int cull_atomic_flags = -1; /* b382: the flags of the geometry atomic_render_body is drawing (-1: none) */
static unsigned cull_back_draws;
extern int p4_keep_previous;
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
static void far_lod_report(void);
static void guard_big_report(void);
static void sl_report(void);
static void ptank_spill_collect(int all);
static void ptank_spill_report(void);
static void cloud_report(void);
static void fx_report(void);
static int fx_set_reg(void *atomic, int which, unsigned value);
static void sk_report(void);
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
extern int p4_skin_strip;
static unsigned strip_batches, strip_failed, strip_list_idx, strip_idx; /* b209 */
float p4_skin_lod_depth = 0; /* b245, see skin_geometry; b254: 900 (b246 dialogue: 78 -> 67 ms). b308: off until rebuilt: the online
   simplifier opened holes in the far party's faces after the Heaven battle (PPSSPP), at any per-mesh minimum */
int p4_skin_lod_keep = 50;
/* b306: meshes under this many triangles stay whole in a simplified entry. Halving the small ones (eyes, lids, the
   parts behind the TV-world glasses) opened holes in the far party's faces after the Heaven battle; they carry little
   of the GE's work. Part of the cache key, so a poke rebuilds. b307: not enough, the holes stayed at 128. */
int p4_skin_lod_min = 128;
/* b254 far level: beyond p4_skin_far_depth a character is drawn with at most p4_skin_far_bones weighted bones
   (bone_lod.h: dropped bones follow their nearest kept ancestor) and p4_skin_far_keep percent of its triangles, so
   each mesh is one GE batch (b245 Heaven: the far Margaret was 34 batches and ~150 bones a frame). 0 depth: off. */
float p4_skin_far_depth = 0; /* b308: off with p4_skin_lod_depth (same simplifier); was 3000, no CPU gain measured */
int p4_skin_far_keep = 30, p4_skin_far_bones = 8;
static unsigned skin_lod_draws, skin_full_draws, skin_far_draws, skin_far_builds, skin_far_bones_kept, skin_far_bones_in, skin_far_us, skin_far_fallbacks;
int p4_skin_free_wait = 1; /* b241 */
int p4_skin_dummy = 0;
static unsigned skin_free_waits, skin_dummies;
/* b213: p4_strip_why 1 runs b210's whole-mesh diagnosis (two strips over every mesh: on the PSP most of the 7 s of a
   party's strip build); p4_strip_check 1 also runs b209's linear start search and compares the strips */
extern int p4_strip_why, p4_strip_check;
/* b250: strips made on the PC (tools/psp_strips.py -> p4strips.bin), looked up by a batch's index list; see port/strippack.h */
extern int p4_strip_pack, p4_strip_prims;
static unsigned pack_hits, pack_misses, pack_refused, pack_us, pack_prims;
static int pack_fd = -1, pack_count;
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
static unsigned guard_cut_edge[16], guard_cut_near, guard_cut_side, guard_cut_both; /* b355: guard_cut_stats in guard.h */
static unsigned guard_pool_recovered, guard_pool_exhausted; /* b309 */
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
static void guard_census_report(void); /* b387, guard.h */
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
int p4_dst_alpha_passes = 1; /* b253: feedback readers need their preceding alpha masks. */
/* b284: with p4_dst_alpha_passes 1, the small destination-alpha passes and their alpha-only writers are still
   skipped (as before b253) in a frame after one that had more than p4_dst_alpha_many of them: a menu has one or two
   (the System menu's selected label, b252), the dream fog's screen-copy puffs dozens (f068 '???', b283: 66.7 -> 52 ms
   with them off, the picture the same to the eye). 0: every pass drawn, as b253-b283. */
int p4_dst_alpha_many = 8;
/* b343: in those crowded frames (enemy appearance clouds) the 2D alpha masks keep only bits >= this: 32 is
   1 clear + 3 bit passes instead of up to 9 per puff, the noise blend by them in 5 steps. 1: every bit, as b342. */
int p4_alpha_mask_coarse = 32;
static unsigned dst_small_now, dst_small_last, dst_small_frame;
/* b177: an alpha-only write (PS2 FBMSK 0x00ffffff) goes to the GE with the stencil test off and RGB masked, so the alpha
   byte takes each pixel's fragment alpha (Gouraud, texture) as the PS2's does, instead of one stencil value per draw.
   The passes that read it (blend 0x52/54/58) then draw too. Field Shadow (PCSX2 GS dump): a fan writes a radial alpha
   disc, then a screen copy and three smoke layers blend by it; skipped, only the spiky outline was left.
   0: one stencil value per draw and small passes skipped, as b176. Off: in PPSSPP it did not bring the Shadow's body
   back (its draws reach neither the 2D path nor Im3D) and drew a dark band by the minimap (a screen-copy pass). */
int p4_alpha_pixel = 0;
/* 2D alpha masks use fragment alpha; keep legacy screen-feedback filtering separate. */
int p4_2d_alpha_masks = 1;
/* Experimental late pass for wholly RGB_ONLY, unlit translucent meshes. */
int p4_translucent_late = 1;
#include "alpha_policy.h"
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
    if (!list_store) { /* b325 */
        list_store = memalign(64, 2 * sizeof *list_store);
        if (!list_store) {
            p4_log("display list allocation failed\n");
            return 0;
        }
        list = list_store[0];
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
    { extern void p4_particle_selftest(void); p4_particle_selftest(); }
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

static int swizzle_to_arena; /* b376: clut32 copies are caches: their swizzled texels go to the arena */
static void *swizzle_alloc(unsigned align, unsigned n)
{
    return swizzle_to_arena ? p4a_memalign(align, n) : memalign(align, n);
}
/* temporary rows: the arena when it has room (no short-lived hole between the game's blocks), else the heap */
static void *scratch_alloc(unsigned n)
{
    void *p = p4a_memalign(64, n);
    return p ? p : memalign(64, n);
}
static void swizzle_raster(Raster *r)
{
    if (!p4_swizzle_textures || !r->originalPixels || swizzle_head(r->originalPixels))
        return;
    int wbytes = (r->stride + 15) & ~15, rows = (r->height + 7) & ~7;
    int levels = mip_levels(r), total = wbytes * rows, w, h;
    for (int l = 1; l < levels; l++)
        total += mip_level_size(r, l, &w, &h);
    unsigned char *base = swizzle_alloc(64, SWIZZLE_HEAD + total), *linear = 0;
    if (base && levels > 1 && !(linear = scratch_alloc(r->stride * r->height / 4))) {
        p4a_free(base);
        base = 0;
    }
    if (!base) { /* no room for the levels: level 0 only */
        levels = 1;
        total = wbytes * rows;
        base = swizzle_alloc(64, SWIZZLE_HEAD + total);
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
    p4a_free(linear);
    SwizzleHead *head = (SwizzleHead *)base;
    *head = (SwizzleHead){ SWIZZLE_MAGIC, (unsigned)pixels ^ SWIZZLE_MAGIC, wbytes, rows, {0}, 0, levels };
    raster_used_entries(r, head->used); /* the levels' texels are a subset of level 0's: same entries, alpha */
    head->alpha_min = raster_alpha_min(r, head->used, &head->alpha_max);
    sceKernelDcacheWritebackRange(base, SWIZZLE_HEAD + total);
    p4a_free(r->originalPixels);
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
#include "clut32.h"
static void tex2d_report(void); /* im2d.h, b374 */

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
typedef struct { Raster *raster; unsigned offset, bytes; int w, h, psm, scale, fade; } RttSlot; /* fade: b332 */
static RttSlot rtt[RTT_SLOTS];
static RttSlot *rtt_target; /* the camera texture being drawn into; 0: the screen */
static int rtt_restore;     /* a target vanished outside a list: the next list starts by binding the screen */
static unsigned rtt_passes, rtt_clears, rtt_atomics, rtt_2d, rtt_sampled; /* per 256 frames, for the log */
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
        *free_slot = (RttSlot){ r, at, bytes, w, h, bpp == 2 ? GU_PSM_5551 : GU_PSM_8888, scale, 0 };
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
    if (s)
        s->fade = 0; /* b332: set again if this pass draws a shadow fade */
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
static unsigned texture_generation;
int func_003fb830(void *out, Raster *r)
{
    translucent_flush();
    texture_generation++; /* b258: prepared lists that bind it are made again */
    RttSlot *slot = rtt_find(r);
    if (slot) {
        if (rtt_target == slot && in_frame)
            rtt_bind_target(0);
        else if (rtt_target == slot)
            rtt_target = 0, rtt_restore = 1; /* begin_frame binds the screen in the next list */
        slot->raster = 0;
    }
    clut32_drop(r);
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
    translucent_flush();
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
    translucent_flush();
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
    translucent_flush();
    if (SKIPPING)
        return 1;
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    if (p4_probe_2d == 3 && census2d_shown < 64)
        census2d_shown++, p4_log("census2d camera clear mask %d colour %08x\n", mask, *colour);
    int bits = (mask & 1 ? GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT : 0) | (mask & 2 ? GU_DEPTH_BUFFER_BIT : 0);
    in_camera_clear = 1; /* b379: a clear is not a draw that needs the previous frame */
    begin_frame();
    in_camera_clear = 0;
    /* b333: the game clears a camera before its begin update (Junes / Living Room shadow camera): clear the camera's
       own texture, not whatever is bound. Before, the texture was never cleared and the silhouettes piled up. */
    RttSlot *was = rtt_target, *own = rtt_get(*(Raster **)((char *)camera + 0x60));
    if (own != was)
        rtt_bind_target(own);
    rtt_clears += rtt_target != 0;
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
    if (own != was)
        rtt_bind_target(was);
    if ((mask & 1) && !own && !was)
        frame_colour_pending = 0; /* the screen's colour is cleared: nothing to keep */
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
    translucent_flush();
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
    if (p4_timeline)
        tl_frame[(ge_queued + 1) & 7].present = tl_now();
    static int cull_back_seen;
    if (p4_cull_back != cull_back_seen) /* b382: prepared lists hold the cull state */
        cull_back_seen = p4_cull_back, texture_generation++;
    static int clut32_seen;
    if (p4_clut32 != clut32_seen) /* b370: prepared lists that bound a 32-bit texture are made again */
        clut32_seen = p4_clut32, texture_generation++;
#ifdef P4_HW_PROFILE
    static unsigned present_end, window_start, samples, cpu_us, ge_us, vblank_us;
    unsigned before_sync = sceKernelGetSystemTimeLow();
    if (present_end)
        cpu_us += before_sync - present_end;
#endif
    watch_device_table(frames);
    { /* b325: the frame's display list peak (LIST_WORDS words, list_room refuses draws near the end) */
        unsigned used = (((unsigned)gu_list->current & ~0x40000000u) - ((unsigned)list & ~0x40000000u)) / 4;
        if (used > list_peak_words)
            list_peak_words = used;
        if (used > LIST_WORDS - LIST_RESERVE && list_full_logged < 8) {
            list_full_logged++;
            p4_log("TRACE display list nearly full: %u of %u words, frame %d\n", used, (unsigned)LIST_WORDS, frames);
        }
    }
    int present_new_frame = in_frame;
    if (present_new_frame && p4_fps_overlay && !rtt_target)
        fps_draw();
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
        gate_at_present = 1; /* b364: with p4_list_double the only place the gate opens */
        gate_poll();
        gate_at_present = 0;
        if (p4_timeline)
            tl_frame[(ge_queued + 1) & 7].resumed = tl_now();
    } else {
        while (flip_seq != shown_seq) /* a frame the flip thread still has to show */
            sceKernelDelayThread(100);
        if (gate_closed) {
            gate_closed = 0;
            gu_object_stack_depth = 0;
        }
        int finished = in_frame;
        if (in_frame) {
            sceGuFinish();
            ge_queued++;
            in_frame = 0;
        }
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        if (gd.stage == 1) { /* the GE is idle and the next list not started: its state starts the dump */
            gd_capture_init();
            gd.stage = 2;
        } else if (gd.stage == 2 && finished) {
            gd_record((unsigned)gu_list->start, (unsigned)gu_list->current, (unsigned)draw_buffer);
            p4_pipeline = gd.saved_pipeline;
            gd.stage = 0;
        }
    }
    prof_end(PROF_SYNC);
#ifdef P4_HW_PROFILE
    unsigned after_sync = sceKernelGetSystemTimeLow();
    ge_us += after_sync - before_sync;
#endif
    vertex_used = index_used = 0;
    cache_frame++;
    { extern volatile int p4_cache_flush_pending; extern unsigned p4_mesh_cache_release(void);
      if (p4_cache_flush_pending) p4_cache_flush_pending = 0, p4_mesh_cache_release(); }
    ptank_spill_collect(0);
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
        if (p4_timeline) { /* the frame just queued is ge_queued; log the one three back, which is shown by now */
            const TlFrame *o = &tl_frame[(ge_queued - 3) & 7];
            if (o->start && o->shown) {
                p4_log("TL frame %u: first draw %d present %d resumed %d gate %d ge done %d shown %d us\n", ge_queued - 3,
                       o->first ? (int)(o->first - o->start) : -1, (int)(o->present - o->start), (int)(o->resumed - o->start),
                       o->gate ? (int)(o->gate - o->start) : -1, (int)(o->gedone - o->start), (int)(o->shown - o->start));
                p4_timeline--;
            }
            TlFrame *n = &tl_frame[(ge_queued + 1) & 7];
            memset(n, 0, sizeof *n);
            n->start = tl_now();
        }
        pipe_set ^= 1;
        list = list_store[pipe_set];
        vertex_pool = vertex_store + pipe_set * (POOL_VERTICES / 2);
        index_pool = index_store + pipe_set * (POOL_INDICES / 2);
        vertex_cap = POOL_VERTICES / 2;
        index_cap = POOL_INDICES / 2;
    } else {
        if (sceDisplaySetFrameBuf((void *)(0x04000000 + (unsigned)draw_buffer), STRIDE,
                                 PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE) >= 0 && present_new_frame)
            fps_presented();
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
    if (samples == 127 && prof_state <= 0 && p4_clock_prof) { /* the system clock stands in for the profiler */
        p4_log("clock prof: hwtask/hwot in us x 333, inclusive (self = inclusive), per frame\n");
        task_report(128);
        ot_report(128);
    }
    prof_begin(PROF_FRAME);
#endif
    /* Debug freeze: hold L + R + SELECT and the frame just shown stays, its display list and vertices untouched, so a
       moment too short to catch can be read over PSPLink (screenshot, savemem of list/vertex_pool). */
    {
        SceCtrlData pad;
        const unsigned combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT;
        int read = sceCtrlPeekBufferPositive(&pad, 1) > 0;
        if (read)
            gd_poll(pad.Buttons); /* L + R + SQUARE: GE dump (ge_dump.h) */
        if (read && (pad.Buttons & combo) == combo) {
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
        extern int p4_tex2d_census;
        if (p4_tex2d_census)
            p4_tex2d_census = 0, tex2d_report();
        if (p4_cull_back)
            p4_log("cull back %d: draws culled-mode %u\n", p4_cull_back, cull_back_draws), cull_back_draws = 0;
        if (keep_previous_copies)
            p4_log("keep previous %d: frames started from the shown one %u\n", p4_keep_previous, keep_previous_copies), keep_previous_copies = 0;
        if (p4_clut32 || clut32_made)
            p4_log("clut32 %d: binds %u made %u (%u ms in all) full %u stale %u\n", p4_clut32, clut32_binds, clut32_made, clut32_ms, clut32_full, clut32_stale), clut32_binds = 0;
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
        arena_report();
        p4_log("frames %d atomics %d skipped %d ptanks %d/%d sectors %d skinned %d other %d tests %d out %d af48 %d\n", frames, atomics_drawn, atomics_skipped, ptanks_drawn, particles_drawn, sectors_drawn, skinned_drawn, pipeline_other, frustum_tests, frustum_out, G32(0xaf48 - 0x10000));
        p4_log("display list peak %u of %u words; draws refused for room %u\n", list_peak_words, (unsigned)LIST_WORDS, list_refused_draws);
        list_peak_words = list_refused_draws = 0;
        p4_log("guard on %d meshes %d clipped %d dropped %d spheres %d vertices %d uvrebase %d uvmemo %d hits %u dstalpha_skipped %d\n", p4_guard_clip, guard_meshes, guard_clipped, guard_dropped, guard_spheres, guard_vertices, uv_rebased, p4_uv_memo, uv_memo_hits, dst_alpha_skipped);
        guard_big_report();
        if (guard_pool_recovered || guard_pool_exhausted) {
            p4_log("guard pool recovered %u exhausted %u\n", guard_pool_recovered, guard_pool_exhausted);
            guard_pool_recovered = guard_pool_exhausted = 0;
        }
        uv_memo_hits = 0;
        if (rtt_passes)
            p4_log("rtt passes %u clears %u atomics %u 2d %u sampled %u\n", rtt_passes, rtt_clears, rtt_atomics, rtt_2d, rtt_sampled);
        rtt_passes = rtt_clears = rtt_atomics = rtt_2d = rtt_sampled = 0;
        { extern void p4_blend_report(void); p4_blend_report(); }
        { extern void p4_census3d_report(void); p4_census3d_report(); }
        { extern void p4_matfx_report(void); p4_matfx_report(); }
        { extern unsigned p4_gs_alpha_splits(void); unsigned n = p4_gs_alpha_splits(); if (n) p4_log("gs alpha fb_only splits %u\n", n); }
        p4_log("guard near_ge %d triangles %u clip_fast %d checks %u diffs %u gnear %d offscreen %u\n", p4_guard_near_ge, guard_near_ge, p4_guard_clip_fast, guard_clip_checks, guard_clip_diffs, p4_guard_gnear, guard_offscreen);
        p4_log("guard boxes %d meshes whole %u chunks inside %u partial %u culled %u builds %u lazy vertices %u vfpu %d checks %u diffs %u\n", p4_guard_boxes, guard_box_whole, guard_box_inside, guard_box_partial, guard_box_culled, guard_box_builds, guard_lazy_vertices, p4_guard_box_vfpu, guard_box_checks, guard_box_diffs);
        guard_box_checks = guard_box_diffs = 0;
        guard_near_ge = guard_clip_checks = guard_clip_diffs = guard_offscreen = 0;
        p4_log("guard small %d chunks %u whole %u; box rebuilds of the same mesh %u\n", p4_guard_small, guard_small_chunks, guard_small_whole, guard_box_same);
        guard_result_report();
        guard_box_same = 0;
        guard_box_whole = guard_box_inside = guard_box_partial = guard_box_culled = guard_box_builds = guard_lazy_vertices = 0;
        guard_small_chunks = guard_small_whole = 0;
        struct mallinfo heap = mallinfo();
        /* b114 set the budget to what the cache held when the heap was short; during a load that was 0 and the cache
           stayed off for the session (Heaven 38 KB free while loading, 1.3 MB a few seconds later). Now the budget only
           follows the heap between the b72 4 MB and 6 MB, both ways. */
        extern unsigned p4_heap_unclaimed(void);
        /* b376: with the caches' arena the budget follows the arena's free bytes (floor 1 MB), not the game's heap */
        unsigned heap_free = arena_ready ? arena_free_bytes() : heap.fordblks + p4_heap_unclaimed(); /* b166: the arena's free alone missed the untouched heap */
        unsigned room = cache_bytes + (heap_free > CACHE_HEAP_RESERVE ? heap_free - CACHE_HEAP_RESERVE : 0), floor = arena_ready ? 1024 * 1024 : CACHE_BUDGET_MIN;
        unsigned want = room < floor ? floor : room > CACHE_BUDGET_MAX ? CACHE_BUDGET_MAX : room;
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
        if (strip_batches || strip_failed)
            p4_log("skin strip %d: batches built as strips %u (list indices %u -> %u), failed checks %u, took %u us; check %d: %u compared, %u differ\n",
                   p4_skin_strip, strip_batches, strip_list_idx, strip_idx, strip_failed, strip_us, p4_strip_check, strip_checks, strip_check_diffs);
        strip_batches = strip_failed = strip_list_idx = strip_idx = strip_us = strip_checks = strip_check_diffs = 0;
        if (p4_strip_pack || pack_hits || pack_misses || pack_refused)
            p4_log("strip pack %d prims %d: %d batches in the file, found %u not found %u refused %u, %u us, PRIMs of found batches %u\n",
                   p4_strip_pack, p4_strip_prims, pack_count, pack_hits, pack_misses, pack_refused, pack_us, pack_prims);
        if (pack_fd >= 0 && !pack_hits && !pack_misses && !pack_refused) /* not used since the last report: give the handle back */
            sceIoClose(pack_fd), pack_fd = -1;
        pack_hits = pack_misses = pack_refused = pack_us = pack_prims = 0;
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
        far_lod_report();
        sl_report();
        ptank_spill_report();
        cloud_report();
        fx_report();
        sk_report();
        other_report(256);
        hanim_report();
        census2d_report(256);
        translucent_report();
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
        { extern int p4_hanim_key_span; extern unsigned p4_hanim_equal_keys[2];
          p4_log("hanim key span %d: equal-time keys plain %u compressed %u\n", p4_hanim_key_span, p4_hanim_equal_keys[0], p4_hanim_equal_keys[1]);
          p4_hanim_equal_keys[0] = p4_hanim_equal_keys[1] = 0; }
        hanim_nan_fixed = hanim_nan_after_update = 0;
        bone_bad_count = 0;
        look_report();
        { extern void p4_anim_report(void); p4_anim_report(); }
        { extern void p4_interp_report(void); p4_interp_report(); }
        { extern int p4_skin_guard_box; extern unsigned skin_box_saved_count(void); p4_log("skin guard box %d: bones kept on the GE by their box %u\n", p4_skin_guard_box, skin_box_saved_count()); }
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
        if (current_camera)
            p4_log("guard camera near %d/100 far %d (the camera drawing now)\n", (int)(*(float *)((char *)current_camera + 0x80) * 100), (int)*(float *)((char *)current_camera + 0x84)); /* CAMERA_NEAR, CAMERA_FAR */
        p4_log("guard cut per 256 frames: near %u side %u both %u; longest edge <2..<32768: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n",
               guard_cut_near, guard_cut_side, guard_cut_both, guard_cut_edge[0], guard_cut_edge[1], guard_cut_edge[2], guard_cut_edge[3],
               guard_cut_edge[4], guard_cut_edge[5], guard_cut_edge[6], guard_cut_edge[7], guard_cut_edge[8], guard_cut_edge[9], guard_cut_edge[10],
               guard_cut_edge[11], guard_cut_edge[12], guard_cut_edge[13], guard_cut_edge[14], guard_cut_edge[15]);
        memset(guard_cut_edge, 0, sizeof guard_cut_edge), guard_cut_near = guard_cut_side = guard_cut_both = 0;
        guard_census_report();
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
/* Prepared-list state key: invalidate at every writer, including saved passes. */
static int sl_state_dirty = 1;

/* b322 (from MeMeNT0xMoRii's fork, home19): the far fog plane (Sky state 4) comes as a pointer to a float in the
   retail setter; its value is kept, not the pointer. */
enum { SKY_FAR_FOG_PLANE = 4 };
void RpSkyRenderStateSet(int state, uintptr_t value)
{
    if (state == SKY_FAR_FOG_PLANE && value)
        memcpy(&value, (const void *)value, sizeof sky_state[state]);
    if (state >= 0 && state < 8 && sky_state[state] != value) {
        sky_state[state] = value;
        sl_state_dirty = 1;
    }
}

unsigned RpSkyRenderStateGet(int state, void *value)
{
    if (state >= 0 && state < 8 && value)
        memcpy(value, &sky_state[state], sizeof sky_state[state]);
    return 1;
}

/* fpRenderStateSet / fpRenderStateGet: remembered until Im2D needs them. */
#define MAX_STATE 64
enum { RW_FILTER_NEAREST = 1, RW_FILTER_LINEAR = 2 };
enum { RS_ZTEST = 6, RS_ZWRITE = 8, RS_TEXTURE_FILTER = 9, RS_FOG_ENABLE = 14, RS_FOG_COLOR = 15, RS_FOG_TYPE = 16, RS_CULLMODE = 20 };
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
static int gu_fog_active; /* b322: GU_FOG is on for a 3D mesh draw */
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
/* b364 p4_list_double 1 (experiment): double-buffered command lists, as native PSP games do. The gate opens only at
   the present, so the GE runs list N whole while the CPU writes list N + 1 (one frame later on screen), instead of
   catching up on list N while it is being written (b82). b360 PSP Margaret: CPU 47.8 ms, GE alone 26 ms, yet 67 ms
   a frame. 0: as before. */
static int gate_poll(void)
{
    if (p4_list_double && !gate_at_present)
        return !gate_closed;
    if (gate_closed && (int)(shown_seq - gate_seq) >= 0) {
        gate_closed = 0;
        gu_object_stack_depth = 0;
        if (p4_timeline)
            tl_frame[(ge_queued + 1) & 7].gate = tl_now();
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
        if (p4_timeline && !tl_frame[(ge_queued + 1) & 7].first)
            tl_frame[(ge_queued + 1) & 7].first = tl_now();
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
    if (gu_fog_active) { /* fog is the 3D meshes' only (b322: also when the state cache did not know it) */
        sceGuDisable(GU_FOG);
        gu_fog_active = 0;
    }
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
/* b365 p4_ge_raw 1: the state cache writes its GE words itself instead of calling libpspgu (one call, a switch and a
   store each in libpspgu), as native games do. libpspgu's own record of enabled states (gu_states, read by
   sceGuGetStatus in im2d.h and shadow_capsules.h) is kept the same. States whose enable sends more than one word or
   reads libpspgu's context (scissor, fragment 2x, face normal reverse, patch face) still go through it. 0: libpspgu. */
int p4_ge_raw = 1;
extern unsigned int gu_states;
static const unsigned char ge_enable_cmd[22] = { 0x22, 0x23, 0, 0x24, 0x21, 0x1D, 0x20, 0x1F, 0x1C, 0x1E, 0x17, 0x18, 0x19,
                                                 0x1A, 0x1B, 0x25, 0x26, 0x27, 0x28, 0, 0, 0 };
static inline void ge_word(unsigned cmd, unsigned arg)
{
    *gu_list->current++ = cmd << 24 | (arg & 0xffffff);
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
    unsigned cmd = p4_ge_raw && state >= 0 && state < 22 ? ge_enable_cmd[state] : 0;
    if (cmd) {
        ge_word(cmd, on != 0);
        gu_states = on ? gu_states | 1u << state : gu_states & ~(1u << state);
    } else if (on)
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
    if (p4_ge_raw)
        ge_word(0xDB, func | (ref & 0xff) << 8 | 0xff << 16);
    else
        sceGuAlphaFunc(func, ref, 0xff);
}
static void st_depth_func(int func)
{
    if (gu_mesh_mode) {
        if (gs.depth_func_known && gs.depth_func == func)
            return;
        gs.depth_func_known = 1, gs.depth_func = func;
    }
    if (p4_ge_raw)
        ge_word(0xDE, func);
    else
        sceGuDepthFunc(func);
}
static void st_depth_mask(int mask)
{
    if (gu_mesh_mode) {
        if (gs.depth_mask_known && gs.depth_mask == mask)
            return;
        gs.depth_mask_known = 1, gs.depth_mask = mask;
    }
    if (p4_ge_raw)
        ge_word(0xE7, mask);
    else
        sceGuDepthMask(mask);
}
static void st_pixel_mask(unsigned mask)
{
    if (gu_mesh_mode) {
        if (gs.pixel_known && gs.pixel == mask)
            return;
        gs.pixel_known = 1, gs.pixel = mask;
    }
    if (p4_ge_raw) {
        ge_word(0xE8, mask);
        ge_word(0xE9, mask >> 24);
    } else
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
/* b250 p4_strip_prims: the indices of this draw are draw_strip_n strips of draw_strip_lens[] indices, one after the
   other. The first PRIM carries the addresses; each further PRIM word draws the next strip, because the GE leaves
   the index address behind the last index it read [public: PPSSPP GPUCommon, AdvanceVerts]. Set by draw_skinned_ge
   around draw_tested, so both alpha-test passes draw the same strips. */
static const unsigned short *draw_strip_lens;
static int draw_strip_n;
static void gu_draw(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    mesh_draw_calls++;
    mesh_draw_vertices += count;
    if (vtype)
        work_vtype = vtype;
    if (ge_work_on)
        work_draw(work_vtype, count, indices);
    int more = draw_strip_n > 1 && indices ? draw_strip_n - 1 : 0;
    if (more)
        count = draw_strip_lens[0];
    if (!gu_mesh_mode || p4_draw_batch <= 1) {
        sceGuDrawArray(prim, vtype, count, indices, vertices);
        for (int k = 1; k <= more; k++)
            *gu_list->current++ = 0x04u << 24 | (((unsigned)prim << 16 | draw_strip_lens[k]) & 0xffffff);
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
    for (int k = 1; k <= more; k++)
        *c++ = 0x04u << 24 | (((unsigned)prim << 16 | draw_strip_lens[k]) & 0xffffff);
    gu_list->current = c;
    if (++draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
}

/* b365: every sceGuDrawArray (2D, particles, clouds, capsule shadows, libpspgu's own sceGuClear) linked through here
   (--wrap): the same words, but the stall address moves every p4_draw_batch draws through stall_flush (and the gate)
   instead of a sceGeListUpdateStallAddr syscall per draw. p4_ge_raw 0: libpspgu's. */
void __real_sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices);
void __wrap_sceGuDrawArray(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    if (!p4_ge_raw) {
        __real_sceGuDrawArray(prim, vtype, count, indices, vertices);
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

/* b381 p4_cull_back: RenderWare's cull mode for 3D meshes (render state 20 == 2, rwCULLMODECULLBACK), as the Shuffle
   Time cards already do in Im3D (b?): 1 culls with CCW as the front face, 2 with CW, 0 never culls (as before). The
   limousine floor patch (Velvet Room, 4-vertex lit untextured quad, normals down) is coplanar with the carpet: a back
   face from the camera, drawn over the carpet with culling off. */
int p4_cull_back = 3; /* b382: 3 by default (Velvet Room PSP: the patch gone, CCW front); 1/2 cut holes in curtains and walls */
static void bind_depth(void)
{
    /* 3 and 4: only lit, untextured meshes without prelight (the f73 class of the limousine floor patch), CCW / CW */
    int cull = p4_cull_back && gu_mesh_mode && render_state[RS_CULLMODE] == 2;
    if (cull && p4_cull_back >= 3) {
        cull = cull_atomic_flags >= 0 && (cull_atomic_flags & 0x2c) == 0x20;
    }
    if (cull) {
        sceGuFrontFace(p4_cull_back == 2 || p4_cull_back == 4 ? GU_CW : GU_CCW);
        cull_back_draws++;
    }
    st_enable(GU_CULL_FACE, cull);
    if ((p4_probe_flags & 32) && rtt_target) { /* b328 diagnostic: no depth test into camera textures */
        st_enable(GU_DEPTH_TEST, 0);
    } else if (render_state[RS_ZTEST] || render_state[RS_ZWRITE]) {
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
/* b331: draws without DATE in a camera texture write their own alpha (material, or the 2D draw's first vertex) where
   they pass (0: 0xFF, as b165). Junes hero shadow: the fan over the upper rows has alpha 0 and erases the shadow there;
   with 0xFF it was a dark square. The DATE fan's alpha gradient is still lost (per-pixel alpha cannot reach the
   stencil): an even shadow. */
int p4_rtt_alpha = 1;
static int date_emulated(unsigned alpha)
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
    } else if (p4_rtt_alpha) {
        sceGuStencilFunc(GU_ALWAYS, alpha, 0xff); /* the draw's own alpha (stencil test off writes nothing, b331) */
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    } else {
        sceGuStencilFunc(GU_ALWAYS, 0xff, 0xff);
        sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
    }
    return 1;
}

static void bind_mask_mode(unsigned alpha, int fragment_alpha)
{
    /* GE alpha is stencil; textured 2D masks use draw_2d_alpha_mask below. */
    (void)fragment_alpha;
    if (date_emulated(alpha))
        return;
    st_pixel_mask(pixel_mask);
    if (pixel_mask) {
        st_enable(GU_STENCIL_TEST, 1);
        if (gu_mesh_mode && gs.stencil_known && gs.stencil_ref == alpha)
            return;
        if (gu_mesh_mode)
            gs.stencil_known = 1, gs.stencil_ref = alpha;
        if (p4_ge_raw) { /* b366: STST, SOP as libpspgu writes them */
            ge_word(0xDC, GU_ALWAYS | (alpha & 0xff) << 8 | 0xff << 16);
            ge_word(0xDD, GU_KEEP | GU_KEEP << 8 | GU_REPLACE << 16);
        } else {
            sceGuStencilFunc(GU_ALWAYS, alpha, 0xff);
            sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
        }
    } else {
        st_enable(GU_STENCIL_TEST, 0);
    }
}
static void bind_mask(unsigned alpha) { bind_mask_mode(alpha, p4_alpha_pixel); }

int func_003f5070(int state, int value)
{
    if (state >= 0 && state < MAX_STATE && render_state[state] != value) {
        render_state[state] = value;
        sl_state_dirty = 1;
    }
    return 1;
}

int func_003f5d90(int state, int *value)
{
    *value = state >= 0 && state < MAX_STATE ? render_state[state] : 0;
    return 1;
}

/* Pixels stay in CPU memory until a draw needs them: the PS2 raster formats are byte-for-byte the GE's, RGBA and low nibble first. */

/* b323 (fix from MeMeNT0xMoRii's fork, home23): the lock mode is remembered in privateFlags. The unlocks convert PS2
   data (alpha 0x80 -> 0xff, the palette's CSM1 order) once per write; a read-only lock and unlock used to convert it
   again: alpha doubled, the palette's middle groups swapped back. Native uploads call the unlocks directly. */
enum {
    RASTER_LOCK_WRITE = 0x01, RASTER_LOCK_READ = 0x02,
    PIXELS_LOCKED_READ = 0x02, PIXELS_LOCKED_WRITE = 0x04,
    PALETTE_LOCKED_READ = 0x08, PALETTE_LOCKED_WRITE = 0x10,
};
/* rwSTANDARDRASTERLOCK: (&pixels, raster, level << 8 | mode) */
int func_003fc5f0(unsigned char **pixels, Raster *r, int how)
{
    pipe_texture_wait();
    texture_generation++;
    unswizzle_raster(r);
    r->cpPixels = r->originalPixels;
    *pixels = r->cpPixels;
    if (!r->cpPixels)
        return 0;
    r->privateFlags |= (how & RASTER_LOCK_READ ? PIXELS_LOCKED_READ : 0) |
                       (how & RASTER_LOCK_WRITE ? PIXELS_LOCKED_WRITE : 0);
    return 1;
}

/* rwSTANDARDRASTERUNLOCK */
int func_003fd230(void *out, Raster *r)
{
    texture_generation++;
    int lock = r->privateFlags & (PIXELS_LOCKED_READ | PIXELS_LOCKED_WRITE);
    r->privateFlags &= ~(PIXELS_LOCKED_READ | PIXELS_LOCKED_WRITE);
    if (r->depth == 32 && lock != PIXELS_LOCKED_READ) { /* PS2 alpha tops out at 0x80 */
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
    if (!r->palette)
        return 0;
    r->privateFlags |= (how & RASTER_LOCK_READ ? PALETTE_LOCKED_READ : 0) |
                       (how & RASTER_LOCK_WRITE ? PALETTE_LOCKED_WRITE : 0);
    return 1;
}

int func_003fd520(void *out, Raster *r)
{
    texture_generation++;
    int lock = r->privateFlags & (PALETTE_LOCKED_READ | PALETTE_LOCKED_WRITE);
    r->privateFlags &= ~(PALETTE_LOCKED_READ | PALETTE_LOCKED_WRITE);
    if (lock == PALETTE_LOCKED_READ)
        return 1;
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
    if (!find_struct(stream, &length) || length != sizeof info)
        return 0;
    if (!p4r_003df360(stream, &info, length))
        return 0;
    /* b311 (fix from MeMeNT0xMoRii's fork): the one version-1 texture is the Town Map's 1024x1024 PSMT8 atlas, swizzled
       indices then the CLUT, no GIF packets. The GE binds at most 512 a side: decode it reduced. Until b310 it stayed
       blank. */
    int legacy = info.version < 2;
    if (legacy && (info.version != 1 || info.depth != 8 ||
                   info.width < 16 || info.width > 1024 || info.width % 16 ||
                   info.height < 16 || info.height > 1024 || info.height % 16 ||
                   info.pixelSize != (unsigned)(info.width * info.height) || info.paletteSize != 1024))
        return 0;
    int width = legacy && info.width > 512 ? 512 : info.width;
    int height = legacy && info.height > 512 ? 512 : info.height;
    int levels = G32(0xabf8 - 0x10000);
    G32(0xabf8 - 0x10000) = legacy ? 1 : (info.tex1[1] >> 2) + 1;
    G32(0xb8ac - 0x10000) = legacy;
    Raster *r = p4r_003ec590(width, height, info.depth, info.format);
    G32(0xabf8 - 0x10000) = levels;
    G32(0xb8ac - 0x10000) = 0;
    if (!r)
        return 0;
    unsigned char *blob = 0;
    if (find_struct(stream, &length) &&
        (!legacy || (length == info.pixelSize + info.paletteSize && r->palette)) &&
        (blob = memalign(16, length)) && p4r_003e2910(stream, blob, length) == length) {
        if (legacy) {
            ps2_index8_reduce(r->originalPixels, r->stride, r->width, r->height, blob, info.width, info.height);
            memcpy(r->palette, blob + info.pixelSize, info.paletteSize);
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
    clut32_written((const unsigned char *)destination, bytes); /* b375 */
    memcpy((void *)destination, (void *)source, bytes);
}

/* b311 (fix from MeMeNT0xMoRii's fork, P4-001: the CRI logo at boot was blank): the GE takes at most 512 texels a
   side. TMX quads give their screen size and UVs separately, so a larger image is point-sampled down (sdkUttmx.c
   through census FIXES). Forward copying also works in place for unpacked images. */
unsigned p4_tmx_texture_size(unsigned size)
{
    return size > 512 ? 512 : size;
}
void p4_tmx_reduce(void *destination, unsigned dst_stride, const void *source, unsigned src_stride,
                   unsigned width, unsigned height, int bits)
{
    unsigned w = p4_tmx_texture_size(width), h = p4_tmx_texture_size(height);
    const unsigned char *pixels = source;
    unsigned char *out = destination;
    for (unsigned y = 0; y < h; y++) {
        const unsigned char *src = pixels + (y * height / h) * src_stride;
        unsigned char *dst = out + y * dst_stride;
        if (bits == 4) {
            memset(dst, 0, dst_stride);
            for (unsigned x = 0; x < w; x++) {
                unsigned sx = x * width / w;
                unsigned index = src[sx / 2] >> (4 * (sx & 1)) & 15;
                dst[x / 2] |= index << (4 * (x & 1));
            }
        } else {
            unsigned bytes = bits / 8;
            for (unsigned x = 0; x < w; x++)
                memmove(dst + x * bytes, src + (x * width / w) * bytes, bytes);
        }
    }
}
void p4_tmx_copy_limited(void *destination, const void *source, unsigned width, unsigned height, int depth)
{
    unsigned bits = depth == 24 ? 32 : depth;
    unsigned src_stride = (width * bits + 7) / 8;
    unsigned dst_stride = (p4_tmx_texture_size(width) * bits + 7) / 8;
    p4_tmx_reduce(destination, dst_stride, source, src_stride, width, height, bits);
}

/* b376: from here on (2D, guard, mesh, skin, particles, far stand-ins, static lists, fx) every allocation is a
   cache: it goes to the arena (arena.h); free takes both arena and heap pointers. Rasters and the texture read above
   stay on the game's heap. */
#define malloc(n) p4a_malloc(n)
#define calloc(a, b) p4a_calloc(a, b)
#define memalign(a, n) p4a_memalign(a, n)
#define realloc(p, n) p4a_realloc(p, n)
#define free(p) p4a_free(p)
#include "im2d.h"
/* Atomics, without RenderWare's PS2 pipelines: the game's geometry is never native, its arrays are usable as they are. */
typedef struct { float x, y, z; } V3;
typedef struct { V3 right; unsigned flags; V3 up; unsigned pad1; V3 at; unsigned pad2; V3 pos; unsigned pad3; } Matrix;

/* The CRI splash (census FIX in code1_0012.c): the 640x448 picture of its 1024x512 atlas, squeezed to 4:3 and centred
   (from the persona4-portable fork, home25: it was stretched across the whole screen). */
void p4_draw_cri_logo(int texture)
{
    extern int sflDataDrawTexturedQuad(int x, int y, float z, int width, int height, int rgb, int alpha, int mode,
                                       short center_x, short center_y, Matrix *matrix, int texture, float (*uv)[2]);
    enum { ACTIVE_W = 640, ACTIVE_H = 448, ATLAS_W = 1024, ATLAS_H = 512 };
    const float width_scale = 4.0f * SCREEN_H / (3.0f * SCREEN_W);
    /* func_003e4320 transforms a vector (no translation): scale about the centre column, then move it back with x. */
    Matrix matrix = { .right = { width_scale, 0, 0 }, .up = { 0, 1, 0 }, .at = { 0, 0, 1 } };
    float uv[4][2] = {
        { 0, 0 }, { (float)ACTIVE_W / ATLAS_W, 0 },
        { 0, (float)ACTIVE_H / ATLAS_H }, { (float)ACTIVE_W / ATLAS_W, (float)ACTIVE_H / ATLAS_H },
    };
    sflDataDrawTexturedQuad(ACTIVE_W / 2, 0, 0, ACTIVE_W, ACTIVE_H, 0xffffff, 255, 1, ACTIVE_W / 2, 0, &matrix, texture, uv);
}
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

enum { CAMERA_PROJECTION = 0x14, CAMERA_VIEW = 0x20, CAMERA_NEAR = 0x80, CAMERA_FAR = 0x84, CAMERA_FOG = 0x88, ATOMIC_FRAME = 4, ATOMIC_GEOMETRY = 0x18 };
enum { GEOMETRY_NATIVE = 0x01000000, MESH_TRISTRIP = 1, MESH_PRIM_MASK = 0xf };

#include "clip.h"
#include "frustum.h"
#include "guard.h"
#include "mesh.h"
#include "hanim.h"
#include "skin.h"
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

#include "ptank.h"
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

int p4_cull_cover = 0; /* b254: off again, by the user's decision: far things get cheaper stand-ins (far_lod.h, the far skin level) instead of going. b247: on at 0.3% of the view on trial (user, 2026-11-14: Margaret in Heaven appearing from the middle of the bridge is fine; other scenes are being looked at). Drops skinned models too. 0 if any scene loses something the player must see. */
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
#include "far_lod.h"
static void ib_boundary(void) { ib_before(0); }
#include "static_list.h"

/* AtomicDefaultRenderCallBack */
static void *atomic_render_body(void *atomic);
static void *atomic_draw(void *atomic, Geometry *g, char *frame, const Matrix *model, const V3 *normals, int env);
void *func_003be9f0(void *atomic)
{
    if (SKIPPING)
        return atomic;
    prof_begin(PROF_ATOMIC);
    void *r = atomic_render_body(atomic);
    prof_end(PROF_ATOMIC);
    return r;
}

/* b268 RpMatFX environment map. The material's MatFX data (material + the plugin offset at 0x7646d0) holds two
   0x40-byte effect entries and the effect flags at +0x80; an env-map entry (+0x20 == 2) keeps the frame at +0,
   texture at +4, coefficient at +8 and the frame-buffer-alpha flag at +0xc (the game's accessors 0039a090-0039a400).
   PS2 GS dump (04/12 school route, the hero's clear umbrella): each env material's draw is followed at once by a
   context-2 pass over the same vertices with the env texture, ALPHA 0x68 with FIX 0x80 (Cs + Cd), no alpha test,
   depth GEQUAL and depth WRITE, vertex colour = the base pass's colour times about 0.1. The canopy itself fails its
   alpha test (RGB_ONLY, no depth), so this pass is what keeps the trees drawn later from painting over it.
   UVs come from the normals in view space (the env frame's space when it has one). */
int p4_matfx_env = 1;      /* 0: no env pass, as b267 */
int p4_matfx_env_skin = 1; /* a skinned atomic with an env map takes the CPU skin path, so the pass has its pose */
static unsigned matfx_env_atomics, matfx_env_draws, matfx_env_vertices, matfx_env_no_normals, matfx_env_ge_skin;
static unsigned matfx_env_skinned, matfx_env_skinned_lit; /* b359: env atomics that are skinned, and of them lit */
static const unsigned char *matfx_env_entry(const Material *material)
{
    if (!p4_matfx_env || !material || fp_owns_material(material))
        return 0;
    extern int iGpffffb5e0; /* RpMatFX's plugin offset in a material (0x7646d0) */
    const unsigned char *p = *(const unsigned char *const *)((const char *)material + iGpffffb5e0);
    if (!p)
        return 0;
    unsigned effects = *(const unsigned *)(p + 0x80);
    if (effects != 2 && effects != 3) /* env map, bump + env map */
        return 0;
    for (int i = 0; i < 2; i++) {
        const unsigned char *e = p + i * 0x40;
        if (*(const int *)(e + 0x20) == 2)
            return *(void *const *)(e + 4) ? e : 0;
    }
    return 0;
}
static int matfx_geometry_env(const Geometry *g)
{
    if (!p4_matfx_env)
        return 0;
    const Mesh *m = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    for (unsigned i = 0; i < g->mesh->numMeshes; i++)
        if (matfx_env_entry(m[i].material))
            return 1;
    return 0;
}
static void matfx_env_log(const Material *material, const unsigned char *e)
{
    static const void *seen[8];
    static int n;
    for (int i = 0; i < n; i++)
        if (seen[i] == material)
            return;
    if (n == 8)
        return;
    seen[n++] = material;
    void **texture = *(void ***)(e + 4);
    p4_log("matfx env material %08x colour %08x texture %08x raster %08x frame %08x coef %d/1000 fbalpha %d\n", (unsigned)material,
           material->colour, (unsigned)texture, texture ? (unsigned)*texture : 0, *(const unsigned *)e,
           (int)(*(const float *)(e + 8) * 1000), *(const int *)(e + 0xc));
}
static void matfx_env_pass(const Geometry *g, const V3 *positions, const V3 *normals, const Matrix *model)
{
    if (!normals) {
        matfx_env_no_normals++;
        return;
    }
    int count = g->numVertices, modulate = g->flags & 0x40;
    const MeshHeader *header = g->mesh;
    const Mesh *mesh = (const Mesh *)((const char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int prim = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP ? GU_TRIANGLE_STRIP : GU_TRIANGLES;
    const Matrix identity = { .right = { 1, 0, 0 }, .up = { 0, 1, 0 }, .at = { 0, 0, 1 } };
    const Matrix *m = model ? model : &identity;
    const V3 *mrow[3] = { &m->right, &m->up, &m->at };
    int ready = 0;
    for (unsigned i = 0; i < header->numMeshes; i++, mesh++) {
        const unsigned char *e = matfx_env_entry(mesh->material);
        if (!e || mesh->numIndices <= 0)
            continue;
        if (p4_probe_3d)
            matfx_env_log(mesh->material, e);
        if (vertex_used + count > MAX_VERTICES) {
            p4_log("TRACE vertex pool full\n");
            return;
        }
        /* rotation rows from object space to the env space: the camera's view, or the inverse of the env frame */
        const void *frame = *(void *const *)e;
        const Matrix *space = frame ? p4r_003e9700((void *)frame) : (const Matrix *)((char *)current_camera + CAMERA_VIEW);
        float r[3][3];
        for (int a = 0; a < 3; a++) {
            const V3 *row = mrow[a];
            if (frame) { /* orthonormal LTM: its inverse rotation is the transpose */
                r[a][0] = row->x * space->right.x + row->y * space->right.y + row->z * space->right.z;
                r[a][1] = row->x * space->up.x + row->y * space->up.y + row->z * space->up.z;
                r[a][2] = row->x * space->at.x + row->y * space->at.y + row->z * space->at.z;
            } else {
                r[a][0] = row->x * space->right.x + row->y * space->up.x + row->z * space->at.x;
                r[a][1] = row->x * space->right.y + row->y * space->up.y + row->z * space->at.y;
                r[a][2] = row->x * space->right.z + row->y * space->up.z + row->z * space->at.z;
            }
        }
        float coef = *(const float *)(e + 8);
        if (!(coef > 0))
            continue;
        if (coef > 1)
            coef = 1;
        unsigned material_colour = modulate ? mesh->material->colour | 0xff000000 : 0xffffffff;
        Vertex3D *v = vertex_pool + vertex_used;
        vertex_used += count;
        for (int j = 0; j < count; j++) {
            const V3 *n = &normals[j];
            float x = n->x * r[0][0] + n->y * r[1][0] + n->z * r[2][0];
            float y = n->x * r[0][1] + n->y * r[1][1] + n->z * r[2][1];
            float z = n->x * r[0][2] + n->y * r[1][2] + n->z * r[2][2];
            float l = x * x + y * y + z * z;
            float k = l > 1e-12f ? 0.5f / sqrtf(l) : 0;
            v[j].u = 0.5f + x * k;
            v[j].v = 0.5f + y * k;
            unsigned base = modulate_colour(g->preLit ? double_colour(g->preLit[j]) : 0xffffffff, material_colour);
            unsigned c = 0xff000000;
            for (int s = 0; s < 24; s += 8)
                c |= (unsigned)((base >> s & 0xff) * coef + 0.5f) << s;
            v[j].colour = c;
            v[j].x = positions[j].x;
            v[j].y = positions[j].y;
            v[j].z = positions[j].z;
        }
        sceKernelDcacheWritebackRange(v, count * sizeof *v);
        sceKernelDcacheWritebackRange(mesh->indices, mesh->numIndices * 2);
        if (!ready) {
            begin_frame();
            mesh_state(model, 0);
            ready = 1;
        }
        st_enable(GU_ALPHA_TEST, 0);
        st_enable(GU_DEPTH_TEST, 1);
        st_depth_func(GU_GEQUAL);
        st_depth_mask(GU_FALSE); /* depth write on, as the PS2's ZBUF_2 */
        st_enable(GU_STENCIL_TEST, 0);
        st_pixel_mask(pixel_mask | 0xff000000); /* our alpha byte is the stencil: leave it */
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
        gs.blend_known = 0;
        void **texture = *(void ***)(e + 4);
        bind_raster(*texture, GU_REPEAT);
        int strips = draw_strip_n;
        draw_strip_n = 0;
        gu_draw(prim, VT_3D, mesh->numIndices, mesh->indices, v);
        draw_strip_n = strips;
        matfx_env_draws++;
        matfx_env_vertices += count;
    }
}

void p4_matfx_report(void)
{
    if (matfx_env_atomics || matfx_env_no_normals || matfx_env_ge_skin)
        p4_log("matfx env %d atomics %u draws %u vertices %u no normals %u ge skin %u; on the GE %d: batches %u\n", p4_matfx_env, matfx_env_atomics,
               matfx_env_draws, matfx_env_vertices, matfx_env_no_normals, matfx_env_ge_skin, p4_matfx_env_ge, matfx_env_ge_draws);
    if (matfx_env_atomics)
        p4_log("matfx env skinned atomics %u (lit %u)\n", matfx_env_skinned, matfx_env_skinned_lit);
    matfx_env_ge_draws = matfx_env_skinned = matfx_env_skinned_lit = 0;
    matfx_env_atomics = matfx_env_draws = matfx_env_vertices = matfx_env_no_normals = matfx_env_ge_skin = 0;
}

#include "fx_direct.h"

static void *atomic_render_body(void *atomic)
{
    if (!list_room()) /* b325: the frame list is nearly full */
        return atomic;
    if ((p4_probe_flags & 16) && rtt_target) /* b327 diagnostic: nothing 3D drawn into camera textures */
        return atomic;
    if ((p4_probe_flags & 64) && rtt_target && !SKIN_OF_GEOMETRY(*(Geometry **)((char *)atomic + ATOMIC_GEOMETRY))) /* b333 diagnostic */
        return atomic;
    Geometry *g = *(Geometry **)((char *)atomic + ATOMIC_GEOMETRY);
    char *frame = *(char **)((char *)atomic + ATOMIC_FRAME);
    char *tank = PTANK_OF_ATOMIC(atomic);
    cull_atomic_flags = g ? (int)(g->flags & 0xffff) : -1;
    ib_before(g); /* b254: a batch of other atomics is drawn first, so the order holds */
    rtt_atomics += rtt_target != 0;
    if (tank && frame && current_camera) {
        draw_ptank(atomic, tank, g, frame);
        return atomic;
    }
    if (!g || !frame || !current_camera || (g->flags & GEOMETRY_NATIVE) || !g->mesh || !g->numVertices || !g->morphTarget || !g->morphTarget->verts) {
        atomics_skipped++;
        return atomic;
    }
    /* p4_fx_direct: effect geometry rebuilt every frame takes its own path (fx_direct.h) */
    if (p4_fx_direct && !shadow_shear_m && fx_marked(atomic)) {
        if (fx_draw(atomic, g, frame)) {
            atomics_drawn++;
            return atomic;
        }
        fx_fallback++;
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
    /* b362 diagnostic: 128 hides the skinned atomics (characters), 256 the others (scenery, props); logic and
       animation go on, only the draw is left out: the GE share of each (b360 PSP: half of everything 67 -> 50 ms) */
    if ((p4_probe_flags & (128 | 256)) && (p4_probe_flags & (SKIN_OF_GEOMETRY(g) ? 128 : 256))) {
        probe_hidden++;
        return atomic;
    }
    atomics_drawn++;
    if (rtt_target && (rtt_atomics & 255) <= 2) { /* b328: where a camera-texture atomic lands (clip x y w), its depth state */
        const Matrix *m = p4r_003e9700(frame);
        Mat4 vp;
        guard_matrix(vp, 0);
        float c[4];
        for (int k = 0; k < 4; k++)
            c[k] = m->pos.x * vp[0][k] + m->pos.y * vp[1][k] + m->pos.z * vp[2][k] + vp[3][k];
        p4_log("rtt atomic v%d skin%d clip %d %d w %d /1000 near %d far %d ztest %d zwrite %d test %x target %dx%d\n", g->numVertices,
               SKIN_OF_GEOMETRY(g) != 0, (int)(c[0] / c[3] * 1000), (int)(c[1] / c[3] * 1000), (int)(c[3] * 1000),
               (int)(*(float *)((char *)current_camera + CAMERA_NEAR) * 1000), (int)(*(float *)((char *)current_camera + CAMERA_FAR) * 1000),
               render_state[RS_ZTEST], render_state[RS_ZWRITE], sky_state[3], target_w, target_h);
    }
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
    /* b268: an env-mapped atomic needs its second pass right after it, so it skips the batched and prepared paths */
    prof_begin(PROF_AENV); /* b280: sections for the static atomic's way to its list (aenv aib afp asl) */
    int env = !shadow_shear_m && matfx_geometry_env(g);
    prof_end(PROF_AENV);
    matfx_env_atomics += env;
    if (!env) { /* b254 far_lod.h: repeated small scenery in one draw; far static scenery as one untextured mesh */
        int skinned = SKIN_OF_GEOMETRY(g) != 0;
        prof_begin(PROF_AIB);
        int taken = !p4_inst_call && ib_take(g, model, normals, skinned); /* b281: instances take the memo instead */
        if (!taken && ib.count)
            ib_flush();
        prof_end(PROF_AIB);
        if (taken)
            return atomic;
        if (!skinned && sl_memo_try(g, model, normals))
            return atomic;
        prof_begin(PROF_AFP);
        taken = fp_try(atomic, g, model, normals, skinned);
        prof_end(PROF_AFP);
        if (taken)
            return atomic;
        prof_begin(PROF_ASL);
        taken = !skinned && sl_try(atomic, g, model, normals); /* b258: one CALL to a prepared list */
        prof_end(PROF_ASL);
        if (taken)
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
    void *r = atomic_draw(atomic, g, frame, model, normals, env);
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

static void *atomic_draw(void *atomic, Geometry *g, char *frame, const Matrix *model, const V3 *normals, int env)
{
    const V3 *positions = g->morphTarget->verts;
    const V3 *env_normals = g->morphTarget->normals;
    churn_atomic_g = g, churn_atomic_locked = g->locked, churn_atomic_skinned = 0;
    const Skin *skin = SKIN_OF_GEOMETRY(g);
    const Hierarchy *hierarchy = HIERARCHY_OF_ATOMIC(atomic);
    if (skin && skin->skinToBone && hierarchy && hierarchy->matrices && !(hierarchy->flags & HIERARCHY_NO_MATRICES)) {
        int env_ge = env && p4_matfx_env_ge && !normals && g->morphTarget->normals; /* b359: the env pass on the GE */
        matfx_env_skinned += env != 0, matfx_env_skinned_lit += env && normals;
        if (p4_ge_skin && (!normals || g->morphTarget->normals) && !(env && p4_matfx_env_skin && !env_ge)) {
            prof_begin(PROF_GESKIN);
            int done = draw_skinned_ge(g, skin, hierarchy, model, normals != 0, g->flags & 0x40, env_ge);
            prof_end(PROF_GESKIN);
            if (done) {
                skinned_drawn++;
                matfx_env_ge_skin += env && !env_ge; /* the posed vertices stay on the GE: no env pass (b359: unless env_ge) */
                return atomic;
            }
        }
        const V3 *skinned = skin_vertices(g, skin, hierarchy, 0);
        if (skinned) {
            positions = skinned;
            churn_atomic_skinned = 1;
            if (normals) normals = skin_vertices(g, skin, hierarchy, 1);
            env_normals = normals ? normals : env && env_normals ? skin_vertices(g, skin, hierarchy, 1) : 0;
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
    if (env)
        matfx_env_pass(g, positions, env_normals, model);
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
/* b323 (from MeMeNT0xMoRii's fork, P4-032: Shuffle Time showed only the blue backs): pscModelDraw and Shuffle Time's
   non-indexed draws are scoped by census hooks; inside them the cards keep the back-face culling the retail asks for. */
int p4_im3d_cull = 1;
static int shuffle_card;
int p4_field_shadow = 1; /* b330: 0: the field shadow quad (non-indexed Im3D) is not drawn, as before */
static int shuffle_nonindexed;
void p4_shuffle_card_begin(int active)
{
    shuffle_card = active;
}
int p4_shuffle_render_primitive(int primitive)
{
    extern int func_004106a0(int);
    int previous = shuffle_nonindexed;
    shuffle_nonindexed = 1;
    int result = func_004106a0(primitive);
    shuffle_nonindexed = previous;
    return result;
}
extern char p4r_00410420[], p4r_004104d0[], p4r_00410520[], p4r_004106a0[], p4r_00410800[];
static int im3d_caller(const char *ra, int atomic)
{
    /* Field shadow projection also calls the non-indexed entry: the direct draw stays scoped to Shuffle Time's calls. */
    if (ra >= p4r_004106a0 && ra < p4r_00410800) /* b330: and the field shadow, the quad textured with its camera texture */
        return shuffle_nonindexed || (p4_field_shadow && rtt_find((const Raster *)render_state[1]));
    return (!atomic || shuffle_card) &&
           ((ra >= p4r_00410420 && ra < p4r_004104d0) || (ra >= p4r_00410520 && ra < p4r_004106a0));
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
    if (!list_room()) /* b325 */
        return;
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
    RttSlot *shadow = r ? rtt_find(r) : 0;
    bind_raster(r, shadow ? GU_CLAMP : GU_REPEAT); /* b331: the field shadow's UVs run past 0..1; the PS2 clamps */
    if (shadow && shadow->fade) { /* b332: the strength is in the texture's RGB (im2d.h shadow_fade): floor x (1 - s) */
        st_enable(GU_BLEND, 1);
        sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_COLOR, 0, 0);
        gs.blend_known = 0;
    }
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
    if (r && rtt_find(r) && !(im3d_draws & 255)) { /* b331: the field shadow quad */
        const Im3DVertex *v = (const Im3DVertex *)im3d_elements;
        p4_log("im3d rtt quad type %u n %d count %u ztest %d zwrite %d test %x ltm %d\n", type, n, count, render_state[RS_ZTEST],
               render_state[RS_ZWRITE], sky_state[3], ltm != 0);
        for (unsigned i = 0; i < count && i < 9; i++, v = (const Im3DVertex *)((const char *)v + im3d_stride))
            p4_log("  v %d %d %d c %08x uv %d %d /1000\n", (int)v->x, (int)v->y, (int)v->z, v->colour, (int)(v->u * 1000), (int)(v->v * 1000));
    }
    /* The blue back (PSC_BASIC entry 7) and the illustrated layers are coplanar with opposite winding: with culling off
       the back paints over the front. The GE state is left as the rest of the port expects it after the draw. */
    /* b386 p4_im3d_cull 1: every Im3D draw with cull mode 2, not only Shuffle Time's (Im3D keeps the game's winding:
       our strips never touch it). The Velvet Room's fusion cards (~15 Im3D quads a frame) showed no faces, as the
       Shuffle cards did before b?: back and face coplanar. 0: Shuffle cards only, as before. */
    int cull_cards = (p4_im3d_cull || shuffle_card || shuffle_nonindexed) && render_state[RS_CULLMODE] == 2;
    if (cull_cards) {
        sceGuFrontFace(GU_CCW);
        st_enable(GU_CULL_FACE, 1);
    }
    draw_tested(prims[type], idx ? VT_3D : VT_3D & ~GU_INDEX_BITS, n, idx, out);
    if (cull_cards)
        st_enable(GU_CULL_FACE, 0);
    im3d_draws++;
    im3d_vertices += count;
}

void *func_004114d0(void *pipeline, void *object, int force)
{
    const char *caller = __builtin_return_address(0);
    int atomic = *(unsigned char *)object == OBJECT_ATOMIC;
    if (p4_im3d && im3d_caller(caller, atomic)) {
        if (SKIPPING)
            return object;
        ib_before(0);
        if (caller < p4r_004104d0)
            im3d_transform(object);
        else
            im3d_draw(object);
        return object;
    }
    if (atomic)
        return func_003be9f0(object);
    pipeline_other++;
    other_note(pipeline, object, caller);
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
    ib_before(0);
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
    gate_at_present = 1; /* b364: the GE must reach the end before the sync below */
    gate_poll();
    gate_at_present = 0;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    guard_result_clear();
    ptank_spill_collect(1);
    cloud_clear();
    unsigned released = cache_bytes + skin_ge_bytes;
    for (int i = 0; i < CACHE_SLOTS; i++) free(mesh_cache[i].v);
    memset(mesh_cache, 0, sizeof mesh_cache);
    cache_bytes = 0;
    if (skin_cache)
        for (int i = 0; i < SKIN_GEOMETRIES; i++) skin_free(&skin_cache[i]);
    /* b375: the prepared static lists, far stand-ins and CLUT8 copies too (b373: 13 MB of caches, 9.5 MB of them
       never released, while a battle load was refused 545 KB) */
    unsigned lists = sl_release_all(), proxies = fp_release_all(), cluts = clut32_release_all();
    released += lists + proxies + cluts;
    p4_log("video renderer idle, released %u bytes of caches (static lists %u, far stand-ins %u, clut8 %u)\n", released, lists, proxies, cluts);
    return 0;
}

/* b340 (issue #12): the last resort of main.c's malloc wrappers for a big block (F023_003.P01 wanted 1.89 MB with
   12 MB free in pieces): every mesh and skin cache entry freed with the GE idle, as before a movie. Only on the
   drawing thread. Returns the bytes freed. */
void p4_video_renderer_end(void);
unsigned p4_heap_release_all(void)
{
    if (cache_thread < 0 || sceKernelGetThreadId() != cache_thread)
        return 0;
    unsigned before = cache_bytes + skin_ge_bytes;
    if (p4_video_renderer_begin() < 0)
        return 0;
    p4_video_renderer_end();
    return before;
}

void p4_video_renderer_end(void)
{
    fps_window_valid = fps_frames = fps_tenths = 0;
    skip_frame = skip_run = 0;
    skip_debt_us = 0;
    skip_last_us = sceKernelGetSystemTimeLow();
    feedback_captured = 0;
    texture_generation++;
    gu_dirty();
}
