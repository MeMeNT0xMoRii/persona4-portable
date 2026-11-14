#!/usr/bin/env python
"""Build a source-compiling Windows preparation package without a game EBOOT.

python tools/psp_prepare_release.py --vgmstream ./helpers/vgmstream --freeze --out ./build/P4-PSP-source

This creates a local package for review; it never publishes a GitHub release.
"""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from psp_prepare import apply_recipe, sha256
from psp_build import (UPSTREAM_COMMIT, UPSTREAM_URL, UPSTREAM_TREE_SHA256,
                       payload_files, find_pspdev)
import re

ROOT = Path(__file__).resolve().parent.parent


def make_recipe(elf, data, strings, eboot, build):
    if elf[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected the original little-endian ELF32 executable.')
    phoff = struct.unpack_from('<I', elf, 28)[0]
    phsize, phnum = struct.unpack_from('<HH', elf, 42)
    segment = next(p for p in (struct.unpack_from('<8I', elf, phoff + i * phsize) for i in range(phnum))
                   if p[0] == 1 and p[2] <= 0x52da00 and p[2] + p[4] >= 0x52da00 + len(data))
    offset = segment[1] + 0x52da00 - segment[2]
    original = elf[offset:offset + len(data)]
    words, zeros = [], []
    for pos in range(0, len(data), 4):
        if data[pos:pos+4] == original[pos:pos+4]:
            continue
        value = struct.unpack_from('<I', data, pos)[0]
        if value == 0:
            if zeros and zeros[-1][0] + zeros[-1][1] == pos:
                zeros[-1][1] += 4
            else:
                zeros.append([pos, 4])
        else:
            words.append([pos, value])
    recipe = dict(version=1, build=build, edition='Persona 4 USA / SLUS-21782',
                  elf_sha256=hashlib.sha256(elf).hexdigest(), data_offset=offset, data_size=len(data),
                  zero_ranges=zeros, words=words, string_offsets=list(struct.unpack(f'<{len(strings)//4}I', strings)),
                  data_sha256=hashlib.sha256(data).hexdigest(), strings_sha256=hashlib.sha256(strings).hexdigest(),
                  eboot_sha256=hashlib.sha256(eboot).hexdigest(), audio_streams=17375, movies=23)
    assert apply_recipe(elf, recipe) == (data, strings)
    return recipe


def copy_licenses(destination):
    destination.mkdir(parents=True, exist_ok=True)
    for name in ('av', 'pyinstaller', 'pyinstaller-hooks-contrib', 'imageio-ffmpeg'):
        dist = importlib.metadata.distribution(name)
        for file in dist.files or ():
            if any(word in str(file).lower() for word in ('license', 'copying')) and Path(dist.locate_file(file)).is_file():
                target = destination / name / str(file).replace('..', '_')
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(dist.locate_file(file), target)
    for path in (Path(sys.base_prefix) / 'LICENSE.txt', Path(sys.base_prefix) / 'tcl/tcl8.6/license.terms',
                 Path(sys.base_prefix) / 'tcl/tk8.6/license.terms'):
        if path.exists(): shutil.copyfile(path, destination / (path.parent.name + '-' + path.name))
    # Python 3.14 stores Tcl's scripts and license inside its DLL zip filesystem.
    import tkinter
    tcl = tkinter.Tcl()
    library = tcl.eval('info library')
    license_path = library + '/license.terms'
    if int(tcl.call('file', 'exists', license_path)):
        handle = tcl.call('open', license_path)
        try:
            (destination / 'Tcl-license.terms').write_text(tcl.call('read', handle), encoding='utf-8')
        finally:
            tcl.call('close', handle)
    for path in (Path(sys.base_prefix) / 'tcl').glob('tk*/**/license.terms'):
        shutil.copyfile(path, destination / 'Tk-license.terms')


def source_package(root, out):
    """Copy source inputs only; no upstream checkout or generated game files."""
    source_hashes = {}
    for path in payload_files(root):
        relative = path.relative_to(root)
        target = out / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        source_hashes[relative.as_posix()] = sha256(target)
    number = int(re.search(r'^#define BUILD (\d+)$', (root / 'port/main.c').read_text(), re.M)[1])
    return dict(version=2, mode='source-build', build=number,
                edition='Persona 4 USA / SLUS-21782', audio_streams=17375, movies=23,
                upstream=dict(commit=UPSTREAM_COMMIT, url=UPSTREAM_URL, tree_sha256=UPSTREAM_TREE_SHA256,
                              hash_format='persona4-upstream-tree-v1'),
                reference=json.loads((root / 'tools/build_reference.json').read_text(encoding='utf-8')),
                source_sha256=source_hashes)


def copy_toolchain(compiler, destination):
    shutil.copytree(compiler, destination)
    # BFD loads every plugin as a DLL; static archives cause Windows Bad Image dialogs.
    for plugin in (destination / 'lib/bfd-plugins').glob('*'):
        if plugin.suffix in ('.a', '.la'):
            plugin.unlink()
    info = destination / 'BUILDINFO.txt'
    if info.is_file():
        text = info.read_text(encoding='utf-8')
        # Keep compiler provenance without publishing the maintainer's install path.
        posix = compiler.as_posix()
        for prefix in (str(compiler), posix, '/' + posix[0].lower() + posix[2:]):
            text = re.sub(re.escape(prefix), '${PSPDEV}', text, flags=re.I)
        info.write_text(text, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path, help='Existing release, only with --legacy-prebuilt')
    parser.add_argument('--legacy-prebuilt', action='store_true', help='Explicitly create the older package containing EBOOT')
    parser.add_argument('--pspdev', type=Path, help='Windows PSPDEV to include in the preparation package')
    parser.add_argument('--bundle-toolchain', action='store_true', help='Include PSPDEV; by default the utility downloads it')
    parser.add_argument('--retail-elf', type=Path, default=ROOT / 'build/SLUS_217.82')
    parser.add_argument('--vgmstream', type=Path, required=True, help='Directory containing the complete Windows vgmstream CLI distribution')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--freeze', action='store_true', help='Bundle Python, Tk and PyAV into Prepare P4.exe')
    parser.add_argument('--zip', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError('Choose a new empty package directory.')
    if args.legacy_prebuilt:
        if not args.release:
            parser.error('--legacy-prebuilt requires --release')
        manifest = json.loads((args.release / 'manifest.json').read_text())
        if not manifest.get('release'): raise ValueError('Expected an XMB release build.')
        for name in ('EBOOT.PBP', 'p4data.bin', 'p4str.bin'):
            if sha256(args.release / name) != manifest['artifact_sha256'][name]:
                raise ValueError('Release artifact hash mismatch: ' + name)
        recipe = make_recipe(args.retail_elf.read_bytes(), (args.release / 'p4data.bin').read_bytes(),
                             (args.release / 'p4str.bin').read_bytes(), (args.release / 'EBOOT.PBP').read_bytes(), manifest['build'])
    elif args.release:
        parser.error('--release requires --legacy-prebuilt; source packages do not read a prebuilt release')
    if not (args.vgmstream / 'vgmstream-cli.exe').is_file():
        raise ValueError('Missing vgmstream-cli.exe.')
    # The utility downloads the recommended compiler from the toolchain release; bundle only on request.
    compiler = find_pspdev(out, args.pspdev) if args.bundle_toolchain else None
    out.mkdir(parents=True, exist_ok=True)
    if args.freeze:
        with tempfile.TemporaryDirectory(prefix='p4-freeze-') as temporary:
            temp = Path(temporary)
            subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onedir', '--windowed',
                '--name', 'Prepare P4', '--distpath', str(temp / 'dist'), '--workpath', str(temp / 'work'),
                '--specpath', str(temp), '--paths', str(ROOT / 'tools'), '--collect-all', 'av',
                '--exclude-module', 'imageio_ffmpeg', '--exclude-module', 'numpy', '--exclude-module', 'PIL',
                '--hidden-import', 'tkinter', '--hidden-import', 'tkinter.filedialog', '--hidden-import', 'tkinter.messagebox',
                '--hidden-import', 'tkinter.ttk', '--add-data', str(ROOT / 'tools/audio_vag_encoder.cs') + ';tools',
                '--hidden-import', 'runpy', '--hidden-import', 'concurrent.futures.thread',
                str(ROOT / 'tools/psp_prepare.py')], check=True)
            shutil.copytree(temp / 'dist/Prepare P4', out, dirs_exist_ok=True)
    else:
        # Developer package: public packages should use --freeze.
        for name in ('psp_prepare.py', 'psp_audio.py', 'psp_video.py', 'psp_disc.py', 'psp_data_census.py',
                     'psp_build.py', 'psp_xmb.py', 'extract_elf.py', 'audio_vag_encoder.cs'):
            target = out / 'tools' / name
            target.parent.mkdir(exist_ok=True); shutil.copyfile(ROOT / 'tools' / name, target)
    if args.legacy_prebuilt:
        (out / 'P4').mkdir()
        shutil.copyfile(args.release / 'EBOOT.PBP', out / 'P4/EBOOT.PBP')
    else:
        recipe = source_package(ROOT, out)
    if compiler is not None:
        copy_toolchain(compiler, out / 'toolchain/pspdev')
    (out / 'prepare.json').write_text(json.dumps(recipe, separators=(',', ':')) + '\n', encoding='utf-8')
    helpers = out / 'helpers'; helpers.mkdir()
    from psp_audio import build_encoder
    shutil.copyfile(build_encoder(), helpers / 'audio_vag_encoder.exe')
    shutil.copytree(args.vgmstream, helpers / 'vgmstream')
    notices = out / 'licenses'
    copy_licenses(notices)
    shutil.copyfile(ROOT / 'docs/prepare_game.md', out / 'README.md')
    shutil.copyfile(ROOT / 'LICENSE', out / 'LICENSE')
    shutil.copyfile(ROOT / 'THIRD_PARTY.md', out / 'PROJECT_NOTICES.md')
    (out / 'THIRD_PARTY.md').write_text(
        'This package includes Python, Tcl/Tk, PyInstaller runtime, PyAV (with its FFmpeg libraries) and vgmstream.\n'
        'The FFmpeg program is not included: the utility downloads the gyan.dev FFmpeg 9.0.1 essentials build '
        'from https://github.com/GyanD/codexffmpeg/releases/tag/9.0.1 on first use.\n'
        'License texts are in licenses/ and helpers/vgmstream/COPYING.\n'
        'The PSP compiler (PSPDEV/PSPSDK) is not included: the utility downloads it on first build from '
        'https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2, '
        'where its corresponding source is published next to it.\n'
        'Upstream projects and source: https://www.python.org/ https://www.tcl.tk/ '
        'https://pyinstaller.org/ https://github.com/PyAV-Org/PyAV '
        'https://ffmpeg.org/ https://github.com/vgmstream/vgmstream '
        'https://github.com/pspdev/pspdev https://github.com/pspdev/pspsdk\n'
        'Before public redistribution, supply the corresponding source and notices required '
        'by the exact bundled third-party binaries.\n', encoding='utf-8')
    files = {p.relative_to(out).as_posix(): sha256(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'package.json').write_text(json.dumps(dict(build=recipe['build'], mode=recipe.get('mode', 'prebuilt'), files=files), indent=2) + '\n')
    if args.zip:
        print(shutil.make_archive(str(out), 'zip', out.parent, out.name))
    print(f'Preparation package ready: {out}', flush=True)


if __name__ == '__main__':
    main()
