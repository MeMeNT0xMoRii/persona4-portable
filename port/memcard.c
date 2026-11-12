/* Keep the game's serialization and card UI; replace the IOP file commands with PSP files. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "lifecycle.h"
#ifdef __psp__
#include <pspiofilemgr.h>
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

static int complete_card_io(int cmd, int value)
{
    int status = complete(cmd, value);
    p4_card_io_end();
    return status;
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
    if (!p4_card_io_begin()) return complete(1, MC_IO);
    p4_mc_calls[0]++;
    char path[256];
    int ok = card_path(path, socket, "probe");
    info[0] = ok ? 2 : 0;
    info[1] = ok ? 1 : 0;
    info[2] = ok ? 8192 : 0;
    return complete_card_io(1, ok ? 0 : MC_IO);
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
    if (!p4_card_io_begin()) return complete(1, MC_IO);
    p4_mc_calls[1]++;
    char path[256];
    if (!card_path(path, socket, name)) return complete_card_io(1, MC_IO);
    FILE *f = open_saved(path);
    if (!f) return complete_card_io(1, errno == ENOENT ? MC_MISSING : MC_IO);
    int ok = fseek(f, 0, SEEK_END) == 0;
    long size = ok ? ftell(f) : -1;
    if (fclose(f) != 0) size = -1;
    if (size < 0 || size > INT_MAX) return complete_card_io(1, MC_IO);
    memset(info, 0, 64);
    ((unsigned *)info)[4] = (unsigned)size;
    return complete_card_io(1, 0);
}

/* Read a file or its serialized summary. */
int func_00431f28(int socket, const char *name, void *data, unsigned offset, unsigned size)
{
    if (!p4_card_io_begin()) return complete(5, MC_IO);
    p4_mc_calls[2]++;
    p4_mc_bytes += size;
    char path[256];
    if (!card_path(path, socket, name) || offset > INT_MAX) return complete_card_io(5, MC_IO);
    FILE *f = open_saved(path);
    if (!f) return complete_card_io(5, errno == ENOENT ? MC_MISSING : MC_IO);
    int ok = fseek(f, offset, SEEK_SET) == 0 && fread(data, 1, size, f) == size;
    if (fclose(f) != 0) ok = 0;
    return complete_card_io(5, ok && size <= INT_MAX ? (int)size : MC_IO);
}

/* Write to a temporary file first; retain the previous committed save if any step fails. */
int func_00432288(int socket, const char *name, const void *data, unsigned offset, unsigned size)
{
    if (!p4_card_io_begin()) return complete(6, MC_IO);
    p4_mc_calls[3]++;
    char path[256], temp[264], backup[264];
    if (!card_path(path, socket, name) || size > INT_MAX || offset > INT_MAX - size)
        return complete_card_io(6, MC_IO);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    snprintf(backup, sizeof backup, "%s.bak", path);
    FILE *f = fopen(temp, "wb");
    if (!f) return complete_card_io(6, MC_IO);
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
    return complete_card_io(6, ok ? (int)size : MC_IO);
}

int func_004323a8(int socket, const char *name)
{
    if (!p4_card_io_begin()) return complete(1, MC_IO);
    p4_mc_calls[4]++;
    char path[256], backup[264];
    struct stat st;
    if (!card_path(path, socket, name)) return complete_card_io(1, MC_IO);
    if (stat(path, &st) == 0) return complete_card_io(1, S_ISREG(st.st_mode) ? 0 : MC_IO);
    if (errno != ENOENT) return complete_card_io(1, MC_IO);
    /* An interrupted replacement can leave the committed slot in .bak.
       Readers use that copy until the next successful save installs a primary. */
    snprintf(backup, sizeof backup, "%s.bak", path);
    if (stat(backup, &st) == 0) return complete_card_io(1, S_ISREG(st.st_mode) ? 0 : MC_IO);
    if (errno != ENOENT) return complete_card_io(1, MC_IO);
    FILE *f = fopen(path, "wb");
    return complete_card_io(1, f && fclose(f) == 0 ? 0 : MC_IO);
}

int func_004326b8(int socket, const char *name)
{
    if (!p4_card_io_begin()) return complete(1, MC_IO);
    p4_mc_calls[5]++;
    char path[256];
    return complete_card_io(1, card_path(path, socket, name) && directory(path) ? 0 : MC_IO);
}
