"""Prove the audit distinguishes constants from memory accesses and follows delay slots."""
import unittest

from psp_address_audit import low_code_references
from psp_reasm import Function, scan_addresses, translate
from psp_xref import symbol_addresses


class AddressAudit(unittest.TestCase):
    def test_signed_offsets_and_ori_addresses(self):
        words = [0x3c020072, 0x344288ac, 0xac440000, 0x3c030071, 0xac64c5d0]
        rows = list(low_code_references(words, 0x8800000))
        self.assertEqual(rows, [(0x8800004, 0x7288ac, 'formed_constant'),
                                (0x8800008, 0x7288ac, 'direct_memory'),
                                (0x8800010, 0x70c5d0, 'direct_memory')])

    def test_delay_slot_is_checked_and_next_function_forgets_constants(self):
        words = [0x3c020071, 0x03e00008, 0xac4465f0, 0xac4465f0]
        self.assertEqual(list(low_code_references(words, 0x8800000)),
                         [(0x8800008, 0x7165f0, 'direct_memory')])
        # A bit mask is only a numeric candidate; it must not fail the build as a memory access.
        self.assertEqual(list(low_code_references([0x3c02007f, 0x3442ffff], 0x8800000)),
                         [(0x8800004, 0x7fffff, 'formed_constant')])

    def test_file_device_callback_is_relocated_as_code(self):
        names = symbol_addresses()
        self.assertEqual(names['func_003de2f0'], 0x3de2f0)
        fn = Function(0x3dea90, 0x3dea9c, [0x3c03003e, 0x2463e2f0, 0xac430028])
        scan_addresses(fn, None, lambda a: a in names.values())
        output = '\n'.join(translate(fn, lambda a: f'p4r_{a:08x}', {}))
        self.assertIn('%hi(p4r_003de2f0)', output)
        self.assertIn('%lo(p4r_003de2f0)', output)
        self.assertIn(0x3de2f0, fn.calls)

    def test_ps2_register_and_scratchpad_accesses_are_reported(self):
        self.assertEqual(list(low_code_references([0x3c021200, 0xac440010, 0x3c037000, 0x8c650000], 0x8800000)),
                         [(0x8800004, 0x12000010, 'ps2_hardware_memory'),
                          (0x880000c, 0x70000000, 'ps2_hardware_memory')])


if __name__ == '__main__':
    unittest.main()
