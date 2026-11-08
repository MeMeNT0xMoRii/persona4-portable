#!/usr/bin/env python
"""Link build/obj into an EBOOT: retail data symbols at their image address, a logging stub for all that is missing.

  python tools/psp_link.py   # after psp_census.py --compile; writes build/psp/ and build/psp_dist/
"""
import os, re, subprocess, sys
from pathlib import Path
from psp_data_census import CODE, END, ROOT, inside
from psp_link_census import OBJ, symbols
from psp_make_data import K, PSP_IMAGE, START, retail_to_native
from psp_reasm import reassemble, write_report
from psp_xref import find_gp, load, sources, stub_address, symbol_addresses

PSPDEV = Path(os.environ["PSPDEV"])
BIN = PSPDEV / "bin"
SDK = PSPDEV / "psp" / "sdk"
OUT = ROOT / "build" / "psp"
DIST = ROOT / "build" / "psp_dist" / "PSP" / "GAME" / "P4"
GP_NAME = re.compile(r"^[a-z]+Gp([0-9a-fA-F]{8})$")
TAIL = re.compile(r"([0-9A-Fa-f]{8})$")
NO_SOUND = "src__cri__"  # ponytail: CRI needs the IOP; every call into it returns 0 until audio has its own mixer
# RenderWare exists three times, twice in C and in retail, each with its own statics. Retail keeps the data consistent.
RETAIL_WORLD = ("src__renderware__", "src__rw__")
PORT = ("main", "platform", "rwsky", "rwdata", "rwmath", "files", "ee", "pad", "memcard", "video", "video_game", "audio", "audio_game")
PORT_ASM = ("video_nal",)
# video_nal.S supplies the first sceMpeg stub. Its remaining stubs must follow
# immediately: psp-fixup-imports describes each library as one contiguous range.
LIB_FLAGS = ["-lpspmpeg", "-lpspsascore", "-lpspgum", "-lpspgu", "-lpspdebug", "-lpspdisplay", "-lpspge", "-lpspctrl", "-lpsppower", "-lpspaudio", "-Wl,-u,sceUtilityGetSystemParamInt", "-lpsputility", "-lm"]


def check_import_ranges(elf):
    from psp_address_audit import elf_sections
    sections = elf_sections(Path(elf).read_bytes())
    ranges = []
    import struct
    for name, _, data in sections:
        if name != '.lib.stub':
            continue
        for offset in range(0, len(data), 20):
            ptr, flags, counts, nids, start = struct.unpack_from('<5I', data, offset)
            count = counts >> 16
            for _, header, content in sections:
                if header[3] <= ptr < header[3] + len(content):
                    module = content[ptr-header[3]:].split(b'\0', 1)[0].decode()
                    break
            else:
                raise ValueError('Invalid PSP import module name')
            ranges.append((start, start + count * 8, module))
    for left, right in zip(sorted(ranges), sorted(ranges)[1:]):
        if left[1] > right[0]:
            raise ValueError(f'Overlapping PSP imports: {left[2]} and {right[2]}; check library order')
    return len(ranges)


def run(*cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, errors="replace")
    if r.returncode:
        raise SystemExit(f"{cmd[0]} failed:\n{r.stdout[-3000:]}{r.stderr[-3000:]}")
    return r.stdout


def library_names():
    found = set()
    gcc = next((PSPDEV / "lib" / "gcc" / "psp").rglob("libgcc.a"))
    for lib in (PSPDEV / "psp" / "lib" / "libc.a", PSPDEV / "psp" / "lib" / "libm.a", gcc):
        out = subprocess.run([str(BIN / "psp-nm.exe"), str(lib)], capture_output=True, text=True, errors="replace").stdout
        found.update(p[2] for p in (l.split() for l in out.splitlines()) if len(p) == 3 and p[1] in "TDBRW")
    return found


def retail_address(name, names, gp):
    m = GP_NAME.match(name)
    if m:
        offset = int(m[1], 16)
        return gp + offset - (1 << 32 if offset >= 1 << 31 else 0)
    if name in names:
        return names[name]
    m = TAIL.search(name)
    return int(m[1], 16) if m else None


def in_image(addr):
    return addr is not None and START <= addr < END


