/* PS2 textures reach the GS as if they were 32-bit (PSMT8) or 16-bit (PSMT4) pictures at half width and height, so
   the file holds them in the order GS memory would. These give, for pixel x, y, where it is in such a picture that
   is rect_w wide. Tested by tools/test_swizzle.py. */
#ifndef SWIZZLE_H
#define SWIZZLE_H

/* Which of the 8 by 8 places of a block the pixel takes: the same for both formats. */
static inline int swizzle_x(int x, int y) { return (x + (((y + 2) >> 2 & 1) << 2)) & 7; }
static inline int swizzle_y(int y) { return (((y & 12) >> 1) + (y & 1)) & 7; }

/* Byte offset of a PSMT8 pixel in a PSMCT32 picture. */
static inline int swizzle8(int x, int y, int rect_w)
{
    int column = ((x & ~15) >> 1) + swizzle_x(x, y), row = ((y & ~15) >> 1) + swizzle_y(y);
    return (row * rect_w + column) * 4 + (y >> 1 & 1) + (x >> 2 & 2);
}

/* Nibble offset of a PSMT4 pixel in a PSMCT16 picture. */
static inline int swizzle4(int x, int y, int rect_w)
{
    int column = ((x & ~31) >> 1) + swizzle_x(x, y) + (x >> 1 & 8), row = ((y & ~15) >> 1) + swizzle_y(y);
    return (row * rect_w + column) * 4 + (y >> 1 & 1) + (x >> 2 & 2);
}

/* The PSP GE's own swizzle: 16-byte by 8-row blocks left to right, rows of blocks top to bottom, each block one
   texture-cache line. wbytes (a multiple of 16) is the padded row in bytes. Tested by tools/test_ge_swizzle.c. */
static inline int ge_swizzle_offset(int xbyte, int y, int wbytes)
{
    return ((y >> 3) * (wbytes >> 4) + (xbyte >> 4)) * 128 + (y & 7) * 16 + (xbyte & 15);
}

/* linear (stride bytes a row, rows rows) -> swizzled (wbytes by padded rows, padding zero), or back with to_ge 0 */
static inline void ge_swizzle(unsigned char *swizzled, unsigned char *linear, int stride, int rows, int wbytes, int padded_rows, int to_ge)
{
    for (int y = 0; y < padded_rows; y++)
        for (int x = 0; x < wbytes; x++) {
            unsigned char *s = &swizzled[ge_swizzle_offset(x, y, wbytes)];
            int inside = y < rows && x < stride;
            if (to_ge)
                *s = inside ? linear[y * stride + x] : 0;
            else if (inside)
                linear[y * stride + x] = *s;
        }
}

#endif
