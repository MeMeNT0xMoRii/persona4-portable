#!/usr/bin/env python
"""Read the retail code directly: which asm stubs does game code reach, which data words does code prove?

Scans R5900 words (no splat, ext/ stays read-only) for jal/j targets and lui+lo address pairs.

  python tools/psp_xref.py   # writes build/xref_report.txt and build/data_pointers.tsv
"""
import json, re, struct
from collections import Counter, defaultdict
from pathlib import Path
from psp_data_census import BASE, CODE, DATA, DECOMP, END, END_DATA, FUNC, NEAR, ROOT, inside, is_string, load

STUB = re.compile(r'INCLUDE_ASM\("[^"]*",\s*([A-Za-z_0-9]+)\)')
MARK = re.compile(r"//\s*FUN_([0-9A-Fa-f]{8})")
THIRD = {"cri", "sce", "middleware", "rw", "renderware", "promoted"}  # promoted: docs/architecture.md, 14 first-party matches
ALU = {9, 13}  # addiu, ori
MEM = {0x14, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E,
       0x31, 0x36, 0x37, 0x39, 0x3E, 0x3F}
JUMP_REG_SAFE = {0x21, 0x2D, 0x25}  # addu, daddu, or: a base register keeps its hi through indexing
GP = 28
SYMBOL = re.compile(r"^\s*(\S+)\s*=\s*0x([0-9A-Fa-f]+)\s*;", re.M)


# Matched by reading the retail body: C and retail copies of one module must not both run, each has its own statics.
READ_FROM_RETAIL = {"RwFreeListCreateAndPreallocateSpace": 0x3E1030, "RwFreeListDestroy": 0x3E1230,
                    "RwResourcesSetArenaSize": 0x3E22C0, "RwTexDictionaryGetCurrent": 0x3EF6D0}


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def symbol_addresses():
    """Every name -> address the decomp knows, type:func entries winning."""
    names = {}
    for f in sorted((DECOMP / "config").glob("symbol*.txt")):
        for n, a in SYMBOL.findall(f.read_text(encoding="utf-8", errors="replace")):
            names.setdefault(n, int(a, 16))
    names.update((n, int(a, 16)) for n, a in FUNC.findall((DECOMP / "config" / "symbol_addrs.txt").read_text(encoding="utf-8")))
    # RenderWare names the decomp recovered by comparing against other games: they tie C like RwEngineStart to func_003e8a50.
    for f in ("known_names.json", "rw_tu_map.json"):
        for a, row in json.loads((DECOMP / "docs" / "sky2" / f).read_text(encoding="utf-8")).items():
            names.setdefault(row["name"], int(a, 16))
    names.update(READ_FROM_RETAIL)
    # func_003dea20 installs this unnamed file-device callback at +0x28; retail has its own stack prologue.
    names["func_003de2f0"] = 0x3DE2F0
    return names


def sdk_functions():
    """Retail address -> name of everything in Sony's libraries: it talks to the EE kernel, IOP and DMA, never reassemble it."""
    text = (DECOMP / "config" / "sdk_symbol_provenance.txt").read_text(encoding="utf-8", errors="replace")
    return {int(a, 16): n for n, a in re.findall(r"^(\w+) = 0x([0-9A-Fa-f]+);", text, re.M)}


def stub_address(name, names):
    m = re.search(r"([0-9A-Fa-f]{8})$", name)
    return int(m[1], 16) if m else names.get(name)


def sources():
    """stub name -> top source dir, retail address -> top source dir for matched C (markers also sit on stubs)."""
    stubs, c_funcs = {}, {}
    for p in (DECOMP / "src").rglob("*.c"):
        rel = p.relative_to(DECOMP / "src").as_posix()
        top = rel.split("/")[0] if "/" in rel else "root"
        text = p.read_text(encoding="utf-8", errors="replace")
        for n in STUB.findall(text):
            stubs[n] = top
        for a in MARK.findall(text):
            c_funcs[int(a, 16)] = top
    names = symbol_addresses()
    stub_addr = {stub_address(n, names): top for n, top in stubs.items()}
    stub_addr.pop(None, None)
    return stubs, stub_addr, {a: top for a, top in c_funcs.items() if a not in stub_addr}


def find_gp(image):
    """crt0 sets $gp at 0x1001c0 with `or gp, a0, zero`; a0 was loaded by lui + addiu/ori just before."""
    assert struct.unpack_from("<I", image, 0x1001c0 - BASE)[0] == 0x80E025
    value = None
    for va in range(0x100100, 0x1001c0, 4):
        w = struct.unpack_from("<I", image, va - BASE)[0]
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 15 and rt == 4:
            value = imm << 16
        elif value is not None and op in ALU and rs == 4 and rt == 4:
            value += s16(imm) if op == 9 else imm
    assert value and inside(DATA, value) or END_DATA <= value < END, f"odd $gp {value}"
    return value


