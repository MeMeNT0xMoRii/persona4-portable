"""Send a scene request to the running diagnostic build over PSPLink (port/scene_menu.c: p4_scene_req).

  python tools/psp_scene.py --base 0x08804000 field 6 1          # field 6 room 1 (entry 0)
  python tools/psp_scene.py --base 0x08804000 field 6 1 2        # ... entry 2
  python tools/psp_scene.py --base 0x08804000 floor 12           # dungeon floor step 12 (default entry)
  python tools/psp_scene.py --base 0x08804000 event 100 1        # event E100_001, back to the field after it
  python tools/psp_scene.py --base 0x08804000 battle 532         # encounter 532, back to the field after it
  python tools/psp_scene.py --base 0x08804000 day 5 3 0          # May 3, slot 0 (AM_A): that day's story script
  python tools/psp_scene.py --base 0x08804000 preset "Heaven"    # a line of tools/scenes.txt, by name
  python tools/psp_scene.py --base 0x08804000 menu               # open the L+R+START menu on the PSP
  python tools/psp_scene.py --list                               # the presets
  python tools/psp_scene.py days 5/3                             # what May 3's script starts (tools/psp_days.py)
  python tools/psp_scene.py history                              # the PSP's last 30 requests and results

History: the PSP keeps its last 30 requests (menu and PSPLink) in ms0:/PSP/GAME/P4/scene_history.txt across reboots,
one line each, "<scenes.txt form> | <date> from field F room R | <result>"; `history` copies it to
build/scene_history.txt (PSPLink cp) and prints it, so a later session can see what was tried and how it ended.

The request waits on the PSP (up to 60 s; a loaded save is needed, the title screen is refused) until the player
stands idle in a field or floor, then runs the game's own script
command; the PSP logs "SCENE ..." lines (where it is now, started, arrived, refused and why), printed here as they
arrive. Every field and floor change is logged too: that is how a scene's numbers are found. Needs PSPDEV (psp-nm)
and P4_SESSION like psp_press.py. Copy tools/scenes.txt to ms0:/PSP/GAME/P4/scenes.txt for the menu's presets.
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from psp_data_census import ROOT
from psp_link import BIN

KINDS = {'field': (1, 2, 4), 'floor': (2, 1, 2), 'event': (3, 2, 3), 'battle': (4, 1, 1), 'menu': (5, 0, 0),
         'day': (6, 2, 3)}
SLOTS = ('AM_A', 'AM_B', 'PM_A', 'PM_B', 'PM_C', 'PM_D')
MONTH_DAYS = (30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 28, 31)  # from April: the game's date index 0 is April 1


def date_index(month, day):
    order = month - 4 if month >= 4 else month + 8
    if not 1 <= day <= MONTH_DAYS[order]:
        raise SystemExit(f'{month}/{day} is not a day of the game year')
    return sum(MONTH_DAYS[:order]) + day - 1
SCENES = ROOT / 'tools' / 'scenes.txt'


def presets():
    """(kind, numbers, name) for every line of tools/scenes.txt, as scene_menu.c reads it."""
    counts = {'field': 3, 'floor': 2, 'event': 2, 'battle': 1, 'day': 3}
    out = []
    for line in SCENES.read_text(encoding='utf-8').splitlines():
        words = line.split()
        if not words or words[0].startswith('#') or words[0] not in counts:
            continue
        n = counts[words[0]]
        numbers = [int(w) for w in words[1:1 + n]]
        out.append((words[0], numbers, ' '.join(words[1 + n:]) or ' '.join(words)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('kind', nargs='?', choices=[*KINDS, 'preset', 'days', 'history'])
    ap.add_argument('args', nargs='*')
    ap.add_argument('--base', type=lambda v: int(v, 0), default=0x08804000)
    ap.add_argument('--elf', type=Path, help='explicit matching ELF, including a release build')
    ap.add_argument('--wait', type=float, default=20.0, help='seconds to print SCENE lines after sending')
    ap.add_argument('--list', action='store_true')
    args = ap.parse_args()
    if args.list:
        for kind, numbers, name in presets():
            print(f'{name:40s} {kind} {" ".join(map(str, numbers))}')
        return
    if not args.kind:
        ap.error('a kind is needed')
    kind, values = args.kind, args.args
    if kind == 'days':
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'psp_days.py'), '--show', *values], check=True)
        return
    if kind == 'history':
        session = Path(os.environ.get('P4_SESSION', ROOT / 'build/psplink_session'))
        host = Path(os.environ.get('P4_HOST0', 'C:/temp/p3f-psplink'))
        target = host / 'scene_history.txt'
        target.unlink(missing_ok=True)
        with (session / 'commands.txt').open('a', encoding='utf-8') as f:
            f.write('cp ms0:/PSP/GAME/P4/scene_history.txt host0:/scene_history.txt\n'
                    + ''.join(f'echo DBG_FLUSH_H{i}\n' for i in range(30)))
        end = time.time() + args.wait
        while not target.is_file() and time.time() < end:
            time.sleep(1)
        if not target.is_file():
            raise SystemExit('no history copied back (no request finished yet, or PSPLink did not answer)')
        time.sleep(1)
        text = target.read_text(errors='replace')
        (ROOT / 'build' / 'scene_history.txt').write_text(text)
        print(text, end='')
        return
    if kind == 'preset':
        wanted = ' '.join(values).lower()
        found = [p for p in presets() if p[2].lower() == wanted] or [p for p in presets() if wanted in p[2].lower()]
        if len(found) != 1:
            raise SystemExit(f'{len(found)} presets match {wanted!r}; see --list')
        kind, numbers, name = found[0]
        print(f'preset {name}: {kind} {" ".join(map(str, numbers))}')
    else:
        numbers = [int(v, 0) for v in values]
    code, least, most = KINDS[kind]
    if not least <= len(numbers) <= most:
        raise SystemExit(f'{kind} takes {least}..{most} numbers')
    if kind == 'floor' and len(numbers) == 1:
        numbers.append(0xFFFF)
    if kind == 'day':
        slot = numbers[2] if len(numbers) > 2 else 0
        if not 0 <= slot <= 5:
            raise SystemExit('time slot 0..5: ' + ' '.join(SLOTS))
        print(f'day {numbers[0]}/{numbers[1]} {SLOTS[slot]}: date index {date_index(numbers[0], numbers[1])}')
        numbers = [date_index(numbers[0], numbers[1]), slot]
    numbers += [0] * (4 - len(numbers))

    session = Path(os.environ.get('P4_SESSION', ROOT / 'build/psplink_session'))
    log = session / 'console.log'
    text = log.read_text(errors='replace')
    boots = re.findall(r'hwdbg b(\d+) PRX image=', text)
    if not boots and not args.elf:
        raise SystemExit('No confirmed diagnostic boot in the session log')
    elf = args.elf or ROOT / f'build/psplink_b{boots[-1]}/p4.elf'
    nm = subprocess.check_output([str(BIN / 'psp-nm.exe'), str(elf)], text=True)
    symbols = {p[2]: int(p[0], 16) + args.base for p in (line.split() for line in nm.splitlines()) if len(p) == 3}
    if 'p4_scene_req' not in symbols:
        raise SystemExit(f'{elf} has no p4_scene_req: build b290 or later')
    req = symbols['p4_scene_req']
    lines = [f'pokew 0x{req + 4 * (i + 1):08x} 0x{n & 0xffffffff:08x}' for i, n in enumerate(numbers)]
    lines += [f'pokew 0x{req:08x} 0x{code:08x}', 'dcache w']
    start = len(text)
    with (session / 'commands.txt').open('a', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n' + ''.join(f'echo DBG_FLUSH_{i}\n' for i in range(40)))
    print(f'sent {kind} {" ".join(map(str, numbers))}', flush=True)
    end = time.time() + args.wait
    while time.time() < end:
        time.sleep(1)
        new = log.read_text(errors='replace')[start:]
        for line in new.splitlines():
            if 'SCENE' in line:
                print(line.strip(), flush=True)
        start += len(new)
        if re.search(r'SCENE (field|floor|event|battle|day) .*: (arrived|returned|refused|done|ended|started|the )', new):
            break


if __name__ == '__main__':
    sys.exit(main())
