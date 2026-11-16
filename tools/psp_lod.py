#!/usr/bin/env python
"""Build static, seam-preserving LODs directly from the player's ISO (private game data).

python tools/psp_lod.py --out build/psp_dist/PSP/GAME/P4/p4lod.bin
python tools/psp_lod.py --limit 10 --out build/lod_sample.bin

Three joined-strip levels, 70/40/15 percent target triangles; locked boundaries
can stop simplification earlier. Materials and vertex attributes are unchanged.
This stage does not bake impostors or merge materials. A JSON report records
actual counts, including models that cannot safely reach a requested target.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import time

from psp_strips import (ISO, DIST, iso_files, geometries, mesh_triangles,
                        best_strips, join, same_triangles)

ROOT = Path(__file__).resolve().parent.parent
MAGIC = 0x504C3450
MAX_ENTRY = 512 * 1024


def build_native(folder):
    """Compile the repository's simplifier for the host, without downloads."""
    folder = Path(folder).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    output = folder / ('p4_lod.dll' if os.name == 'nt' else 'p4_lod.so')
    source = ROOT / 'tools/lod_native.c'
    cc = shutil.which('gcc') or shutil.which('clang')
    if cc:
        command = [cc, '-std=c11', '-O2', '-shared', '-I' + str(ROOT / 'port'),
                   str(source), '-o', str(output)]
        if os.name != 'nt':
            command += ['-fPIC', '-lm']
    else:
        vs = Path(os.environ.get('ProgramFiles', 'C:/Program Files')) / 'Microsoft Visual Studio'
        compilers = sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
        kits = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Windows Kits/10'
        versions = sorted((kits / 'Include').glob('10.*'))
        if not compilers or not versions:
            raise RuntimeError('Install a host C compiler, or supply --library with a built lod_native library')
        compiler, headers = compilers[-1], versions[-1]
        vc = compiler.parents[3]
        command = [str(compiler), '/nologo', '/std:c11', '/O2', '/LD',
                   f'/I{ROOT / "port"}', f'/I{vc / "include"}', f'/I{headers / "ucrt"}',
                   str(source), f'/Fo:{folder / "lod_native.obj"}', '/link', f'/OUT:{output}',
                   f'/LIBPATH:{vc / "lib/x64"}',
                   f'/LIBPATH:{kits / "Lib" / headers.name / "ucrt/x64"}',
                   f'/LIBPATH:{kits / "Lib" / headers.name / "um/x64"}']
    subprocess.run(command, cwd=folder, check=True, capture_output=True, text=True)
    return output


def native_library(path):
    library = ctypes.CDLL(str(Path(path).resolve()))
    function = library.p4_lod_simplify
    function.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_int,
                         ctypes.POINTER(ctypes.c_ushort), ctypes.c_int, ctypes.c_int]
    function.restype = ctypes.c_int
    return library


def source_bytes(g):
    """The PSP hashes the same vertex positions and ordered source topology."""
    data = bytearray(struct.pack('<III', g['nverts'], len(g['meshes']), g['strip']))
    data += g['verts']
    for mesh in g['meshes']:
        if any(i < 0 or i >= g['nverts'] for i in mesh):
            raise ValueError('source index outside vertex array')
        data += struct.pack('<I', len(mesh))
        data += struct.pack(f'<{len(mesh)}H', *mesh)
    return bytes(data)


def key_for(data):
    a, b = 0x811C9DC5, 0x9E3779B9
    for byte in data:
        a = ((a ^ byte) * 16777619) & 0xFFFFFFFF
        b = ((b ^ byte) * 2246822519) & 0xFFFFFFFF
    return a, b


