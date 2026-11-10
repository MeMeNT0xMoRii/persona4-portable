#!/usr/bin/env python
"""Turn retail R5900 functions the decomp has no C for into Allegrex assembly, from the player's own executable.

The two CPUs share registers, argument order and most encodings, so most words pass through unchanged.
K is 64 KB aligned: a data address in code only needs its lui moved, $gp points into the image like on PS2.
A function with something that has no PSP meaning (VU0, MMI, 64-bit math, hardware registers) is refused,
stays a logging stub, and is listed in build/psp/reasm_refused.txt.

  python tools/psp_reasm.py   # census only; psp_link.py calls reassemble()
"""
import re, struct, sys
from collections import Counter
from psp_data_census import BASE, CODE, END, END_DATA, ROOT, inside, load
from psp_make_data import K, START
from psp_xref import JUMP_REG_SAFE, MEM, s16, sdk_functions, sources, stub_address, symbol_addresses

REPORT = ROOT / "build" / "psp" / "reasm_refused.txt"
R = ["zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
     "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"]
BRANCH = {4: "beq", 5: "bne", 6: "blez", 7: "bgtz", 20: "beql", 21: "bnel", 22: "blezl", 23: "bgtzl"}
REGIMM = {0: "bltz", 1: "bgez", 2: "bltzl", 3: "bgezl", 16: "bltzal", 17: "bgezal"}
BC1 = {0: "bc1f", 1: "bc1t", 2: "bc1fl", 3: "bc1tl"}
HW = {0x1000, 0x1001, 0x1200, 0x1201, 0x1100, 0x1101, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x100E, 0x100F}
SCRATCH = 0x7000  # EE scratchpad, 16 KB like the PSP's at 0x00010000
CRI = 0x4C0000  # sound and movies to the end of code, see NO_SOUND in psp_link.py
NOT_32_BIT = 0x43C000  # libc and soft float: real 64-bit values
WIDE_SHIFTS = {0x14, 0x16, 0x17, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F}
SAVED = {16, 17, 18, 19, 20, 21, 22, 23, 28, 30, 31}
KHI = K >> 16
assert K & 0xFFFF == 0


class Refuse(Exception):
    pass


def reg(n):
    return "$" + R[n]


