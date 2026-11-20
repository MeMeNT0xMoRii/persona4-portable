#!/usr/bin/env python3
"""Adversarial fixtures against unmodified QA11: report unexpected false passes.

Standalone host test. Does not change repository runtime or user files.
"""
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path

SRC = Path(__file__).with_name('qa11_release_update_guard.py')
spec = importlib.util.spec_from_file_location('actual_qa11', SRC)
qa11 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qa11)


def fixture(parent):
    release = parent/'release'
    base = release/'UPDATE/PSP/GAME/P4'
    base.mkdir(parents=True)
    contents = {'EBOOT.PBP': b'fake eboot', 'p4data.bin': b'fake data', 'p4str.bin': b'fake strings'}
    hashes = {}
    for n,b in contents.items():
        (base/n).write_bytes(b)
        hashes[n]=hashlib.sha256(b).hexdigest()
    manifest = {'release': True, 'build': 249, 'update_root':'UPDATE/PSP/GAME/P4',
                'files':hashes,'bytes':sum(map(len,contents.values())),'movies':0,'audio_streams':0}
    (release/'release.json').write_text(json.dumps(manifest))
    return release, base, manifest


def check(label, mutate, expected_bad=True):
    with tempfile.TemporaryDirectory(prefix='p4pqa12-') as t:
        release,base,m = fixture(Path(t))
        mutate(release,base,m)
        (release/'release.json').write_text(json.dumps(m))
        result=qa11.check(release)
        false_pass=(expected_bad and not result)
        print(f'{label}:', 'FALSE_PASS' if false_pass else ('REJECTED '+','.join(result) if result else 'PASS'))
        return false_pass


def main():
    cases={
      'missing mandatory EBOOT removed from manifest': lambda r,b,m:(m['files'].pop('EBOOT.PBP'),(b/'EBOOT.PBP').unlink()),
      'missing mandatory p4str removed from manifest': lambda r,b,m:(m['files'].pop('p4str.bin'),(b/'p4str.bin').unlink()),
      'manifest build field from other profile':lambda r,b,m:m.update(build=999),
      'manifest bytes understated':lambda r,b,m:m.update(bytes=1),
      'zero movie count accepted regardless release contract':lambda r,b,m:m.update(movies=0),
      'no SOUND/audio manifest accepted':lambda r,b,m:None,
      'EBOOT content+manifest modified together':lambda r,b,m:((b/'EBOOT.PBP').write_bytes(b'altered'),m['files'].update({'EBOOT.PBP':hashlib.sha256(b'altered').hexdigest()})),
      'unlisted obsolete payload':lambda r,b,m:((b/'VIDEO').mkdir(),(b/'VIDEO/OLD.p4v').write_bytes(b'old')),
      'changed EBOOT without manifest':lambda r,b,m:(b/'EBOOT.PBP').write_bytes(b'tampered'),
    }
    fails={k:check(k,v) for k,v in cases.items()}
    missed=[k for k,v in fails.items() if v]
    print('false passes',len(missed),'/',len(cases),'note some require trusted reference inputs to catch')
    for k in missed:print(' -',k)
    if len(missed) < 6: raise RuntimeError('expected several specific QA11 policy gaps')
    return 0

if __name__=='__main__':raise SystemExit(main())