def make_lods(g, library, percentages):
    nv = g['nverts']
    if len(g['verts']) != nv * 12:
        raise ValueError('missing positions')
    floats = struct.unpack(f'<{nv * 3}f', g['verts'])
    if not all(map(math.isfinite, floats)):
        raise ValueError('non-finite position')
    positions = (ctypes.c_float * (nv * 3)).from_buffer_copy(g['verts'])
    original = [mesh_triangles(m, g['strip']) for m in g['meshes']]
    if any(not tris for tris in original):
        raise ValueError('empty mesh')
    blob = bytearray(struct.pack('<II', nv, len(original)))
    levels = []
    for percent in percentages:
        counts = {'percent': percent, 'triangles': 0, 'indices': 0}
        for tris in original:
            flat = [v for tri in tris for v in tri]
            indices = (ctypes.c_ushort * len(flat))(*flat)
            nt = library.p4_lod_simplify(positions, nv, indices, len(tris), percent)
            if nt < 1 or nt > len(tris):
                raise ValueError('simplifier returned invalid size')
            simplified = [tuple(indices[i:i + 3]) for i in range(0, nt * 3, 3)]
            strips = best_strips(simplified, 1, random.Random(0))
            whole = join(strips)
            if not same_triangles(simplified, [whole]):
                raise ValueError('strip changed simplified topology')
            blob += struct.pack(f'<I{len(whole)}H', len(whole), *whole)
            blob += b'\0' * (-len(blob) % 4)
            counts['triangles'] += nt
            counts['indices'] += len(whole)
        levels.append(counts)
    if len(blob) > MAX_ENTRY:
        raise ValueError('entry exceeds PSP cache limit')
    return bytes(blob), levels


def write_pack(path, entries):
    rows = sorted(entries.items())
    if len(rows) > 32768:
        raise ValueError('too many pack entries')
    offset = 16 + 16 * len(rows)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('wb') as f:
        f.write(struct.pack('<IIII', MAGIC, 1, len(rows), offset))
        for key, blob in rows:
            f.write(struct.pack('<IIII', *key, offset, len(blob)))
            offset += len(blob)
        for _, blob in rows:
            f.write(blob)
    temporary.replace(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=ISO)
    ap.add_argument('--out', type=Path, default=DIST / 'p4lod.bin')
    ap.add_argument('--library', type=Path)
    ap.add_argument('--percent', type=int, nargs=3, default=[70, 40, 15])
    ap.add_argument('--min-vertices', type=int, default=500)
    ap.add_argument('--limit', type=int, default=0, help='maximum unique eligible geometries; 0 means all')
    ap.add_argument('--path', default='', help='only ISO paths containing this substring (case insensitive)')
    args = ap.parse_args()
    if not (100 > args.percent[0] > args.percent[1] > args.percent[2] > 0):
        ap.error('percentages must decrease between 99 and 1')
    if args.min_vertices < 1 or args.limit < 0:
        ap.error('invalid vertex minimum or limit')
    library_path = args.library or build_native(ROOT / 'build/lod_native')
    library = native_library(library_path)
    entries, seen, identities, report = {}, set(), {}, []
    started = time.monotonic()
    for name, data in iso_files(args.iso):
        if args.path.upper() not in name.upper():
            continue
        for g in geometries(data):
            if g['skin'] or g['nverts'] < args.min_vertices or not 1 <= len(g['meshes']) <= 32 or g['strip'] not in (0, 1):
                continue
            try:
                source = source_bytes(g)
                digest = hashlib.sha256(source).digest()
                if digest in seen:
                    continue
                seen.add(digest)
                key = key_for(source)
                if key in identities and identities[key] != digest:
                    raise RuntimeError('LOD key collision; pack generation stopped')
                identities[key] = digest
                blob, levels = make_lods(g, library, args.percent)
                original_indices = sum(map(len, g['meshes']))
                record = dict(path=name, key='%08x%08x' % key, vertices=g['nverts'],
                              meshes=len(g['meshes']), source_indices=original_indices,
                              source_triangles=sum(len(mesh_triangles(m, g['strip'])) for m in g['meshes']), levels=levels)
                # A level that costs more indices cannot improve this first-stage path.
                if any(l['indices'] >= original_indices for l in levels):
                    record['excluded'] = 'not every level reduces submitted indices'
                else:
                    entries[key] = blob
                report.append(record)
            except ValueError as error:
                report.append(dict(path=name, excluded=str(error)))
            if len(report) % 10 == 0:
                print(f'{len(report)} candidates, {len(entries)} retained, {time.monotonic()-started:.1f}s', flush=True)
            if args.limit and len(report) >= args.limit:
                break
        if args.limit and len(report) >= args.limit:
            break
    write_pack(args.out, entries)
    args.out.with_suffix('.json').write_text(json.dumps(dict(entries=len(entries),
        percentages=args.percent, candidates=report), indent=2), encoding='utf-8')
    print(f'{args.out}: {len(entries)} entries, {args.out.stat().st_size} bytes, {time.monotonic()-started:.1f}s')


if __name__ == '__main__':
    main()
