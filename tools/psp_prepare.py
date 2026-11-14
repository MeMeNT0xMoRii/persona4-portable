#!/usr/bin/env python
"""Build and prepare a PSP installation from a supported retail ISO.

Run without arguments for the desktop interface. --cli is also available for automation.
Source packages compile EBOOT.PBP locally; old prebuilt packages remain readable.
"""
import sys


def _hide_child_windows():
    import os
    import subprocess
    if os.name != 'nt':
        return
    original = subprocess.Popen.__init__

    def hidden(self, *args, **kwargs):
        kwargs['creationflags'] = kwargs.get('creationflags', 0) | subprocess.CREATE_NO_WINDOW
        original(self, *args, **kwargs)

    subprocess.Popen.__init__ = hidden


# This must run before importing any embedded psp_* modules. Each build script
# gets a fresh interpreter and imports the staged source tree, not frozen copies.
if __name__ == '__main__' and sys.argv[1:2] == ['--build-worker']:
    from pathlib import Path
    import runpy
    if len(sys.argv) < 3:
        raise SystemExit('Missing build script.')
    script = Path(sys.argv[2]).resolve()
    sys.path.insert(0, str(script.parent))
    from psp_build import WORKER_SCRIPTS
    if script.name not in WORKER_SCRIPTS or script.parent.name != 'tools':
        raise SystemExit('Unsupported build worker script.')
    # Windowed PyInstaller executables unset Python's streams even when a parent
    # supplies pipe handles. Reopen the inherited descriptors for build logs.
    if sys.stdout is None:
        sys.stdout = open(1, 'w', encoding='utf-8', buffering=1, closefd=False)
    if sys.stderr is None:
        sys.stderr = open(2, 'w', encoding='utf-8', buffering=1, closefd=False)
    _hide_child_windows()
    sys.argv = [str(script), *sys.argv[3:]]
    try:
        runpy.run_path(str(script), run_name='__main__')
    except Exception:
        import traceback
        traceback.print_exc()
        raise SystemExit(1)
    raise SystemExit(0)

import argparse
from concurrent.futures import ThreadPoolExecutor
import contextlib
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import struct
import sys
import threading
import traceback

from psp_disc import CONTAINERS, CVM_BASE, PART, SECTOR, path_hash, root, walk

