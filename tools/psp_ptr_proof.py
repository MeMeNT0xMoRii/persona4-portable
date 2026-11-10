#!/usr/bin/env python
"""Independent evidence for each pointer candidate in retail data, calibrated on the noise groups.

An evidence test only counts if it rarely fires on noise_unaligned and suspect_aligned.

  python tools/psp_ptr_proof.py   # writes build/ptr_proof.txt and build/pointer_samples.txt
"""
import re, struct
from collections import Counter
from psp_data_census import BASE, CODE, DATA, DECOMP, END, END_DATA, NEAR, ROOT, inside, is_string, load
from psp_xref import GP, MEM, find_gp, s16, scan, sources, symbol_addresses

NAMED = re.compile(r"\b(?:D|DAT)_([0-9A-Fa-f]{8})\b")
STEP = 0x1000
DEREF_REACH = 4
VU_NOP = {0x000002FF, 0x8000033C}  # upper and lower NOP of the 64-bit VU instruction
VU_GAP, VU_MIN = 0x400, 0x800  # bytes
# shdWindow.c: 23 ShdWindowEntry {text, behavior} rows; font control bytes defeat is_string().
WINDOW_TEXT = range(0x637190, 0x637248, 8)
# shdDrawEntryRow's Yes/No labels are shorter than the string heuristic's minimum length.
WINDOW_CHOICES = range(0x763790, 0x763798, 4)
# MT_Scene_GetTerrainAtPoint and FldAreaSe index this array of byte-table pointers, including BSS tables.
TERRAIN_TABLE = range(0x5F08B0, 0x5F0A40, 4)
# FldPanel2dSelectEntry indexes 40 pointers to u16 location-name indices, including BSS tables.
PANEL_TABLE = range(0x5F51E0, 0x5F5280, 4)
# btlCutinLoad / btlCutinSetupName select one-byte S/W resource suffixes by day window.
CUTIN_SUFFIXES = (*range(0x763610, 0x763618, 4), *range(0x763620, 0x763628, 4))
# TnGet accessors index this cut-in table of pointers to BSS-backed TnTable objects.
CUTIN_TABLE = range(0x5DC8CC, 0x5DC920, 4)
# fclFuseRecipeLookup: five {u16 *entries, s32 count} rows.
FUSION_RECIPES = range(0x642F00, 0x642F28, 8)
# K_TestMenu_Update passes ten 32-byte KWindowEntryDescriptors to sdkLboxAddDescriptors.
TEST_MENU_NAMES = range(0x5F55B0, 0x5F56F0, 0x20)
# Aligned, evidence-free and not text, yet not pointers: rebasing them corrupts the value.
NOT_POINTERS = {
    0x5BC4D8, 0x5BF388, 0x5BF3B8, 0x5BFC58, 0x5BFE38, 0x5C0BE8, 0x5C55C8,  # file table: size, sector, name
    0x641C64,  # D_00641C40 s16[][19] fusion inheritance weights: row 1 starts with 144
    0x714BF4,  # descending s16 pair table 01cc01ec ... 00200028
    0x72A9E4, 0x72AB40,  # packed s16 keyframe-like records
}


def word(image, va):
    return struct.unpack_from("<I", image, va - BASE)[0]


def vu_regions(image):
    """Embedded VU microcode, found by its NOP words. Only the PS2 reads it: nothing in it is a pointer."""
    hits = [va for a, b in DATA for va in range(a, b, 4) if word(image, va) in VU_NOP]
    runs, start, last = [], hits[0], hits[0]
    for va in hits[1:]:
        if va - last > VU_GAP:
            runs.append((start, last + 4))
            start = va
        last = va
    runs.append((start, last + 4))
    return [(a, b) for a, b in runs if b - a >= VU_MIN]


def collect(image, starts, vu):
    proven, cands = {}, {}
    for a, b in DATA:
        for va in range(a, b, 4):
            v = word(image, va)
            if not v or va in NOT_POINTERS or inside(vu, va) or inside(vu, v):
                continue
            if (va in TERRAIN_TABLE or va in PANEL_TABLE or va in CUTIN_TABLE or va in FUSION_RECIPES) and DATA[0][0] <= v < END:
                proven[va] = v
            elif v in starts:
                proven[va] = v
            elif inside(DATA, v) and (va in WINDOW_TEXT or va in WINDOW_CHOICES or va in CUTIN_SUFFIXES or va in TEST_MENU_NAMES or is_string(image, v)):
                proven[va] = v
            elif inside(DATA, v) or END_DATA <= v < END:
                cands[va] = v
    return proven, cands


def named_targets():
    """Addresses of every D_/DAT_ object the decomp's C and headers mention: real object starts."""
    found = set()
    for tree in ("src", "include"):
        for f in (DECOMP / tree).rglob("*"):
            if f.suffix in (".c", ".h"):
                found.update(int(a, 16) for a in NAMED.findall(f.read_text(encoding="utf-8", errors="replace")))
    return found


def text_like(v):
    """The end of a string read as a word, like TMX and a NUL: addresses in the image never look like this by accident."""
    return all(c == 0 or 0x20 <= c < 0x7F for c in v.to_bytes(4, "little"))


def stepped(image, va, v):
    """A neighbour word holds a nearby address: an array of pointers into one array."""
    for d in (-4, 4):
        n = word(image, va + d)
        if n != v and abs(n - v) <= STEP and not (n - v) & 3:
            return True
    return False


