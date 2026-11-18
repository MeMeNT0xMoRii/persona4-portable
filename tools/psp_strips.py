#!/usr/bin/env python
"""Offline triangle strips for the skinned models: the player's own ISO -> p4strips.bin (game data: never ship it).

The PSP's GE transforms a vertex for every index it reads, so a skinned batch costs by its index count. The port
builds each batch's strip on the PSP (greedy, no swaps: ~2.33 indices a triangle, ~0.24 s when a party appears).
This tool does the same work on the PC with a better stripifier and writes the result where port/rwsky.c looks it
up by the batch's own index list (p4_strip_pack 1). Nothing is made, moved or dropped: a strip is kept only when it
unrolls to exactly the batch's triangles with their winding, and the PSP checks that again before it uses one.

  python tools/psp_strips.py                       # every skinned geometry of the ISO -> build/psp_dist/.../p4strips.bin
  python tools/psp_strips.py --measure --every 20  # numbers only, a sample
  python tools/psp_strips.py --static              # also what the static meshes would gain (numbers only)
  python tools/psp_strips.py --lists build/strip_lists.bin   # also the batches' index lists, for tools/test_strippack.c

How a batch is found again on the PSP: the port cuts a mesh into batches of at most 8 bones (skin_geometry in
port/rwsky.c: first-fit for opaque meshes, in order for the others) and numbers a batch's vertices by first use, so
the batch's index list depends on nothing but the mesh. Both cuts are made here and each batch is filed under the
FNV-1a hash of its list. A batch the PSP cuts differently is simply not found and takes the PSP's own strip.

File: 'P4SP' 1 count data_offset, count x (key, corners, offset) sorted by key then corners, then per batch
u16 strips, u16 total, u16 length[strips], u16 index[total], padded to 4 bytes. The strips are in the order and
direction that joins them cheapest; port/strippack.h joins them (or draws them one PRIM each, p4_strip_prims 1).
"""
import argparse
import collections
import hashlib
import heapq
import multiprocessing
import os
import random
import struct
import sys
import time
from pathlib import Path

from psp_disc import walk, root, ISO, SECTOR, CVM_BASE, DIST

PALETTE = 8  # SKIN_PALETTE and p4_skin_palette in port/rwsky.c
SKIP = {'ADX', 'AHX', 'SFD', 'ACX', 'SE', 'AFS'}  # sound and movies


# ---- RenderWare: every Geometry chunk of a file -------------------------------------------------------------------

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


def chunks(d, o, end):
    while o + 12 <= end:
        kind, size, stamp = struct.unpack_from('<III', d, o)
        if o + 12 + size > end:
            return
        yield kind, o + 12, size, stamp
        o += 12 + size


def geometries(d):
    """Geometry chunks found by their header (type 0x0F holding a struct chunk with the same library stamp)."""
    o = 0
    while True:
        o = d.find(b'\x0f\x00\x00\x00', o)
        if o < 0 or o + 24 > len(d):
            return
        kind, size, stamp, kind2, size2, stamp2 = struct.unpack_from('<IIIIII', d, o)
        g = None
        if kind2 == 1 and stamp == stamp2 and stamp and o + 12 + size <= len(d) and 16 <= size2 <= size - 12:
            try:
                g = parse_geometry(d, o + 12, size)
            except (struct.error, StopIteration, IndexError):
                g = None
        if g:
            yield g
            o += 12 + size
        else:
            o += 1


