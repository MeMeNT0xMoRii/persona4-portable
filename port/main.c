/* Boot: map the rebased retail data at its fixed PSP address, prove the pointers landed, enter the game. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspsdk.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <pspge.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("P4", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-1); /* the heap is claimed before main */
PSP_HEAP_THRESHOLD_SIZE_KB(4096); /* left free: thread stacks */

#define BUILD 340
#define P4_IMAGE_SIZE 0x40B000 /* retail 0x52da00..0x938a00: data and bss */
#include "paths.h"
#define GAME_DIR "ms0:/PSP/GAME/P4/" /* only until p4_paths_init: the log before it */
#ifdef P4_HW_PROFILE
#define DATA_FILE "p4data.hwdbg.bin"
#define LOG_FILE "p4_hwdbg.log"
#else
#define DATA_FILE "p4data.bin"
#define LOG_FILE "p4.log"
#endif
#define MAX_MISSING 512
#define P4_STR2(x) #x
#define P4_STR(x) P4_STR2(x)

/* The data image is part of the ELF, at P4_IMAGE, so the loader reserves it with the code. A fixed address high in
   the partition exists only on PPSSPP: hardware's 64 MB mode ends near 0x0BB00000, with the main stack below that. */
__asm__(".section .p4image,\"aw\",@nobits\n.globl p4_image\np4_image:\n.space " P4_STR(P4_IMAGE_SIZE) "\n.text\n");

int kwlnBoot(void);

int p4_quiet; /* the game owns the screen once the GE is up */

/* A log that is only flushed is empty after a crash: open, append, close for every commit. That costs ~17 ms per
   sceIoOpen on hardware, so the game's threads only copy lines into a ring and a writer thread commits it every
   100 ms. Before the writer starts (boot) and with p4_log_sync set, lines are committed at once as before.
   A crash loses at most the last 100 ms; the ring (p4_log_ring) can still be read with savemem. */
#define LOG_RING (64 * 1024)
void p4_log(const char *fmt, ...);
char p4_log_ring[LOG_RING];
static volatile unsigned log_head, log_tail, log_dropped;
static int log_thread_running;
int p4_log_sync;

/* The file first: a PSPLink stdout write blocks while the USB cable is out (b111: thousands of reconnects an evening),
   and the log on the memory stick must keep going meanwhile. */
/* b321 (from MeMeNT0xMoRii's fork): files live in the folder the EBOOT was launched from (paths.c), so the game
   also runs from CAT_ folders; the log starts in the default folder until that is known */
static char log_path[P4_PATH_MAX] = GAME_DIR LOG_FILE;
static void log_commit(const char *a, int n, const char *b, int m)
{
    SceUID fd = sceIoOpen(log_path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) {
        if (n) sceIoWrite(fd, a, n);
        if (m) sceIoWrite(fd, b, m);
        sceIoClose(fd);
    }
#ifdef P4_HW_PROFILE
    if (n) sceIoWrite(1, a, n);
    if (m) sceIoWrite(1, b, m);
#endif
}

/* Writes everything in the ring up to the head seen on entry; only the writer (or a sync caller) moves the tail. */
static void log_drain(void)
{
    static int busy;
    int intr = pspSdkDisableInterrupts();
    int mine = !busy;
    busy = 1;
    pspSdkEnableInterrupts(intr);
    if (!mine)
        return; /* the writer is on it */
    unsigned head = log_head, tail = log_tail;
    if (head == tail) {
        busy = 0;
        return;
    }
    unsigned from = tail % LOG_RING, n = head - tail;
    unsigned first = n < LOG_RING - from ? n : LOG_RING - from;
    log_commit(p4_log_ring + from, first, p4_log_ring, n - first);
    log_tail = head;
    if (log_dropped) {
        char line[64];
        int k = snprintf(line, sizeof line, "b%d log ring full, dropped %u lines\n", BUILD, log_dropped);
        log_dropped = 0;
        log_commit(line, k, 0, 0);
    }
    busy = 0;
}

static int log_writer(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        sceKernelDelayThread(100 * 1000);
        log_drain();
    }
    return 0;
}

static void log_start(void)
{
    /* above the game's threads so a busy frame cannot starve it; it sleeps or waits on the stick otherwise */
    SceUID id = sceKernelCreateThread("p4_log", log_writer, 0x18, 16 * 1024, PSP_THREAD_ATTR_USER, 0);
    if (id >= 0 && sceKernelStartThread(id, 0, 0) >= 0)
        log_thread_running = 1;
    else
        p4_log("log writer thread failed %x, logging stays synchronous\n", id);
}

