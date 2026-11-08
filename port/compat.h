/* Force-included into every decomp file: what the PS2 compilers provide and psp-gcc does not. */
#ifndef P4_COMPAT_H
#define P4_COMPAT_H

/* EE 128-bit register type: the size and alignment must hold, structs are read at retail offsets. */
typedef struct p4_q {
    unsigned w[4];
} __attribute__((aligned(16))) p4_q;

/* pcpyld rd, hi, lo: two 64-bit halves into one quadword */
static __inline__ p4_q _pcpyld(unsigned long long hi, unsigned long long lo)
{
    p4_q q;
    q.w[0] = (unsigned)lo;
    q.w[1] = (unsigned)(lo >> 32);
    q.w[2] = (unsigned)hi;
    q.w[3] = (unsigned)(hi >> 32);
    return q;
}

/* EE inline asm with no Allegrex form: reaching one stops the run and names the file and line. */
#include "vu0.h"
void p4_ee_asm_trap(const char *file, int line);
#define P4_EE_ASM() p4_ee_asm_trap(__FILE__, __LINE__)

/* EE timer 0 count register, read and cleared directly by game code */
unsigned *p4_timer0(void);
#define P4_T0_COUNT (*p4_timer0())
extern volatile unsigned p4_timer_control; /* PS2 timer setup: the PSP clock needs none */
#define P4_T0_CONTROL p4_timer_control

#endif
