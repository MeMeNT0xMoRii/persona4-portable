#!/usr/bin/env python3
"""QA13 fail-closed PRE-LINK game-object inventory check (read-only).

Run ONLY after a successful isolated psp_census.py --compile, before
psp_link.py weakens/mutates selected game .o files. This gate verifies that
one fresh MIPS ELF32 relocatable object exists for each mirrored source and
no extra old compiled object can sneak into the link inventory.

It cannot certify the toolchain, source semantics, SDK linking, symbol owner,
compiled machine-code equivalence or PSP execution. Use QA08 before compile,
QA10/QA07 on the actual final ELF / artifacts after link.

    python docs/qa03/host_tests/qa13_census_prelink_gate.py --workspace .
Exit: 0 inventory PASS, 2 NO-GO. No writes outside its own test harness.
"""
from __future__ import annotations
import argparse
import re
import struct
from pathlib import Path

GROUPS = ("FIRST", "RW", "CRI", "SCE", "MIDDLEWARE")
GROUP_RE = re.compile(r"^(FIRST|RW|CRI|SCE|MIDDLEWARE):\s+(\d+)/(\d+)\s+files fail,\s+(\d+)\s+errors\s*$")


def normalized_name(rel: str) -> str:
    """Match exactly tools/psp_census.py::check() object naming."""
    return rel.replace("/", "__")[:-2] + ".o"


def mips_elf_object(path: Path) -> str | None:
    try:
        with path.open("rb") as f:
            h = f.read(52)
    except OSError as e:
        return f"cannot read .o: {e}"
    if len(h) < 52:
        return "truncated object (<52 bytes)"
    if h[:4] != b"\x7fELF":
        return "not an ELF object"
    if h[4:7] != b"\x01\x01\x01":
        return "not ELFCLASS32 little-endian version 1"
    # ELF32 e_type/e_machine/e_version @ offsets 16,18,20.
    e_type, machine, version = struct.unpack_from("<HHI", h, 16)
    if e_type != 1:
        return f"not a relocatable ELF (e_type={e_type})"
    if machine != 8:
        return f"not a MIPS object (e_machine={machine})"
    if version != 1:
        return f"wrong ELF header version ({version})"
    return None


def scan(workspace: str | Path) -> tuple[list[str], dict]:
    root = Path(workspace).resolve()
    issues: list[str] = []
    stats = {"sources": 0, "expected_objects": 0, "actual_objects": 0, "reported_failed": None}
    mirror = root / "build/census"
    source_root = mirror / "src"
    object_root = root / "build/obj"
    failure_list = mirror / "failed_compile.txt"
    summary = mirror / "report_compile.txt"
    for label, folder in (("census source mirror", source_root), ("compiled game objects", object_root)):
        if not folder.is_dir() or folder.is_symlink():
            issues.append(f"MISSING_OR_LINKED_DIRECTORY:{label}")
    if issues:
        return issues, stats
    if not failure_list.is_file() or failure_list.is_symlink():
        issues.append("MISSING_FAILED_COMPILE_REPORT")
    else:
        try:
            entries = failure_list.read_text(encoding="utf-8").strip().splitlines()
            if entries:
                issues.append(f"NONEMPTY_FAILED_COMPILE_REPORT:{len(entries)}")
        except OSError as e:
            issues.append("UNREADABLE_FAILED_COMPILE_REPORT:" + str(e))
    if not summary.is_file() or summary.is_symlink():
        issues.append("MISSING_COMPILE_SUMMARY")
    else:
        try:
            found = {}
            for line in summary.read_text(encoding="utf-8").splitlines():
                m = GROUP_RE.fullmatch(line)
                if m:
                    g, failed, count, errors = m.groups()
                    if g in found:
                        issues.append("DUPLICATE_COMPILE_GROUP:" + g)
                    found[g] = (int(failed), int(count), int(errors))
            if set(found) != set(GROUPS):
                issues.append("MISSING_COMPILE_GROUPS:" + ",".join(sorted(set(GROUPS) - set(found))))
            elif any(failed or errors for failed, count, errors in found.values()):
                issues.append("COMPILE_SUMMARY_FAILURES")
            stats["reported_failed"] = sum(failed for failed, count, errors in found.values())
            stats["reported_total"] = sum(count for failed, count, errors in found.values())
        except (OSError, ValueError) as e:
            issues.append("UNREADABLE_COMPILE_SUMMARY:" + str(e))
    sources = sorted(source_root.rglob("*.c"))
    stats["sources"] = len(sources)
    if not sources:
        issues.append("EMPTY_CENSUS_SOURCE_MIRROR")
    expected: dict[str, Path] = {}
    for source in sources:
        if source.is_symlink():
            issues.append("LINKED_SOURCE:" + source.relative_to(source_root).as_posix())
            continue
        rel = source.relative_to(mirror).as_posix()
        name = normalized_name(rel)
        if name in expected:
            issues.append("COLLIDING_OBJECT_NAME:" + name)
        expected[name] = source
    stats["expected_objects"] = len(expected)
    objects = {p.name: p for p in object_root.glob("*.o")}
    stats["actual_objects"] = len(objects)
    for name in sorted(expected.keys() - objects.keys()):
        issues.append("MISSING_OBJECT:" + name)
    for name in sorted(objects.keys() - expected.keys()):
        issues.append("STALE_EXTRA_OBJECT:" + name)
    for name in sorted(expected.keys() & objects.keys()):
        obj = objects[name]
        source = expected[name]
        if obj.is_symlink():
            issues.append("LINKED_OBJECT:" + name)
            continue
        problem = mips_elf_object(obj)
        if problem:
            issues.append("BAD_OBJECT:" + name + ":" + problem)
        try:
            if obj.stat().st_mtime_ns < source.stat().st_mtime_ns:
                issues.append("OBJECT_PREDATES_MIRROR_SOURCE:" + name)
        except OSError as e:
            issues.append("OBJECT_STAT_FAILURE:" + name + ":" + str(e))
    if stats.get("reported_total") is not None and stats["reported_total"] != len(sources):
        issues.append(f"REPORTED_SOURCE_COUNT_MISMATCH:{stats['reported_total']}!={len(sources)}")
    return issues, stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--workspace", type=Path, required=True)
    args = p.parse_args()
    issues, stats = scan(args.workspace)
    print("QA13 PRELINK", stats)
    for problem in issues[:50]:
        print("NO-GO", problem)
    if len(issues) > 50:
        print("NO-GO", len(issues) - 50, "additional issues (output capped)")
    if issues:
        print("B0 PRELINK NOT VERIFIED: do not call psp_link.py")
        return 2
    print("B0 PRELINK INVENTORY PASS ONLY: requires real PSP linker/ELF provenance + PPSSPP/PSP-Go tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
