"""Native card I/O, including a fresh process and interrupted replacement recovery."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class Memcard(unittest.TestCase):
    def test_persistent_slots_and_failed_write(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        root = Path(__file__).resolve().parent.parent
        harness = r'''
#include <assert.h>
#include <string.h>
int sceMc2GetInfoAsync(int, int *);
int sceMc2CheckAsync(int *, int *);
int func_004326b8(int, const char *);
int func_004323a8(int, const char *);
int func_00432288(int, const char *, const void *, unsigned, unsigned);
int func_00431f28(int, const char *, void *, unsigned, unsigned);
int func_00431d78(int, const char *, void *);
static const char *file = "/BASLUS-21782Save0/BASLUS-21782Save0";
static int poll(int expected) {
    int cmd, result;
    assert(sceMc2CheckAsync(&cmd, &result) == 1 && cmd == expected);
    assert(sceMc2CheckAsync(&cmd, &result) == -1);
    return result;
}
int main(int argc, char **argv) {
    unsigned char data[128], out[128]; int info[3]; unsigned stat[17];
    assert(sceMc2GetInfoAsync(0, info) == 0 && poll(1) == 0);
    assert(info[0] == 2 && info[1] == 1 && info[2] >= 230);
    if (!strcmp(argv[1], "write")) {
        assert(func_004326b8(0, "/BASLUS-21782Save0") == 0 && poll(1) == 0);
        assert(func_004323a8(0, file) == 0 && poll(1) == 0);
        for (int i = 0; i < 128; i++) data[i] = i;
        func_00432288(0, file, data, 0, 128); assert(poll(6) == 128);
        data[0] = 255;
        func_00432288(0, file, data, 0, 128); assert(poll(6) == 128);
        func_004323a8(0, file); assert(poll(1) == 0);
    } else if (!strcmp(argv[1], "fail")) {
        memset(data, 77, sizeof data);
        func_00432288(0, file, data, 0, 128); assert(poll(6) < 0);
    }
    func_00431f28(0, file, out, 0, 128); assert(poll(5) == 128);
    assert(out[0] == (!strcmp(argv[1], "recover") ? 0 : 255));
    for (int i = 1; i < 128; i++) assert(out[i] == i);
    stat[16] = 0x12345678;
    func_00431d78(0, file, stat); assert(poll(1) == 0 && stat[4] == 128 && stat[16] == 0x12345678);
    func_00431f28(1, file, out, 0, 128); assert((poll(5) & 0xffff) == 2);
    func_004326b8(0, "../outside"); assert(poll(1) < 0);
}
'''
        with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
            path = Path(temporary)
            card = path / 'card'
            (path / 'check.c').write_text(harness)
            exe = path / 'check.exe'
            subprocess.run([gcc, '-O2', f'-DP4_CARD_ROOT="{card.as_posix()}"', str(root / 'port/memcard.c'),
                            str(path / 'check.c'), '-o', str(exe)], check=True, capture_output=True)
            subprocess.run([str(exe), 'write'], check=True)
            subprocess.run([str(exe), 'read'], check=True)
            save = card / 'card0/BASLUS-21782Save0/BASLUS-21782Save0'
            self.assertEqual(save.stat().st_size, 128)
            Path(str(save) + '.tmp').mkdir()
            subprocess.run([str(exe), 'fail'], check=True)
            save.unlink()  # Simulate interruption after the old file was renamed to its backup.
            subprocess.run([str(exe), 'recover'], check=True)


if __name__ == '__main__':
    unittest.main()
