#!/usr/bin/env python
"""Measure the running game's audio workers through PSPLink counters."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
NAMES = ['p4_audio_cpu_us', 'p4_audio_loader_us', 'p4_audio_commands', 'p4_audio_underruns',
         'p4_audio_errors', 'p4_audio_dropped', 'p4_audio_blocks', 'p4_audio_started', 'p4_audio_finished']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('label')
    parser.add_argument('--build', required=True, type=int)
    parser.add_argument('--base', required=True, type=lambda x: int(x, 0))
    parser.add_argument('--seconds', type=float, default=15)
    parser.add_argument('--old-build', action='store_true', help='use an earlier build and its own ELF')
    parser.add_argument('--release', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.label) or args.seconds < 5:
        parser.error('simple label and at least 5 seconds required')
    folder = ROOT / f'build/{"release" if args.release else "psplink"}_b{args.build}'
    manifest = json.loads((folder / 'manifest.json').read_text())
    for name, digest in manifest['source_sha256'].items():
        if not args.old_build and hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise ValueError('Source changed since build: ' + name)
    dev = Path(os.environ['PSPDEV'])
    nm = subprocess.check_output([str(dev / 'bin/psp-nm.exe'), str(folder / 'p4.elf')], text=True)
    symbols = {p[2]: int(p[0], 16) + args.base for p in (line.split() for line in nm.splitlines()) if len(p) == 3}
    session = Path(os.environ.get('P4_SESSION', 'C:/temp/p3f-session'))
    log, queue = session / 'console.log', session / 'commands.txt'

    def query():
        position, start = log.stat().st_size, time.monotonic()
        with queue.open('a', encoding='ascii', newline='\n') as f:
            f.write(''.join(f'peekw 0x{symbols[name]:08x}\n' for name in NAMES))
        while time.monotonic() - start < 15:
            length = log.stat().st_size - position
            with log.open('rb') as f:
                f.seek(position); text = f.read(length).decode(errors='replace')
            values = {}
            for name in NAMES:
                match = re.search(rf'0x{symbols[name]:08x}: 0x([0-9a-f]{{8}})', text, re.I)
                if match:
                    values[name] = int(match[1], 16)
            if len(values) == len(NAMES):
                return values, text
            time.sleep(.1)
        raise TimeoutError('No complete audio-counter readback')

    before, raw_before = query()
    start = time.monotonic()
    time.sleep(args.seconds)
    after, raw_after = query()
    elapsed = time.monotonic() - start
    delta = {n: (after[n] - before[n]) & 0xffffffff for n in NAMES}
    frames = delta['p4_audio_blocks'] * 512 / 44100 * 30
    result = dict(build=args.build, base=args.base, elapsed_seconds=elapsed, before=before, after=after,
                  delta=delta, audio_ms_per_tick=delta['p4_audio_cpu_us']/1000/frames if frames else None,
                  loader_ms_per_tick=delta['p4_audio_loader_us']/1000/frames if frames else None)
    if frames:
        result['total_ms_per_tick'] = result['audio_ms_per_tick'] + result['loader_ms_per_tick']
    out = folder / 'experiments'; out.mkdir(exist_ok=True)
    (out / (args.label + '_audio.json')).write_text(json.dumps(result, indent=2) + '\n')
    (out / (args.label + '_audio.log')).write_text(raw_before + '\n' + raw_after)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
