"""PRX data pointers must include the actual text load base."""
import unittest
import os
import struct
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

import psp_make_data


class RelocatedFunctionPointers(unittest.TestCase):
    def test_rush_anchor_matches_both_retail_loads(self):
        from psp_data_census import BASE, ELF, load
        from psp_xref import find_gp
        with patch.dict(os.environ, PSPDEV=os.environ.get("PSPDEV", ".")):
            from psp_link import K, correct_retail_data_symbols, retail_address
        expected = retail_address("fGpffff84a0", {}, 0x7690f0)
        self.assertEqual(expected, 0x7615a0)
        staged = "fGpffff849c = 0x924158c;\nfGpffff84a0 = 0x9241590;\n"
        self.assertEqual(correct_retail_data_symbols(staged),
                         f"fGpffff849c = 0x924158c;\nfGpffff84a0 = {expected + K:#x};\n")
        if not ELF.exists():
            self.skipTest("original US executable is unavailable for the instruction check")
        image = load()[0]
        for pc in (0x2117a4, 0x2118bc):
            word, = struct.unpack_from("<I", image, pc - BASE)
            self.assertEqual((word >> 26, word >> 21 & 31), (0x31, 28))
            displacement, = struct.unpack_from("<h", image, pc - BASE)
            self.assertEqual(find_gp(image) + displacement, expected)
        self.assertEqual(struct.unpack_from("<f", image, expected - BASE)[0], 420.5)

    def test_prx_offsets_become_runtime_pointers(self):
        with patch.object(Path, "exists", return_value=True), \
             patch.object(psp_make_data.subprocess, "run", return_value=SimpleNamespace(
                 stdout="00001000 T first\n00002000 t second\n00003000 D data\n")), \
             patch.object(psp_make_data, "retail_to_native", side_effect=lambda linked: linked):
            self.assertEqual(psp_make_data.native_functions(Path("diagnostic.elf"), 0x08804000),
                             {"first": 0x08805000, "second": 0x08806000})

    def test_static_elf_addresses_keep_their_base(self):
        with patch.object(Path, "exists", return_value=True), \
             patch.object(psp_make_data.subprocess, "run", return_value=SimpleNamespace(
                 stdout="08805000 T first\n")), \
             patch.object(psp_make_data, "retail_to_native", side_effect=lambda linked: linked):
            self.assertEqual(psp_make_data.native_functions(Path("release.elf")), {"first": 0x08805000})


if __name__ == "__main__":
    unittest.main()
