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

    def test_call_key_matches_full_reference_with_camera_gap(self):
        state = re.search(r"static struct \{\n    unsigned enabled, known;.*?^\} gs;",
                          RENDERER, re.M | re.S)[0]
        offsets = "\n".join(re.findall(r"^#define CALL_CAM[01].*$", RENDERER, re.M))
        self.run_fixture(r"""
#include <assert.h>
#include <stddef.h>
#include <string.h>
""" + "\n".join((state, offsets, function("call_mix"), function("call_key"))) + r"""
static void reference(const unsigned *input, int count, unsigned out[2])
{
    out[0] = 2166136261u;
    out[1] = 0x9e3779b9u;
    for (int i = 0; i < count; i++)
        call_mix(out, input[i]);
    unsigned words[sizeof gs / sizeof(unsigned)];
    memcpy(words, &gs, sizeof gs);
    for (size_t i = 0; i < sizeof words / sizeof *words; i++) {
        if (i * sizeof(unsigned) < CALL_CAM0 || i * sizeof(unsigned) >= CALL_CAM1)
            call_mix(out, words[i]);
    }
}

static unsigned random_word(void)
{
    static unsigned seed = 0xb249;
    seed = seed * 1664525u + 1013904223u;
    return seed;
}

int main(void)
{
    unsigned inputs[48], expected[2], actual[2], moved_camera[2];
    unsigned char *bytes = (unsigned char *)&gs;
    for (int j = 0; j < 4096; j++) {
        for (size_t i = 0; i < sizeof gs; i++)
            bytes[i] = random_word() >> 24;
        for (int i = 0; i < 48; i++)
            inputs[i] = random_word();
        int count = j % 49;
        reference(inputs, count, expected);
        call_key(inputs, count, actual);
        assert(!memcmp(expected, actual, sizeof actual));
        for (size_t i = CALL_CAM0; i < CALL_CAM1; i++)
            bytes[i] ^= 0xff;
        call_key(inputs, count, moved_camera);
        assert(!memcmp(actual, moved_camera, sizeof actual));
    }
}
""")
