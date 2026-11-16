"""List what each day of the calendar can start: fields, dungeon floors, events and battles of its story script.

Every day and time slot runs one procedure of scheduler/scheduler_MM.bf (cldSchedulerTaskUpdate: "sdlMM_DD_<slot>",
slot AM_A AM_B PM_A PM_B PM_C PM_D). This walks each one, and every procedure it calls or jumps to, through all
branches (conditions are not evaluated), and collects the script commands that change the scene with constant
arguments:
  0x0030 CALL_FIELD (field, room, entry, direction)  0x0031 CALL_FIELD_RESET   0x0032 CALL_EVENT (id, sub, value)
  0x003E CALL_DNG_FLOOR_ONLY (step)                  0x003F CALL_DNG_FLOOR (step, entry)
  0x1016 CALL_BATTLE (encounter, flags)              0x20DC btlScriptStartEncounter (encounter)
Ids are the retail command tables (D_00748530: group << 12 | index). So a day's list says what its script may
start, not what a given save will see: branches on flags, and events that field scripts start on their own, are not
followed.

  python tools/psp_days.py                  # writes build/days.json and build/days.txt (copy days.txt to ms0:/PSP/GAME/P4/)
  python tools/psp_days.py --show 5/3       # one day, all slots

days.txt (read by port/scene_menu.c's Days list): "M D SLOT kind a b c", kind field/floor/event/battle.
Reads build/psp_disc/DATA.CVM (tools/psp_disc.py). The outputs are game data: they stay under build/ (ignored).
"""
import argparse
import json
import struct
from pathlib import Path

from psp_data_census import ROOT
from psp_disc import CVM_BASE, SECTOR, WHOLE, root, walk

SLOTS = ('AM_A', 'AM_B', 'PM_A', 'PM_B', 'PM_C', 'PM_D')
COMMANDS = {0x30: ('field', 4), 0x31: ('field', 4), 0x32: ('event', 3), 0x3E: ('floor', 1), 0x3F: ('floor', 2),
            0x1016: ('battle', 2), 0x20DC: ('battle', 1)}
PUSHI, COMM, JUMP, CALL, PUSHIS = 0, 8, 10, 11, 29
MONTH_DAYS = (30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 28, 31)  # from April


def scheduler_files():
    with open(WHOLE / 'DATA.CVM', 'rb') as f:
        found = [(p, lba, size) for p, lba, size in walk(f, CVM_BASE, *root(f, CVM_BASE))
                 if p.upper().startswith('SCHEDULER/SCHEDULER_') and p.upper().endswith('.BF')]
        for path, lba, size in sorted(found):
            f.seek(CVM_BASE + lba * SECTOR)
            yield int(path[-5:-3]), f.read(size)


def parse(bf):
    """procedures [(name, start)], code as (opcode, operand16, word) per instruction"""
    assert bf[8:12] == b'FLW0', 'not a FlowScript'
    sections = {}
    for i in range(struct.unpack_from('<I', bf, 0x10)[0]):
        kind, size, count, offset = struct.unpack_from('<4I', bf, 0x20 + 16 * i)
        sections[kind] = (size, count, offset)
    size, count, offset = sections[0]
    procedures = [(bf[offset + i * size:offset + i * size + 24].split(b'\0')[0].decode('ascii', 'replace'),
                   struct.unpack_from('<I', bf, offset + i * size + 24)[0]) for i in range(count)]
    size, count, offset = sections[2]
    words = struct.unpack_from(f'<{count}I', bf, offset)
    code = [(w & 0xFFFF, (w >> 16) - 0x10000 if w >> 31 else w >> 16, w) for w in words]
    return procedures, code


def scan(procedures, code, first):
    """the scene commands reachable from procedure index first: [(kind, args)] in code order, deduplicated"""
    starts = sorted({start for _, start in procedures} | {len(code)})
    seen, todo, found = set(), [first], []
    while todo:
        index = todo.pop()
        if index in seen or not 0 <= index < len(procedures):
            continue
        seen.add(index)
        pc = procedures[index][1]
        end = next(s for s in starts if s > pc)
        stack = []
        while pc < end:
            opcode, operand, _ = code[pc]
            if opcode == PUSHI:
                stack.append(code[pc + 1][2] if pc + 1 < len(code) else None)
                pc += 2
                continue
            if opcode == PUSHIS:
                stack.append(operand)
            elif opcode == COMM:
                command = operand & 0xFFFF
                if command in COMMANDS:
                    kind, count = COMMANDS[command]
                    args = [stack[-1 - i] if i < len(stack) else None for i in range(count)]
                    entry = (kind, tuple(args))
                    if None not in args[:2 if kind != 'floor' else 1] and entry not in found:
                        found.append(entry)
                stack.clear()
            elif opcode in (CALL, JUMP):
                todo.append(operand)
                stack.clear()
            else:
                stack.append(None)  # anything else: not a constant we can follow
            pc += 1
    return found


def build():
    days = {}
    for month, bf in scheduler_files():
        procedures, code = parse(bf)
        for index, (name, _) in enumerate(procedures):
            parts = name.split('_')
            if len(parts) != 4 or parts[0] != f'sdl{month:02d}' or f'{parts[2]}_{parts[3]}' not in SLOTS:
                continue
            day, slot = int(parts[1]), SLOTS.index(f'{parts[2]}_{parts[3]}')
            items = scan(procedures, code, index)
            days.setdefault(f'{month}/{day}', {})[SLOTS[slot]] = {
                'procedure': name, 'items': [{'kind': k, 'args': list(a)} for k, a in items]}
    order = lambda key: ((int(key.split('/')[0]) - 4) % 12, int(key.split('/')[1]))
    return {key: days[key] for key in sorted(days, key=order)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--show', help='M/D: print one day')
    args = ap.parse_args()
    out_json, out_txt = ROOT / 'build' / 'days.json', ROOT / 'build' / 'days.txt'
    if args.show and out_json.is_file():
        days = json.loads(out_json.read_text())
    else:
        days = build()
        out_json.write_text(json.dumps(days, indent=1))
        lines = ['# M D SLOT kind a b c (tools/psp_days.py)']
        for key, slots in days.items():
            month, day = key.split('/')
            for slot, info in slots.items():
                for item in info['items']:
                    a = [v if v is not None else 0 for v in item['args']] + [0, 0, 0]
                    if item['kind'] == 'floor' and len(item['args']) == 1:
                        a[1] = 65535
                    lines.append(f"{month} {day} {SLOTS.index(slot)} {item['kind']} {a[0]} {a[1]} {a[2]}")
        out_txt.write_text('\n'.join(lines) + '\n', newline='\n')
        items = sum(len(i['items']) for s in days.values() for i in s.values())
        print(f'{len(days)} days, {sum(len(s) for s in days.values())} slots, {items} scene commands -> {out_json.name}, {out_txt.name}')
    for key in [args.show] if args.show else []:
        for slot, info in days.get(key, {}).items():
            print(f"{key} {slot} {info['procedure']}")
            for item in info['items']:
                print(f"    {item['kind']:6s} {' '.join(str(v) for v in item['args'])}")


if __name__ == '__main__':
    main()
