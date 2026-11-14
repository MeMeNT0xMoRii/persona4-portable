#!/usr/bin/env python
"""Measure retail-to-PSP translation coverage without compiling decompiled C.

This is a feasibility audit, not an EBOOT generator. It never emits an EBOOT,
assembly, logging substitutes or a publishable runtime package. Upstream source
is read only for names/function boundaries. Translation success is not a proof
of CPU/ABI equivalence or gameplay compatibility.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import re

from psp_data_census import CODE, DECOMP, ROOT, inside, load
from psp_make_data import MARKED
from psp_reasm import (CRI, NOT_32_BIT, WIDE_SHIFTS, Function, Refuse,
                       case_words, extents, falls_through, scan_addresses,
                       translate, trim)
from psp_xref import sdk_functions, sources, symbol_addresses

RETAIL_SHA1 = '4eeec0360cf2715535d9f7e52eb69d786fb0158c'
TAIL = re.compile(r'(?:func_|p4r_)([0-9a-fA-F]{8})$')


def runtime_bindings(symbols, names):
    """Map only explicit text exports, never compiled game objects or data."""
    bound = {}
    for line in symbols.splitlines():
        words = line.split()
        if len(words) != 3 or words[1] not in ('T', 'W'):
            continue
        name = words[2]
        # A wrapper export does not establish the wrapped ABI/behavior here.
        if name.startswith('__wrap_'):
            continue
        match = TAIL.fullmatch(name)
        address = int(match[1], 16) if match else names.get(name)
        if address is not None and inside(CODE, address):
            bound.setdefault(address, name)
    return bound


def classify(address, image, spans, starts, cases, sdk, native):
    if address in native:
        return 'runtime candidate', None
    if address in sdk:
        return 'SDK boundary', sdk[address]
    if address >= CRI:
        return 'CRI boundary', None
    words = trim(image, address, spans[address])
    if address >= NOT_32_BIT and any(w >> 26 == 0 and w & 63 in WIDE_SHIFTS for w in words):
        return '64-bit shift', None
    fn = Function(address, spans[address], words)
    try:
        scan_addresses(fn, None, lambda target: target in starts or target in native)
        translate(fn, lambda target: native.get(target, f'p4r_{target:08x}'), cases)
        if falls_through(words):
            raise Refuse('falls through')
    except Refuse as error:
        return str(error), None
    return 'translation candidate', None


def direct_dependencies(image, spans):
    """Conservative direct j/jal graph; indirect calls are outside this audit."""
    graph = {}
    for address, end in spans.items():
        targets = set()
        for i, word in enumerate(trim(image, address, end)):
            if word >> 26 in (2, 3):
                target = ((address + i * 4 + 4) & 0xf0000000) | ((word & 0x3ffffff) << 2)
                if not address <= target < end:
                    targets.add(target)
        graph[address] = targets
    return graph


def boot_frontier(entry, graph, statuses):
    """Stop at the first non-translatable boundary on each direct path."""
    if entry is None:
        return [], 0
    pending = deque([(entry, [entry])])
    seen, blocked = set(), []
    while pending:
        address, path = pending.popleft()
        if address in seen:
            continue
        seen.add(address)
        status = statuses.get(address, 'unknown direct target')
        if status == 'runtime candidate':
            continue
        if status != 'translation candidate':
            blocked.append({'address': f'{address:08x}', 'status': status,
                            'path': [f'{a:08x}' for a in path]})
            continue
        pending.extend((target, path + [target]) for target in sorted(graph.get(address, ()))
                       if target not in seen)
    return blocked, len(seen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-symbols', type=Path,
                        help='psp-nm --defined-only output from port-only objects; no game objects')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/generator_audit')
    args = parser.parse_args()
    # Validate the whole ELF before trusting fixed retail addresses.
    elf_path = ROOT / 'build/SLUS_217.82'
    digest = hashlib.sha1(elf_path.read_bytes()).hexdigest()
    if digest != RETAIL_SHA1:
        parser.error(f'Unsupported retail ELF SHA1: {digest}')
    image, _, _ = load()
    names = symbol_addresses()
    _, stubs, recovered = sources()
    # Read names directly from upstream; a census/compiler output is not needed.
    for path in sorted((DECOMP / 'src').rglob('*.c')):
        for address, name in MARKED.findall(path.read_text(encoding='utf-8', errors='replace')):
            names.setdefault(name, int(address, 16))
    starts = {a for a in set(names.values()) | set(stubs) | set(recovered)
              if a is not None and inside(CODE, a)}
    native = runtime_bindings(args.runtime_symbols.read_text() if args.runtime_symbols else '', names)
    spans, cases, sdk = extents(starts), case_words(image, starts), sdk_functions()
    by_address = {}
    for name, address in names.items():
        old = by_address.get(address)
        if old is None or (old.startswith(('func_', 'p4r_')) and not name.startswith(('func_', 'p4r_'))):
            by_address[address] = name
    rows = []
    for address in sorted(starts):
        status, detail = classify(address, image, spans, starts, cases, sdk, native)
        rows.append({'address': f'{address:08x}', 'name': by_address.get(address),
                     'status': status, 'detail': detail})
    counts = Counter(row['status'] for row in rows)
    boot = names.get('kwlnBoot')
    frontier, visited = boot_frontier(boot, direct_dependencies(image, spans),
                                      {int(row['address'], 16): row['status'] for row in rows})
    report = {'retail_sha1': digest, 'known_starts': len(starts),
              'counts': dict(counts.most_common()),
              'boot': next((row for row in rows if int(row['address'], 16) == boot), None),
              'boot_direct_frontier': frontier, 'boot_direct_visited': visited,
              'eboot_generation_supported': False,
              'limitations': [
                  'All known starts are scanned, including unreachable and replaced routines.',
                  'A refusal is the first detected problem, not a complete instruction audit.',
                  'Translation candidates can still have unresolved calls or unsupported indirect paths.',
                  'The boot frontier follows syntactic direct calls only, not a complete runtime call graph.',
                  'Runtime candidates identify exports, not verified EE-to-PSP ABI bridges.',
                  'The existing translator is specialized for its current C-linked fallback set.',
                  'No generated code or original game objects are included in this report.',
              ], 'functions': rows}
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'audit.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (args.out / 'functions.tsv').write_text(
        'address\tname\tstatus\tdetail\n' + ''.join(
            '\t'.join(str(row.get(k) or '') for k in ('address', 'name', 'status', 'detail')) + '\n'
            for row in rows), encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ('functions', 'boot_direct_frontier')}, indent=2), flush=True)
    print(f'Blocked direct boot boundaries: {len(frontier)}', flush=True)
    print(f'Report: {args.out}')


if __name__ == '__main__':
    main()
