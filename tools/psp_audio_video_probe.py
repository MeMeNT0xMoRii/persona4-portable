"""Build the real SAS/AVC services with a long-session regression entry.

After PSPLink reset: ldstart host0:/avtest/probe.prx 660 2
Uses the existing ms0:/PSP/GAME/P4/{SOUND,VIDEO}; ignores NOSOUND.
Writes ms0:/PSP/GAME/P4/avtest.log. Does not deploy or start anything.
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
    parser.add_argument('--out', type=Path, default=ROOT / 'build/audio_video_probe')
    args = parser.parse_args()
    dev = Path(os.environ['PSPDEV'])
    sdk, binaries = dev / 'psp/sdk', dev / 'bin'
    sources = ['audio_video_probe.c', 'audio.c', 'video.c', 'video_nal.S']
    with tempfile.TemporaryDirectory(prefix='p4-avtest-') as temp:
        work = Path(temp)
        objects = []
        for name in sources:
            obj = work / (Path(name).stem + '.o')
            run(binaries / 'psp-gcc.exe', '-O2', '-G0', '-Wall', '-Wextra', '-Werror',
                '-D_PSP_FW_VERSION=600', f'-I{sdk / "include"}', '-c', ROOT / 'port' / name, '-o', obj)
            objects.append(obj)
        elf = work / 'probe.elf'
        run(binaries / 'psp-gcc.exe', '-G0', f'-L{sdk / "lib"}', '-Wl,-zmax-page-size=128',
            f'-specs={sdk / "lib/prxspecs"}', f'-Wl,-q,-T{sdk / "lib/linkfile.prx"}',
            *objects, sdk / 'lib/prxexports.o', '-lpspmpeg', '-lpspdmac', '-lpspsascore',
            '-Wl,-u,sceUtilityGetSystemParamInt', '-lpsputility', '-lpspaudio', '-lpspdisplay',
            '-lpspctrl', '-lpspdebug', '-lpspge', '-lpsppower', '-lm', '-o', elf)
        run(binaries / 'psp-fixup-imports.exe', elf)
        check_import_ranges(elf)
        run(binaries / 'psp-prxgen.exe', elf, work / 'probe.prx')
        args.out.mkdir(parents=True, exist_ok=True)
        for name in ['probe.elf', 'probe.prx']:
            shutil.copyfile(work / name, args.out / name)
        manifest = {
            'source_sha256': {name: hashlib.sha256((ROOT / 'port' / name).read_bytes()).hexdigest()
                              for name in sources + ['audio.h', 'video.h']},
            'artifact_sha256': {name: hashlib.sha256((args.out / name).read_bytes()).hexdigest()
                                for name in ['probe.elf', 'probe.prx']}}
        (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(args.out)


if __name__ == '__main__':
    main()
