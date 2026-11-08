#!/usr/bin/env python
"""Decode every frame from converted P4V files on the PC and verify artifact hashes.

python tools/psp_video_verify.py build/psp_dist/PSP/GAME/P4/VIDEO
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import av
from psp_video import HEADER, RECORD


def verify(path):
    manifest = json.loads(path.with_suffix('.json').read_text())
    for name, digest in manifest['outputs'].items():
        artifact = path.parent / name
        if artifact.parent != path.parent or hashlib.sha256(artifact.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Hash mismatch: {name}')
    with path.open('rb') as f:
        h = HEADER.unpack(f.read(HEADER.size))
        magic, version, width, height, num, den, count, maximum, nsps, npps, rate, channels, samples, *_ = h
        if magic != b'P4V1' or version != 1 or not num or not den or not count or not 0 < maximum <= 262144:
            raise ValueError('Invalid header')
        if (width, height) != (480, 272) or not 0 < nsps <= 1024 or not 0 < npps <= 1024:
            raise ValueError('Invalid dimensions/parameters')
        sps, pps = f.read(nsps), f.read(npps)
        if len(sps) < 4 or len(pps) != npps:
            raise ValueError('Truncated parameters')
        decoder = av.CodecContext.create('h264', 'r')
        decoder.extradata = (bytes([1, sps[1], sps[2], sps[3], 255, 225]) +
                            struct.pack('>H', nsps) + sps + b'\x01' + struct.pack('>H', npps) + pps)
        decoded = 0
        last = -1
        for index in range(count):
            size, pts = RECORD.unpack(f.read(RECORD.size))
            if not 0 < size <= maximum or pts <= last or abs(pts - round(index * den * 90000 / num)) > 1:
                raise ValueError(f'Invalid packet/timestamp at {index}')
            last = pts
            data = f.read(size)
            if len(data) != size:
                raise ValueError('Truncated packet')
            for frame in decoder.decode(av.Packet(data)):
                if (frame.width, frame.height, frame.format.name) != (width, height, 'yuv420p'):
                    raise ValueError('Unexpected decoded frame format')
                decoded += 1
        decoded += len(decoder.decode(None))
        if decoded != count or f.read(1):
            raise ValueError(f'Frame count/trailing bytes: {decoded} vs {count}')
        if samples and ((rate, channels) != (44100, 2) or path.with_suffix('.pcm').stat().st_size != samples * 4):
            raise ValueError('Invalid PCM audio size/format')
    return {'movie': path.stem, 'decoded_frames': decoded, 'audio_samples': samples,
            'duration_s': count * den / num}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    paths = sorted(args.directory.glob('*.p4v'))
    if not paths:
        parser.error('No P4V movies found')
    results = []
    for path in paths:
        result = verify(path)
        results.append(result)
        print(f'{path.stem}: {result["decoded_frames"]} frames OK', flush=True)
    if args.report:
        args.report.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(f'Validated {len(results)} movies, {sum(r["decoded_frames"] for r in results)} frames')


if __name__ == '__main__':
    main()
