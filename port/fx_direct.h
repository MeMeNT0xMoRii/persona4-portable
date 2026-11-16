#ifndef P4_FX_DIRECT_H
#define P4_FX_DIRECT_H
/* p4_fx_direct (2026-11-15): skill and battle effect geometry on its own path (included by rwsky.c after the PTank code).

   The effect code (effPolygonFlash, effPolygonWind, effPolygonThunder, ... through effGeometry.c's GeometryBlock*)
   rewrites the vertices and colours of its own atomics every frame. On the generic mesh path those atomics met what
   is built for scenery: the mesh cache and lock memo (keyed by sampled vertices and RenderWare's lock flag), static
   lists, instance batches, and a guard pass that drops the whole mesh when its pool is full. P3P's effGeomEngine (the
   same effect system on the PSP, docs/p3p_reference.md) draws such geometry straight from its arrays, one draw a part,
   vertices taken from frame memory. This path does the same with our own code:
   - effect atomics are marked where effGeometry.c creates them (census FIXES) and unmarked in RpAtomicDestroy;
   - each frame the arrays are read as they are now: nothing cached, nothing recorded;
   - triangles that cross the near plane or the guard band are cut per triangle (clip.h), never refused whole;
   - vertex memory comes from the pool, else from the PTank spill (kept 4 frames), so a full pool drops nothing.
   Lit (normals), skinned and env-mapped geometry stays on the generic path.
   p4_fx_direct 0: the generic mesh path, as before. */

int p4_fx_direct = 1;
/* p4_fx_opaque: a mesh whose material alpha is 0xFF is drawn without blend and alpha test, as the PS2's RenderWare
   does (see fx_draw). 0: blended by vertex alpha like the generic path. */
int p4_fx_opaque = 1;
/* p4_fx_regs: a blended mesh takes the ALPHA_1/TEST_1 its effect set through func_003a2950 (kept per atomic) instead
   of the scene's. 0: the scene's blend and test, as the generic path. */
int p4_fx_regs = 1;
static unsigned fx_opaque_meshes;

enum { FX_SLOTS = 2048 };
#define FX_TOMB ((const void *)1)
/* Per marked atomic: the GS ALPHA_1 and TEST_1 the effect asked for through func_003a2950 (GeometryBlockSetPartState:
   additive 0x44/0x48/0x42, TEST 0x17FB or 0x1801), as the PS2's RenderWare kept them for its draw. */
typedef struct { unsigned alpha, test; unsigned char has_alpha, has_test, pad[2]; } FxRegs;
static const void **fx_table;
static FxRegs *fx_regs;
static unsigned fx_live, fx_table_full, fx_reg_sets, fx_reg_unmarked;
static unsigned fx_atomics, fx_fallback, fx_meshes, fx_triangles, fx_cut, fx_cut_out, fx_dropped, fx_spilled, fx_refused;
static unsigned fx_explicit_blend, fx_explicit_test;

static inline unsigned fx_slot(const void *atomic) { return ((unsigned)atomic >> 4) * 2654435761u >> 21; }

static int fx_find(const void *atomic)
{
    if (!fx_table || !atomic)
        return -1;
    for (unsigned i = 0, s = fx_slot(atomic); i < FX_SLOTS && fx_table[s]; i++, s = (s + 1) & (FX_SLOTS - 1))
        if (fx_table[s] == atomic)
            return (int)s;
    return -1;
}

void p4_fx_mark(void *atomic)
{
    if (!atomic)
        return;
    if (!fx_table) {
        fx_table = calloc(FX_SLOTS, sizeof *fx_table);
        fx_regs = calloc(FX_SLOTS, sizeof *fx_regs);
        if (!fx_table || !fx_regs) {
            free(fx_table), free(fx_regs);
            fx_table = 0, fx_regs = 0;
            return;
        }
    }
    if (fx_find(atomic) >= 0)
        return;
    for (unsigned i = 0, s = fx_slot(atomic); i < FX_SLOTS; i++, s = (s + 1) & (FX_SLOTS - 1))
        if (!fx_table[s] || fx_table[s] == FX_TOMB) {
            fx_table[s] = atomic;
            memset(&fx_regs[s], 0, sizeof fx_regs[s]);
            fx_live++;
            return;
        }
    fx_table_full++;
}