def dereferenced(image, gp):
    """Data words code loads and then uses as a base register: only a pointer is used that way."""
    found = set()
    hi = {}
    for a, b in CODE:
        for va in range(a, b, 4):
            w = word(image, va)
            op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, s16(w & 0xFFFF)
            if op == 15:
                hi[rt] = (w & 0xFFFF) << 16
                continue
            if op == 0x23 and (rs == GP or rs in hi):
                addr = (gp if rs == GP else hi[rs]) + imm
                for n in range(1, DEREF_REACH + 1):
                    x = word(image, va + n * 4)
                    xop = x >> 26
                    if xop in MEM and (x >> 21) & 31 == rt:
                        found.add(addr)
                        break
                    if xop in (2, 3, 4, 5) or (xop == 0 and x & 63 in (8, 9)):
                        break
                    if (xop == 0 and (x >> 11) & 31 == rt) or (xop in (9, 13, 15, 0x23) and (x >> 16) & 31 == rt):
                        break
            if op in (9, 0x23, 13, 0x24, 0x25, 0x20, 0x21) or op == 0:
                hi.pop(((w >> 11) & 31) if op == 0 else rt, None)
    return found


def v_of(cands, va):
    return cands[va]


def main():
    image, _, _ = load()
    names = symbol_addresses()
    stubs, stub_addr, c_funcs = sources()
    starts = {a for a in names.values() if a < DATA[0][0]} | set(c_funcs) | set(stub_addr)
    calls, refs = scan(image, starts)
    vu = vu_regions(image)
    proven, cands = collect(image, starts, vu)
    noise = {va for va, v in cands.items() if v & 3}
    suspect = {va for va in cands.keys() - noise if any(va + d * 4 in noise for d in range(-NEAR, NEAR + 1))}
    group = lambda va: "noise" if va in noise else "suspect" if va in suspect else "aligned"
    pointerish = set(proven) | set(cands)
    named = named_targets()
    derefs = dereferenced(image, find_gp(image))

    tests = {
        "code_formed": lambda va, v: v in refs,
        "next_to_proven": lambda va, v: va - 4 in proven or va + 4 in proven,
        "target_holds_proven": lambda va, v: not v & 3 and any(v + k in proven for k in range(0, 32, 4)),
        "target_is_table": lambda va, v: v in pointerish,
        "target_named": lambda va, v: v in named,
        "stepped": lambda va, v: stepped(image, va, v),
        "dereferenced": lambda va, v: va in derefs,
    }
    hits, totals, any_hit, samples = Counter(), Counter(), Counter(), []
    verdict = {}
    for va, v in cands.items():
        g = group(va)
        totals[g] += 1
        fired = [t for t, f in tests.items() if f(va, v)]
        for t in fired:
            hits[g, t] += 1
        any_hit[g] += bool(fired)
        verdict[va] = fired
        if g == "aligned" and not fired:
            samples.append(f"{va:08x} -> {v:08x}  before {word(image, va - 4):08x} after {word(image, va + 4):08x}")

    # Evidence that never fires on noise is enough next to noise too, as is being the other field of a pointer struct.
    SURE = {"dereferenced", "next_to_proven", "target_holds_proven", "target_is_table", "target_named", "code_formed"}
    rebased = {va for va in cands if (group(va) == "aligned" and (verdict[va] or not text_like(v_of(cands, va)))) or (group(va) == "suspect" and SURE & set(verdict[va]))}
    fields = {va for va, v in cands.items() if group(va) == "suspect" and not text_like(v) and not v & 15 and (va - 8 in rebased or va + 8 in rebased)}
    print("struct fields next to noise:", " ".join(f"{va:x}>{cands[va]:x}" for va in sorted(fields)))
    table = sorted([(va, v, "func" if v in starts else "data" if va in TERRAIN_TABLE or va in PANEL_TABLE or va in CUTIN_TABLE or va in FUSION_RECIPES else "string") for va, v in proven.items()]
                   + [(va, cands[va], "aligned") for va in rebased | fields])
    (ROOT / "build" / "rebase_table.tsv").write_text("".join(f"{va:08x}\t{v:08x}\t{k}\n" for va, v, k in table), encoding="utf-8")
    lines = [f"VU microcode {sum(b - a for a, b in vu) // 1024} KB: " + " ".join(f"{a:x}-{b:x}" for a, b in vu),
             f"proven func/string words {len(proven)}, rebase table {len(table)}", f"{'test':22}" + "".join(f"{g:>16}" for g in ("aligned", "suspect", "noise"))]
    for t in tests:
        lines.append(f"{t:22}" + "".join(f"{hits[g, t]:7}/{totals[g]:<5}{hits[g, t] / max(1, totals[g]):4.0%}" for g in ("aligned", "suspect", "noise")))
    lines.append(f"{'any test':22}" + "".join(f"{any_hit[g]:7}/{totals[g]:<5}{any_hit[g] / max(1, totals[g]):4.0%}" for g in ("aligned", "suspect", "noise")))
    report = "\n".join(lines)
    (ROOT / "build" / "ptr_proof.txt").write_text(report + "\n", encoding="utf-8")
    (ROOT / "build" / "pointer_samples.txt").write_text("\n".join(samples) + "\n", encoding="utf-8")
    print(report)
    print("unexplained aligned:", len(samples))


if __name__ == "__main__":
    main()
