#!/usr/bin/env python
"""Build and package the XMB release plus converted movies for an existing P4 installation.

python tools/psp_release.py
python tools/psp_release.py --no-build  # package an already built release
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / 'build/psp_dist/PSP/GAME/P4'


def sha256(path):
    # Some SMB servers transiently reject a large read after a directory rename.
    for attempt in range(3):
        try:
            h = hashlib.sha256()
            with path.open('rb') as f:
                for block in iter(lambda: f.read(1 << 20), b''):
                    h.update(block)
            return h.hexdigest()
        except OSError as error:
            if attempt == 2:
                raise OSError(f'Cannot hash {path}: {error}') from error
            time.sleep(.25)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--audio-dir', type=Path, default=ROOT / 'build/audio_pack/SOUND')
    args = parser.parse_args()
    number = int(re.search(r'^#define BUILD (\d+)$', (ROOT / 'port/main.c').read_text(), re.M)[1])
    out = args.out or ROOT / f'build/release_b{number}'
    if not args.no_build:
        env = os.environ.copy()
        subprocess.run([sys.executable, str(ROOT / 'tools/psp_hw_debug.py'),
                        '--release', '--out', str(out)], env=env, check=True)
    manifest = json.loads((out / 'manifest.json').read_text())
    if manifest.get('build') != number or not manifest.get('release'):
        raise ValueError('Expected a release manifest for the current BUILD')
    for name, digest in manifest['source_sha256'].items():
        if sha256(ROOT / name) != digest:
            raise ValueError(f'Source changed since the build: {name}')
    update = out / 'UPDATE/PSP/GAME/P4'
    update.mkdir(parents=True, exist_ok=True)
    planned = {}
    for name in ['EBOOT.PBP', 'p4data.bin', 'p4str.bin']:
        expected = manifest['artifact_sha256'][name]
        if sha256(out / name) != expected:
            raise ValueError(f'Build artifact hash mismatch: {name}')
        planned[name] = (out / name, expected)
    movies = sorted((DIST / 'VIDEO').glob('*.json'))
    if len(movies) != 23:
        raise ValueError('Prepare all 23 movies with tools/psp_video.py --all first')
    for movie in movies:
        data = json.loads(movie.read_text())
        if data['options'].get('seconds') is not None:
            raise ValueError(f'Partial movie in release resources: {movie.stem}')
        for name, expected in data['outputs'].items():
            if Path(name).suffix in ['.p4v', '.pcm']:
                planned['VIDEO/' + name] = (movie.parent / name, expected)
    audio = json.loads((args.audio_dir / 'audio_manifest.json').read_text())
    catalogue = json.loads((ROOT / 'build/audio_loose/catalogue.json').read_text())
    if audio.get('prepared') != len(catalogue) or audio.get('expected') != len(catalogue):
        raise ValueError('Prepare and pack the complete audio catalogue before releasing')
    index = (args.audio_dir / 'audio.idx').read_bytes()
    if index[:4] != b'P4I3':
        raise ValueError('Repack audio with the current tools/psp_audio.py')
    for name, expected in audio['files'].items():
        planned['SOUND/' + name] = (args.audio_dir / name, expected)
    planned['SOUND/audio_manifest.json'] = (args.audio_dir / 'audio_manifest.json', sha256(args.audio_dir / 'audio_manifest.json'))
    hashes = {}
    for name, (source, expected) in planned.items():
        target = update / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or sha256(target) != expected:
            temporary = target.with_suffix(target.suffix + '.tmp')
            shutil.copyfile(source, temporary)
            if sha256(temporary) != expected:
                raise ValueError(f'Copy hash mismatch: {name}')
            os.replace(temporary, target)
        hashes[name] = expected
    # Keep the full installation directory usable too; archives/index and saves are independent.
    for name in ['EBOOT.PBP', 'p4data.bin', 'p4str.bin']:
        shutil.copyfile(out / name, DIST / name)
    for name in hashes:
        if name.startswith('SOUND/'):
            target = DIST / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(update / name, target)
    size = sum((update / name).stat().st_size for name in hashes)
    (out / 'release.json').write_text(json.dumps({
        'build': number, 'release': True, 'files': hashes, 'bytes': size,
        'movies': len(movies), 'audio_streams': audio['prepared'], 'update_root': 'UPDATE/PSP/GAME/P4',
        'required_existing': ['p4files.idx', 'DATA.0', 'DATA.1', 'DATA.2',
                              'BTL.0', 'BGM.0', 'ENV.0', 'ZZZZZ.0']}, indent=2) + '\n', encoding='utf-8')
    (out / 'COPY_TO_CARD.txt').write_text(
        f'Persona 4 PSP b{number}\n\n'
        'SOUND/ contains the converted original music, voices and effect banks. Copy the whole directory.\n\n'
        'Copy the PSP directory from UPDATE to the memory-card root, merging directories\n'
        'and replacing files with the same names. Destination: ms0:/PSP/GAME/P4/.\n\n'
        'This updates EBOOT.PBP, p4data.bin, p4str.bin, SOUND/ and VIDEO/*.p4v, VIDEO/*.pcm.\n'
        'Game archives DATA.*, BTL.0, BGM.0, ENV.0, ZZZZZ.0 and p4files.idx\n'
        'are required from an existing installation. Saves are not included in the update.\n\n'
        'For a clean installation, copy build/psp_dist/PSP/GAME/P4 to PSP/GAME/P4\n'
        'on the card. Start Persona 4 from XMB. Start or Circle skips a movie.\n', encoding='utf-8')
    print(f'b{number}: {len(hashes)} files, {size / (1<<20):.1f} MiB; copy {out / "UPDATE/PSP"} to the card root')


if __name__ == '__main__':
    main()
