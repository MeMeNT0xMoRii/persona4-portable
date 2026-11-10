/* Boot: map the rebased retail data at its fixed PSP address, prove the pointers landed, enter the game. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspsdk.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("P4", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-1); /* the heap is claimed before main */
PSP_HEAP_THRESHOLD_SIZE_KB(4096); /* left free: thread stacks */

#define BUILD 165
#define P4_IMAGE_SIZE 0x40B000 /* retail 0x52da00..0x938a00: data and bss */
#define GAME_DIR "ms0:/PSP/GAME/P4/"
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
static void log_commit(const char *a, int n, const char *b, int m)
{
    SceUID fd = sceIoOpen(GAME_DIR LOG_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
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

int main(void)
{
    static unsigned list[8192];
    __asm__ volatile("ctc1 $0, $31"); /* FPU traps off: on hardware a NaN compare powers the console off */
    __asm__ volatile("move $gp, %0" : : "r"(P4_GP)); /* reassembled code reads small data off $gp; threads inherit it */
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
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
    int data = load(GAME_DIR DATA_FILE, got, P4_IMAGE_SIZE);
    int count = load(GAME_DIR "p4str.bin", list, sizeof list) / 4;
    p4_log("loaded data=%d list=%d\n", data, count);
    if (data <= 0 || count <= 0 || check_strings(list, count)) {
        stop();
        return 1;
    }
    sceKernelDcacheWritebackAll();
    log_start();
    p4_log("entering kwlnBoot free=%d KB\n", (int)sceKernelTotalFreeMemSize() / 1024);
    int result = kwlnBoot();
    void p4_video_shutdown(void);
    p4_video_shutdown();
    p4_log("kwlnBoot returned %d\n", result);
    stop();
    return 0;
}
