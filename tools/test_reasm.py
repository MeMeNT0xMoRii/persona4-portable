"""Check COP1 encodings whose operand fields differ between EE and Allegrex."""
import unittest

from psp_reasm import cop1


class SquareRoot(unittest.TestCase):
    def test_ee_source_is_ft(self):
        # Quaternion conversion's negative-trace branch: sqrt.s f3, f5.
        self.assertEqual(cop1(0x460500C4, (19, 18)), ["sqrt.s $f3, $f5"])

    def test_zero_source(self):
        self.assertEqual(cop1(0x46000084, (19, 18)), ["sqrt.s $f2, $f0"])


if __name__ == "__main__":
    unittest.main()
