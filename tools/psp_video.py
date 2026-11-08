#!/usr/bin/env python
"""Prepare PSP movies from the player's own Persona 4 ISO (never distribute assets).

pip install -r tools/requirements-video.txt
python tools/psp_video.py --list
python tools/psp_video.py --movie P4CTOP1 --seconds 10
python tools/psp_video.py --all
"""
import argparse
import fnmatch
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from psp_disc import ISO, SECTOR, CVM_BASE, root, walk

ROOT = Path(__file__).resolve().parent.parent
HEADER = struct.Struct('<4s15I')
RECORD = struct.Struct('<II')  # AVCC packet bytes, presentation timestamp in 90 kHz
VERSION = 1


def movies(iso):
    with iso.open('rb') as f:
        top = {p: (lba, size) for p, lba, size in walk(f, 0, *root(f, 0))}
        if 'DATA.CVM' not in top:
            raise ValueError('DATA.CVM missing from ISO')
        base = top['DATA.CVM'][0] * SECTOR + CVM_BASE
        return [(p, base + lba * SECTOR, size)
                for p, lba, size in walk(f, base, *root(f, base)) if p.endswith('.SFD')]


def ffmpeg_path(explicit=None):
    if explicit:
        return str(explicit)
    found = shutil.which('ffmpeg')
    if found:
        return found
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def run(command):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-6000:])


