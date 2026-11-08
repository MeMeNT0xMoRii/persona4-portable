"""Regression checks for retail address casts in the PSP source mirror."""
import unittest

from psp_census import shim, trap_ee_asm, void_quad_setters
from psp_make_data import K


class AddressCasts(unittest.TestCase):
    def test_script_integer_result_matches_canonical_header(self):
        self.assertEqual(shim('extern s64 scrGetParamInt();'), 'extern s32 scrGetParamInt();')
        self.assertEqual(shim('s64 actual_wide_result(void);'), 's64 actual_wide_result(void);')

    def test_quad_callback_aliases_do_not_add_hidden_result_pointer(self):
        for quad in ('s128', 'u128', 'u_long128'):
            with self.subTest(quad=quad):
                source = f'{quad} EffBattleStoreRotation(char *instance, {quad} *value) {{\n{quad} copy = *value; (({quad} *)instance)[1] = copy; return copy;\n}}'
                converted = void_quad_setters(source)
                self.assertTrue(converted.startswith('void EffBattleStoreRotation('))
                self.assertIn(')[1] = copy; return;', converted)
                self.assertNotIn('return copy;', converted)
        self.assertEqual(void_quad_setters('s128 get_value(void) { return value; }'),
                         's128 get_value(void) { return value; }')

    def test_battle_effects_use_shared_state(self):
        self.assertEqual(shim('static u8 *D_0072449C;', 'src/Battle/btlEPL.c'),
                         'extern u8 *D_0072449C;')

    def test_named_stack_colour_output(self):
        asm = r'''void f(void) { __asm__ volatile("qmfc2.ni $2, $vf10\n" "ppach $2, $0, $2\n" "ppacb $2, $0, $2\n" "sw $2, 0x64($sp)\n" : : : "$2", "memory");'''
        reload = '\n/* Output slot in the retail stack. */\nresult = *(volatile s32 *)&combinedColor;'
        translated = trap_ee_asm(asm + reload)
        self.assertNotIn('P4_EE_ASM()', translated)
        self.assertIn('((char *)&combinedColor)', translated)
        self.assertIn('p4_ppacb(', translated)
        self.assertIn('P4_EE_ASM()', trap_ee_asm(asm + '\nresult = combinedColor;'))
        self.assertIn('P4_EE_ASM()', trap_ee_asm(asm.replace('sw $2', 'lw $2') + reload))

    def test_data_and_callback_addresses(self):
        for cast in ("(u32 *)", "(u32 **)", "(s32 (**)(s32, void*, s32))"):
            for literal in ("0x00887310", "0x887310"):
                with self.subTest(cast=cast, literal=literal):
                    source = f"*{cast}{literal}"
                    self.assertEqual(shim(source), f"*{cast}{0x887310 + K:#x}")

    def test_non_address_values(self):
        for source in ("mask = 0x00887310;", "(void *)0x10000000", "(void *)0x08873100"):
            with self.subTest(source=source):
                self.assertEqual(shim(source), source)


if __name__ == "__main__":
    unittest.main()