def parse_geometry(d, o, size):
    end = o + size
    _, so, ss, stamp = next(chunks(d, o, end))
    p = so
    fmt, ntri, nv, nmorph = struct.unpack_from('<IIII', d, p)
    p += 16
    if nv > 65535 or nmorph != 1 or ntri > 200000 or fmt & 0x01000000:  # native (pre-instanced) data: the port skips it too
        return None
    version = ((stamp >> 14) & 0x3ff00) + 0x30000 | ((stamp >> 16) & 0x3f) if stamp & 0xffff0000 else stamp << 8
    if version < 0x34000:
        p += 12  # surface properties
    if fmt & 8:
        p += nv * 4
    sets = (fmt >> 16) & 0xff or (2 if fmt & 0x80 else 1 if fmt & 4 else 0)
    p += sets * nv * 8 + ntri * 8 + 16
    has_verts, has_normals = struct.unpack_from('<II', d, p)
    p += 8
    if has_verts > 1 or has_normals > 1:
        return None
    verts = d[p:p + nv * 12 * has_verts]
    p += nv * 12 * (has_verts + has_normals)
    if p != so + ss:
        return None
    g = dict(nverts=nv, verts=verts, meshes=None, strip=0, skin=None)
    for kind, co, cs, _ in chunks(d, so + ss, end):
        if kind != 3:  # extension
            continue
        for ek, eo, es, _ in chunks(d, co, co + cs):
            if ek == 0x50e:  # bin mesh
                flags, nm, _ = struct.unpack_from('<III', d, eo)
                q = eo + 12
                meshes = []
                for _ in range(nm):
                    ni, _ = struct.unpack_from('<II', d, q)
                    meshes.append(struct.unpack_from('<%dI' % ni, d, q + 8))
                    q += 8 + ni * 4
                g['meshes'], g['strip'] = meshes, flags & 0xf
            elif ek == 0x116:  # skin
                bones, used, maxw = d[eo], d[eo + 1], d[eo + 2]
                q = eo + 4 + used
                g['skin'] = dict(bones=bones, maxw=maxw, indices=d[q:q + nv * 4], weights=d[q + nv * 4:q + nv * 20])
    return g if g['meshes'] is not None else None


# ---- triangles, as skin_geometry sees them --------------------------------------------------------------------------

def mesh_triangles(indices, strip):
    """the mesh's triangles: strips unrolled (odd ones wound the other way), degenerate ones left out"""
    out = []
    if strip == 1:
        for t in range(len(indices) - 2):
            a, b, c = indices[t + (t & 1)], indices[t + 1 - (t & 1)], indices[t + 2]
            if a != b and b != c and a != c:
                out.append((a, b, c))
    else:
        for t in range(0, len(indices) - 2, 3):
            out.append(tuple(indices[t:t + 3]))
    return out


def vertex_bones(g):
    """a bit per bone of each vertex (vertex_bones in port/rwsky.c, with every bone of the skin in the hierarchy)"""
    skin, nv = g['skin'], g['nverts']
    index, n = skin['indices'], skin['bones']
    weight = struct.unpack('<%df' % (nv * 4), skin['weights'])
    out = []
    for i in range(nv):
        mask = 0
        if skin['maxw'] == 1:
            if index[i * 4] < n:
                mask = 1 << index[i * 4]
        else:
            for k in range(min(skin['maxw'], 4)):
                if weight[i * 4 + k] > 0 and index[i * 4 + k] < n:
                    mask |= 1 << index[i * 4 + k]
        out.append(mask)
    return out


def batches(bones, regroup):
    """the port's cut of a mesh into batches of at most PALETTE bones: lists of triangle numbers"""
    n = len(bones)
    done = bytearray(n)
    out = []
    for first in range(n):
        if done[first]:
            continue
        palette, batch = 0, []
        for t in range(first, n):
            if done[t]:
                continue
            if (palette | bones[t]).bit_count() > PALETTE:
                if regroup:
                    continue
                break
            palette |= bones[t]
            done[t] = 1
            batch.append(t)
        out.append(batch)
    return out


def local_list(tris):
    """the batch's index list as skin_batch numbers it: vertices by first use"""
    remap, out = {}, []
    for tri in tris:
        for v in tri:
            out.append(remap.setdefault(v, len(remap)))
    return out


# ---- strips -----------------------------------------------------------------------------------------------------------
# A strip is a list of indices; triangle k is (i[k], i[k+1], i[k+2]), wound the other way for odd k.

def canon(a, b, c):
    if b < a and b < c:
        return b, c, a
    if c < a and c < b:
        return c, a, b
    return a, b, c


def strip_triangles(s):
    out = []
    for k in range(len(s) - 2):
        a, b, c = s[k], s[k + 1], s[k + 2]
        if a != b and b != c and a != c:
            out.append(canon(b, a, c) if k & 1 else canon(a, b, c))
    return out


def same_triangles(tris, strips):
    """the acceptance test: the strips draw exactly these triangles, each as often and wound as in the list"""
    return sorted(canon(*t) for t in tris) == sorted(x for s in strips for x in strip_triangles(s))


