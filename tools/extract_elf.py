#!/usr/bin/env python
"""Pull SLUS_217.82 out of the player's ISO into build/ (read-only on the ISO, game data never ships).

  python tools/extract_elf.py "E:/.../Persona 4 (USA).iso"
"""
import hashlib, struct, sys
from pathlib import Path

SECTOR = 2048
NAME = b"SLUS_217.82;1"
SHA1_PREFIX = "4eeec036"  # docs/port/host_build_census.md section 4
OUT = Path(__file__).resolve().parent.parent / "build" / "SLUS_217.82"


def extract(iso):
    with open(iso, "rb") as f:
        f.seek(16 * SECTOR)
        pvd = f.read(SECTOR)
        assert pvd[1:6] == b"CD001", "not an ISO9660 image"
        lba, size = struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]
        f.seek(lba * SECTOR)
        root = f.read(size)
        pos = 0
        while pos < len(root):
            n = root[pos]
            if n == 0:  # records never cross a sector: skip the padding
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            if root[pos + 33:pos + 33 + root[pos + 32]] == NAME:
                f.seek(struct.unpack_from("<I", root, pos + 2)[0] * SECTOR)
                return f.read(struct.unpack_from("<I", root, pos + 10)[0])
            pos += n
    raise SystemExit("SLUS_217.82 not in the root directory")


def main():
    data = extract(sys.argv[1])
    sha = hashlib.sha1(data).hexdigest()
    assert sha.startswith(SHA1_PREFIX), f"unexpected executable {sha}"
    OUT.write_bytes(data)
    print(f"{OUT} {len(data)} bytes sha1 {sha}")


if __name__ == "__main__":
    main()
