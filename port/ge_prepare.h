#ifndef P4_GE_PREPARE_H
#define P4_GE_PREPARE_H

/* Offline/first-use geometry work for the GE-only side-guard path. No camera-dependent
   triangle classification is needed once a prepared mesh satisfies the edge certificate. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float p[3], uv[2], normal[3]; unsigned colour; } P4GeVertex;
typedef struct { P4GeVertex *v; unsigned short *idx; int nv, ni; float edge; } P4GePrepared;

/* m maps model XYZ,1 to clip XYZW (rows as in rwsky.c). All visible points obey
   |clipX|,|clipY| <= W and W >= near. The inverse XYW map plus the model AABB
   gives a stronger lower bound on visible W, e.g. the camera's height above a floor. */
static float p4_ge_edge_limit(const float m[4][4], const float bounds[6], float near, float guard)
{
    if (!(near > 0) || !(guard > 1) || !isfinite(near)) return 0;
    for (int r=0;r<4;r++) for (int col=0;col<4;col++) if (!isfinite(m[r][col])) return 0;
    for (int axis=0;axis<3;axis++) if (!isfinite(bounds[axis]) || !isfinite(bounds[axis+3]) || bounds[axis]>bounds[axis+3]) return 0;
    float a=m[0][0], b=m[0][1], c=m[0][3], d=m[1][0], e=m[1][1], f=m[1][3];
    float g=m[2][0], h=m[2][1], i=m[2][3];
    float inv[3][3]={{e*i-f*h,c*h-b*i,b*f-c*e}, {f*g-d*i,a*i-c*g,c*d-a*f}, {d*h-e*g,b*g-a*h,a*e-b*d}};
    float det=a*inv[0][0]+b*inv[1][0]+c*inv[2][0], wmin=near;
    if (isfinite(det) && fabsf(det)>1e-20f) {
        for (int axis=0;axis<3;axis++) {
            float x=inv[0][axis]/det, y=inv[1][axis]/det, z=inv[2][axis]/det;
            float eye=-(m[3][0]*x+m[3][1]*y+m[3][3]*z), den=fabsf(x)+fabsf(y)+fabsf(z);
            float dist=eye<bounds[axis] ? bounds[axis]-eye : eye>bounds[axis+3] ? eye-bounds[axis+3] : 0;
            float bound=den>0 ? dist/den : 0;
            if (isfinite(bound) && bound>wmin) wmin=bound;
        }
    }
    float grad2=0;
    for (int axis=0;axis<2;axis++) for (int sign=-1;sign<=1;sign+=2) {
        float n2=0;
        for(int j=0;j<3;j++) { float q=sign*m[j][axis]+guard*m[j][3]; n2+=q*q; }
        if (!isfinite(n2)) return 0;
        if(n2>grad2) grad2=n2;
    }
    /* A visible point has side distance >= (guard-1)*wmin. A triangle's clipped
       vertices are in its convex hull, at most max_edge away. Reserve 20% for
       float24 transforms / truncation rather than using an equality boundary. */
    return grad2>0 ? 0.8f*(guard-1)*wmin/sqrtf(grad2) : 0;
}

typedef struct { unsigned key; int vertex; } P4GeEdge;
static float p4_ge_len2(const P4GeVertex *v,int a,int b)
{
    float x=v[a].p[0]-v[b].p[0],y=v[a].p[1]-v[b].p[1],z=v[a].p[2]-v[b].p[2];
    return x*x+y*y+z*z;
}
static int p4_ge_mid(P4GeVertex *v,int *nv,int maxv,P4GeEdge *map,int cap,int a,int b)
{
    unsigned key=a<b ? (unsigned)a<<16|b : (unsigned)b<<16|a;
    unsigned at=key*2654435761u & (cap-1);
    while(map[at].key && map[at].key!=key+1) at=(at+1)&(cap-1);
    if(map[at].key) return map[at].vertex;
    if(*nv>=maxv) return -1;
    int k=(*nv)++;
    for(int j=0;j<3;j++) { v[k].p[j]=(v[a].p[j]+v[b].p[j])*0.5f; v[k].normal[j]=(v[a].normal[j]+v[b].normal[j])*0.5f; }
    for(int j=0;j<2;j++) v[k].uv[j]=(v[a].uv[j]+v[b].uv[j])*0.5f;
    v[k].colour=0;
    for(int j=0;j<32;j+=8) v[k].colour|=(((v[a].colour>>j&255)+(v[b].colour>>j&255)+1)/2)<<j;
    map[at].key=key+1;map[at].vertex=k;
    return k;
}
static void p4_ge_prepared_free(P4GePrepared *p)
{
    free(p->v);free(p->idx);memset(p,0,sizeof *p);
}

