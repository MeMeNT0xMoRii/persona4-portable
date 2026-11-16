/* Build as a host shared library; tools/psp_lod.py supplies the compiler flags.
   No PSP calls and no runtime simplification on the device. */
#include "meshkit.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
EXPORT int p4_lod_simplify(const float (*pos)[3], int nv, unsigned short *tri, int nt, int percent)
{
    if(nv<1 || nv>65535 || nt<1 || percent<1 || percent>99) return -1;
    for(int t=0;t<nt*3;t++) if(tri[t]>=nv) return -1;
    MkStats stats;
    int target=nt*percent/100;
    if(target<1) target=1;
    return mk_simplify(pos,nv,0,0,tri,nt,target,INFINITY,1,&stats);
}
