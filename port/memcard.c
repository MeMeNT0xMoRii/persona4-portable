/* Keep the game's serialization and card UI; replace the IOP file commands with PSP files. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef __psp__
#include <pspiofilemgr.h>
#include <pspthreadman.h>
extern void p4_log(const char *fmt, ...);
#endif
#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#ifndef P4_CARD_ROOT
#ifdef __psp__
#include "paths.h"
#define P4_CARD_ROOT p4_save_dir
#else
#define P4_CARD_ROOT "saves"
#endif
#endif

enum { MC_MISSING = (int)0x80000002u, MC_IO = (int)0x80000013u };
static int pending, command, result;
/* b150: the save screen ran ~7 FPS, mcProc 17 Mcycles a frame: every card call made the two save directories again
   (mkdir + stat on the memory stick). Once they exist they are taken as existing; calls are counted for the log. */
static int root_ok, card_ok[2];
unsigned p4_mc_calls[6], p4_mc_bytes;

static int sync_card(void)
{
#ifdef __psp__
    return sceIoSync("ms0:", 0) >= 0;
#else
    return 1;
#endif
}

static int complete(int cmd, int value)
{
    command = cmd;
    result = value;
    pending = 1;
    return 0;
}

int sceMc2CheckAsync(int *cmd, int *value)
{
    if (!pending)
        return -1;
    *cmd = command;
    *value = result;
    pending = 0;
    return 1;
}

static int directory(const char *path)
{
    struct stat st;
    if (mkdir(path, 0777) == 0)
        return 1;
    return errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int card_path(char *out, int socket, const char *name)
{
    char root[256];
    if (socket < 0 || socket > 1 || !name || strchr(name, ':') || strchr(name, '\\') || strstr(name, ".."))
        return 0;
    while (*name == '/') name++;
    if (!*name || !(root_ok || (root_ok = directory(P4_CARD_ROOT))))
        return 0;
    if (snprintf(root, sizeof root, "%s/card%d", P4_CARD_ROOT, socket) >= sizeof root || !(card_ok[socket] || (card_ok[socket] = directory(root))))
        return 0;
    return snprintf(out, 256, "%s/%s", root, name) < 256;
}

int sceMc2GetInfoAsync(int socket, int *info)
{
    p4_mc_calls[0]++;
    char path[256];
    int ok = card_path(path, socket, "probe");
    info[0] = ok ? 2 : 0;
    info[1] = ok ? 1 : 0;
    info[2] = ok ? 8192 : 0;
    return complete(1, ok ? 0 : MC_IO);
}

/* b259: from b252 on (strip pack on) the title's save check failed now and then with 0x13 on a save that exists
   (SDKMEMCARD_ERROR -3, "no memory card"), and a retry of the menu read it. A failed open or read of an existing
   file is logged with errno and the firmware's own code of a fresh sceIoOpen, then tried again a few times. */
/* b263: the code was 80020320, too many open files (the strip pack's held handle plus audio streams). The open
   handles are then listed once (number@position) and the read waits up to 0.5 s: an audio stream gives its handle
   back soon. Returns nonzero while waiting can help. */
enum { CARD_TRIES = 25 }; /* 20 ms apart: up to 0.5 s */

static int card_failure(const char *what, const char *path, int attempt)
{
#ifdef __psp__
    int e = errno;
    int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0) sceIoClose(fd);
    if (attempt == 1 || attempt == CARD_TRIES || fd >= 0)
        p4_log("memcard %s failed (try %d): %s errno %d sceIoOpen %08x\n", what, attempt, path, e, (unsigned)fd);
    if (attempt == 1 && fd == (int)0x80020320) {
        char line[512] = "";
        int n = 0, count = 0;
        for (int i = 0; i < 128; i++) {
            int at = sceIoLseek32(i, 0, PSP_SEEK_CUR);
            if (at == (int)0x80020323) /* bad file descriptor: not open */
                continue;
            count++;
            if (n < (int)sizeof line - 24)
                n += snprintf(line + n, sizeof line - n, " %d@%x", i, (unsigned)at);
        }
        p4_log("memcard open handles %d:%s\n", count, line);
    }
    return fd >= 0 || fd == (int)0x80020320;
#else
    (void)what; (void)path; (void)attempt;
    return 0;
#endif
}

static void card_wait(void)
{
#ifdef __psp__
    sceKernelDelayThread(20 * 1000);
#endif
}

static FILE *open_saved(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f && errno == ENOENT) {
        char backup[264];
        snprintf(backup, sizeof backup, "%s.bak", path);
        f = fopen(backup, "rb");
    }
    return f;
}

