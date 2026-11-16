"""First frame of a decompressed PCSX2 GS dump: draw groups by render target and state.
usage: gs_frame.py file.gs [frames]"""
import struct
import sys
from collections import OrderedDict

data = open(sys.argv[1], 'rb').read()
want_frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1
assert struct.unpack_from('<I', data, 0)[0] == 0xFFFFFFFF
header_size = struct.unpack_from('<I', data, 4)[0]
state_version, state_size = struct.unpack_from('<II', data, 8)
pos = 8 + header_size
pos += state_size          # GS state (registers + VRAM)
pos += 8192                # privileged registers
REG_NAMES = {0x00: 'PRIM', 0x06: 'TEX0_1', 0x07: 'TEX0_2', 0x14: 'TEX1_1', 0x15: 'TEX1_2', 0x42: 'ALPHA_1', 0x43: 'ALPHA_2',
             0x47: 'TEST_1', 0x48: 'TEST_2', 0x4C: 'FRAME_1', 0x4D: 'FRAME_2', 0x4E: 'ZBUF_1', 0x4F: 'ZBUF_2',
             0x1A: 'PRMODECONT', 0x1B: 'PRMODE', 0x40: 'SCISSOR_1', 0x41: 'SCISSOR_2', 0x18: 'XYOFFSET_1', 0x19: 'XYOFFSET_2',
             0x50: 'BITBLTBUF', 0x51: 'TRXPOS', 0x52: 'TRXREG', 0x53: 'TRXDIR', 0x08: 'CLAMP_1', 0x09: 'CLAMP_2'}
regs = {}
prim = 0
prmodecont = 1
prmode = 0
groups = []      # list of dicts (consecutive draws with the same key)
vertices = 0
xy = []
uploads = []
rgba = 0


def key():
    p = prim if prmodecont else (prim & 0x7) | (prmode & ~0x7)
    ctxt = (p >> 9) & 1
    fr = regs.get('FRAME_2' if ctxt else 'FRAME_1', 0)
    return (ctxt, fr & 0x1FF, (fr >> 16) & 0x3F, fr >> 32, regs.get('TEST_2' if ctxt else 'TEST_1', 0) & 0x7FFFF,
            regs.get('ALPHA_2' if ctxt else 'ALPHA_1', 0), (regs.get('TEX0_2' if ctxt else 'TEX0_1', 0) & 0x3FFF) if (p >> 4) & 1 else -1,
            p & 7, (p >> 4) & 1, (p >> 3) & 1, regs.get('ZBUF_2' if ctxt else 'ZBUF_1', 0) >> 32 & 1)


def kick(x, y):
    global vertices
    k = key()
    if not groups or groups[-1]['key'] != k:
        groups.append({'key': k, 'v': 0, 'x0': 1e9, 'y0': 1e9, 'x1': -1e9, 'y1': -1e9})
    g = groups[-1]
    g['v'] += 1
    g.setdefault('rgba', set()).add(rgba)
    g['tex0'] = regs.get('TEX0_2' if k[0] else 'TEX0_1', 0)
    g['clamp'] = regs.get('CLAMP_2' if k[0] else 'CLAMP_1', 0)
    off = regs.get('XYOFFSET_2' if k[0] else 'XYOFFSET_1', 0)
    ox, oy = (off & 0xFFFF) / 16.0, (off >> 32 & 0xFFFF) / 16.0
    fx, fy = x / 16.0 - ox, y / 16.0 - oy
    g['x0'], g['y0'], g['x1'], g['y1'] = min(g['x0'], fx), min(g['y0'], fy), max(g['x1'], fx), max(g['y1'], fy)


def write(addr, value):
    global prim, prmodecont, prmode, rgba
    if addr == 0x01:
        rgba = value & 0xFFFFFFFF
    name = REG_NAMES.get(addr)
    if addr == 0x00:
        prim = value & 0x7FF
    elif addr == 0x1A:
        prmodecont = value & 1
    elif addr == 0x1B:
        prmode = value & 0x7FF
    elif addr in (0x04, 0x05, 0x0C, 0x0D):  # XYZF2 XYZ2 XYZF3 XYZ3
        if addr in (0x04, 0x05):
            kick(value & 0xFFFF, value >> 16 & 0xFFFF)
    elif addr == 0x53 and (value & 3) == 0:  # host -> local upload
        b = regs.get('BITBLTBUF', 0)
        r = regs.get('TRXREG', 0)
        uploads.append((len(groups), (b >> 32) & 0x3FFF, (b >> 56) & 0x3F, r & 0xFFF, r >> 32 & 0xFFF))
    if name:
        regs[name] = value


