"""Execute generated quad loads/stores against the port's real register layout."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_vu0 import translate


class QuadMemory(unittest.TestCase):
    def test_effect_projection_shares_the_quad_store_with_scalar_reads(self):
        from psp_census import DECOMP, shim

        rel = "src/Graphics/Model/mdlEffect.c"
        if not (DECOMP / rel).is_file():
            self.skipTest("pinned decompilation unavailable")
        generated = shim((DECOMP / rel).read_text(encoding="utf-8"), rel)
        for lane, label in ((1, "D_00713D14"), (2, "D_00713D18")):
            self.assertNotIn(label + "[0]", generated)
            self.assertIn(f"input.{'yz'[lane - 1]} = D_00713D10[{lane}];", generated)
            self.assertEqual(generated.count(f".value = D_00713D10[{lane}];"), 2)
            self.assertIn(f"&D_00713D10[{lane}] =", generated)

    def test_encoded_square_root_updates_q_before_read(self):
        encoded = translate(r'".word 0x4A0203BD\n" "cfc2.ni $2, $vi22\n"')
        named = translate(r'"vsqrt $Q, $vf2x\n" "cfc2.ni $2, $vi22\n"')
        self.assertIsNotNone(encoded)
        self.assertEqual(encoded, named)
        self.assertLess(encoded.index("p4_vsqrt"), encoded.index("p4_float_to_reg"))
        self.assertIsNone(translate(r'".word 0xFFFFFFFF\n" "vwaitq\n"'))

    def test_quad_setter_keeps_assignment_in_return(self):
        from psp_census import void_quad_setters

        setters = """
u_long128 EffModelCopyQuad(u_long128 *dst, u_long128 *src) {
    return *dst = *src;
}
u_long128 EffModelCopyQuadSecond(u_long128 *dst, u_long128 *src) {
    return dst[1] = *src;
}
u_long128 RingSetFirstQuad(u_long128 *destination, u_long128 *source) {
    return *destination = *source;
}
u_long128 mdlEffectStoreQuad(u_long128 *destination, u_long128 *source) {
    return *destination = *source;
}
u_long128 ExistingSetQuad(u_long128 *dst, u_long128 *src) {
    *dst = *src;
    return *src;
}
"""
        generated = void_quad_setters(setters)
        self.assertEqual(generated.count("void "), 5)
        self.assertEqual(generated.count("*dst = *src;"), 2)
        self.assertIn("dst[1] = *src; return;", generated)
        self.assertEqual(generated.count("*destination = *source; return;"), 2)
        self.assertNotIn("return *", generated)
        conditional = """
u_long128 ConditionalSetQuad(u_long128 *dst, u_long128 *src, int first) {
    if (first)
        return *dst = *src;
    dst[1] = *src;
    return *src;
}
"""
        self.assertIn("if (first)\n        { *dst = *src; return; }",
                      void_quad_setters(conditional))

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
int p4_vu0_profile = 1;
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
        harness += f'assert(profiled_ops == {2 * len(templates)});\n'
        harness += 'p4_vu0_profile = 0;\nmemset(&output, 0, sizeof output);\n' + generated[0]
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
