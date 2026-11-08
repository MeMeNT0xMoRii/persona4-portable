#!/usr/bin/env python
"""Link the current compiled game as a PSPLink diagnostic or an XMB release.

PSPDEV must point to the Windows toolchain. Outputs build/psplink_diag;
the normal build, EBOOT and data image are not replaced. --release removes
hardware profiling and emits p4data.bin; tools/psp_release.py packages it.
"""
import os
import re
import struct
import shutil
import tempfile
import argparse
import hashlib
import json
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from psp_data_census import ROOT, load
from psp_link import PORT, PORT_ASM, LIB_FLAGS, run, check_import_ranges
from psp_make_data import K, PSP_IMAGE, build
from psp_xref import find_gp

LOAD_BASE = 0x08804000  # main checks the loader used this base before touching retail data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "psplink_diag")
    parser.add_argument("--release", action="store_true", help="Build an XMB release without hardware profiling")
    args = parser.parse_args()
    sdkroot = Path(os.environ["PSPDEV"])
    binpath, sdk = sdkroot / "bin", sdkroot / "psp" / "sdk"
    base = ROOT / "build" / "psp"
    destination = args.out.resolve()
    build_number = int(re.search(r"^#define BUILD (\d+)$", (ROOT / "port/main.c").read_text(), re.M)[1])
    out = Path(tempfile.mkdtemp(prefix="p3f-hwdbg-"))
    print(f"Staging network objects in {out}", flush=True)
    stage = out / "objects"
    stage.mkdir()
    original_objects = [ROOT / "build" / "obj" / Path(line.strip()).name
                        for line in (base / "objects.rsp").read_text().splitlines() if line.strip()]
    inputs = list(original_objects)
    inputs += [base / name for name in ("stubs.o", "reasm.o", "image.ld", "symbols.ld")]
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda path: shutil.copyfile(path, stage / path.name), inputs))
    print(f"Staged {len(inputs)} inputs; compiling diagnostic entry and renderer", flush=True)
    gp = find_gp(load()[0]) + K
    flags = ["-O2", "-g", "-G0", "-D_PSP_FW_VERSION=600",
             f"-DP4_GP={gp:#x}u", f"-DP4_K={K:#x}u", f"-DP4_IMAGE={PSP_IMAGE:#x}u",
             f"-I{sdk / 'include'}", f"-I{ROOT / 'port'}"]
    if not args.release:
        flags.append("-DP4_HW_PROFILE=1")
    source_hashes = {}
    for name in PORT:
        source_path = ROOT / "port" / f"{name}.c"
        source_hashes[str(source_path.relative_to(ROOT))] = hashlib.sha256(source_path.read_bytes()).hexdigest()
        run(binpath / "psp-gcc.exe", *flags, "-c", ROOT / "port" / f"{name}.c", "-o", out / f"{name}.o")
    for name in PORT_ASM:
        source_path = ROOT / "port" / f"{name}.S"
        source_hashes[str(source_path.relative_to(ROOT))] = hashlib.sha256(source_path.read_bytes()).hexdigest()
        run(binpath / "psp-gcc.exe", *flags, "-c", source_path, "-o", out / f"{name}.o")
    # The retail image and $gp retain their runtime addresses. Relocated symbols
    # use offsets from the expected PRX text base; main refuses another base.
    link_script = (sdk / "lib" / "linkfile.prx").read_text()
    assert link_script.count("  _end = .;") == 1
    link_script = link_script.replace("  _end = .;",
        f"  .p4image {PSP_IMAGE - LOAD_BASE:#x} (NOLOAD) : {{ *(.p4image) }}\n  _end = .;")
    (stage / "linkfile.prx").write_text(link_script)
    symbols = (stage / "symbols.ld").read_text()
    symbols = re.sub(r"= (0x[0-9a-f]+);", lambda m: f"= {int(m[1], 16) - LOAD_BASE:#x};", symbols)
    (stage / "symbols.ld").write_text(symbols)
    objects = [stage / path.name for path in original_objects]
    for path in objects:
        if not path.is_file():
            raise FileNotFoundError(path)
    rsp = out / "objects.rsp"
    rsp.write_text("".join('"' + p.as_posix() + '"\n' for p in objects), encoding="utf-8")
    elf = out / "p4.elf"
    port = [out / f"{name}.o" for name in (*PORT, *PORT_ASM)]
    wraps = ["sdkSpriteRequestLoad", "sceGuGetMemory", "sceGeListUpdateStallAddr", "func_00397c40"]
    if not args.release:
        wraps += ["mdlQueueModelForDraw", "mdlComputeIndexedAttachMatrix", "qsort",
                  "mdlEffectFlushWithTable", "mdlEffectAdvanceAndFlushWithTable",
                  "FldResource_QueueDrawCallbacks", "FldResource_QueueDrawCallbacksPrimary",
                  "FldResource_QueueDrawCallbacksSecondary", "PrimAllocQueueNode", "PrimAppendNode",
                  "func_003d5bc0", "func_003d5e40", "func_003d5e90", "func_003d5840", "func_003e9680"]
    run(binpath / "psp-gcc.exe", "-G0", f"-L{sdk / 'lib'}", "-Wl,-zmax-page-size=128",
        f"-specs={sdk / 'lib' / 'prxspecs'}", f"-Wl,-q,-T{(stage / 'linkfile.prx').as_posix()}",
        "-Wl,--allow-multiple-definition", *[f"-Wl,--wrap={name}" for name in wraps],
        *port, sdk / "lib" / "prxexports.o",
        stage / "stubs.o", stage / "reasm.o", f"@{rsp.as_posix()}", stage / "symbols.ld", *LIB_FLAGS, "-o", elf)
    run(binpath / "psp-fixup-imports.exe", elf)
    print(f"Validated {check_import_ranges(elf)} PSP import libraries", flush=True)
    nm = run(binpath / "psp-nm.exe", elf)
    image = next(int(p[0], 16) for p in (line.split() for line in nm.splitlines())
                 if len(p) == 3 and p[2] == "p4_image")
    assert image + LOAD_BASE == PSP_IMAGE, f"data image moved: {image:#x}"
    run(binpath / "psp-prxgen.exe", elf, out / "p4.prx")
    data, strings = build(elf=elf, load_base=LOAD_BASE)
    data_name = "p4data.bin" if args.release else "p4data.hwdbg.bin"
    (out / data_name).write_bytes(data)
    (out / "p4str.bin").write_bytes(struct.pack(f"<{len(strings)}I", *strings))
    assert (out / "p4str.bin").read_bytes() == (base / "p4str.bin").read_bytes()
    title = f"Persona 4 b{build_number}" if args.release else f"Persona 4 HW Debug b{build_number}"
    run(binpath / "mksfoex.exe", "-d", "MEMSIZE=1", title, out / "PARAM.SFO")
    run(binpath / "pack-pbp.exe", out / "EBOOT.PBP", out / "PARAM.SFO", "NULL", "NULL", "NULL", "NULL", "NULL", out / "p4.prx", "NULL")
    print(run(binpath / "psp-size.exe", elf).strip())
    destination.mkdir(parents=True, exist_ok=True)
    for name in ("p4.elf", "p4.prx", "EBOOT.PBP", data_name, "p4str.bin"):
        shutil.copyfile(out / name, destination / name)
    for path in (ROOT / "port").glob("*.h"):
        source_hashes[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"build": build_number, "release": args.release, "source_sha256": source_hashes, "artifact_sha256": {
        name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
        for name in ("p4.elf", "p4.prx", "EBOOT.PBP", data_name, "p4str.bin")}}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Diagnostic output: {destination}")


if __name__ == "__main__":
    main()
