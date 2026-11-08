#!/usr/bin/env python
"""Temporary A/B/A probes on the exact b44 PRX currently in PSPLINK.

The one patched instruction resets feedback_captured at present. NOP keeps
the last captured texture, avoiding its copy/padding while retaining draw calls.
Other probes force nearest filtering, skip the 2D tail-call, or map 2D
triangles to points. The filter_draw probe is specific to the captured station
dialogue: its only 2D triangle draw is the fullscreen feedback pass. These are
diagnostic cuts, not rendering fixes. Each word is restored in finally.
"""
import json
import argparse
import re
import time
from psp_data_census import ROOT

SESSION = ROOT / "build" / "psplink_session"
LOG = SESSION / "console.log"
PATTERN = re.compile(r"hwperf n=(\d+) ms=(\d+) cpu_us=(\d+) ge_wait_us=(\d+) present_us=(\d+)")
ADDRESS = 0x08806BD0
ORIGINAL = 0xAC406350


def send(command):
    with (SESSION / "commands.txt").open("a", encoding="utf-8") as stream:
        stream.write(command + "\n")


def rows():
    return [dict(zip(("n", "ms", "cpu_us", "ge_us", "present_us"), map(int, m)))
            for m in PATTERN.findall(LOG.read_text(encoding="utf-8", errors="replace"))]


def collect(label, count=3):
    start = len(rows())
    deadline = time.monotonic() + 65
    while time.monotonic() < deadline:
        found = rows()[start:]
        if len(found) >= count:
            # The first 128-frame window can contain the preceding variant.
            return {"label": label, "rows": found[1:count]}
        time.sleep(0.25)
    raise TimeoutError(label)


def patch(value):
    send(f"pokew 0x{ADDRESS:08x} 0x{value:08x}\ndcache w 0x{ADDRESS:08x} 4\nicache 0x{ADDRESS:08x} 4\npeekw 0x{ADDRESS:08x}")


def main():
    global ADDRESS, ORIGINAL
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("capture", "nearest", "twod", "filter_draw"), default="capture")
    args = parser.parse_args()
    if args.kind == "nearest":
        # sceGuTexFilter: emit the GE command with zero filter payload.
        ADDRESS, ORIGINAL, replacement = 0x08AFE748, 0x00A62825, 0x00C02825
    elif args.kind == "twod":
        # draw.part.0 tail-call -> return, preserving its stack-pop delay slot.
        ADDRESS, ORIGINAL, replacement = 0x08805748, 0x0A2BF56D, 0x03E00008
    elif args.kind == "filter_draw":
        # gu_prim[3]: the captured stationary scene has exactly one 2D triangle
        # draw, its six-vertex fullscreen feedback filter. Points retain every
        # other 2D draw and the screen copy; six corner pixels replace its fill.
        ADDRESS, ORIGINAL, replacement = 0x08B2A800, 3, 0
    else:
        replacement = 0
    text = LOG.read_text(encoding="utf-8", errors="replace")
    boots = re.findall(r"b(\d+) P4 boot free=", text)
    if not boots or int(boots[-1]) != 44:
        raise RuntimeError("These runtime addresses are only valid for the original b44 PRX; refusing another build")
    values = re.findall(rf"0x{ADDRESS:08X}: 0x([0-9A-Fa-f]{{8}})", text)
    if not values or int(values[-1], 16) != ORIGINAL:
        raise RuntimeError("Exact runtime instruction has not been verified; refusing patch")
    result = [{"label": "control_before", "rows": rows()[-3:]}]
    try:
        send(f"echo HWAB_{args.kind.upper()}_TEST")
        patch(replacement)
        result.append(collect(args.kind + "_test"))
        print(json.dumps(result[-1]), flush=True)
    finally:
        send(f"echo HWAB_{args.kind.upper()}_RESTORE")
        patch(ORIGINAL)
    result.append(collect("control_after"))
    (SESSION / f"{args.kind}_ab.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(result[-1]), flush=True)


if __name__ == "__main__":
    main()
