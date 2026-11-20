#!/usr/bin/env python3
"""Read-only B0 native PSP build preparation guard; Python >=3.10, stdlib.

Example on WINDOWS in a separate disposable RESONANCE01 worktree:
  python docs/qa03/host_tests/resonance01_b0_preflight_qa08.py --workspace . --control-dir D:/backup/perf06/P4 --pspdev D:/pspdev --decomp ext/Persona4-Decompilation --iso D:/private/P4-USA.iso --retail-elf build/SLUS_217.82

The script DOES NOT run compilation, extract ISO, alter Git, create files or
delete builds. --self-test only writes a disposable temporary mock fixture.
"PASS" means inputs checked; it NEVER certifies a playable PSP EBOOT.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import tempfile

FROZEN = "29b0fcfdec70222a4052099b67fb4a11e8d8aff4"
DECOMP = "363eab8a966d60a9773b45f330f2344329ec4f0a"
RETAIL_SHA1 = "4eeec0360cf2715535d9f7e52eb69d786fb0158c"
TOOLS = ("psp-gcc", "psp-nm", "psp-as", "psp-ld", "psp-objcopy",
         "psp-objdump", "psp-size", "psp-strip", "psp-fixup-imports",
         "psp-prxgen", "mksfoex", "pack-pbp")
SOURCES = ("tools/psp_census.py", "tools/psp_ptr_proof.py",
           "tools/psp_link.py", "tools/psp_hw_debug.py",
           "tools/psp_release.py", "tools/psp_make_data.py",
           "port/main.c", "port/rwsky.c", "port/paths.c", "docs/building.md")
TRIPLET = ("EBOOT.PBP", "p4data.bin", "p4str.bin")


def hash_file(path, algorithm="sha256"):
    h = hashlib.new(algorithm)
    with Path(path).open("rb") as stream:
        for b in iter(lambda: stream.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def includes(a, b):
    return Path(a).resolve().is_relative_to(Path(b).resolve())


def control_hashes(folder, expected=None):
    hashes, problems = {}, []
    for name in TRIPLET:
        p = Path(folder) / name
        if not p.is_file() or not p.stat().st_size:
            problems.append(name + " missing/empty")
            continue
        hashes[name] = hash_file(p)
        if expected is not None and expected.get(name) != hashes[name]:
            problems.append(name + " changed from pinned SHA256")
    return hashes, problems


def git_read(path, *cmd):
    try:
        v = subprocess.run(["git", "-C", str(path), *cmd], capture_output=True,
                           text=True, timeout=12, check=False)
        return v.stdout.strip() if v.returncode == 0 else None
    except (OSError, subprocess.SubprocessError):
        return None


class Report:
    def __init__(self):
        self.items = []

    def add(self, status, label, note):
        self.items.append((status, label, note))

    def block(self, label, note):
        self.add("BLOCKED", label, note)

    def ok(self, label, note):
        self.add("PASS", label, note)

    def warn(self, label, note):
        self.add("WARN", label, note)

    def blockers(self):
        return sum(x[0] == "BLOCKED" for x in self.items)


def audit(args, host=None, git=git_read):
    r = Report()
    root = Path(args.workspace).resolve()
    if (host or platform.system()) != "Windows":
        r.block("Host OS", "Frozen builder requires Windows PSPDEV *.exe, not Linux native gcc")
    else:
        r.ok("Host OS", "Windows; PSP tool files checked separately")
    if not root.is_dir():
        r.block("Experiment workspace", "Not a directory")
        return r
    missing = [p for p in SOURCES if not (root / p).is_file()]
    if missing:
        r.block("Build pipeline", "Missing " + ", ".join(missing))
    else:
        r.ok("Build pipeline", "Expected native builder source paths present")

    if not args.control_dir or not Path(args.control_dir).is_dir():
        r.block("Immutable perf06 control", "Supply separate --control-dir with EBOOT/P4DATA/P4STR")
    else:
        control = Path(args.control_dir).resolve()
        if includes(control, root) or includes(root, control):
            r.block("Workspace isolation", "Control is within experiment workspace or vice versa")
        else:
            r.ok("Workspace isolation", "Experiment/control roots separate")
            try:
                pinned = json.loads(Path(args.control_hashes).read_text()) if args.control_hashes else None
                if pinned is not None and not isinstance(pinned, dict):
                    raise ValueError("Expected control hashes JSON object")
                actual, issues = control_hashes(control, pinned)
                if issues:
                    r.block("Control artifact match", "; ".join(issues))
                else:
                    r.ok("Control artifact match", "Three nonempty control artifacts rehashed")
                if pinned is None:
                    r.warn("Baseline trust", "No independent private --control-hashes supplied")
                for name, sha in actual.items():
                    r.ok("Private SHA256 " + name, sha + " (do not commit private logs)")
            except (OSError, ValueError, TypeError) as exc:
                r.block("Pinned control metadata", str(exc))

    pspdev = Path(args.pspdev or os.environ.get("PSPDEV") or root / "toolchain/pspdev").resolve()
    missing = [name for name in TOOLS if not (pspdev / "bin" / (name + ".exe")).is_file()]
    if missing:
        r.block("Windows PSPDEV", "Missing tools: " + ", ".join(missing))
    else:
        r.ok("Windows PSPDEV", "All 12 required .exe files present (not executed)")
    if not (pspdev / "psp/sdk/lib/linkfile.prx").is_file():
        r.block("PSPSDK", "Missing psp/sdk/lib/linkfile.prx")
    r.warn("Compiler identity", "Presence does not authenticate compiler/SDK hashes; record BUILDINFO + toolchain_identity() separately")

    dec = Path(args.decomp or root / "ext/Persona4-Decompilation").resolve()
    if not dec.is_dir():
        r.block("Original decomp", "Pinned checkout unavailable")
    else:
        if git(dec, "rev-parse", "HEAD") != DECOMP:
            r.block("Original decomp", "Git HEAD does not match pinned commit or cannot be inspected")
        else:
            r.ok("Original decomp", "Exact pinned Git HEAD")
        dirty = git(dec, "status", "--porcelain", "--untracked-files=no")
        if dirty is None or dirty:
            r.block("Original decomp", "Tracked modifications or inaccessible clean-state proof")

    root_head = git(root, "rev-parse", "HEAD")
    if root_head:
        r.ok("Experimental Git", "HEAD " + root_head)
        changed = git(root, "diff", "--name-only", FROZEN, "HEAD", "--",
                      "port", "src", "data", "build")
        if changed is None:
            r.warn("Frozen runtime comparison", "Source revision comparison unavailable")
        elif changed:
            r.warn("Runtime delta", "Native files differ from frozen baseline: inspect intentional patch owners")
        else:
            r.ok("Runtime baseline", "No committed port/src/data/build differences (not a binary SHA proof)")
        dirty = git(root, "status", "--porcelain", "--untracked-files=no")
        if dirty:
            r.warn("Experiment working tree", "Tracked dirty files; record exact patch before building")
    else:
        r.block("Experimental Git", "No readable commit for source provenance")

    if args.iso and Path(args.iso).is_file() and Path(args.iso).stat().st_size:
        r.ok("Private ISO", "Nonempty source file present; ISO contents NOT validated")
    else:
        r.block("Private ISO", "Original supported USA ISO missing")
    if args.retail_elf and Path(args.retail_elf).is_file():
        try:
            if hash_file(args.retail_elf, "sha1") == RETAIL_SHA1:
                r.ok("Retail SLUS_217.82", "Full SHA1 matches supported USA executable")
            else:
                r.block("Retail SLUS_217.82", "Wrong full SHA1; do not build")
        except OSError as exc:
            r.block("Retail SLUS_217.82", str(exc))
    else:
        r.block("Retail SLUS_217.82", "Supply exact --retail-elf after safe extraction")

    objects = root / "build/obj"
    if objects.is_dir() and any(objects.glob("*.o")):
        r.warn("Original compiled objects", "Existing *.o: isolate and clean initial own census; linker mutates .o with objcopy")
    if (root / "build/psp_dist").exists():
        r.warn("Packaging output", "Builder can overwrite this folder; experimental disposable tree ONLY")
    r.warn("Evidence ceiling", "READ-ONLY host preflight: no native compile, PRX/ELF owner proof or PSP Go test")
    return r


def self_test():
    passed = 0
    with tempfile.TemporaryDirectory(prefix="p4p_qa08_") as td:
        work = Path(td) / "experiment"
        control = Path(td) / "perf06"
        work.mkdir()
        control.mkdir()
        for i, file in enumerate(TRIPLET):
            (control / file).write_bytes(bytes([i + 1]) * 32)
        good, issues = control_hashes(control)
        cases = [
            ("valid three-file mock control", not issues and len(good) == 3),
            ("separate project trees", not includes(work, control) and not includes(control, work)),
            ("same project rejected", includes(work, work)),
            ("two different immutable pins", DECOMP != FROZEN and len(FROZEN) == 40),
            ("Windows-specific compiler inventory", "psp-gcc" in TOOLS and len(TOOLS) == 12),
            ("destructive stages known", "tools/psp_link.py" in SOURCES and "tools/psp_release.py" in SOURCES),
        ]
        (control / "p4data.bin").write_bytes(b"changed")
        cases.append(("tampered data SHA found", "p4data.bin" in ";".join(control_hashes(control, good)[1])))
        (control / "p4data.bin").unlink()
        cases.append(("missing data found", "p4data.bin" in ";".join(control_hashes(control)[1])))
        (control / "p4data.bin").write_bytes(b"")
        cases.append(("empty data found", "p4data.bin" in ";".join(control_hashes(control)[1])))
        cases.append(("mock is not genuine retail ELF", hash_file(control / "EBOOT.PBP", "sha1") != RETAIL_SHA1))
        for name, passed_case in cases:
            if not passed_case:
                raise AssertionError(name)
            passed += 1
            print(f"PASS {passed:02d} {name}")
    print(f"ALL {passed}/10 QA08 synthetic B0 checks PASS. No PSPDEV/PSP build was run.")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workspace", type=Path, default=Path("."))
    p.add_argument("--control-dir", type=Path)
    p.add_argument("--control-hashes", type=Path)
    p.add_argument("--pspdev", type=Path)
    p.add_argument("--decomp", type=Path)
    p.add_argument("--iso", type=Path)
    p.add_argument("--retail-elf", type=Path)
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    report = audit(args)
    for status, label, note in report.items:
        print(f"{status}: {label}: {note}")
    if report.blockers():
        print(f"NO-GO: {report.blockers()} B0 build blocker(s). Source-only investigation allowed.")
        raise SystemExit(2)
    print("B0 INPUT CHECK PASS ONLY. Real builder, ELF owner, GE safety and PSP Go still UNTESTED.")


if __name__ == "__main__":
    main()
