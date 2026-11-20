import importlib.util
import os
import struct
import tempfile
import unittest
from pathlib import Path

_here = Path(__file__).with_name("qa13_census_prelink_gate.py")
_spec = importlib.util.spec_from_file_location("qa13_census", _here)
g = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(g)


def mock_mips32():
    h = bytearray(52)
    h[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", h, 16, 1, 8, 1)
    return bytes(h)


class QA13Test(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory(prefix="qa13-b0-")
        self.addCleanup(self.t.cleanup)
        self.root = Path(self.t.name)
        self.mirror = self.root / "build/census"
        self.obj = self.root / "build/obj"
        self.obj.mkdir(parents=True)
        for rel in ("src/Field/fldDraw.c", "src/Battle/btlCompendium.c"):
            file = self.mirror / rel
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("int demo(void) {return 1;}\n")
            output = self.obj / g.normalized_name(rel)
            output.write_bytes(mock_mips32())
            os.utime(output, None)
        (self.mirror / "failed_compile.txt").write_text("\n")
        (self.mirror / "report_compile.txt").write_text(
            "FIRST: 0/2 files fail, 0 errors\nRW: 0/0 files fail, 0 errors\nCRI: 0/0 files fail, 0 errors\n"
            "SCE: 0/0 files fail, 0 errors\nMIDDLEWARE: 0/0 files fail, 0 errors\n")

    def why(self):
        return g.scan(self.root)[0]

    def test_01_clean(self):
        self.assertEqual(self.why(), [])

    def test_02_missing_object(self):
        next(self.obj.glob("*.o")).unlink()
        self.assertTrue(any(x.startswith("MISSING_OBJECT:") for x in self.why()))

    def test_03_extra_stale_object(self):
        (self.obj / "old.o").write_bytes(mock_mips32())
        self.assertIn("STALE_EXTRA_OBJECT:old.o", self.why())

    def test_04_zero_object(self):
        next(self.obj.glob("*.o")).write_bytes(b"")
        self.assertTrue(any(x.startswith("BAD_OBJECT:") for x in self.why()))

    def test_05_wrong_host_architecture(self):
        p = next(self.obj.glob("*.o"))
        h = bytearray(mock_mips32())
        struct.pack_into("<H", h, 18, 62)
        p.write_bytes(h)
        self.assertTrue(any("not a MIPS object" in x for x in self.why()))

    def test_06_wrong_elf_type(self):
        p = next(self.obj.glob("*.o"))
        h = bytearray(mock_mips32())
        struct.pack_into("<H", h, 16, 2)
        p.write_bytes(h)
        self.assertTrue(any("not a relocatable ELF" in x for x in self.why()))

    def test_07_failed_compile_entry(self):
        (self.mirror / "failed_compile.txt").write_text("src/Field/fldDraw.c\n")
        self.assertTrue(any(x.startswith("NONEMPTY_FAILED_COMPILE_REPORT") for x in self.why()))

    def test_08_report_records_failure(self):
        p = self.mirror / "report_compile.txt"
        p.write_text(p.read_text().replace("FIRST: 0/2 files fail, 0 errors", "FIRST: 1/2 files fail, 7 errors"))
        self.assertIn("COMPILE_SUMMARY_FAILURES", self.why())

    def test_09_missing_report(self):
        (self.mirror / "report_compile.txt").unlink()
        self.assertIn("MISSING_COMPILE_SUMMARY", self.why())

    def test_10_mismatched_census_count(self):
        p = self.mirror / "report_compile.txt"
        p.write_text(p.read_text().replace("FIRST: 0/2 files fail", "FIRST: 0/3 files fail"))
        self.assertTrue(any(x.startswith("REPORTED_SOURCE_COUNT_MISMATCH") for x in self.why()))

    def test_11_mirror_newer_than_object(self):
        source = next((self.mirror / "src").rglob("*.c"))
        p = self.obj / g.normalized_name(source.relative_to(self.mirror).as_posix())
        os.utime(source, ns=(p.stat().st_mtime_ns + 10_000_000_000,) * 2)
        self.assertTrue(any(x.startswith("OBJECT_PREDATES_MIRROR_SOURCE") for x in self.why()))

    def test_12_source_symlink(self):
        source = next((self.mirror / "src").rglob("*.c"))
        real = self.root / "external.c"
        source.rename(real)
        source.symlink_to(real)
        self.assertTrue(any(x.startswith("LINKED_SOURCE") for x in self.why()))

    def test_13_object_symlink(self):
        obj = next(self.obj.glob("*.o"))
        real = self.root / "external.o"
        obj.rename(real)
        obj.symlink_to(real)
        self.assertTrue(any(x.startswith("LINKED_OBJECT") for x in self.why()))

    def test_14_duplicate_compile_group(self):
        p = self.mirror / "report_compile.txt"
        p.write_text(p.read_text() + "FIRST: 0/2 files fail, 0 errors\n")
        self.assertIn("DUPLICATE_COMPILE_GROUP:FIRST", self.why())

    def test_15_missing_failed_report(self):
        (self.mirror / "failed_compile.txt").unlink()
        self.assertIn("MISSING_FAILED_COMPILE_REPORT", self.why())

    def test_16_no_census_source(self):
        for p in (self.mirror / "src").rglob("*.c"):
            p.unlink()
        self.assertIn("EMPTY_CENSUS_SOURCE_MIRROR", self.why())


if __name__ == '__main__':
    unittest.main(verbosity=2)