class Stripper:
    """Greedy strips with swaps. From the triangle with the fewest unused neighbours a strip goes on through shared
    edges (same winding only); entering a triangle it may leave by either other edge, the second one at the price
    of one repeated index (a swap) instead of ending the strip. Each start is tried from its three corners and
    then carried on backwards; the longest result is kept. rng: ties broken at random (for several runs)."""

    def __init__(self, tris, rng=None):
        self.tris = tris
        self.rng = rng
        self.edge = {}
        for t, (a, b, c) in enumerate(tris):
            for p, q in ((a, b), (b, c), (c, a)):
                self.edge.setdefault(p << 16 | q, []).append(t)
        self.used = bytearray(len(tris))

    def find(self, x, y, odd, taken):
        for u in self.edge.get((y << 16 | x) if odd else (x << 16 | y), ()):
            if not self.used[u] and u not in taken:
                return u
        return -1

    def free(self, t, taken):
        a, b, c = self.tris[t]
        n = 0
        for p, q in ((b, a), (c, b), (a, c)):
            for u in self.edge.get(p << 16 | q, ()):
                if not self.used[u] and u not in taken and u != t:
                    n += 1
        return n

    def grow(self, out, taken):
        while True:
            x, y = out[-2], out[-1]
            odd = (len(out) - 2) & 1
            n = self.find(x, y, odd, taken)
            if n < 0:
                return
            a, b, c = self.tris[n]
            r = a if a != x and a != y else b if b != x and b != y else c
            taken.add(n)
            straight = self.find(y, r, odd ^ 1, taken)  # after ..., x, y, r
            swapped = self.find(x, r, odd, taken)       # after ..., x, y, x, r
            if swapped >= 0:
                if straight < 0:
                    out.append(x)
                else:
                    fs, fw = self.free(straight, taken), self.free(swapped, taken)
                    if fw < fs or (fw == fs and self.rng and self.rng.random() < 0.3):
                        out.append(x)
            out.append(r)

    def from_corner(self, t, corner):
        a, b, c = self.tris[t]
        out = list((a, b, c, a, b)[corner:corner + 3])
        taken = {t}
        self.grow(out, taken)
        # the other end: backwards every winding flips, and so does an odd shift of position, so an even length is
        # the same triangles as it stands and an odd one needs one more index in front
        back = out[::-1]
        if len(back) & 1:
            back.insert(0, back[0])
        n = len(taken)
        self.grow(back, taken)
        return (back if len(taken) > n else out), taken

    def run(self):
        rnd = self.rng.random if self.rng else (lambda: 0)
        heap = [(self.free(t, ()), rnd(), t) for t in range(len(self.tris))]
        heapq.heapify(heap)
        strips = []
        while heap:
            n, _, t = heapq.heappop(heap)
            if self.used[t]:
                continue
            now = self.free(t, ())
            if now != n:
                heapq.heappush(heap, (now, rnd(), t))
                continue
            best = None
            for corner in range(3):
                out, taken = self.from_corner(t, corner)
                if best is None or len(taken) * 4 - len(out) > len(best[1]) * 4 - len(best[0]):
                    best = out, taken
            for u in best[1]:
                self.used[u] = 1
            strips.append(best[0])
        return strips


def join(strips):
    """One strip from many, exactly as sp_join in port/strippack.h: a strip that starts with the index the one before
    ended on follows it directly (after one more copy when it would land on an odd position: its first triangle must
    keep its winding); otherwise the last index and the next first one are repeated (zero-area triangles)."""
    out = []
    for s in strips:
        if out and out[-1] != s[0]:
            out += [out[-1], s[0]]
        if len(out) & 1:
            out.append(s[0])
        out += s
    return out


def spellings(s):
    """the ways one strip can be written: as it is, backwards, a lone triangle from any corner"""
    if len(s) == 3:
        return [s, [s[1], s[2], s[0]], [s[2], s[0], s[1]]]
    back = s[::-1]
    if len(back) & 1:
        back.insert(0, back[0])
    return [s, back]