/* Source arrays use the game's ordinary float XYZ/UV/normal layouts. Compact by first
   index use, then split longest edges with shared midpoints. Bounded growth fails closed. */
static int p4_ge_prepare(P4GePrepared *out,const float *pos,const float (*uv)[2],const float *normal,const unsigned *colour,
                         int count,const unsigned short *indices,int ni,int strip,float limit,int growth)
{
    memset(out,0,sizeof *out);
    int tris=strip?ni-2:ni/3;
    if(count<=0 || count>65535 || tris<=0 || tris>21845 || !(limit>0) || !isfinite(limit) || growth<1) return 0;
    int maxt=tris>21845/growth ? 21845 : tris*growth, maxv=maxt+count;
    if(maxv>32768) maxv=32768;
    int cap=1;while(cap<maxv*2) cap<<=1;
    P4GeVertex *v=malloc(maxv*sizeof *v);
    unsigned short *ix=malloc(maxt*3*sizeof *ix),*remap=malloc(count*sizeof *remap);
    P4GeEdge *map=calloc(cap,sizeof *map);
    if(!v || !ix || !remap || !map) { free(v);free(ix);free(remap);free(map);return 0; }
    memset(remap,255,count*sizeof *remap);
    int nv=0,nt=0,ok=1;float lim2=limit*limit,edge2=0;
    for(int t=0;t<tris && ok;t++) {
        int tri[3],sp=0,stack[64][3];
        for(int c=0;c<3;c++) {
            int src=indices[strip?t+c:t*3+c];
            if(src>=count) { ok=0;break; }
            if(remap[src]==65535) {
                if(nv>=maxv) { ok=0;break; }
                remap[src]=nv;
                for(int j=0;j<3;j++) {
                    v[nv].p[j]=pos[src*3+j];v[nv].normal[j]=normal?normal[src*3+j]:0;
                    if(!isfinite(v[nv].p[j]) || !isfinite(v[nv].normal[j])) ok=0;
                }
                for(int j=0;j<2;j++) { v[nv].uv[j]=uv?uv[src][j]:0; if(!isfinite(v[nv].uv[j])) ok=0; }
                v[nv].colour=colour?colour[src]:0xffffffffu;
                nv++;
            }
            tri[c]=remap[src];
        }
        if(!ok) break;
        if(strip && (t&1)) { int x=tri[0];tri[0]=tri[1];tri[1]=x; }
        if(tri[0]==tri[1] || tri[1]==tri[2] || tri[2]==tri[0]) continue;
        memcpy(stack[sp++],tri,sizeof tri);
        while(sp && ok) {
            sp--;
            int x=stack[sp][0],y=stack[sp][1],z=stack[sp][2];
            float lengths[3]={p4_ge_len2(v,x,y),p4_ge_len2(v,y,z),p4_ge_len2(v,z,x)},best=lim2;
            int e=-1;
            for(int k=0;k<3;k++) { if(!isfinite(lengths[k])) {ok=0;break;} if(lengths[k]>best) {best=lengths[k];e=k;} }
            if(!ok) break;
            if(e<0) {
                if(nt>=maxt) {ok=0;break;}
                ix[nt*3]=x;ix[nt*3+1]=y;ix[nt*3+2]=z;nt++;
                for(int k=0;k<3;k++) if(lengths[k]>edge2) edge2=lengths[k];
            } else {
                if(sp+2>64) {ok=0;break;}
                int a=e==0?x:e==1?y:z,b=e==0?y:e==1?z:x,c=e==0?z:e==1?x:y;
                int mid=p4_ge_mid(v,&nv,maxv,map,cap,a,b);
                if(mid<0) {ok=0;break;}
                stack[sp][0]=a;stack[sp][1]=mid;stack[sp++][2]=c;
                stack[sp][0]=mid;stack[sp][1]=b;stack[sp++][2]=c;
            }
        }
    }
    free(remap);free(map);
    if(!ok || !nt) {free(v);free(ix);return 0;}
    out->v=v;out->idx=ix;out->nv=nv;out->ni=nt*3;out->edge=sqrtf(edge2);
    return 1;
}

#endif
