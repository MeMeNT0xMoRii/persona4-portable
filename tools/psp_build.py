#!/usr/bin/env python
"""Build an EBOOT locally from the player's ELF and pinned upstream C sources.

The source preparation package contains no game objects, EBOOT or data recipe.
This runner also works in the frozen preparer's isolated --build-worker mode.
"""
from contextlib import contextmanager, ExitStack
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
import zipfile

UPSTREAM_COMMIT = '363eab8a966d60a9773b45f330f2344329ec4f0a'
UPSTREAM_URL = f'https://codeload.github.com/Raikaru/Persona4-Decompilation/zip/{UPSTREAM_COMMIT}'
RETAIL_SHA1 = '4eeec0360cf2715535d9f7e52eb69d786fb0158c'
BUILD_TOOLS = ('psp_census', 'psp_vu0', 'psp_data_census', 'psp_make_data',
               'psp_ptr_proof', 'psp_xref', 'psp_link', 'psp_link_census',
               'psp_reasm', 'psp_address_audit', 'psp_hw_debug', 'psp_build',
               'psp_prepare', 'extract_elf')
WORKER_SCRIPTS = {'psp_census.py', 'psp_ptr_proof.py', 'psp_link.py',
                  'psp_make_data.py', 'psp_address_audit.py', 'psp_hw_debug.py'}
TOOLS = ('psp-gcc', 'psp-nm', 'psp-as', 'psp-ld', 'psp-objcopy', 'psp-objdump',
         'psp-size', 'psp-strip', 'psp-fixup-imports', 'psp-prxgen', 'mksfoex', 'pack-pbp')
ARTIFACTS = {'EBOOT.PBP', 'p4.prx', 'p4.elf', 'p4data.bin', 'p4str.bin'}
# Hash of canonical paths and file contents, independent of ZIP packaging.
UPSTREAM_TREE_SHA256 = 'b1a5681e7b30280b558c2deb438b2815b414eee4e6a59da55e551778ead45bc0'
_cancelled = threading.Event()
_active = []
CACHE_KIND = 'persona4-psp-build-cache-v1'


def cancel_build():
    """Stop only child build workers owned by this preparer, including compilers."""
    _cancelled.set()
    for process in tuple(_active):
        if process.poll() is None:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
            else:
                process.terminate()


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            result.update(block)
    return result.hexdigest()


def save_json(path, data):
    path = Path(path)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def cache_path(cache=None):
    return Path(cache or Path(os.environ.get('LOCALAPPDATA', tempfile.gettempdir())) / 'Persona4PSP').expanduser().resolve()


def prepare_cache(cache):
    cache = Path(cache).resolve()
    if cache.parent == cache or cache in (Path.home().resolve(), Path.cwd().resolve()):
        raise ValueError('Choose a dedicated build-cache folder, not a drive, home or project root.')
    if any((cache / name).exists() for name in ('.git', 'port', 'tools', 'source', 'P4', 'PSP')):
        raise ValueError('The selected cache folder contains project or installation files. Choose a dedicated folder.')
    cache.mkdir(parents=True, exist_ok=True)
    marker = cache / 'cache.json'
    if marker.exists() and json.loads(marker.read_text()).get('kind') != CACHE_KIND:
        raise ValueError('This folder belongs to another cache format.')
    if not marker.exists():
        save_json(marker, {'kind': CACHE_KIND})
    return cache


def folder_bytes(folder):
    total = 0
    if Path(folder).exists():
        for path in Path(folder).rglob('*'):
            if path.is_file() and not path.is_symlink():
                total += path.stat().st_size
    return total


def cache_info(cache=None):
    root = cache_path(cache)
    return {'path': str(root), 'build_bytes': folder_bytes(root / 'builds'),
            'download_bytes': folder_bytes(root / 'downloads')}