def arrange(strips):
    """order and direction for the cheapest join: greedily, the next strip is one that starts where the last ended"""
    forms = [spellings(s) for s in strips]
    starts = {}
    for i, group in enumerate(forms):
        for k, form in enumerate(group):
            starts.setdefault(form[0], []).append((i, k))
    left = set(range(len(strips)))
    longest = sorted(left, key=lambda i: -len(strips[i]))
    at = 0

    def followers(v, skip):
        return sum(1 for i, _ in starts.get(v, ()) if i in left and i != skip)

    out, last = [], None
    while left:
        best = None
        for i, k in starts.get(last, ()) if last is not None else ():
            if i in left:
                form = forms[i][k]
                rank = (len(form) - len(strips[i]), -followers(form[-1], i), -len(form))
                if best is None or rank < best[0]:
                    best = rank, i, k
        if best is None:
            while longest[at] not in left:
                at += 1
            i = longest[at]
            k = min(range(len(forms[i])), key=lambda k: (len(forms[i][k]) - len(strips[i]), -followers(forms[i][k][-1], i)))
            best = 0, i, k
        _, i, k = best
        left.discard(i)
        out.append(forms[i][k])
        last = forms[i][k][-1]
    return out


def best_strips(tris, runs, rng):
    """the arrangement with the shortest joined strip over several runs (the first without random ties)"""
    best = None
    for run in range(runs):
        strips = arrange(Stripper(tris, rng if run else None).run())
        n = len(join(strips))
        if best is None or n < best[0]:
            best = n, strips
    return best[1]


