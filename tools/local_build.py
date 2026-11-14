"""Build on a local disk, keep the sources on the network share.

The repository can live on a share. Every build step reads and writes thousands of
small files, which over SMB took 29 minutes for psp_census.py --compile; on a local disk the PSPLink build takes 20 s.
This script mirrors the share's sources into a local copy, builds there, and copies the result back:

  python tools/local_build.py                 # sync port/ and tools/, psp_hw_debug.py, result to build/psplink_bNNN
  python tools/local_build.py --census        # also psp_census.py --compile (after a FIXES change in psp_census.py)
  python tools/local_build.py --ext           # also mirror ext/Persona4-Decompilation (after updating the decomp)
  python tools/local_build.py --init          # first time: also the build inputs (build/psp, build/obj, build/census, ...)

The local root is P4_LOCAL (default ~/p3f-src). Sources are edited on the share only: port/ and tools/ are mirrored
(/MIR), so local edits there are overwritten. PSPDEV must point at the toolchain.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time
from pathlib import Path

SHARE = Path(__file__).resolve().parent.parent


def robocopy(src: Path, dst: Path, *options: str) -> None:
    cmd = ["robocopy", str(src), str(dst), *options, "/MT:32", "/R:1", "/W:1", "/NFL", "/NDL", "/NJH", "/NJS", "/NP"]
    rc = subprocess.run(cmd).returncode
    if rc >= 8:  # robocopy: 0-7 success (1 copied, 2 extra, 4 mismatched), 8+ failure
        sys.exit(f"robocopy {src} -> {dst} failed: {rc}")


def run(script: str, *args: str, cwd: Path) -> None:
    started = time.time()
    rc = subprocess.run([sys.executable, str(cwd / "tools" / script), *args], cwd=cwd).returncode
    print(f"{script} {' '.join(args)}: exit {rc}, {time.time() - started:.0f} s", flush=True)
    if rc:
        sys.exit(rc)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--census", action="store_true", help="run psp_census.py --compile before linking")
    parser.add_argument("--ext", action="store_true", help="mirror ext/Persona4-Decompilation too")
    parser.add_argument("--init", action="store_true", help="first time: copy the build inputs as well")
    parser.add_argument("--release", action="store_true", help="pass --release to psp_hw_debug.py")
    args = parser.parse_args()
    if "PSPDEV" not in os.environ:
        sys.exit("PSPDEV is not set")
    local = Path(os.environ.get("P4_LOCAL", Path.home() / "p3f-src"))
    if local.resolve() == SHARE:
        sys.exit("P4_LOCAL is the share itself")

    robocopy(SHARE / "port", local / "port", "/MIR")
    robocopy(SHARE / "tools", local / "tools", "/MIR", "/XD", "__pycache__")
    if args.ext or args.init:
        robocopy(SHARE / "ext" / "Persona4-Decompilation", local / "ext" / "Persona4-Decompilation", "/MIR", "/XD", ".git", "build")
    if args.init:
        for sub in ("psp", "obj", "census"):
            robocopy(SHARE / "build" / sub, local / "build" / sub, "/MIR")
        robocopy(SHARE / "build", local / "build", "*.tsv", "*.txt", "*.json", "SLUS_217.82")

    # An absent/changed stamp requires regenerated objects: copying new FIXES alone does not
    # rebuild build/obj (b194 accidentally linked the pre-b192 mdlManager.o).
    census_hash = hashlib.sha256()
    for name in ("tools/psp_census.py", "port/compat.h"):
        census_hash.update(name.encode())
        census_hash.update((local / name).read_bytes())
    census_stamp = local / "build/census/local_build_inputs.sha256"
    expected = census_hash.hexdigest()
    previous = census_stamp.read_text().strip() if census_stamp.exists() else ""
    if args.census or previous != expected:
        print("Regenerating game objects: requested or census inputs unverified/changed", flush=True)
        run("psp_census.py", "--compile", cwd=local)
        failed = (local / "build" / "census" / "failed_compile.txt").read_text(encoding="utf-8").strip()
        if failed:
            sys.exit("census compile failures:\n" + failed)
        census_stamp.write_text(expected + "\n")

    build = int(re.search(r"^#define BUILD (\d+)$", (local / "port" / "main.c").read_text(), re.M)[1])
    name = f"psplink_b{build}" + ("_release" if args.release else "")
    run("psp_hw_debug.py", "--out", str(local / "build" / name), *(["--release"] if args.release else []), cwd=local)
    robocopy(local / "build" / name, SHARE / "build" / name, "/MIR")
    print(f"result: {SHARE / 'build' / name}")


if __name__ == "__main__":
    main()