def scan(image, starts):
    """-> (function start -> called/jumped/address-taken functions, every absolute address code forms)."""
    calls, refs = defaultdict(set), set()
    gp = find_gp(image)
    order = sorted(starts)
    for i, start in enumerate(order):
        end = order[i + 1] if i + 1 < len(order) else CODE[-1][1]
        if not inside(CODE, start):
            continue
        hi = {}
        for va in range(start, min(end, start + 0x40000), 4):
            w = struct.unpack_from("<I", image, va - BASE)[0]
            op, rs, rt, rd, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 0xFFFF
            if op in (2, 3):
                t = (w & 0x3FFFFFF) << 2
                if t in starts:
                    calls[start].add(t)
            elif op == 15:
                hi[rt] = imm << 16
            elif op in ALU or op in MEM:
                addr = None
                if rs == GP and rs not in hi:
                    addr = gp + s16(imm)
                elif rs in hi:
                    addr = hi[rs] + (imm if op == 13 else s16(imm))
                if addr is not None:
                    refs.add(addr)
                    if addr in starts:
                        calls[start].add(addr)
                if op in ALU or op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x27, 0x37):
                    hi.pop(rt, None)
            elif op == 0:
                funct = w & 63
                if funct in JUMP_REG_SAFE and (rs in hi or rt in hi):
                    hi[rd] = hi.get(rs, hi.get(rt))
                else:
                    hi.pop(rd, None)
    return calls, refs


def reach(roots, calls, stop):
    seen, todo = set(roots), list(roots)
    while todo:
        for t in calls[todo.pop()]:
            if t not in seen:
                seen.add(t)
                if t not in stop:
                    todo.append(t)
    return seen


def main():
    image, _, _ = load()
    name_at = {a: n for n, a in symbol_addresses().items() if inside(CODE, a)}
    stubs, stub_addr, c_funcs = sources()
    starts = set(name_at) | set(c_funcs) | set(stub_addr)
    calls, refs = scan(image, starts)
    size = {a: b - a for a, b in zip(sorted(starts), sorted(starts)[1:])}

    roots = {a for a, top in c_funcs.items() if top not in THIRD}
    boundary = {a for a, top in stub_addr.items() if top in THIRD} | {a for a, top in c_funcs.items() if top in THIRD}
    near = reach(roots, calls, boundary)
    full = reach(roots, calls, set())
    lines = [f"stubs {len(stubs)}, with an address {len(stub_addr)}, game C roots {len(roots)}, gp {find_gp(image):#x}, code refs {len(refs)}"]
    for label, seen in (("first third-party layer game C touches", near), ("whole closure through third-party", full)):
        hit = [a for a in seen if a in stub_addr]
        kb = Counter()
        for a in hit:
            kb[stub_addr[a]] += size.get(a, 0)
        lines.append(f"{label}: {len(hit)} stubs, {sum(kb.values()) // 1024} KB, " + str({k: (sum(1 for a in hit if stub_addr[a] == k), v // 1024) for k, v in kb.items()}))
    direct = sorted((size.get(a, 0), a) for a in near if a in stub_addr)
    lines.append("largest stubs game C touches: " + " ".join(f"{stub_addr[a]}/{a:08x}:{s}" for s, a in direct[-15:]))
    (ROOT / "build" / "game_asm_stubs.txt").write_text("".join(f"{a:08x} {s:6} {stub_addr[a]}\n" for s, a in sorted(((s, a) for s, a in direct), key=lambda x: x[1])), encoding="utf-8")

    rows, table = Counter(), []
    cands = {}
    for a, b in DATA:
        for va in range(a, b, 4):
            v = struct.unpack_from("<I", image, va - BASE)[0]
            if not v:
                continue
            if v in starts:
                table.append((va, v, "func"))
            elif inside(DATA, v) and is_string(image, v):
                rows["string", v in refs] += 1
                table.append((va, v, "string"))
            elif inside(DATA, v) or END_DATA <= v < END:
                cands[va] = v
    noise = {va for va, v in cands.items() if v & 3}
    for va, v in cands.items():
        kind = "noise_unaligned" if va in noise else "suspect_aligned" if any(va + d * 4 in noise for d in range(-NEAR, NEAR + 1)) else "aligned"
        rows[kind, v in refs] += 1
        if kind != "noise_unaligned":
            table.append((va, v, kind + ("_proven" if v in refs else "_unproven")))
    lines.append("class: target formed by code / not formed")
    lines += [f"{k:18} {rows[k, True]:6} {rows[k, False]:6}  {rows[k, True] / max(1, rows[k, True] + rows[k, False]):.1%}"
              for k in ("string", "aligned", "suspect_aligned", "noise_unaligned")]
    report = "\n".join(lines)
    (ROOT / "build" / "xref_report.txt").write_text(report + "\n", encoding="utf-8")
    (ROOT / "build" / "data_pointers.tsv").write_text("".join(f"{va:08x}\t{v:08x}\t{k}\n" for va, v, k in sorted(table)), encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
