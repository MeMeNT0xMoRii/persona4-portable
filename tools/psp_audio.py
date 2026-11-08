#!/usr/bin/env python
"""Prepare SAS audio from the user's ISO. Generated game assets stay in build/.

python tools/psp_audio.py --list
python tools/psp_audio.py --match 'BGM/01.ADX'
python tools/psp_audio.py --all
"""
import argparse
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from psp_disc import ISO, SECTOR, CVM_BASE, root, walk, path_hash
from psp_video import ffmpeg_path

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent.parent))
LOOSE = ROOT / 'build/audio_loose'
HEADER = struct.Struct('<4s7I')


def fingerprint(data):
    h = 0x811c9dc5
    for value in data[:1024]:
        h = ((h ^ value) * 0x01000193) & 0xffffffff
    return h


def signature2(data):
    h = 0x811c9dc5
    chunks = [data] if len(data) <= 1024 else [data[k*(len(data)-64)//15: k*(len(data)-64)//15+64] for k in range(16)]
    for chunk in chunks:
        for value in chunk:
            h = ((h ^ value) * 0x01000193) & 0xffffffff
    return h


def audit_signatures(rows, iso):
    groups = defaultdict(list)
    for row in rows:
        if 'signature' in row:
            groups[row['signature']].append(row)
            row['signature2'] = 0
            row['signature_offsets'] = []
    with iso.open('rb') as f:
        for primary, group in groups.items():
            if len(group) < 2:
                continue
            contents = {}
            for row in group:
                f.seek(row['offset']); data = f.read(row['size'])
                digest = hashlib.sha256(data).hexdigest()
                row['_content_digest'] = digest
                contents[digest] = data
            if len(contents) > 1:
                # Pick actual distinguishing byte positions, rather than hope
                # fixed samples happen to include a small differing region.
                offsets = []
                while True:
                    buckets = defaultdict(list)
                    for digest, data in contents.items():
                        key = (len(data), bytes(data[o] if o < len(data) else 0 for o in offsets))
                        buckets[key].append(digest)
                    collision = next((v for v in buckets.values() if len(v) > 1), None)
                    if collision is None:
                        break
                    a, b = (contents[d] for d in collision[:2])
                    offsets.append(next(i for i, (x, y) in enumerate(zip(a, b)) if x != y))
                    if len(offsets) > 64:
                        raise ValueError(f'Too many memory-audio discriminators: {primary:08x}')
                seen = {}
                for digest, data in contents.items():
                    h = 0x811c9dc5
                    for o in offsets:
                        h = ((h ^ (data[o] if o < len(data) else 0)) * 0x01000193) & 0xffffffff
                    key = (len(data), h)
                    if key in seen and seen[key] != digest:
                        raise ValueError('Discriminator hash collision')
                    seen[key] = digest
                for row in group:
                    data = contents[row['_content_digest']]
                    h = 0x811c9dc5
                    for o in offsets:
                        h = ((h ^ (data[o] if o < len(data) else 0)) * 0x01000193) & 0xffffffff
                    row['signature2'], row['signature_offsets'] = h, offsets
            for row in group:
                del row['_content_digest']
    return rows


def catalogue(iso):
    """Absolute byte ranges; AFS indices and ACX indices preserve game numbering."""
    rows = []
    # Voice archives contain thousands of small adjacent streams. Read ahead
    # rather than turn every header into a separate network/disk operation.
    with iso.open('rb', buffering=1 << 20) as f:
        top = {p: (l, n) for p, l, n in walk(f, 0, *root(f, 0))}
        for container in ('BGM', 'ENV', 'DATA', 'BTL'):
            base = top[container + '.CVM'][0] * SECTOR + CVM_BASE
            for path, lba, size in walk(f, base, *root(f, base)):
                offset = base + lba * SECTOR
                if path.endswith(('.ADX', '.AHX')):
                    key = container + '/' + path if container in ('BGM', 'ENV') else path
                    rows.append(dict(path=key, offset=offset, size=size))
                elif path.endswith(('.AFS', '.ACX')):
                    f.seek(offset); header = f.read(8)
                    endian = '<' if path.endswith('.AFS') else '>'
                    if endian == '<' and header[:4] != b'AFS\0':
                        raise ValueError('Invalid AFS: ' + path)
                    count = struct.unpack_from(endian + 'I', header, 4)[0]
                    if count > 100000 or 8 + count * 8 > size:
                        raise ValueError('Invalid archive count: ' + path)
                    table = f.read(count * 8)
                    for index in range(count):
                        start, length = struct.unpack_from(endian + 'II', table, index * 8)
                        if not length:
                            continue
                        if start < 8 + count * 8 or start + length > size:
                            raise ValueError(f'Invalid member {path}#{index}')
                        rows.append(dict(path=f'{path}#{index}', offset=offset + start, size=length))
        for row in rows:
            f.seek(row['offset']); data = f.read(min(1024, row['size']))
            if data[:2] != b'\x80\0':
                row['unsupported'] = data[:8].hex()
                continue
            row['encoding'] = data[4]
            row['channels'] = data[7]
            row['rate'], row['samples'] = struct.unpack_from('>II', data, 8)
            row['signature'] = fingerprint(data)
            row['hash'] = path_hash(row['path'])
            row['loop_start'] = row['loop_end'] = 0
            if data[4] == 3:
                version = struct.unpack_from('>H', data, 18)[0]
                loop = 0x14 if version == 0x300 else 0x18 + max(8, 4 * data[7])
                header_end = struct.unpack_from('>H', data, 2)[0] + 4
                if version in (0x300, 0x400) and loop + 24 <= min(header_end - 6, len(data)):
                    flag, begin, _, end = struct.unpack_from('>4I', data, loop + 4)
                    if flag and 0 <= begin < end <= row['samples']:
                        row['loop_start'], row['loop_end'] = begin, end
    keys = {}
    for row in rows:
        if 'hash' in row:
            if row['hash'] in keys and keys[row['hash']] != row['path']:
                raise ValueError('Path hash collision')
            keys[row['hash']] = row['path']
    return audit_signatures(rows, iso)


def build_encoder(explicit=None):
    if explicit is not None:
        if not Path(explicit).is_file():
            raise FileNotFoundError(explicit)
        return Path(explicit)
    folder = ROOT / 'build/audio_tools'
    folder.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/audio_vag_encoder.cs'
    target = folder / 'audio_vag_encoder.exe'
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    stamp = folder / 'encoder.sha256'
    if not target.exists() or not stamp.exists() or stamp.read_text() != digest:
        compiler = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
        result = subprocess.run([str(compiler), '/nologo', '/optimize+', f'/out:{target}', str(source)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        stamp.write_text(digest)
    return target


def convert(row, iso, out, ffmpeg, encoder, vgmstream=None):
    if 'unsupported' in row:
        raise ValueError(f'Unsupported audio {row["path"]}: {row["unsupported"]}')
    name = f'{row["hash"]:08x}.p4a'
    target = out / name
    stamp = out / (name + '.json')
    options = dict(version=1, signature=row['signature'], size=row['size'],
                   encoder_sha256=hashlib.sha256((ROOT / 'tools/audio_vag_encoder.cs').read_bytes()).hexdigest())
    if target.exists() and stamp.exists():
        saved = json.loads(stamp.read_text())
        if saved.get('options') == options and hashlib.sha256(target.read_bytes()).hexdigest() == saved['sha256']:
            return row
    with tempfile.TemporaryDirectory(prefix='p4-audio-') as temporary:
        work = Path(temporary)
        source = work / ('input.ahx' if row['encoding'] == 0x11 else 'input.adx')
        pcm, blocks = work / 'audio.pcm', work / 'audio.blocks'
        with iso.open('rb') as f:
            f.seek(row['offset']); source.write_bytes(f.read(row['size']))
        if row['encoding'] == 0x11:
            if not vgmstream:
                raise ValueError('AHX requires --vgmstream (vgmstream-cli.exe): ' + row['path'])
            wav = work / 'audio.wav'
            command = [str(vgmstream), '-i', '-o', str(wav), str(source)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(row['path'] + '\n' + result.stderr)
            source = wav
        command = [str(ffmpeg), '-v', 'error', '-y', '-i', str(source), '-map', '0:a:0',
                   '-ar', str(row['rate']), '-ac', str(row['channels']), '-f', 's16le', str(pcm)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(row['path'] + '\n' + result.stderr)
        samples = pcm.stat().st_size // (2 * row['channels'])
        if samples < row['samples']:
            raise ValueError('Short decoded audio: ' + row['path'])
        # Trim decoder padding and the tail beyond the authored loop end.
        samples = row['loop_end'] or row['samples']
        with pcm.open('r+b') as f:
            f.truncate(samples * row['channels'] * 2)
        loop_block = row['loop_start'] // 28 if row['loop_end'] else -1
        result = subprocess.run([str(encoder), str(pcm), str(blocks), str(row['channels']), str(loop_block)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        payload = blocks.read_bytes()
        count = (samples + 27) // 28
        if len(payload) != count * row['channels'] * 16:
            raise ValueError('Invalid encoded block count')
        header = HEADER.pack(b'P4A1', row['rate'], row['channels'], samples,
                             loop_block if loop_block >= 0 else 0xffffffff, count, 1, 0)
        temporary_target = target.with_suffix('.tmp')
        temporary_target.write_bytes(header + payload)
        os.replace(temporary_target, target)
        stamp.write_text(json.dumps(dict(source=row, options=options,
                         sha256=hashlib.sha256(target.read_bytes()).hexdigest()), indent=2) + '\n')
    return row


def copy_banks(iso, out):
    with iso.open('rb') as f:
        top = {p: (l, n) for p, l, n in walk(f, 0, *root(f, 0))}
        base = top['DATA.CVM'][0] * SECTOR + CVM_BASE
        for p, lba, size in walk(f, base, *root(f, base)):
            if p.startswith('SPU/') and p.endswith(('.HD', '.BD', '.SQ')):
                f.seek(base + lba * SECTOR)
                (out / p.split('/')[-1]).write_bytes(f.read(size))


def available_rows(rows, out):
    ready = {int(p.stem, 16) for p in out.glob('*.p4a')}
    return [r for r in rows if r.get('hash') in ready]


def pack_audio(rows, loose, destination):
    """A handful of large files avoids thousands of FAT directory lookups on PSP."""
    destination.mkdir(parents=True, exist_ok=True)
    selected = available_rows(rows, loose)
    records, seen, parts, selectors = [], {}, [], {}
    blob = bytearray()
    part, position, output = -1, 0, None
    try:
        for row in sorted(selected, key=lambda r: r['hash']):
            name = f'{row["hash"]:08x}.p4a'
            stamp = json.loads((loose / (name + '.json')).read_text())
            digest = stamp['sha256']
            if digest not in seen:
                data = (loose / name).read_bytes()
                if hashlib.sha256(data).hexdigest() != digest:
                    raise ValueError('Corrupted prepared audio: ' + name)
                aligned = (position + 63) & ~63
                if output is None or aligned + len(data) > 1 << 30:
                    if output:
                        output.close()
                    part += 1; position = aligned = 0
                    target = destination / f'AUDIO.{part}.tmp'
                    parts.append(target)
                    output = target.open('wb')
                output.write(b'\0' * (aligned-position))
                output.write(data)
                position = aligned + len(data)
                seen[digest] = (part, aligned)
            number, offset = seen[digest]
            offsets = tuple(row.get('signature_offsets', []))
            if offsets and offsets not in selectors:
                selectors[offsets] = len(blob)
                blob.extend(struct.pack('<I', len(offsets)))
                blob.extend(struct.pack(f'<{len(offsets)}I', *offsets))
            selector = selectors[offsets] if offsets else 0xffffffff
            records.append((row['hash'], row['signature'], row['size'], row['hash'], number, offset, row.get('signature2', 0), selector))
    finally:
        if output:
            output.close()
    for temporary in parts:
        os.replace(temporary, temporary.with_suffix(''))
    index = b'P4I3' + struct.pack('<3I', len(records), 32, len(blob))
    index += b''.join(struct.pack('<8I', *r) for r in records) + blob
    (destination / 'audio.idx.tmp').write_bytes(index)
    os.replace(destination / 'audio.idx.tmp', destination / 'audio.idx')
    for p in loose.glob('*'):
        if p.suffix in ('.HD', '.BD', '.SQ'):
            (destination / p.name).write_bytes(p.read_bytes())
    files = [p for p in destination.iterdir() if p.name == 'audio.idx' or p.name.startswith('AUDIO.') or p.suffix in ('.HD', '.BD', '.SQ')]
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    manifest = dict(version=1, prepared=len(selected), expected=len(rows), unique=len(seen),
                    files=hashes, bytes=sum(p.stat().st_size for p in files))
    (destination / 'audio_manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packed {len(records)}/{len(rows)} resources, {len(parts)} parts, {len(seen)} unique streams', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iso', type=Path, default=ISO)
    parser.add_argument('--out', type=Path, default=LOOSE)
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--match', action='append', default=[])
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--ffmpeg')
    parser.add_argument('--vgmstream', type=Path)
    parser.add_argument('--catalogue', type=Path, help='reuse a catalogue generated from this exact ISO')
    parser.add_argument('--pack', type=Path, help='pack prepared audio into this SOUND directory')
    args = parser.parse_args()
    rows = json.loads(args.catalogue.read_text()) if args.catalogue else catalogue(args.iso)
    if args.list:
        for r in rows:
            print(r['path'], r['size'], r.get('encoding', 'unsupported'))
        return
    if not args.all and not args.match and not args.pack:
        parser.error('select --all, --match or --pack')
    selected = [r for r in rows if args.all or any(fnmatch.fnmatchcase(r['path'], p.upper()) for p in args.match)]
    args.out.mkdir(parents=True, exist_ok=True)
    print(f'{len(selected)}/{len(rows)} audio resources selected', flush=True)
    copy_banks(args.iso, args.out)
    (args.out / 'catalogue.json').write_text(json.dumps(rows, indent=2) + '\n')
    if selected:
        encoder, ffmpeg = build_encoder(), ffmpeg_path(args.ffmpeg)
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            for i, row in enumerate(pool.map(lambda r: convert(r, args.iso, args.out, ffmpeg, encoder, args.vgmstream), selected), 1):
                if i % 100 == 0 or i == len(selected):
                    print(f'{i}/{len(selected)} {row["path"]}', flush=True)
    available = available_rows(rows, args.out)
    (args.out / 'audio.idx').write_bytes(b''.join(struct.pack('<4I', r['hash'], r['signature'], r['size'], r['hash'])
                                               for r in sorted(available, key=lambda r: r['hash'])))
    print(f'{len(available)}/{len(rows)} streams prepared; banks copied')
    if args.pack:
        pack_audio(rows, args.out, args.pack)


if __name__ == '__main__':
    main()
