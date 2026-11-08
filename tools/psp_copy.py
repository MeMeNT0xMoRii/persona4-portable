#!/usr/bin/env python
"""Build a verified 64KB PSPLink copier and stage a release deployment list."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
from psp_link import run, check_import_ranges

ROOT = Path(__file__).resolve().parent.parent


def build():
    dev = Path(os.environ['PSPDEV'])
    sdk, binaries = dev / 'psp/sdk', dev / 'bin'
    destination = ROOT / 'build/copy_tool'; destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='p4-copy-build-') as temp:
        work = Path(temp); elf = work / 'copy.elf'
        run(binaries / 'psp-gcc.exe', '-O2', '-G0', '-Wall', '-Wextra', '-Werror', '-D_PSP_FW_VERSION=600',
            f'-I{sdk / "include"}', '-c', ROOT / 'port/copy_tool.c', '-o', work / 'copy.o')
        run(binaries / 'psp-gcc.exe', '-G0', f'-L{sdk / "lib"}', '-Wl,-zmax-page-size=128',
            f'-specs={sdk / "lib/prxspecs"}', f'-Wl,-q,-T{sdk / "lib/linkfile.prx"}',
            work / 'copy.o', sdk / 'lib/prxexports.o', '-lpspdisplay', '-lpspdebug', '-lpspge', '-lpsppower', '-o', elf)
        run(binaries / 'psp-fixup-imports.exe', elf); check_import_ranges(elf)
        run(binaries / 'psp-prxgen.exe', elf, destination / 'copy.prx')
    return destination / 'copy.prx'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--stage', type=Path, default=Path('C:/temp/p3f-psplink/release_audio'))
    args = parser.parse_args()
    copier = build(); print(copier, flush=True)
    if not args.release:
        return
    manifest = json.loads((args.release / 'release.json').read_text())
    args.stage.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(copier, args.stage / 'copy.prx')
    names = [n for n in manifest['files'] if not n.startswith('VIDEO/')]
    # Publish the index last, once all ranges it references are on the card.
    names.sort(key=lambda n: (n == 'SOUND/audio.idx', n))
    lines = []
    for name in names:
        if '..' in Path(name).parts or Path(name).is_absolute():
            raise ValueError('Unsafe release path')
        source = args.release / 'UPDATE/PSP/GAME/P4' / name
        target = args.stage / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        sha256, sha1 = hashlib.sha256(), hashlib.sha1()
        with target.open('rb') as f:
            for block in iter(lambda: f.read(1 << 20), b''):
                sha256.update(block); sha1.update(block)
        if sha256.hexdigest() != manifest['files'][name]:
            raise ValueError('Staged file hash mismatch: ' + name)
        lines.append(f'{sha1.hexdigest()} {target.stat().st_size} host0:/{args.stage.name}/{name} ms0:/PSP/GAME/P4/{name}')
    (args.stage / 'deploy.lst').write_text('\n'.join(lines) + '\n', encoding='ascii')
    print(f'{len(lines)} files staged; deploy.lst is ready', flush=True)


if __name__ == '__main__':
    main()
