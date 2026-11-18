/* b254: cheaper far and repeated scenery, included by rwsky.c just before atomic_render_body.

   1. Instance batching (p4_inst_batch, exact). A small unlit static geometry that the game draws many times in a
      row (Heaven: f028_002_flower, 32 vertices, 84 copies a frame, each 2 GE draws for its TEST_1 pair) is copied
      into one world-space array per run and drawn with one draw_meshes call: the same materials, textures, render
      state and TEST_1 passes, so the picture is the same; only the draw count changes. A run is batched once it was
      seen at least p4_inst_batch_min long, and is flushed before anything else is drawn (the next other atomic, a
      world sector, Im3D, 2D, a camera or raster change: everything that calls translucent_flush), so the order
      against other draws holds.
   2. Far static stand-ins (p4_far_proxy, lossy, far only). A static unlit atomic of several meshes whose bounding
      sphere covers less than p4_far_proxy / 100000 of the view is drawn as one untextured mesh: each vertex keeps
      its prelight and material colour, times its texture's average around its UV. Several draws (texture binds,
      state, guard tests) become one. Only opaque meshes (material alpha 255, sampled texels all opaque); anything
      else keeps the full path. 20% hysteresis per atomic. The user (2026-11-14): far scenery may lose detail the
      PSP screen does not show; what is seen up close stays. */

/* ---------- 1. instance batching ---------- */
int p4_inst_batch = 1, p4_inst_batch_vertices = 64, p4_inst_batch_min = 4;
enum { IB_VERTS = 4096, IB_MESHES = 4, IB_INDICES = 12288, IB_KNOWN = 64 };
typedef struct {
    const Geometry *g;
    int count, vertices, indices[IB_MESHES], modulate, has_uv, has_prelit, has_normals, meshes;
    unsigned flags, colour[IB_MESHES], sky[8], mask;
    int render[MAX_STATE];
    void *camera;
    Material *material[IB_MESHES];
    V3 *pos, *nrm;
    float (*uv)[2];
    unsigned *prelit;
    unsigned short *idx[IB_MESHES];
    struct { MeshHeader h; Mesh m[IB_MESHES]; } header;
} InstBatch;
static InstBatch ib;
static int ib_flushing, ib_failed;
static const Geometry *ib_run_g;
static int ib_run_n;
static struct IbKnown { const Geometry *g; unsigned frame; } *ib_known; /* heap: .bss is full */
static unsigned ib_instances, ib_flushes, ib_drawn_vertices, ib_runs_known, ib_refused_full;

