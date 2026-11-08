/* Host correctness check for the actual mesh rejection helper. */
#include <assert.h>
#include <stdio.h>
#include "frustum.h"

int main(void)
{
    float m[4][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1} };
    float p[3] = {0,0,0};
    assert(frustum_code(m,p) == 0);
    for (int axis=0; axis<3; axis++) {
        p[axis] = -2;
        assert(frustum_code(m,p) == (1u << (2*axis)));
        p[axis] = 2;
        assert(frustum_code(m,p) == (2u << (2*axis)));
        p[axis] = 1.00001f; /* GE rounding margin: retain edge geometry */
        assert(frustum_code(m,p) == 0);
        p[axis] = 0;
    }
    unsigned short ix[3] = {0,1,2};
    unsigned char c[3] = {1,5,17};
    assert(frustum_mesh_outside(c,3,ix,3));
    c[2] = 2; /* vertices offscreen on opposite sides can cross the screen */
    assert(!frustum_mesh_outside(c,3,ix,3));
    c[2] = 0; /* one inside vertex also forces retain */
    assert(!frustum_mesh_outside(c,3,ix,3));
    c[2] = 1;
    ix[2] = 3;
    assert(!frustum_mesh_outside(c,3,ix,3));
    assert(!frustum_mesh_outside(c,3,ix,0));
    p[0] = NAN;
    assert(frustum_code(m,p) == 0);
    p[0] = INFINITY;
    assert(frustum_code(m,p) == 0);
    p[0] = 0;
    m[3][0] = 4; /* translated model/camera */
    assert(frustum_code(m,p) == 2);
    m[3][0] = 0;
    m[3][3] = -1; /* point behind the eye */
    assert(frustum_code(m,p) == 63);
    puts("frustum checks passed");
    return 0;
}