def strip_batch(job):
    """one batch (its index list as bytes, runs) -> (list, strips or None when they do not match, joined length)"""
    data, runs = job
    local = struct.unpack('<%dH' % (len(data) // 2), data)
    tris = [local[k:k + 3] for k in range(0, len(local), 3)]
    strips = best_strips(tris, runs, random.Random(fnv1a(data)))  # the same file from the same disc, whatever the order
    joined = join(strips)
    if not same_triangles(tris, strips) or not same_triangles(tris, [joined]):
        return data, None, 0
    return data, strips, len(joined)


# ---- the pack ---------------------------------------------------------------------------------------------------------

def fnv1a(data):
    h = 0x811C9DC5
    for c in data:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def write_pack(path, entries):
    """entries: {list bytes: strips}"""
    rows, blob, seen, clashes = [], bytearray(), set(), 0
    for data, strips in entries.items():
        key, corners = fnv1a(data), len(data) // 2
        if (key, corners) in seen:  # two different lists under one key: the PSP's check would refuse the second
            clashes += 1
            continue
        seen.add((key, corners))
        rows.append((key, corners, len(blob)))
        total = sum(map(len, strips))
        assert len(strips) < 65536 and total < 65536
        blob += struct.pack('<HH%dH' % len(strips), len(strips), total, *map(len, strips))
        for s in strips:
            blob += struct.pack('<%dH' % len(s), *s)
        blob += b'\0' * (-len(blob) % 4)
    rows.sort()
    base = 16 + len(rows) * 12
    with open(path, 'wb') as f:
        f.write(struct.pack('<IIII', 0x50533450, 1, len(rows), base))
        for key, corners, offset in rows:
            f.write(struct.pack('<III', key, corners, base + offset))
        f.write(blob)
    return len(rows), base + len(blob), clashes


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso', type=Path, default=ISO)
    ap.add_argument('--out', type=Path, default=DIST / 'p4strips.bin')
    ap.add_argument('--runs', type=int, default=4, help='stripifier runs a batch, the best kept')
    ap.add_argument('--every', type=int, default=1, help='only every Nth skinned geometry (a sample)')
    ap.add_argument('--measure', action='store_true', help='numbers only, no file')
    ap.add_argument('--static', action='store_true', help='also measure the static meshes (whole mesh, numbers only)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    ap.add_argument('--lists', type=Path, help="also write the batches' index lists (the host check reads them; game data)")
    args = ap.parse_args()
    started = time.time()
    seen, entries, jobs = set(), {}, {}
    n = collections.Counter()
    for name, data in iso_files(args.iso):
        n['files'] += 1
        for g in geometries(data):
            n['geometries'] += 1
            digest = hashlib.sha1(g['verts'] + repr(g['meshes']).encode() + (g['skin']['indices'] + g['skin']['weights'] if g['skin'] else b'')).digest()
            if digest in seen:
                continue
            seen.add(digest)
            n['unique geometries'] += 1
            if not g['skin']:
                if args.static:
                    for indices in g['meshes']:
                        tris = mesh_triangles(indices, g['strip'])
                        if len(tris) < 2:
                            continue
                        strips = arrange(Stripper(tris).run())
                        assert same_triangles(tris, strips)
                        n['static triangles'] += len(tris)
                        n['static game indices'] += len(indices)
                        n['static joined'] += min(len(join(strips)), len(indices))
                        n['static separate'] += sum(map(len, strips))
                        n['static strips'] += len(strips)
                continue
            n['skinned'] += 1
            if n['skinned'] % args.every:
                continue
            masks = vertex_bones(g)
            meshes = [mesh_triangles(indices, g['strip']) for indices in g['meshes']]
            bones = [[masks[a] | masks[b] | masks[c] for a, b, c in tris] for tris in meshes]
            if any(x.bit_count() > PALETTE for mesh in bones for x in mesh):
                n['geometries left to the CPU (a triangle on more than 8 bones)'] += 1
                continue
            n['triangles'] += sum(map(len, meshes))
            n['game indices'] += sum(map(len, g['meshes']))
            for tris, tb in zip(meshes, bones):
                cuts = {tuple(map(tuple, batches(tb, regroup))) for regroup in (True, False)}
                n['meshes'] += 1
                n['meshes cut two ways'] += len(cuts) > 1
                for which, cut in enumerate(cuts):
                    for batch in cut:
                        if len(batch) < 2:  # under 6 corners the port keeps the list
                            continue
                        mine = [tris[t] for t in batch]
                        local = local_list(mine)
                        data = struct.pack('<%dH' % len(local), *local)
                        if data in jobs:
                            n['batches seen before'] += 1
                            continue
                        jobs[data] = name
        if n['files'] % 500 == 0:
            print(f"{n['files']} files, {n['unique geometries']} geometries, {len(jobs)} batches, {time.time() - started:.0f} s", flush=True)
    print(f'{len(jobs)} batches to strip, {time.time() - started:.0f} s', flush=True)
    work = [(data, args.runs) for data in jobs]  # in the order found: a geometry's batches lie together in the file
    # By module name, not __main__: a frozen Prepare P4.exe does not rebuild __main__ in its pool workers, which then
    # fail to unpickle the task and the pool hangs (b389).
    import psp_strips
    with multiprocessing.Pool(args.jobs) as pool:
        for data, strips, joined in pool.imap(psp_strips.strip_batch, work, chunksize=8):
            if strips is None:
                raise SystemExit(f'{jobs[data]}: a strip does not match its batch')
            n['batches'] += 1
            n['batch triangles'] += len(data) // 6
            n['joined indices'] += joined
            n['separate indices'] += sum(map(len, strips))
            n['strips'] += len(strips)
            if joined >= len(data) // 2:
                n['batches left as lists'] += 1
                continue
            entries[data] = strips
    for key in sorted(n):
        print(f'{key}: {n[key]}')
    if n['batch triangles']:
        t = n['batch triangles']
        print(f"indices a triangle over {n['batches']} batches: joined {n['joined indices'] / t:.3f}, one PRIM a strip "
              f"{n['separate indices'] / t:.3f} ({n['strips'] / n['batches']:.1f} strips a batch); the game's own strips {n['game indices'] / n['triangles']:.3f}")
    if n['static triangles']:
        t = n['static triangles']
        print(f"static meshes: {t} triangles, the game's strips {n['static game indices'] / t:.3f} indices a triangle, "
              f"joined {n['static joined'] / t:.3f}, one PRIM a strip {n['static separate'] / t:.3f}")
    if not args.measure:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        count, size, clashes = write_pack(args.out, entries)
        print(f'{args.out}: {count} batches, {size} bytes, {clashes} key clashes left out')
        if args.lists:  # 'P4SL' count, then per batch u32 corners and the list, padded to 4 bytes
            kept, seen_keys = [], set()
            for data in entries:
                if (fnv1a(data), len(data) // 2) not in seen_keys:
                    seen_keys.add((fnv1a(data), len(data) // 2))
                    kept.append(data)
            with open(args.lists, 'wb') as f:
                f.write(struct.pack('<II', 0x4c533450, len(kept)))
                for data in kept:
                    f.write(struct.pack('<I', len(data) // 2) + data + b'\0' * (-len(data) % 4))
            print(f'{args.lists}: {len(kept)} lists')
    print(f'{time.time() - started:.0f} s')


if __name__ == '__main__':
    main()
