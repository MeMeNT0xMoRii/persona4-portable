/* QA06 2026-11-20, no retail assets. Standalone host regression harness.
   Function func_004c9010 below was transcribed verbatim (only dependencies
   mocked) from frozen port/files.c lines 148-202 at
   29b0fcfdec70222a4052099b67fb4a11e8d8aff4.
   This is NOT a PSP executable or a linked EBOOT test. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define SECTOR 0x800
#define NUM_CONTAINERS 5
#define NUM_HANDLES 16
#define PART (1 << 30)
#define MAX_PARTS 4
#define P4_PATH_MAX 512
#define PSP_O_RDONLY 1
#define PSP_SEEK_SET 0
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);} } while(0)
typedef int SceUID;
typedef struct { int used,container;long long offset;unsigned size;int stat; } Handle;
static const char *const containers[NUM_CONTAINERS]={"DATA","BTL","ENV","BGM","ZZZZZ"};
static SceUID fds[NUM_CONTAINERS][MAX_PARTS];
static const char p4_game_dir[]="test/";
static int io_pos[20],io_reads,fail_open_part=-1,fail_after_first,open_calls,wait_failure,sem_count;
static void mock_reset(void) {
    memset(fds,0,sizeof fds);memset(io_pos,0,sizeof io_pos);
    io_reads=0;fail_open_part=-1;fail_after_first=0;open_calls=0;wait_failure=0;sem_count=0;
}
static SceUID sceKernelCreateSema(const char *name,int flags,int init,int max,void *opts) {
    (void)name;(void)flags;(void)init;(void)max;(void)opts;return 1;
}
static int sceKernelWaitSema(SceUID sem,int n,void *timeout) {
    (void)sem;(void)n;(void)timeout; if(wait_failure)return -1;sem_count++;return 0;
}
static int sceKernelSignalSema(SceUID sem,int n) {
    (void)sem;(void)n;sem_count--;return 0;
}
static SceUID sceIoOpen(const char *path,int flags,int mode) {
    (void)flags;(void)mode;int part=-1;
    if(sscanf(path,"test/DATA.%d",&part)!=1||part<0||part>=MAX_PARTS||part==fail_open_part)return -1;
    open_calls++;return 10+part;
}
static int sceIoLseek(SceUID fd,int pos,int whence) {
    if(fd<10||fd>=14||whence!=PSP_SEEK_SET||pos<0)return -1;
    io_pos[fd]=pos;return pos;
}
static int sceIoRead(SceUID fd,void *buffer,int size) {
    if(fd<10||fd>=14||size<0)return -1;
    if(fail_after_first && io_reads>=1)return -1;
    int n=size;
    if(fail_after_first && io_reads==0 && n>1024)n=1024;
    int part=fd-10;
    for(int i=0;i<n;i++) {
        uint64_t abs=(uint64_t)part*(uint64_t)PART+(uint64_t)io_pos[fd]+(uint64_t)i;
        uint64_t sector=abs/SECTOR;
        ((char*)buffer)[i]=sector==0?'H':sector==1?'D':sector==2?'E':part==1?'Y':'Z';
    }
    io_pos[fd]+=n;io_reads++;return n;
}
static void sceKernelDcacheWritebackRange(void *ptr,int bytes) {(void)ptr;(void)bytes;}
static void p4_log(const char *fmt, ...) {(void)fmt;}

/* === exact frozen func_004c9010 body, except this enclosing comment === */
/* ADXF_ReadNw32: whole sectors, synchronous. ponytail: blocks the frame, a loader thread when load times show. */
int func_004c9010(Handle *h, int sectors, void *buffer)
{
    static SceUID lock;
    if (lock <= 0)
        lock = sceKernelCreateSema("p4io", 0, 1, 1, 0);
    char *to = buffer;
    long long offset = h->offset;
    int left = sectors * SECTOR, got = 0;
    /* A read yields, and the game's worker thread reads too. */
    if (lock < 0 || sceKernelWaitSema(lock, 1, 0) < 0) {
        h->stat = 4;
        return 0;
    }
    while (left > 0) {
        int part = offset / PART;
        int inside = offset % PART;
        int size = left < PART - inside ? left : PART - inside;
        if (part >= MAX_PARTS)
            break;
        /* Keep zero for unopened entries, including after a failed open.
           The same semaphore protects opening, seeking and reading. */
        SceUID fd = fds[h->container][part] - 1;
        if (fd < 0) {
            char path[P4_PATH_MAX];
            snprintf(path, sizeof path, "%s%s.%d", p4_game_dir, containers[h->container], part);
            fd = sceIoOpen(path, PSP_O_RDONLY, 0);
            if (fd < 0)
                break;
            fds[h->container][part] = fd + 1;
        }
        if (sceIoLseek(fd, inside, PSP_SEEK_SET) != inside)
            break;
        int n = sceIoRead(fd, to + got, size);
        if (n <= 0)
            break;
        got += n;
        left -= n;
        offset += n;
    }
    sceKernelSignalSema(lock, 1);
    /* Rounding a short final sector up to a block let the archive loader
       parse a buffer whose tail had never been read. */
    if (left != 0 || !got) {
        static unsigned reports;
        if (reports < 8) {
            reports++;
            p4_log("disc read incomplete: %s sector %u got %d of %d bytes\n",
                   containers[h->container], (unsigned)(h->offset / SECTOR), got, sectors * SECTOR);
        }
        h->stat = 4;
        return 0;
    }
    sceKernelDcacheWritebackRange(buffer, sectors * SECTOR);
    h->stat = 3;
    return sectors;
}
/* === exact body ends === */

