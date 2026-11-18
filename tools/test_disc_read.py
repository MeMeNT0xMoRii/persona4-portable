"""Exercise the native sector reader against short and split reads."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class DiscRead(unittest.TestCase):
    def test_reports_completion_only_after_all_requested_bytes_arrive(self):
        cc = shutil.which('gcc')
        if not cc:
            self.skipTest('host gcc is unavailable')

        source = (Path(__file__).resolve().parents[1] / 'port/files.c').read_text()
        read = re.search(r'^int func_004c9010\([^;]*?\n\{.*?^\}', source, re.M | re.S)[0]
        harness = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define P4_PATH_MAX 256
#define PSP_O_RDONLY 1
static char p4_game_dir[P4_PATH_MAX];
static int sceIoOpen(const char *path, int mode, int permissions) { return -1; }
enum { SECTOR = 2048, PART = 4096, MAX_PARTS = 2, PSP_SEEK_SET = 0 };
typedef int SceUID;
typedef struct { int container, stat; long long offset; } Handle;
static SceUID fds[1][MAX_PARTS] = {{11, 12}};
static const char *containers[] = { "DATA" };
static int available[2], position, seek_fails, wait_fails;
static int seeks, reads, locks, writebacks;
static void p4_log(const char *fmt, ...) {}
static int sceKernelCreateSema(const char *name, int options, int initial, int max, void *opt) { return 1; }
static int sceKernelWaitSema(int lock, int count, void *timeout) {
    if (wait_fails) return -1;
    ++locks;
    return 0;
}
static void sceKernelSignalSema(int lock, int count) { --locks; }
static long long sceIoLseek(int fd, long long where, int mode) {
    ++seeks;
    return seek_fails ? -1 : (position = where);
}
static int sceIoRead(int fd, void *out, int size) {
    ++reads;
    int remaining = available[fd - 10] - position;
    if (remaining <= 0) return 0;
    int count = size < remaining ? size : remaining;
    memset(out, fd, count);
    return count;
}
static void sceKernelDcacheWritebackRange(void *buffer, int size) { ++writebacks; }
'''
        cases = r'''
int main(void) {
    unsigned char bytes[SECTOR * 2];
    Handle h = { .offset = PART - SECTOR };
    available[0] = PART;
    available[1] = SECTOR - 1;
    assert(func_004c9010(&h, 2, bytes) == 0 && h.stat == 4);
    assert(seeks == 3 && reads == 3 && !locks && !writebacks);

    available[1] = SECTOR;
    assert(func_004c9010(&h, 2, bytes) == 2 && h.stat == 3);
    assert(bytes[SECTOR - 1] == 10 && bytes[SECTOR] == 11 && bytes[SECTOR * 2 - 1] == 11);
    assert(!locks && writebacks == 1);

    seek_fails = 1;
    int before = reads;
    assert(func_004c9010(&h, 2, bytes) == 0 && h.stat == 4);
    assert(reads == before && !locks && writebacks == 1);
    seek_fails = 0;
    wait_fails = 1;
    assert(func_004c9010(&h, 2, bytes) == 0 && h.stat == 4 && !locks);
    assert(reads == before && writebacks == 1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            fixture, binary = path / 'disc.c', path / 'disc'
            fixture.write_text(harness + read + cases)
            subprocess.run([cc, '-std=c99', '-O2', str(fixture), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], cwd=path, check=True)


if __name__ == '__main__':
    unittest.main()
