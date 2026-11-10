#!/usr/bin/env python
"""Build the isolated PSP movie probe. Does not rebuild or deploy the game.

PSPDEV=... python tools/psp_video_probe.py
Copy build/video_probe/EBOOT.PBP and VIDEO/ to ms0:/PSP/GAME/P4VIDEO/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from psp_link import run, check_import_ranges

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=ROOT / 'build/video_probe')
    args = parser.parse_args()
    dev = Path(os.environ['PSPDEV'])
    sdk = dev / 'psp/sdk'
    binpath = dev / 'bin'
    with tempfile.TemporaryDirectory(prefix='p4-video-build-') as temporary:
        work = Path(temporary)
        flags = ['-O2', '-G0', '-Wall', '-Wextra', '-Werror', '-D_PSP_FW_VERSION=600', f'-I{sdk / "include"}']
        for source, name in [('video_probe.c', 'probe'), ('video.c', 'video'), ('video_nal.S', 'nal')]:
            run(binpath / 'psp-gcc.exe', *flags, '-c', ROOT / 'port' / source, '-o', work / (name + '.o'))
        elf = work / 'probe.elf'
        run(binpath / 'psp-gcc.exe', '-G0', f'-L{sdk / "lib"}', '-Wl,-zmax-page-size=128',
            f'-specs={sdk / "lib/prxspecs"}', f'-Wl,-q,-T{sdk / "lib/linkfile.prx"}',
            work / 'probe.o', work / 'video.o', work / 'nal.o', sdk / 'lib/prxexports.o',
            '-lpspmpeg', '-lpspdmac', '-Wl,-u,sceUtilityGetSystemParamInt', '-lpsputility', '-lpspaudio', '-lpspdisplay', '-lpspctrl',
            '-lpspdebug', '-lpspge', '-lpsppower', '-o', elf)
        run(binpath / 'psp-fixup-imports.exe', elf)
        check_import_ranges(elf)
        run(binpath / 'psp-prxgen.exe', elf, work / 'video.prx')
        run(binpath / 'mksfoex.exe' if (binpath / 'mksfoex.exe').exists() else binpath / 'mksfo.exe',
            'P4 Video Probe', work / 'PARAM.SFO')
        run(binpath / 'pack-pbp.exe', work / 'EBOOT.PBP', work / 'PARAM.SFO',
            'NULL', 'NULL', 'NULL', 'NULL', 'NULL', work / 'video.prx', 'NULL')
        args.out.mkdir(parents=True, exist_ok=True)
        for name in ['probe.elf', 'video.prx', 'EBOOT.PBP']:
            shutil.copyfile(work / name, args.out / name)
        manifest = {
            'source_sha256': {name: hashlib.sha256((ROOT / 'port' / name).read_bytes()).hexdigest()
                              for name in ['video_probe.c', 'video.c', 'video.h', 'video_nal.S']},
            'artifact_sha256': {name: hashlib.sha256((args.out / name).read_bytes()).hexdigest()
                                for name in ['probe.elf', 'video.prx', 'EBOOT.PBP']}}
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(args.out.resolve())


if __name__ == '__main__':
    main()
