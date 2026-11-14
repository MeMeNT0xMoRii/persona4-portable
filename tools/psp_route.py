#!/usr/bin/env python
"""Turn a p4.log into a route for P4_ROUTE (port/pad.c): held buttons (PADS) and the left stick (STICK).

  python tools/psp_route.py p4.log tools/routes/name.txt [--until SECONDS] [--cap SECONDS]

--cap shortens every wait with no button held and the stick near the centre to at most SECONDS (a player's pauses);
waits the game needs (loading, long animations) may be longer than that: replay the capped route and check.
"""
import re, sys


def main():
    source, target = sys.argv[1], sys.argv[2]
    until = float(sys.argv[sys.argv.index("--until") + 1]) if "--until" in sys.argv else float("inf")
    cap = float(sys.argv[sys.argv.index("--cap") + 1]) if "--cap" in sys.argv else float("inf")
    lines, shift, last, buttons, stick = [], 0.0, None, 0, (128, 128)  # STICK logs raw 0-255, centre 0x80
    for line in open(source, encoding="utf-8", errors="replace"):
        m = re.search(r"\b(PADS|STICK) (\d+\.\d+) (.*)$", line)
        if not m or float(m.group(2)) > until:
            continue
        t = float(m.group(2))
        idle = buttons == 0 and all(96 <= v < 160 for v in stick)
        if last is not None and idle and t - last > cap:
            shift += t - last - cap
        last = t
        if m.group(1) == "PADS":
            buttons = int(m.group(3).split()[0], 16)
        else:
            stick = tuple(int(v) for v in m.group(3).split()[:2])
        lines.append(("S " if m.group(1) == "PADS" else "A ") + f"{t - shift:.2f} " + m.group(3).strip())
    open(target, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    if shift:
        print(f"waits shortened by {shift:.1f} s")
    print(f"{len(lines)} lines -> {target}")


if __name__ == "__main__":
    main()
