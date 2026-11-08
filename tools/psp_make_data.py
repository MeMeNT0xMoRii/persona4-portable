#!/usr/bin/env python
"""Build the rebased data image from the player's own executable (the result is game data: never ship it).

Retail 0x52da00 moves to PSP_IMAGE. Words in build/rebase_table.tsv move with it, VU microcode is blanked.
Function pointers stay retail until native code exists to point them at.

  python tools/psp_make_data.py [--break N] [outdir]   # --break skips N rebases, to prove the boot check fires
"""
import os, re, struct, subprocess, sys
from pathlib import Path
from psp_data_census import BASE, END_DATA, ROOT, load
from psp_ptr_proof import vu_regions
from psp_xref import symbol_addresses

MARKED = re.compile(r"//\s*FUN_([0-9A-Fa-f]{8})\s*\n(?:\s*(?:/\*.*?\*/|//[^\n]*|#[^\n]*)\s*\n)*[^(;{}]*?\b(\w+)\s*\([^;{}]*\)\s*\{", re.S)
# K is 64 KB aligned so code only moves its lui. ponytail: the program may grow to here, about 4.7 MB of it unused now; lower this when the code is final.
PSP_IMAGE = 0x0900DA00
START = 0x52DA00
K = PSP_IMAGE - START


def source_markers():
    """(address, name) of every // FUN_ marker under build/census/src, in file order. Over the network share each
    open costs ~50 ms (1433 files: 70 s a build), so markers are cached by path, size and mtime in
    build/census/markers_cache.json; the directory walk's own stat data (scandir) costs no extra round trip."""
    import json
    root = ROOT / "build" / "census" / "src"
    cache_path = ROOT / "build" / "census" / "markers_cache.json"
    try:
        cache = json.loads(cache_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        cache = {}
    fresh, changed, out = {}, False, []
    stack = [str(root)]
    files = []
    while stack:
        with os.scandir(stack.pop()) as it:
            for entry in it:
                if entry.is_dir():
                    stack.append(entry.path)
                elif entry.name.endswith(".c"):
                    files.append(entry)
    for entry in sorted(files, key=lambda e: e.path):
        st = entry.stat()
        key = os.path.relpath(entry.path, root)
        stamp = [st.st_size, st.st_mtime_ns]
        hit = cache.get(key)
        if hit and hit[0] == stamp:
            markers = hit[1]
        else:
            text = Path(entry.path).read_text(encoding="utf-8", errors="replace")
            markers = [list(m) for m in MARKED.findall(text)]
            changed = True
        fresh[key] = [stamp, markers]
        out.extend(tuple(m) for m in markers)
    if changed or len(fresh) != len(cache):
        cache_path.write_text(json.dumps(fresh), encoding="utf-8")
    return out


def retail_to_native(linked):
    """linked: name -> anything. -> retail function address -> its value, by symbol table, marker or address in the name."""
    native = {}
    for name, addr in symbol_addresses().items():
        if name in linked:
            native[addr] = linked[name]
    # A // FUN_ marker sits on the definition: it survives the renames symbol_addrs.txt does not know.
    for addr, name in source_markers():
        if name in linked:
            native.setdefault(int(addr, 16), linked[name])
    for name, target in linked.items():
        tail = re.search(r"([0-9A-Fa-f]{8})$", name)
        if tail:
            native.setdefault(int(tail[1], 16), target)
    return native


def native_functions(elf=None, load_base=0):
    """Retail function address -> linked address, read from the ELF's symbols. Empty before the first link."""
    elf = elf or ROOT / "build" / "psp" / "p4.elf"
    if not elf.exists():
        return {}
    nm = Path(os.environ["PSPDEV"]) / "bin" / "psp-nm.exe"
    linked = {}
    for line in subprocess.run([str(nm), str(elf)], capture_output=True, text=True, errors="replace").stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "Tt":
            linked[parts[2]] = int(parts[0], 16) + load_base
    return retail_to_native(linked)


def build(broken=0, elf=None, load_base=0):
    image, _, _ = load()
    data = bytearray(image[START - BASE:END_DATA - BASE])
    for a, b in vu_regions(image):
        data[a - START:b - START] = bytes(b - a)
    strings = []
    native = native_functions(elf, load_base) if elf is not None else native_functions()
    unlinked = 0
    for line in (ROOT / "build" / "rebase_table.tsv").read_text(encoding="utf-8").splitlines():
        va, v, kind = line.split("\t")
        va, v = int(va, 16), int(v, 16)
        assert struct.unpack_from("<I", data, va - START)[0] == v, f"{va:x} is not {v:x}"
        if kind == "func":
            target = native.get(v)
            unlinked += target is None
            if target is not None:
                struct.pack_into("<I", data, va - START, target)
            continue
        if kind == "string":
            strings.append(va - START)
            if len(strings) <= broken:
                continue
        struct.pack_into("<I", data, va - START, v + K)
    cases = ROOT / "build" / "psp" / "reasm_cases.tsv"
    for line in cases.read_text().splitlines() if cases.exists() else ():  # jump tables of reassembled functions
        va, v = line.split("\t")
        struct.pack_into("<I", data, int(va, 16) - START, native[int(v, 16)])
    # PCSX2's Persona 4 widescreen patch: horizontal view window follows the original vertical FOV.
    struct.pack_into("<f", data, 0x76111C - START, 16.0 / 9.0)
    print(f"function pointers with no native target: {unlinked}")
    return bytes(data), strings


def main():
    args = sys.argv[1:]
    broken = int(args[args.index("--break") + 1]) if "--break" in args else 0
    args = [a for i, a in enumerate(args) if a != "--break" and (i == 0 or args[i - 1] != "--break")]
    out = Path(args[0]) if args else ROOT / "build" / "psp"
    out.mkdir(parents=True, exist_ok=True)
    data, strings = build(broken)
    (out / "p4data.bin").write_bytes(data)
    (out / "p4str.bin").write_bytes(struct.pack(f"<{len(strings)}I", *strings))
    print(f"{out}: data {len(data)} bytes, {len(strings)} string pointers to check, {broken} left broken, K={K:#x}")


if __name__ == "__main__":
    main()
