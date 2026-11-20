#!/usr/bin/env python3
"""QA10 read-only owner candidate gate for PSP ELF nm/map output.

These inputs MUST come from the *same final linked* RESONANCE01 ELF and its
exact original and port object directory. Use only in separate experiment tree.

python qa10_elf_symbol_owner_gate.py --elf-nm final.nm --objects-nm owners.nm \
  --linker-map final.map --expect 'func_003f79b0=rwsky.o'

Object symbol association and a matching linker map are necessary evidence,
NOT a substitute for actual MIPS disassembly, provenance or device execution.
The frozen builder does NOT produce an ld -Map artifact by default. Missing map
returns INCONCLUSIVE (exit 3); use actual Codex-built linker map later.
"""
import argparse
import hashlib
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path

# MIPS psp-nm -a normal flat line: 04xxxxxx T func_name
FLAT = re.compile(r'^\s*([0-9a-fA-F]{4,16})\s+([A-Za-z])\s+(\S+)\s*$')
# psp-nm -A path; Windows drives (C:) handled by greedy prefix.
OWNER = re.compile(r'^\s*(.+):([0-9a-fA-F]{4,16})\s+([A-Za-z])\s+(\S+)\s*$')
MAP_SECTION = re.compile(r'^\s*(\.text(?:\.[\w.$-]+)?)\s+(0x[\da-fA-F]+)\s+(0x[\da-fA-F]+)\s+(.+?\.o)(?:\s.*)?$')
MAP_SYMBOL = re.compile(r'^\s*(0x[\da-fA-F]+)\s+(\S+)\s*$')
STRONG = set('TDBRSG')
WEAK = set('WwVv')


@dataclass(frozen=True)
class Definition:
    name: str
    addr: int
    kind: str
    owner: str


def parse_elf(text):
    sym = {}
    for line in text.splitlines():
        m = FLAT.match(line)
        if m:
            addr, typ, name = m.groups()
            if typ.upper() in ('T', 'D', 'B', 'R', 'W', 'V', 'A'):
                sym.setdefault(name, []).append((int(addr, 16), typ))
    return sym


def parse_objects(text):
    sym = {}
    for line in text.splitlines():
        m = OWNER.match(line)
        if m:
            owner, addr, typ, name = m.groups()
            if typ.upper() in ('T', 'D', 'B', 'R', 'W', 'V', 'A'):
                sym.setdefault(name, []).append(Definition(name, int(addr, 16), typ, owner.replace('\\', '/')))
    return sym


def parse_map(text):
    """Conservative GNU ld .text range ownership; no guessing absent sections."""
    sections = []
    symbols = {}
    for line in text.splitlines():
        m = MAP_SECTION.match(line)
        if m:
            _, addr, size, owner = m.groups()
            lo, extent = int(addr, 16), int(size, 16)
            if extent:
                sections.append((lo, lo + extent, owner.strip().replace('\\', '/')))
        m = MAP_SYMBOL.match(line)
        if m:
            addr, name = m.groups()
            if re.fullmatch(r'[A-Za-z_][\w.$@]*', name):
                symbols.setdefault(name, []).append(int(addr, 16))
    return sections, symbols


def inspect(name, expected, elf, obj, map_content=None):
    results = []
    def add(state, msg):
        results.append((state, msg))
    targets = elf.get(name, [])
    if not targets:
        add('BLOCKED', 'missing linked ELF symbol')
        return results
    unique = set(addr for addr, typ in targets)
    if len(unique) != 1:
        add('BLOCKED', 'ELF symbol has conflicting linked addresses')
        return results
    address = next(iter(unique))
    add('PASS', f'linked ELF public symbol address 0x{address:x}')
    definitions = obj.get(name, [])
    strong = [d for d in definitions if d.kind in STRONG]
    expected_matches = [d for d in definitions if Path(d.owner).name == Path(expected).name]
    if not expected_matches:
        add('BLOCKED', f'expected object {expected} has no defined symbol')
        return results
    if len({d.owner for d in strong}) > 1:
        add('BLOCKED', 'MULTIPLE STRONG definitions + --allow-multiple-definition: hidden winner risk')
        return results
    if any(d.kind in STRONG for d in definitions) and not any(d.kind in STRONG for d in expected_matches):
        add('BLOCKED', 'expected owner is weak but a different object is strong')
        return results
    if len({d.owner for d in expected_matches}) > 1:
        add('BLOCKED', 'ambiguous object basename, use distinct owner path')
        return results
    add('PASS', f'object owner candidate {expected_matches[0].owner} [{expected_matches[0].kind}]')
    if map_content is None:
        add('INCONCLUSIVE', 'no actual linker -Map output, cannot prove final placed section owner')
        return results
    sections, symbols = parse_map(map_content)
    if address not in symbols.get(name, []):
        add('INCONCLUSIVE', 'symbol address absent from map (no fabricated section attribution)')
        return results
    owners = {owner for lo, hi, owner in sections if lo <= address < hi}
    if len(owners) != 1:
        add('INCONCLUSIVE', f'zero or ambiguous map object section ranges: {sorted(owners)}')
        return results
    map_owner = next(iter(owners))
    if Path(map_owner).name != Path(expected).name:
        add('BLOCKED', f'final linked owner {map_owner} differs from expected {expected}')
        return results
    # A copied/stale object may share the basename with a current object.
    # The map and psp-nm -A must describe the EXACT SAME staged object path.
    if map_owner != expected_matches[0].owner:
        add('INCONCLUSIVE', f'same object basename but full map/nm owner path differs: {map_owner} vs {expected_matches[0].owner}')
        return results
    add('PASS', f'link-map range selects {map_owner}, not proof of machine-code delta')
    add('INCONCLUSIVE', 'final ELF function instruction diff / generated-object provenance NOT independently attested')
    return results


