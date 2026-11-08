/* The game's disc: files inside the .CVM containers, found through the index tools/psp_disc.py wrote.
   The game asks through CRI's ADXF calls and H_Cdvd_SelectDirectory; those names are replaced here. */
#include <pspkernel.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#define GAME_DIR "ms0:/PSP/GAME/P4/"
#define SECTOR 0x800
#define NUM_CONTAINERS 5
#define NUM_HANDLES 16
#define PART (1 << 30) /* containers are cut into parts: PPSSPP cannot seek past 2 GB */
#define MAX_PARTS 4

void p4_log(const char *fmt, ...);
void p4_backtrace(void);

typedef struct { unsigned hash, where, size; } Row;
typedef struct { int used, container; long long offset; unsigned size; int stat; } Handle;

static const char *const containers[NUM_CONTAINERS] = { "DATA", "BTL", "ENV", "BGM", "ZZZZZ" };
static SceUID fds[NUM_CONTAINERS][MAX_PARTS];
static Row *rows;
static int num_rows;
static char directory[256];
static Handle handles[NUM_HANDLES];

/* FNV-1a over the upper-case path with forward slashes, the same in tools/psp_disc.py. */
static unsigned path_hash(const char *dir, const char *name)
{
    unsigned h = 0x811C9DC5u;
    const char *parts[2] = { dir, name };
    for (int p = 0; p < 2; p++)
        for (const char *s = parts[p]; *s; s++) {
            char c = *s == '\\' ? '/' : *s;
            if (c >= 'a' && c <= 'z')
                c -= 32;
            h = (h ^ (unsigned char)c) * 0x01000193u;
        }
    return h;
}

static const Row *find(unsigned hash)
{
    int lo = 0, hi = num_rows - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rows[mid].hash == hash)
            return &rows[mid];
        if (rows[mid].hash < hash)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static int open_index(void)
{
    SceUID fd = sceIoOpen(GAME_DIR "p4files.idx", PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int size = (int)sceIoLseek(fd, 0, PSP_SEEK_END);
    sceIoLseek(fd, 0, PSP_SEEK_SET);
    rows = malloc(size);
    sceIoRead(fd, rows, size);
    sceIoClose(fd);
    num_rows = size / sizeof(Row);
    for (int i = 0; i < NUM_CONTAINERS; i++)
        for (int part = 0; part < MAX_PARTS; part++) {
            char path[64];
            sprintf(path, GAME_DIR "%s.%d", containers[i], part);
            fds[i][part] = sceIoOpen(path, PSP_O_RDONLY, 0);
        }
    p4_log("disc index %d files, DATA part 0 %x\n", num_rows, fds[0][0]);
    return 1;
}

/* H_Cdvd_SelectDirectory: "\\TITLE\\" style, with an optional cdrom0: or rofs prefix before the first backslash. */
void H_Cdvd_SelectDirectory(const char *dir)
{
    const char *s = strchr(dir, '\\');
    if (!s)
        s = strchr(dir, '/');
    strncpy(directory, s ? s + 1 : "", sizeof directory - 1);
}

/* Probe: a sprite name that is not text is a pointer that was never rebased. */
unsigned char *__real_sdkSpriteRequestLoad(const char *name);
unsigned char *__wrap_sdkSpriteRequestLoad(const char *name)
{
    int bad = (unsigned)name < 0x08800000u || !name[0];
    for (int i = 0; !bad && name[i] && i < 200; i++)
        bad = (unsigned char)name[i] > '~' || (unsigned char)name[i] < ' ';
    if (bad) {
        p4_log("SPRITE bad name %x\n", (unsigned)name);
        p4_backtrace();
    }
    return __real_sdkSpriteRequestLoad(name);
}

/* ADXF_Open */
Handle *func_004c85a0(const char *name, int mode)
{
    if (!rows && !open_index())
        return 0;
    const char *base = strrchr(name, ':');
    base = base ? base + 1 : name;
    while (*base == '\\' || *base == '/')
        base++;
    unsigned hash = path_hash(directory, base);
    const Row *row = find(hash);
    if (!row) {
        static unsigned last; /* the request list retries every frame */
        if (hash != last)
        {
            p4_log("no file [%s][%s] from %s\n", directory, base, name);
            p4_backtrace();
        }
        last = hash;
        return 0;
    }
    for (int i = 0; i < NUM_HANDLES; i++)
        if (!handles[i].used) {
            Handle *h = &handles[i];
            h->used = 1;
            h->container = row->where & 7;
            h->offset = (long long)(row->where >> 3) * SECTOR;
            h->size = row->size;
            h->stat = 1;
            return h;
        }
    p4_log("out of file handles\n");
    return 0;
}

/* ADXF_Close */
void func_004c8a60(Handle *h) { h->used = 0; }

/* ADXF_GetFsizeByte, ADXF_GetFsizeSct */
unsigned func_004c9670(Handle *h) { return h->size; }
unsigned func_004c95f8(Handle *h) { return (h->size + SECTOR - 1) / SECTOR; }

/* ADXF_ReadNw32: whole sectors, synchronous. ponytail: blocks the frame, a loader thread when load times show. */
int func_004c9010(Handle *h, int sectors, void *buffer)
{
    static SceUID lock;
    if (!lock)
        lock = sceKernelCreateSema("p4io", 0, 1, 1, 0);
    char *to = buffer;
    long long offset = h->offset;
    int left = sectors * SECTOR, got = 0;
    sceKernelWaitSema(lock, 1, 0); /* a read yields, and the game's worker thread reads too */
    while (left > 0) {
        int part = offset / PART;
        int inside = offset % PART;
        int size = left < PART - inside ? left : PART - inside;
        SceUID fd = fds[h->container][part];
        sceIoLseek(fd, inside, PSP_SEEK_SET);
        int n = sceIoRead(fd, to + got, size);
        if (n <= 0)
            break;
        got += n;
        left -= n;
        offset += n;
    }
    sceKernelSignalSema(lock, 1);
    sceKernelDcacheWritebackRange(buffer, sectors * SECTOR);
    h->stat = got > 0 ? 3 : 4;
    return got > 0 ? (got + SECTOR - 1) / SECTOR : 0;
}

/* ADXF_GetStat: 3 done, 4 error */
int func_004c9820(Handle *h) { return h->stat; }

/* sceCdInit and streaming bring-up: the disc is a file here. */
int func_0042eae0(int mode) { return 1; }
int func_0042efe0(int mode) { return 1; }
