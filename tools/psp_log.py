#!/usr/bin/env python
"""Print the end of p4.log with code addresses named, one chain per line.

  python tools/psp_log.py [lines]
"""
import bisect, os, re, subprocess, sys
from psp_data_census import ROOT
from psp_link import BIN

LOG = os.path.join(os.environ["PPSSPP_DIR"], "memstick/PSP/GAME/P4/p4.log")  # PPSSPP folder that holds memstick/


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    nm = subprocess.run([str(BIN / "psp-nm.exe"), "-n", str(ROOT / "build" / "psp" / "p4.elf")], capture_output=True, text=True).stdout
    table = [(int(p[0], 16), p[2]) for p in (l.split() for l in nm.splitlines()) if len(p) == 3 and p[1] in "Tt"]
    name = lambda a: table[bisect.bisect_right(table, (a, "~")) - 1][1]
    lines = open(LOG, errors="replace").read().splitlines()
    out, chain = [], []
    for line in lines:
        m = re.search(r"  from ([0-9a-f]+)$", line)
        if m:
            chain.append(name(int(m[1], 16)))
            continue
        if chain:
            out.append("    < " + " < ".join(chain))
            chain = []
        if line.strip():
            out.append(re.sub(r"from ([0-9a-f]{7})$", lambda x: "from " + name(int(x[1], 16)), line[3:]))
    if chain:
        out.append("    < " + " < ".join(chain))
    print("\n".join(l[:400] for l in out[-count:]))


if __name__ == "__main__":
    main()