def clean_cache(cache=None, include_downloads=False):
    root = cache_path(cache)
    if root.parent == root or root in (Path.home().resolve(), Path.cwd().resolve()):
        raise ValueError('Refusing to clean a drive, home or project root.')
    marker = root / 'cache.json'
    if not marker.is_file() or json.loads(marker.read_text()).get('kind') != CACHE_KIND:
        raise ValueError('This is not a registered Persona 4 build cache. No files were removed.')
    with exclusive_lock(root / 'cache.lock'):
        targets = []
        builds = root / 'builds'
        if builds.exists():
            if builds.resolve() != builds or builds.is_symlink():
                raise ValueError('Refusing to clean a redirected cache directory.')
            for job in builds.iterdir():
                if not re.fullmatch('[0-9a-f]{64}', job.name) or not job.is_dir() or job.resolve() != job:
                    raise ValueError('Unknown or redirected build entry. No cache jobs were removed.')
                if not job.resolve().is_relative_to(root):
                    raise ValueError('Refusing to clean outside the selected cache.')
                targets.append(job)
        downloads = root / 'downloads'
        if include_downloads and downloads.exists():
            if downloads.resolve() != downloads or downloads.is_symlink():
                raise ValueError('Refusing to clean redirected downloads.')
            for path in downloads.iterdir():
                if not path.is_file() or path.is_symlink() or not re.fullmatch(
                        r'(persona4-[0-9a-f]{40}|pspdev-[0-9a-f]{64})\.(zip|lock|download)', path.name):
                    raise ValueError('Unknown download-cache entry. No files were removed.')
                targets.append(path)
        toolchains = root / 'toolchain'
        if include_downloads and toolchains.exists():
            if toolchains.resolve() != toolchains or toolchains.is_symlink():
                raise ValueError('Refusing to clean a redirected compiler cache.')
            targets.append(toolchains)
        # Older preparers hold job locks but do not know about cache.lock.
        with ExitStack() as locks:
            for target in targets:
                if target.is_dir():
                    locks.enter_context(exclusive_lock(target / 'build.lock'))
        removed = sum(folder_bytes(p) if p.is_dir() else p.stat().st_size for p in targets)
        for target in targets:
            # Recheck each absolute target immediately before deletion.
            if target.resolve() != target or not target.resolve().is_relative_to(root):
                raise ValueError('Cache target changed during cleanup.')
            if target.is_dir():
                shutil.rmtree(target)
            else:
                target.unlink()
        return {'path': str(root), 'removed_bytes': removed, 'downloads_kept': not include_downloads}


def script_command(script, *args):
    if getattr(sys, 'frozen', False):
        prefix = [sys.executable, '--build-worker']
    else:
        # Use the same hidden-process bootstrap for source CLI builds. Starting
        # python.exe without a console alone does not hide the compiler it spawns.
        prefix = [sys.executable, '-u', str(Path(__file__).with_name('psp_prepare.py')), '--build-worker']
    return prefix + [str(script), *map(str, args)]


