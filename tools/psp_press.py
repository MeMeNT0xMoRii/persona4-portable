"""Press PSP buttons in the running diagnostic build over PSPLink (port/pad.c: p4_pad_press, p4_pad_reads).

  python tools/psp_press.py --base 0x08804000 start cross:1.5 down down cross
  python tools/psp_press.py --base 0x08804000 --route tools/routes/heaven_save3.txt

Each step is NAME[:WAIT]: press NAME for --reads pad reads, then wait WAIT seconds (default --gap) before the next.
A route file ("seconds hex" lines, tools/psp_route.py) is replayed with its own spacing, from its first press.
Needs PSPDEV (psp-nm) and P4_SESSION like psp_probe_ab.py. Never resets or starts the game.
"""
import argparse
import os
import re
import subprocess
import time
from pathlib import Path

from psp_data_census import ROOT
from psp_link import BIN

BUTTONS = {'select': 0x0001, 'start': 0x0008, 'up': 0x0010, 'right': 0x0020, 'down': 0x0040, 'left': 0x0080,
           'ltrigger': 0x0100, 'rtrigger': 0x0200, 'triangle': 0x1000, 'circle': 0x2000, 'cross': 0x4000,
           'square': 0x8000}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('steps', nargs='*')
    ap.add_argument('--base', required=True, type=lambda v: int(v, 0))
    ap.add_argument('--route', type=Path)
    ap.add_argument('--gap', type=float, default=1.0)
    ap.add_argument('--reads', type=int, default=3)
    ap.add_argument('--elf', type=Path, help='explicit matching ELF, including a release build')
    args = ap.parse_args()
    session = Path(os.environ.get('P4_SESSION', ROOT / 'build/psplink_session'))
    text = (session / 'console.log').read_text(errors='replace')
    boots = re.findall(r'hwdbg b(\d+) PRX image=', text)
    if not boots and not args.elf:
        raise SystemExit('No confirmed diagnostic boot in the session log')
    elf = args.elf or ROOT / f'build/psplink_b{boots[-1]}/p4.elf'
    nm = subprocess.check_output([str(BIN / 'psp-nm.exe'), str(elf)], text=True)
    symbols = {p[2]: int(p[0], 16) + args.base for p in (line.split() for line in nm.splitlines()) if len(p) == 3}
    press, reads = symbols['p4_pad_press'], symbols['p4_pad_reads']

    plan = []  # (buttons, wait after)
    if args.route:
        lines = [l.split() for l in args.route.read_text().splitlines() if l.strip()]
        events = [(float(l[0]), int(l[1], 16)) for l in lines if len(l) == 2]
        for i, (at, buttons) in enumerate(events):
            plan.append((buttons, events[i + 1][0] - at if i + 1 < len(events) else 0))
    for step in args.steps:
        name, _, wait = step.partition(':')
        plan.append((BUTTONS[name.lower()], float(wait) if wait else args.gap))

    queue = session / 'commands.txt'
    for buttons, wait in plan:
        with queue.open('a', encoding='utf-8') as f:
            f.write(f'pokew 0x{press:08x} 0x{buttons:08x}\npokew 0x{reads:08x} 0x{args.reads:08x}\ndcache w\n')
        print(f'press {buttons:04x}, then {wait:.1f} s', flush=True)
        time.sleep(max(wait, 0.3))


if __name__ == '__main__':
    main()
