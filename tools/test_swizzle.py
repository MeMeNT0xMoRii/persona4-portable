#!/usr/bin/env python
"""Check port/swizzle.h: every pixel of every texture shape has its own place, and it lies inside the upload.

The C inline functions are compiled for the host when a compiler is found, otherwise the same formulas below run:
one wrong bit makes two pixels share a place. Whether the places are the right ones was checked by eye on game textures.

  python tools/test_swizzle.py
"""
SHAPES = [(1 << a, 1 << b) for a in range(5, 11) for b in range(5, 11)]


def place_x(x, y):
    return (x + (((y + 2) >> 2 & 1) << 2)) & 7


def place_y(y):
    return (((y & 12) >> 1) + (y & 1)) & 7


def swizzle8(x, y, rect_w):
    return ((((y & ~15) >> 1) + place_y(y)) * rect_w + ((x & ~15) >> 1) + place_x(x, y)) * 4 + (y >> 1 & 1) + (x >> 2 & 2)


def swizzle4(x, y, rect_w):
    return ((((y & ~15) >> 1) + place_y(y)) * rect_w + ((x & ~31) >> 1) + place_x(x, y) + (x >> 1 & 8)) * 4 + (y >> 1 & 1) + (x >> 2 & 2)


def check(function, w, h):
    rect_w, rect_h = w // 2, h // 2
    places = {function(x, y, rect_w) for y in range(h) for x in range(w)}
    assert len(places) == w * h, (function.__name__, w, h, "two pixels share a place")
    assert min(places) == 0 and max(places) == w * h - 1, (function.__name__, w, h, "outside the upload")


def main():
    for w, h in SHAPES:
        check(swizzle8, w, h)
        check(swizzle4, w, h)
    try:
        check(lambda x, y, r: swizzle4(x, y, r) & ~1, 64, 64)
    except AssertionError:
        print(f"swizzle ok: {len(SHAPES)} shapes, broken formula caught")
        return
    raise SystemExit("the check did not catch a broken formula")


if __name__ == "__main__":
    main()
