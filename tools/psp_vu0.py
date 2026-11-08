#!/usr/bin/env python
"""Turn the game's VU0 and MMI inline assembly into plain C over the shared state in port/vu0.h.

Registers persist between statements, as the game's code expects: one asm statement often loads what the next uses.
Anything not understood (a stack slot by number, a branch) returns None and stays a P4_EE_ASM() trap.

  python tools/psp_vu0.py   # self-check
"""
import re

VF = re.compile(r"^\$?vf(\d+)([xyzw]*)$")
GPR_NAMES = {"zero": 0, "at": 1, "v0": 2, "v1": 3, "a0": 4, "a1": 5, "a2": 6, "a3": 7, "t0": 8, "t1": 9, "t2": 10, "t3": 11,
             "t4": 12, "t5": 13, "t6": 14, "t7": 15, "t8": 24, "t9": 25, "ra": 31}
ARITH = re.compile(r"^v(add|sub|mul|madd|msub)(a?)([xyzwq]?)\.([xyzw]+)$")
KIND = {"add": 0, "sub": 1, "mul": 2, "madd": 3, "msub": 4}
LANE = "xyzw"
MEM = re.compile(r"^(-?(?:0x[0-9a-fA-F]+|\d+))?\((.+)\)$")
IDENT = re.compile(r"^[A-Za-z_]\w*$")
SIMPLE = {"pextlb", "pextlh", "pextuw", "ppach", "ppacb", "pcpyld", "pcpyud", "or", "and", "daddu"}


class Unknown(Exception):
    pass


def mask(field):
    return sum(1 << LANE.index(c) for c in field)


def split_top(text):
    """Split at top-level colons, outside strings and parentheses."""
    parts, depth, quoted, start, i = [], 0, False, 0, 0
    while i < len(text):
        c = text[i]
        if quoted:
            if c == "\\":
                i += 1
            elif c == '"':
                quoted = False
        elif c == '"':
            quoted = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif c == ":" and depth == 0:
            parts.append(text[start:i])
            start = i + 1
        i += 1
    parts.append(text[start:])
    return parts


