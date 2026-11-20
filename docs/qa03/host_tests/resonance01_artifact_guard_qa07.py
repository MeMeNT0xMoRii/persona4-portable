#!/usr/bin/env python3
"""Read-only QA07 guard for a built Persona4-PORTABLE release (stdlib only).

Run INSIDE a disposable experiment checkout, not against original perf06:
  python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py \
      --root . --manifest build/RESONANCE01-regular/manifest.json \
      --expected-profile regular --strict-provenance
  python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py --self-test

The frozen b249 builder does NOT emit a profile/provenance block: regular
and diag-lite with --strict-provenance intentionally FAIL until Codex adds it.
This checks file consistency, NOT native PSP validity or actually selected ELF
symbol owners, CPU/GE safety, private ISO/asset content, or physical Go speed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile

REQUIRED = ('p4.elf', 'p4.prx', 'EBOOT.PBP', 'p4data.bin', 'p4str.bin')
PROVENANCE = ('port_commit', 'decomp_commit', 'pspsdk_commit',
              'toolchain_id', 'compile_flags', 'link_flags', 'builder_hashes')
EXCLUDED = ('src__cri__', 'src__renderware__', 'src__rw__')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_file(base, subpath):
    """Reject absolute/escaping paths in untrusted manifests."""
    rel = Path(subpath)
    if rel.is_absolute() or '..' in rel.parts or '\\' in subpath:
        raise ValueError('unsafe relative path: ' + subpath)
    dest = (base / rel).resolve()
    if not dest.is_relative_to(base.resolve()):
        raise ValueError('escaped root: ' + subpath)
    return dest


def pbp_prx(pbp):
    if len(pbp) < 40 or pbp[:4] != b'\x00PBP':
        raise ValueError('EBOOT is not a PBP header')
    starts = struct.unpack_from('<8I', pbp, 8)
    if starts[0] < 40 or list(starts) != sorted(starts):
        raise ValueError('invalid PBP section offsets')
    if starts[7] > len(pbp) or starts[6] == starts[7]:
        raise ValueError('empty/out-of-range DATA.PSP section')
    return pbp[starts[6]:starts[7]]


def check(root, manifest, expected_profile='legacy', strict_provenance=False):
    root, manifest = Path(root).resolve(), Path(manifest).resolve()
    errors, warnings = [], []
    try:
        if not manifest.is_relative_to(root):
            raise ValueError('manifest outside project workspace')
        meta = json.loads(manifest.read_text(encoding='utf-8'))
    except (OSError, ValueError) as err:
        return ['cannot load project manifest: ' + str(err)], warnings
    out = manifest.parent
    artifacts = meta.get('artifact_sha256', {})
    for name in REQUIRED:
        if name not in artifacts:
            errors.append('missing artifact hash: ' + name)
            continue
        try:
            file = safe_file(out, name)
            if digest(file.read_bytes()) != artifacts[name]:
                errors.append('artifact hash mismatch: ' + name)
        except (ValueError, OSError) as err:
            errors.append('artifact unavailable: ' + name + ': ' + str(err))
    if all((out / x).is_file() for x in ('p4.prx', 'EBOOT.PBP')):
        try:
            if pbp_prx((out / 'EBOOT.PBP').read_bytes()) != (out / 'p4.prx').read_bytes():
                errors.append('EBOOT embedded DATA.PSP differs from p4.prx')
        except (OSError, ValueError) as err:
            errors.append('invalid PBP/PRX embedding: ' + str(err))
    for name, expected in meta.get('source_sha256', {}).items():
        try:
            if digest(safe_file(root, name).read_bytes()) != expected:
                errors.append('source changed after build: ' + name)
        except (OSError, ValueError) as err:
            errors.append('missing or unsafe source: ' + name + ': ' + str(err))
    object_hashes = meta.get('object_sha256', {})
    stage = root / 'build' / 'obj'
    listed = root / 'build' / 'psp' / 'objects.rsp'
    try:
        rows = [line.strip().strip('"').replace('\\', '/').rsplit('/', 1)[-1]
                for line in listed.read_text(encoding='utf-8').splitlines() if line.strip()]
        if len(rows) != len(set(rows)):
            errors.append('duplicate object basenames in objects.rsp')
        if set(rows) != set(object_hashes):
            errors.append('objects.rsp and manifest object set mismatch')
        actual = {p.name for p in stage.glob('*.o')
                  if not p.name.startswith(EXCLUDED)}
        if actual != set(rows):
            errors.append('active build/obj inventory differs from link response file')
        for name, h in object_hashes.items():
            if Path(name).name != name or not name.endswith('.o'):
                errors.append('unsafe object basename: ' + name)
                continue
            try:
                if digest((stage / name).read_bytes()) != h:
                    errors.append('linked game object hash mismatch: ' + name)
            except OSError as err:
                errors.append('missing linked game object ' + name + ': ' + str(err))
    except OSError as err:
        errors.append('missing original object response file: ' + str(err))
    if expected_profile != 'legacy':
        if meta.get('profile') != expected_profile:
            errors.append('actual profile field missing/mismatch: expected ' + expected_profile)
        if expected_profile in ('regular', 'diag-lite') and (
                not meta.get('release') or meta.get('frame_timing') or meta.get('ge_dump')):
            errors.append('profile incorrectly uses serial/timing/nonrelease build')
    else:
        warnings.append('legacy mode cannot attest the regular/diag-lite profile')
    prov = meta.get('provenance', {})
    missing = [x for x in PROVENANCE if not prov.get(x)]
    if missing:
        line = 'incomplete reproducibility provenance: ' + ','.join(missing)
        (errors if strict_provenance else warnings).append(line)
    warnings.append('No native ELF symbol, runtime in-game ID, GE or PSP-Go verification')
    return errors, warnings


def synthetic_pbp(prx):
    return b'\x00PBP' + struct.pack('<I', 0x00010000) + struct.pack(
        '<8I', 40, 40, 40, 40, 40, 40, 40, 40 + len(prx)) + prx


def fixture(root):
    out = root / 'build' / 'RESONANCE01-diag-lite'
    obj = root / 'build' / 'obj'
    obj.mkdir(parents=True)
    (root / 'build' / 'psp').mkdir(parents=True)
    (root / 'port').mkdir()
    out.mkdir()
    (root / 'port' / 'main.c').write_text('dummy native code', encoding='utf-8')
    files = {'p4.elf': b'ELF-mock', 'p4.prx': b'PRX-mock',
             'p4data.bin': b'game data mock', 'p4str.bin': b'string table mock'}
    files['EBOOT.PBP'] = synthetic_pbp(files['p4.prx'])
    for name, blob in files.items():
        (out / name).write_bytes(blob)
    (obj / 'src__battle__demo.o').write_bytes(b'unmodified obj')
    (root / 'build' / 'psp' / 'objects.rsp').write_text('"build/obj/src__battle__demo.o"\n')
    meta = {'profile': 'diag-lite', 'release': True, 'frame_timing': False,
            'ge_dump': False, 'artifact_sha256': {k: digest(v) for k, v in files.items()},
            'source_sha256': {'port/main.c': digest((root / 'port' / 'main.c').read_bytes())},
            'object_sha256': {'src__battle__demo.o': digest(b'unmodified obj')},
            'provenance': {key: 'test-pin' for key in PROVENANCE}}
    f = out / 'manifest.json'
    f.write_text(json.dumps(meta), encoding='utf-8')
    return f


def self_test():
    successes = 0
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        f = fixture(root)
        def valid(label, want_fail=False, why=None):
            nonlocal successes
            errs, _ = check(root, f, 'diag-lite', True)
            ok = ((not errs) if not want_fail else any(why in x for x in errs))
            if not ok:
                raise AssertionError(label + ': ' + repr(errs))
            successes += 1
            print('PASS %02d %s' % (successes, label))
        def reset():
            f.write_text(json.dumps(json.loads(f.read_text()), indent=2), encoding='utf-8')
        valid('matching mock artifacts and object provenance')
        p = f.parent / 'p4data.bin'
        previous = p.read_bytes(); p.write_bytes(b'changed!')
        valid('mismatched game data triplet rejected', True, 'artifact hash mismatch')
        p.write_bytes(previous)
        e = f.parent / 'EBOOT.PBP'
        previous = e.read_bytes(); e.write_bytes(synthetic_pbp(b'wrong embedded prx'))
        valid('embedded PRX mismatch detected', True, 'EBOOT embedded')
        e.write_bytes(previous)
        stale = root / 'build' / 'obj' / 'src__battle__stale.o'
        stale.write_bytes(b'old object')
        valid('stale extra compiled object detected', True, 'inventory differs')
        stale.unlink()
        meta = json.loads(f.read_text()); old = meta.pop('profile'); f.write_text(json.dumps(meta))
        valid('missing true Diag-Lite identity rejected', True, 'actual profile')
        meta['profile'] = old; f.write_text(json.dumps(meta))
        meta = json.loads(f.read_text()); meta['ge_dump'] = True; f.write_text(json.dumps(meta))
        valid('serial GE dump incorrectly called Diag-Lite rejected', True, 'serial/timing')
        meta['ge_dump'] = False; f.write_text(json.dumps(meta))
        meta = json.loads(f.read_text()); meta.pop('provenance'); f.write_text(json.dumps(meta))
        valid('missing strict provenance rejected', True, 'incomplete reproducibility')
        meta['provenance'] = {key: 'test-pin' for key in PROVENANCE}
        f.write_text(json.dumps(meta))
        (root / 'port' / 'main.c').write_text('changed after build', encoding='utf-8')
        valid('post-build source edit rejected', True, 'source changed')
    print('ALL %d QA07 synthetic manifest guard checks PASS. NO PSP code tested.' % successes)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root', type=Path, default=Path('.'))
    ap.add_argument('--manifest', type=Path)
    ap.add_argument('--expected-profile', choices=('legacy', 'regular', 'diag-lite', 'fusion-exp'),
                    default='legacy')
    ap.add_argument('--strict-provenance', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.manifest:
        ap.error('--manifest required outside --self-test')
    manifest = args.manifest if args.manifest.is_absolute() else args.root / args.manifest
    errs, warns = check(args.root, manifest, args.expected_profile, args.strict_provenance)
    for x in warns:
        print('WARN: ' + x)
    for x in errs:
        print('FAIL: ' + x)
    if errs:
        raise SystemExit(1)
    print('PASS: consistent manifest, source, game objects and PBP embedding (HOST ONLY)')


if __name__ == '__main__':
    main()
