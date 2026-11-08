#!/usr/bin/env python
"""Build a public Windows preparation package, excluding original game assets.

python tools/psp_prepare_release.py --release build/release_b164 --vgmstream C:/tools/vgmstream --freeze --out C:/temp/P4-PSP-b164

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release', type=Path, required=True)
    parser.add_argument('--retail-elf', type=Path, default=ROOT / 'build/SLUS_217.82')
    parser.add_argument('--vgmstream', type=Path, required=True, help='Directory containing the complete Windows vgmstream CLI distribution')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--freeze', action='store_true', help='Bundle Python, Tk and PyAV into Prepare P4.exe')
    parser.add_argument('--zip', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        raise ValueError('Choose a new empty package directory.')
    manifest = json.loads((args.release / 'manifest.json').read_text())
    if not manifest.get('release'): raise ValueError('Expected an XMB release build.')
    for name in ('EBOOT.PBP', 'p4data.bin', 'p4str.bin'):
        if sha256(args.release / name) != manifest['artifact_sha256'][name]:
            raise ValueError('Release artifact hash mismatch: ' + name)
    recipe = make_recipe(args.retail_elf.read_bytes(), (args.release / 'p4data.bin').read_bytes(),
                         (args.release / 'p4str.bin').read_bytes(), (args.release / 'EBOOT.PBP').read_bytes(), manifest['build'])
    if not (args.vgmstream / 'vgmstream-cli.exe').is_file():
        raise ValueError('Missing vgmstream-cli.exe.')
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
                str(ROOT / 'tools/psp_prepare.py')], check=True)
            shutil.copytree(temp / 'dist/Prepare P4', out, dirs_exist_ok=True)
    else:
        # Developer package: public packages should use --freeze.
        for name in ('psp_prepare.py', 'psp_audio.py', 'psp_video.py', 'psp_disc.py', 'psp_data_census.py', 'audio_vag_encoder.cs'):
            target = out / 'tools' / name
            target.parent.mkdir(exist_ok=True); shutil.copyfile(ROOT / 'tools' / name, target)
    (out / 'P4').mkdir()
    shutil.copyfile(args.release / 'EBOOT.PBP', out / 'P4/EBOOT.PBP')
    (out / 'prepare.json').write_text(json.dumps(recipe, separators=(',', ':')) + '\n', encoding='utf-8')
    helpers = out / 'helpers'; helpers.mkdir()
    from psp_audio import build_encoder
    from psp_video import ffmpeg_path
    shutil.copyfile(build_encoder(), helpers / 'audio_vag_encoder.exe')
    shutil.copyfile(ffmpeg_path(), helpers / 'ffmpeg.exe')
    shutil.copytree(args.vgmstream, helpers / 'vgmstream')
    notices = out / 'licenses'
    copy_licenses(notices)
    license_info = subprocess.run([str(helpers / 'ffmpeg.exe'), '-L'], capture_output=True, text=True, check=True)
    (notices / 'FFmpeg.txt').write_text(license_info.stdout + license_info.stderr, encoding='utf-8')
    shutil.copyfile(ROOT / 'docs/prepare_game.md', out / 'README.md')
    shutil.copyfile(ROOT / 'LICENSE', out / 'LICENSE')
    shutil.copyfile(ROOT / 'THIRD_PARTY.md', out / 'PROJECT_NOTICES.md')
    (out / 'THIRD_PARTY.md').write_text(
        'This package includes Python, Tcl/Tk, PyInstaller runtime, PyAV, FFmpeg and vgmstream.\n'
        'License texts are in licenses/ and helpers/vgmstream/COPYING.\n'
        'Upstream projects and source: https://www.python.org/ https://www.tcl.tk/ '
        'https://pyinstaller.org/ https://github.com/PyAV-Org/PyAV '
        'https://ffmpeg.org/ https://github.com/vgmstream/vgmstream\n'
        'FFmpeg binary provider: imageio-ffmpeg (https://github.com/imageio/imageio-ffmpeg).\n'
        'Before public redistribution, supply the corresponding source and notices required '
        'by the exact bundled third-party binaries.\n', encoding='utf-8')
    files = {p.relative_to(out).as_posix(): sha256(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out / 'package.json').write_text(json.dumps(dict(build=manifest['build'], files=files), indent=2) + '\n')
    if args.zip:
        print(shutil.make_archive(str(out), 'zip', out.parent, out.name))
    print(f'Preparation package ready: {out}', flush=True)


if __name__ == '__main__':
    main()
