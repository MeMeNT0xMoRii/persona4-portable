/* PSP HOME confirmation and a barrier for in-flight memory-card operations. */
#include <pspkernel.h>
#include <pspge.h>
#include "lifecycle.h"

extern void p4_log(const char *fmt, ...);

static SceUID card_lock = -1;
static SceUID callback_ready = -1;
static int callback_status;
static volatile int closing;
enum { HOME_IDLE, HOME_WAIT_CARD, HOME_CHECK_GE, HOME_RESET_GE, HOME_EXIT_GAME, HOME_EXIT_RETURNED };
static volatile int exit_phase, exit_ge_state, exit_ge_result;

void p4_lifecycle_report(void)
{
    static int last_phase;
    static const char *const phases[] = {
        "idle", "waiting for card", "checking GE", "resetting GE queue", "calling ExitGame", "ExitGame returned"
    };
    int phase = exit_phase;
    if (phase != last_phase) {
        p4_log("HOME exit: %s (GE state %d, reset %d)\n", phases[phase], exit_ge_state, exit_ge_result);
        last_phase = phase;
    }
}

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
    exit_phase = HOME_WAIT_CARD;
    sceKernelWaitSema(card_lock, 1, 0);
    /* HOME can pause the game with a frame still stopped at its GE stall address. */
    exit_phase = HOME_CHECK_GE;
    exit_ge_state = sceGeDrawSync(1);
    if (exit_ge_state > PSP_GE_LIST_DONE) {
        exit_phase = HOME_RESET_GE;
        exit_ge_result = sceGeBreak(1, 0);
    }
    exit_phase = HOME_EXIT_GAME;
    sceKernelExitGame();
    exit_phase = HOME_EXIT_RETURNED;
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
    /* Run ahead of the display thread after the last card command finishes. */
    SceUID thread = sceKernelCreateThread("p4_callbacks", callback_thread, 0x0e,
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
