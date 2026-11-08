#!/usr/bin/env python
"""Audit every rebased word and retained retail-looking value in the linked PSP build.

Remaining numeric values are candidates, not proof of pointers. Reports go to build/address_audit.
Bad rebases and detected direct retail RAM accesses fail the build; MMIO needs PSP implementations.
"""
import bisect
import json
import struct
from collections import Counter

from psp_data_census import BASE, CODE, DATA, END, END_DATA, ROOT, inside, load
from psp_make_data import K, START, native_functions
from psp_ptr_proof import vu_regions
from psp_reasm import HW
from psp_xref import s16


def elf_sections(blob):
    assert blob[:6] == b'\x7fELF\x01\x01', 'expected ELF32 little-endian'
    offset = struct.unpack_from('<I', blob, 32)[0]
    size, count, names = struct.unpack_from('<HHH', blob, 46)
    assert size == 40
    headers = [struct.unpack_from('<10I', blob, offset + i * size) for i in range(count)]
    strings = headers[names]
    names_blob = blob[strings[4]:strings[4] + strings[5]]
    return [(names_blob[h[0]:].split(b'\0', 1)[0].decode(), h,
             b'' if h[1] == 8 else blob[h[4]:h[4] + h[5]]) for h in headers]


def low_code_references(words, address):
    """Straight-line constant tracking; report formed addresses separately from actual accesses."""
    live, reset = {}, False
    for i, w in enumerate(words):
        pc = address + i * 4
        op, rs, rt, rd, imm = w >> 26, w >> 21 & 31, w >> 16 & 31, w >> 11 & 31, w & 65535
        after_slot, reset = reset, False
        if op in (2, 3):
            target = ((pc + 4) & 0xf0000000) | (w & 0x3ffffff) << 2
            if BASE <= target < END:
                yield pc, target, 'direct_jump'
            reset = True
        elif op == 15:
            live[rt] = imm << 16
        elif op in (9, 13):
            value = (live[rs] + s16(imm) if op == 9 else live[rs] | imm) if rs in live else None
            live.pop(rt, None)
            if value is not None:
                live[rt] = value & 0xffffffff
                if BASE <= value < END:
                    yield pc, value, 'formed_constant'
        elif op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2e, 0x31, 0x39):
            if rs in live:
                target = live[rs] + s16(imm)
                if BASE <= target < END:
                    yield pc, target, 'direct_memory'
                elif (target & 0x1fffffff) >> 16 in HW or 0x70000000 <= target < 0x70004000:
                    yield pc, target, 'ps2_hardware_memory'
            if op in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26):
                live.pop(rt, None)
        elif op == 0:
            if w & 63 in (0x21, 0x25) and (rs == 0 or rt == 0):
                value = live.get(rs or rt)
                live.pop(rd, None)
                if value is not None:
                    live[rd] = value
            else:
                live.pop(rd, None)
            reset = w & 63 in (8, 9)
        elif op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or op == 0x11 and rs == 8:
            reset = True
        else:
            live.pop(rt, None)
        live.pop(0, None)
        if after_slot:
            live.clear()