void p4_log_flush(void)
{
    log_drain();
}

/* Memory census (b166): psp_link wraps malloc/calloc/realloc/memalign/free; live bytes are counted per caller (return
   address, named by psp_log.py). The game's own allocations arrive through chkmemAllocate and RenderWare's malloc
   wrapper, the port's from its own functions. Blocks libc allocates internally are not seen. */
#include <malloc.h>
#include <unistd.h>
int p4_memstat = 0; /* 1 for a census build: it must be on from boot, the table takes 768 KB of heap */
void *__real_malloc(size_t); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
void *__real_memalign(size_t, size_t); void __real_free(void *);
enum { MS_SLOTS = 1 << 16, MS_SITES = 1024 };
typedef struct { void *p; unsigned size, site; } MsBlock;
typedef struct { unsigned ra, live, peak, blocks; } MsSite;
static MsBlock *ms_table; /* open addressing, backward-shift deletion */
static MsSite ms_site[MS_SITES], ms_copy[MS_SITES];
static unsigned ms_used, ms_live, ms_peak, ms_lost, ms_allocs;

static unsigned ms_hash(unsigned v, unsigned bits) { return (v >> 3) * 2654435761u >> (32 - bits); }

static void ms_add(void *p, unsigned size, unsigned ra)
{
    if (!p || !p4_memstat)
        return;
    if (!ms_table && !(ms_table = __real_calloc(MS_SLOTS, sizeof *ms_table))) /* not under the interrupt lock: malloc may wait */
        p4_memstat = 0;
    int intr = pspSdkDisableInterrupts();
    if (!ms_table || ms_used >= MS_SLOTS / 8 * 7) {
        ms_lost++;
        pspSdkEnableInterrupts(intr);
        return;
    }
    unsigned s = ms_hash(ra, 10);
    while (ms_site[s].ra && ms_site[s].ra != ra)
        s = (s + 1) & (MS_SITES - 1);
    ms_site[s].ra = ra;
    if ((ms_site[s].live += size) > ms_site[s].peak)
        ms_site[s].peak = ms_site[s].live;
    ms_site[s].blocks++;
    if ((ms_live += size) > ms_peak)
        ms_peak = ms_live;
    ms_allocs++;
    unsigned i = ms_hash((unsigned)p, 16);
    while (ms_table[i].p)
        i = (i + 1) & (MS_SLOTS - 1);
    ms_table[i] = (MsBlock){ p, size, s };
    ms_used++;
    pspSdkEnableInterrupts(intr);
}

static void ms_remove(void *p)
{
    if (!p || !ms_table)
        return;
    int intr = pspSdkDisableInterrupts();
    unsigned i = ms_hash((unsigned)p, 16);
    while (ms_table[i].p && ms_table[i].p != p)
        i = (i + 1) & (MS_SLOTS - 1);
    if (ms_table[i].p) {
        MsSite *site = &ms_site[ms_table[i].site];
        site->live -= ms_table[i].size;
        site->blocks--;
        ms_live -= ms_table[i].size;
        ms_used--;
        for (unsigned j = i;;) { /* pull later entries of the run back over the hole */
            j = (j + 1) & (MS_SLOTS - 1);
            if (!ms_table[j].p)
                break;
            unsigned k = ms_hash((unsigned)ms_table[j].p, 16);
            if (i <= j ? i < k && k <= j : i < k || k <= j)
                continue;
            ms_table[i] = ms_table[j];
            i = j;
        }
        ms_table[i].p = 0;
    }
    pspSdkEnableInterrupts(intr);
}

#define MS_RA ((unsigned)__builtin_return_address(0))
/* b272: an allocation that fails gets one retry after the animation tracks (rwsky.c anim_tracks.h) give their memory
   back: the game writes through the null block of a failed chkmemAllocate (b271 bus error loading a dialogue) */