def strings(text):
    """The template: adjacent string literals only."""
    rest = re.sub(r'"(?:[^"\\]|\\.)*"', "", text)
    if re.sub(r"\s|\\\n", "", rest):
        raise Unknown("not a literal template")
    return "".join(s.replace("\\n", "\n").replace("\\t", "\t") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', text))


def operands(text):
    """[(constraint, C expression)] of one colon section."""
    out, i = [], 0
    for m in re.finditer(r'"([^"]*)"\s*\(', text):
        if m.start() < i:
            continue
        depth, j = 1, m.end()
        while depth:
            depth += {"(": 1, ")": -1}.get(text[j], 0)
            j += 1
        out.append((m[1], text[m.end():j - 1]))
        i = j
    return out


class Block:
    def __init__(self, ops):
        self.ops = ops

    def gpr(self, token):
        token = token.strip()
        if token.startswith("%"):
            k = int(token[1:])
            if "m" in self.ops[k][0]:
                raise Unknown("memory operand as a register")
            return f"o{k}"
        name = token.lstrip("$")
        n = int(name) if name.isdigit() else GPR_NAMES.get(name)
        if n is None or not token.startswith("$"):
            raise Unknown(token)
        return f"p4vu.r[{n}]"

    def vf(self, token):
        m = VF.match(token.strip())
        if not m:
            raise Unknown(token)
        return int(m[1]), m[2]

    def address(self, token):
        token = token.strip()
        if re.match(r"^%\d+$", token):
            k = int(token[1:])
            if "m" not in self.ops[k][0]:
                raise Unknown("register used as memory")
            return f"m{k}"
        if IDENT.match(token):
            return f"((char *)&{token})"
        m = MEM.match(token)
        if not m:
            raise Unknown(token)
        base = m[2].strip()
        if base in ("$sp", "$29", "$gp", "$28"):
            raise Unknown("fixed stack or small-data slot")
        if re.match(r"^%\d+$", base) and "m" in self.ops[int(base[1:])][0]:
            return f"({self.address(base)} + ({m[1] or 0}))"
        return f"((char *){self.gpr(base)}.w[0] + ({m[1] or 0}))"

    def line(self, line):
        parts = line.split(None, 1)
        op = parts[0]
        args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
        if op in ("nop", "vnop", "vwaitq"):
            return ""
        m = ARITH.match(op)
        if m:
            kind, acc, bc, field = m.groups()
            if len(args) != 3:
                raise Unknown(line)
            s = self.vf(args[1])[0]
            dest = "&p4vu.acc" if acc else f"&p4vu.vf[{self.vf(args[0])[0]}]"
            if not acc and self.vf(args[0])[0] == 0:
                return ""
            if bc == "q":
                return f"p4_vop({KIND[kind]}, {mask(field)}, {dest}, &p4vu.vf[{s}], &p4vu.vf[0], -2);"
            t = self.vf(args[2])[0]
            lane = LANE.index(bc) if bc else -1
            return f"p4_vop({KIND[kind]}, {mask(field)}, {dest}, &p4vu.vf[{s}], &p4vu.vf[{t}], {lane});"
        base = op.split(".")[0]
        field = op.split(".")[1] if "." in op else ""
        if base in ("vmove", "vitof0", "vftoi0", "vabs"):
            d, s = self.vf(args[0])[0], self.vf(args[1])[0]
            fn = {"vmove": "p4_vmove", "vitof0": "p4_vitof", "vftoi0": "p4_vftoi", "vabs": "p4_vabs"}[base]
            return f"{fn}({mask(field)}, &p4vu.vf[{d}], &p4vu.vf[{s}]);" if d else ""
        if base == "vopmula":
            return f"p4_vopmula(&p4vu.vf[{self.vf(args[1])[0]}], &p4vu.vf[{self.vf(args[2])[0]}]);"
        if base == "vopmsub":
            return f"p4_vopmsub(&p4vu.vf[{self.vf(args[0])[0]}], &p4vu.vf[{self.vf(args[1])[0]}], &p4vu.vf[{self.vf(args[2])[0]}]);"
        if base in ("vrsqrt", "vdiv"):
            (s, sl), (t, tl) = self.vf(args[1]), self.vf(args[2])
            return f"p4_{base}(p4vu.vf[{s}].f[{LANE.index(sl)}], p4vu.vf[{t}].f[{LANE.index(tl)}]);"
        if base == "vsqrt":
            t, tl = self.vf(args[1])
            return f"p4_vsqrt(p4vu.vf[{t}].f[{LANE.index(tl)}]);"
        if base == "lqc2":
            n = self.vf(args[0])[0]
            return f"__builtin_memcpy(&p4vu.vf[{n}], {self.address(args[1])}, 16);" if n else ""
        if base == "sqc2":
            return f"__builtin_memcpy({self.address(args[1])}, &p4vu.vf[{self.vf(args[0])[0]}], 16);"
        if base == "qmtc2":
            n = self.vf(args[1])[0]
            return f"p4vu.vf[{n}] = {self.gpr(args[0])};" if n else ""
        if base == "qmfc2":
            return f"{self.gpr(args[0])} = p4vu.vf[{self.vf(args[1])[0]}];"
        if base == "cfc2":
            if args[1] not in ("$vi22", "vi22"):
                raise Unknown(line)
            return f"p4_float_to_reg(&{self.gpr(args[0])}, p4vu.q);"
        if op in SIMPLE:
            d, s, t = (self.gpr(a) for a in args)
            return f"p4_{op}(&{d}, &{s}, &{t});"
        if op == "prot3w":
            return f"p4_prot3w(&{self.gpr(args[0])}, &{self.gpr(args[1])});"
        if op == "lw":
            return f"p4_lw(&{self.gpr(args[0])}, {self.address(args[1])});"
        if op == "lwu":
            return f"p4_lwu(&{self.gpr(args[0])}, {self.address(args[1])});"
        if op == "lq":
            dest = self.gpr(args[0])
            return f"__builtin_memcpy(&{dest}, {self.address(args[1])}, 16);" if dest != "p4vu.r[0]" else ""
        if op == "sw":
            return f"__builtin_memcpy({self.address(args[1])}, &{self.gpr(args[0])}.w[0], 4);"
        if op == "sq":
            return f"__builtin_memcpy({self.address(args[1])}, &{self.gpr(args[0])}, 16);"
        if op == "lui":
            return f"{self.gpr(args[0])}.s[0] = (int)({args[1]}u << 16); {self.gpr(args[0])}.s[1] = {self.gpr(args[0])}.s[0] >> 31;"
        if op in ("addiu", "addi"):
            return f"{self.gpr(args[0])}.s[0] = {self.gpr(args[1])}.s[0] + ({args[2]}); {self.gpr(args[0])}.s[1] = {self.gpr(args[0])}.s[0] >> 31;"
        if op in ("dsll32", "dsrl32") and args[2] == "0":
            fn = "p4_dsll32" if op == "dsll32" else "p4_dsrl32"
            return f"{fn}(&{self.gpr(args[0])}, &{self.gpr(args[1])});"
        if op in ("mfc1", "mtc1"):  # "f" operands hold their bits in the operand copy
            d, s = (args[1], args[0]) if op == "mtc1" else (args[0], args[1])
            if "$f" in d or "$f" in s:
                raise Unknown("named FPU register")
            return f"{self.gpr(d)}.s[0] = {self.gpr(s)}.s[0]; {self.gpr(d)}.s[1] = {self.gpr(d)}.s[0] >> 31;"
        raise Unknown(line)


def incoming(lines):
    """Argument registers a statement reads before it writes them: retail asm relies on them still holding the argument."""
    seen, read = set(), []
    for line in lines:
        parts = line.split(None, 1)
        tokens = re.findall(r"\$(\d+|a[0-3]|t[0-3])\b", parts[1]) if len(parts) > 1 else []
        numbers = [int(t) if t.isdigit() else GPR_NAMES[t] for t in tokens]
        writes_first = parts[0].split(".")[0] not in ("sw", "sq", "qmtc2", "mtc1", "sqc2", "lqc2")
        for i, n in enumerate(numbers):
            if 4 <= n <= 11 and n not in seen:
                seen.add(n)
                if not (i == 0 and writes_first and parts[1].lstrip().startswith("$")):
                    read.append(n)
    return read


def translate(inner, parameters=()):
    """inner: the text between the parentheses of asm(...); parameters: the enclosing function's integer arguments.
    -> one line of C, or None."""
    try:
        parts = split_top(inner)
        template = strings(parts[0])
        outs = operands(parts[1]) if len(parts) > 1 else []
        ins = operands(parts[2]) if len(parts) > 2 else []
        ops = outs + ins
        block = Block(ops)
        body, decls, after = [], [], []
        lines = [raw.strip() for raw in re.split(r"[\n;]", template)]
        lines = [line for line in lines if line and not line.startswith(".")]
        for n in incoming(lines):
            if n - 4 >= len(parameters):
                raise Unknown("argument register without an argument")
            body.append(f"p4vu.r[{n}].w[0] = (unsigned)({parameters[n - 4]});")
        for line in lines:
            if line.endswith(":"):
                raise Unknown("label")
            body.append(block.line(line))
        for k, (constraint, expr) in enumerate(ops):
            if "m" in constraint:
                decls.append(f"char *m{k} = (char *)&({expr});")
                continue
            decls.append(f"P4Reg o{k};")
            if "=" not in constraint:
                decls.insert(0, f"__auto_type t{k} = ({expr});")
                body.insert(0, f"p4_load(&o{k}, &t{k}, sizeof t{k});")
            else:
                body.insert(0, f"__builtin_memset(&o{k}, 0, 16);")
            if "=" in constraint or "+" in constraint:
                after.append(f"__builtin_memcpy(&({expr}), &o{k}, sizeof({expr}));")
        body.insert(0, f"P4_VU0_COUNT({len(lines)});")  # work per call site, logged every 256 frames (rwmath.c)
        return "{ " + " ".join(s for s in decls + body + after if s) + " }"
    except (Unknown, IndexError, ValueError):
        return None


def main():
    c = translate(r'"lqc2 $vf1, 0x10(%0)\n" "vmulax.xyz $ACC, $vf2, $vf1x\n" "sqc2 $vf1, 0(%0)\n" : : "r"(p) : "$vf1"')
    assert c and "p4_vop(2, 7, &p4vu.acc, &p4vu.vf[2], &p4vu.vf[1], 0);" in c, c
    assert "+ (0x10)" in c
    assert translate('"sw $2, 0x64($sp)\\n"') is None
    assert translate('"1:\\n" "nop\\n"') is None
    c = translate(r'"qmfc2.ni %0, $vf10\n" "ppach %0, $zero, %0\n" "sw %0, outer\n" : "=&r"(t), "=m"(outer) : "r"(x)')
    assert c and "p4_ppach(&o0, &p4vu.r[0], &o0);" in c and "(char *)&outer" in c, c
    c = translate('"lqc2 $vf10, 0($5)"', ("effect", "value"))
    assert c and "p4vu.r[5].w[0] = (unsigned)(value);" in c, c
    assert translate('"lqc2 $vf10, 0($5)"') is None
    c = translate('"lw $5, 0(%0)\\n" "qmtc2.ni $5, $vf1\\n" : : "r"(p)')
    assert c and "(unsigned)" not in c.split("t0")[0], c
    print("ok")


if __name__ == "__main__":
    main()