@contextmanager
def exclusive_lock(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('a+b') as handle:
        if os.name == 'nt':
            import msvcrt
            handle.seek(0); handle.write(b'0'); handle.flush(); handle.seek(0)
            try:
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise ValueError('Another build is using this cache. Wait for it to finish.') from error
        else:
            import fcntl
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def safe_relative(name):
    path = PurePosixPath(name)
    if not name or not path.parts or '\x00' in name or '\\' in name or ':' in name or path.is_absolute() or '..' in path.parts:
        raise ValueError(f'Unsafe package path: {name}')
    return path


def archive_files(source):
    """Validate a source ZIP and ignore its optional single outer directory."""
    files = []
    entries = source.infolist()
    if len(entries) > 50000 or sum(e.file_size for e in entries) > 512 << 20:
        raise ValueError('The upstream archive exceeds the expected source-tree size.')
    for entry in entries:
        path = safe_relative(entry.filename)
        if (entry.external_attr >> 16) & 0o170000 == 0o120000:
            raise ValueError('Upstream archive symlinks are not supported.')
        if not entry.is_dir():
            files.append((path, entry))
    if not files:
        raise ValueError('The upstream archive is empty.')
    outer = {path.parts[0] for path, _ in files}
    tree_roots = {'src', 'config', 'include', 'docs', 'tools', 'tests', '.github', 'asm', 'wiki', 'progress'}
    strip = len(outer) == 1 and not outer & tree_roots and all(len(path.parts) > 1 for path, _ in files)
    result, seen, parents = [], set(), set()
    for path, entry in files:
        relative = PurePosixPath(*path.parts[1:]) if strip else path
        key = relative.as_posix().casefold()
        if key in seen:
            raise ValueError(f'Duplicate or case-colliding upstream path: {relative}')
        seen.add(key)
        parents.update(p.as_posix().casefold() for p in relative.parents if p.parts)
        result.append((relative, entry))
    if seen & parents:
        raise ValueError('An upstream file conflicts with a directory.')
    return sorted(result, key=lambda row: row[0].as_posix())


def tree_digest(archive):
    """SHA256 over sorted (path length, UTF-8 path, byte length, file SHA256)."""
    result = hashlib.sha256(b'persona4-upstream-tree-v1\0')
    with zipfile.ZipFile(archive) as source:
        for path, entry in archive_files(source):
            name = path.as_posix().encode('utf-8')
            content = hashlib.sha256()
            with source.open(entry) as stream:
                for block in iter(lambda: stream.read(1 << 20), b''):
                    content.update(block)
            result.update(struct.pack('<I', len(name)) + name)
            result.update(struct.pack('<Q', entry.file_size) + content.digest())
    return result.hexdigest()


def verify_upstream(archive, wanted):
    try:
        actual = tree_digest(archive)
    except (OSError, zipfile.BadZipFile, RuntimeError) as error:
        raise ValueError(f'Cannot read the upstream source ZIP: {error}') from error
    if actual != wanted:
        raise ValueError('Upstream source-tree SHA256 mismatch. Use an unchanged ZIP of commit '
                         f'{UPSTREAM_COMMIT}; recompression is allowed, source changes are not.')


def payload_files(root):
    root = Path(root)
    files = list((root / 'port').glob('*.c')) + list((root / 'port').glob('*.h'))
    files += list((root / 'port').glob('*.S'))
    files += [root / 'tools' / (name + '.py') for name in BUILD_TOOLS]
    return sorted(files)


def verify_payload(bundle, config):
    expected = config.get('source_sha256', {})
    if not expected:
        raise ValueError('Source package has no source manifest.')
    for name, wanted in expected.items():
        path = safe_relative(name)
        if path.parts[0] not in ('port', 'tools') or path.suffix not in ('.c', '.h', '.S', '.py'):
            raise ValueError(f'Unexpected build input: {name}')
        source = Path(bundle) / 'source' / str(path)
        if not source.is_file() or digest(source) != wanted:
            raise ValueError(f'Source package is incomplete or changed: {name}. Extract it again.')


def find_pspdev(bundle, explicit=None):
    candidates = [explicit] if explicit else [
        Path(bundle) / 'toolchain/pspdev', os.environ.get('PSPDEV'),
        Path(shutil.which('psp-gcc.exe')).parent.parent if shutil.which('psp-gcc.exe') else None]
    for candidate in filter(None, candidates):
        candidate = Path(candidate).resolve()
        if all((candidate / 'bin' / (name + '.exe')).is_file() for name in TOOLS):
            if not (candidate / 'psp/sdk/lib/linkfile.prx').is_file():
                continue
            return candidate
    raise ValueError('Windows PSPDEV toolchain not found. Extract the complete utility archive, including toolchain/pspdev. '
                     'For developer builds, pass --pspdev with the PSPDEV root containing bin/ and psp/. '
                     'You need PSP GCC 15.2 and PSPSDK, not a PS2 compiler.')


def toolchain_identity(pspdev):
    print('Checking compiler and PSP libraries...', flush=True)
    result = hashlib.sha256()
    files = set()
    for folder in ('bin', 'lib/gcc', 'libexec/gcc', 'psp/include', 'psp/lib', 'psp/sdk'):
        files.update(p for p in (pspdev / folder).rglob('*') if p.is_file())
    for path in sorted(files):
        result.update(path.relative_to(pspdev).as_posix().encode() + b'\0')
        result.update(bytes.fromhex(digest(path)))
    version = subprocess.run([str(pspdev / 'bin/psp-gcc.exe'), '--version'],
                             check=True, capture_output=True, text=True).stdout.splitlines()[0]
    return {'sha256': result.hexdigest(), 'gcc': version}


def upstream_archive(cache, config, supplied=None):
    upstream = config['upstream']
    if upstream.get('commit') != UPSTREAM_COMMIT or upstream.get('url') != UPSTREAM_URL:
        raise ValueError('Unsupported upstream revision or download URL.')
    wanted = upstream.get('tree_sha256', '')
    if len(wanted) != 64:
        raise ValueError('Missing pinned upstream source-tree SHA256. Update the preparation package.')
    if supplied:
        path = Path(supplied).resolve()
        verify_upstream(path, wanted)
        return path
    path = cache / 'downloads' / f'persona4-{UPSTREAM_COMMIT}.zip'
    path.parent.mkdir(parents=True, exist_ok=True)
    with exclusive_lock(path.with_suffix('.lock')):
        if path.is_file():
            try:
                verify_upstream(path, wanted)
                return path
            except ValueError:
                print('Cached upstream sources are invalid; downloading a fresh archive.', flush=True)
        print('Downloading the pinned Persona 4 decompilation sources...', flush=True)
        temporary = path.with_suffix('.download')
        try:
            with urllib.request.urlopen(UPSTREAM_URL, timeout=60) as response, temporary.open('wb') as out:
                for block in iter(lambda: response.read(1 << 20), b''):
                    out.write(block)
            verify_upstream(temporary, wanted)
        except (OSError, ValueError, urllib.error.URLError) as error:
            temporary.unlink(missing_ok=True)
            raise RuntimeError(f'Unable to obtain the pinned upstream sources: {error}\n'
                               'If the repository or download is unavailable, retry with Internet access '
                               f'or pass --upstream-archive PATH (commit {UPSTREAM_COMMIT}). '
                               'The ZIP must preserve the original file paths and contents. '
                               'A different revision will not be substituted.') from error
        os.replace(temporary, path)
    return path


def download_toolchain(cache, config):
    """Fetch the recommended Windows PSPDEV once into the cache and return its root."""
    wanted = config.get('reference', {}).get('recommended_toolchain', {})
    url, archive_sha = wanted.get('url', ''), wanted.get('archive_sha256', '')
    if not url.startswith('https://github.com/dreikk91/persona4-psp/releases/download/') or len(archive_sha) != 64:
        raise ValueError('This package has no compiler download. Pass --pspdev with a PSPDEV root.')
    target = cache / 'toolchain' / archive_sha
    if (target / 'complete').is_file():
        return target / 'pspdev'
    path = cache / 'downloads' / f'pspdev-{archive_sha}.zip'
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file() or digest(path) != archive_sha:
        print('Downloading the PSP compiler (about 100 MB, once)...', flush=True)
        temporary = path.with_suffix('.download')
        try:
            with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as out:
                shutil.copyfileobj(response, out, 1 << 20)
            if digest(temporary) != archive_sha:
                raise ValueError('compiler archive SHA256 mismatch')
        except (OSError, ValueError, urllib.error.URLError) as error:
            temporary.unlink(missing_ok=True)
            raise RuntimeError(f'Unable to download the PSP compiler: {error}\n'
                               f'Retry with Internet access, or download {url} yourself, extract it '
                               'and pass --pspdev with its pspdev folder.') from error
        os.replace(temporary, path)
    print('Extracting the PSP compiler...', flush=True)
    partial = target.with_name(archive_sha + '.partial')
    shutil.rmtree(partial, ignore_errors=True)
    with zipfile.ZipFile(path) as source:
        for entry in source.infolist():
            name = safe_relative(entry.filename)
            if name.parts[0] != 'pspdev' or (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f'Unexpected compiler archive entry: {entry.filename}')
            if not entry.is_dir():
                out = partial.joinpath(*name.parts)
                out.parent.mkdir(parents=True, exist_ok=True)
                with source.open(entry) as src, out.open('wb') as dst:
                    shutil.copyfileobj(src, dst)
    (partial / 'complete').write_text(url + '\n')
    shutil.rmtree(target, ignore_errors=True)
    os.replace(partial, target)
    path.unlink()
    return target / 'pspdev'


def extract_upstream(archive, destination):
    with zipfile.ZipFile(archive) as source:
        for path, entry in archive_files(source):
            target = destination.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(entry) as src, target.open('wb') as out:
                shutil.copyfileobj(src, out)


def validate_result(release, build):
    release = Path(release)
    manifest = json.loads((release / 'manifest.json').read_text())
    if manifest.get('build') != build or manifest.get('release') is not True:
        raise ValueError('The local build did not produce the expected release manifest.')
    hashes = manifest.get('artifact_sha256', {})
    if set(hashes) != ARTIFACTS:
        raise ValueError('The local build artifact manifest is incomplete.')
    for name, wanted in hashes.items():
        if digest(release / name) != wanted:
            raise ValueError(f'Local build hash mismatch: {name}')
    pbp = (release / 'EBOOT.PBP').read_bytes()
    if len(pbp) < 40 or pbp[:4] != b'\0PBP':
        raise ValueError('Invalid generated PBP header.')
    offsets = struct.unpack_from('<8I', pbp, 8)
    if offsets[0] < 40 or list(offsets) != sorted(offsets) or offsets[-1] > len(pbp):
        raise ValueError('Invalid generated PBP offsets.')
    if hashlib.sha256(pbp[offsets[6]:offsets[7]]).hexdigest() != hashes['p4.prx']:
        raise ValueError('Generated PBP does not contain the matching PRX.')
    return manifest


def classify_reference(config, hashes, toolchain):
    reference = config.get('reference', {})
    recommended = reference.get('recommended_toolchain', {}).get('sha256')
    result = {'status': 'unverified', 'profile': None,
              'message': 'Unverified build: launch files do not match a recorded reference. PSP testing is required.',
              'toolchain_matches_recommended': bool(recommended and recommended == toolchain.get('sha256'))}
    for profile in reference.get('profiles', []):
        files = profile.get('files', {})
        if profile.get('build') != config.get('build') or set(files) != {'EBOOT.PBP', 'p4data.bin', 'p4str.bin'}:
            continue
        if not all(hashes.get(name) == wanted for name, wanted in files.items()):
            continue
        kind = profile.get('kind')
        if kind not in ('hardware', 'host'):
            continue
        result.update(status=kind + '_match', profile=profile['id'], scope=profile.get('scope', ''))
        result['message'] = ('Byte-identical to the recorded PSP-tested launch files (selected scenes only).'
                             if kind == 'hardware' else
                             'Matches the host-checked source build. This is not yet a PSP-validated build.')
        return result
    return result


def record_verification(release, config, manifest, toolchain):
    report = classify_reference(config, manifest['artifact_sha256'], toolchain)
    save_json(Path(release) / 'verification.json', report)
    print(report['message'], flush=True)
    if not report['toolchain_matches_recommended']:
        print('Note: this compiler/library fingerprint differs from the recommended toolchain.', flush=True)
    return report


def run_stage(workspace, name, args, env, log):
    if _cancelled.is_set():
        raise RuntimeError('Build cancelled.')
    command = script_command(workspace / 'tools' / name, *args)
    print(f'Building: {name} {" ".join(map(str, args))}', flush=True)
    with subprocess.Popen(command, cwd=workspace, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                          **({'creationflags': subprocess.CREATE_NO_WINDOW} if os.name == 'nt' else {})) as process:
        _active.append(process)
        try:
            if _cancelled.is_set():
                cancel_build()
            for line in process.stdout:
                log.write(line); log.flush()
                print(line, end='', flush=True)
            code = process.wait()
        finally:
            _active.remove(process)
    if code:
        raise RuntimeError(f'{name} failed (exit {code}). See build.log in the local build cache.')


def build_eboot(iso, bundle, pspdev=None, cache=None, archive=None):
    root = prepare_cache(cache_path(cache))
    with exclusive_lock(root / 'cache.lock'):
        return _build_eboot(iso, bundle, pspdev=pspdev, cache=root, archive=archive)


def _build_eboot(iso, bundle, pspdev=None, cache=None, archive=None):
    bundle, iso = Path(bundle).resolve(), Path(iso).resolve()
    config = json.loads((bundle / 'prepare.json').read_text(encoding='utf-8'))
    if config.get('version') != 2 or config.get('mode') != 'source-build':
        raise ValueError('This is not a source-build preparation package.')
    verify_payload(bundle, config)
    # The extraction helper reads the disc and never writes into the bundle.
    from extract_elf import extract
    elf = extract(iso)
    if hashlib.sha1(elf).hexdigest() != RETAIL_SHA1:
        raise ValueError('Unsupported executable. Use the original Persona 4 USA ISO.')
    try:
        compiler = find_pspdev(bundle, pspdev)
    except ValueError:
        if pspdev:
            raise
        compiler = download_toolchain(cache, config)
    toolchain = toolchain_identity(compiler)
    cache = cache_path(cache)
    archive_path = upstream_archive(cache, config, archive)
    identity = {'config_sha256': digest(bundle / 'prepare.json'),
                'elf_sha256': hashlib.sha256(elf).hexdigest(), 'toolchain': toolchain}
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    job = cache / 'builds' / key
    job.mkdir(parents=True, exist_ok=True)
    workspace = job / 'work'
    release = workspace / 'build/source_release'
    with exclusive_lock(job / 'build.lock'):
        ready = job / 'ready.json'
        if ready.exists():
            try:
                if json.loads(ready.read_text()) == identity:
                    manifest = validate_result(release, config['build'])
                    record_verification(release, config, manifest, toolchain)
                    print('Using the verified cached EBOOT build.', flush=True)
                    return release
            except (OSError, ValueError, KeyError):
                print('Cached build is incomplete or changed; rebuilding it.', flush=True)
        ready.unlink(missing_ok=True)
        if workspace.exists():
            resolved = workspace.resolve()
            if resolved.parent != job.resolve() or resolved.name != 'work':
                raise ValueError('Refusing to clean a workspace outside the build cache.')
            shutil.rmtree(resolved)
        workspace.mkdir()
        for name in config['source_sha256']:
            target = workspace / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(bundle / 'source' / name, target)
        extract_upstream(archive_path, workspace / 'ext/Persona4-Decompilation')
        (workspace / 'build').mkdir()
        (workspace / 'build/SLUS_217.82').write_bytes(elf)
        env = os.environ.copy()
        env.update(PSPDEV=str(compiler), PYTHONUNBUFFERED='1', PYTHONIOENCODING='utf-8')
        env['PATH'] = str(compiler / 'bin') + os.pathsep + env.get('PATH', '')
        print(f'Local build cache: {job}', flush=True)
        with (job / 'build.log').open('w', encoding='utf-8') as log:
            run_stage(workspace, 'psp_census.py', ['--compile'], env, log)
            if (workspace / 'build/census/failed_compile.txt').read_text().strip():
                raise RuntimeError('Some recovered C files did not compile. See the census report.')
            run_stage(workspace, 'psp_ptr_proof.py', [], env, log)
            run_stage(workspace, 'psp_link.py', [], env, log)
            run_stage(workspace, 'psp_hw_debug.py', ['--release', '--out', release], env, log)
        manifest = validate_result(release, config['build'])
        record_verification(release, config, manifest, toolchain)
        save_json(job / 'build_identity.json', identity)
        save_json(ready, identity)
        print('EBOOT.PBP and its matching data were built and verified.', flush=True)
        return release
