"""Exercise the port's actual camera path interpolation at a repeated endpoint key."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from psp_census import DECOMP, shim


class FieldCamera(unittest.TestCase):
    def test_endpoint_and_regular_interpolation(self):
        gcc = shutil.which('gcc')
        if not gcc:
            self.skipTest('host gcc is unavailable')
        rel = 'src/Kosaka/Field/k_fldFrame_inferred_0016b8a0.c'
        source = shim((DECOMP / rel).read_text(), rel)
        interpolation = re.search(r'interpolationFraction = [^;]+;', source)[0]
        harness = r'''
#include <assert.h>
static float sample(float seededTime, float lowTime, float span, float lo, float delta) {
    float interpolationFraction;
''' + interpolation + r'''
    return lo + delta * interpolationFraction;
}
int main(void) {
    assert(sample(6.0f/7, 6.0f/7, 0, 188, 0) == 188);
    assert(sample(.75f, .5f, .5f, 10, 20) == 20);
    assert(sample(.5f, .5f, .5f, 10, 20) == 10);
    assert(sample(1, .5f, .5f, 10, 20) == 30);
}
'''
        root = Path(__file__).resolve().parent.parent
        with tempfile.TemporaryDirectory(dir=root / 'build') as temporary:
            path = Path(temporary)
            (path / 'check.c').write_text(harness)
            subprocess.run([gcc, '-O2', str(path / 'check.c'), '-o', str(path / 'check.exe')],
                           check=True, capture_output=True)
            subprocess.run([str(path / 'check.exe')], check=True)


if __name__ == '__main__':
    unittest.main()