unsigned p4_anim_tracks_release(void);
unsigned p4_mesh_cache_release(void);
/* b274: the mesh cache too (rwsky.c). b275: logs the heap before and after (free total and the largest block). */
unsigned p4_heap_unclaimed(void);
void __real_free(void *);
/* b275: the largest block malloc can give now (binary search, 64 KB steps) */
static unsigned heap_largest(void)
{
    unsigned lo = 0, hi = 32u << 20;
    while (hi - lo > 65536) {
        unsigned mid = (lo + hi) / 2;
        void *p = __real_malloc(mid);
        if (p)
            __real_free(p), lo = mid;
        else
            hi = mid;
    }
    return lo;
}
static unsigned heap_release(unsigned n)
{
    struct mallinfo a = mallinfo();
    unsigned free_before = a.fordblks + p4_heap_unclaimed(), big_before = heap_largest();
    unsigned freed = p4_anim_tracks_release();
    freed += p4_mesh_cache_release();
    if (n >= 1u << 20 && heap_largest() < n + 65536) { /* b340 (issue #12): still no run that size: every cache */
        extern unsigned p4_heap_release_all(void);
        freed += p4_heap_release_all();
    }
    struct mallinfo b = mallinfo();
    p4_log("heap short: request %u, free %u largest %u; released %u, now free %u largest %u, arena %u\n", n, free_before,
           big_before, freed, b.fordblks + p4_heap_unclaimed(), heap_largest(), (unsigned)b.arena);
    p4_log_flush();
    return freed;
}
void *__wrap_malloc(size_t n)
{
    void *p = __real_malloc(n);
    if (!p && n && heap_release(n))
        p = __real_malloc(n);
    ms_add(p, n, MS_RA);
    return p;
}
void *__wrap_calloc(size_t a, size_t b)
{
    void *p = __real_calloc(a, b);
    if (!p && a && b && heap_release(a * b))
        p = __real_calloc(a, b);
    ms_add(p, a * b, MS_RA);
    return p;
}
void *__wrap_memalign(size_t a, size_t n)
{
    void *p = __real_memalign(a, n);
    if (!p && n && heap_release(n))
        p = __real_memalign(a, n);
    ms_add(p, n, MS_RA);
    return p;
}
void __wrap_free(void *p) { ms_remove(p); __real_free(p); }
void *__wrap_realloc(void *old, size_t n)
{
    void *p = __real_realloc(old, n);
    if (!p && n && heap_release(n))
        p = __real_realloc(old, n);
    if (p || !n) {
        ms_remove(old);
        ms_add(p, n, MS_RA);
    }
    return p;
}

/* Heap the allocator has not taken from sbrk yet. mallinfo's fordblks counts only the arena already taken: in the b166
   census the battle had 88 KB "free" there with ~5.7 MB of the heap block never touched. libcglue keeps the block's top
   in a local static, so it is found once by asking sbrk (which refuses past the top), under malloc's lock. */
void __malloc_lock(struct _reent *); void __malloc_unlock(struct _reent *);
unsigned p4_heap_unclaimed(void)
{
    static char *top;
    __malloc_lock(_REENT);
    char *now = sbrk(0);
    if (!top) {
        unsigned lo = 0, hi = 64u << 20; /* lo fits, hi does not */
        while (hi - lo > 4096) {
            unsigned mid = (lo + hi) / 2 & ~15u;
            if (sbrk(mid) != (void *)-1) {
                sbrk(-(int)mid);
                lo = mid;
            } else
                hi = mid;
        }
        top = now + lo;
    }
    __malloc_unlock(_REENT);
    return top > now ? top - now : 0;
}

/* every 256 frames (rwsky.c): totals, then the 24 callers holding the most */
void p4_memstat_report(void)
{
    if (!ms_table)
        return;
    struct mallinfo m = mallinfo();
    int intr = pspSdkDisableInterrupts();
    memcpy(ms_copy, ms_site, sizeof ms_copy);
    unsigned live = ms_live, peak = ms_peak, used = ms_used, lost = ms_lost, allocs = ms_allocs;
    pspSdkEnableInterrupts(intr);
    p4_log("memstat live %u KB peak %u KB blocks %u allocs %u lost %u table %u KB\n", live >> 10, peak >> 10, used, allocs, lost,
           (unsigned)(MS_SLOTS * sizeof(MsBlock)) >> 10);
    p4_log("memstat heap arena %d KB used %d KB free %d KB unclaimed %u KB | sys free %d KB\n", m.arena >> 10, m.uordblks >> 10,
           m.fordblks >> 10, p4_heap_unclaimed() >> 10, (int)sceKernelTotalFreeMemSize() >> 10);
    for (int n = 0; n < 24; n++) {
        int best = -1;
        for (int s = 0; s < MS_SITES; s++)
            if (ms_copy[s].ra && (best < 0 || ms_copy[s].live > ms_copy[best].live))
                best = s;
        if (best < 0 || !ms_copy[best].live)
            break;
        p4_log("memsite %6u KB peak %6u KB blocks %5u from %07x\n", ms_copy[best].live >> 10, ms_copy[best].peak >> 10,
               ms_copy[best].blocks, ms_copy[best].ra - 8);
        ms_copy[best].live = 0;
    }
}