void p4_fx_unmark(void *atomic)
{
    int s = fx_find(atomic);
    if (s < 0)
        return;
    fx_table[s] = FX_TOMB;
    fx_live--;
}

static int fx_marked(const void *atomic) { return fx_find(atomic) >= 0; }

/* func_003a2950 for an atomic without a PTank: which 1 ALPHA_1 (low byte the A B C D fields, bits 24-31 FIX),
   which 2 the TEST_1 low half. Returns 0 when the atomic is not an effect's. */
static int fx_set_reg(void *atomic, int which, unsigned value)
{
    int s = fx_find(atomic);
    if (s < 0) {
        fx_reg_unmarked++;
        return 0;
    }
    fx_reg_sets++;
    if (which == 1)
        fx_regs[s].alpha = value, fx_regs[s].has_alpha = 1;
    else if (which == 2)
        fx_regs[s].test = value & 0xffff, fx_regs[s].has_test = 1;
    return 1;
}

/* p4_fx_trace N: log the next N effect geometries not seen before, one line each on first draw (a skill cast makes
   new ones). Poke it again to re-arm. */
int p4_fx_trace = 48;
static const void *fx_seen[64];
static unsigned fx_seen_at;
static int fx_trace_new(const void *g)
{
    if (p4_fx_trace <= 0)
        return 0;
    for (int i = 0; i < 64; i++)
        if (fx_seen[i] == g)
            return 0;
    fx_seen[fx_seen_at++ & 63] = g;
    p4_fx_trace--;
    return 1;
}

/* Vertex memory for this frame's draw: the pool while it lasts, then the spill. */
static Vertex3D *fx_alloc(int count)
{
    if (vertex_used + count <= MAX_VERTICES) {
        Vertex3D *v = vertex_pool + vertex_used;
        vertex_used += count;
        return v;
    }
    Vertex3D *v = ptank_spill_alloc(count);
    if (v)
        fx_spilled++;
    else
        fx_refused++;
    return v;
}

