"""One measured diagnostic arm through the persistent PSPLink bridge; restores controls.

Example (after verifying modinfo and reaching a stationary scene):
  python tools/psp_probe_ab.py baseline --base 0x08804000
  python tools/psp_probe_ab.py no_particles --base 0x08804000 --set p4_probe_flags=4
Requires PSPDEV for the matching psp-nm. Never resets or starts the game.
"""
import argparse
import os
import json
import re
import struct
import subprocess
import time
from pathlib import Path

from psp_data_census import ROOT
from psp_link import BIN

DEFAULTS = {'p4_probe_flags': 0, 'p4_probe_geometry': 0, 'p4_probe_keep_every': 0,
            'p4_probe_geometry2': 0, 'p4_probe_geometry3': 0, 'p4_probe_geometry4': 0,
            'p4_probe_census': 0,
            'p4_probe_depth_mode': 0, 'p4_probe_depth_cut': 2000.0,
            'p4_mesh_frustum': 0, 'p4_exp_scissor': 0, 'p4_exp_ge_time': 0,
            'p4_ge_skin': 1, 'p4_ge_light': 1, 'p4_guard_clip': 1, 'p4_guard_compact': 1, 'p4_guard_near_ge': 0, 'p4_guard_clip_fast': 1, 'p4_guard_gnear': 1, 'p4_guard_boxes': 1, 'p4_uv_shift_step': 1, 'p4_cache_audit': 0, 'p4_cache_budget': 6291456,
            'p4_gs_test': 1, 'p4_gs_test_skip': 1, 'p4_pipeline': 1, 'p4_mipmaps': 1, 'p4_cull_far': 0, 'p4_guard_world': 1, 'p4_guard_full_cache': 1, 'p4_instance_replay': 1, 'p4_fog': 0, 'p4_vsync_late': 1, 'p4_light_prelit': 1, 'p4_light_unprelit': 1, 'p4_gs_mesh_alpha': 1, 'p4_flip_event': 1, 'p4_cover_stats': 0, 'p4_bone_direct': 1, 'p4_list_cached': 1, 'p4_skin_alpha': 1, 'p4_alpha_x2': 0, 'p4_im3d': 1, 'p4_uv_memo': 1, 'p4_hanim_c': 0, 'p4_hanim_check': 0, 'p4_guard_chunk': 8, 'p4_guard_loop2': 1, 'p4_guard_box_vfpu': 1, 'p4_guard_box_check': 0, 'p4_probe_2d': 0, 'p4_frameskip': 1, 'p4_frameskip_max': 2, 'p4_frameskip_bones': 1, 'p4_uv_rebase': 1, 'p4_pose_plain': 0, 'p4_hanim_nan_fix': 1, 'p4_bone_check': 1, 'p4_dst_alpha_passes': 0}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('label')
    ap.add_argument('--base', required=True, type=lambda v: int(v, 0))
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--shot', action='store_true')
    ap.add_argument('--preview', action='store_true', help='screenshot only, no timing windows')
    ap.add_argument('--samples', type=int, default=3)
    ap.add_argument('--old-build', action='store_true', help='measure an earlier build: its symbols come from its own ELF')
    args = ap.parse_args()
    if args.preview and not args.shot:
        ap.error('--preview requires --shot')
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.label) or args.samples < 2:
        ap.error('simple label and at least two samples required')
    session = Path(os.environ.get('P4_SESSION', ROOT / 'build/psplink_session'))
    log = session / 'console.log'
    queue = session / 'commands.txt'
    text = log.read_text(errors='replace')
    boots = re.findall(r'hwdbg b(\d+) PRX image=', text)
    if not boots or int(boots[-1]) < 85:
        raise SystemExit('No supported confirmed diagnostic boot')
    build_number = int(boots[-1])
    build = ROOT / f'build/psplink_b{build_number}'
    nm = subprocess.check_output([str(BIN / 'psp-nm.exe'), str(build / 'p4.elf')], text=True)
    symbols = {p[2]: int(p[0], 16) + args.base for p in (line.split() for line in nm.splitlines()) if len(p) == 3}
    manifest = json.loads((build / 'manifest.json').read_text())
    import hashlib
    for name, digest in manifest['source_sha256'].items():
        if not args.old_build and hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('Build source changed; recheck symbols before touching RAM (--old-build: an earlier build, its own ELF)')
    changes = {}
    for item in args.set:
        name, value = item.split('=', 1)
        if name not in DEFAULTS:
            ap.error('unsupported control: ' + name)
        if name not in symbols:
            ap.error('control absent in this build: ' + name)
        changes[name] = float(value) if name == 'p4_probe_depth_cut' else int(value, 0)

    def bits(name, value):
        return struct.unpack('<I', struct.pack('<f', value))[0] if name == 'p4_probe_depth_cut' else value & 0xffffffff

    def send(commands):
        with queue.open('a', encoding='utf-8') as f:
            f.write('\n'.join(commands) + '\n')

    def write_controls(values, check=False):
        if not values:
            return
        commands = [f'pokew 0x{symbols[n]:08x} 0x{bits(n,v):08x}' for n, v in values.items()]
        commands.append('dcache w')
        if check:
            commands += [f'peekw 0x{symbols[n]:08x}' for n in values]
            commands += [f'echo DBG_FLUSH_AB_{i}_................................................' for i in range(40)]
        send(commands)

    def tail(offset):
        with log.open('rb') as f:
            f.seek(offset)
            return f.read().decode('utf-8', 'replace')

    position = log.stat().st_size
    started = time.monotonic()
    result = {'label': args.label, 'base': args.base, 'controls': changes, 'build': build_number}
    raw = ''
    try:
        write_controls(changes, check=True)
        # Readback proves receipt; exclude log windows preceding the last ack.
        ack_end = 0
        while changes:
            raw = tail(position)
            ends = []
            for name, value in changes.items():
                match = re.search(rf'0x{symbols[name]:08X}: 0x{bits(name,value):08X}', raw, re.I)
                if match:
                    ends.append(match.end())
            if len(ends) == len(changes):
                ack_end = max(ends)
                break
            if time.monotonic() - started > 30:
                raise TimeoutError('No control readback from PSP')
            time.sleep(0.5)
        pattern = rf'b{build_number} hwperf n=(\d+) ms=(\d+) cpu_us=(\d+) ge_wait_us=(\d+) present_us=(\d+)'
        previous = 0
        while not args.preview:
            raw = tail(position)
            matches = list(re.finditer(pattern, raw[ack_end:]))
            if len(matches) > previous:
                previous = len(matches)
                print(f'{args.label}: window {previous}/{args.samples}', flush=True)
            if len(matches) >= args.samples:
                break
            if time.monotonic() - started > 180:
                raise TimeoutError('No complete frame windows from PSP')
            time.sleep(0.5)
        if args.preview:
            time.sleep(0.75)  # let the changed frame reach the display
            matches = []
        samples = []
        for match in matches[:args.samples]:
            n, ms, cpu, ge, present = map(int, match.groups())
            samples.append(dict(frames=n, elapsed_ms=ms, frame_ms=ms/n, fps=n*1000/ms,
                                cpu_ms=cpu/1000, ge_wait_ms=ge/1000, present_ms=present/1000))
        result['samples'] = samples
        result['kept'] = samples[1:]  # first window may span the switch or screenshot
        if samples:
            result['average'] = {k: sum(s[k] for s in samples[1:])/len(samples[1:])
                                 for k in ('frame_ms','fps','cpu_ms','ge_wait_ms','present_ms')}
            print(json.dumps(result['average']), flush=True)
        if args.shot:
            # PSPLink scrshot can deadlock AVC playback on real hardware.
            # Check the latest boot's task log immediately before requesting it.
            latest = log.read_text(errors='replace').rsplit('PRX image=', 1)[-1]
            movie_events = re.findall(r'video task (begin|end)\b', latest)
            if movie_events and movie_events[-1] == 'begin':
                raise RuntimeError('Movie playback is active; PSPLink scrshot can hang the AVC decoder')
            destination = Path(f'C:/temp/p3f-psplink/b{build_number}') / (args.label + '.bmp')
            if destination.exists():
                raise FileExistsError(destination)
            send([f'scrshot host0:/b{build_number}/{args.label}.bmp'])
            until = time.monotonic() + 20
            while time.monotonic() < until:
                if destination.exists():
                    data = destination.read_bytes()
                    if len(data) >= 6 and len(data) == struct.unpack_from('<I', data, 2)[0]:
                        from PIL import Image
                        Image.open(destination).save(destination.with_suffix('.png'))
                        result['screenshot'] = str(destination.with_suffix('.png'))
                        break
                time.sleep(0.5)
            else:
                raise TimeoutError('Screenshot did not complete')
    except BaseException as error:
        result['error'] = str(error)
        raise
    finally:
        write_controls({name: DEFAULTS[name] for name in changes})
        result['restore_requested'] = bool(changes)
        output = build / 'experiments'
        output.mkdir(exist_ok=True)
        (output / (args.label + '.json')).write_text(json.dumps(result, indent=2))
        (output / (args.label + '.log')).write_text(raw, encoding='utf-8')


if __name__ == '__main__':
    main()
