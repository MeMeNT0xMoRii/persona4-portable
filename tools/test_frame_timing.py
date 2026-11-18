"""Exercise window attribution, movie boundaries and the PSP's wrapping clock."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FrameTiming(unittest.TestCase):
    def test_windows_and_finish_callback(self):
        cc = shutil.which("cc")
        if not cc:
            self.skipTest("C compiler required")
        source = r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define p4_log printf
#include "frame_timing.h"
int main(void) {
    assert(timing_ge_tail(99,30,0,1,111)==0);
    assert(timing_ge_tail(99,30,1,1,111)==12);
    assert(timing_ge_tail(99,30,1,0,90)==30);
    assert(timing_ge_tail(99,30,1,1,92)==0);
    assert(timing_ge_tail(99,30,1,1,139)==30);
    assert(timing_ge_tail(0xfffffff0,30,1,1,2)==18);
    unsigned now=0xffffffb0;
    timing_record(0,now,now,now,0,0,1);
    for (unsigned n=1; n<=32; ++n) {
        timing_writeback=3*n;
        timing_copy_bytes=2*n;
        timing_record(n,now+10*n,now+10*n+2,now+10*n+5,1,4*n,1);
        now+=10*n+5;
    }
    assert(!timing_window.count);
    timing_movie(0,now);
    timing_movie(1,900000);
    timing_record(33,900004,900010,900020,3,80,1);
    assert(timing_window.peak.total==20 && !timing_window.peak.writeback);
    timing_movie(0,900035);
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "timing"
            port = Path(__file__).resolve().parents[1] / "port"
            subprocess.run([cc, "-std=c99", "-I", port, "-x", "c", "-", "-o", exe],
                           input=source, text=True, check=True)
            log = subprocess.check_output([exe], text=True)
        self.assertIn("last=32 n=32 pipe=32 p50=165 p95=315 cpu=165 wait=2 ge_tail=1 post=3", log)
        self.assertIn("pipe=1 wall=325 cpu=320 wait=2 ge_tail=1 post=3 listB=128 rasterWB=96 copyB=64", log)
        self.assertIn("hwfirst f=33 t=900020 wall=20 listB=80 pipe=1", log)
        self.assertIn("last=33 n=1 pipe=1 p50=20 p95=20", log)
