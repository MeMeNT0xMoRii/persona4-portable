/* Host check for ge_swizzle in port/swizzle.h: gcc -I port tools/test_ge_swizzle.c -o build/test_ge_swizzle && build/test_ge_swizzle */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "swizzle.h"

int main(void)
{
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
