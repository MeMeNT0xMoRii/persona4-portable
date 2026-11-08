#!/usr/bin/env python
"""Turn a p4.log into a route for P4_ROUTE (port/pad.c): held buttons (PADS) and the left stick (STICK).

  python tools/psp_route.py p4.log tools/routes/name.txt [--until SECONDS]
"""
import re, sys


def main():
    source, target = sys.argv[1], sys.argv[2]
    until = float(sys.argv[sys.argv.index("--until") + 1]) if "--until" in sys.argv else float("inf")
    lines = []
    for line in open(source, encoding="utf-8", errors="replace"):
        m = re.search(r"\b(PADS|STICK) (\d+\.\d+) (.*)$", line)
        if not m or float(m.group(2)) > until:
            continue
        lines.append(("S " if m.group(1) == "PADS" else "A ") + m.group(2) + " " + m.group(3).strip())
    open(target, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print(f"{len(lines)} lines -> {target}")


if __name__ == "__main__":
    main()
