"""Release recipe identity, disc-part indexing and interrupted-extraction checks."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from psp_prepare import apply_recipe, extract_containers, sha256
from psp_prepare_release import make_recipe


class PreparationTests(unittest.TestCase):
    def test_recipe_roundtrip_and_wrong_edition(self):
        elf = bytearray(512)
        elf[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<I', elf, 28, 52)
        struct.pack_into('<HH', elf, 42, 32, 1)
        struct.pack_into('<8I', elf, 52, 1, 128, 0x52da00, 0, 128, 128, 0, 0)
        elf[128:256] = bytes(range(128))
        data = bytearray(elf[128:256])
        data[12:24] = bytes(12)
        struct.pack_into('<I', data, 32, 0x09001234)
        strings = struct.pack('<2I', 32, 64)
        recipe = make_recipe(bytes(elf), bytes(data), strings, b'EBOOT', 164)
        self.assertEqual(recipe['zero_ranges'], [[12, 12]])
        self.assertEqual(apply_recipe(bytes(elf), recipe), (data, strings))
        elf[250] ^= 1
        with self.assertRaisesRegex(ValueError, 'Unsupported game executable'):
            apply_recipe(bytes(elf), recipe)
        recipe['data_sha256'] = 'bad'
        elf[250] ^= 1
        with self.assertRaisesRegex(ValueError, 'does not match'):
            apply_recipe(bytes(elf), recipe)

    def test_extraction_replaces_corruption_resumes_and_indexes_offsets(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            iso, dest, state_path = folder / 'game.iso', folder / 'P4', folder / 'state.json'
            iso.write_bytes(b'prefix00' + bytes(range(64)) + b'otherEND')
            dest.mkdir()
            state = {'files': {}}
            # Small artificial part size exercises boundaries without GiB allocations.
            def files(_src, base, *_args):
                return iter([('COMMON.BIN', 2, 3), ('UNIQUE.BIN', 3, 4)]) if base == 8 + 0x1800 else iter([('COMMON.BIN', 5, 3)])
            with patch('psp_prepare.PART', 32), patch('psp_prepare.root', return_value=(0, 0)), patch('psp_prepare.walk', side_effect=files):
                table = extract_containers(iso, [('DATA', 8, 64), ('BTL', 72, 8)], dest, state, state_path)
                self.assertEqual((dest / 'DATA.0').read_bytes(), bytes(range(32)))
                self.assertEqual((dest / 'DATA.1').read_bytes(), bytes(range(32, 64)))
                self.assertFalse((dest / 'DATA.2').exists())
                index = dict((key, (where, size)) for key, where, size in struct.iter_unpack('<3I', (dest / 'p4files.idx').read_bytes()))
                from psp_disc import path_hash
                self.assertEqual(index[path_hash('COMMON.BIN')], ((2 + 3) * 8, 3))
                self.assertEqual(len(index), 2)  # first container wins
                first_mtime = (dest / 'DATA.0').stat().st_mtime_ns
                (dest / 'DATA.1').write_bytes(b'bad')
                (dest / 'DATA.1.tmp').write_bytes(b'interrupted')
                resumed = json.loads(state_path.read_text())
                second = extract_containers(iso, [('DATA', 8, 64), ('BTL', 72, 8)], dest, resumed, state_path)
                self.assertEqual(table, second)
                self.assertEqual((dest / 'DATA.0').stat().st_mtime_ns, first_mtime)
                self.assertEqual(sha256(dest / 'DATA.1'), hashlib.sha256(bytes(range(32, 64))).hexdigest())
                self.assertFalse((dest / 'DATA.1.tmp').exists())

    def test_truncated_disc_never_commits_part(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary); iso = folder / 'short.iso'; iso.write_bytes(b'short')
            with self.assertRaisesRegex(ValueError, 'truncated'):
                extract_containers(iso, [('DATA', 0, 20)], folder, {'files': {}}, folder / 'state.json')
            self.assertFalse((folder / 'DATA.0').exists())


if __name__ == '__main__':
    unittest.main()
