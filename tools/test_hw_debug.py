"""PRX data pointers must include the actual text load base."""
import unittest
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace

import psp_make_data


class RelocatedFunctionPointers(unittest.TestCase):
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
