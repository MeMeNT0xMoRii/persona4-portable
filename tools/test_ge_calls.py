"""Check the input hash and address words consumed by the GE CALL path."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


RENDERER = (Path(__file__).resolve().parents[1] / "port/rwsky.c").read_text()


def function(name):
    return re.search(rf"^static [^\n]*\b{name}\([^\n]*\)\n\{{.*?^\}}", RENDERER, re.M | re.S)[0]


class GeCalls(unittest.TestCase):
    def run_fixture(self, code):
        cc = shutil.which("gcc")
        if not cc:
            self.skipTest("host gcc is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "check.c"
            executable = source.with_suffix("")
            source.write_text(code)
            subprocess.run([cc, "-O2", str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_address_words_omit_only_equal_base(self):
        self.run_fixture(r"""
#include <assert.h>
#include <stdint.h>
""" + function("ge_draw_addresses") + r"""
static void check(uintptr_t index, uintptr_t vertex, int expected_words)
{
    unsigned words[5] = {0, 0, 0, 0, 0xDEADBEEF};
    unsigned *end = ge_draw_addresses(words, (const void *)index, (const void *)vertex);
    assert(end == words + expected_words && words[4] == 0xDEADBEEF);
    unsigned base = 0x04000000, iaddr = 0xAA, vaddr = 0xBB;
    unsigned offset = 0x1000;
    for (const unsigned *word = words; word < end; word++) {
        unsigned command = *word >> 24, value = *word & 0xFFFFFF;
        switch (command) {
        case 0x10: base = (value & 0xF0000) << 8; break;
        case 0x02: iaddr = base + value + offset; break;
        case 0x01: vaddr = base + value + offset; break;
        default: assert(0);
        }
    }
    assert(iaddr == (index ? (index & 0x0FFFFFFF) + offset : 0xAA));
    assert(vaddr == (vertex ? (vertex & 0x0FFFFFFF) + offset : 0xBB));
    assert(base == ((vertex ? vertex : index ? index : 0x04000000) & 0x0F000000));
}

int main(void)
{
    check(0, 0, 0);
    check(0, 0x08814000, 2);
    check(0x08818008, 0, 2);
    check(0x08818008, 0x08814000, 3);
    check(0x08FFFC08, 0x09000000, 4);
    check(0x09000100, 0x08FFFE00, 4);
    check(0x48818008, 0x08814000, 3); /* cached and uncached aliases */
}
""")
