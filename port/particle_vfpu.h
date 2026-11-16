#ifndef P4_PARTICLE_VFPU_H
#define P4_PARTICLE_VFPU_H

#include "vu0.h"

/* The exact nine-instruction VU0 matrix block used by ParticleProjectVertices.
   Keep all observable register writes, including ACC and the four loaded basis vectors. */
static void particle_matrix_reference(const void *matrix, const void *point)
{
    __builtin_memcpy(&p4vu.vf[28], matrix, 64);
    __builtin_memcpy(&p4vu.vf[10], point, 16);
    p4_vop(2, 15, &p4vu.acc, &p4vu.vf[28], &p4vu.vf[10], 0);
    p4_vop(3, 15, &p4vu.acc, &p4vu.vf[29], &p4vu.vf[10], 1);
    p4_vop(3, 15, &p4vu.acc, &p4vu.vf[30], &p4vu.vf[10], 2);
    p4_vop(3, 15, &p4vu.vf[10], &p4vu.vf[31], &p4vu.vf[0], 3);
}

/* VFPU arithmetic has different exceptional/denormal behavior from the VU0 model.
   Only take this path for normal, bounded inputs, with ample overflow headroom. */
static int particle_matrix_regular(const void *matrix, const void *point)
{
    const unsigned char *p = matrix;
    for (int i = 0; i < 19; i++) {
        unsigned u;
        __builtin_memcpy(&u, i < 16 ? p + i * 4 : (const unsigned char *)point + (i - 16) * 4, 4);
        u &= 0x7fffffffu;
        if (u > 0x58000000u || (u && u < 0x00800000u))
            return 0;
    }
    return p4vu.vf[0].w[3] == 0x3f800000u;
}

static void particle_matrix_native(const void *matrix, const void *point)
{
#if defined(__mips__)
    /* Sequential mul/adds mirror the reference's rounding stages; a matrix dot instruction
       would also work for positions but would not leave the required intermediate ACC. */
    __asm__ volatile(
        "lv.q C400, 0(%[m])\n lv.q C410, 16(%[m])\n"
        "lv.q C420, 32(%[m])\n lv.q C430, 48(%[m])\n"
        "lv.q C600, 0(%[p])\n"
        "vscl.q C700, C400, S600\n"
        "vscl.q C710, C410, S601\n vadd.q C700, C700, C710\n"
        "vscl.q C710, C420, S602\n vadd.q C700, C700, C710\n"
        "vadd.q C720, C700, C430\n"
        "sv.q C400, 0(%[basis])\n sv.q C410, 16(%[basis])\n"
        "sv.q C420, 32(%[basis])\n sv.q C430, 48(%[basis])\n"
        "sv.q C700, 0(%[acc])\n sv.q C720, 0(%[out])\n"
        : : [m] "r"(matrix), [p] "r"(point), [basis] "r"(&p4vu.vf[28]),
            [acc] "r"(&p4vu.acc), [out] "r"(&p4vu.vf[10]) : "memory");
#else
    particle_matrix_reference(matrix, point);
#endif
}

void p4_particle_matrix(const void *matrix, const void *point);

#endif