static int ib_is_known(const Geometry *g)
{
    unsigned slot = ((unsigned)g >> 4) * 2654435761u >> 26;
    return ib_known && ib_known[slot].g == g && cache_frame - ib_known[slot].frame <= 2;
}
static void ib_run_end(void)
{
    if (!ib_known)
        ib_known = calloc(IB_KNOWN, sizeof *ib_known);
    if (ib_run_g && ib_run_n >= p4_inst_batch_min && ib_known) {
        unsigned slot = ((unsigned)ib_run_g >> 4) * 2654435761u >> 26;
        ib_known[slot].g = ib_run_g, ib_known[slot].frame = cache_frame;
        ib_runs_known++;
    }
    ib_run_g = 0, ib_run_n = 0;
}
static void ib_flush(void)
{
    if (!ib.count || ib_flushing)
        return;
    if (!in_frame) {
        ib.count = 0;
        return;
    }
    ib_flushing = 1;
    int saved_render[MAX_STATE];
    unsigned saved_sky[8], saved_mask = pixel_mask;
    memcpy(saved_render, render_state, sizeof saved_render);
    memcpy(saved_sky, sky_state, sizeof saved_sky);
    void *saved_camera = current_camera;
    Matrix saved_model = translucent_model;
    current_camera = ib.camera, pixel_mask = ib.mask;
    memcpy(render_state, ib.render, sizeof ib.render);
    memcpy(sky_state, ib.sky, sizeof ib.sky);
    sl_state_dirty = 1; /* restoring a saved pass changes the key inputs */
    gu_dirty();
    MeshHeader *h = &ib.header.h;
    h->flags = ib.flags & ~MESH_PRIM_MASK; /* lists: strips were unrolled */
    h->numMeshes = ib.meshes, h->serial = 0, h->firstMeshOffset = 0, h->totalIndices = 0;
    for (int m = 0; m < ib.meshes; m++) {
        ib.header.m[m] = (Mesh){ ib.idx[m], ib.indices[m], ib.material[m] };
        h->totalIndices += ib.indices[m];
    }
    draw_geometry = 0;
    /* world space, model 0: GE lights are in the world, so world normals light as the model's did */
    draw_meshes(h, ib.pos, ib.has_uv ? ib.uv : 0, ib.has_prelit ? ib.prelit : 0, ib.vertices, 0, ib.modulate, ib.has_normals ? ib.nrm : 0, 0);
    ib_flushes++, ib_drawn_vertices += ib.vertices;
    ib.count = 0;
    current_camera = saved_camera, pixel_mask = saved_mask;
    memcpy(render_state, saved_render, sizeof saved_render);
    memcpy(sky_state, saved_sky, sizeof saved_sky);
    sl_state_dirty = 1; /* restoring a saved pass changes the key inputs */
    gu_dirty();
    if (saved_camera) { /* as translucent_flush: the GE back to what the next caller set up */
        mesh_state(&saved_model, 0);
        st_pixel_mask(saved_mask);
        gu_dirty();
    }
    ib_flushing = 0;
}
/* before anything other than atomic g is drawn (g 0: not an atomic) */
static void ib_before(const Geometry *g)
{
    if (ib.count && ib.g != g)
        ib_flush();
    if (ib_run_g && ib_run_g != g)
        ib_run_end();
}
static int ib_same(const Geometry *g, int modulate, const V3 *normals)
{
    if (ib.g != g || ib.camera != current_camera || ib.mask != pixel_mask || ib.modulate != modulate || ib.has_normals != (normals != 0) ||
        memcmp(ib.sky, sky_state, sizeof ib.sky) || memcmp(ib.render, render_state, sizeof ib.render))
        return 0;
    const Mesh *mesh = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    for (int m = 0; m < ib.meshes; m++)
        if (mesh[m].material != ib.material[m] || mesh[m].material->colour != ib.colour[m])
            return 0;
    return 1;
}
/* 1: atomic g at model is in the batch (drawn later); 0: draw it as usual */
static int ib_take(const Geometry *g, const Matrix *model, const V3 *normals, int skinned)
{
    /* b382: an atomic that culls its back faces (p4_cull_back, bind_depth) is drawn on its own, with its own state */
    if (p4_cull_back && render_state[RS_CULLMODE] == 2 && (p4_cull_back < 3 || (g->flags & 0x2c) == 0x20))
        return 0;
    if (!p4_inst_batch || skinned || (normals && !p4_ge_light) || shadow_shear_m || rtt_target || ib_failed || g->numVertices > p4_inst_batch_vertices ||
        g->numVertices > IB_VERTS || g->numMorphTargets != 1 || !g->mesh->numMeshes || g->mesh->numMeshes > IB_MESHES)
        return 0;
    if (ib_run_g != g)
        ib_run_end(), ib_run_g = g;
    ib_run_n++;
    if (!ib_is_known(g))
        return 0;
    if (!ib.pos) {
        ib.pos = malloc(IB_VERTS * sizeof *ib.pos);
        ib.nrm = malloc(IB_VERTS * sizeof *ib.nrm);
        ib.uv = malloc(IB_VERTS * sizeof *ib.uv);
        ib.prelit = malloc(IB_VERTS * sizeof *ib.prelit);
        for (int m = 0; m < IB_MESHES; m++)
            ib.idx[m] = malloc(IB_INDICES * 2);
        int ok = ib.pos && ib.nrm && ib.uv && ib.prelit;
        for (int m = 0; m < IB_MESHES; m++)
            ok = ok && ib.idx[m];
        if (!ok) {
            ib_failed = 1; /* no room: never again */
            return 0;
        }
    }
    const MeshHeader *header = g->mesh;
    const Mesh *mesh = (const Mesh *)((const char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, modulate = (g->flags & 0x40) != 0;
    int need[IB_MESHES];
    for (int m = 0; m < header->numMeshes; m++) {
        need[m] = strip ? (mesh[m].numIndices > 2 ? (mesh[m].numIndices - 2) * 3 : 0) : mesh[m].numIndices;
        if (need[m] > IB_INDICES)
            return 0;
    }
    if (ib.count && !ib_same(g, modulate, normals))
        ib_flush();
    int fits = ib.vertices + g->numVertices <= IB_VERTS;
    for (int m = 0; m < header->numMeshes && fits; m++)
        fits = ib.indices[m] + need[m] <= IB_INDICES;
    if (ib.count && !fits) {
        ib_flush();
        ib_refused_full++;
    }
    if (!ib.count) {
        ib.g = g, ib.vertices = 0, ib.modulate = modulate, ib.meshes = header->numMeshes, ib.flags = header->flags;
        ib.has_uv = g->numTexCoordSets > 0 && g->texCoords[0], ib.has_prelit = g->preLit != 0, ib.has_normals = normals != 0;
        ib.camera = current_camera, ib.mask = pixel_mask;
        memcpy(ib.sky, sky_state, sizeof ib.sky);
        memcpy(ib.render, render_state, sizeof ib.render);
        for (int m = 0; m < ib.meshes; m++)
            ib.material[m] = mesh[m].material, ib.colour[m] = mesh[m].material->colour, ib.indices[m] = 0;
    }
    int base = ib.vertices, nv = g->numVertices;
    const V3 *p = g->morphTarget->verts;
    for (int j = 0; j < nv; j++)
        ib.pos[base + j] = transform(&p[j], model);
    if (ib.has_normals)
        for (int j = 0; j < nv; j++)
            ib.nrm[base + j] = rotate(&normals[j], model); /* put_lit_vertex normalises */
    if (ib.has_uv)
        memcpy(ib.uv + base, g->texCoords[0], nv * sizeof *ib.uv);
    if (ib.has_prelit)
        memcpy(ib.prelit + base, g->preLit, nv * sizeof *ib.prelit);
    for (int m = 0; m < ib.meshes; m++) {
        const unsigned short *in = mesh[m].indices;
        unsigned short *out = ib.idx[m] + ib.indices[m];
        int k = 0;
        if (strip) {
            for (int t = 0; t + 2 < mesh[m].numIndices; t++) { /* odd strip triangles are wound the other way */
                int a = in[t + (t & 1)], b = in[t + 1 - (t & 1)], c = in[t + 2];
                if (a == b || b == c || a == c)
                    continue;
                out[k++] = base + a, out[k++] = base + b, out[k++] = base + c;
            }
        } else {
            for (int t = 0; t < mesh[m].numIndices; t++)
                out[k++] = base + in[t];
        }
        ib.indices[m] += k;
    }
    ib.vertices += nv;
    ib.count++;
    ib_instances++;
    return 1;
}

/* ---------- 2. far static stand-ins ---------- */
int p4_far_proxy = 300, p4_far_proxy_min_vertices = 100;
enum { FP_SLOTS = 48, FP_ATOMICS = 512 };
typedef struct {
    const Geometry *g;
    const V3 *verts;
    unsigned sig, frame, built;
    int state, vertices; /* 1 built, -1 refused (do not retry while sig holds) */
    V3 *pos;
    unsigned *prelit;
    unsigned short *idx;
    Material material;
    struct { MeshHeader h; Mesh m; } header;
    Geometry fake; /* b262: the stand-in as a geometry, for static_list.h */
    MorphTarget fake_target;
} FarProxy;
static int sl_try(const void *atomic, const Geometry *g, const Matrix *model, const V3 *normals);
static FarProxy *fp_slots;
/* Stand-in materials contain no RenderWare plugin tail (b327 on perf/shuffle-time: matfx_env_entry read the next
   mesh header's numMeshes=1 as a plugin pointer, an invalid read at 0x81). */
static int fp_owns_material(const Material *material)
{
    size_t offset = (size_t)material - (size_t)fp_slots;
    return fp_slots && offset < FP_SLOTS * sizeof *fp_slots && offset % sizeof *fp_slots == offsetof(FarProxy, material);
}
static struct FpAtomic { const void *atomic; unsigned char far; } *fp_atomic; /* heap: .bss is full */
static unsigned fp_drawn, fp_builds, fp_refused_build, fp_bytes, fp_meshes_saved;
static unsigned fp_why[6]; /* refused: 0 lit/skinned, 1 alpha, 2 texture kind, 3 memory, 4 one mesh, 5 too big */

/* lit: the light and the model's rotation are baked in, so they are part of the key */
static unsigned fp_signature(const Geometry *g, const Matrix *model, const V3 *normals, const SceneLight *light)
{
    unsigned h = hash_sample(2166136261u, g->morphTarget->verts, g->numVertices, sizeof(V3));
    if (normals) {
        float r[9] = { model->right.x, model->right.y, model->right.z, model->up.x, model->up.y, model->up.z, model->at.x, model->at.y, model->at.z };
        h = hash_sample(h, normals, g->numVertices, sizeof(V3));
        h = hash_words(h, r, sizeof r);
        h = hash_words(h, light, sizeof *light);
    }
    if (g->preLit)
        h = hash_sample(h, g->preLit, g->numVertices, 4);
    if (g->numTexCoordSets && g->texCoords[0])
        h = hash_sample(h, g->texCoords[0], g->numVertices, 8);
    const Mesh *mesh = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    for (int m = 0; m < g->mesh->numMeshes; m++) {
        unsigned w[3] = { (unsigned)mesh[m].material, mesh[m].material->colour,
                          mesh[m].material->texture ? (unsigned)*(Raster **)mesh[m].material->texture : 0 };
        h = hash_words(h, w, sizeof w);
    }
    return hash_words(h, &g->numVertices, 4) | 1;
}
/* texel (x, y) of level 0 as the GE reads it: RGBA, R in the low byte */
static unsigned fp_texel(const Raster *r, const SwizzleHead *sw, int x, int y)
{
    const unsigned char *p = r->originalPixels;
    int xbyte = x * r->depth / 8, off = sw ? ge_swizzle_offset(xbyte, y, sw->wbytes) : y * r->stride + xbyte;
    const unsigned *palette = (const unsigned *)r->palette;
    switch (r->depth) {
    case 4:
        return palette[p[off] >> ((x & 1) * 4) & 15];
    case 8:
        return palette[p[off]];
    case 16: {
        unsigned v = p[off] | p[off + 1] << 8, r5 = v & 31, g5 = v >> 5 & 31, b5 = v >> 10 & 31;
        return (r5 << 3 | r5 >> 2) | (g5 << 3 | g5 >> 2) << 8 | (b5 << 3 | b5 >> 2) << 16 | (v >> 15 ? 0xffu : 0) << 24;
    }
    default: {
        unsigned v;
        memcpy(&v, p + off, 4);
        return v;
    }
    }
}
/* the average of a 5 x 5 grid around (u, v) spanning about 1/8 of the texture: a low mip level's texel */
static unsigned fp_sample(const Raster *r, const SwizzleHead *sw, float u, float v, unsigned *alpha_min)
{
    int w = r->width, h = r->height;
    float fu = u - floorf(u), fv = v - floorf(v);
    if (!(fu >= 0 && fu < 1)) fu = 0;
    if (!(fv >= 0 && fv < 1)) fv = 0;
    int cx = (int)(fu * w), cy = (int)(fv * h), sx = w / 32, sy = h / 32;
    unsigned sum[4] = { 0 }, n = 0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int x = ((cx + dx * sx) % w + w) % w, y = ((cy + dy * sy) % h + h) % h;
            unsigned c = fp_texel(r, sw, x, y);
            for (int k = 0; k < 4; k++)
                sum[k] += c >> (k * 8) & 255;
            if ((c >> 24) < *alpha_min)
                *alpha_min = c >> 24;
            n++;
        }
    return (sum[0] / n) | (sum[1] / n) << 8 | (sum[2] / n) << 16 | 0xffu << 24;
}
static void fp_free(FarProxy *e)
{
    if (e->pos)
        fp_bytes -= e->vertices * (sizeof(V3) + 4) + e->header.m.numIndices * 2;
    free(e->pos);
    memset(e, 0, sizeof *e);
}
/* b375: every stand-in freed (GE idle) */
static unsigned fp_release_all(void)
{
    unsigned before = fp_bytes;
    for (int i = 0; fp_slots && i < FP_SLOTS; i++)
        fp_free(&fp_slots[i]);
    return before - fp_bytes;
}
/* the colour the GE lights a vertex to (bind_ge_light: emissive + global ambient (material colour) x vertex colour
   + directional x max(0, N.L)), before the texture */
static unsigned fp_lit(unsigned vertex, unsigned c, const Material *mat, const SceneLight *light, const V3 *n, const Matrix *model)
{
    V3 w = rotate(n, model);
    float len = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z), dot = 0;
    const V3 *d = &light->direction;
    float dl = sqrtf(d->x * d->x + d->y * d->y + d->z * d->z);
    if (len > 0 && dl > 0)
        dot = -(w.x * d->x + w.y * d->y + w.z * d->z) / (len * dl);
    if (!(dot > 0))
        dot = 0;
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    unsigned out = 0;
    for (int k = 0; k < 3; k++) {
        float mc = c >> (k * 8) & 255, vc = vertex >> (k * 8) & 255;
        float v = (float)byte_colour(mc * mat->ambient * ambient[k] + 0.5f) + mc * vc / 255.0f +
                  (float)byte_colour(mc * mat->diffuse * directional[k] + 0.5f) * dot;
        out |= (unsigned)(v >= 255 ? 255 : v + 0.5f) << (k * 8);
    }
    unsigned a = ((vertex >> 24) * (c >> 24) + 127) / 255;
    return out | a << 24;
}
static int fp_build(FarProxy *e, const Geometry *g, const Matrix *model, const V3 *normals, const SceneLight *light)
{
    const MeshHeader *header = g->mesh;
    const Mesh *mesh = (const Mesh *)((const char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, modulate = (g->flags & 0x40) != 0, nv = 0, ni = 0;
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    if (header->numMeshes < 2) {
        fp_why[4]++;
        return 0;
    }
    for (int m = 0; m < header->numMeshes; m++) {
        const Material *mat = mesh[m].material;
        Raster *r = mat->texture ? *(Raster **)mat->texture : 0;
        if (modulate && (mat->colour >> 24) != 255) {
            fp_why[1]++;
            return 0;
        }
        if (r && (r == &display_raster || rtt_find(r) || !r->originalPixels || r->width <= 0 || r->height <= 0 ||
                  (r->depth <= 8 && !r->palette) || (r->depth != 4 && r->depth != 8 && r->depth != 16 && r->depth != 32))) {
            fp_why[2]++;
            return 0;
        }
        ni += strip ? (mesh[m].numIndices > 2 ? (mesh[m].numIndices - 2) * 3 : 0) : mesh[m].numIndices;
        nv += mesh[m].numIndices < g->numVertices ? mesh[m].numIndices : g->numVertices;
    }
    if (nv > 65535 || nv > MAX_GUARDED || ni <= 0 || !mesh_scratch(g->numVertices)) {
        fp_why[5]++;
        return 0;
    }
    char *block = malloc(nv * (sizeof(V3) + 4) + ni * 2);
    if (!block) {
        fp_why[3]++;
        return 0;
    }
    V3 *pos = (V3 *)block;
    unsigned *prelit = (unsigned *)(pos + nv);
    unsigned short *idx = (unsigned short *)(prelit + nv);
    int used = 0, k = 0;
    for (int m = 0; m < header->numMeshes; m++) {
        const Material *mat = mesh[m].material;
        Raster *r = mat->texture ? *(Raster **)mat->texture : 0;
        const SwizzleHead *sw = r ? swizzle_head(r->originalPixels) : 0;
        unsigned colour = modulate ? mat->colour : 0xffffffff, alpha_min = 255;
        unsigned stamp = ++mesh_generation;
        const unsigned short *in = mesh[m].indices;
        int start = k;
        for (int t = 0; strip ? t + 2 < mesh[m].numIndices : t < mesh[m].numIndices; t += strip ? 1 : 3) {
            int c[3];
            if (strip) {
                c[0] = in[t + (t & 1)], c[1] = in[t + 1 - (t & 1)], c[2] = in[t + 2];
                if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                    continue;
            } else {
                if (t + 2 >= mesh[m].numIndices)
                    break;
                c[0] = in[t], c[1] = in[t + 1], c[2] = in[t + 2];
            }
            for (int q = 0; q < 3; q++) {
                int j = c[q];
                if (mesh_stamp[j] != stamp) {
                    mesh_stamp[j] = stamp;
                    mesh_remap[j] = used;
                    unsigned lit;
                    if (normals)
                        lit = fp_lit(g->preLit ? double_colour(g->preLit[j]) : 0xff000000, colour, mat, light, &normals[j], model);
                    else
                        lit = modulate_colour(g->preLit ? double_colour(g->preLit[j]) : 0xffffffff, colour);
                    if (r)
                        lit = modulate_colour(lit, fp_sample(r, sw, uv ? uv[j][0] : 0.5f, uv ? uv[j][1] : 0.5f, &alpha_min));
                    /* the draw path doubles prelight (PS2 0x80 = 1.0): halve the colour, keep the alpha it had */
                    unsigned half = 0;
                    for (int s = 0; s < 24; s += 8)
                        half |= (((lit >> s & 255) + 1) >> 1) << s;
                    /* alpha: the draw path keeps prelight alpha as it is (p4_alpha_x2 0); lit, the lit alpha */
                    half |= normals ? lit & 0xff000000u : (g->preLit ? g->preLit[j] : 0xffffffffu) & 0xff000000u;
                    pos[used] = g->morphTarget->verts[j];
                    prelit[used] = half;
                    used++;
                }
                idx[k++] = mesh_remap[j];
            }
        }
        if (alpha_min < 0xf0) { /* see-through texels (leaves, railings): the stand-in would fill them */
            (void)start;
            free(block);
            fp_why[1]++;
            return 0;
        }
    }
    e->g = g;
    e->verts = g->morphTarget->verts;
    e->vertices = used;
    e->pos = pos, e->prelit = prelit, e->idx = idx;
    e->material = *mesh[0].material;
    e->material.texture = 0;
    e->material.colour = 0xffffffff;
    e->header.h = (MeshHeader){ header->flags & ~MESH_PRIM_MASK, 1, 0, (unsigned)k, 0 };
    e->header.m = (Mesh){ idx, k, &e->material };
    e->state = 1;
    memset(&e->fake, 0, sizeof e->fake);
    e->fake_target.verts = pos;
    memcpy(e->fake_target.sphere, g->morphTarget->sphere, sizeof e->fake_target.sphere);
    e->fake.numVertices = used, e->fake.numMorphTargets = 1, e->fake.preLit = prelit, e->fake.mesh = &e->header.h;
    e->fake.morphTarget = &e->fake_target;
    e->fake.locked = 1; /* a list recorded for an earlier stand-in in this slot is made again */
    fp_bytes += used * (sizeof(V3) + 4) + k * 2;
    fp_builds++;
    return 1;
}
static unsigned fp_churn;
static FarProxy *fp_find(const Geometry *g, const Matrix *model, const V3 *normals)
{
    if (!fp_slots && !(fp_slots = calloc(FP_SLOTS, sizeof *fp_slots)))
        return 0;
    SceneLight light = normals ? scene_lights() : (SceneLight){ 0 };
    unsigned sig = fp_signature(g, model, normals, &light);
    FarProxy *spare = 0;
    for (int i = 0; i < FP_SLOTS; i++) {
        FarProxy *e = &fp_slots[i];
        if (e->g == g && e->verts == g->morphTarget->verts && e->sig == sig) {
            e->frame = cache_frame;
            return e->state > 0 ? e : 0;
        }
        if (e->frame != cache_frame && (!spare || !e->g || (spare->g && e->frame < spare->frame)))
            spare = e;
    }
    if (!spare)
        return 0;
    /* at most 2 builds a frame; more than 64 in 64 frames (a key that keeps changing: a flickering light, a turning
       model) stops building for 256 frames. Until built, the atomic is drawn as usual. */
    static unsigned window, window_builds, frame_seen, frame_builds, paused_until;
    if (frame_seen != cache_frame)
        frame_seen = cache_frame, frame_builds = 0;
    if (cache_frame - window >= 64)
        window = cache_frame, window_builds = 0;
    if (cache_frame < paused_until || frame_builds >= 2) {
        fp_churn++;
        return 0;
    }
    if (++window_builds > 64)
        paused_until = cache_frame + 256;
    frame_builds++;
    fp_free(spare);
    spare->frame = spare->built = cache_frame;
    if (!fp_build(spare, g, model, normals, &light)) {
        spare->g = g, spare->verts = g->morphTarget->verts, spare->state = -1;
        fp_refused_build++;
    }
    spare->sig = sig;
    return spare->state > 0 ? spare : 0;
}
/* 1: drawn as its stand-in */
static int fp_try(const void *atomic, const Geometry *g, const Matrix *model, const V3 *normals, int skinned)
{
    if (p4_far_proxy <= 0 || skinned || shadow_shear_m || rtt_target || g->numVertices < p4_far_proxy_min_vertices || g->mesh->numMeshes < 2)
        return 0;
    if (!fp_atomic && !(fp_atomic = calloc(FP_ATOMICS, sizeof *fp_atomic)))
        return 0;
    float size = atomic_cover(g, model) * 100000.0f;
    unsigned slot = ((unsigned)atomic >> 4) * 2654435761u >> 23;
    int was = fp_atomic[slot].atomic == atomic && fp_atomic[slot].far;
    int far = size < (float)p4_far_proxy || (was && size < (float)p4_far_proxy * 1.2f);
    fp_atomic[slot].atomic = atomic, fp_atomic[slot].far = far;
    if (!far)
        return 0;
    if (normals && !p4_ge_light) {
        fp_why[0]++;
        return 0;
    }
    FarProxy *e = fp_find(g, model, normals);
    if (!e)
        return 0;
    if (!sl_try((const char *)atomic + 1, &e->fake, model, 0)) { /* b262: a prepared list too */
        draw_geometry = 0;
        draw_meshes(&e->header.h, e->pos, 0, e->prelit, e->vertices, model, 0, 0, p4_guard_sphere ? g->morphTarget->sphere : 0);
    }
    fp_drawn++;
    fp_meshes_saved += g->mesh->numMeshes - 1;
    return 1;
}

static void far_lod_report(void)
{
    p4_log("inst batch %d max v %d min run %d: instances %u in %u draws (%u vertices), runs known %u, full %u per 256 frames\n",
           p4_inst_batch, p4_inst_batch_vertices, p4_inst_batch_min, ib_instances, ib_flushes, ib_drawn_vertices, ib_runs_known, ib_refused_full);
    p4_log("far proxy %d min v %d: drawn %u meshes saved %u; builds %u refused %u churn %u (cpu-lit %u alpha %u tex %u mem %u one %u big %u) %u KB\n",
           p4_far_proxy, p4_far_proxy_min_vertices, fp_drawn, fp_meshes_saved, fp_builds, fp_refused_build, fp_churn, fp_why[0], fp_why[1],
           fp_why[2], fp_why[3], fp_why[4], fp_why[5], fp_bytes / 1024);
    fp_churn = 0;
    ib_instances = ib_flushes = ib_drawn_vertices = ib_runs_known = ib_refused_full = 0;
    fp_drawn = fp_meshes_saved = fp_builds = fp_refused_build = 0;
    memset(fp_why, 0, sizeof fp_why);
}
