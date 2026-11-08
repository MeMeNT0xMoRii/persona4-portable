#!/usr/bin/env python
"""Copy the game's .CVM containers from the player's own ISO, cut them into 1 GB parts and index every file
inside them (game data: never ship it). PPSSPP cannot seek past 2 GB in a file, so no part is bigger than that.

Each CVM is a 0x1800-byte header, then an ISO9660 image. The index is sorted by the hash port/files.c looks up:
three words per file: hash of the upper-case path, container number + sector * 8 of the start, size in bytes.

  python tools/psp_disc.py DATA BTL   # containers to copy; the index always covers those on disk
"""
import struct, sys
from pathlib import Path
from psp_data_census import ROOT

ISO = ROOT / "Shin Megami Tensei - Persona 4 (USA).iso"
DIST = ROOT / "build" / "psp_dist" / "PSP" / "GAME" / "P4"
SECTOR = 0x800
CVM_BASE = 0x1800
PART = 1 << 30  # keep equal to PART in port/files.c
WHOLE = ROOT / "build" / "psp_disc"
CONTAINERS = ("DATA", "BTL", "ENV", "BGM", "ZZZZZ")  # keep the order of port/files.c


def walk(f, base, lba, size, prefix=""):
    """Every (path, lba, size) below a directory; lba is relative to base."""
    pos = 0
    while pos < size:
        f.seek(base + lba * SECTOR + pos)
        length = f.read(1)[0]
        if length == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        f.seek(base + lba * SECTOR + pos)
        rec = f.read(length)
        ext, data = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        flags, nlen = rec[25], rec[32]
        name = rec[33:33 + nlen]
        pos += length
        if name in (b"\0", b"\1"):
            continue
        text = name.decode("ascii").split(";")[0]
        if flags & 2:
            yield from walk(f, base, ext, data, prefix + text + "/")
        else:
            yield prefix + text, ext, data


def root(f, base):
    f.seek(base + 16 * SECTOR)
    pvd = f.read(SECTOR)
    assert pvd[1:6] == b"CD001", f"no ISO9660 at {base:#x}"
    return struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]


def path_hash(path):
    """FNV-1a, the same in port/files.c."""
    h = 0x811C9DC5
    for c in path.upper().encode("ascii"):
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    DIST.mkdir(parents=True, exist_ok=True)
    WHOLE.mkdir(parents=True, exist_ok=True)
    with ISO.open("rb") as iso:
        top = {p: (lba, size) for p, lba, size in walk(iso, 0, *root(iso, 0))}
        for name in sys.argv[1:]:
            lba, size = top[name + ".CVM"] if name + ".CVM" in top else top[name + ".BIN"]  # ZZZZZ is a .BIN on the disc
            target = WHOLE / (name + ".CVM")
            if target.exists() and target.stat().st_size == size:
                continue
            print(f"copying {name}.CVM {size >> 20} MB")
            iso.seek(lba * SECTOR)
            with target.open("wb") as out:
                while size:
                    chunk = iso.read(min(size, 1 << 24))
                    out.write(chunk)
                    size -= len(chunk)
    rows = {}
    for number, name in enumerate(CONTAINERS):
        target = WHOLE / (name + ".CVM")
        if not target.exists():
            continue
        with target.open("rb") as f:
            for part in range(f.seek(0, 2) // PART + 1):
                out = DIST / f"{name}.{part}"
                if not out.exists():
                    f.seek(part * PART)
                    out.write_bytes(f.read(PART))
            for path, lba, size in walk(f, CVM_BASE, *root(f, CVM_BASE)):
                h = path_hash(path)
                assert rows.get(h, (path,))[0] == path, f"hash collision {path}"
                rows.setdefault(h, (path, number + (lba + CVM_BASE // SECTOR) * 8, size))  # first container wins
    table = sorted((h, where, size) for h, (_, where, size) in rows.items())
    (DIST / "p4files.idx").write_bytes(b"".join(struct.pack("<3I", *row) for row in table))
    print(f"{len(table)} files indexed")


if __name__ == "__main__":
    main()
