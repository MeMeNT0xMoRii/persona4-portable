/* PSP HOME confirmation and a barrier for in-flight memory-card operations. */
#include <pspkernel.h>
#include "lifecycle.h"

static SceUID card_lock = -1;
static SceUID callback_ready = -1;
static int callback_status;
static volatile int closing;

int p4_card_io_begin(void)
{
    if (closing || card_lock < 0 || sceKernelWaitSema(card_lock, 1, 0) < 0)
        return 0;
    if (closing) {
        sceKernelSignalSema(card_lock, 1);
        return 0;
    }
    return 1;
}

void p4_card_io_end(void)
{
    sceKernelSignalSema(card_lock, 1);
}

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    /* Stop new card commands after HOME confirmation. The in-flight command
       holds this lock through its file renames and save sync. */
    closing = 1;
    sceKernelWaitSema(card_lock, 1, 0);
    /* The firmware owns teardown. Keep file and device I/O in the game threads. */
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    SceUID callback = sceKernelCreateCallback("p4_exit", exit_callback, 0);
    callback_status = callback < 0 ? callback : sceKernelRegisterExitCallback(callback);
    sceKernelSignalSema(callback_ready, 1);
    if (callback_status < 0) {
        if (callback >= 0) sceKernelDeleteCallback(callback);
        return 0;
    }
    /* A dedicated callback-aware wait also works while the game is in a movie
       or a retail thread waits without the CB variant. */
    for (;;) sceKernelSleepThreadCB();
    return 0;
}

int p4_lifecycle_init(void)
{
    card_lock = sceKernelCreateSema("p4_card_io", 0, 1, 1, 0);
    if (card_lock < 0) return card_lock;
    callback_ready = sceKernelCreateSema("p4_exit_ready", 0, 0, 1, 0);
    if (callback_ready < 0) {
        int error = callback_ready;
        sceKernelDeleteSema(card_lock);
        card_lock = -1;
        return error;
    }
    SceUID thread = sceKernelCreateThread("p4_callbacks", callback_thread, 0x10,
                                         16 * 1024, PSP_THREAD_ATTR_USER, 0);
    int status = thread < 0 ? thread : sceKernelStartThread(thread, 0, 0);
    if (status >= 0) {
        status = sceKernelWaitSema(callback_ready, 1, 0);
        if (status >= 0) status = callback_status;
        if (status < 0) sceKernelWaitThreadEnd(thread, 0);
    }
    sceKernelDeleteSema(callback_ready);
    callback_ready = -1;
    if (status < 0) {
        if (thread >= 0) sceKernelDeleteThread(thread);
        sceKernelDeleteSema(card_lock);
        card_lock = -1;
    }
    return status;
}