def fp_scratch(words):
    """Two caller-saved FPU registers this function never names, for what R5900 keeps in ACC."""
    used = set()
    for w in words:
        op, rs = w >> 26, (w >> 21) & 31
        if op in (0x31, 0x39):
            used.add((w >> 16) & 31)
        elif op == 0x11:
            used |= {(w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31}
    free = [f for f in (19, 18, 17, 16, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15, 14, 13, 12, 0) if f not in used]
    if len(free) >= 2:
        return free[0], free[1]
    if {30, 31} & used:  # the only registers left belong to the caller: translate() saves them
        raise Refuse("no free fp")
    return (free[0] if free else 30), 31


def cop1(w, scratch):
    """-> list of asm lines for a COP1 word."""
    rs, ft, fs, fd, funct = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    if rs != 16:
        return [f".word {w:#010x}"]  # mfc1, mtc1, cfc1, ctc1, W format
    if funct == 0x24:
        return [f"trunc.w.s $f{fd}, $f{fs}"]  # R5900 cvt.w.s truncates, Allegrex rounds
    if funct == 4:
        return [f"sqrt.s $f{fd}, $f{ft}"]  # R5900 reads ft; Allegrex encodes the source in fs
    if funct < 8 or 0x30 <= funct:
        return [f".word {w:#010x}"]
    acc, tmp = scratch
    if funct == 0x16:  # rsqrt.s fd, fs, ft
        return [f"sqrt.s $f{tmp}, $f{ft}", f"div.s $f{fd}, $f{fs}, $f{tmp}"]
    if funct == 0x18:
        return [f"add.s $f{acc}, $f{fs}, $f{ft}"]
    if funct == 0x19:
        return [f"sub.s $f{acc}, $f{fs}, $f{ft}"]
    if funct == 0x1A:
        return [f"mul.s $f{acc}, $f{fs}, $f{ft}"]
    if funct in (0x1C, 0x1D):  # madd.s/msub.s fd, fs, ft: fd = ACC +- fs * ft
        return [f"mul.s $f{tmp}, $f{fs}, $f{ft}", f"{'add' if funct == 0x1C else 'sub'}.s $f{fd}, $f{acc}, $f{tmp}"]
    if funct in (0x1E, 0x1F):  # madda.s/msuba.s
        return [f"mul.s $f{tmp}, $f{fs}, $f{ft}", f"{'add' if funct == 0x1E else 'sub'}.s $f{acc}, $f{acc}, $f{tmp}"]
    if funct in (0x28, 0x29):  # max.s/min.s: keep ft unless fs wins; R5900 never sees NaN here
        keep = f"c.lt.s $f{ft}, $f{fs}" if funct == 0x28 else f"c.lt.s $f{fs}, $f{ft}"
        return [f"mov.s $f{tmp}, $f{ft}", keep, "nop", "bc1f 1f", "nop", f"mov.s $f{tmp}, $f{fs}", "1:", f"mov.s $f{fd}, $f{tmp}"]
    raise Refuse(f"cop1 {funct:#x}")


def plain(w, scratch, extended):
    """-> asm lines for a word that is not a branch or jump."""
    op, rs, rt, rd, sa, funct = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31, w & 63
    imm = s16(w & 0xFFFF)
    if op == 0:
        # dsll32 x, y, 0 then dsra32/dsrl32 x, x, 0 only widens a 32-bit value: nothing to do on a 32-bit CPU
        # with dsll32 x, y, n it keeps the low 32-n bits; the same pair of 32-bit shifts does that here
        if funct == 0x3C:
            extended.add(rd)
            return [f"sll {reg(rd)}, {reg(rt)}, {sa}"]
        if funct in (0x3E, 0x3F) and rd == rt and rd in extended:
            return [f"{'srl' if funct == 0x3E else 'sra'} {reg(rd)}, {reg(rd)}, {sa}"]
        if funct == 0x3F and sa == 0 and rt in extended:  # the high word of a sign-extended value
            return [f"sra {reg(rd)}, {reg(rt)}, 31"]
        if funct == 0x38:  # game values fit 32 bits; libc and soft float are kept out by NOT_32_BIT
            return [f"sll {reg(rd)}, {reg(rt)}, {sa}"]
        if funct == 0x3B:
            return [f"sra {reg(rd)}, {reg(rt)}, {sa}"]
        if funct == 0x14:
            return [f"sllv {reg(rd)}, {reg(rt)}, {reg(rs)}"]
        if funct == 0x2D:
            return [f"addu {reg(rd)}, {reg(rs)}, {reg(rt)}"]
        if funct == 0x2C:
            return [f"addu {reg(rd)}, {reg(rs)}, {reg(rt)}"]
        if funct in (0x2F, 0x2E):
            return [f"subu {reg(rd)}, {reg(rs)}, {reg(rt)}"]
        if funct == 0x18 or funct == 0x19:  # three-operand mult
            name = "mult" if funct == 0x18 else "multu"
            return [f"{name} {reg(rs)}, {reg(rt)}"] + ([f"mflo {reg(rd)}"] if rd else [])
        if funct in (0x14, 0x16, 0x17, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F):
            raise Refuse("64-bit shift")
        if funct in (0x0C, 0x28, 0x29):
            raise Refuse("syscall/mfsa")
        if funct == 0x0F:
            return ["sync"]
        return [f".word {w:#010x}"]
    if op == 0x27:  # lwu: the upper half does not exist here
        return [f"lw {reg(rt)}, {imm}({reg(rs)})"]
    if op == 0x19:
        return [f"addiu {reg(rt)}, {reg(rs)}, {imm}"]
    if op == 0x18:
        return [f"addiu {reg(rt)}, {reg(rs)}, {imm}"]
    if op in (0x37, 0x3F, 0x1E, 0x1F):  # wide memory goes through wide_memory()
        raise Refuse("64-bit memory")
    if op == 0x11:
        return cop1(w, scratch)
    if op == 0x33:
        return ["nop"]  # pref
    if op == 0x1C:
        if funct in (0x12, 0x10):
            return [f".word {(w & 0x03FFFFFF & ~63) | funct:#010x}"]
        if funct in (0x18, 0x19):  # mult1, multu1: the second multiplier, which the PSP does not have
            return [f"{'mult' if funct == 0x18 else 'multu'} {reg(rs)}, {reg(rt)}"] + ([f"mflo {reg(rd)}"] if rd else [])
        if funct in (0x1A, 0x1B):  # div1, divu1
            return [f".word {w & 0x03FFFFFF:#010x}"]
        if funct in (0, 1):  # madd, maddu rd, rs, rt
            return [f"{'madd' if funct == 0 else 'maddu'} {reg(rs)}, {reg(rt)}"] + ([f"mflo {reg(rd)}"] if rd else [])
        raise Refuse("MMI")
    if op in (0x12, 0x36, 0x3E):
        raise Refuse("VU0")
    if op in (0x34, 0x3C, 0x3D):
        raise Refuse("64-bit memory")
    if op == 0x10:
        raise Refuse("COP0")
    if op == 0x2F:
        raise Refuse("cache")
    return [f".word {w:#010x}"]


def shadow(rt, word=1):
    """Where the upper words of a register live between a wide load and the store it feeds: just below $sp."""
    return f"{-(rt + 1) * 16 + (word - 1) * 4}($sp)"


def high_word(rt, wide):
    return f"lw $at, {shadow(rt)}" if rt in wide else f"sra $at, {reg(rt)}, 31"


def stack_slots(words):
    """Offsets of $sp slots that sq fills: an lq from one of them restores a value that fits a register."""
    return {(w >> 16 & 31, s16(w & 0xFFFF)) for w in words if w >> 26 == 0x1F and (w >> 21) & 31 == 29}


def wide_memory(words, i, wide, slots):
    """-> (asm lines, words consumed) for a 64- or 128-bit load or store. Struct copies are most of them:
    EE holds 8 bytes in one register, here the low word stays in it and the high word waits in shadow()."""
    w = words[i]
    op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
    imm = s16(w & 0xFFFF)
    base = reg(rs)
    if op == 0x1F:  # sq
        if rs == 29 and rt not in wide:  # a register save
            return [f"sw {reg(rt)}, {imm}($sp)"], 1
        if rs == 29:
            wide.add(("slot", imm))
        if rt == 0:
            return [f"sw $zero, {imm + i * 4}({base})" for i in range(4)], 1
        lines = [f"sw {reg(rt)}, {imm}({base})"]
        if rt not in wide:
            raise Refuse("128-bit store of a narrow register")
        for i in (1, 2, 3):
            lines += [f"lw $at, {shadow(rt, i)}", f"sw $at, {imm + i * 4}({base})"]
        return lines, 1
    if op == 0x1E:  # lq
        if rs == 29 and (rt, imm) in slots and ("slot", imm) not in wide:
            return [f"lw {reg(rt)}, {imm}($sp)"], 1
        if rs == rt:
            raise Refuse("128-bit memory")
        wide.add(rt)
        lines = []
        for i in (1, 2, 3):
            lines += [f"lw $at, {imm + i * 4}({base})", f"sw $at, {shadow(rt, i)}"]
        return lines + [f"lw {reg(rt)}, {imm}({base})"], 1
    if rs == 29 and rt in SAVED:  # a register save
        return [f"{'lw' if op == 0x37 else 'sw'} {reg(rt)}, {imm}($sp)"], 1
    if op == 0x37:  # ld
        if rs == rt:
            raise Refuse("64-bit memory")
        wide.add(rt)
        return [f"lw $at, {imm + 4}({base})", f"sw $at, {shadow(rt)}", f"lw {reg(rt)}, {imm}({base})"], 1
    if op == 0x3F:  # sd
        if rt == 0:
            return [f"sw $zero, {imm}({base})", f"sw $zero, {imm + 4}({base})"], 1
        return [high_word(rt, wide), f"sw {reg(rt)}, {imm}({base})", f"sw $at, {imm + 4}({base})"], 1
    # ldl+ldr and sdl+sdr: an unaligned 8 bytes, always written as an adjacent pair
    other = words[i + 1] if i + 1 < len(words) else 0
    pair = {0x1A: 0x1B, 0x1B: 0x1A, 0x2C: 0x2D, 0x2D: 0x2C}[op]
    if other >> 26 != pair or (other >> 16) & 31 != rt or (other >> 21) & 31 != rs or rs == rt:
        raise Refuse("lone unaligned 64-bit")
    low = imm if op in (0x1B, 0x2D) else s16(other & 0xFFFF)
    if op in (0x1A, 0x1B):
        wide.add(rt)
        return [f"lwl $at, {low + 7}({base})", f"lwr $at, {low + 4}({base})", f"sw $at, {shadow(rt)}",
                f"lwl {reg(rt)}, {low + 3}({base})", f"lwr {reg(rt)}, {low}({base})"], 2
    return [high_word(rt, wide), f"swl {reg(rt)}, {low + 3}({base})", f"swr {reg(rt)}, {low}({base})",
            f"swl $at, {low + 7}({base})", f"swr $at, {low + 4}({base})"], 2


class Function:
    def __init__(self, start, end, words):
        self.start, self.end, self.words = start, end, words
        self.calls = set()
        self.lui_moved, self.lui_code = {}, {}  # va -> new hi; va -> (lo va, retail address)


def branch_target(va, w):
    """Where a branch or j goes (None for anything else)."""
    op, rs = w >> 26, (w >> 21) & 31
    if op in BRANCH or op == 1 or (op == 17 and rs == 8):
        return va + 4 + (s16(w & 0xFFFF) << 2)
    if op == 2:
        return ((va + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
    return None


def scan_addresses(fn, code_pointer, ok_code):
    """Follow lui through its uses: decide which hi words are data addresses, code addresses or plain numbers."""
    live = {}
    forget_after_slot = None  # a call or return still runs its delay slot
    # A lui in a branch's delay slot whose use is at the branch target: the linear walk never sees that use (b110:
    # sinf's small-argument path in func_00449120 kept the retail hi 0x75 and faulted in a Zio battle effect).
    arriving = {}  # target va -> [(rt, lui va, hi)]
    for i in range(len(fn.words) - 1):
        va, nxt = fn.start + i * 4, fn.words[i + 1]
        target = branch_target(va, fn.words[i])
        if target is not None and nxt >> 26 == 15 and (nxt >> 16) & 31:
            arriving.setdefault(target, []).append(((nxt >> 16) & 31, va + 4, nxt & 0xFFFF))
    extra = {}  # rt -> [(lui va, hi)] reaching here through a branch
    for i, w in enumerate(fn.words):
        va = fn.start + i * 4
        op, rs, rt, rd, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 0xFFFF
        for r, lui_va, hi in arriving.get(va, ()):
            extra.setdefault(r, []).append((lui_va, hi))
        if (op == 9 or op in MEM) and rs in extra:
            for lui_va, hi in extra[rs]:
                if START <= (hi << 16) + s16(imm) < END and hi not in HW:
                    fn.lui_moved[lui_va] = hi + KHI
        if 8 <= op <= 15 or op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x37):  # writes rt
            extra.pop(rt, None)
        elif op == 0:
            extra.pop(rd, None)
        forget, forget_after_slot = forget_after_slot, None
        if op == 15:
            if rt:
                live[rt] = (va, imm)
            if forget is not None:
                live = {r: v for r, v in live.items() if forget and r not in forget}
            continue
        if op == 9 or op in MEM:
            if rs in live:
                lui, hi = live[rs]
                addr = (hi << 16) + s16(imm)
                if op in MEM and hi in HW:  # only a memory access: the same high word is also a float or a flag
                    raise Refuse("hardware register")
                if START <= addr < END:
                    fn.lui_moved[lui] = hi + KHI
                elif op == 9 and inside(CODE, addr) and ok_code(addr):
                    fn.lui_code[lui] = (va, addr)
            if op == 9 or op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x37):
                live.pop(rt, None)
            if forget is not None:
                live = {r: v for r, v in live.items() if forget and r not in forget}
            continue
        if op == 13:
            live.pop(rt, None)
        elif op == 0:
            funct = w & 63
            if funct in JUMP_REG_SAFE and (rs in live) != (rt in live) and rd:
                live[rd] = live[rs if rs in live else rt]
            elif funct == 8 and rs == 31:
                forget_after_slot = ()
            else:
                live.pop(rd, None)
        elif op == 3:
            forget_after_slot = (2, 3)
        if forget is not None:
            live = {r: v for r, v in live.items() if forget and r not in forget}
    for lui, _ in list(fn.lui_code.items()):
        fn.lui_moved.pop(lui, None)


STORES = ("sw ", "sh ", "sb ", "swc1 ", "swl ", "swr ")  # a store's first operand is read, not written


def translate(fn, name_of, cases):
    """-> asm text; branches go through labels, jumps through name_of."""
    words, start = fn.words, fn.start
    scratch = fp_scratch(words) if any(w >> 26 == 0x11 for w in words) else (0, 0)
    targets = set()
    for i, w in enumerate(words):
        op = w >> 26
        if op in BRANCH or op == 1 or (op == 0x11 and (w >> 21) & 31 == 8):
            targets.add(start + (i + 1 + s16(w & 0xFFFF)) * 4)
        elif op == 2 and start <= (w & 0x3FFFFFF) << 2 < fn.end:
            targets.add((w & 0x3FFFFFF) << 2)
    targets = {t for t in targets if start <= t < fn.end}
    jumps = any(w >> 26 == 0 and w & 63 == 8 and (w >> 21) & 31 != 31 for w in words)
    lo_code = {lo: addr for _, (lo, addr) in fn.lui_code.items()}
    out = []
    pending = None  # a branch or jump waiting for its delay slot
    wide, extended, slots, skip = set(), set(), stack_slots(words), 0
    for i, w in enumerate(words):
        va = start + i * 4
        if skip:  # second half of an unaligned pair
            if va in targets:
                raise Refuse("target inside an unaligned pair")
            skip -= 1
            continue
        if va in targets:
            if pending:
                raise Refuse("target in a delay slot")
            out.append(f".L{va:x}:")
        if jumps and va in cases:  # a jump table in data lands here
            out.append(f".globl p4l_{va:08x}\np4l_{va:08x}:")
        op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op in BRANCH or op == 1 or op in (2, 3) or (op == 0x11 and rs == 8) or (op == 0 and w & 63 in (8, 9)):
            if pending:
                raise Refuse("branch in a delay slot")
            if op in (2, 3):
                t = (w & 0x3FFFFFF) << 2
                sym = f".L{t:x}" if op == 2 and start <= t < fn.end else name_of(t)
                pending = (f"{'j' if op == 2 else 'jal'} {sym}", (), False)
                if not (op == 2 and start <= t < fn.end):
                    fn.calls.add(t)
            elif op == 0:
                pending = (f".word {w:#010x}", (rs,), False)
            else:
                t = start + (i + 1 + s16(imm)) * 4
                if start <= t < fn.end:
                    sym = f".L{t:x}"
                elif op == 4 and rs == rt == 0:  # an unconditional tail branch
                    pending = (f"j {name_of(t)}", (), False)
                    fn.calls.add(t)
                    continue
                else:
                    raise Refuse("branch out of the function")
                if op == 0x11:
                    likely = rt >= 2
                    pending = (f"{BC1[rt]} {sym}", (), likely)
                elif op == 1:
                    if rt not in REGIMM:
                        raise Refuse("regimm")
                    pending = (f"{REGIMM[rt]} {reg(rs)}, {sym}", (rs,), rt in (2, 3))
                elif op in (4, 5, 20, 21):
                    pending = (f"{BRANCH[op]} {reg(rs)}, {reg(rt)}, {sym}", (rs, rt), op > 7)
                else:
                    pending = (f"{BRANCH[op]} {reg(rs)}, {sym}", (rs,), op > 7)
            continue
        if op == 15 and va in fn.lui_code:
            lines = [f"lui {reg(rt)}, %hi({name_of(fn.lui_code[va][1])})"]
            fn.calls.add(fn.lui_code[va][1])
        elif op == 15 and va in fn.lui_moved:
            lines = [f"lui {reg(rt)}, {fn.lui_moved[va]:#x}"]
        elif op == 15 and imm == SCRATCH:
            lines = [f"lui {reg(rt)}, 0x1"]
        elif op == 9 and va in lo_code:
            lines = [f"addiu {reg(rt)}, {reg(rs)}, %lo({name_of(lo_code[va])})"]
        elif op in (0x37, 0x3F, 0x1E, 0x1F, 0x1A, 0x1B, 0x2C, 0x2D):
            lines, used = wide_memory(words, i, wide, slots)
            if used > 1:
                if pending:
                    raise Refuse("unaligned pair in a delay slot")
                skip = used - 1
        else:
            lines = plain(w, scratch, extended)
            if op == 0 or 8 <= op <= 15 or 0x20 <= op <= 0x27:
                wide.discard(((w >> 11) & 31) if op == 0 else rt)
        if pending:
            branch, sources_, likely = pending
            pending = None
            real = [l for l in lines if l != "nop"]
            if len(lines) == 1:
                out += [branch, lines[0]]
            else:  # a two-word slot goes before the branch when that cannot change it
                if likely or any(l.split()[1].rstrip(",").lstrip("$") in [R[s] for s in sources_] for l in real if not l.startswith(STORES)):
                    raise Refuse("wide delay slot")
                out += lines + [branch, "nop"]
        else:
            out += lines
            if op == 0x11 and (w & 0x30) == 0x30 and rs == 16 and i + 1 < len(words) and (words[i + 1] >> 21) == 0x228:
                out.append("nop")  # psp-gcc never puts a compare right before its branch
    if pending:
        raise Refuse("ends in a branch")
    if scratch[1] == 31:  # the whole body runs inside a call that keeps $f30 and $f31 for the caller
        body = f".Lbody{start:x}"
        out = ["addiu $sp, $sp, -16", "sw $ra, 0($sp)", "swc1 $f30, 4($sp)", "swc1 $f31, 8($sp)", f"jal {body}", "nop",
               "lwc1 $f30, 4($sp)", "lwc1 $f31, 8($sp)", "lw $ra, 0($sp)", "jr $ra", "addiu $sp, $sp, 16", f"{body}:"] + out
    return out


def extents(starts):
    order = sorted(a for a in starts if inside(CODE, a))
    return {a: (b if any(x <= a < b <= y for x, y in CODE) else next(y for x, y in CODE if x <= a < y))
            for a, b in zip(order, order[1:] + [CODE[-1][1]])}


def trim(image, start, end):
    """Words of a function, without the zero padding before the next one."""
    n = (end - start) // 4
    words = list(struct.unpack_from(f"<{n}I", image, start - BASE))
    while len(words) > 1 and words[-1] == 0 and words[-2] == 0:
        words.pop()
    return words


def falls_through(words):
    """Does the last real word run on into the next function?"""
    for i in range(len(words) - 1, 0, -1):
        if words[i]:
            w = words[i - 1]
            last = words[i]
            return not any(x >> 26 == 2 or (x >> 26 == 0 and x & 63 == 8) for x in (w, last)) and not (
                w >> 26 == 4 and (w >> 16) & 31 == (w >> 21) & 31)
    return True


def case_words(image, starts):
    """Retail code address inside a function -> data words holding it: jump tables, never in the rebase table."""
    cases = {}
    for va in range(START, END_DATA, 4):
        v = struct.unpack_from("<I", image, va - BASE)[0]
        if not v & 3 and inside(CODE, v) and v not in starts:
            cases.setdefault(v, []).append(va)
    return cases


def reassemble(roots, native, image, starts):
    """roots: retail address -> names the link wants. native: retail address -> name already linked.
    -> (assembler text, set of names provided, refused address -> reason, unresolved p4r names)."""
    spans = extents(starts | set(roots))
    cases = case_words(image, starts)
    sdk = sdk_functions()
    done, refused, seen = {}, {}, set()
    todo = list(roots)
    wanted_refused = set()

    def name_of(addr):
        return native.get(addr) or f"p4r_{addr:08x}"

    while todo:
        addr = todo.pop()
        if addr in seen or (addr in native and addr not in roots):
            continue
        seen.add(addr)
        if addr not in spans:
            refused[addr] = "not a known start"
            continue
        if addr in sdk:
            refused[addr] = "SDK " + sdk[addr]
            continue
        if addr >= CRI:
            refused[addr] = "CRI"
            continue
        if addr >= NOT_32_BIT and any(w >> 26 == 0 and w & 63 in WIDE_SHIFTS for w in trim(image, addr, spans[addr])):
            refused[addr] = "64-bit shift"
            continue
        words = trim(image, addr, spans[addr])
        fn = Function(addr, spans[addr], words)
        try:
            scan_addresses(fn, None, lambda a: a in spans or a in native)
            lines = translate(fn, name_of, cases)
            if falls_through(words):
                raise Refuse("falls through")
        except Refuse as e:
            refused[addr] = str(e)
            continue
        done[addr] = lines
        todo += [t for t in fn.calls if t not in seen]

    text = [".set noreorder", ".set nomacro", ".set noat", ".text"]
    provided = set()
    for addr, lines in sorted(done.items()):
        label = f"p4r_{addr:08x}"
        all_names = {label} | set(roots.get(addr, ()))
        provided |= all_names
        text.append(".align 2")
        text += [f".globl {n}\n.type {n}, @function\n{n}:" for n in sorted(all_names)]
        text += lines
    joined = "\n".join(text)
    used = set(re.findall(r"\bp4r_[0-9a-f]{8}\b", joined))
    labels = [(va, v) for v in sorted(int(x, 16) for x in re.findall(r"\.globl p4l_([0-9a-f]{8})", joined)) for va in cases[v]]
    return joined + "\n", provided, refused, used - provided, done, labels


def write_report(refused):
    REPORT.parent.mkdir(exist_ok=True)
    REPORT.write_text("".join(f"{a:08x} {r}\n" for a, r in sorted(refused.items())), encoding="utf-8")


def main():
    image, _, _ = load()
    names = symbol_addresses()
    stubs, stub_addr, c_funcs = sources()
    starts = {a for a in names.values() if inside(CODE, a)} | {a for a in stub_addr if a} | set(c_funcs)
    roots = {}
    for n in (ROOT / "build" / "psp" / "missing.txt").read_text().split():
        a = stub_address(n, names)
        if a and inside(CODE, a):
            roots.setdefault(a, []).append(n)
    native = {a: f"native_{a:x}" for a in c_funcs}
    text, provided, refused, unresolved, done, labels = reassemble(roots, native, image, starts)
    why = Counter(refused.values())
    print(f"roots {len(roots)}, reassembled {len(done)} ({sum(len(l) for l in done.values()) * 4 // 1024} KB), refused {len(refused)}")
    print(why.most_common())
    root_refused = Counter(r for a, r in refused.items() if a in roots)
    print("roots refused", root_refused.most_common())
    print(f"jump table words {len(labels)}")
    write_report(refused)


if __name__ == "__main__":
    main()