BUNDLE = Path(sys.executable).parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parent.parent


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for chunk in iter(lambda: f.read(4 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def save_json(path, data):
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def read_range(source, offset, size):
    source.seek(offset)
    data = source.read(size)
    if len(data) != size:
        raise ValueError('The ISO is truncated or unreadable.')
    return data


def apply_recipe(elf, recipe):
    if hashlib.sha256(elf).hexdigest() != recipe['elf_sha256']:
        raise ValueError('Unsupported game executable. Use the original Persona 4 USA (SLUS-21782) ISO.')
    start, size = recipe['data_offset'], recipe['data_size']
    data = bytearray(elf[start:start + size])
    if len(data) != size:
        raise ValueError('Truncated game executable.')
    for offset, length in recipe['zero_ranges']:
        if offset < 0 or offset + length > size:
            raise ValueError('Invalid data recipe range.')
        data[offset:offset + length] = bytes(length)
    for offset, value in recipe['words']:
        struct.pack_into('<I', data, offset, value)
    offsets = recipe['string_offsets']
    strings = struct.pack(f'<{len(offsets)}I', *offsets)
    if hashlib.sha256(data).hexdigest() != recipe['data_sha256'] or hashlib.sha256(strings).hexdigest() != recipe['strings_sha256']:
        raise ValueError('Prepared data does not match this EBOOT build.')
    return data, strings


def inspect_disc(iso, recipe=None):
    with iso.open('rb') as f:
        top = {p: (lba, n) for p, lba, n in walk(f, 0, *root(f, 0))}
        if 'SLUS_217.82' not in top:
            raise ValueError('Use a Persona 4 USA (SLUS-21782) ISO; other editions are not supported yet.')
        lba, size = top['SLUS_217.82']
        elf = read_range(f, lba * SECTOR, size)
        if recipe is None:
            from psp_build import RETAIL_SHA1
            if hashlib.sha1(elf).hexdigest() != RETAIL_SHA1:
                raise ValueError('Use the original Persona 4 USA executable.')
            data, strings = None, None
        else:
            data, strings = apply_recipe(elf, recipe)
        containers = []
        for name in CONTAINERS:
            entry = top.get(name + '.CVM') or top.get(name + '.BIN')
            if entry is None:
                raise ValueError(f'The ISO is missing {name}.')
            containers.append((name, entry[0] * SECTOR, entry[1]))
        # Validate every embedded filesystem before creating installation files.
        for name, offset, _ in containers:
            root(f, offset + CVM_BASE)
    return containers, data, strings


def extract_containers(iso, containers, destination, state, state_path):
    rows, files = {}, {}
    with iso.open('rb') as src:
        for number, (name, offset, size) in enumerate(containers):
            for part, relative in enumerate(range(0, size, PART)):
                filename = f'{name}.{part}'
                target = destination / filename
                count = min(PART, size - relative)
                previous = state.get('files', {}).get(filename)
                if previous and target.is_file() and target.stat().st_size == count and sha256(target) == previous:
                    print(f'{filename}: verified, keeping existing file', flush=True)
                    files[filename] = previous
                    continue
                print(f'Extracting {filename} ({count / (1 << 20):.0f} MiB)', flush=True)
                temporary = target.with_name(filename + '.tmp')
                h = hashlib.sha256()
                src.seek(offset + relative)
                with temporary.open('wb') as dst:
                    left = count
                    while left:
                        chunk = read_range(src, src.tell(), min(left, 4 << 20))
                        dst.write(chunk); h.update(chunk); left -= len(chunk)
                os.replace(temporary, target)
                files[filename] = h.hexdigest()
                state.setdefault('files', {})[filename] = files[filename]
                save_json(state_path, state)
            base = offset + CVM_BASE
            for path, lba, length in walk(src, base, *root(src, base)):
                key = path_hash(path)
                if key in rows and rows[key][0] != path:
                    raise ValueError('Game file path hash collision: ' + path)
                rows.setdefault(key, (path, number + (lba + CVM_BASE // SECTOR) * 8, length))
    index = b''.join(struct.pack('<3I', key, where, size) for key, (_, where, size) in sorted(rows.items()))
    (destination / 'p4files.idx').write_bytes(index)
    files['p4files.idx'] = hashlib.sha256(index).hexdigest()
    return files


def bundle_ffmpeg(bundle, cache=None):
    """A package's own helpers/ffmpeg.exe, else the pinned gyan.dev build downloaded once."""
    helper = Path(bundle) / 'helpers/ffmpeg.exe'
    if helper.is_file():
        return helper
    from psp_build import download_ffmpeg
    return download_ffmpeg(cache)


def prepare(iso, output, bundle=BUNDLE, workers=4, *, pspdev=None, build_cache=None,
            upstream_archive=None, build_only=False):
    from psp_audio import available_rows, build_encoder, catalogue, convert as convert_audio, copy_banks, pack_audio
    from psp_video import convert as convert_video, movies

    iso, output, bundle = Path(iso).resolve(), Path(output).resolve(), Path(bundle).resolve()
    recipe = json.loads((bundle / 'prepare.json').read_text(encoding='utf-8'))
    verification = None
    source_build = recipe.get('version') == 2 and recipe.get('mode') == 'source-build'
    if recipe.get('version') != 1 and not source_build:
        raise ValueError('Unsupported preparation package version.')
    if not source_build:
        eboot = bundle / 'P4/EBOOT.PBP'
        if sha256(eboot) != recipe['eboot_sha256']:
            raise ValueError('EBOOT.PBP does not match prepare.json. Extract the release archive again.')
    print('Checking the game edition…', flush=True)
    containers, data, strings = inspect_disc(iso, None if source_build else recipe)
    # Preflight all dependencies before the large extraction.
    if not build_only:
        import av  # noqa: F401
        ffmpeg = bundle_ffmpeg(bundle, build_cache)
        encoder = build_encoder(bundle / 'helpers/audio_vag_encoder.exe')
        vgmstream = bundle / 'helpers/vgmstream/vgmstream-cli.exe'
        for helper in (Path(ffmpeg), vgmstream):
            if not helper.is_file():
                raise FileNotFoundError(f'Missing release helper: {helper}')
    if not 1 <= workers <= 16:
        raise ValueError('Choose between 1 and 16 audio workers.')
    output.mkdir(parents=True, exist_ok=True)
    destination, work = output / 'P4', output / '.p4-work'
    if destination == bundle / 'P4':
        raise ValueError('Choose an output folder outside the release package.')
    if iso.is_relative_to(destination) or iso.is_relative_to(work):
        raise ValueError('Keep the source ISO outside the output P4 and work folders.')
    work.mkdir(exist_ok=True)
    # Exclusive lock also protects two independent utility instances.
    lock = (work / 'prepare.lock').open('a+b')
    try:
        if os.name == 'nt':
            import msvcrt
            lock.seek(0); lock.write(b'0'); lock.flush(); lock.seek(0)
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise ValueError('Preparation is already running for this output folder.') from error
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        print('Checking the complete ISO (this also makes resuming safe)…', flush=True)
        iso_digest = sha256(iso)
        state_path = work / 'state.json'
        previous = json.loads(state_path.read_text()) if state_path.exists() else None
        if previous and previous.get('iso_sha256') != iso_digest:
            raise ValueError('This output folder belongs to another ISO. Choose a new output folder.')
        if destination.exists() and previous is None and any(destination.iterdir()):
            raise ValueError('The output already contains a P4 installation. Choose an empty output folder.')
        source_identity = sha256(bundle / 'prepare.json') if source_build else None
        if previous and source_build and previous.get('source_config_sha256') != source_identity:
            raise ValueError('This output folder belongs to another source package. Choose a new output folder.')
        if source_build:
            from psp_build import build_eboot
            built = build_eboot(iso, bundle, pspdev=pspdev, cache=build_cache, archive=upstream_archive)
            built_manifest = json.loads((built / 'manifest.json').read_text())
            verification_path = built / 'verification.json'
            if verification_path.exists():
                verification = json.loads(verification_path.read_text())
            else:
                from psp_build import classify_reference
                verification = classify_reference(recipe, built_manifest['artifact_sha256'], {})
            hashes = built_manifest['artifact_sha256']
            recipe.update(eboot_sha256=hashes['EBOOT.PBP'], data_sha256=hashes['p4data.bin'],
                          strings_sha256=hashes['p4str.bin'])
            eboot = built / 'EBOOT.PBP'
            data, strings = (built / 'p4data.bin').read_bytes(), (built / 'p4str.bin').read_bytes()
        identity = {'iso_sha256': iso_digest, 'eboot_sha256': recipe['eboot_sha256']}
        if source_build:
            identity['source_config_sha256'] = source_identity
        state = previous if previous is not None else dict(identity, files={})
        if any(state.get(k) != v for k, v in identity.items()):
            raise ValueError('This output folder belongs to another ISO or release. Choose a new output folder.')
        if destination.exists() and not state_path.exists() and any(destination.iterdir()):
            raise ValueError('The output already contains a P4 installation. Choose an empty output folder.')
        # Fresh preparation needs both final archives and intermediate converted audio/video.
        if not build_only and not state_path.exists() and shutil.disk_usage(output).free < sum(c[2] for c in containers) + (4 << 30):
            raise ValueError('Not enough free space. Free at least the ISO size plus 4 GiB on the output drive.')
        destination.mkdir(exist_ok=True)
        save_json(state_path, state)
        if build_only:
            print('Installing the locally built launch files...', flush=True)
            (destination / 'p4data.bin').write_bytes(data)
            (destination / 'p4str.bin').write_bytes(strings)
            shutil.copyfile(eboot, destination / 'EBOOT.PBP')
            hashes = {name: sha256(destination / name) for name in ('EBOOT.PBP', 'p4data.bin', 'p4str.bin')}
            for name, expected in (('EBOOT.PBP', recipe['eboot_sha256']), ('p4data.bin', recipe['data_sha256']),
                                   ('p4str.bin', recipe['strings_sha256'])):
                if hashes[name] != expected:
                    raise ValueError('Launch file verification failed: ' + name)
            save_json(destination / 'build_manifest.json', dict(identity, build=recipe['build'], files=hashes,
                                                               verification=verification))
            print(f'Build ready: {destination}. Game resources are still required.', flush=True)
            return destination
        (destination / 'installation.json').unlink(missing_ok=True)
        print('Step 1/4: extracting game files', flush=True)
        files = extract_containers(iso, containers, destination, state, state_path)
        print('Step 2/4: preparing original music, voices and effects', flush=True)
        loose = work / 'audio'
        loose.mkdir(exist_ok=True)
        rows = catalogue(iso)
        if len(rows) != recipe['audio_streams']:
            raise ValueError('The ISO audio catalogue does not match this release.')
        copy_banks(iso, loose)
        with ThreadPoolExecutor(max_workers=workers) as pool:
            for i, _ in enumerate(pool.map(lambda r: convert_audio(r, iso, loose, ffmpeg, encoder, vgmstream), rows), 1):
                if i % 100 == 0 or i == len(rows):
                    print(f'Audio: {i}/{len(rows)}', flush=True)
        if len(available_rows(rows, loose)) != len(rows):
            raise ValueError('Audio conversion is incomplete. Run preparation again to resume.')
        pack_audio(rows, loose, destination / 'SOUND')
        audio = json.loads((destination / 'SOUND/audio_manifest.json').read_text())
        files.update({'SOUND/' + name: digest for name, digest in audio['files'].items()})
        print('Step 3/4: preparing movies', flush=True)
        video_work = work / 'video'
        movie_rows = movies(iso)
        if len(movie_rows) != recipe['movies']:
            raise ValueError('The ISO movie catalogue does not match this release.')
        for i, row in enumerate(movie_rows, 1):
            print(f'Movie {i}/{len(movie_rows)}: {row[0]}', flush=True)
            movie = convert_video(iso, row, video_work, ffmpeg)
            for name, digest in movie['outputs'].items():
                if Path(name).suffix in ('.p4v', '.pcm'):
                    target = destination / 'VIDEO' / name
                    target.parent.mkdir(exist_ok=True)
                    if not target.exists() or sha256(target) != digest:
                        temporary = target.with_name(target.name + '.tmp')
                        shutil.copyfile(video_work / name, temporary)
                        os.replace(temporary, target)
                    files['VIDEO/' + name] = digest
        print('Step 4/4: preparing launch files and verifying the installation', flush=True)
        (destination / 'p4data.bin').write_bytes(data)
        (destination / 'p4str.bin').write_bytes(strings)
        shutil.copyfile(eboot, destination / 'EBOOT.PBP')
        files.update({'p4data.bin': recipe['data_sha256'], 'p4str.bin': recipe['strings_sha256'], 'EBOOT.PBP': recipe['eboot_sha256']})
        for name, digest in files.items():
            if sha256(destination / name) != digest:
                raise ValueError('Final verification failed: ' + name)
        save_json(destination / 'installation.json', dict(identity, build=recipe['build'], files=files,
                  audio_streams=len(rows), movies=len(movie_rows), verification=verification))
        print(f'\nReady! Copy {destination} to PSP/GAME/P4 on the memory card.', flush=True)
        return destination
    finally:
        lock.close()


class LogWriter:
    def __init__(self, messages, log, echo=None):
        self.messages, self.log, self.echo = messages, log, echo

    def write(self, text):
        self.log.write(text); self.log.flush()
        self.messages.put(text)
        if self.echo is not None:
            try:
                self.echo.write(text); self.echo.flush()
            except OSError:
                # A windowed EXE can inherit an unusable console handle.
                self.echo = None
        return len(text)

    def flush(self):
        self.log.flush()


def gui(bundle, *, pspdev=None, build_cache=None, upstream_archive=None, build_only=False):
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    window = tk.Tk()
    window.title('Persona 4 — Prepare for PSP')
    window.geometry('820x700')
    frame = ttk.Frame(window, padding=18); frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='Prepare Persona 4 for your PSP', font=('Segoe UI', 16)).pack(anchor='w')
    ttk.Label(frame, text='Choose your original Persona 4 USA ISO and an output folder.').pack(anchor='w', pady=(8, 16))
    iso, output = tk.StringVar(), tk.StringVar(value=str(Path.home() / 'Persona4-PSP'))
    only_build = tk.BooleanVar(value=build_only)
    selectors = []
    for label, variable, browse in [
        ('Game ISO', iso, lambda: filedialog.askopenfilename(filetypes=[('ISO image', '*.iso')])),
        ('Output folder', output, filedialog.askdirectory)]:
        row = ttk.Frame(frame); row.pack(fill='x', pady=4)
        ttk.Label(row, text=label, width=14).pack(side='left')
        entry = ttk.Entry(row, textvariable=variable); entry.pack(side='left', fill='x', expand=True)
        def choose(var=variable, callback=browse):
            selected = callback()
            if selected: var.set(selected)
        button = ttk.Button(row, text='Browse…', command=choose); button.pack(side='left', padx=(8, 0))
        selectors.extend([entry, button])
    ttk.Label(frame, text='The compiler and game sources are downloaded automatically on the first build.',
              wraplength=750).pack(anchor='w')
    mode_button = ttk.Checkbutton(frame, text='Build EBOOT only (skip game archive, audio and movie preparation)',
                                  variable=only_build)
    mode_button.pack(anchor='w', pady=(8, 0)); selectors.append(mode_button)
    status = tk.StringVar(value='Ready. Interrupted preparation can be resumed with the same ISO and output folder.')
    ttk.Label(frame, textvariable=status, wraplength=730).pack(anchor='w', pady=(14, 8))
    progress = ttk.Progressbar(frame, mode='indeterminate'); progress.pack(fill='x')
    text = tk.Text(frame, height=15, wrap='word', state='disabled'); text.pack(fill='both', expand=True, pady=12)
    messages, running = queue.Queue(), [False]

    def start():
        source, target, launch_only = iso.get(), output.get(), only_build.get()
        if not Path(source).is_file() or not target.strip():
            messagebox.showerror('Choose files', 'Select an existing ISO and an output folder.'); return
        running[0] = True
        for widget in selectors + [start_button]: widget.configure(state='disabled')
        progress.start(); status.set('Preparing… This can take a while. Keep this window open.')

        def worker():
            try:
                log_folder = Path(target); log_folder.mkdir(parents=True, exist_ok=True)
                with (log_folder / 'prepare.log').open('a', encoding='utf-8') as log:
                    writer = LogWriter(messages, log)
                    with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                        try:
                            prepare(source, target, bundle, pspdev=pspdev,
                                    build_cache=build_cache, upstream_archive=upstream_archive,
                                    build_only=launch_only)
                        except Exception:
                            traceback.print_exc(); raise
                messages.put(('built' if launch_only else 'done', str(Path(target) / 'P4')))
            except Exception as error:
                messages.put(('error', str(error)))
        threading.Thread(target=worker, daemon=True).start()

    start_button = ttk.Button(frame, text='Build and prepare', command=start); start_button.pack(anchor='e')

    def clear_build_cache():
        from psp_build import cache_info, clean_cache
        info = cache_info(build_cache)
        if not messagebox.askyesno('Clean build cache?',
                f"Remove {info['build_bytes'] / (1 << 20):.1f} MiB of compiler workspaces, objects and game ELF files?\n\n"
                f"{info['path']}\n\nCached upstream downloads and your prepared P4 installation will be kept."):
            return
        running[0] = True
        for widget in selectors + [start_button]: widget.configure(state='disabled')
        progress.start(); status.set('Cleaning the build cache...')

        def worker():
            try:
                result = clean_cache(build_cache)
                messages.put(('cleaned', f"Removed {result['removed_bytes'] / (1 << 20):.1f} MiB. Cached sources were kept."))
            except Exception as error:
                messages.put(('error', str(error)))
        threading.Thread(target=worker, daemon=True).start()

    clean_button = ttk.Button(frame, text='Clean build cache...', command=clear_build_cache)
    clean_button.pack(anchor='w'); selectors.append(clean_button)

    def completion(title, message, folder, launch_only):
        name = 'build_manifest.json' if launch_only else 'installation.json'
        report = json.loads((Path(folder) / name).read_text()).get('verification') or {}
        detail = report.get('message', 'Build verification status is not recorded.')
        status.set(status.get() + ' ' + detail)
        if report.get('status') == 'hardware_match':
            messagebox.showinfo(title, message + '\n\n' + detail)
        else:
            messagebox.showwarning(title, message + '\n\n' + detail)

    def poll():
        while not messages.empty():
            value = messages.get()
            if isinstance(value, tuple):
                running[0] = False; progress.stop()
                for widget in selectors + [start_button]: widget.configure(state='normal')
                kind, content = value
                if kind == 'done':
                    status.set('Ready! Copy the P4 folder to PSP/GAME/ on your memory card.')
                    completion('Game prepared', f'Copy this folder:\n{content}\n\nto PSP/GAME/ on your memory card.', content, False)
                elif kind == 'built':
                    status.set('EBOOT built. Game archives, audio and movies are still required.')
                    completion('EBOOT built', f'Launch files are ready in:\n{content}\n\nRun full preparation to create a complete installation.', content, True)
                elif kind == 'cleaned':
                    status.set(content)
                    messagebox.showinfo('Build cache cleaned', content)
                else:
                    status.set('Preparation stopped. Details are in prepare.log in the output folder.')
                    messagebox.showerror('Preparation stopped', content)
            else:
                text.configure(state='normal'); text.insert('end', value); text.see('end'); text.configure(state='disabled')
        window.after(100, poll)

    def close():
        if running[0] and not messagebox.askyesno('Stop preparation?', 'Stop now? Completed conversions will be reused when you run preparation again.'):
            return
        if running[0]:
            from psp_build import cancel_build
            cancel_build()
        window.destroy()
    window.protocol('WM_DELETE_WINDOW', close)
    poll(); window.mainloop()
    # Terminate the daemon worker when closing the frozen desktop process.
    if running[0]: os._exit(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', action='store_true')
    parser.add_argument('--iso', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--bundle', type=Path, default=BUNDLE)
    parser.add_argument('--workers', type=int, default=4)
    parser.add_argument('--pspdev', type=Path, help='Windows PSPDEV root (bin/ and psp/)')
    parser.add_argument('--build-cache', type=Path, help='Local compiler workspace/cache directory')
    parser.add_argument('--upstream-archive', type=Path, help='Pinned upstream ZIP for offline builds')
    parser.add_argument('--build-only', action='store_true', help='Build/install launch files without converting game resources')
    cache_actions = parser.add_mutually_exclusive_group()
    cache_actions.add_argument('--cache-info', action='store_true', help='Report build-cache disk usage')
    cache_actions.add_argument('--clean-cache', action='store_true', help='Remove cached compiler workspaces and game objects')
    parser.add_argument('--include-downloads', action='store_true', help='With --clean-cache, also remove cached upstream ZIPs')
    parser.add_argument('--check', action='store_true', help='Check the ISO and bundled runtime without converting')
    parser.add_argument('--probe', action='store_true', help='With --check, convert sample original audio and a short movie')
    args = parser.parse_args()
    # Do not flash dozens of console windows: the frozen EXE has no console even with --cli.
    _hide_child_windows()
    if args.cache_info or args.clean_cache:
        from psp_build import cache_info, clean_cache
        result = clean_cache(args.build_cache, args.include_downloads) if args.clean_cache else cache_info(args.build_cache)
        print(json.dumps(result, indent=2))
        return
    if args.check:
        if not args.iso or not args.output: parser.error('--check needs --iso and --output (report directory)')
        args.output.mkdir(parents=True, exist_ok=True)
        report = {'ok': False}
        try:
            import av
            import tkinter
            from psp_audio import ROOT as audio_root
            recipe = json.loads((args.bundle / 'prepare.json').read_text())
            source_build = recipe.get('version') == 2
            containers, data, strings = inspect_disc(args.iso, None if source_build else recipe)
            if source_build:
                from psp_build import verify_payload, find_pspdev
                verify_payload(args.bundle, recipe)
                try:
                    find_pspdev(args.bundle, args.pspdev)
                except ValueError:
                    # A package without toolchain/ downloads the recommended compiler on first build.
                    if args.pspdev or not recipe.get('reference', {}).get('recommended_toolchain', {}).get('url'):
                        raise
            av.CodecContext.create('h264', 'r')
            window = tkinter.Tk(); window.withdraw(); window.update_idletasks(); window.destroy()
            ffmpeg = bundle_ffmpeg(args.bundle, args.build_cache)
            for name in ('helpers/audio_vag_encoder.exe', 'helpers/vgmstream/vgmstream-cli.exe'):
                if not (args.bundle / name).is_file(): raise FileNotFoundError(name)
            if not source_build and sha256(args.bundle / 'P4/EBOOT.PBP') != recipe['eboot_sha256']:
                raise ValueError('EBOOT hash mismatch')
            if not (audio_root / 'tools/audio_vag_encoder.cs').is_file():
                raise FileNotFoundError('Bundled encoder source identity is missing')
            report.update(ok=True, build=recipe['build'], av=av.__version__, tk=tkinter.TkVersion,
                          mode=recipe.get('mode', 'prebuilt'),
                          data_sha256=hashlib.sha256(data).hexdigest() if data is not None else None,
                          containers=[r[0] for r in containers])
            if args.probe:
                from psp_audio import catalogue, convert as convert_audio
                from psp_video import movies, convert as convert_video
                with (args.output / 'probe.log').open('w', encoding='utf-8') as log, contextlib.redirect_stdout(log):
                    rows = catalogue(args.iso)
                    audio = args.output / 'audio'; audio.mkdir(exist_ok=True)
                    selected = [min((r for r in rows if r['encoding'] == encoding), key=lambda r: r['size']) for encoding in (3, 0x11)]
                    for row in selected:
                        convert_audio(row, args.iso, audio, ffmpeg,
                                      args.bundle / 'helpers/audio_vag_encoder.exe', args.bundle / 'helpers/vgmstream/vgmstream-cli.exe')
                    movie = min(movies(args.iso), key=lambda r: r[2])
                    convert_video(args.iso, movie, args.output / 'video', str(ffmpeg), seconds=1, crop_black=False)
                    report['probe_audio'] = [r['path'] for r in selected]
                    report['probe_movie'] = movie[0]
        except Exception:
            report['ok'] = False
            report['error'] = traceback.format_exc()
        save_json(args.output / 'check.json', report)
        if not report['ok']: raise SystemExit(1)
    elif args.cli:
        if not args.iso or not args.output: parser.error('--cli needs --iso and --output')
        args.output.mkdir(parents=True, exist_ok=True)
        with (args.output / 'prepare.log').open('a', encoding='utf-8') as log:
            writer = LogWriter(queue.Queue(), log, echo=sys.stdout)
            with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                try:
                    prepare(args.iso, args.output, args.bundle, args.workers, pspdev=args.pspdev,
                            build_cache=args.build_cache, upstream_archive=args.upstream_archive,
                            build_only=args.build_only)
                except Exception:
                    traceback.print_exc()
                    raise SystemExit(1)
    else:
        gui(args.bundle, pspdev=args.pspdev, build_cache=args.build_cache,
            upstream_archive=args.upstream_archive, build_only=args.build_only)


if __name__ == '__main__':
    main()
