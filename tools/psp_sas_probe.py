#!/usr/bin/env python
"""Build the isolated SAS benchmark; does not rebuild or deploy the game.

Copy EBOOT.PBP to ms0:/PSP/GAME/P4SAS/ and launch it. Results: P4SAS/sas.log.
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
    parser.add_argument('--out', type=Path, default=ROOT / 'build/sas_probe')
    args = parser.parse_args()
    dev = Path(os.environ['PSPDEV'])
    sdk, binaries = dev / 'psp/sdk', dev / 'bin'
    source = ROOT / 'port/sas_probe.c'
    with tempfile.TemporaryDirectory(prefix='p4-sas-build-') as temporary:
        work = Path(temporary)
        run(binaries / 'psp-gcc.exe', '-O2', '-G0', '-Wall', '-Wextra', '-Werror',
            '-D_PSP_FW_VERSION=600', f'-I{sdk / "include"}', '-c', source, '-o', work / 'probe.o')
        elf = work / 'probe.elf'
        run(binaries / 'psp-gcc.exe', '-G0', f'-L{sdk / "lib"}', '-Wl,-zmax-page-size=128',
            f'-specs={sdk / "lib/prxspecs"}', f'-Wl,-q,-T{sdk / "lib/linkfile.prx"}',
            work / 'probe.o', sdk / 'lib/prxexports.o', '-lpspsascore',
            '-Wl,-u,sceUtilityGetSystemParamInt', '-lpsputility', '-lpspaudio',
            '-lpspdisplay', '-lpspdebug', '-lpspge', '-lpsppower', '-o', elf)
        run(binaries / 'psp-fixup-imports.exe', elf)
        check_import_ranges(elf)
        run(binaries / 'psp-prxgen.exe', elf, work / 'sas.prx')
        run(binaries / 'mksfoex.exe' if (binaries / 'mksfoex.exe').exists() else binaries / 'mksfo.exe',
            'P4 SAS Probe', work / 'PARAM.SFO')
        run(binaries / 'pack-pbp.exe', work / 'EBOOT.PBP', work / 'PARAM.SFO',
            'NULL', 'NULL', 'NULL', 'NULL', 'NULL', work / 'sas.prx', 'NULL')
        args.out.mkdir(parents=True, exist_ok=True)
        artifacts = ['probe.elf', 'sas.prx', 'EBOOT.PBP']
        for name in artifacts:
            shutil.copyfile(work / name, args.out / name)
        manifest = {
            'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in [source, Path(__file__).resolve()]},
            'artifact_sha256': {name: hashlib.sha256((args.out / name).read_bytes()).hexdigest()
                                for name in artifacts}}
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(args.out.resolve())


if __name__ == '__main__':
    main()
