"""Verify the retail window text table without treating its behavior flags as pointers."""
import unittest

from psp_ptr_proof import CUTIN_SUFFIXES, CUTIN_TABLE, FUSION_RECIPES, TEST_MENU_NAMES, PANEL_TABLE, TERRAIN_TABLE, WINDOW_TEXT, WINDOW_CHOICES, collect, load, word


class WindowPointers(unittest.TestCase):
    def test_typed_tables_do_not_depend_on_neighbour_values(self):
        image, _, _ = load()
        proven, candidates = collect(image, set(), [])
        for address in (*CUTIN_TABLE, *FUSION_RECIPES, *TEST_MENU_NAMES):
            target = word(image, address)
            with self.subTest(address=hex(address)):
                if target:
                    self.assertEqual(proven[address], target)
                    self.assertNotIn(address, candidates)
        self.assertEqual(proven[0x5DC8F0], 0x795050)
        self.assertEqual(proven[0x642F20], 0x642E40)
        self.assertEqual(proven[0x5F5630], 0x5F5528)
        for address in range(0x642F04, 0x642F28, 8):
            self.assertNotIn(address, proven)  # adjacent recipe counts are integers

    def test_day_and_evening_cutin_resource_suffixes(self):
        image, _, _ = load()
        proven, candidates = collect(image, set(), [])
        for address, suffix in zip(CUTIN_SUFFIXES, (b'S\0', b'W\0', b'S\0', b'W\0')):
            with self.subTest(address=hex(address)):
                target = word(image, address)
                self.assertEqual(proven[address], target)
                self.assertNotIn(address, candidates)
                self.assertEqual(image[target - 0x100000:target - 0x100000 + 2], suffix)
        self.assertNotIn(0x763618, proven)  # neighbouring printf format is text, not a pointer

    def test_location_panel_tables_include_bss(self):
        image, _, _ = load()
        proven, candidates = collect(image, set(), [])
        self.assertEqual(len(PANEL_TABLE), 40)
        for address in PANEL_TABLE:
            target = word(image, address)
            with self.subTest(address=hex(address)):
                if target:
                    self.assertEqual(proven[address], target)
                else:
                    self.assertNotIn(address, proven)
                self.assertNotIn(address, candidates)
        self.assertEqual(proven[0x5F51FC], 0x763068)
        self.assertEqual(proven[0x5F5210], 0x7F1770)

    def test_confirmation_choices(self):
        image, _, _ = load()
        proven, _ = collect(image, set(), [])
        self.assertEqual([proven[a] for a in WINDOW_CHOICES], [0x763788, 0x76378C])

    def test_terrain_tables_include_zero_bytes_and_bss(self):
        image, _, _ = load()
        proven, _ = collect(image, set(), [])
        for address in TERRAIN_TABLE:
            target = word(image, address)
            with self.subTest(address=hex(address)):
                if target:
                    self.assertEqual(proven[address], target)
                else:
                    self.assertNotIn(address, proven)
        self.assertEqual(proven[0x5F08CC], 0x762F2C)
        self.assertEqual(proven[0x5F0954], 0x764338)

    def test_all_window_messages(self):
        image, _, _ = load()
        proven, candidates = collect(image, set(), [])
        self.assertEqual(len(WINDOW_TEXT), 23)
        for address in WINDOW_TEXT:
            with self.subTest(address=hex(address)):
                self.assertEqual(proven[address], word(image, address))
                self.assertNotIn(address + 4, proven)
                self.assertNotIn(address + 4, candidates)


if __name__ == "__main__":
    unittest.main()
