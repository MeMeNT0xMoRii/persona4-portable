"""Subdivision topology and the GE guard-band edge certificate, using the production helpers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parent.parent

class GEPrepare(unittest.TestCase):
    def test_preparation_and_camera_certificate(self):
        code=r'''
#include <assert.h>
#include <stdio.h>
#include "ge_prepare.h"
typedef struct { float x,y,w; } C;
static float dist(C p,int plane,float near) {
    if(plane==0)return p.w-near;
    if(plane==1)return p.w+p.x;
    if(plane==2)return p.w-p.x;
    if(plane==3)return p.w+p.y;
    return p.w-p.y;
}
static int clip(C *in,int n,C *out,int plane,float near) {
    if(!n)return 0;int used=0;C a=in[n-1];float da=dist(a,plane,near);
    for(int i=0;i<n;i++) {
        C b=in[i];float db=dist(b,plane,near);
        if((da<0)!=(db<0)) {float t=da/(da-db);out[used++]=(C){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.w+(b.w-a.w)*t};}
        if(db>=0)out[used++]=b;
        a=b;da=db;
    }
    return used;
}
static C transform(const float p[3],const float m[4][4]) {
    return (C){p[0]*m[0][0]+p[1]*m[1][0]+p[2]*m[2][0]+m[3][0],
               p[0]*m[0][1]+p[1]*m[1][1]+p[2]*m[2][1]+m[3][1],
               p[0]*m[0][3]+p[1]*m[1][3]+p[2]*m[2][3]+m[3][3]};
}
static int verify_triangle(C tri[3],float near) {
    C a[16],b[16];int n=clip(tri,3,a,0,near),bad=0;
    for(int i=0;i<n;i++)if(fabsf(a[i].x)>8*a[i].w || fabsf(a[i].y)>8*a[i].w)bad=1;
    for(int p=1;p<=4 && n;p++) {n=clip(a,n,b,p,near);memcpy(a,b,n*sizeof *a);}
    assert(!bad || !n);
    return n>0;
}
static unsigned seed=47;
static float rnd(void) {seed=seed*1664525u+1013904223u;return (seed>>8)*(1.0f/16777216.0f);}
int main(void) {
    float p[12]={0,0,0,100,0,0,100,0,100,0,0,100}, uv[4][2]={{0,0},{1,0},{1,1},{0,1}};
    unsigned short ix[6]={0,1,2,0,2,3};P4GePrepared out;
    assert(p4_ge_prepare(&out,p,uv,0,0,4,ix,6,0,20,128));
    assert(out.ni>6 && out.edge<=20.001f);
    double area=0;
    for(int i=0;i<out.ni;i+=3) {
        P4GeVertex a=out.v[out.idx[i]],b=out.v[out.idx[i+1]],c=out.v[out.idx[i+2]];
        double z=(b.p[0]-a.p[0])*(c.p[2]-a.p[2])-(b.p[2]-a.p[2])*(c.p[0]-a.p[0]);
        assert(z>0);area+=z*.5;
    }
    assert(fabs(area-10000)<.01);
    for(int i=0;i<out.nv;i++){assert(fabsf(out.v[i].uv[0]-out.v[i].p[0]*.01f)<1e-5f);assert(fabsf(out.v[i].uv[1]-out.v[i].p[2]*.01f)<1e-5f);}
    /* Every interior edge has a partner; no T-junction or duplicate midpoint on the shared diagonal. */
    for(int i=0;i<out.ni;i+=3)for(int k=0;k<3;k++) {
        int a=out.idx[i+k],b=out.idx[i+(k+1)%3],found=0;
        P4GeVertex va=out.v[a],vb=out.v[b];
        for(int j=0;j<out.ni;j+=3)for(int e=0;e<3;e++)if(out.idx[j+e]==b && out.idx[j+(e+1)%3]==a)found++;
        if(!found)assert((va.p[0]==vb.p[0]&&(va.p[0]==0||va.p[0]==100)) || (va.p[2]==vb.p[2]&&(va.p[2]==0||va.p[2]==100)));
        else assert(found==1);
    }
    p4_ge_prepared_free(&out);assert(!out.v && !out.idx);
    assert(!p4_ge_prepare(&out,p,uv,0,0,4,ix,6,0,1,1));
    ix[0]=4;assert(!p4_ge_prepare(&out,p,uv,0,0,4,ix,6,0,20,128));ix[0]=0;
    p[0]=NAN;assert(!p4_ge_prepare(&out,p,uv,0,0,4,ix,6,0,20,128));p[0]=0;
    unsigned short strip[4]={0,1,3,2};assert(p4_ge_prepare(&out,p,uv,0,0,4,strip,4,1,1000,1));assert(out.ni==6);p4_ge_prepared_free(&out);
    float bounds[6]={-3000,0,-6000,3000,0,6000};int visible=0;
    for(int camera=0;camera<250;camera++) {
        float m[4][4]={{0}};float angle=rnd()*6.2831853f,cs=cosf(angle),sn=sinf(angle),sx=.7f+rnd()*3,sy=.7f+rnd()*3;
        float pitch=(rnd()-.5f)*2.2f, cpitch=cosf(pitch), spitch=sinf(pitch);
        float right[3]={cs,0,-sn},up[3]={sn*spitch,cpitch,cs*spitch},forward[3]={sn*cpitch,-spitch,cs*cpitch};
        float cx=(rnd()-.5f)*8000,cy=100+rnd()*1800,cz=(rnd()-.5f)*8000;
        for(int j=0;j<3;j++) {float scale=.5f+rnd()*2;m[j][0]=right[j]*sx*scale;m[j][1]=up[j]*sy*scale;m[j][3]=forward[j]*scale;}
        m[3][0]=-(cx*right[0]+cy*right[1]+cz*right[2])*sx;
        m[3][1]=-(cx*up[0]+cy*up[1]+cz*up[2])*sy;
        m[3][3]=-(cx*forward[0]+cy*forward[1]+cz*forward[2]);
        float near=1+rnd()*100,limit=p4_ge_edge_limit(m,bounds,near,8);
        assert(limit>0 && isfinite(limit));
        for(int tri=0;tri<120;tri++) {
            float centre[3]={(rnd()-.5f)*5000,0,(rnd()-.5f)*10000};C cp[3];
            for(int k=0;k<3;k++) {
                float q[3]={centre[0]+(rnd()-.5f)*limit*.25f,0,centre[2]+(rnd()-.5f)*limit*.25f};
                for(int j=0;j<3;j++){if(q[j]<bounds[j])q[j]=bounds[j];if(q[j]>bounds[j+3])q[j]=bounds[j+3];}
                cp[k]=transform(q,m);
            }
            visible+=verify_triangle(cp,near);
        }
    }
    assert(visible>500);
    float singular[4][4]={{0}};assert(p4_ge_edge_limit(singular,bounds,1,8)==0);
    puts("GE subdivision winding, area, UVs, shared edges, bounds, and 30000 camera/triangle certificates OK");
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-ge-test-') as name:
            folder=Path(name);c=folder/'check.c';exe=folder/'check.exe';c.write_text(code)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:
                cmd=[cc,'-std=c11','-O2',f'-I{ROOT/"port"}',str(c),'-o',str(exe),'-lm']
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'; compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10'; versions=sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:self.skipTest('host compiler unavailable')
                compiler,headers=compilers[-1],versions[-1];vc=compiler.parents[3]
                cmd=[str(compiler),'/nologo','/std:c11','/O2',f'/I{ROOT/"port"}',f'/I{vc/"include"}',f'/I{headers/"ucrt"}',str(c),f'/Fe:{exe}',f'/Fo:{folder/"check.obj"}','/link',f'/LIBPATH:{vc/"lib/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"ucrt/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"um/x64"}']
            r=subprocess.run(cmd,cwd=folder,capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(r.returncode,0,r.stdout+r.stderr)

if __name__=='__main__':unittest.main()