def main():
    out = ROOT / 'build/address_audit'
    out.mkdir(parents=True, exist_ok=True)
    image, _, _ = load()
    built = (ROOT / 'build/psp/p4data.bin').read_bytes()
    assert len(built) == END_DATA - START
    native = native_functions()
    cases = {int(a, 16): native[int(v, 16)] for a, v in
             (line.split() for line in (ROOT / 'build/psp/reasm_cases.tsv').read_text().splitlines())}
    errors, proof = [], {}
    for line in (ROOT / 'build/rebase_table.tsv').read_text().splitlines():
        a, v, kind = line.split(); a, v = int(a, 16), int(v, 16)
        assert a not in proof, hex(a)
        proof[a] = v
        expected = cases.get(a, native.get(v) if kind == 'func' else v + K)
        actual = struct.unpack_from('<I', built, a - START)[0]
        if expected is None or actual != expected:
            errors.append({'address': a, 'expected': expected, 'actual': actual, 'kind': kind})
    for a, expected in cases.items():
        actual = struct.unpack_from('<I', built, a - START)[0]
        if actual != expected:
            errors.append({'address': a, 'expected': expected, 'actual': actual, 'kind': 'switch_target'})
    vu = vu_regions(image)
    remaining = []
    for a in range(START, END_DATA, 4):
        value = struct.unpack_from('<I', built, a - START)[0]
        if BASE <= value < END and not inside(vu, a):
            kind = 'code' if inside(CODE, value) else 'microcode' if inside(vu, value) else 'data' if inside(DATA, value) else 'bss'
            remaining.append({'address': a, 'target': value, 'kind': kind})
    sections = elf_sections((ROOT / 'build/psp/p4.elf').read_bytes())
    functions = []
    for _, h, data in sections:
        if h[1] == 2:
            strings = sections[h[6]][2]
            for offset in range(0, len(data), 16):
                name, value, _, info, _, section = struct.unpack_from('<IIIBBH', data, offset)
                if info & 15 == 2 and section and value:
                    functions.append((value, strings[name:].split(b'\0', 1)[0].decode()))
    functions.sort()
    starts = [value for value, _ in functions]
    code, constants = [], []
    for name, header, data in sections:
        if not header[2] & 2 or not data:
            continue
        words = struct.unpack_from(f'<{len(data) // 4}I', data)
        if header[2] & 4:
            code.extend({'address': a, 'target': v, 'kind': kind, 'section': name}
                        for a, v, kind in low_code_references(words, header[3]))
        else:
            constants.extend({'address': header[3] + i * 4, 'target': v, 'section': name}
                             for i, v in enumerate(words) if BASE <= v < END)
    for row in code:
        index = bisect.bisect_right(starts, row['address']) - 1
        row['function'] = functions[index][1] if index >= 0 else None
    hardware = [row for row in code if row['kind'] == 'ps2_hardware_memory']
    report = {'proof_entries_checked': len(proof), 'switch_entries_checked': len(cases),
              'image_words_scanned': len(built) // 4,
              'linked_executable_bytes_scanned': sum(len(data) for _, h, data in sections if h[2] & 4),
              'proof_errors': errors, 'code_references': code, 'linked_constants': constants,
              'ps2_hardware_accesses': len(hardware),
              'remaining_counts': dict(Counter(row['kind'] for row in remaining)),
              'remaining_values': remaining,
              'limits': ['Numeric candidates are not confirmed pointers.',
                         'Straight-line code tracking does not prove all computed or indirect addresses.',
                         'Files loaded from disc and runtime-generated pointers require gameplay checks.',
                         'PS2 MMIO accesses need PSP implementations; they are not repaired by rebasing.']}
    (out / 'audit.json').write_text(json.dumps(report, indent=2))
    for name, rows in [('remaining', remaining), ('code', code), ('linked_constants', constants)]:
        (out / (name + '.tsv')).write_text(''.join(f"{row['address']:08x}\t{row['target']:08x}\t{row.get('kind', row.get('section'))}\n" for row in rows))
    print(json.dumps({k: v for k, v in report.items() if k not in ('remaining_values', 'linked_constants', 'code_references')}, indent=2))
    print('Code candidates:', dict(Counter(row['kind'] for row in code)), 'linked data candidates:', len(constants))
    summary = [
        '# PSP address audit', '',
        f"Scanned {len(built) // 4:,} data words and {report['linked_executable_bytes_scanned']:,} executable bytes.",
        f"Checked {len(proof):,} rebase entries and {len(cases)} active switch targets; mismatches: {len(errors)}.", '',
        '## PS2 hardware accesses needing PSP implementations', '',
        'These retained routines require service replacements, not address rebasing. Static presence does not prove they run in the tested scene.', '',
        '| Function | Accesses |', '|---|---:|',
        *[f'| {fn} | {count} |' for fn, count in Counter(row['function'] for row in hardware).most_common()], '',
        '## Numeric candidates', '',
        'remaining.tsv lists retained values in the retail address range, including text bytes, archive offsets and unused retail switch tables. They must not be rebased blindly.',
        'code.tsv and audit.json list code references with function names; linked_constants.tsv covers linked non-executable sections.', '',
        '## Limits', '', *['- ' + limit for limit in report['limits']], '',
    ]
    (out / 'report.md').write_text('\n'.join(summary))
    if errors or any(row['kind'] in ('direct_memory', 'direct_jump') for row in code):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