void p4_log(const char *fmt, ...)
{
    char line[160];
    va_list args;
    int n = snprintf(line, sizeof line, "b%d ", BUILD);
    va_start(args, fmt);
    n += vsnprintf(line + n, sizeof line - n, fmt, args);
    va_end(args);
    if (n < 0)
        return;
    if (n >= (int)sizeof line)
        n = sizeof line - 1;
    if (!p4_quiet)
        pspDebugScreenPrintf("%s", line);
    int intr = pspSdkDisableInterrupts();
    unsigned head = log_head;
    int fits = head + n - log_tail <= LOG_RING;
    if (fits) {
        for (int i = 0; i < n; i++)
            p4_log_ring[(head + i) % LOG_RING] = line[i];
        log_head = head + n;
    } else {
        log_dropped++;
    }
    pspSdkEnableInterrupts(intr);
    if (!log_thread_running || p4_log_sync)
        log_drain();
}

static void stop(void)
{
    p4_log_flush();
    sceKernelDelayThread(2 * 1000 * 1000);
    sceKernelExitGame();
}

/* A function the port does not have yet: log it once with its caller, keep going while the list is short.
   The stubs tail-call this, so the return address is the game's. */
void p4_backtrace(void);
int p4_missing(const char *name)
{
    void *from = __builtin_return_address(0);
    static const char *seen[MAX_MISSING];
    static int count;
    for (int i = 0; i < count; i++)
        if (seen[i] == name)
            return 0;
    p4_log("missing %s from %x\n", name, (unsigned)from);
    p4_backtrace();
    seen[count++] = name;
    if (count == MAX_MISSING)
        count--;
    return 0;
}

/* Return addresses left on the stack, newest first: reassembled frames defeat PPSSPP's walker. */
void p4_backtrace(void)
{
    extern char __executable_start[], __etext[];
    unsigned *sp = __builtin_frame_address(0);
    for (int i = 0, n = 0; i < 2048 && n < 16 && (unsigned)(sp + i) < 0x0BFFFF00u; i++) {
        unsigned w = sp[i];
        if (w > (unsigned)__executable_start && w < (unsigned)__etext && !(w & 3) && *(unsigned *)(w - 8) >> 26 == 3) {
            p4_log("  from %x\n", w);
            n++;
        }
    }
}

/* ponytail: the statement is skipped, what it would have written stays stale; each one is logged once to be translated. */
void p4_ee_asm_trap(const char *file, int line)
{
    static const char *seen[64];
    static int seen_line[64], count;
    for (int i = 0; i < count; i++)
        if (seen_line[i] == line && seen[i] == file)
            return;
    p4_log("skipped asm %s:%d\n", file, line);
    if (count < 64) {
        seen[count] = file;
        seen_line[count++] = line;
    }
}

static int load(const char *path, void *dst, int max)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return fd;
    int n = sceIoRead(fd, dst, max);
    sceIoClose(fd);
    return n;
}

/* Every checked word must point inside the image, at text that follows a NUL or a rebased pointer. */
static int check_strings(const unsigned *list, int count)
{
    const unsigned end = P4_IMAGE + P4_IMAGE_SIZE;
    int bad = 0, first_bad = -1;
    for (int i = 0; i < count; i++) {
        unsigned p = *(unsigned *)(P4_IMAGE + list[i]);
        const unsigned char *s = (const unsigned char *)p;
        /* s[-1] may be the top byte of a rebased pointer that sits right before the text */
        if (p <= P4_IMAGE || p >= end || (s[-1] != 0 && s[-1] != P4_IMAGE >> 24) || s[0] == 0) {
            if (first_bad < 0)
                first_bad = (int)(0x52DA00 + list[i]);
            bad++;
        }
    }
    p4_log("strings checked=%d bad=%d first_bad=%x\n", count, bad, first_bad);
    return bad;
}

/* HOME (PS) button: the system menu offers "Quit game" only once an exit callback is registered. The firmware stops
   the game's threads, audio and the Media Engine itself; the callback only lets a save in progress finish (up to 5 s)
   and commits the log. */
extern volatile int p4_mc_writing;