def crop_window(width, height, bounds, margin=2):
    """Outward aligned union: preserve every observed content rectangle, including titles."""
    valid = [(x1, x2, y1, y2) for x1, x2, y1, y2 in bounds
             if 0 <= x1 <= x2 < width and 0 <= y1 <= y2 < height]
    if not valid:
        return (0, 0, width, height)
    left = max(0, min(b[0] for b in valid) - margin) // 2 * 2
    top = max(0, min(b[2] for b in valid) - margin) // 2 * 2
    right = min(width, (max(b[1] for b in valid) + 1 + margin + 1) // 2 * 2)
    bottom = min(height, (max(b[3] for b in valid) + 1 + margin + 1) // 2 * 2)
    # Tiny borders may just be codec noise; keep that edge intact.
    if left < 4: left = 0
    if top < 4: top = 0
    if width - right < 4: right = width
    if height - bottom < 4: bottom = height
    return (left, top, right - left, bottom - top)


def detect_crop(ffmpeg, source, width, height):
    # Scan the COMPLETE movie, even when encoding a short probe. A fade or an
    # inset TV image at the beginning must not hide later edge content/subtitles.
    # Compare to the corner colour, rather than to absolute black: the original
    # letterbox fades to white at the end of the opening. Uniform fade borders
    # remain borders; disconnected edge titles still contribute to the union.
    analysis = (f'[0:v:0]split=2[image][edge];'
                f'[edge]crop=2:2:0:0,scale={width}:{height}:flags=neighbor[background];'
                '[image][background]blend=all_mode=difference,bbox=min_val=8[scan]')
    result = subprocess.run(list(map(str, [ffmpeg, '-hide_banner', '-nostats', '-loglevel', 'info',
        '-i', source, '-filter_complex', analysis, '-map', '[scan]', '-an', '-f', 'null', '-'])),
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr[-6000:])
    bounds = [tuple(map(int, values)) for values in re.findall(
        r'x1:(-?\d+)\s+x2:(-?\d+)\s+y1:(-?\d+)\s+y2:(-?\d+)', result.stderr)]
    if not bounds:
        raise ValueError('bbox did not report any frames')
    return crop_window(width, height, bounds), len(bounds)


def avcc_parameters(data):
    """Read the SPS/PPS from an AVCDecoderConfigurationRecord (ISO/IEC 14496-15)."""
    if len(data) < 7 or data[0] != 1 or (data[4] & 3) != 3:
        raise ValueError('Expected AVCC with four-byte NAL lengths')
    if data[5] & 31 != 1:
        raise ValueError('Expected one SPS')
    pos = 6

    def parameter():
        nonlocal pos
        if pos + 2 > len(data):
            raise ValueError('Truncated AVC parameter length')
        n = int.from_bytes(data[pos:pos + 2], 'big')
        pos += 2
        if not n or pos + n > len(data):
            raise ValueError('Truncated AVC parameters')
        result = data[pos:pos + n]
        pos += n
        return result

    sps = parameter()
    if pos >= len(data) or data[pos] != 1:
        raise ValueError('Expected one PPS')
    pos += 1
    pps = parameter()
    return sps, pps


def pack_video(mp4, target, pcm):
    import av
    with av.open(str(mp4)) as src, target.open('wb') as dst:
        stream = src.streams.video[0]
        sps, pps = avcc_parameters(stream.codec_context.extradata)
        rate = stream.average_rate
        dst.write(bytes(HEADER.size)); dst.write(sps); dst.write(pps)
        count = maximum = 0
        last = -1
        for packet in src.demux(stream):
            if not packet.size:
                continue
            if packet.pts is None or packet.pts != packet.dts:
                raise ValueError('Reordered or untimestamped video: B-frames must be disabled')
            pts = round(packet.pts * packet.time_base * 90000)
            if pts < 0 or pts <= last:
                raise ValueError('Non-monotonic video timestamps')
            last = pts
            data = bytes(packet)
            if len(data) > 256 * 1024:
                raise ValueError('AVC packet exceeds the probe buffer limit')
            dst.write(RECORD.pack(len(data), pts)); dst.write(data)
            count += 1; maximum = max(maximum, len(data))
        if not count or not rate or rate <= 0:
            raise ValueError('No video packets')
        samples = pcm.stat().st_size // 4 if pcm.exists() else 0
        if samples > 0xffffffff:
            raise ValueError('Audio too long')
        fields = (VERSION, stream.codec_context.width, stream.codec_context.height,
                  rate.numerator, rate.denominator, count, maximum, len(sps), len(pps),
                  44100 if samples else 0, 2 if samples else 0, samples, 0, 0, 0)
        dst.seek(0); dst.write(HEADER.pack(b'P4V1', *fields))
    return dict(zip(('version', 'width', 'height', 'fps_num', 'fps_den', 'frames',
                     'max_packet', 'sps_bytes', 'pps_bytes', 'sample_rate', 'channels',
                     'audio_samples', 'reserved0', 'reserved1', 'reserved2'), fields))


def convert(iso, row, output, ffmpeg, seconds=None, fps='30000/1001', force=False, crop_black=True):
    import av
    name, offset, size = row
    stem = Path(name).stem
    options = {'version': VERSION, 'fps': fps, 'seconds': seconds, 'crf': 20,
               'canvas': [480, 272], 'audio': 's16le/44100/stereo',
               'crop_black': 'whole-movie-border-union-v2' if crop_black else False}
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / (stem + '.json')
    with tempfile.TemporaryDirectory(prefix='p4-video-') as temporary:
        temp = Path(temporary)
        source = temp / (stem + '.sfd')
        digest = hashlib.sha256()
        with iso.open('rb') as src, source.open('wb') as dst:
            src.seek(offset)
            left = size
            while left:
                data = src.read(min(left, 4 << 20))
                if not data:
                    raise ValueError('Truncated source ISO')
                digest.update(data); dst.write(data); left -= len(data)
        fingerprint = {'source_sha256': digest.hexdigest(), 'options': options}
        if not force and manifest.exists():
            old = json.loads(manifest.read_text())
            if all(old.get(k) == v for k, v in fingerprint.items()) and all(
                (output / p).is_file() and hashlib.sha256((output / p).read_bytes()).hexdigest() == h
                for p, h in old.get('outputs', {}).items()
            ) and len(old.get('outputs', {})) >= 2:
                print(stem + ': unchanged', flush=True)
                return old
        with av.open(str(source)) as container:
            video = container.streams.video[0]
            source_info = {'codec': video.codec_context.name, 'width': video.codec_context.width,
                           'height': video.codec_context.height, 'fps': str(video.average_rate),
                           'has_audio': bool(container.streams.audio),
                           'sample_aspect_ratio': str(video.sample_aspect_ratio)}
        crop = (0, 0, source_info['width'], source_info['height'])
        scanned = 0
        if crop_black:
            print(stem + ': scanning black borders through the full movie', flush=True)
            crop, scanned = detect_crop(ffmpeg, source, source_info['width'], source_info['height'])
        x, y, w, h = crop
        print(f'{stem}: crop {w}x{h}+{x}+{y}', flush=True)
        mp4, pcm, packed = (temp / (stem + suffix) for suffix in ('.mp4', '.pcm', '.p4v'))
        # Expand the source sample aspect ratio before fitting; never stretch 4:3 to 16:9.
        filters = (f'crop={w}:{h}:{x}:{y},'
                   'scale=trunc(iw*sar/2)*2:ih,setsar=1,'
                   'scale=480:272:force_original_aspect_ratio=decrease:force_divisible_by=2,'
                   'pad=480:272:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=' + fps)
        common = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', source]
        duration = ['-t', str(seconds)] if seconds else []
        print(stem + ': encoding H.264', flush=True)
        run(common + duration + ['-map', '0:v:0', '-an', '-vf', filters,
            '-c:v', 'libx264', '-profile:v', 'baseline', '-level:v', '3.0',
            '-pix_fmt', 'yuv420p', '-preset', 'medium', '-crf', '20',
            '-maxrate', '1500k', '-bufsize', '1500k', '-bf', '0', '-refs', '1',
            '-g', '30', '-x264-params', 'bframes=0:ref=1:weightp=0',
            '-movflags', '+faststart', mp4])
        if source_info['has_audio']:
            # Start at zero, fill any initial timestamp gap with silence.
            run(common + duration + ['-map', '0:a:0', '-vn', '-af', 'aresample=44100:first_pts=0',
                                     '-ac', '2', '-c:a', 'pcm_s16le', '-f', 's16le', pcm])
        info = pack_video(mp4, packed, pcm)
        artifacts = [mp4, packed] + ([pcm] if pcm.exists() else [])
        result = {**fingerprint, 'source': name, 'source_info': source_info, 'video': info,
                  'crop': {'x': x, 'y': y, 'width': w, 'height': h, 'frames_with_content': scanned},
                  'outputs': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts}}
        for path in artifacts:
            staged = output / (path.name + '.tmp')
            shutil.copyfile(path, staged); os.replace(staged, output / path.name)
        if not pcm.exists():
            (output / (stem + '.pcm')).unlink(missing_ok=True)
        staged = manifest.with_suffix('.json.tmp')
        staged.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        os.replace(staged, manifest)
        print(f'{stem}: {info["frames"]} frames, {sum(p.stat().st_size for p in artifacts) / (1<<20):.1f} MiB', flush=True)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--iso', type=Path, default=ISO)
    parser.add_argument('--out', type=Path, help='Output directory (clips default to build/video_probe/VIDEO)')
    parser.add_argument('--list', action='store_true')
    choice = parser.add_mutually_exclusive_group()
    choice.add_argument('--movie', help='Name or glob, e.g. P4CTOP1 or P4CT*')
    choice.add_argument('--all', action='store_true')
    parser.add_argument('--seconds', type=float, help='Convert only the first N seconds (probe)')
    parser.add_argument('--fps', choices=['30000/1001', '15000/1001'], default='30000/1001')
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--keep-black-bars', action='store_true', help='Disable automatic outer black-border cropping')
    args = parser.parse_args()
    if args.seconds is not None and args.seconds <= 0:
        parser.error('--seconds must be positive')
    if args.out is None:
        args.out = ROOT / ('build/video_probe/VIDEO' if args.seconds else 'build/psp_dist/PSP/GAME/P4/VIDEO')
    rows = movies(args.iso)
    if args.list:
        for name, _, size in rows:
            print(f'{name:24s} {size / (1<<20):8.2f} MiB')
        return
    if not args.movie and not args.all:
        parser.error('Choose --list, --movie NAME, or --all')
    selected = rows if args.all else [r for r in rows if fnmatch.fnmatchcase(
        Path(r[0]).stem, args.movie.upper().removesuffix('.SFD'))]
    if not selected:
        parser.error('No matching movies')
    ffmpeg = ffmpeg_path(args.ffmpeg)
    for row in selected:
        convert(args.iso, row, args.out, ffmpeg, args.seconds, args.fps, args.force, not args.keep_black_bars)


if __name__ == '__main__':
    main()
