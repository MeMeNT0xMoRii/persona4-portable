"""Host checks of the renderer's actual mesh partitioning and ordered fallback helpers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def function(source, name):
    # Find the preceding declaration's start, then balance its body.
    start = source.rfind('\nstatic ', 0, source.index(name)) + 1
    brace = source.index('{', source.index(name))
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class StaticMeshes(unittest.TestCase):
    def test_bounds_invalidation_order_and_fallback(self):
        rw = (ROOT/'port/rwsky.c').read_text()
        source = (ROOT/'port/static_list.h').read_text()
        types = rw[rw.index('typedef struct { float x, y, z; } V3;'):rw.index('\nenum { CAMERA_VIEW')]
        entries = source[source.index('typedef struct {'):source.index('static StaticList *sl_table;')]
        harness = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''+types+entries+r'''
enum { MESH_TRISTRIP=1 };
static unsigned sl_ge_hits,sl_ge_fallbacks;
static unsigned sl_bytes, cache_frame=50, sl_mesh_builds, sl_mesh_hits, sl_mesh_fallback, sl_hits;
static int p4_static_list_budget=1<<20, p4_ge_static_clip, sl_ge_uncut;
static const Geometry *draw_geometry;
static int calls[16], ncall;
static int translucent_material(const Material *m,int modulate) { return modulate && (m->colour>>24)!=255; }
static void begin_frame(void) {}
static void gu_mesh_begin(void) {}
static void sl_bury(void *p) { free(p); }
static float sl_ge_limit(const Matrix *m,const float b[6]) { return 0; }
static void sl_ge_prepare_part(SlMeshPart *p,const Matrix *m,const V3 *n) {}
static int sl_draw(StaticList *e,const void *a,const Geometry *g,const Matrix *m,const V3 *n) {
    int id=(int)(g->mesh->totalIndices);
    calls[ncall++]=id;
    if(id==3) { sl_hits++; return 1; }
    return 0;
}
static void draw_meshes(MeshHeader *h,const V3 *p,float (*uv)[2],const unsigned *pre,int count,const Matrix *m,int mod,const V3 *n,const float *sphere) {
    assert(draw_geometry && draw_geometry->mesh==h);
    calls[ncall++]=-(int)h->totalIndices;
}
'''+function(source,'sl_drop(')+'\n'+function(source,'sl_mesh_same(')+'\n'+function(source,'sl_mesh_build(')+'\n'+function(source,'sl_mesh_draw(')+r'''
int main(void) {
    V3 v[6]={{-100,-2,-3},{-98,2,3},{-99,0,1},{100,-2,-3},{102,2,3},{101,0,1}};
    unsigned short a[]={0,1,2}, b[]={3,4,5,3,5,4};
    Material mat={0};mat.colour=0xffffffff;
    struct { MeshHeader h; Mesh m[2]; } h={0};
    h.h.numMeshes=2;h.h.totalIndices=9;h.h.flags=MESH_TRISTRIP;h.h.serial=7;
    h.h.firstMeshOffset=(char*)&h.m[0]-(char*)(&h.h+1);
    h.m[0]=(Mesh){a,3,&mat};h.m[1]=(Mesh){b,6,&mat};
    MorphTarget mt={0};mt.verts=v;mt.sphere[3]=1000;
    Geometry g={0};g.flags=0x40;g.mesh=&h.h;g.numVertices=6;g.morphTarget=&mt;
    SlMeshSet *s=sl_mesh_build(&g,0,0);assert(s && sl_mesh_builds==1 && sl_bytes>0);
    assert(sl_mesh_same(s,&g,0));
    for(int i=0;i<2;i++) {
        SlMeshPart *p=&s->part[i];
        assert(p->draw.header.flags==MESH_TRISTRIP && p->draw.header.numMeshes==1);
        assert((char*)(&p->draw.header+1)+p->draw.header.firstMeshOffset==(char*)&p->draw.mesh);
        assert(p->geometry.morphTarget==&p->target && p->target.parentGeom==&p->geometry);
        assert(p->geometry.numVertices==6 && p->target.verts==v);
        assert(p->geometry.locked==1); /* a recycled wrapper must not inherit an old LockMemo */
        assert(p->draw.mesh.indices==h.m[i].indices && p->draw.mesh.material==&mat);
        for(int j=0;j<h.m[i].numIndices;j++) {
            V3 x=v[h.m[i].indices[j]];float *c=p->target.sphere;
            float dx=x.x-c[0],dy=x.y-c[1],dz=x.z-c[2];
            assert(dx*dx+dy*dy+dz*dz<c[3]*c[3]);
        }
        assert(p->target.sphere[3]<5);
    }
    assert(sl_mesh_draw(s,0,0)==1 && ncall==3);
    assert(calls[0]==3 && calls[1]==6 && calls[2]==-6);
    assert(sl_mesh_hits==1 && sl_mesh_fallback==1 && !draw_geometry);
    g.locked=1;assert(!sl_mesh_same(s,&g,0) && !sl_mesh_build(&g,0,0));g.locked=0;
    h.h.serial++;assert(!sl_mesh_same(s,&g,0));h.h.serial--;
    h.m[1].numIndices--;assert(!sl_mesh_same(s,&g,0));h.m[1].numIndices++;
    g.flags=0;assert(!sl_mesh_same(s,&g,0));g.flags=0x40;
    mat.colour=0x80ffffff;assert(!sl_mesh_same(s,&g,0) && !sl_mesh_build(&g,0,0));mat.colour=0xffffffff;
    unsigned before=sl_bytes;
    a[0]=6;assert(!sl_mesh_build(&g,0,0) && sl_bytes==before);a[0]=0;
    v[0].x=NAN;assert(!sl_mesh_build(&g,0,0) && sl_bytes==before);v[0].x=-100;
    p4_static_list_budget=1;assert(!sl_mesh_build(&g,0,0) && sl_bytes==before);
    StaticList parent={0};parent.meshes=s;
    s->part[0].entry.list=malloc(4);s->part[0].entry.data=malloc(8);
    s->part[0].entry.bytes=12;sl_bytes+=12;
    s->part[1].entry.parts=malloc(16);
    sl_drop(&parent);
    assert(!sl_bytes && !parent.meshes && !parent.list && !parent.data);
    puts("static mesh bounds, topology, invalidation, order and fallback: OK");
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-static-mesh-test-') as name:
            folder=Path(name);c=folder/'check.c';exe=folder/'check.exe';c.write_text(harness)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:
                command=[cc,'-std=c11','-O2',str(c),'-o',str(exe),'-lm']
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'
                compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10'
                versions=sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:self.skipTest('host C compiler unavailable')
                compiler,headers=compilers[-1],versions[-1];vc=compiler.parents[3]
                command=[str(compiler),'/nologo','/std:c11','/O2',f'/I{vc/"include"}',f'/I{headers/"ucrt"}',str(c),
                         f'/Fe:{exe}',f'/Fo:{folder/"check.obj"}','/link',f'/LIBPATH:{vc/"lib/x64"}',
                         f'/LIBPATH:{kits/"Lib"/headers.name/"ucrt/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"um/x64"}']
            r=subprocess.run(command,cwd=folder,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)


if __name__=='__main__':unittest.main()



