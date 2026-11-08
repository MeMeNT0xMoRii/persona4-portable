/* The EE kernel calls the game makes directly, on the PSP kernel. EE priorities run 0-127 with 0 highest, like the PSP's. */
#include <pspkernel.h>

#define MAX_THREADS 16
#define MIN_STACK (64 * 1024)
#define DUMMY "p4"

typedef struct { int status; void *func, *stack; int stackSize; void *gp; int initPriority, currentPriority; unsigned attr, option; } ThreadParam;
typedef struct { int count, maxCount, initCount, waitThreads; unsigned attr, option; } SemaParam;

static struct { SceUID id; void (*func)(void *); } threads[MAX_THREADS];

static int psp_priority(int ee)
{
    int p = 16 + ee / 2;
    return p > 111 ? 111 : p;
}

static int trampoline(SceSize size, void *arg)
{
    SceUID self = sceKernelGetThreadId();
    __asm__ volatile("move $gp, %0" : : "r"(P4_GP)); /* a new thread may start with the module's $gp */
    for (int i = 0; i < MAX_THREADS; i++)
        if (threads[i].id == self)
            threads[i].func(*(void **)arg);
    return 0;
}

int CreateThread(ThreadParam *param)
{
    for (int i = 0; i < MAX_THREADS; i++)
        if (!threads[i].id) {
            int stack = param->stackSize < MIN_STACK ? MIN_STACK : param->stackSize;
            SceUID id = sceKernelCreateThread(DUMMY, trampoline, psp_priority(param->initPriority), stack, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, 0);
            threads[i].id = id;
            threads[i].func = param->func;
            return id;
        }
    return -1;
}

int StartThread(int id, void *arg) { return sceKernelStartThread(id, sizeof arg, &arg) >= 0 ? id : -1; }
int DeleteThread(int id)
{
    for (int i = 0; i < MAX_THREADS; i++)
        if (threads[i].id == id)
            threads[i].id = 0;
    return sceKernelDeleteThread(id);
}
int TerminateThread(int id) { return sceKernelTerminateThread(id); }
int GetThreadId(void) { return sceKernelGetThreadId(); }
int ChangeThreadPriority(int id, int priority) { return sceKernelChangeThreadPriority(id, psp_priority(priority)); }
int SleepThread(void) { sceKernelSleepThread(); return 0; }
int WakeupThread(int id) { return sceKernelWakeupThread(id); }
int CancelWakeupThread(int id) { return sceKernelCancelWakeupThread(id); }
int SuspendThread(int id) { return sceKernelSuspendThread(id); }
int ResumeThread(int id) { return sceKernelResumeThread(id); }

/* Enough for the game: "is it running", its priority. */
int ReferThreadStatus(int id, ThreadParam *out)
{
    SceKernelThreadInfo info;
    info.size = sizeof info;
    if (sceKernelReferThreadStatus(id, &info) < 0)
        return -1;
    if (out) {
        out->status = info.status & PSP_THREAD_WAITING ? 2 : 1; /* EE: running or ready, waiting */
        out->currentPriority = info.currentPriority;
    }
    return id;
}

int CreateSema(SemaParam *param)
{
    return sceKernelCreateSema(DUMMY, 0, param->initCount, param->maxCount > 0 ? param->maxCount : 1, 0);
}
int DeleteSema(int id) { return sceKernelDeleteSema(id); }
int WaitSema(int id) { return sceKernelWaitSema(id, 1, 0) < 0 ? -1 : id; }
int SignalSema(int id) { return sceKernelSignalSema(id, 1) < 0 ? -1 : id; }
int PollSema(int id) { return sceKernelPollSema(id, 1) < 0 ? -1 : id; }

/* Reached by retail address from C that only knows func_ names. */
#define AT(address, name) __typeof__(name) func_##address __attribute__((alias(#name)));
AT(004215e0, CreateThread)
AT(00422920, StartThread)
AT(004215f0, DeleteThread)
AT(00421630, TerminateThread)
AT(004216d0, GetThreadId)
AT(00421670, ChangeThreadPriority)
AT(00421680, ChangeThreadPriority)
AT(00421700, SleepThread)
AT(00421710, WakeupThread)
AT(004226a8, WakeupThread)
AT(00421730, CancelWakeupThread)
AT(00421740, CancelWakeupThread)
AT(00421750, SuspendThread)
AT(004227c0, SuspendThread)
AT(00421770, ResumeThread)
AT(00421780, ResumeThread)
AT(004216e0, ReferThreadStatus)
AT(004216f0, ReferThreadStatus)
AT(004217e0, CreateSema)
AT(004217f0, DeleteSema)
AT(00421800, SignalSema)
AT(00421810, SignalSema)
AT(00421820, WaitSema)
AT(00421830, PollSema)
AT(00421840, PollSema)

int func_00421a60(int mode) { return 0; } /* FlushCache */