/* Returns 0 when the atomic should take the generic path instead. */
static int fx_draw(void *atomic, Geometry *g, char *frame)
{
    (void)atomic;
    if ((g->flags & 0x20) && g->morphTarget->normals) /* lit by the scene's lights: the generic path shades it */
        return 0;
    if (SKIN_OF_GEOMETRY(g) || matfx_geometry_env(g))
        return 0;
    const int count = g->numVertices;
    const V3 *position = g->morphTarget->verts;
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    const unsigned *preLit = g->preLit;
    const int modulate = (g->flags & 0x40) != 0;
    const Matrix *model = p4r_003e9700(frame);
    MeshHeader *header = g->mesh;
    if (count <= 0 || count > 65535 || !header->numMeshes)
        return 1;
    fx_atomics++;
    begin_frame();
    gu_dirty();
    SceneLight light = mesh_state(model, 0);
    (void)light;
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    Mat4 clip;
    guard_matrix(clip, model);
    /* Clip coordinates and outcodes once for the whole array: the meshes share it. */
    float (*cc)[4] = memalign(16, count * sizeof *cc);
    unsigned char *code = malloc(count);
    if (!cc || !code) {
        free(cc);
        free(code);
        fx_refused++;
        return 1;
    }
    for (int j = 0; j < count; j++) {
        const V3 *p = &position[j];
        for (int k = 0; k < 4; k++)
            cc[j][k] = p->x * clip[0][k] + p->y * clip[1][k] + p->z * clip[2][k] + clip[3][k];
        code[j] = (unsigned char)guard_outcode(cc[j], near);
    }
    const int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    unsigned tri_before = fx_triangles, cut_before = fx_cut, drop_before = fx_dropped;
    const int slot = fx_find(atomic);
    int traced = fx_trace_new(g);
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        if (!mesh->numIndices || !mesh->material)
            continue;
        int triangles = strip ? mesh->numIndices - 2 : mesh->numIndices / 3;
        if (triangles <= 0)
            continue;
        fx_meshes++;
        unsigned colour = modulate ? mesh->material->colour : 0xffffffff;
        Vertex3D *v = fx_alloc(count);
        if (!v)
            continue;
        for (int j = 0; j < count; j++) {
            v[j].u = uv ? uv[j][0] : 0;
            v[j].v = uv ? uv[j][1] : 0;
            v[j].colour = modulate_colour(preLit ? double_colour(preLit[j]) : 0xffffffff, colour);
            v[j].x = position[j].x;
            v[j].y = position[j].y;
            v[j].z = position[j].z;
        }
        /* Triangles wholly inside the band keep their indices; crossing ones are cut into a triangle list. */
        unsigned short *kept = memalign(16, triangles * 3 * sizeof *kept);
        Vertex3D *cut = 0;
        int nkept = 0, ncut = 0, cut_cap = 0;
        if (!kept) {
            fx_refused++;
            continue;
        }
        for (int t = 0; t < triangles; t++) {
            int a, b, c;
            if (strip) {
                a = mesh->indices[t], b = mesh->indices[t + 1], c = mesh->indices[t + 2];
                if (a == b || b == c || a == c)
                    continue;
                if (t & 1) { int s = b; b = c; c = s; } /* keep the strip's winding in a list */
            } else
                a = mesh->indices[t * 3], b = mesh->indices[t * 3 + 1], c = mesh->indices[t * 3 + 2];
            if (a >= count || b >= count || c >= count)
                continue;
            unsigned ca = code[a], cb = code[b], cx = code[c];
            fx_triangles++;
            if (!(ca | cb | cx)) {
                kept[nkept++] = a, kept[nkept++] = b, kept[nkept++] = c;
                continue;
            }
            if (ca & cb & cx) {
                fx_dropped++;
                continue;
            }
            if (ncut + GUARD_OUT_MAX > cut_cap) {
                int remaining = triangles - t;
                int want = ncut + (remaining < 64 ? remaining : 64) * GUARD_OUT_MAX;
                Vertex3D *grown = fx_alloc(want);
                if (!grown)
                    break;
                if (ncut)
                    memcpy(grown, cut, ncut * sizeof *cut);
                cut = grown, cut_cap = want;
            }
            ClipVertex tri[3];
            const int corner[3] = { a, b, c };
            for (int k = 0; k < 3; k++) {
                memcpy(tri[k].c, cc[corner[k]], sizeof tri[k].c);
                tri[k].v = v[corner[k]];
                tri[k].n[0] = tri[k].n[1] = tri[k].n[2] = 0;
            }
            int out = clip_triangle_guard_fast(tri, near, cut + ncut, 0, ca | cb | cx);
            ncut += out;
            fx_cut++;
            fx_cut_out += out;
        }
        void **texture = mesh->material->texture;
        bind_mask(mesh->material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        draw_material_alpha = colour >> 24;
        /* RenderWare on the PS2 turns the GS blend (PRIM ABE) on for a mesh by its material's alpha, not by the
           prelight's: Megidolaon's three spheres (PCSX2 GS dump 2026-11-15: three 504-vertex triangle lists, vertex
           alpha 0..96) are drawn with ABE 0 and no alpha test, i.e. opaque. The effect code knows this and writes
           alpha 0xFE into a material it wants blended (FlashRotatingPackColors). Here the generic state blended them
           with their vertex alpha and alpha-tested them away. */
        int opaque = p4_fx_opaque && (mesh->material->colour >> 24) == 0xff;
        const FxRegs *regs = slot >= 0 && p4_fx_regs ? &fx_regs[slot] : 0;
        unsigned saved_test = sky_state[3];
        int explicit_blend = 0;
        if (opaque) {
            st_enable(GU_BLEND, 0);
            st_enable(GU_ALPHA_TEST, 0);
            fx_opaque_meshes++;
        } else if (regs) {
            /* the blend and test the effect set for this atomic (GeometryBlockSetPartState), as P3P states its own */
            if (regs->has_alpha) {
                st_enable(GU_BLEND, 1);
                set_blend(regs->alpha & 0xff);
                gs.blend_known = 0;
                explicit_blend = 1;
                fx_explicit_blend++;
            }
            if (regs->has_test) {
                sky_state[3] = (sky_state[3] & 0xffff0000u) | regs->test;
                fx_explicit_test++;
            }
        }
        if (nkept) {
            /* the indices go to the GE from the list's memory: the malloc'd copy is freed below */
            unsigned short *idx = sceGuGetMemory((nkept * 2 + 3) & ~3);
            memcpy(idx, kept, nkept * sizeof *kept);
            sceKernelDcacheWritebackRange(v, count * sizeof *v);
            if (opaque)
                gu_draw(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, nkept, idx, v);
            else
                draw_tested(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, nkept, idx, v);
        }
        if (ncut) {
            sceKernelDcacheWritebackRange(cut, ncut * sizeof *cut);
            if (opaque)
                gu_draw(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, ncut, 0, cut);
            else
                draw_tested(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, ncut, 0, cut);
        }
        sky_state[3] = saved_test;
        if (opaque || explicit_blend) { /* the next draw expects mesh_state's blend and alpha test */
            st_enable(GU_BLEND, 1);
            st_enable(GU_ALPHA_TEST, 1);
            st_alpha_func(GU_GREATER, 8);
            set_blend(sky_state[2] ? sky_state[2] : BLEND_NORMAL);
            gs.blend_known = 0;
        }
        free(kept);
    }
    if (traced) {
        /* what the effect left in its arrays this frame: alpha range of the prelight, the first material, where it is */
        unsigned amin = 255, amax = 0, zero = 0;
        for (int j = 0; preLit && j < count; j++) {
            unsigned a = preLit[j] >> 24;
            amin = a < amin ? a : amin, amax = a > amax ? a : amax;
            zero += !position[j].x && !position[j].y && !position[j].z;
        }
        const Mesh *m0 = (const Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
        p4_log("TRACE fx atom %p geo %p v%d meshes %d strip %d flags %x prelit %d a%u-%u zero-pos %u mat %08x tex %p "
               "pos %d %d %d tris %u cut %u dropped %u src/dst %d/%d zwrite %d\n",
               atomic, g, count, header->numMeshes, strip, g->flags, preLit != 0, amin, amax, zero,
               m0->material ? m0->material->colour : 0, m0->material && m0->material->texture ? *(void **)m0->material->texture : 0,
               (int)model->pos.x, (int)model->pos.y, (int)model->pos.z, fx_triangles - tri_before, fx_cut - cut_before,
               fx_dropped - drop_before, render_state[10], render_state[11], render_state[RS_ZWRITE],
               slot >= 0 ? fx_regs[slot].alpha : 0, slot >= 0 && fx_regs[slot].has_alpha, slot >= 0 ? fx_regs[slot].test : 0,
               slot >= 0 && fx_regs[slot].has_test);
    }
    free(cc);
    free(code);
    return 1;
}

static void fx_report(void)
{
    if (fx_atomics || fx_fallback || fx_refused || fx_table_full)
        p4_log("fx direct %d per 256 frames: atomics %u (generic %u) meshes %u triangles %u cut %u -> %u vertices, "
               "dropped %u spilled %u refused %u; marked %u table full %u\n",
               p4_fx_direct, fx_atomics, fx_fallback, fx_meshes, fx_triangles, fx_cut, fx_cut_out, fx_dropped, fx_spilled,
               fx_refused, fx_opaque_meshes, fx_explicit_blend, fx_explicit_test, fx_reg_sets, fx_reg_unmarked, fx_live, fx_table_full);
    fx_atomics = fx_fallback = fx_meshes = fx_triangles = fx_cut = fx_cut_out = fx_dropped = fx_spilled = fx_refused = fx_opaque_meshes = 0;
    fx_explicit_blend = fx_explicit_test = fx_reg_sets = fx_reg_unmarked = 0;
}

#endif