/* Get file information: the retail caller reads the file size at offset 0x10. */
int func_00431d78(int socket, const char *name, void *info)
{
    p4_mc_calls[1]++;
    char path[256];
    if (!card_path(path, socket, name)) return complete(1, MC_IO);
    long size = -1;
    for (int attempt = 1; attempt <= CARD_TRIES; attempt++) {
        FILE *f = open_saved(path);
        int opened = f != NULL;
        if (!f && errno == ENOENT) return complete(1, MC_MISSING);
        if (f) {
            int ok = fseek(f, 0, SEEK_END) == 0;
            size = ok ? ftell(f) : -1;
            if (fclose(f) != 0) size = -1;
            if (size >= 0 && size <= INT_MAX) break;
        }
        size = -1;
        if (!card_failure(opened ? "size" : "open for size", path, attempt)) break;
        if (attempt < CARD_TRIES) card_wait();
    }
    if (size < 0 || size > INT_MAX) return complete(1, MC_IO);
    memset(info, 0, 64);
    ((unsigned *)info)[4] = (unsigned)size;
    return complete(1, 0);
}

/* Read a file or its serialized summary. */
int func_00431f28(int socket, const char *name, void *data, unsigned offset, unsigned size)
{
    p4_mc_calls[2]++;
    p4_mc_bytes += size;
    char path[256];
    if (!card_path(path, socket, name) || offset > INT_MAX) return complete(5, MC_IO);
    int ok = 0;
    for (int attempt = 1; attempt <= CARD_TRIES && !ok; attempt++) {
        FILE *f = open_saved(path);
        int opened = f != NULL;
        if (!f && errno == ENOENT) return complete(5, MC_MISSING);
        if (f) {
            ok = fseek(f, offset, SEEK_SET) == 0 && fread(data, 1, size, f) == size;
            if (fclose(f) != 0) ok = 0;
        }
        if (!ok) {
            if (!card_failure(opened ? "read" : "open", path, attempt)) break;
            if (attempt < CARD_TRIES) card_wait();
        }
    }
    return complete(5, ok && size <= INT_MAX ? (int)size : MC_IO);
}

/* Nonzero while a save is being written: the HOME exit waits for it (main.c). */
volatile int p4_mc_writing;

static int write_save(int socket, const char *name, const void *data, unsigned offset, unsigned size);

/* Write to a temporary file first; retain the previous committed save if any step fails. */
int func_00432288(int socket, const char *name, const void *data, unsigned offset, unsigned size)
{
    p4_mc_writing++;
    int r = write_save(socket, name, data, offset, size);
    p4_mc_writing--;
    return r;
}

static int write_save(int socket, const char *name, const void *data, unsigned offset, unsigned size)
{
    p4_mc_calls[3]++;
    char path[256], temp[264], backup[264];
    if (!card_path(path, socket, name) || size > INT_MAX || offset > INT_MAX - size)
        return complete(6, MC_IO);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    snprintf(backup, sizeof backup, "%s.bak", path);
    FILE *f = fopen(temp, "wb");
    if (!f) return complete(6, MC_IO);
    int ok = 1;
    if (offset) {
        FILE *old = open_saved(path);
        if (!old) ok = 0;
        else {
            unsigned char bytes[4096];
            size_t n;
            while ((n = fread(bytes, 1, sizeof bytes, old)) != 0)
                if (fwrite(bytes, 1, n, f) != n) { ok = 0; break; }
            if (ferror(old)) ok = 0;
            if (fclose(old) != 0) ok = 0;
        }
    }
    if (ok) ok = fseek(f, offset, SEEK_SET) == 0 && fwrite(data, 1, size, f) == size;
    if (fflush(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (ok) ok = sync_card();
    if (ok) {
        struct stat st;
        if (stat(path, &st) == 0) {
            if (remove(backup) != 0 && errno != ENOENT) ok = 0;
            if (ok && rename(path, backup) != 0) ok = 0;
        } else if (errno != ENOENT) ok = 0;
        if (ok && rename(temp, path) != 0) {
            rename(backup, path);
            ok = 0;
        }
    }
    if (ok) ok = sync_card();
    if (!ok) remove(temp);
    return complete(6, ok ? (int)size : MC_IO);
}

int func_004323a8(int socket, const char *name)
{
    p4_mc_calls[4]++;
    char path[256], backup[264];
    struct stat st;
    if (!card_path(path, socket, name)) return complete(1, MC_IO);
    if (stat(path, &st) == 0) return complete(1, S_ISREG(st.st_mode) ? 0 : MC_IO);
    if (errno != ENOENT) return complete(1, MC_IO);
    /* An interrupted replacement can leave the committed slot only in .bak; an empty primary would hide it
       from open_saved (fix from MeMeNT0xMoRii's fork). */
    snprintf(backup, sizeof backup, "%s.bak", path);
    if (stat(backup, &st) == 0) return complete(1, S_ISREG(st.st_mode) ? 0 : MC_IO);
    if (errno != ENOENT) return complete(1, MC_IO);
    FILE *f = fopen(path, "wb");
    return complete(1, f && fclose(f) == 0 ? 0 : MC_IO);
}

int func_004326b8(int socket, const char *name)
{
    p4_mc_calls[5]++;
    char path[256];
    return complete(1, card_path(path, socket, name) && directory(path) ? 0 : MC_IO);
}