def self_test():
    passed = 0
    def check(label, condition):
        nonlocal passed
        if not condition:
            raise AssertionError(label)  # MUST run with python -O too
        passed += 1
        print(f'PASS {passed:02d}: {label}')
    elf = parse_elf('04001000 T func_good\n04001200 T func_other\n')
    obj = parse_objects('C:/exp/build/psp/rwsky.o:00000010 T func_good\nC:/exp/build/obj/scene.o:00000000 W func_good\n')
    mapper = '.text 0x04000f00 0x200 C:/exp/build/psp/rwsky.o\n 0x04001000 func_good\n'
    good = inspect('func_good', 'rwsky.o', elf, obj, mapper)
    check('one strong expected owner and one weak native shadow', not any(x[0] == 'BLOCKED' for x in good))
    check('GNU ld section ownership candidate found', any('link-map range selects' in x[1] for x in good))
    check('final disassembly proof explicitly remains inconclusive', any(x[0] == 'INCONCLUSIVE' for x in good))
    fail = inspect('func_good', 'rwsky.o', elf, obj)
    check('missing map never incorrectly certifies linked source owner', any('no actual linker' in x[1] for x in fail))
    both = parse_objects('x/rwsky.o:00000010 T func_good\nx/scene.o:00000020 T func_good')
    check('two strong object definitions rejected', any(x[0] == 'BLOCKED' for x in inspect('func_good', 'rwsky.o', elf, both, mapper)))
    wrong = '.text 0x04000f00 0x200 C:/exp/build/obj/scene.o\n 0x04001000 func_good\n'
    check('wrong winner linker-map rejected', any(x[0] == 'BLOCKED' for x in inspect('func_good', 'rwsky.o', elf, obj, wrong)))
    check('absent linked symbol rejected', any(x[0] == 'BLOCKED' for x in inspect('absent', 'rwsky.o', elf, obj, mapper)))
    check('missing map symbol is unresolved not invented', any(x[0] == 'INCONCLUSIVE' for x in inspect('func_good', 'rwsky.o', elf, obj, '.text 0x04000f00 0x200 x/rwsky.o')))
    obj2 = parse_objects('a/rwsky.o:00000000 W func_good\na/scene.o:00000000 T func_good')
    check('unexpected strong owner over weak owner rejected', any(x[0] == 'BLOCKED' for x in inspect('func_good','rwsky.o',elf,obj2,mapper)))
    obj3 = parse_objects('a/rwsky.o:00000000 T func_good\nb/rwsky.o:00000000 W func_good')
    check('duplicate basename rejected', any(x[0] == 'BLOCKED' for x in inspect('func_good','rwsky.o',elf,obj3,mapper)))
    check('Windows drive path parsed as owner not C prefix', obj['func_good'][0].owner.startswith('C:/exp/'))
    check('map section extent excludes later symbol', not any(lo<=0x4001200<hi for lo,hi,o in parse_map(mapper)[0]))
    same_basename_stale = '.text 0x04000f00 0x200 C:/stale/rwsky.o\n 0x04001000 func_good\n'
    collision = inspect('func_good', 'rwsky.o', elf, obj, same_basename_stale)
    check('same basename from different full path cannot candidate-PASS', any('full map/nm owner path differs' in x[1] and x[0] == 'INCONCLUSIVE' for x in collision))
    check('exact candidate path remains supported', any('link-map range selects' in x[1] for x in good))
    print(f'{passed}/{passed} synthetic ELF/map owner fixtures PASS; no real PSP ELF examined')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--elf-nm', type=Path)
    p.add_argument('--objects-nm', type=Path)
    p.add_argument('--linker-map', type=Path)
    p.add_argument('--expect', action='append', default=[], help='FUNC=owner.o repeatable')
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.elf_nm or not args.objects_nm or not args.expect:
        p.error('--elf-nm, --objects-nm and >=1 --expect are mandatory for audit')
    e = parse_elf(args.elf_nm.read_text(encoding='utf-8', errors='replace'))
    o = parse_objects(args.objects_nm.read_text(encoding='utf-8', errors='replace'))
    m = args.linker_map.read_text(encoding='utf-8', errors='replace') if args.linker_map else None
    states = []
    for want in args.expect:
        if '=' not in want:
            p.error('expected FUNC=owner.o')
        name, owner = want.split('=', 1)
        print(f'OWNER {name} expects {owner}')
        for state, msg in inspect(name, owner, e, o, m):
            states.append(state)
            print(f'  {state}: {msg}')
    if 'BLOCKED' in states:
        print('NO-GO: wrong/ambiguous final symbol owner')
        raise SystemExit(2)
    if 'INCONCLUSIVE' in states:
        print('NOT VERIFIED: final machine-code delta / linker attribution unproven')
        raise SystemExit(3)
    print('OWNER CANDIDATE PASS; still require ELF disassembly and runtime')

if __name__ == '__main__':
    main()
