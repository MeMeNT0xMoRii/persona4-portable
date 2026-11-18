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
from psp_link import PORT, PORT_ASM, LIB_FLAGS, run, check_import_ranges, correct_retail_data_symbols
from psp_make_data import K, PSP_IMAGE, build
from psp_xref import find_gp
from psp_xmb import ARTWORK, TITLE, pack_eboot

LOAD_BASE = 0x08804000  # main checks the loader used this base before touching retail data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "psplink_diag")
    parser.add_argument("--object-dir", type=Path, default=ROOT / "build" / "obj",
                        help="Prepared game objects matching the linker census in build/psp")
    parser.add_argument("--release", action="store_true", help="Build an XMB release without hardware profiling")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--frame-timing", action="store_true", help="Build a light XMB pipeline profiler with p4-timing.log")
    modes.add_argument("--ge-dump", action="store_true", help="Build a serial GE-frame capture diagnostic for PPSSPP replay")
    args = parser.parse_args()
    if args.frame_timing and not args.release:
        parser.error("--frame-timing needs --release for the XMB runtime layout")
    sdkroot = Path(os.environ["PSPDEV"])
    binpath, sdk = sdkroot / "bin", sdkroot / "psp" / "sdk"
    base = ROOT / "build" / "psp"
    destination = args.out.resolve()
    entry_source = (ROOT / "port/main.c").read_text()
    build_number = int(re.search(r"^#define BUILD (\d+)$", entry_source, re.M)[1])
    build_label = re.search(r'^#define P4_BUILD_ID "([^"]+)"$', entry_source, re.M)
    build_id = build_label[1] if build_label else None
    out = Path(tempfile.mkdtemp(prefix="p3f-hwdbg-"))
    print(f"Staging network objects in {out}", flush=True)
    stage = out / "objects"
    stage.mkdir()
    original_objects = [args.object_dir.resolve() / Path(line.strip().strip('"')).name
                        for line in (base / "objects.rsp").read_text().splitlines() if line.strip()]
    inputs = list(original_objects)
    inputs += [base / name for name in ("stubs.o", "reasm.o", "image.ld", "symbols.ld")]
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda path: shutil.copyfile(path, stage / path.name), inputs))
    print(f"Staged {len(inputs)} inputs; compiling diagnostic entry and renderer", flush=True)
    gp = find_gp(load()[0]) + K
    flags = [os.environ.get("P4_PORT_OPT", "-O2"), "-g", "-G0", "-D_PSP_FW_VERSION=600",  # P4_PORT_OPT=-Os: b241 instruction-cache experiment
             f"-DP4_GP={gp:#x}u", f"-DP4_K={K:#x}u", f"-DP4_IMAGE={PSP_IMAGE:#x}u",
             f"-I{sdk / 'include'}", f"-I{ROOT / 'port'}"]
    if not args.release:
        flags.append("-DP4_HW_PROFILE=1")
    else:
        flags.append("-DP4_VERIFY_RUNTIME=1")
    if args.ge_dump:
        flags.append("-DP4_GE_DUMP=1")
    if args.frame_timing:
        flags.append("-DP4_FRAME_TIMING=1")
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
    symbols = correct_retail_data_symbols((stage / "symbols.ld").read_text())
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
    wraps = ["sdkSpriteRequestLoad", "sceGuGetMemory", "sceGeListUpdateStallAddr", "func_00397c40",
             "malloc", "calloc", "realloc", "memalign", "free"]  # b166 memory census (main.c), as psp_link
    if not args.release:
        wraps += ["mdlQueueModelForDraw", "mdlComputeIndexedAttachMatrix", "qsort",
                  "mdlEffectFlushWithTable", "mdlEffectAdvanceAndFlushWithTable",
                  "FldResource_QueueDrawCallbacks", "FldResource_QueueDrawCallbacksPrimary",
                  "FldResource_QueueDrawCallbacksSecondary", "PrimAllocQueueNode", "PrimAppendNode",
                  "func_003d5bc0", "func_003d5e40", "func_003d5e90", "func_003d5840", "func_003e9680"]
    digest_source = out / "runtime-digests.S"
    digest_object = out / "runtime-digests.o"

    def assemble_digests(payload):
        digest_source.write_text(".section .rodata\n.balign 4\n.global p4_runtime_digests\n"
                                 "p4_runtime_digests:\n.byte " + ",".join(map(str, payload)) + "\n")
        run(binpath / "psp-gcc.exe", "-G0", "-c", digest_source, "-o", digest_object)

    if args.release:
        assemble_digests(bytes(40))
        port.append(digest_object)
    link = [binpath / "psp-gcc.exe", "-G0", f"-L{sdk / 'lib'}", "-Wl,-zmax-page-size=128",
        f"-specs={sdk / 'lib' / 'prxspecs'}", f"-Wl,-q,-T{(stage / 'linkfile.prx').as_posix()}",
        "-Wl,--allow-multiple-definition", *[f"-Wl,--wrap={name}" for name in wraps],
        *port, sdk / "lib" / "prxexports.o",
        stage / "stubs.o", stage / "reasm.o", f"@{rsp.as_posix()}", stage / "symbols.ld", *LIB_FLAGS, "-o", elf]
    run(*link)
    if args.release:
        # Retail data contains addresses from this link. Reserve the digest space
        # first, then fill it and verify that the final link produces identical data.
        first_data, first_offsets = build(elf=elf, load_base=LOAD_BASE)
        first_strings = struct.pack(f"<{len(first_offsets)}I", *first_offsets)
        assemble_digests(hashlib.sha1(first_data).digest() + hashlib.sha1(first_strings).digest())
        run(*link)
    run(binpath / "psp-fixup-imports.exe", elf)
    print(f"Validated {check_import_ranges(elf)} PSP import libraries", flush=True)
    nm = run(binpath / "psp-nm.exe", elf)
    image = next(int(p[0], 16) for p in (line.split() for line in nm.splitlines())
                 if len(p) == 3 and p[2] == "p4_image")
    assert image + LOAD_BASE == PSP_IMAGE, f"data image moved: {image:#x}"
    run(binpath / "psp-prxgen.exe", elf, out / "p4.prx")
    data, strings = build(elf=elf, load_base=LOAD_BASE)
    if args.release and (data != first_data or list(strings) != list(first_offsets)):
        raise RuntimeError("Embedding the runtime digests changed the relocated data")
    data_name = "p4data.bin" if args.release else "p4data.hwdbg.bin"
    (out / data_name).write_bytes(data)
    (out / "p4str.bin").write_bytes(struct.pack(f"<{len(strings)}I", *strings))
    assert (out / "p4str.bin").read_bytes() == (base / "p4str.bin").read_bytes()
    title = (build_id or TITLE) if args.release else f"{TITLE} HW Debug b{build_number}"
    if args.ge_dump:
        title = f"{TITLE} GE Dump"
    if args.frame_timing:
        title = f"{title} Timing"
    pack_eboot(ROOT, out, binpath, run, out / "p4.prx", title=title)
    print(run(binpath / "psp-size.exe", elf).strip())
    destination.mkdir(parents=True, exist_ok=True)
    for name in ("p4.elf", "p4.prx", "EBOOT.PBP", data_name, "p4str.bin"):
        shutil.copyfile(out / name, destination / name)
    for path in (ROOT / "port").glob("*.h"):
        source_hashes[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in (*ARTWORK, "tools/psp_xmb.py"):
        source_hashes[name] = hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
    manifest = {"build": build_number, "build_id": build_id,
                "release": args.release and not (args.ge_dump or args.frame_timing), "frame_timing": args.frame_timing, "ge_dump": args.ge_dump,
                "source_sha256": source_hashes,
                "object_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in original_objects}, "artifact_sha256": {
        name: hashlib.sha256((destination / name).read_bytes()).hexdigest()
        for name in ("p4.elf", "p4.prx", "EBOOT.PBP", data_name, "p4str.bin")}}
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Diagnostic output: {destination}")


if __name__ == "__main__":
    main()