def data_uses(objects, stubs):
    """Stubs no object ever calls: if one is really data the game will read code as numbers."""
    wanted, called = set(stubs), set()
    used = set()
    for path in objects:
        out = subprocess.run([str(BIN / "psp-objdump.exe"), "-r", str(path)], capture_output=True, text=True, errors="replace").stdout
        for line in out.splitlines():
            p = line.split()
            if len(p) == 3 and p[2] in wanted:
                used.add(p[2])
                if p[1] == "R_MIPS_26":
                    called.add(p[2])
    return sorted(used - called)


def main():
    image, _, _ = load()
    gp = find_gp(image)
    names = symbol_addresses()
    objects = [p for p in sorted(OBJ.glob("*.o")) if not p.name.startswith((NO_SOUND, *RETAIL_WORLD))]
    results = [symbols(p) for p in objects]
    defined = {n for _, d, _ in results for _, n in d}
    flags = ["-O2", "-G0", "-D_PSP_FW_VERSION=600", f"-DP4_GP={gp + K:#x}u", f"-DP4_K={K:#x}u", f"-DP4_IMAGE={PSP_IMAGE:#x}u", f"-I{SDK / 'include'}", f"-I{ROOT / 'port'}"]
    OUT.mkdir(parents=True, exist_ok=True)
    port_objects = []
    for source in PORT:
        port_objects.append(OUT / (source + ".o"))
        run(BIN / "psp-gcc.exe", *flags, "-c", ROOT / "port" / (source + ".c"), "-o", port_objects[-1])
    for source in PORT_ASM:
        port_objects.append(OUT / (source + ".o"))
        run(BIN / "psp-gcc.exe", *flags, "-c", ROOT / "port" / (source + ".S"), "-o", port_objects[-1])
    ours = {n for path in port_objects for kind, n in symbols(path)[1] if kind in "TDB"}
    for path, (_, defs, _) in zip(objects, results):  # a port function replaces the game's
        replaced = [n for kind, n in defs if kind in "TW" and n in ours]
        if replaced:
            run(BIN / "psp-objcopy.exe", *[f"--weaken-symbol={n}" for n in replaced], path)
    defined |= ours
    wanted = ({n for _, _, u in results for n in u} | {n for path in port_objects for n in symbols(path)[2] if n.startswith(("p4r_", "func_"))}) - defined
    libs = library_names()

    data, stubs, aliases = {}, [], {}
    for n in sorted(wanted):
        addr = retail_address(n, names, gp)
        if in_image(addr):
            data[n] = addr
        elif n not in libs:
            stubs.append(n)

    # The image is authoritative for data that C also defines: a strong script symbol beats a weak definition.
    for path, (_, defs, _) in zip(objects, results):
        twins = [n for kind, n in defs if kind in "DBRV" and in_image(retail_address(n, names, gp))]
        if twins:
            data.update((n, retail_address(n, names, gp)) for n in twins)
            run(BIN / "psp-objcopy.exe", *[f"--weaken-symbol={n}" for n in twins], path)

    # What has no C but a retail body comes from the executable.
    native = {a: n for a, n in retail_to_native({n: n for n in defined | libs}).items() if inside(CODE, a)}
    roots = {}
    for n in stubs:
        a = stub_address(n, names)
        if a and inside(CODE, a):
            if a in native:  # the same function under a name C does not use
                aliases[n] = native[a]
            else:
                roots.setdefault(a, []).append(n)
    table = [l.split("	") for l in (ROOT / "build" / "rebase_table.tsv").read_text(encoding="utf-8").splitlines()]
    pointed = {int(v, 16) for _, v, kind in table if kind == "func"} - set(native)  # function pointers in data count
    for a in pointed:
        roots.setdefault(a, [])
    _, stub_addr, c_funcs = sources()
    starts = {a for a in names.values() if inside(CODE, a)} | set(stub_addr) | set(c_funcs)
    text, provided, refused, unresolved, done, labels = reassemble(roots, native, image, starts)
    write_report(refused)
    (OUT / "reasm.S").write_text(text, encoding="utf-8")
    (OUT / "reasm_cases.tsv").write_text("".join(f"{va:x}\t{v:x}\n" for va, v in labels), encoding="utf-8")
    run(BIN / "psp-gcc.exe", "-G0", "-c", OUT / "reasm.S", "-o", OUT / "reasm.o")
    stubs = sorted((set(stubs) - provided - set(aliases)) | unresolved | {f"p4r_{a:08x}" for a in pointed - set(done)})
    used_as_data = data_uses(objects, stubs)
    (OUT / "stubs_as_data.txt").write_text("\n".join(used_as_data) + "\n", encoding="utf-8")
    print(f"stubs reached only by address, check they are functions: {len(used_as_data)} (build/psp/stubs_as_data.txt)")
    print(f"reassembled {len(done)} functions ({sum(len(l) for l in done.values()) * 4 // 1024} KB), refused {len(refused)}, aliases {len(aliases)}, jump table words {len(labels)}")

    # The image follows .bss in one segment, padded up to its address: a gap would make a separate load segment.
    (OUT / "image.ld").write_text(f"SECTIONS {{ .p4image (NOLOAD) : {{ . = ALIGN(0x1000000); . += {PSP_IMAGE & 0xFFFFFF:#x}; *(.p4image) }} }} INSERT AFTER .bss;\n", encoding="utf-8")
    (OUT / "symbols.ld").write_text("".join(f"{n} = {a + K:#x};\n" for n, a in sorted(data.items())) + "".join(f"{n} = {t};\n" for n, t in sorted(aliases.items())), encoding="utf-8")
    (OUT / "stubs.c").write_text("int p4_missing(const char *name);\n" + "".join(
        f'int {n}(void) {{ return p4_missing("{n} {refused.get(stub_address(n, names), "")}"); }}\n' for n in stubs), encoding="utf-8")
    (OUT / "missing.txt").write_text("\n".join(stubs) + "\n", encoding="utf-8")

    run(BIN / "psp-gcc.exe", "-O2", "-G0", "-w", "-fno-builtin", "-c", OUT / "stubs.c", "-o", OUT / "stubs.o")
    rsp = OUT / "objects.rsp"
    rsp.write_text("\n".join(p.as_posix() for p in objects) + "\n", encoding="utf-8")
    elf = OUT / "p4.elf"
    # ponytail: two CRI generations define the same names, first object wins; drop one set when audio lands.
    run(BIN / "psp-gcc.exe", "-G0", f"-L{SDK / 'lib'}", "-Wl,-zmax-page-size=128", "-Wl,--allow-multiple-definition", "-Wl,--wrap=sdkSpriteRequestLoad", "-Wl,--wrap=sceGuGetMemory", "-Wl,--wrap=sceGeListUpdateStallAddr", "-Wl,--wrap=func_00397c40", f"-Wl,-T,{(OUT / 'image.ld').as_posix()}",
        *port_objects, OUT / "stubs.o", OUT / "reasm.o", f"@{rsp.as_posix()}", OUT / "symbols.ld", *LIB_FLAGS, "-o", elf)
    run(BIN / "psp-fixup-imports.exe", elf)
    check_import_ranges(elf)
    table = {l.split()[2]: int(l.split()[0], 16) for l in run(BIN / "psp-nm.exe", elf).splitlines() if l.count(" ") == 2}
    start, end = table["p4_image"], table["_end"]
    assert start == PSP_IMAGE, f"program too long: the data image landed at {start:#x}"
    print(run(BIN / "psp-size.exe", elf).strip())
    print(f"data symbols {len(data)}, stubs {len(stubs)}, program end {end:#x}")

    subprocess.run([sys.executable, str(ROOT / "tools" / "psp_make_data.py")], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / "psp_address_audit.py")], check=True)
    run(BIN / "mksfoex.exe", "-d", "MEMSIZE=1", "Persona 4", OUT / "PARAM.SFO")
    run(BIN / "psp-strip.exe", elf, "-o", OUT / "strip.elf")
    run(BIN / "pack-pbp.exe", OUT / "EBOOT.PBP", OUT / "PARAM.SFO", "NULL", "NULL", "NULL", "NULL", "NULL", OUT / "strip.elf", "NULL")
    DIST.mkdir(parents=True, exist_ok=True)
    for name in ("EBOOT.PBP", "p4data.bin", "p4str.bin"):
        (DIST / name).write_bytes((OUT / name).read_bytes())
    print(DIST)


if __name__ == "__main__":
    main()
