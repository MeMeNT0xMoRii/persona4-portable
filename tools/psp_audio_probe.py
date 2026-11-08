#!/usr/bin/env python
"""Build the real audio service with an isolated streaming/effect test entry."""
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
    parser.add_argument('--out', type=Path, default=ROOT / 'build/audio_probe')
    args = parser.parse_args()
    dev = Path(os.environ['PSPDEV'])
    sdk, binaries = dev / 'psp/sdk', dev / 'bin'
    with tempfile.TemporaryDirectory(prefix='p4-audio-build-') as temp:
        work = Path(temp)
        for name in ['audio', 'audio_probe']:
            run(binaries / 'psp-gcc.exe', '-O2', '-G0', '-Wall', '-Wextra', '-Werror',
                '-D_PSP_FW_VERSION=600', f'-I{sdk / "include"}', '-c', ROOT / 'port' / (name + '.c'),
                '-o', work / (name + '.o'))
        elf = work / 'probe.elf'
        run(binaries / 'psp-gcc.exe', '-G0', f'-L{sdk / "lib"}', '-Wl,-zmax-page-size=128',
            f'-specs={sdk / "lib/prxspecs"}', f'-Wl,-q,-T{sdk / "lib/linkfile.prx"}',
            work / 'audio_probe.o', work / 'audio.o', sdk / 'lib/prxexports.o', '-lpspsascore',
            '-Wl,-u,sceUtilityGetSystemParamInt', '-lpsputility', '-lpspaudio', '-lpspdisplay',
            '-lpspdebug', '-lpspge', '-lpsppower', '-lm', '-o', elf)
        run(binaries / 'psp-fixup-imports.exe', elf)
        check_import_ranges(elf)
        run(binaries / 'psp-prxgen.exe', elf, work / 'audio.prx')
        args.out.mkdir(parents=True, exist_ok=True)
        for name in ['probe.elf', 'audio.prx']:
            shutil.copyfile(work / name, args.out / name)
        manifest = {'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in [ROOT / 'port/audio.c', ROOT / 'port/audio.h', ROOT / 'port/audio_probe.c']},
                    'artifact_sha256': {name: hashlib.sha256((args.out / name).read_bytes()).hexdigest()
                                       for name in ['probe.elf', 'audio.prx']}}
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(args.out)


if __name__ == '__main__':
    main()