def gif(buf):
    i = 0
    n = len(buf)
    while i + 16 <= n:
        lo, hi = struct.unpack_from('<QQ', buf, i)
        i += 16
        nloop = lo & 0x7FFF
        pre = lo >> 46 & 1
        flg = lo >> 58 & 3
        nreg = lo >> 60 & 0xF or 16
        if pre and flg != 2:
            write(0x00, lo >> 47 & 0x7FF)
        regs_desc = [(hi >> (4 * k)) & 0xF for k in range(nreg)]
        if flg == 0:  # PACKED
            for _ in range(nloop):
                for r in regs_desc:
                    if i + 16 > n:
                        return
                    a, b = struct.unpack_from('<QQ', buf, i)
                    i += 16
                    if r == 0x0E:
                        write(b & 0xFF, a)
                    elif r == 0x00:
                        write(0x00, a & 0x7FF)
                    elif r in (0x04, 0x05):  # XYZF2 / XYZ2 packed: X bits 0-15, Y bits 32-47, ADC at bit 111
                        adc = b >> 47 & 1
                        if not adc:
                            kick(a & 0xFFFF, a >> 32 & 0xFFFF)
                    elif r == 0x01:  # RGBAQ packed
                        write(0x01, (a & 0xFF) | (a >> 32 & 0xFF) << 8 | (b & 0xFF) << 16 | (b >> 32 & 0xFF) << 24)
                    elif r in (0x06, 0x07):
                        write(r, a)
        elif flg == 1:  # REGLIST
            total = nloop * nreg
            for k in range(total):
                if i + 8 > n:
                    return
                v = struct.unpack_from('<Q', buf, i)[0]
                i += 8
                r = regs_desc[k % nreg]
                if r != 0x0E and r != 0x0F:
                    write({0x04: 0x04, 0x05: 0x05}.get(r, r), v)
            if total & 1:
                i += 8
        else:  # IMAGE
            i += nloop * 16


frames = 0
while pos < len(data) and frames < want_frames:
    t = data[pos]
    pos += 1
    if t == 0:
        path = data[pos]
        size = struct.unpack_from('<I', data, pos + 1)[0]
        pos += 5
        gif(data[pos:pos + size])
        pos += size
    elif t == 1:
        pos += 1
        frames += 1
    elif t == 2:
        pos += 4
    elif t == 3:
        pos += 8192
    else:
        break

targets = OrderedDict()
for g in groups:
    ctxt, fbp, fbw, fbmsk, test, alpha, tbp, ptype, tme, abe, zmsk = g['key']
    targets.setdefault((fbp, fbw), []).append(g)
print(f'groups {len(groups)}, uploads {len(uploads)}')
for (fbp, fbw), gs in targets.items():
    v = sum(g['v'] for g in gs)
    print(f'== target FBP {fbp:#x} (TBP {fbp * 32:#x}) FBW {fbw} ({fbw * 64} px): {len(gs)} groups, {v} vertices')
    for g in gs[:40]:
        ctxt, fbp_, fbw_, fbmsk, test, alpha, tbp, ptype, tme, abe, zmsk = g['key']
        print(f'   idx {groups.index(g):4d} prim {ptype} tex {tbp if tbp >= 0 else "-":>6} fbmsk {fbmsk:08x} test {test:05x} '
              f'alpha {alpha:x} abe {abe} zmsk {zmsk} v {g["v"]:5d} box {g["x0"]:.0f},{g["y0"]:.0f}-{g["x1"]:.0f},{g["y1"]:.0f}'
              f' rgba {" ".join(f"{c:08x}" for c in sorted(g["rgba"])[:4])}{" ..%d" % len(g["rgba"]) if len(g["rgba"]) > 4 else ""}')
small = {fbp * 32 for (fbp, fbw) in targets if fbw <= 4}
print('reads of small targets as textures:')
for idx, g in enumerate(groups):
    tbp = g['key'][6]
    if tbp in small:
        print(f'   idx {idx} target FBP {g["key"][1]:#x} reads TBP {tbp:#x} test {g["key"][4]:05x} alpha {g["key"][5]:x} '
              f'fbmsk {g["key"][3]:08x} v {g["v"]} box {g["x0"]:.0f},{g["y0"]:.0f}-{g["x1"]:.0f},{g["y1"]:.0f}'
              f' clamp {g["clamp"]:x} tcc {g["tex0"] >> 34 & 1} tfx {g["tex0"] >> 35 & 3} psm {g["tex0"] >> 20 & 0x3f:#x} rgba {" ".join(f"{c:08x}" for c in sorted(g["rgba"])[:9])}')