static int exit_callback(int a, int b, void *c)
{
    (void)a; (void)b; (void)c;
    for (int i = 0; p4_mc_writing && i < 500; i++)
        sceKernelDelayThread(10 * 1000);
    /* b321 (from MeMeNT0xMoRii's fork, HOME froze on a PSP Go): HOME can pause the game with a frame stopped at its
       GE stall address; a list that is not done is broken off before the exit */
    int ge = sceGeDrawSync(1), broke = ge > PSP_GE_LIST_DONE ? sceGeBreak(1, 0) : 0;
    p4_log("HOME: exit to XMB (GE state %d, break %d)\n", ge, broke);
    p4_log_flush();
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("p4_exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static void exit_callback_start(void)
{
    SceUID id = sceKernelCreateThread("p4_exit", callback_thread, 0x11, 4 * 1024, PSP_THREAD_ATTR_USER, 0);
    if (id < 0 || sceKernelStartThread(id, 0, 0) < 0)
        p4_log("exit callback thread failed %x, HOME will not quit\n", id);
}

int main(int argc, char **argv)
{
    static unsigned list[4096]; /* b250: 8192 -> 4096 words (the image was 856 bytes over); p4str.bin is 2476 words, a full buffer stops the boot below */
    __asm__ volatile("ctc1 $0, $31"); /* FPU traps off: on hardware a NaN compare powers the console off */
    __asm__ volatile("move $gp, %0" : : "r"(P4_GP)); /* reassembled code reads small data off $gp; threads inherit it */
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    {
        char cwd[P4_PATH_MAX];
        /* a launch from elsewhere (PSPLink's host0:) keeps the old fixed folder */
        if (p4_paths_init(argc > 0 ? argv[0] : NULL, getcwd(cwd, sizeof cwd)) < 0 && p4_paths_init(NULL, "ms0:/PSP/GAME/P4") < 0) {
            pspDebugScreenPrintf("Cannot resolve the game folder from the launch path.\n");
            sceKernelDelayThread(5 * 1000 * 1000);
            sceKernelExitGame();
            return 1;
        }
        snprintf(log_path, sizeof log_path, "%s%s", p4_game_dir, LOG_FILE);
        p4_log("game folder %s\n", p4_game_dir);
    }
    { /* b321: an empty FPS file next to the EBOOT turns the frame-rate overlay on (rwsky.c) */
        extern int p4_fps_overlay;
        SceIoStat st;
        p4_fps_overlay = sceIoGetstat(p4_path("FPS"), &st) >= 0;
    }
#ifdef P4_HW_PROFILE
    extern char p4_image[];
    if ((unsigned)p4_image != P4_IMAGE) {
        p4_log("hwdbg wrong load base: image=%08x expected=%08x\n", (unsigned)p4_image, P4_IMAGE);
        stop();
        return 1;
    }
    p4_log("hwdbg b%d PRX image=%08x\n", BUILD, (unsigned)p4_image);
#endif
    void *got = (void *)P4_IMAGE;
    unsigned stack = (unsigned)__builtin_frame_address(0);
    p4_log("P4 boot free=%d KB maxfree=%d KB stack=%x\n", (int)sceKernelTotalFreeMemSize() / 1024, (int)sceKernelMaxFreeMemSize() / 1024, stack);
    if (stack < 0x0A000000u) { /* the main stack sits at the top of the partition: this is the 24 MB one */
        p4_log("Extra memory is off. Enable the custom firmware's extra memory setting and check the memory stick.\n");
        p4_log("The PSP-1000 has no extra memory and is not supported.\n");
        p4_log_flush();
        sceKernelDelayThread(8 * 1000 * 1000);
        sceKernelExitGame();
        return 1;
    }
    memset(got, 0, P4_IMAGE_SIZE);
    int data = load(p4_path(DATA_FILE), got, P4_IMAGE_SIZE);
    int count = load(p4_path("p4str.bin"), list, sizeof list) / 4;
    p4_log("loaded data=%d list=%d\n", data, count);
    if (count >= (int)(sizeof list / 4))
        p4_log("p4str.bin fills the list of %d words: enlarge it in main()\n", (int)(sizeof list / 4));
    if (data <= 0 || count <= 0 || count >= (int)(sizeof list / 4) || check_strings(list, count)) {
        stop();
        return 1;
    }
    sceKernelDcacheWritebackAll();
    log_start();
    exit_callback_start();
    p4_log("entering kwlnBoot free=%d KB\n", (int)sceKernelTotalFreeMemSize() / 1024);
    int result = kwlnBoot();
    void p4_video_shutdown(void);
    p4_video_shutdown();
    p4_log("kwlnBoot returned %d\n", result);
    stop();
    return 0;
}
