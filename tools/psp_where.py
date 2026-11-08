#!/usr/bin/env python
"""Which data words and which retail code point at an address? Debug aid for what Ghidra's xrefs miss.

  python tools/psp_where.py 3fe700 3fe470   # retail addresses in hex
"""
import struct, sys
from psp_data_census import BASE, CODE, END_DATA, inside, load
from psp_make_data import START
from psp_xref import s16

NEAR = 6


def main():
    image, _, _ = load()
    wanted = {int(a, 16) for a in sys.argv[1:]}
    for va in range(START, END_DATA, 4):
        v = struct.unpack_from("<I", image, va - BASE)[0]
        if v in wanted:
            around = struct.unpack_from(f"<{2 * NEAR + 1}I", image, va - NEAR * 4 - BASE)
            print(f"data {va:08x} = {v:08x}: " + " ".join(f"{w:x}" for w in around))
    for a, b in CODE:
        hi = {}
        for va in range(a, b, 4):
            w = struct.unpack_from("<I", image, va - BASE)[0]
            op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
            if op == 15:
                hi[rt] = (w & 0xFFFF) << 16
            elif op == 9 and rs in hi and hi[rs] + s16(w & 0xFFFF) in wanted:
                print(f"code {va:08x} forms {hi[rs] + s16(w & 0xFFFF):08x}")
            elif op == 3 and (w & 0x3FFFFFF) << 2 in wanted:
                print(f"code {va:08x} calls {(w & 0x3FFFFFF) << 2:08x}")


if __name__ == "__main__":
    main()
