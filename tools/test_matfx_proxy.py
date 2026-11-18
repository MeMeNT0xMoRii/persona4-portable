"""A FarProxy material has no plugin tail; its following mesh header is not a MatFX pointer."""
from pathlib import Path
import unittest
import ui_test_support
from ui_test_support import function

ROOT = Path(__file__).resolve().parents[1]


class MatFXProxy(unittest.TestCase):
    check_32 = ui_test_support.UiTestCase.check_32

    def test_proxy_exclusion_and_real_environment_material(self):
        rw = (ROOT / 'port/rwsky.c').read_text()
        far = (ROOT / 'port/far_lod.h').read_text()
        v3 = rw.index('typedef struct { float x, y, z; } V3;')
        types = rw[v3:rw.index('} Matrix;', v3) + 10] + \
            rw[rw.index('typedef struct { void *parentGeom;'):rw.index('\nenum { CAMERA_PROJECTION')]
        proxy = far[far.index('enum { FP_SLOTS'):far.index('static int sl_try(')]
        code = '#include <assert.h>\n#include <stdlib.h>\n#include <stddef.h>\n#include <string.h>\n#include <stdio.h>\n'
        code += types + proxy + '\nstatic FarProxy *fp_slots;\n'
        code += function(far, 'fp_owns_material(')
        code += '\nint p4_matfx_env=1,iGpffffb5e0=28;\n'
        code += function(rw, 'matfx_env_entry(')
        code += r'''
int main(void) {
    assert(sizeof(Material)==24 && offsetof(FarProxy,header)==offsetof(FarProxy,material)+24);
    fp_slots=calloc(FP_SLOTS,sizeof *fp_slots);assert(fp_slots);
    for(int i=0;i<FP_SLOTS;i++) {
        fp_slots[i].header.h.numMeshes=1;
        assert(*(unsigned *)((char *)&fp_slots[i].material+28)==1);
        assert(fp_owns_material(&fp_slots[i].material));
        assert(!matfx_env_entry(&fp_slots[i].material));
        assert(!fp_owns_material((Material *)((char *)&fp_slots[i].material+4)));
    }
    unsigned material[8]={0},plugin[33]={0};
    material[7]=(unsigned)plugin;plugin[32]=2;plugin[8]=2;plugin[1]=(unsigned)material;
    assert(!fp_owns_material((Material *)material));
    assert(matfx_env_entry((Material *)material)==(unsigned char *)plugin);
    plugin[32]=0;assert(!matfx_env_entry((Material *)material));
    plugin[32]=3;plugin[8]=0;plugin[24]=2;plugin[17]=(unsigned)material;
    assert(matfx_env_entry((Material *)material)==(unsigned char *)plugin+64);
    assert(!matfx_env_entry(0));free(fp_slots);fp_slots=0;
    assert(!fp_owns_material((Material *)material));
    puts("48 compact proxy materials reject plugin-tail reads; real env and bump+env materials remain supported");
}
'''
        self.check_32(code)


if __name__ == '__main__': unittest.main()
