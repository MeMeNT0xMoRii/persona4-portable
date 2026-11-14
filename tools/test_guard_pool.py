"""Run the port's guarded draw path with tight, host-side geometry pools."""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class GuardPoolTest(unittest.TestCase):
    def test_draw_uses_actual_clipped_size_and_respects_both_pools(self):
        cc = shutil.which("gcc")
        if not cc:
            self.skipTest("host gcc is unavailable")

        root = Path(__file__).resolve().parents[1]
        source = (root / "port/rwsky.c").read_text()
        types = re.findall(r"^typedef struct \{[^\n]+} Vertex3D(?:N)?;", source, re.M)
        capture = re.search(r"^typedef struct \{[^\n]+} GuardCapture;\nstatic GuardCapture \*guard_capture;", source, re.M)[0]
        draw = re.search(
            r"^static void draw_guarded_chunks\([^;]*?\n\{.*?^\}",
            source, re.M | re.S,
        )[0]
        harness = r'''
#include <assert.h>
#include <string.h>
#include "clip.h"

enum { GU_TRIANGLES, GU_TRIANGLE_STRIP };
enum { GU_INDEX_16BIT = 1, GU_NORMAL_8BIT = 2 };
enum { PROF_GLOOP, PROF_GCLIP, PROF_GSUB };

typedef struct { int unused; } Material;
typedef struct { int unused; } SceneLight;
typedef struct { int unused; } GuardLazy;

static unsigned short index_pool[64];
static Vertex3D vertex_pool[64];
static int index_used, vertex_used, index_cap, vertex_cap;
#define MAX_INDICES index_cap
#define MAX_VERTICES vertex_cap
#define GUARD_MASK 0x1fu
static int guard_meshes, guard_clipped, guard_dropped;
static unsigned guard_offscreen, guard_near_ge, guard_clip_checks, guard_clip_diffs;
static unsigned guard_pool_recovered, guard_pool_exhausted;
static int p4_guard_loop2 = 1, p4_guard_near_ge, p4_guard_clip_fast = 1;
static int p4_guard_clip_check = 1;
static float guard_clip[4][4] = {
    {0, 0, 0, 1}, {0, .5f, 0, 1}, {.5f, 0, 0, 1}, {10, 0, 0, 1},
};
static unsigned guard_code[4];
static unsigned profile_mask;
static void prof_begin(int p) {
    assert(!(profile_mask & (1u << p)));
    profile_mask |= 1u << p;
}
static void prof_end(int p) {
    assert(profile_mask & (1u << p));
    profile_mask &= ~(1u << p);
}
static void guard_code_lazy(const GuardLazy *lazy, int i, float near) {
    (void)lazy; (void)i; (void)near;
    assert(0 && "fixture already supplied outcodes");
}
static signed char normal_s8(float n) { return (signed char)(n * 127); }
static void sceKernelDcacheWritebackRange(const void *p, unsigned size) {
    (void)p;
    assert(size > 0);
}

static struct { int count, vtype; const void *indices, *vertices; } draws[2];
static int draw_count;
static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices,
                               const void *vertices, const Material *material,
                               int modulate, const SceneLight *light) {
    (void)material; (void)modulate; (void)light;
    assert(prim == GU_TRIANGLES && draw_count < 2);
    draws[draw_count].count = count;
    draws[draw_count].vtype = vtype;
    draws[draw_count].indices = indices;
    draws[draw_count++].vertices = vertices;
}

static void begin_mesh(int free_indices, int free_vertex_units) {
    index_used = 4;
    vertex_used = 1;
    index_cap = index_used + free_indices;
    vertex_cap = vertex_used + free_vertex_units;
    memset(index_pool, 0x5a, sizeof index_pool);
    memset(vertex_pool, 0x5a, sizeof vertex_pool);
    draw_count = guard_pool_recovered = guard_pool_exhausted = 0;
    for (int i = 0; i < 4; i++) guard_code[i] = guard_outcode(guard_clip[i], .1f);
}

static void verify_bounds(void) {
    assert(!profile_mask && !guard_clip_diffs);
    assert(index_pool[3] == 0x5a5a && index_pool[index_cap] == 0x5a5a);
    const unsigned char *bytes = (const unsigned char *)vertex_pool;
    assert(bytes[sizeof(Vertex3D) - 1] == 0x5a);
    assert(bytes[vertex_cap * sizeof(Vertex3D)] == 0x5a);
}
'''
        cases = r'''
int main(void) {
    const unsigned short mixed[] = {0, 1, 2, 0, 1, 3};
    const unsigned short inside[] = {0, 1, 2, 0, 2, 1};
    const unsigned char chunks[] = {0, 1};
    Vertex3D vertices[4] = {0};
    Vertex3DN lit[4] = {0};
    GuardCapture cap = {0};
    guard_capture = &cap;

    /* One kept triangle and one clipped to a quad share small pools. */
    begin_mesh(3, 6);
    draw_guarded_chunks(GU_TRIANGLES, mixed, 6, vertices, GU_INDEX_16BIT, .1f,
                        0, 0, 0, chunks, 1, 0);
    assert(guard_pool_recovered == 1 && guard_pool_exhausted == 0 && draw_count == 2);
    assert(draws[0].count == 3 && draws[0].indices == index_pool + 4);
    assert(draws[0].vertices == vertices && !memcmp(draws[0].indices, mixed, 6));
    assert(draws[1].count == 6 && !draws[1].indices && draws[1].vertices == vertex_pool + 1);
    assert(index_used == index_cap && vertex_used == vertex_cap);
    assert(cap.valid && cap.nkept == 3 && cap.ncut == 6);
    assert(cap.idx == draws[0].indices && cap.cut == draws[1].vertices);
    verify_bounds();

    /* The same clipped quad takes seven 24-byte units with 28-byte vertices. */
    begin_mesh(3, 7);
    draw_guarded_chunks(GU_TRIANGLES, mixed, 6, lit, GU_INDEX_16BIT | GU_NORMAL_8BIT, .1f,
                        0, 0, 0, chunks, 1, 0);
    assert(guard_pool_recovered == 1 && draw_count == 2 && vertex_used == vertex_cap);
    assert(draws[1].vtype == GU_NORMAL_8BIT && draws[1].count == 6);
    verify_bounds();

    begin_mesh(3, 6);
    cap.valid = 0;
    draw_guarded_chunks(GU_TRIANGLES, mixed, 6, lit, GU_INDEX_16BIT | GU_NORMAL_8BIT, .1f,
                        0, 0, 0, chunks, 1, 0);
    assert(guard_pool_exhausted == 1 && draw_count == 0 && index_used == 4 && vertex_used == 1);
    assert(!cap.valid);
    verify_bounds();

    begin_mesh(2, 6);
    draw_guarded_chunks(GU_TRIANGLES, mixed, 6, vertices, GU_INDEX_16BIT, .1f,
                        0, 0, 0, chunks, 1, 0);
    assert(guard_pool_exhausted == 1 && draw_count == 0 && index_used == 4);
    verify_bounds();

    /* Per-triangle index overflow leaves the pool available for another mesh. */
    begin_mesh(3, 0);
    draw_guarded_chunks(GU_TRIANGLES, inside, 6, vertices, GU_INDEX_16BIT, .1f,
                        0, 0, 0, 0, 0, 0);
    assert(guard_pool_exhausted == 1 && draw_count == 0 && index_used == 4);
    draw_guarded_chunks(GU_TRIANGLES, inside + 3, 3, vertices, GU_INDEX_16BIT, .1f,
                        0, 0, 0, 0, 0, 0);
    assert(guard_pool_recovered == 1 && draw_count == 1 && index_used == index_cap);
    assert(!memcmp(draws[0].indices, inside + 3, 6));
    verify_bounds();
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            test = path / "guard_pool.c"
            test.write_text("\n".join([*types, harness, capture, draw, cases]))
            exe = path / "guard_pool.exe"
            subprocess.run(
                [cc, "-std=c11", "-O2", "-I", str(root / "port"), str(test), "-o", str(exe)],
                check=True,
            )
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
