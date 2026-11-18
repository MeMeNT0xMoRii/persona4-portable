/* Host check for ge_swizzle in port/swizzle.h: gcc -I port tools/test_ge_swizzle.c -o build/test_ge_swizzle && build/test_ge_swizzle */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "swizzle.h"

static void check_legacy_map_texture(void)
{
    enum { SOURCE = 1024, DEST = 512, STRIDE = 528 };
    static unsigned char source[SOURCE * SOURCE], dest[STRIDE * DEST];
    memset(dest, 0x55, sizeof dest);
    /* The downsample keeps every even texel and drops the odd rows and columns. */
    for (int y = 0; y < SOURCE; ++y)
        for (int x = 0; x < SOURCE; ++x)
            source[swizzle8(x, y, SOURCE / 2)] = (x * 11 + y * 17) % 255;
    ps2_index8_reduce(dest, STRIDE, DEST, DEST, source, SOURCE, SOURCE);
    for (int y = 0; y < DEST; ++y) {
        for (int x = 0; x < DEST; ++x)
            assert(dest[y * STRIDE + x] == (x * 22 + y * 34) % 255);
        assert(dest[y * STRIDE + DEST] == 0x55);
    }
}

/* Compare each byte against its GE address. A round trip alone would also
   pass if both directions made the same addressing mistake. */
static void check_texture_shapes(void)
{
    unsigned char input[129 * 35 + 4], encoded[144 * 40 + 4], decoded[sizeof input];
    for (unsigned i = 0; i < sizeof input; i++)
        input[i] = (unsigned char)(i * 31 + (i >> 6));
    for (int stride = 1; stride <= 129; stride++) {
        for (int rows = 1; rows <= 35; rows++) {
            int wbytes = (stride + 15) & ~15, padded = (rows + 7) & ~7;
            memset(encoded, 0xcc, sizeof encoded);
            memset(decoded, 0xbb, sizeof decoded);
            ge_swizzle(encoded + 1, input + 2, stride, rows, wbytes, padded, 1);
            for (int y = 0; y < padded; y++)
                for (int x = 0; x < wbytes; x++)
                    assert(encoded[1 + ge_swizzle_offset(x, y, wbytes)] ==
                           (x < stride && y < rows ? input[2 + y * stride + x] : 0));
            ge_swizzle(encoded + 1, decoded + 3, stride, rows, wbytes, padded, 0);
            assert(memcmp(decoded + 3, input + 2, stride * rows) == 0);
            assert(encoded[0] == 0xcc && encoded[1 + wbytes * padded] == 0xcc);
            assert(decoded[2] == 0xbb && decoded[3 + stride * rows] == 0xbb);
        }
    }
}

int main(void)
{
    check_legacy_map_texture();
    check_texture_shapes();
    /* a bijection onto the padded picture, and the documented layout: block 1 starts at byte 128 */
    static unsigned char seen[64 * 24];
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 64; x++) {
            int at = ge_swizzle_offset(x, y, 64);
            assert(at >= 0 && at < 64 * 24 && !seen[at]);
            seen[at] = 1;
        }
    assert(ge_swizzle_offset(16, 0, 64) == 128 && ge_swizzle_offset(0, 1, 64) == 16 && ge_swizzle_offset(0, 8, 64) == 4 * 128);
    /* there and back, odd sizes padded: 20 bytes a row (T4 40 px) by 13 rows -> 32 by 16 */
    unsigned char linear[20 * 13], back[20 * 13], swizzled[32 * 16];
    for (int i = 0; i < (int)sizeof linear; i++)
        linear[i] = (unsigned char)(i * 7 + 1);
    ge_swizzle(swizzled, linear, 20, 13, 32, 16, 1);
    memset(back, 0, sizeof back);
    ge_swizzle(swizzled, back, 20, 13, 32, 16, 0);
    assert(!memcmp(linear, back, sizeof linear));
    assert(swizzled[ge_swizzle_offset(25, 0, 32)] == 0 && swizzled[ge_swizzle_offset(0, 14, 32)] == 0);
    puts("ge swizzle ok");
    return 0;
}
