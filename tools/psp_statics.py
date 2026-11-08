#!/usr/bin/env python
"""Find retail addresses of file statics C declares without one, by comparing marked C functions with their retail bodies.

A marked function's data references in the object and in the retail code must be the same set. Known names cancel out;
when one unknown name and one unknown address are left, they are the same thing. Repeats until nothing changes.

  python tools/psp_statics.py [names...]   # writes build/psp/statics.tsv: name, retail address
"""
import re, subprocess, sys
from collections import defaultdict
from psp_data_census import CODE, DATA, END, ROOT, END_DATA, BASE, inside, load
from psp_link import BIN, retail_address
from psp_link_census import OBJ
from psp_make_data import MARKED, START
from psp_xref import ALU, GP, MEM, find_gp, s16, symbol_addresses
import struct

OUT = ROOT / "build" / "psp" / "statics.tsv"
FN = re.compile(r"^[0-9a-f]{8} <(\w+)>:$")
RELOC = re.compile(r"^\s*[0-9a-f]+:\s+R_MIPS_(?:HI16|LO16)\s+(\S+)$")


def object_refs(path):
    """function name in the object -> data-looking symbols it references."""
    out = subprocess.run([str(BIN / "psp-objdump.exe"), "-dr", str(path)], capture_output=True, text=True, errors="replace").stdout
    refs, current = defaultdict(set), None
    for line in out.splitlines():
        m = FN.match(line)
        if m:
            current = m[1]
            continue
        m = RELOC.match(line)
        if m and current and not m[1].startswith("."):
            refs[current].add(m[1].split("+")[0])
    return refs


def retail_refs(image, start, end, gp):
    found, hi = set(), {}
    for va in range(start, end, 4):
        w = struct.unpack_from("<I", image, va - BASE)[0]
        op, rs, rt, rd, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 0xFFFF
        if op == 15:
            hi[rt] = imm << 16
        elif op in ALU or op in MEM:
            addr = gp + s16(imm) if rs == GP else hi[rs] + s16(imm) if rs in hi else None
            if addr is not None and START <= addr < END:
                found.add(addr)
            if op in ALU or op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37):
                hi.pop(rt, None)
        elif op == 0:
            hi.pop(rd, None)
    return found


def main():
    image, _, _ = load()
    gp = find_gp(image)
    names = symbol_addresses()
    marks = {}
    for path in (ROOT / "build" / "census" / "src").rglob("*.c"):
        for addr, name in MARKED.findall(path.read_text(encoding="utf-8", errors="replace")):
            marks[name] = int(addr, 16)
    starts = sorted(set(a for a in names.values() if inside(CODE, a)) | set(marks.values()))
    following = dict(zip(starts, starts[1:]))
    known = {}
    pairs = []
    for path in sorted(OBJ.glob("*renderware*.o")):
        for fn, refs in object_refs(path).items():
            if fn in marks and marks[fn] in following:
                pairs.append((fn, refs, retail_refs(image, marks[fn], following[marks[fn]], gp)))
    address = lambda n: known.get(n, retail_address(n, names, gp))
    for _ in range(8):
        changed = False
        for fn, refs, retail in pairs:
            unknown = [n for n in refs if not (address(n) and START <= address(n) < END) and not (address(n) and inside(CODE, address(n)))]
            left = retail - {address(n) for n in refs}
            if len(unknown) == 1 and len(left) == 1:
                known[unknown[0]] = left.pop()
                changed = True
        if not changed:
            break
    OUT.write_text("".join(f"{n}\t{a:x}\n" for n, a in sorted(known.items())), encoding="utf-8")
    wanted = sys.argv[1:] or sorted(known)
    print(f"{len(pairs)} marked functions compared, {len(known)} statics found")
    print(" ".join(f"{n}={known.get(n, 0):x}" for n in wanted))


if __name__ == "__main__":
    main()
