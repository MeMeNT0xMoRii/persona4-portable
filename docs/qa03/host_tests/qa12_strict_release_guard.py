#!/usr/bin/env python3
"""QA12 read-only, fail-closed release/update verifier for an isolated candidate.

External expected-build, trusted build manifest digest and original-source movie
stems are independent inputs. This gate alone cannot attest true PSP MIPS code.
Use ONLY on a freshly staged candidate, never mutate an installed PSP directory.
"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

TRIPLET = {'EBOOT.PBP', 'p4data.bin', 'p4str.bin'}
SHA = set('0123456789abcdef')


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for part in iter(lambda: f.read(1 << 20), b''):
            h.update(part)
    return h.hexdigest()


def load_json(path):
    def unique(pairs):
        output = {}
        for key, value in pairs:
            if key in output:
                raise ValueError('duplicate JSON key: ' + key)
            output[key] = value
        return output
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)


def safe_key(key):
    return isinstance(key, str) and bool(key) and '\\' not in key and ':' not in key and '\x00' not in key and all(
        part and part not in ('.', '..') for part in key.split('/')) and not key.startswith('/')


def verify_reference(path, expected_digest, release):
    """Require a pre-frozen independent SHA and a location outside release tree."""
    path, release = Path(path), Path(release)
    if path.resolve().is_relative_to(release.resolve()):
        return ['TRUST_REFERENCE_INSIDE_RELEASE_TREE']
    if not isinstance(expected_digest, str) or len(expected_digest) != 64 or set(expected_digest) - SHA:
        return ['BAD_TRUST_DIGEST']
    if not path.is_file() or sha256(path) != expected_digest:
        return ['TRUST_REFERENCE_SHA_MISMATCH']
    return []


def inspect(release, trusted_manifest, manifest_digest, expected_build, movie_stems):
    """Return a list of failure reasons; empty list only after all independent gates."""
    issues = []
    release = Path(release)
    trusted_manifest = Path(trusted_manifest)
    issues.extend(verify_reference(trusted_manifest, manifest_digest, release))
    if issues:
        return issues
    try:
        build = load_json(trusted_manifest)
        data = load_json(release / 'release.json')
    except (OSError, ValueError, UnicodeError) as err:
        return ['INVALID_JSON:' + type(err).__name__ + ':' + str(err)[:80]]
    if not isinstance(data, dict) or not isinstance(build, dict):
        return ['MANIFEST_NOT_OBJECT']
    if data.get('release') is not True or build.get('release') is not True:
        issues.append('NOT_RELEASE')
    if type(expected_build) is not int or data.get('build') != expected_build or build.get('build') != expected_build:
        issues.append('WRONG_BUILD')
    if data.get('update_root') != 'UPDATE/PSP/GAME/P4':
        issues.append('BAD_UPDATE_ROOT')
    base = release / 'UPDATE/PSP/GAME/P4'
    for parent in [release, release / 'UPDATE', release / 'UPDATE/PSP', release / 'UPDATE/PSP/GAME', base]:
        if parent.is_symlink():
            issues.append('SYMLINK_IN_ROOT:' + str(parent))
    if not base.is_dir():
        return issues + ['MISSING_UPDATE_ROOT']
    files = data.get('files')
    build_files = build.get('artifact_sha256')
    if not isinstance(files, dict) or not isinstance(build_files, dict):
        return issues + ['MISSING_HASH_MAP']
    required = TRIPLET | {'SOUND/audio_manifest.json'}
    if not required <= files.keys():
        issues.append('MISSING_REQUIRED:' + ','.join(sorted(required - files.keys())))
    actual = set()
    for p in base.rglob('*'):
        key = p.relative_to(base).as_posix()
        if p.is_symlink():
            issues.append('SYMLINK:' + key)
        elif p.is_file():
            actual.add(key)
    if set(files) != actual:
        issues += ['UNDECLARED:' + name for name in sorted(actual - set(files))]
        issues += ['MISSING:' + name for name in sorted(set(files) - actual)]
    total = 0
    for key, claimed in files.items():
        if not safe_key(key):
            issues.append('UNSAFE_KEY:' + str(key)[:80])
            continue
        if not isinstance(claimed, str) or len(claimed) != 64 or set(claimed) - SHA:
            issues.append('BAD_SHA:' + key)
            continue
        p = base.joinpath(*key.split('/'))
        if p.is_symlink() or not p.is_file():
            continue
        total += p.stat().st_size
        if sha256(p) != claimed:
            issues.append('HASH_MISMATCH:' + key)
        if key in TRIPLET and build_files.get(key) != claimed:
            issues.append('INDEPENDENT_TRIPLET_MISMATCH:' + key)
    if data.get('bytes') != total:
        issues.append('BYTES_MISMATCH')
    if len(set(movie_stems)) != len(movie_stems) or len(movie_stems) != 23:
        issues.append('UNTRUSTED_MOVIE_CATALOGUE')
    if data.get('movies') != 23:
        issues.append('MOVIE_COUNT_NOT_23')
    actual_stems = [p.rsplit('/', 1)[-1][:-4].upper() for p in actual if p.startswith('VIDEO/') and p.endswith('.p4v')]
    if len(actual_stems) != 23 or len(set(actual_stems)) != 23 or set(actual_stems) != set(movie_stems):
        issues.append('MOVIE_STEM_ID_MISMATCH')
    return issues


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--release-folder', type=Path, required=True)
    p.add_argument('--trusted-manifest', type=Path, required=True, help='Independent immutable build manifest, OUTSIDE release folder')
    p.add_argument('--trusted-manifest-sha256', required=True, help='Previously frozen independent SHA-256')
    p.add_argument('--expected-build', type=int, required=True)
    p.add_argument('--original-movie-stems', type=Path, required=True, help='Source-verified JSON list of 23 filename stems')
    p.add_argument('--original-movie-stems-sha256', required=True, help='Previously frozen independent catalogue SHA-256')
    args = p.parse_args(argv)
    try:
        problems = verify_reference(args.original_movie_stems, args.original_movie_stems_sha256, args.release_folder)
        if problems:
            for issue in problems: print('NO-GO:', issue)
            return 2
        stems = load_json(args.original_movie_stems)
        if not isinstance(stems, list) or not all(isinstance(x, str) and x == x.upper() for x in stems):
            raise ValueError('movie stems must be uppercase JSON strings')
        problems = inspect(args.release_folder, args.trusted_manifest, args.trusted_manifest_sha256,
                           args.expected_build, stems)
    except (OSError, ValueError, UnicodeError) as err:
        problems = ['INVALID_TRUST_INPUT:' + str(err)]
    for issue in problems:
        print('NO-GO:', issue)
    if problems:
        return 2
    print('CANDIDATE PASS: closed-world packaging and independent manifest/catalogue match; PSP runtime NOT attested')
    return 0


if __name__ == '__main__':
    sys.exit(main())
