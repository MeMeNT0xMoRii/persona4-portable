#!/usr/bin/env python
"""Offline CLUT textures: the player's own ISO -> p4clut.bin (game data: never ship it).

The PS2 keeps some textures as 32-bit (PSMCT32) or 16-bit pixels. On the PSP each 32-bit texel costs 4 bytes of RAM
and of GE texture bandwidth; an 8-bit index into a 256-entry palette costs 1 (CLUT8), a 4-bit one half a byte (CLUT4).
This tool finds every RenderWare PS2 native texture (TextureNative chunk 0x15, platform 'PS2\\0') in the ISO,
and for the 32/16-bit ones at least --min-texels in size writes a palettised copy that port/rwsky.c takes instead of
the original when it reads that texture (func_003fd940; p4_clut_pack). The lookup key is the FNV-1a hash of the
raster's width, height, depth and its pixel bytes as the stream holds them, so a texture the tool never saw is simply
not found and loads as before.

  python tools/psp_clut.py --census              # numbers only: textures by depth and size, the largest 32-bit ones
  python tools/psp_clut.py                       # -> build/psp_dist/PSP/GAME/P4/p4clut.bin

The conversion is lossy (at most 256 or 16 colours). A texture is written as CLUT4 when 16 colours reproduce it
within --clut4-error (mean absolute error a channel), else CLUT8.

File: 'P4CL' 1 count data_offset, count x (key u32, offset u32) sorted by key; per entry: u16 width, u16 height,
u8 bits (4 or 8), u8 0, u16 0, palette (16 or 256 x u32 ABGR, alpha 0..255, PSP order), indices row by row
(width bytes a row for CLUT8, width / 2 for CLUT4, low nibble first), padded to 4 bytes.
"""
import argparse
import collections
import os
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from psp_disc import walk, root, ISO, SECTOR, CVM_BASE, DIST  # noqa: E402

SKIP = {'ADX', 'AHX', 'SFD', 'ACX', 'SE', 'AFS'}
PS2 = 0x00325350
GIF_HEADER = 0x50
VERSION_MIN, VERSION_MAX = 0x35000, 0x37002  # port/rwsky.c CHUNK_VERSION_*


def iso_files(iso):
    with open(iso, 'rb') as f:
        lba, size = root(f, 0)
        for path, start, length in list(walk(f, 0, lba, size)):
            if not path.upper().endswith('.CVM'):
                continue
            base = start * SECTOR + CVM_BASE
            try:
                rl, rs = root(f, base)
            except AssertionError:
                continue
            for name, at, n in list(walk(f, base, rl, rs)):
                if name.rsplit('.', 1)[-1].upper() in SKIP:
                    continue
                f.seek(base + at * SECTOR)
                yield path + '/' + name, f.read(n)


def version_ok(stamp):
    v = ((stamp >> 14 & 0x3ff00) + 0x30000) | (stamp >> 16 & 0x3f) if stamp & 0xffff0000 else stamp << 8
    return VERSION_MIN <= v <= VERSION_MAX


def header(d, o):
    return struct.unpack_from('<III', d, o) if o + 12 <= len(d) else (None, 0, 0)


def textures(d):
    """(name, width, height, depth, pixel bytes) of every PS2 native texture chunk found by its header."""
    o = 0
    while True:
        o = d.find(b'\x15\x00\x00\x00', o)
        if o < 0:
            return
        found = None
        try:
            found = parse_native(d, o)
        except (struct.error, IndexError, ValueError):
            found = None
        if found:
            yield found[0]
            o = found[1]
        else:
            o += 1


def parse_native(d, o):
    kind, size, stamp = header(d, o)
    end = o + 12 + size
    if kind != 0x15 or end > len(d) or not version_ok(stamp):
        return None
    p = o + 12
    k, s, st = header(d, p)  # struct: platform, filtering
    if k != 1 or s < 8 or st != stamp or struct.unpack_from('<I', d, p + 12)[0] != PS2:
        return None
    p += 12 + s
    names = []
    for _ in range(2):  # name, mask
        k, s, st = header(d, p)
        if k != 2 or p + 12 + s > end:
            return None
        names.append(d[p + 12:p + 12 + s].split(b'\0', 1)[0].decode('latin-1'))
        p += 12 + s
    k, s, st = header(d, p)  # raster struct
    if k != 1 or p + 12 + s > end:
        return None
    q = p + 12
    k, s, st = header(d, q)  # RasterInfo
    if k != 1 or s < 64:
        return None
    width, height, depth = struct.unpack_from('<iii', d, q + 12)
    fmt, version = struct.unpack_from('<HH', d, q + 24)
    pixel_size, palette_size = struct.unpack_from('<II', d, q + 12 + 44)
    q += 12 + s
    k, s, st = header(d, q)  # data
    if k != 1 or q + 12 + s > end or not (0 < width <= 1024 and 0 < height <= 1024 and depth in (4, 8, 16, 32)):
        return None
    blob = q + 12
    pixels = None
    if version >= 2 and depth >= 16:
        stride = width * depth // 8
        pixels = d[blob + GIF_HEADER:blob + GIF_HEADER + stride * height]
        if len(pixels) != stride * height:
            pixels = None
    return (names[0], width, height, depth, fmt, pixels), end


def fnv1a(*parts):
    h = 2166136261
    for data in parts:
        for b in data:
            h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def key_of(width, height, depth, pixels):
    return fnv1a(struct.pack('<III', width, height, depth), pixels)


def census(args):
    by_depth = collections.Counter()
    bytes_depth = collections.Counter()
    seen = {}
    files = 0
    started = time.time()
    for path, data in iso_files(args.iso):
        files += 1
        for name, w, h, depth, fmt, pixels in textures(data):
            by_depth[depth] += 1
            bytes_depth[depth] += w * h * depth // 8
            if depth >= 16 and pixels is not None:
                k = key_of(w, h, depth, pixels)
                if k not in seen:
                    seen[k] = (w * h, name, w, h, depth, path)
    print(f'{files} files, {time.time() - started:.0f} s')
    for depth in sorted(by_depth):
        print(f'depth {depth:2}: {by_depth[depth]:6} textures (with repeats), {bytes_depth[depth] / 2**20:8.1f} MB of texels')
    unique = sorted(seen.values(), reverse=True)
    big = [u for u in unique if u[0] >= args.min_texels]
    print(f'distinct 16/32-bit textures: {len(unique)}, of them >= {args.min_texels} texels: {len(big)}, '
          f'{sum(u[0] * u[4] // 8 for u in big) / 2**20:.1f} MB -> CLUT8 {sum(u[0] + 1024 for u in big) / 2**20:.1f} MB')
    folders = collections.Counter(u[5].rsplit('/', 1)[0] for u in big)
    for folder, n in folders.most_common(20):
        print(f'  {n:5}  {folder}')
    for u in big[:40]:
        print(f'  {u[2]}x{u[3]}x{u[4]}  {u[1]}  {u[5]}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=ISO)
    ap.add_argument('--out', type=Path, default=DIST / 'p4clut.bin')
    ap.add_argument('--census', action='store_true', help='numbers only, no file')
    ap.add_argument('--min-texels', type=int, default=128 * 128)
    ap.add_argument('--clut4-error', type=float, default=2.0)
    args = ap.parse_args()
    if args.census:
        census(args)
        return
    raise SystemExit('pack writing: next step (run --census first)')


if __name__ == '__main__':
    main()
