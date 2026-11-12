"""Execute generated quad loads/stores against the port's real register layout."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_vu0 import translate


class QuadMemory(unittest.TestCase):
    def test_fixed_stack_still_refused(self):
        self.assertIsNone(translate(r'"lq $2, 16($sp)\n"'))
        self.assertIsNone(translate(r'"lq $2, 0($gp)\n"'))

    def test_load_zero_keeps_zero_register(self):
        code = translate(r'"lq $zero, 0(%0)\n" : : "r"(source)')
        self.assertIsNotNone(code)
        self.assertNotIn('memcpy(&p4vu.r[0]', code)

    def test_quad_copy_and_memory_constraints(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        root = Path(__file__).resolve().parent.parent
        templates = [
            r'"lq $2, 16(%1)\nsq $2, 0(%0)\n" : : "r"(target), "r"(source) : "$2", "memory"',
            r'"lqc2 $vf10, 16(%1)\nsqc2 $vf10, 0(%0)\n" : "=m"(*target) : "r"(source) : "$vf10", "memory"',
            r'"lq $2, 16(%1)\nsq $2, 0(%0)\n" : "=m"(*target) : "m"(*source) : "$2", "memory"',
        ]
        generated = []
        for template in templates:
            code = translate(template)
            self.assertIsNotNone(code, template)
            generated.append(code)
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "vu0.h"
struct P4Vu p4vu;
static unsigned profiled_ops;
void p4_vu0_site(unsigned short *id, const char *file, int line, int ops) {
    profiled_ops += ops;
}
static P4Reg input[3], output;
int main(void) {
    assert((uintptr_t)input <= UINT32_MAX && (uintptr_t)&output <= UINT32_MAX);
    for (int i = 0; i < 12; i++) ((unsigned *)input)[i] = 0x10203040u + i * 0x01010101u;
    P4Reg *source = input, *target = &output;
'''
        for code in generated:
            harness += 'memset(&output, 0, sizeof output);\n' + code
            harness += '\nassert(memcmp(&output, input + 1, 16) == 0);\n'
        harness += f'assert(profiled_ops == {2 * len(templates)});\n}}\n'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness)
            # Generated PSP addresses are 32-bit; keep this host test's static buffers below 4 GB.
            target = subprocess.check_output([gcc, '-dumpmachine'], text=True)
            flags = ['-Wl,--image-base,0x400000', '-Wl,--disable-dynamicbase'] if 'mingw' in target else ['-no-pie']
            exe = path / 'check.exe'
            subprocess.run([gcc, '-O2', *flags, '-I' + str(root / 'port'), str(path / 'check.c'), '-o', str(exe)],
                           check=True, capture_output=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
