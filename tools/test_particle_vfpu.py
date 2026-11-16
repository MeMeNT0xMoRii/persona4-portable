"""Check the actual VU0 matrix helper, exceptional inputs, and exact translator recognition."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from psp_vu0 import translate

ROOT = Path(__file__).resolve().parent.parent


class ParticleVFPU(unittest.TestCase):
    def test_translation_is_limited_to_exact_matrix_block(self):
        source = (ROOT/'ext/Persona4-Decompilation/src/Graphics/Effect/effParticle.c').read_text()
        blocks = re.findall(r'__asm__\s+volatile\((.*?)(?=\);)\);', source, re.S)
        native = [b for b in blocks if (r := translate(b)) and 'p4_particle_matrix(' in r]
        self.assertGreaterEqual(len(native), 2)
        for b in native:
            self.assertNotIn('p4_particle_matrix(', translate(b.replace('vmaddaz.xyzw', 'vmadday.xyzw')))

    def test_reference_register_state_and_exception_gate(self):
        code = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef _MSC_VER
#define __attribute__(x)
#define __inline__ __inline
#define __builtin_memcpy memcpy
#define __builtin_memset memset
#define __builtin_sqrtf sqrtf
#endif
#include "particle_vfpu.h"
struct P4Vu p4vu;
int main(void) {
    float matrix[16] __attribute__((aligned(16)))={1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    float point[4] __attribute__((aligned(16)))={2,3,4,99};
    p4vu.vf[0].f[3]=1;
    p4vu.vf[11].w[0]=0xdeadbeef;
    assert(particle_matrix_regular(matrix,point));
    particle_matrix_native(matrix,point);
    for(int k=0;k<4;k++) {
        assert(p4vu.acc.f[k]==matrix[k]*2+matrix[k+4]*3+matrix[k+8]*4);
        assert(p4vu.vf[10].f[k]==p4vu.acc.f[k]+matrix[k+12]);
    }
    assert(!memcmp(&p4vu.vf[28],matrix,64) && p4vu.vf[11].w[0]==0xdeadbeef);
    point[0]=INFINITY;assert(!particle_matrix_regular(matrix,point));
    particle_matrix_reference(matrix,point);
    for(int i=0;i<4;i++)assert(isfinite(p4vu.vf[10].f[i]));
    point[0]=1e-40f;assert(!particle_matrix_regular(matrix,point));
    point[0]=1e30f;assert(!particle_matrix_regular(matrix,point));
    point[0]=2;p4vu.vf[0].f[3]=2;assert(!particle_matrix_regular(matrix,point));
    particle_matrix_reference(matrix,point);assert(p4vu.vf[10].f[0]==53+26);
    puts("particle reference/state/exception checks OK; VFPU itself requires device check mode");
}
'''
        with tempfile.TemporaryDirectory(prefix='p4-particle-test-') as name:
            folder=Path(name);c=folder/'check.c';exe=folder/'check.exe';c.write_text(code)
            cc=shutil.which('gcc') or shutil.which('clang')
            if cc:
                command=[cc,'-std=c11','-O2',f'-I{ROOT/"port"}',str(c),'-o',str(exe),'-lm']
            else:
                vs=Path(os.environ.get('ProgramFiles','C:/Program Files'))/'Microsoft Visual Studio'
                compilers=sorted(vs.glob('*/Community/VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe'))
                kits=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Windows Kits/10'
                versions=sorted((kits/'Include').glob('10.*'))
                if not compilers or not versions:self.skipTest('host C compiler unavailable')
                compiler,headers=compilers[-1],versions[-1];vc=compiler.parents[3]
                command=[str(compiler),'/nologo','/std:c11','/O2',f'/I{ROOT/"port"}',f'/I{vc/"include"}',f'/I{headers/"ucrt"}',str(c),
                         f'/Fe:{exe}',f'/Fo:{folder/"check.obj"}','/link',f'/LIBPATH:{vc/"lib/x64"}',
                         f'/LIBPATH:{kits/"Lib"/headers.name/"ucrt/x64"}',f'/LIBPATH:{kits/"Lib"/headers.name/"um/x64"}']
            r=subprocess.run(command,cwd=folder,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)


if __name__=='__main__':unittest.main()
