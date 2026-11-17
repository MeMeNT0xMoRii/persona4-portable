"""Check the renderer's animation diagnostics after the layer table fills."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class ScaleTrace(unittest.TestCase):
    def test_saturated_table_preserves_bounded_outlier_reports(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("host gcc is unavailable")
        renderer = (Path(__file__).resolve().parents[1] / "port/rwsky.c").read_text()
        trace = re.search(r"^void p4_scale_hook_note\(.*?^\}", renderer, re.M | re.S)[0]
        harness = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static unsigned normal_reports, outlier_reports;

static void p4_log(const char *format, ...) {
    char line[160];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (strstr(line, " ODD")) outlier_reports++;
    else normal_reports++;
}
''' + trace + r'''
int main(void) {
    unsigned layers[24];
    const float normal[3] = {1, 1, 1};
    const float outlier[3] = {1, 0, 1};
    for (int repeat = 0; repeat < 32; repeat++)
        for (int i = 0; i < 24; i++)
            p4_scale_hook_note(layers + i, 28, normal, 1, 1, 0);
    assert(normal_reports == 16 && !outlier_reports);
    for (int i = 0; i < 64; i++)
        p4_scale_hook_note(layers + (i & 1 ? 23 : 0), 28, outlier, 1, 1, 0);
    assert(normal_reports == 16 && outlier_reports == 32);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "check.c"
            binary = source.with_suffix("")
            source.write_text(harness)
            subprocess.run([gcc, "-O2", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