static int tests;
#define TEST(name,...) do {mock_reset();__VA_ARGS__;tests++;printf("PASS %02d: %s\n",tests,name);}while(0)
int main(void) {
    char buf[3*SECTOR];
    TEST("one call 2 sectors H,D (one-shot works)", {
        Handle h={.used=1,.container=0,.offset=0,.size=3*SECTOR,.stat=1};
        memset(buf,0,sizeof buf);
        CHECK(func_004c9010(&h,2,buf)==2);
        CHECK(buf[0]=='H' && buf[SECTOR]=='D' && h.stat==3 && h.offset==0);
    });
    TEST("two calls one handle -> H,H (cursor not committed)", {
        Handle h={.used=1,.container=0,.offset=0,.size=3*SECTOR,.stat=1};
        CHECK(func_004c9010(&h,1,buf)==1);
        CHECK(buf[0]=='H' && h.offset==0);
        CHECK(func_004c9010(&h,1,buf+SECTOR)==1);
        CHECK(buf[SECTOR]=='H' && h.offset==0);
    });
    TEST("separate handles seek independently", {
        Handle h1={1,0,0,3*SECTOR,1},h2={1,0,SECTOR,3*SECTOR,1};
        CHECK(func_004c9010(&h1,1,buf)==1 && buf[0]=='H');
        CHECK(func_004c9010(&h2,1,buf)==1 && buf[0]=='D');
        CHECK(func_004c9010(&h1,1,buf)==1 && buf[0]=='H');
    });
    TEST("read across 1GiB part boundary in one invocation", {
        Handle h={1,0,(long long)PART-SECTOR,2*SECTOR,1};
        CHECK(func_004c9010(&h,2,buf)==2);
        CHECK(buf[0]=='Z' && buf[SECTOR]=='Y' && h.offset==(long long)PART-SECTOR);
        CHECK(open_calls==2);
    });
    TEST("declared handle size does not bound read", {
        Handle h={1,0,0,SECTOR,1};
        CHECK(func_004c9010(&h,2,buf)==2);
        CHECK(buf[0]=='H' && buf[SECTOR]=='D' && h.stat==3);
    });
    TEST("failed open gives stat=4 and return0", {
        Handle h={1,0,0,SECTOR,1};fail_open_part=0;
        CHECK(func_004c9010(&h,1,buf)==0 && h.stat==4 && h.offset==0);
    });
    TEST("partial read followed by error gives stat4/no cursor commit", {
        Handle h={1,0,0,2*SECTOR,1};fail_after_first=1;
        CHECK(func_004c9010(&h,1,buf)==0 && h.stat==4 && h.offset==0 && io_reads==1);
        CHECK(buf[0]=='H');
    });
    TEST("zero sector count gives stat4", {
        Handle h={1,0,0,SECTOR,1};
        CHECK(func_004c9010(&h,0,buf)==0 && h.stat==4 && io_reads==0);
    });
    TEST("negative sector count is rejected implicitly", {
        Handle h={1,0,0,SECTOR,1};
        CHECK(func_004c9010(&h,-1,buf)==0 && h.stat==4 && io_reads==0);
    });
    TEST("semaphore wait failure returns without IO", {
        Handle h={1,0,0,SECTOR,1};wait_failure=1;
        CHECK(func_004c9010(&h,1,buf)==0 && h.stat==4 && io_reads==0);
    });
    TEST("part index beyond MAX_PARTS ends in stat4", {
        Handle h={1,0,(long long)MAX_PARTS*PART,SECTOR,1};
        CHECK(func_004c9010(&h,1,buf)==0 && h.stat==4 && io_reads==0);
    });
    CHECK(sem_count==0);
    printf("ALL %d exact-body/mocked-IO C host tests passed (NOT a PSP runtime test)\n",tests);
    return 0;
}
