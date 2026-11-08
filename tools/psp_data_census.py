#!/usr/bin/env python
"""Classify every word of the retail data sections: what must be rebased for a PSP image?

The PSP cannot map 0x100000, so the image moves by a constant and every stored pointer has to
move with it. The retail ELF is asked for relocations first, then words are classified by value.

  python tools/psp_data_census.py   # writes build/data_census.txt
"""
import re, struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "ext" / "Persona4-Decompilation"
ELF = ROOT / "build" / "SLUS_217.82"
BASE = 0x100000
CODE = [(0x100000, 0x52da00), (0x70c850, 0x70e140)]
DATA = [(0x52da00, 0x70c850), (0x70e140, 0x764280)]
END_DATA, END = 0x764280, 0x938a00
NEAR = 8  # words: an aligned candidate this close to unaligned noise is probably part of the same int table
FUNC = re.compile(r"^\s*(\S+)\s*=\s*0x([0-9A-Fa-f]+);.*type:func", re.M)


def inside(ranges, v):
    return any(a <= v < b for a, b in ranges)


def load():
    elf = ELF.read_bytes()
    assert elf[:4] == b"\x7fELF"
    phoff, shoff = struct.unpack_from("<II", elf, 28)
    phnum, shentsize, shnum = struct.unpack_from("<HHH", elf, 44)
    load = [struct.unpack_from("<8I", elf, phoff + i * 32) for i in range(phnum)]
    types = Counter(struct.unpack_from("<II", elf, shoff + i * shentsize)[1] for i in range(shnum)) if shoff else Counter()
    seg = next(p for p in load if p[0] == 1 and p[2] == BASE)
    return elf[seg[1]:seg[1] + seg[4]], load, types


def is_string(image, v):
    """Target starts a NUL-terminated text after a NUL: 4+ bytes, mostly ASCII or two-byte SJIS (0x80+ only in pairs)."""
    o = v - BASE
    text = lambda c: 32 <= c < 127 or c in (9, 10, 13) or c >= 0x80
    if image[o - 1]:
        return False
    n = high = 0
    while n < 400 and text(image[o + n]):
        high += image[o + n] >= 0x80
        n += 1
    if n < 4 or image[o + n] or high % 2:
        return False
    return high * 2 <= n or image[o] >= 0x81


def asm_stubs():
    stub = re.compile(r'INCLUDE_ASM\("[^"]*",\s*([A-Za-z_0-9]+)\)')
    names = set()
    for p in (DECOMP / "src").rglob("*.c"):
        names.update(stub.findall(p.read_text(encoding="utf-8", errors="replace")))
    return names


def main():
    image, phdrs, sections = load()
    symbols = dict(FUNC.findall((DECOMP / "config" / "symbol_addrs.txt").read_text(encoding="utf-8")))
    funcs = {int(a, 16): n for n, a in symbols.items()}
    stubs = asm_stubs()
    words, cands = Counter(), {}
    for a, b in DATA:
        for va in range(a, b, 4):
            v = struct.unpack_from("<I", image, va - BASE)[0]
            if v == 0:
                words["zero"] += 1
            elif v in funcs:
                words["func_start_asm" if funcs[v] in stubs else "func_start_c"] += 1
            elif inside(CODE, v):
                words["code_inner"] += 1  # mostly retail switch tables: dead once C is recompiled
            elif inside(DATA, v) and is_string(image, v):
                words["ptr_string"] += 1
            elif inside(DATA, v) or END_DATA <= v < END:
                cands[va] = v
            else:
                words["plain"] += 1
    noise = {va for va, v in cands.items() if v & 3}  # no real struct pointer lands on an odd address
    words["noise_unaligned"] = len(noise)
    for va in cands.keys() - noise:
        near = any(va + d * 4 in noise for d in range(-NEAR, NEAR + 1))
        words["suspect_aligned" if near else "ptr_aligned"] += 1
    lines = [f"section header types {dict(sections)} (9=SHT_REL, 4=SHT_RELA; .relmain has size 0)",
             f"program headers {[(hex(p[2]), hex(p[4]), hex(p[5])) for p in phdrs]}",
             f"data+bss size {(END - DATA[0][0]) / 1048576:.2f} MB, code {sum(b - a for a, b in CODE) / 1048576:.2f} MB"]
    lines += [f"{n:8} {k}" for k, n in words.most_common()]
    report = "\n".join(lines)
    (ROOT / "build" / "data_census.txt").write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
