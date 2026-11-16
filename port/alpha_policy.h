#ifndef P4_ALPHA_POLICY_H
#define P4_ALPHA_POLICY_H

/* Keep opacity for blending; defer only fragments proved to be RGB_ONLY.
 * These do not write depth on the GS and must see the opaque background. */
static inline int p4_late_rgb_material(unsigned test, unsigned blend, unsigned mask, unsigned alpha)
{
    unsigned atst=(test>>1)&7;
    return (test&1) && (atst==5||atst==6) && ((test>>12)&3)==3 &&
        !(test&(1u<<14)) && blend==0x44 && !mask && alpha>0 && alpha<255;
}
static inline int p4_alpha_all_fail(unsigned test, unsigned reference, unsigned highest)
{
    unsigned atst=(test>>1)&7;
    return atst==5 ? highest<reference : atst==6 && highest<=reference;
}
static inline unsigned p4_alpha_product_ceiling(unsigned a,unsigned b)
{
    return (a*b+254)/255;
}
#endif
