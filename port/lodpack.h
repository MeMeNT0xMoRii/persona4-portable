/* Offline static LOD format shared by the PSP and host checks. Little endian.
   P4LP, version, entries, data offset; sorted (key[2], offset, bytes) entries.
   Payload: vertices, meshes, then three levels of (index count, u16 indices,
   padding to 4 bytes) per mesh. Every level is joined triangle strips.
   Only topology changes; positions/UV/colour/normals stay in the source asset. */
#ifndef P4_LODPACK_H
#define P4_LODPACK_H
#include <string.h>
#include <math.h>
enum { LP_MAGIC=0x504c3450, LP_VERSION=1, LP_LEVELS=3, LP_MESHES=32,
       LP_MAX_ENTRY=512*1024, LP_MAX_ENTRIES=32768 };
typedef struct { unsigned key[2], offset, bytes; } LpEntry;
typedef struct { unsigned count; const unsigned short *indices; } LpMesh;
static void lp_hash(unsigned h[2], const void *data, unsigned bytes)
{
    const unsigned char *p=data;
    while(bytes--) {
        h[0]=(h[0]^*p)*16777619u;
        h[1]=(h[1]^*p++)*2246822519u;
    }
}
static const LpEntry *lp_find(const LpEntry *entries, unsigned n, const unsigned key[2])
{
    unsigned lo=0,hi=n;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        const LpEntry *e=entries+mid;
        if(e->key[0]<key[0] || (e->key[0]==key[0] && e->key[1]<key[1])) lo=mid+1;
        else hi=mid;
    }
    return lo<n && entries[lo].key[0]==key[0] && entries[lo].key[1]==key[1] ? entries+lo : 0;
}
/* Input must be 4-byte aligned; no pointer into it escapes on failure. */
static int lp_decode(const void *data, unsigned bytes, unsigned vertices, unsigned meshes,
                     LpMesh out[LP_LEVELS][LP_MESHES])
{
    if(bytes<8 || bytes>LP_MAX_ENTRY || !vertices || vertices>65535 || !meshes || meshes>LP_MESHES) return 0;
    const unsigned char *p=data;
    unsigned head[2]; memcpy(head,p,8);
    if(head[0]!=vertices || head[1]!=meshes) return 0;
    unsigned at=8;
    for(unsigned l=0;l<LP_LEVELS;l++) for(unsigned m=0;m<meshes;m++) {
        if(bytes-at<4) return 0;
        unsigned n;memcpy(&n,p+at,4);at+=4;
        if(n<3 || n>(bytes-at)/2) return 0;
        const unsigned short *ix=(const unsigned short *)(p+at);
        for(unsigned i=0;i<n;i++) if(ix[i]>=vertices) return 0;
        out[l][m]=(LpMesh){n,ix};
        unsigned size=(n*2+3)&~3u;
        if(size>bytes-at) return 0;
        at+=size;
    }
    return at==bytes;
}
/* Thresholds are fractions of the view * 100000; previous is per instance.
   Twenty percent hysteresis on the return to a more detailed level. */
static int lp_choose(float cover, int previous, const int thresholds[LP_LEVELS])
{
    if(!isfinite(cover) || cover<0 || thresholds[0]<=thresholds[1] ||
       thresholds[1]<=thresholds[2] || thresholds[2]<=0) return 0;
    float size=cover*100000.0f;
    int l=previous>=0 && previous<=LP_LEVELS ? previous : 0;
    while(l<LP_LEVELS && size<thresholds[l]) ++l;
    while(l>0 && size>thresholds[l-1]*1.2f) --l;
    return l;
}
#endif
