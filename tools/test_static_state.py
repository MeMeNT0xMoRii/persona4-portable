"""Compile actual prepared-list key code and verify mutation/toggle behavior."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_static_meshes import function, ROOT

class StaticState(unittest.TestCase):
    def test_state_key(self):
        source=(ROOT/'port/static_list.h').read_text()
        rw=(ROOT/'port/rwsky.c').read_text()
        start=source.index('int p4_static_state_cache')
        end=source.index('static unsigned sl_material_key')
        harness=r'''#include <assert.h>
#include <string.h>
#include <stdio.h>
#define MAX_STATE 64
static int render_state[MAX_STATE], sl_state_dirty=1;
static unsigned sky_state[8],pixel_mask;
static unsigned hash_words(unsigned h,const void *p,unsigned n) {
 const unsigned char *b=p;while(n--)h=(h^*b++)*16777619u;return h;
}
'''+function(source,'sl_state_key_full(')+'\n'+source[start:end]
        # Public setters, copied verbatim from the renderer.
        for name,next_name in [('void RpSkyRenderStateSet(', 'unsigned RpSkyRenderStateGet('),('int func_003f5070(', 'int func_003f5d90(')]:
            harness+=rw[rw.index(name):rw.index(next_name)]
        harness+=r'''
int main(void) {
 p4_static_state_cache=1;
 assert(sl_state_key()==sl_state_key_full());
 unsigned rebuilt=sl_state_rebuilds;
 for(int i=0;i<100;i++)assert(sl_state_key()==sl_state_key_full());
 assert(sl_state_rebuilds==rebuilt && sl_state_reuses==100);
 for(int i=0;i<64;i++) {
  func_003f5070(i,i+1);assert(sl_state_dirty);
  assert(sl_state_key()==sl_state_key_full());
  func_003f5070(i,i+1);assert(!sl_state_dirty);
 }
 for(int i=0;i<8;i++) {
  RpSkyRenderStateSet(i,i+7);assert(sl_state_dirty);
  assert(sl_state_key()==sl_state_key_full());
  RpSkyRenderStateSet(i,i+7);assert(!sl_state_dirty);
 }
 func_003f5070(-1,1);func_003f5070(64,1);RpSkyRenderStateSet(8,1);assert(!sl_state_dirty);
 pixel_mask=0xffffff;assert(sl_state_key()==sl_state_key_full());
 p4_static_state_cache=0;render_state[2]=777;assert(sl_state_key()==sl_state_key_full());
 p4_static_state_cache=1;assert(sl_state_key()==sl_state_key_full());
 p4_static_state_check=1;render_state[3]=999;assert(sl_state_key()==sl_state_key_full());assert(sl_state_diffs==1);
 unsigned saved[8]={1,2,3,4,5,6,7,8};memcpy(sky_state,saved,sizeof saved);sl_state_dirty=1;
 assert(sl_state_key()==sl_state_key_full());assert(sl_state_diffs==1);
 puts("state invalidation, masks, toggles and check recovery: OK");
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
