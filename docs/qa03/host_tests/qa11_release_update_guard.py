#!/usr/bin/env python3
"""Read-only closed-world integrity check for psp_release.py UPDATE artifacts.

Checks declared release.json hashes AND rejects any undeclared regular file in
UPDATE/PSP/GAME/P4. Does not inspect private ISO or certify a PSP build.
"""
import argparse
import hashlib
import json
import tempfile
from pathlib import Path, PurePosixPath


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def check(folder):
    folder = Path(folder)
    data = json.loads((folder / 'release.json').read_text(encoding='utf-8'))
    if data.get('release') is not True:
        return ['NOT_RELEASE']
    if data.get('update_root') != 'UPDATE/PSP/GAME/P4':
        return ['UNEXPECTED_UPDATE_ROOT']
    base = folder / 'UPDATE/PSP/GAME/P4'
    if not base.is_dir():
        return ['MISSING_UPDATE_ROOT']
    expected = data.get('files')
    if not isinstance(expected, dict) or not expected:
        return ['MISSING_FILE_MAP']
    issues = []
    for key, expected_hash in sorted(expected.items()):
        p = PurePosixPath(key)
        if not key or p.is_absolute() or any(x in ('', '.', '..') for x in key.split('/')) or '\\' in key:
            issues.append('UNSAFE_FILE_KEY:' + key)
            continue
        if not isinstance(expected_hash, str) or len(expected_hash) != 64 or any(c not in '0123456789abcdef' for c in expected_hash):
            issues.append('BAD_SHA256:' + key)
            continue
        file = base.joinpath(*p.parts)
        if not file.is_file() or file.is_symlink():
            issues.append('MISSING_OR_LINK:' + key)
        elif digest(file) != expected_hash:
            issues.append('HASH_MISMATCH:' + key)
    actual = set()
    for p in base.rglob('*'):
        if p.is_symlink():
            issues.append('SYMLINK:' + p.relative_to(base).as_posix())
        elif p.is_file():
            actual.add(p.relative_to(base).as_posix())
    for key in sorted(actual - set(expected)):
        issues.append('UNDECLARED:' + key)
    videos = sum(1 for key in actual if key.startswith('VIDEO/') and key.endswith('.p4v'))
    if videos != data.get('movies'):
        issues.append('MOVIE_COUNT_MISMATCH:' + str(videos))
    return issues


def self_test():
    def fixture(root):
        out = root / 'release'
        base = out / 'UPDATE/PSP/GAME/P4'
        base.mkdir(parents=True)
        files = {'EBOOT.PBP': b'code', 'p4data.bin': b'data', 'p4str.bin': b'str'}
        for name, content in files.items():
            (base / name).write_bytes(content)
        manifest = dict(release=True, update_root='UPDATE/PSP/GAME/P4', movies=0,
                        files={name: hashlib.sha256(content).hexdigest() for name, content in files.items()})
        (out / 'release.json').write_text(json.dumps(manifest), encoding='utf-8')
        return out, base, manifest

    with tempfile.TemporaryDirectory(prefix='p4p-qa11-') as tmp:
        root = Path(tmp)
        out, base, manifest = fixture(root)
        assert check(out) == [], 'clean case'
        (base / 'VIDEO').mkdir()
        (base / 'VIDEO/RETIRED.p4v').write_bytes(b'old')
        assert any(x.startswith('UNDECLARED:') for x in check(out)), 'stale movie must fail'
        (base / 'VIDEO/RETIRED.p4v').unlink()
        (base / 'EBOOT.PBP').write_bytes(b'mutated')
        assert 'HASH_MISMATCH:EBOOT.PBP' in check(out), 'changed binary must fail'
        (base / 'EBOOT.PBP').write_bytes(b'code')
        (base / 'p4str.bin').unlink()
        assert 'MISSING_OR_LINK:p4str.bin' in check(out), 'absent file must fail'
        (base / 'p4str.bin').write_bytes(b'str')
        manifest['files']['../unsafe.bin'] = hashlib.sha256(b'x').hexdigest()
        (out / 'release.json').write_text(json.dumps(manifest), encoding='utf-8')
        assert any(x.startswith('UNSAFE_FILE_KEY:') for x in check(out)), 'traversal must fail'
        print('QA11 5/5 synthetic closed-world checks PASS (normal)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-folder', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.release_folder:
        parser.error('--release-folder is required without --self-test')
    issues = check(args.release_folder)
    for issue in issues:
        print('NO-GO', issue)
    if issues:
        return 2
    print('PASS: UPDATE files match release.json exactly (not PSP/T2/T3 certification)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
