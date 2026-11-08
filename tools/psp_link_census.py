#!/usr/bin/env python
"""What stops build/obj from linking: duplicate definitions and undefined symbols, by kind.

  python tools/psp_link_census.py   # after psp_census.py --compile; writes build/link_census.txt
"""
import os, re, subprocess
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from psp_data_census import ROOT
from psp_xref import sources, symbol_addresses

NM = Path(os.environ["PSPDEV"]) / "bin" / "psp-nm.exe"
OBJ = ROOT / "build" / "obj"
DATA_NAME = re.compile(r"^(?:D|DAT)_[0-9A-Fa-f]{8}$|^iGp|^_?gp_|^D_")


def symbols(path):
    out = subprocess.run([str(NM), str(path)], capture_output=True, text=True, errors="replace").stdout
    defined, undefined = [], []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] == "U":
            undefined.append(parts[1])
        elif len(parts) == 3 and parts[1] in "TDBRSVWGC":
            defined.append((parts[1], parts[2]))
    return path.stem, defined, undefined


def main():
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        results = list(pool.map(symbols, sorted(OBJ.glob("*.o"))))
    owners, wanted = defaultdict(list), Counter()
    for stem, defined, undefined in results:
        for kind, name in defined:
            owners[name].append((stem, kind))
        wanted.update(undefined)

    names = symbol_addresses()
    _, stub_addr, _ = sources()
    missing = [n for n in wanted if n not in owners]
    kinds, stub_tops = Counter(), Counter()
    for n in missing:
        addr = names.get(n)
        m = re.search(r"([0-9A-Fa-f]{8})$", n)
        addr = addr if addr is not None else int(m[1], 16) if m else None
        if addr in stub_addr:
            kinds["asm stub"] += 1
            stub_tops[stub_addr[addr]] += 1
        elif DATA_NAME.match(n):
            kinds["retail data"] += 1
        elif addr is not None and 0x100000 <= addr < 0x52DA00:
            kinds["code without C"] += 1
        elif addr is not None:
            kinds["retail data (named)"] += 1
        else:
            kinds["no address"] += 1
    strong = {n: o for n, o in owners.items() if len([1 for _, k in o if k in "TDBRSG"]) > 1}
    lines = [f"objects {len(results)}, defined {len(owners)}, undefined {len(missing)}",
             f"undefined by kind {dict(kinds.most_common())}", f"asm stubs by area {dict(stub_tops.most_common())}",
             f"defined more than once {len(strong)}"]
    no_addr = sorted((n for n in missing if names.get(n) is None and not re.search(r"[0-9A-Fa-f]{8}$", n) and not DATA_NAME.match(n)), key=lambda n: -wanted[n])
    lines.append("no address: " + " ".join(no_addr[:60]))
    lines.append("duplicates: " + " ".join(f"{n}({len(o)})" for n, o in sorted(strong.items(), key=lambda x: -len(x[1]))[:40]))
    report = "\n".join(lines)
    (ROOT / "build" / "link_census.txt").write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
