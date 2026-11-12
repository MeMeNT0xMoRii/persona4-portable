"""Run the real HOME/card code against a pthread-backed PSP kernel stub.

This checks exit ordering and saved-file contents, not PSP system-dialog UI.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


HEADER = r'''
#ifndef TEST_PSPKERNEL_H
#define TEST_PSPKERNEL_H
typedef int SceUID;
typedef unsigned SceSize;
#define PSP_THREAD_ATTR_USER 0x80000000
SceUID sceKernelCreateSema(const char *, unsigned, int, int, void *);
int sceKernelWaitSema(SceUID, int, unsigned *);
int sceKernelSignalSema(SceUID, int);
int sceKernelDeleteSema(SceUID);
SceUID sceKernelCreateThread(const char *, int (*)(SceSize, void *), int, int, unsigned, void *);
int sceKernelStartThread(SceUID, SceSize, void *);
int sceKernelWaitThreadEnd(SceUID, unsigned *);
int sceKernelDeleteThread(SceUID);
SceUID sceKernelCreateCallback(const char *, int (*)(int, int, void *), void *);
int sceKernelRegisterExitCallback(SceUID);
int sceKernelDeleteCallback(SceUID);
int sceKernelSleepThreadCB(void);
void sceKernelExitGame(void);
int sceIoSync(const char *, unsigned);
#endif
'''

HARNESS = r'''
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pspkernel.h"
#include "lifecycle.h"

static sem_t sems[2];
static int sem_count, live_sems, live_threads, live_callbacks;
static int (*entry)(SceSize, void *);
static int (*exit_cb)(int, int, void *);
static pthread_t cb_thread;
static pthread_mutex_t events = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int exit_requested, exit_waiting, rename_waiting, allow_rename, hold_rename;
static atomic_int exited, save_syncs;
static int installed_saves;
static const char *failure;

static void wait_event(int *flag) {
    pthread_mutex_lock(&events);
    while (!*flag) pthread_cond_wait(&changed, &events);
    pthread_mutex_unlock(&events);
}
static void set_event(int *flag) {
    pthread_mutex_lock(&events);
    *flag = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&events);
}
static int fails(const char *name) { return !strcmp(failure, name); }

SceUID sceKernelCreateSema(const char *name, unsigned attr, int initial, int max, void *opt) {
    (void)name; (void)attr; (void)opt;
    assert(max == 1 && sem_count < 2);
    if (fails(sem_count == 0 ? "card_sema" : "ready_sema")) return -11;
    int id = sem_count++;
    assert(sem_init(&sems[id], 0, initial) == 0);
    live_sems++;
    return id;
}
int sceKernelWaitSema(SceUID id, int n, unsigned *timeout) {
    assert(n == 1 && !timeout);
    if (id == 0 && pthread_equal(pthread_self(), cb_thread)) set_event(&exit_waiting);
    return sem_wait(&sems[id]);
}
int sceKernelSignalSema(SceUID id, int n) { assert(n == 1); return sem_post(&sems[id]); }
int sceKernelDeleteSema(SceUID id) { live_sems--; return sem_destroy(&sems[id]); }
static void *start_cb(void *unused) { (void)unused; entry(0, 0); return 0; }
SceUID sceKernelCreateThread(const char *name, int (*fn)(SceSize, void *), int priority,
                             int stack, unsigned attr, void *opt) {
    (void)name; (void)priority; (void)stack; (void)attr; (void)opt;
    if (fails("create_thread")) return -12;
    entry = fn; live_threads++; return 10;
}
int sceKernelStartThread(SceUID id, SceSize size, void *arg) {
    assert(id == 10 && !size && !arg);
    if (fails("start_thread")) return -13;
    return pthread_create(&cb_thread, 0, start_cb, 0);
}
int sceKernelWaitThreadEnd(SceUID id, unsigned *timeout) {
    assert(id == 10 && !timeout); return pthread_join(cb_thread, 0);
}
int sceKernelDeleteThread(SceUID id) { assert(id == 10); live_threads--; return 0; }
SceUID sceKernelCreateCallback(const char *name, int (*fn)(int, int, void *), void *arg) {
    (void)name; assert(!arg);
    if (fails("create_callback")) return -14;
    exit_cb = fn; live_callbacks++; return 20;
}
int sceKernelRegisterExitCallback(SceUID id) {
    assert(id == 20); return fails("register_callback") ? -15 : 0;
}
int sceKernelDeleteCallback(SceUID id) { assert(id == 20); live_callbacks--; return 0; }
int sceKernelSleepThreadCB(void) {
    wait_event(&exit_requested);
    return exit_cb(0, 0, 0);
}
int sceIoSync(const char *path, unsigned flags) {
    assert(!strcmp(path, "ms0:") && flags == 0);
    assert(!pthread_equal(pthread_self(), cb_thread));
    int syncs = atomic_fetch_add(&save_syncs, 1);
    assert(installed_saves == (syncs + 1) / 2);
    return 0;
}
void sceKernelExitGame(void) {
    assert(atomic_load(&save_syncs) == (hold_rename ? 4 : 0));
    atomic_store(&exited, 1);
    pthread_exit(0);
}

int __real_rename(const char *, const char *);
int __wrap_rename(const char *from, const char *to) {
    size_t length = strlen(from);
    int installing = length >= 4 && !strcmp(from + length - 4, ".tmp");
    if (installing) assert(atomic_load(&save_syncs) == installed_saves * 2 + 1);
    if (hold_rename && installing) {
        set_event(&rename_waiting);
        wait_event(&allow_rename);
    }
    int status = __real_rename(from, to);
    if (installing && status == 0) installed_saves++;
    return status;
}

int func_004326b8(int, const char *);
int func_004323a8(int, const char *);
int func_00432288(int, const char *, const void *, unsigned, unsigned);
int sceMc2CheckAsync(int *, int *);
static const char *slot = "/BASLUS-21782Save0/BASLUS-21782Save0";
static unsigned char replacement[128];
static void *save_thread(void *unused) {
    (void)unused;
    assert(func_00432288(0, slot, replacement, 0, sizeof replacement) == 0);
    return 0;
}
static int result(void) {
    int command, value;
    assert(sceMc2CheckAsync(&command, &value) == 1);
    return value;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    failure = argv[1];
    if (strcmp(failure, "idle") && strcmp(failure, "saving")) {
        assert(p4_lifecycle_init() < 0);
        assert(!live_sems && !live_threads && !live_callbacks);
        assert(!p4_card_io_begin() && !atomic_load(&exited));
        return 0;
    }
    assert(p4_lifecycle_init() == 0);
    assert(p4_card_io_begin());
    p4_card_io_end();
    assert(!atomic_load(&exited));
    if (fails("idle")) {
        set_event(&exit_requested);
        assert(pthread_join(cb_thread, 0) == 0);
        assert(atomic_load(&exited) && !p4_card_io_begin());
        return 0;
    }

    assert(func_004326b8(0, "/BASLUS-21782Save0") == 0 && result() == 0);
    assert(func_004323a8(0, slot) == 0 && result() == 0);
    unsigned char original[128];
    memset(original, 'A', sizeof original);
    memset(replacement, 'B', sizeof replacement);
    assert(func_00432288(0, slot, original, 0, sizeof original) == 0 && result() == 128);
    hold_rename = 1;
    pthread_t writer;
    assert(pthread_create(&writer, 0, save_thread, 0) == 0);
    wait_event(&rename_waiting);
    set_event(&exit_requested);
    wait_event(&exit_waiting);
    assert(!atomic_load(&exited) && atomic_load(&save_syncs) == 3);
    assert(!p4_card_io_begin());
    assert(func_00432288(0, "/rejected", original, 0, sizeof original) == 0);
    assert(result() < 0);
    set_event(&allow_rename);
    assert(pthread_join(writer, 0) == 0);
    assert(pthread_join(cb_thread, 0) == 0);
    assert(atomic_load(&exited) && !p4_card_io_begin());

    char path[1024];
    snprintf(path, sizeof path, "%s/card0%s", argv[2], slot);
    unsigned char actual[128];
    FILE *f = fopen(path, "rb");
    assert(f && fread(actual, 1, sizeof actual, f) == sizeof actual);
    assert(fclose(f) == 0 && !memcmp(actual, replacement, sizeof actual));
    strcat(path, ".bak");
    f = fopen(path, "rb");
    assert(f && fread(actual, 1, sizeof actual, f) == sizeof actual);
    assert(fclose(f) == 0 && !memcmp(actual, original, sizeof actual));
    snprintf(path, sizeof path, "%s/card0/rejected", argv[2]);
    assert(!fopen(path, "rb"));
    return 0;
}
'''


@unittest.skipUnless(os.name == 'posix' and shutil.which('gcc'),
                     'requires POSIX pthreads and host GCC')
class HomeLifecycle(unittest.TestCase):
    def test_startup_failure_and_exit_during_save(self):
        root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            card = path / 'card'
            (path / 'pspkernel.h').write_text(HEADER)
            (path / 'pspiofilemgr.h').write_text('#include "pspkernel.h"\n')
            (path / 'check.c').write_text(HARNESS)
            executable = path / 'check'
            subprocess.run([
                shutil.which('gcc'), '-O2', '-std=c11', '-pthread', '-D__psp__',
                '-Wall', '-Wextra', f'-I{path}', f'-I{root / "port"}',
                f'-DP4_CARD_ROOT="{card.as_posix()}"',
                str(root / 'port/lifecycle.c'), str(root / 'port/memcard.c'),
                str(path / 'check.c'), '-Wl,--wrap=rename', '-o', str(executable),
            ], check=True, capture_output=True)
            for scenario in ('card_sema', 'ready_sema', 'create_thread', 'start_thread',
                             'create_callback', 'register_callback', 'idle', 'saving'):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), scenario, str(card)],
                                   check=True, timeout=10, capture_output=True)


if __name__ == '__main__':
    unittest.main()
