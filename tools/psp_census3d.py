#!/usr/bin/env python
"""3D draw census of the running game in PPSSPP, read from memory (p4.log drops lines under load).

  python tools/psp_census3d.py          # materials drawn in ~1 s: texture, blend, ATEST_1, colour, pixel mask, plugin words
  python tools/psp_census3d.py hide DIR # then hide each material in turn (p4_hide_material) and screenshot it into DIR

Needs the build in build/psp/p4.elf to be the one running (symbol addresses come from it) and PSPDEV set.
ATEST_1 bit 14 is DATE (destination alpha test), bit 15 DATM.
"""
import base64, os, struct, subprocess, sys, time
from pathlib import Path
from psp_debug import Debugger

ROOT = Path(__file__).resolve().parent.parent
ENTRY = 32  # Census3D: material, blend, test, vtype, calls, triangles, modulate, mask


def symbols(*names):
    nm = Path(os.environ["PSPDEV"]) / "bin" / "psp-nm.exe"
    out = subprocess.run([str(nm), "-S", str(ROOT / "build" / "psp" / "p4.elf")], capture_output=True, text=True).stdout
    found = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4 and p[3] in names:
            found[p[3]] = (int(p[0], 16), int(p[1], 16))
    return [found[n] for n in names]


def main():
    (probe, _), (hide, _), (census, size) = symbols("p4_probe_3d", "p4_hide_material", "census3d")
    d = Debugger()
    read = lambda a, n: base64.b64decode(d.ask("memory.read", address=a, size=n)["base64"])
    d.ask("memory.write_u32", address=probe, value=1)
    time.sleep(1.0)  # inside one 256-frame report window
    raw = read(census, size)
    d.ask("memory.write_u32", address=probe, value=0)
    rows = []
    for i in range(size // ENTRY):
        mat, blend, test, vtype, calls, tris, mod, mask = struct.unpack_from("<8I", raw, i * ENTRY)
        if not calls:
            continue
        words = struct.unpack("<10I", read(mat, 40))
        name = read(words[0] + 0x10, 32).split(b"\0")[0].decode(errors="replace") if words[0] else "-"
        rows.append((tris, mat, name, blend, test, words[1], mask, vtype, mod, words[6:10]))
    for r in sorted(rows, reverse=True):
        date = " DATE%d" % (r[4] >> 15 & 1) if r[4] >> 14 & 1 else ""
        print(f"{r[1]:08x} {r[2]:22} tris {r[0]:6} blend {r[3]:02x} test {r[4]:05x}{date} colour {r[5]:08x} mask {r[6]:08x} "
              f"vtype {r[7]:x} mod {r[8]} plugin " + " ".join(f"{w:08x}" for w in r[9]))
    if len(sys.argv) > 2 and sys.argv[1] == "hide":
        out = Path(sys.argv[2])
        out.mkdir(parents=True, exist_ok=True)
        for r in sorted(rows, reverse=True):
            d.ask("memory.write_u32", address=hide, value=r[1])
            time.sleep(0.4)
            subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "shot", str(out / f"{r[1]:08x}_{r[2]}.png")],
                           capture_output=True, timeout=20)
        d.ask("memory.write_u32", address=hide, value=0)


if __name__ == "__main__":
    main()
