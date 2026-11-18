/* Mesh drawing (moved from rwsky.c, b322; included once, at the same place): vertex colour and lighting,
   census, the mesh cache and its keys, instance replay, fog, the 3D mesh state, alpha-test passes, draw_meshes. */
/* On the PS2 0x80 is full brightness in a vertex colour. */
/* 1: vertex alpha doubled like RGB (0x80 = opaque); 0: alpha already 0-255, as the dream's fog planes look on the PS2 */
int p4_alpha_x2 = 0;
static inline unsigned double_colour(unsigned c)
{
    unsigned r = (c & 0xff) * 2, g = (c >> 8 & 0xff) * 2, b = (c >> 16 & 0xff) * 2, a = (c >> 24) << p4_alpha_x2;
    return (r > 255 ? 255 : r) | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b) << 16 | (a > 255 ? 255u : a) << 24;
}

static unsigned modulate_colour(unsigned vertex, unsigned material)
{
    if (material == 0xffffffff)
        return vertex;
    unsigned result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        unsigned t = (vertex >> shift & 255) * (material >> shift & 255) + 128; /* round(a * b / 255) */
        result |= ((t + (t >> 8)) >> 8) << shift;
    }
    return result;
}

static inline V3 rotate(const V3 *v, const Matrix *m);
const Matrix *p4r_003e9700(void *frame);

static unsigned shade_colour(unsigned base, const V3 *normal, const Matrix *model, const Material *material, const SceneLight *light)
{
    V3 n = model ? rotate(normal, model) : *normal;
    float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    float diffuse = length > 0 ? -(n.x * light->direction.x + n.y * light->direction.y + n.z * light->direction.z) / length : 0;
    if (diffuse < 0) diffuse = 0;
    if (diffuse > 1) diffuse = 1;
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    unsigned colour = base & 0xff000000;
    for (int channel = 0; channel < 3; channel++) {
        float value = (base >> (channel * 8) & 255) + 255 *
            (material->ambient * ambient[channel] + material->diffuse * diffuse * directional[channel]);
        colour |= byte_colour(value) << (channel * 8);
    }
    return colour;
}

/* shade_colour with the light turned into the model's space once: the model matrices here are rotations with one
   uniform scale, so the normal's length after them is that scale. b68 profile: 367 cycles a vertex, mostly sqrt/div. */
typedef struct { float lx, ly, lz, ambient[3], directional[3]; } ShadeSetup;
static void shade_setup(ShadeSetup *k, const Matrix *model, const Material *material, const SceneLight *light)
{
    const V3 *l = &light->direction;
    float lx = l->x, ly = l->y, lz = l->z;
    if (model) {
        float scale = sqrtf(model->right.x * model->right.x + model->right.y * model->right.y + model->right.z * model->right.z);
        float inv = scale > 0 ? 1.0f / scale : 0;
        lx = (model->right.x * l->x + model->right.y * l->y + model->right.z * l->z) * inv;
        ly = (model->up.x * l->x + model->up.y * l->y + model->up.z * l->z) * inv;
        lz = (model->at.x * l->x + model->at.y * l->y + model->at.z * l->z) * inv;
    }
    k->lx = -lx; k->ly = -ly; k->lz = -lz;
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    for (int c = 0; c < 3; c++) {
        k->ambient[c] = 255 * material->ambient * ambient[c];
        k->directional[c] = 255 * material->diffuse * directional[c];
    }
}

static inline unsigned shade_fast(unsigned base, const V3 *n, const ShadeSetup *k)
{
    float d = n->x * k->lx + n->y * k->ly + n->z * k->lz;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    unsigned colour = base & 0xff000000;
    for (int c = 0; c < 3; c++) {
        float value = (base >> (c * 8) & 255) + k->ambient[c] + d * k->directional[c];
        colour |= byte_colour(value) << (c * 8);
    }
    return colour;
}

/* RenderWare's lighting from GE constants per mesh: the vertex colour (doubled prelight, or black) is the material's
   ambient and the global ambient is the material colour, so prelight x colour; emissive is colour x ka x the ambient
   light; light 0 adds colour x kd x the directional light x N.L. Differs from shade_fast + modulate_colour only where
   a channel saturates: the GE clamps after the material colour, the CPU path before it. */
static void bind_ge_light_body(const Material *material, int modulate, const SceneLight *light);
static void bind_ge_light(const Material *material, int modulate, const SceneLight *light)
{
    unsigned kept = kept_lights;
    prof_begin(PROF_LBIND);
    bind_ge_light_body(material, modulate, light);
    prof_end(PROF_LBIND);
    lights_bound += kept_lights == kept;
}
static void bind_ge_light_body(const Material *material, int modulate, const SceneLight *light)
{
    unsigned c = modulate ? material->colour : 0xffffffff, emissive = 0, diffuse = 0;
    if (gu_mesh_mode) { /* same colour, material terms and light as the last lit draw: only switch lighting on */
        unsigned key[13];
        key[0] = c;
        memcpy(key + 1, &material->ambient, 4);
        memcpy(key + 2, &material->diffuse, 4);
        memcpy(key + 3, light, 9 * 4);
        key[12] = 0;
        if (gs.light_known && !memcmp(key, gs.light_key, sizeof key)) {
            kept_lights++;
            st_enable(GU_LIGHTING, 1);
            return;
        }
        memcpy(gs.light_key, key, sizeof key);
        gs.light_known = 1;
    }
    const float *ambient = &light->ambient.x, *directional = &light->diffuse.x;
    for (int k = 0; k < 3; k++) {
        float mc = c >> (k * 8) & 255;
        emissive |= byte_colour(mc * material->ambient * ambient[k] + 0.5f) << (k * 8);
        diffuse |= byte_colour(mc * material->diffuse * directional[k] + 0.5f) << (k * 8);
    }
    const V3 *d = &light->direction;
    st_enable(GU_LIGHTING, 1);
    st_enable(GU_LIGHT1, 0);
    st_enable(GU_LIGHT2, 0);
    st_enable(GU_LIGHT3, 0);
    if (p4_ge_raw) { /* b366: libpspgu's words (read from its objects): CMAT, AC/AA, model colours EMC DMC AMC SMC */
        ge_word(0x53, GU_AMBIENT);
        ge_word(0x5C, c), ge_word(0x5D, c >> 24);
        ge_word(0x54, emissive), ge_word(0x56, 0xffffff), ge_word(0x55, 0xffffff), ge_word(0x57, 0);
    } else {
        sceGuColorMaterial(GU_AMBIENT);
        sceGuAmbient(c);
        sceGuModelColor(emissive, 0xffffffff, 0xffffffff, 0);
    }
    if (diffuse && (d->x || d->y || d->z)) { /* a zero direction would normalise to NaN */
        ScePspFVector3 to = { -d->x, -d->y, -d->z };
        if (p4_ge_raw) { /* light 0: position x y z (float >> 8), type (directional, diffuse), diffuse and ambient colour */
            union { float f; unsigned u; } x = { to.x }, y = { to.y }, z = { to.z };
            ge_word(0x63, x.u >> 8), ge_word(0x64, y.u >> 8), ge_word(0x65, z.u >> 8);
            ge_word(0x5F, (GU_DIRECTIONAL << 8 & 0x300) | 0);
            ge_word(0x90, diffuse), ge_word(0x8F, 0);
        } else {
            sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &to);
            sceGuLightColor(0, GU_DIFFUSE, diffuse);
            sceGuLightColor(0, GU_AMBIENT, 0);
        }
        st_enable(GU_LIGHT0, 1);
    } else {
        st_enable(GU_LIGHT0, 0);
    }
}

/* A unit normal's component as s8 (127 = 1). */
static inline signed char normal_s8(float c)
{
    float s = c * 127.0f;
    return s >= 127 ? 127 : s <= -127 ? -127 : (signed char)(s + (s >= 0 ? 0.5f : -0.5f));
}

/* Vertex j of an array as the GE lights it: doubled prelight (or black) as the material's ambient, unit normal in s8. */
static inline void put_lit_vertex(Vertex3DN *v, const V3 *position, float (*uv)[2], float du, float dv, const unsigned *preLit, const V3 *normals, int j)
{
    const V3 *n = &normals[j];
    float len = sqrtf(n->x * n->x + n->y * n->y + n->z * n->z), inv = len > 0 ? 1.0f / len : 0;
    v->u = uv ? uv[j][0] - du : 0;
    v->v = uv ? uv[j][1] - dv : 0;
    v->colour = preLit ? double_colour(preLit[j]) : 0xff000000;
    v->nx = normal_s8(n->x * inv);
    v->ny = normal_s8(n->y * inv);
    v->nz = normal_s8(n->z * inv);
    v->pad = 0;
    v->x = position[j].x;
    v->y = position[j].y;
    v->z = position[j].z;
}

/* draw_tested of a compact or cached mesh; vtype with normals means GE-lit */
/* b165 census of 3D draws (p4_probe_3d, poke it live): per material, its texture name, blend, colour, flags and the
   words after the material (RenderWare plugin data such as MatFX), to tell the arena floor, chests and Shadows. */
int p4_probe_3d = 0;
unsigned p4_hide_material; /* debug: this material is not drawn (identify a surface by hiding it) */
typedef struct { const Material *material; unsigned blend, test, vtype, calls, triangles; int modulate; unsigned mask; } Census3D;
enum { CENSUS3D = 160 };
static Census3D *census3d; /* b256: heap on first use (5 KB of .bss for a diagnostic) */
static void census3d_note(const Material *material, int vtype, int count, int modulate)
{
    if (!census3d && !(census3d = calloc(CENSUS3D, sizeof *census3d)))
        return;
    for (int i = 0; i < CENSUS3D; i++) {
        Census3D *e = &census3d[i];
        if (e->calls && (e->material != material || e->blend != (sky_state[2] & 0xff) || e->test != sky_state[3]))
            continue;
        if (!e->calls)
            *e = (Census3D){ material, sky_state[2] & 0xff, sky_state[3], vtype, 0, 0, modulate, pixel_mask };
        e->calls++;
        e->triangles += count / 3;
        return;
    }
}
void p4_census3d_report(void)
{
    if (!p4_probe_3d || !census3d)
        return;
    for (int i = 0; i < CENSUS3D && census3d[i].calls; i++) {
        const Census3D *e = &census3d[i];
        const unsigned *m = (const unsigned *)e->material;
        const char *texture = m && m[0] ? (const char *)m[0] + 0x10 : "-";
        p4_log("census3d %08x %-20.20s blend %02x test %05x colour %08x mod %d vtype %x draws %u tris %u plugin %08x %08x %08x %08x\n",
               (unsigned)e->material, texture, e->blend, e->test & 0xfffff, m ? m[1] : 0, e->modulate, e->vtype, e->calls, e->triangles,
               m ? m[6] : 0, m ? m[7] : 0, m ? m[8] : 0, m ? m[9] : 0);
    }
    memset(census3d, 0, CENSUS3D * sizeof *census3d);
}

/* Census and the debug hide for every 3D path (material draws, CPU-vertex meshes, skinned batches). */
static int census3d_skip(const Material *material, int vtype, int count, int modulate)
{
    if (p4_probe_3d)
        census3d_note(material, vtype, count, modulate);
    return p4_hide_material && (unsigned)material == p4_hide_material;
}

static const unsigned char *matfx_env_entry(const Material *material);
static int translucent_material(const Material *material, int modulate)
{
    /* b268: a MatFX env-map material is followed by its env pass, which writes the depth (PS2 GS dump): keep its order */
    return p4_translucent_late && p4_gs_test && !rtt_target && modulate && !matfx_env_entry(material) &&
        p4_late_rgb_material(sky_state[3],sky_state[2]?sky_state[2]:BLEND_NORMAL,pixel_mask,material->colour>>24);
}
static int translucent_defer(int prim,int vtype,int count,const void *indices,const void *vertices,const Material *material,int modulate);
static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices, const void *vertices, const Material *material, int modulate, const SceneLight *light)
{
    if (census3d_skip(material, vtype, count, modulate))
        return;
    int ge = (vtype & GU_NORMAL_8BIT) || draw_material_ge;
    prof_begin(PROF_DLIGHT);
    if (ge) { /* draw_material_ge: unlit, the light is zero; the material colour comes as the GE's ambient */
        bind_ge_light(material, modulate, light);
        work_ge_draws++;
    }
    prof_end(PROF_DLIGHT);
    draw_material_alpha = modulate ? material->colour >> 24 : 255;
    if (!ge && translucent_defer(prim,vtype,count,indices,vertices,material,modulate))
        return;
    prof_begin(PROF_DDRAW);
    draw_tested(prim, vtype, count, indices, vertices);
    prof_end(PROF_DDRAW);
    if (ge)
        st_enable(GU_LIGHTING, 0);
}

static SceneLight scene_lights(void)
{
    SceneLight out = {0};
    unsigned char *ambient = (void *)R32(0x764b64), *directional = (void *)R32(0x764b68);
    if (ambient && ambient[1] == 2 && (ambient[2] & 1))
        memcpy(&out.ambient, ambient + 24, sizeof(V3));
    if (directional && directional[1] == 1 && (directional[2] & 1)) {
        void *frame = *(void **)(directional + 4);
        if (frame) {
            memcpy(&out.diffuse, directional + 24, sizeof(V3));
            out.direction = p4r_003e9700(frame)->at;
        }
    }
    return out;
}

/* One vertex-array object: an atomic's geometry, or a world sector with no transform of its own. */
/* Per-mesh compact arrays kept between frames while what they are built from stays the same (same arrays and
   contents, light, model rotation, material). A mesh drawn twice in one frame with other inputs builds the second
   copy in the frame pool: the GE may still be reading the first. Budget-capped; full means build per frame. */
int p4_shade_fast = 1; /* 0: shade_colour per vertex, as b68 */
enum { CACHE_SLOTS = 4096 };
typedef struct { const void *mesh; unsigned key, frame, built; int vertices, indices, capacity, churn, vtype; void *v; unsigned short *idx; int alpha_lo, alpha_hi; } MeshCache;
static MeshCache mesh_cache[CACHE_SLOTS];
static SceUID cache_thread = -1; /* the drawing thread (b273 p4_mesh_cache_release) */
volatile int p4_cache_flush_pending; /* b276: set by files.c (the loader may not be the drawing thread) */
int p4_cache_flush_on_load = 1; /* b275: release the cache when a field pack (.p00) starts loading (files.c); 0: as b274 */
static unsigned cache_emergencies;
int p4_material_state = 1; /* b177: 1: GE-lit cache keys without the material; 2: unlit compact meshes too (material
                              colour as GE state); 0: the material in every cache key, as b176 */
int p4_cache_reuse_last = 1; /* 1: a miss may take its mesh's entry drawn last frame (pipeline off); 0: as b75 */
/* draw_geometry: declared in guard.h (b387) */
typedef struct { const Geometry *g; const void *pos, *uv, *pre, *nrm; int count; float du, dv; unsigned nsample, part[4]; } LockMemo;
static LockMemo lock_memo[1024];

/* Why a mesh's cache entry stops matching: the first input whose running hash differs (pointers, positions, UVs,
   prelight, then count + UV shift, then lighting and normals), vertices weighted. Heap: .bss is full. */
static unsigned (*churn_parts)[6];
static unsigned churn_count[7], churn_vertices[7];
static const char *const churn_names[7] = { "pointers", "positions", "uvs", "prelight", "shift", "light/normals", "material" };
static void churn_remember(int index, const unsigned part[6])
{
    if (!churn_parts)
        churn_parts = calloc(CACHE_SLOTS, sizeof *churn_parts);
    if (churn_parts)
        memcpy(churn_parts[index], part, sizeof churn_parts[index]);
}
/* b178: which geometries rebuild for their positions (battle: ~15 meshes, 29k vertices a frame), and what moves them:
   CPU skinning (a new array every frame), a lock (the game rewrote the morph target), or neither (written unlocked) */
int p4_churn_top = 1;
static Geometry *churn_atomic_g; /* the atomic being drawn (also when skinned on the CPU), else 0 */
static int churn_atomic_locked, churn_atomic_skinned;
typedef struct { const void *header; const Geometry *g; unsigned meshes, vertices, locked, skinned, flags, morphs, nverts, frames, last; char tex[24]; } ChurnTop;
static ChurnTop *churn_top; /* b257: heap on first use (.bss is full) */
static void churn_top_note(const void *header, const Material *material, int vertices)
{
    if (!churn_top && !(churn_top = calloc(32, sizeof *churn_top)))
        return;
    ChurnTop *e = 0, *low = &churn_top[0];
    for (int i = 0; i < 32; i++) {
        if (churn_top[i].header == header) { e = &churn_top[i]; break; }
        if (churn_top[i].vertices < low->vertices) low = &churn_top[i];
    }
    if (!e) {
        e = low;
        memset(e, 0, sizeof *e);
        e->header = header;
        e->g = churn_atomic_g;
        if (churn_atomic_g)
            e->flags = churn_atomic_g->flags, e->morphs = churn_atomic_g->numMorphTargets, e->nverts = churn_atomic_g->numVertices;
        void **texture = material ? material->texture : 0;
        if (texture && *texture)
            memcpy(e->tex, (const char *)*texture + 0x10, 23);
        e->last = ~0u;
    }
    e->meshes++;
    e->vertices += vertices;
    if (e->last != cache_frame) {
        e->last = cache_frame;
        e->frames++;
        e->locked += churn_atomic_locked != 0;
        e->skinned += churn_atomic_skinned;
    }
}
static void churn_top_report(unsigned frames)
{
    if (!churn_top)
        return;
    for (int n = 0; n < 12; n++) {
        ChurnTop *best = 0;
        for (int i = 0; i < 32; i++)
            if (churn_top[i].header && churn_top[i].vertices && (!best || churn_top[i].vertices > best->vertices))
                best = &churn_top[i];
        if (!best)
            break;
        p4_log("churn top geometry %p header %p v%u flags %x morphs %u: frames %u/%u vertices %u per frame meshes %u locked %u skinned %u tex %.23s\n",
               best->g, best->header, best->nverts, best->flags, best->morphs, best->frames, frames, best->vertices / frames, best->meshes,
               best->locked, best->skinned, best->tex);
        best->vertices = 0;
    }
    memset(churn_top, 0, 32 * sizeof *churn_top);
}
static int churn_why(int index, const unsigned part[6], int vertices)
{
    if (!churn_parts)
        return -1;
    int k = 0;
    while (k < 6 && churn_parts[index][k] == part[k])
        k++;
    churn_count[k]++;
    churn_vertices[k] += vertices;
    return k;
}
static void churn_report(unsigned frames)
{
    for (int k = 0; k < 7; k++)
        if (churn_count[k])
            p4_log("churn %s: %u meshes %u vertices per frame (material_state %d)\n", churn_names[k], churn_count[k] / frames, churn_vertices[k] / frames, p4_material_state);
    memset(churn_count, 0, sizeof churn_count);
    memset(churn_vertices, 0, sizeof churn_vertices);
}

/* may_alias: callers hash float arrays and structs (the UV shift, the light, matrices). Read through a plain unsigned *,
   GCC -O2 may load the words before the caller's float stores (strict aliasing): b104-b108 hashed stale stack words, so
   ~100 cache keys a frame changed at random (Dojima's room 50 -> 75 ms, Heaven differing by 18 ms between builds). */
typedef unsigned __attribute__((may_alias)) AliasWord;
static unsigned hash_words(unsigned h, const void *data, int bytes)
{
    const AliasWord *w = data;
    for (int i = 0; i < bytes / 4; i++)
        h = (h ^ w[i]) * 16777619u;
    return h;
}

/* Audit of the sampled key (b105): the key hashes 8 elements per array, so an array rewritten elsewhere in place
   (scrolling UVs of part of a mesh) can hit with old contents. Every 8th hit rehashes the whole arrays and compares
   them with the hashes taken when the entry was built; a difference is a stale hit (drawn with old vertices). */
static unsigned (*audit_hash)[3], audit_tick;
static void audit_inputs(unsigned out[3], const V3 *position, float (*uv)[2], const unsigned *preLit, int count)
{
    out[0] = hash_words(2166136261u, position, count * sizeof *position);
    out[1] = uv ? hash_words(2166136261u, uv, count * 8) : 0;
    out[2] = preLit ? hash_words(2166136261u, preLit, count * 4) : 0;
}

/* One entry per (mesh, inputs): the limousine draws ~30 meshes twice a frame with different inputs (b69: they rebuilt
   every frame). A miss takes an empty entry or one unused for two seconds. */
static MeshCache *cache_slot(const void *mesh, unsigned key, int *hit)
{
    /* entries of one mesh sit together; a miss reuses this mesh's entry that was not drawn this frame (its inputs
       changed), so animated meshes do not fill the budget with dead copies (b71: 1.5 MB full, 38 misses a frame) */
    unsigned i = ((unsigned)mesh >> 2) * 2654435761u >> 20; /* meshes are 12 bytes apart: >> 4 put a geometry's meshes in one 16-entry window (b87 Heaven: 60 rebuilds a frame) */
    if (cache_thread < 0)
        cache_thread = sceKernelGetThreadId();
    MeshCache *same = 0, *spare = 0;
    for (int probe = 0; probe < 16; probe++, i = (i + 1) & (CACHE_SLOTS - 1)) {
        MeshCache *e = &mesh_cache[i];
        if (e->mesh == mesh && e->key == key) {
            *hit = 1;
            return e;
        }
        /* Drawn this frame: the GE has not read it yet. Last frame: only the pipeline still reads it. Excluding last
           frame always (as b75) gave an animated mesh a new entry every frame: ~120 dead copies each, the 4 MB
           budget full in the first dialogue and battle (b76), and churn never saw the same entry come back. */
        int busy = e->frame == cache_frame || ((p4_pipeline || !p4_cache_reuse_last) && e->frame + 1 == cache_frame);
        if (!same && e->mesh == mesh && !busy)
            same = e;
        if (!spare && (!e->mesh || cache_frame - e->frame > 120))
            spare = e;
    }
    *hit = 0;
    return same ? same : spare;
}

/* b273: main.c's malloc wrappers, when an allocation fails (the game's chkmemAllocate writes through the null block:
   b271-b273 bus error loading f044 after Heaven, a 2 MB request with ~5 MB of cache spread over the heap). Frees every
   entry not drawn this frame or the last (the GE may still read those); static lists hold their own copies. Only on
   the drawing thread, which is the one that reads the entries. Returns the bytes freed. */
unsigned p4_mesh_cache_release(void)
{
    if (cache_thread < 0 || sceKernelGetThreadId() != cache_thread)
        return 0;
    unsigned freed = 0;
    for (int i = 0; i < CACHE_SLOTS; i++) {
        MeshCache *e = &mesh_cache[i];
        if (!e->v || e->frame == cache_frame || e->frame + 1 == cache_frame)
            continue;
        free(e->v);
        freed += e->capacity;
        cache_bytes -= e->capacity;
        e->v = 0, e->idx = 0, e->capacity = 0, e->mesh = 0, e->key = 0;
    }
    cache_emergencies++;
    p4_log("mesh cache emergency %u: freed %u bytes, %u left\n", cache_emergencies, freed, cache_bytes);
    return freed;
}

/* Contents change in place (scrolling UVs, skinned vertices): a few elements spread over the array stand for it. */
static unsigned hash_sample(unsigned h, const void *data, int count, int bytes)
{
    if (count <= 0)
        return h;
    for (int k = 0; k < 8; k++)
        h = hash_words(h, (const char *)data + (count - 1) * k / 7 * bytes, bytes);
    return h;
}

/* b84: 0 = original path, 1 = count only, 2 = reject offscreen meshes.
   Codes are rebuilt from the current positions, including CPU-skinned positions;
   no cached bounds can become stale when geometry changes in place. GE-skinned
   batches keep their existing path and are not included in these counters. */
int p4_mesh_frustum = 0;
static unsigned char *frustum_codes;
static unsigned frustum_arrays, frustum_vertices, frustum_meshes, frustum_outside, frustum_indices, frustum_us;
static int frustum_prepare(const V3 *position, int count, const Matrix *model)
{
    if (!p4_mesh_frustum || count <= 0 || count > MAX_GUARDED)
        return 0;
    if (!frustum_codes && !(frustum_codes = malloc(MAX_GUARDED)))
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    Mat4 m;
    guard_matrix(m, model);
    for (int i = 0; i < count; i++)
        frustum_codes[i] = frustum_code(m, &position[i].x);
    frustum_arrays++;
    frustum_vertices += count;
    frustum_us += sceKernelGetSystemTimeLow() - start;
    return 1;
}
static int frustum_reject(const Mesh *mesh, int count, int prepared)
{
    if (!prepared)
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    int outside = frustum_mesh_outside(frustum_codes, count, mesh->indices, mesh->numIndices);
    frustum_meshes++;
    if (outside) {
        frustum_outside++;
        frustum_indices += mesh->numIndices;
    }
    frustum_us += sceKernelGetSystemTimeLow() - start;
    return outside && p4_mesh_frustum == 2;
}
static void frustum_report(unsigned frames)
{
    p4_log("mesh frustum mode %d per frame: arrays %u vertices %u meshes %u outside %u outside_indices %u scan_us %u (GE skin excluded)\n",
           p4_mesh_frustum, frustum_arrays / frames, frustum_vertices / frames, frustum_meshes / frames,
           frustum_outside / frames, frustum_indices / frames, frustum_us / frames);
    frustum_arrays = frustum_vertices = frustum_meshes = frustum_outside = frustum_indices = frustum_us = 0;
}

/* Depth experiments use the ACTUAL indexed vertices in the current view, not
   an atomic's enormous sphere. 0 off; 1 count only; 2 hide wholly farther;
   3 hide wholly nearer. Meshes crossing the cut are retained in both modes.
   This diagnostic scan is intentionally identical in count and reject modes.
   It covers draw_meshes, not the separate GE-skinned batch path. */
int p4_probe_depth_mode;
float p4_probe_depth_cut = 2000;
static unsigned probe_depth_bins[6], probe_depth_meshes, probe_depth_hidden, probe_depth_indices, probe_depth_us;
static int probe_depth_reject(const Mesh *mesh, const V3 *position, int count, const Matrix *model)
{
    if (!p4_probe_depth_mode || !mesh->indices || mesh->numIndices <= 0)
        return 0;
    unsigned start = sceKernelGetSystemTimeLow();
    const Matrix *view = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    float ax = view->right.z, ay = view->up.z, az = view->at.z, offset = view->pos.z;
    if (model) {
        offset += model->pos.x*ax + model->pos.y*ay + model->pos.z*az;
        float x = model->right.x*ax + model->right.y*ay + model->right.z*az;
        float y = model->up.x*ax + model->up.y*ay + model->up.z*az;
        az = model->at.x*ax + model->at.y*ay + model->at.z*az;
        ax = x, ay = y;
    }
    float lo = INFINITY, hi = -INFINITY;
    for (int k = 0; k < mesh->numIndices; k++) {
        unsigned j = mesh->indices[k];
        if (j >= count)
            return 0;
        const V3 *p = &position[j];
        float z = p->x*ax + p->y*ay + p->z*az + offset;
        if (!isfinite(z))
            return 0;
        if (z < lo) lo = z;
        if (z > hi) hi = z;
    }
    int bin = lo < 500 ? 0 : lo < 1000 ? 1 : lo < 2000 ? 2 : lo < 4000 ? 3 : lo < 8000 ? 4 : 5;
    probe_depth_bins[bin]++;
    probe_depth_meshes++;
    int hide = (p4_probe_depth_mode == 2 && lo > p4_probe_depth_cut) ||
               (p4_probe_depth_mode == 3 && hi < p4_probe_depth_cut);
    if (hide) {
        probe_depth_hidden++;
        probe_depth_indices += mesh->numIndices;
    }
    probe_depth_us += sceKernelGetSystemTimeLow() - start;
    return hide;
}
static void probe_report(unsigned frames)
{
    p4_log("probe flags %x geometry %x keep_every %d selected %u hidden %u per frame\n",
           p4_probe_flags, p4_probe_geometry, p4_probe_keep_every, probe_selected / frames, probe_hidden / frames);
    p4_log("probe depth mode %d cut %d per frame: meshes %u hidden %u indices %u scan_us %u bins_minz_500_1000_2000_4000_8000_rest %u %u %u %u %u %u (GE skin excluded)\n",
           p4_probe_depth_mode, (int)p4_probe_depth_cut, probe_depth_meshes / frames, probe_depth_hidden / frames,
           probe_depth_indices / frames, probe_depth_us / frames, probe_depth_bins[0] / frames, probe_depth_bins[1] / frames,
           probe_depth_bins[2] / frames, probe_depth_bins[3] / frames, probe_depth_bins[4] / frames, probe_depth_bins[5] / frames);
    probe_selected = probe_hidden = 0;
    probe_depth_meshes = probe_depth_hidden = probe_depth_indices = probe_depth_us = 0;
    memset(probe_depth_bins, 0, sizeof probe_depth_bins);
}

int p4_mesh_compact = 1; /* 0: every mesh rebuilds the whole array, as b66 */
static unsigned *mesh_lit, *mesh_stamp, mesh_generation;
static unsigned short *mesh_remap;
static int mesh_scratch(int count)
{
    if (!mesh_lit) {
        mesh_lit = malloc(MAX_GUARDED * sizeof *mesh_lit);
        mesh_stamp = calloc(MAX_GUARDED, sizeof *mesh_stamp);
        mesh_remap = malloc(MAX_GUARDED * sizeof *mesh_remap);
        if (!mesh_lit || !mesh_stamp || !mesh_remap) {
            p4_log("mesh scratch failed, compact meshes off\n");
            p4_mesh_compact = 0;
            return 0;
        }
    }
    return count <= MAX_GUARDED;
}

#include "mesh_topology.h"
#include "meshkit.h"
#include <stdio.h>
int p4_topology_cache = 1;
unsigned p4_topology_budget = 192 * 1024;
static P4TopologyCache topology_cache;
static void topology_report(unsigned frames)
{
    p4_log("topology cache %d per frame: hits %u builds %u fallback %u bytes %u budget %u evictions %u\n",
           p4_topology_cache, topology_cache.hits / frames, topology_cache.builds / frames,
           topology_cache.misses / frames, topology_cache.bytes, p4_topology_budget, topology_cache.evictions);
    topology_cache.hits = topology_cache.builds = topology_cache.misses = topology_cache.evictions = 0;
}
static void draw_meshes_body(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere);
/* Instance replay. The game draws copies of one geometry back to back (Heaven: 84 flowers of 32 vertices, same
   arrays, only the matrix differs; b86 ~110 k cycles an atomic, nearly all fixed cost). After a copy is drawn, the
   GE words it wrote after its model matrix are kept; the next copy sends its matrix and those words again when
   nothing was written to the list in between, every input the words depend on is the same, the GE state cache
   ended the copy as it began it (so no state the words skipped can differ), and its sphere needs no guard clip. */
/* b189-b190 GE CALL records (p4_skin_call: GE-skinned batches; p4_mesh_call: mesh cache hits drawn whole). The
   state and draw words of bind_mask, bind_raster, bind_ge_light and draw_tested are recorded once into a ring and
   CALLed in later frames, so the CPU skips those calls. The key hashes every input they read: the drawn arrays,
   material, texture, light, TEST_1, masks, alpha bounds and the state cache gs (its camera fields aside: nothing here
   reads or sets them, and they change whenever the camera moves). A replay leaves gs and the bound texture globals as
   the recording did. Two records per drawn thing: the body and its shadow pass. An entry older than half the ring is
   copied forward before it is CALLed, and one frame writes at most an eighth of the ring, so an entry the GE may still
   read (up to 2 frames late) is never overwritten. GE CALLs nest 2 deep on hardware; this is level 1. Instance replay
   copies these BASE+CALL words like any others. */
typedef struct { unsigned h[2], start, words, draws, used; } GeCall;
int p4_skin_call = 1; /* 0: every skinned batch's state and draw built every frame, as b188 */
int p4_mesh_call = 1; /* 0: every mesh cache hit's state and draw built every frame, as b189 */
enum { CALL_RING_WORDS = 256 * 1024, CALL_FRAME_WORDS = CALL_RING_WORDS / 8, CALL_MAX_WORDS = 512 };
enum { CALL_GS_WORDS = (sizeof gs + 3) / 4, CALL_HEAD = CALL_GS_WORDS + 4 };
static unsigned *call_ring, call_head, call_frame, call_frame_words;
static unsigned call_hits[2], call_records[2], call_misses[2], call_refreshes, call_refused, call_full; /* [0] skin, [1] mesh */
#define CALL_CAM0 offsetof(__typeof__(gs), camera_known)
#define CALL_CAM1 offsetof(__typeof__(gs), texscale_known)
static unsigned *call_alloc(int words, unsigned *start)
{
    if (call_frame != cache_frame)
        call_frame = cache_frame, call_frame_words = 0;
    if (!call_ring && !(call_ring = memalign(64, CALL_RING_WORDS * 4)))
        return 0;
    unsigned at = call_head % CALL_RING_WORDS, skip = at + words > CALL_RING_WORDS ? CALL_RING_WORDS - at : 0;
    if (call_frame_words + skip + words > CALL_FRAME_WORDS) {
        call_full++;
        return 0;
    }
    call_head += skip;
    call_frame_words += skip + words;
    *start = call_head;
    call_head += words;
    return call_ring + *start % CALL_RING_WORDS;
}
static inline void call_mix(unsigned h[2], unsigned w)
{
    h[0] = (h[0] ^ w) * 16777619u;
    h[1] = (h[1] + w) * 2654435761u;
    h[1] ^= h[1] >> 15;
}
/* the inputs every recorded draw shares, into w; -1: not recorded (camera or feedback texture, no mesh mode, a probe
   or an experiment switch) */
static int call_inputs(unsigned *w, const Material *m, int modulate, const SceneLight *light)
{
    if (!gu_mesh_mode || p4_draw_batch <= 1 || p4_gs_test_raw_prelit || p4_probe_3d || p4_hide_material)
        return -1;
    Raster *r = m->texture ? *(Raster **)m->texture : 0;
    if (r && (r == &display_raster || rtt_find(r)))
        return -1;
    int n = 0;
    w[n++] = (unsigned)m, w[n++] = m->colour;
    memcpy(&w[n++], &m->ambient, 4);
    memcpy(&w[n++], &m->diffuse, 4);
    w[n++] = (unsigned)r, w[n++] = r ? (unsigned)r->originalPixels : 0, w[n++] = r ? (unsigned)r->palette : 0, w[n++] = texture_generation;
    w[n++] = modulate;
    memcpy(w + n, light, 36);
    n += 9;
    w[n++] = pixel_mask, w[n++] = sky_state[3], w[n++] = (unsigned)rtt_target, w[n++] = render_state[RS_ZWRITE];
    w[n++] = p4_screen_date | p4_rtt_date << 1 | p4_alpha_pixel << 2 | p4_gs_test << 3 | p4_gs_test_skip << 4 | p4_gs_mesh_alpha << 5 | p4_mipmaps << 6;
    w[n++] = draw_alpha_known, w[n++] = draw_skin_alpha ? draw_skin_alpha[0] : -1, w[n++] = draw_skin_alpha ? draw_skin_alpha[1] : -1;
    w[n++] = (unsigned)draw_fixed_alpha, w[n++] = draw_material_ge;
    return n;
}
static void call_key(const unsigned *w, int n, unsigned h[2])
{
    h[0] = 2166136261u, h[1] = 0x9e3779b9u;
    for (int i = 0; i < n; i++)
        call_mix(h, w[i]);
    const unsigned *s = (const unsigned *)&gs;
    for (unsigned i = 0; i < sizeof gs / 4; i++)
        if (i < CALL_CAM0 / 4 || i >= CALL_CAM1 / 4)
            call_mix(h, s[i]);
}
/* the record with this key, or 0 with *slot the one to record into (empty, else the longest unused) */
static GeCall *call_find(GeCall c[2], const unsigned h[2], GeCall **slot)
{
    GeCall *spare = 0;
    for (int i = 0; i < 2; i++) {
        if (c[i].words && c[i].h[0] == h[0] && c[i].h[1] == h[1])
            return &c[i];
        if (!spare || !c[i].words || (spare->words && c[i].used < spare->used))
            spare = &c[i];
    }
    *slot = spare;
    return 0;
}
static int call_replay(GeCall *c, int kind)
{
    unsigned age = call_head - c->start;
    if (age > CALL_RING_WORDS - CALL_MAX_WORDS) { /* overwritten since: record again */
        c->words = 0;
        return 0;
    }
    if (age > CALL_RING_WORDS / 2) {
        unsigned start, *to = call_alloc(c->words, &start);
        if (!to)
            return 0;
        memcpy(to, call_ring + c->start % CALL_RING_WORDS, c->words * 4);
        sceKernelDcacheWritebackRange(to, c->words * 4);
        c->start = start;
        call_refreshes++;
    }
    const unsigned *e = call_ring + c->start % CALL_RING_WORDS, *ge = e + CALL_HEAD;
    unsigned *l = gu_list->current;
    l[0] = 0x10u << 24 | ((unsigned)ge >> 8 & 0xf0000); /* BASE */
    l[1] = 0x0Au << 24 | ((unsigned)ge & 0xffffff);     /* CALL */
    gu_list->current = l + 2;
    memcpy(&gs, e, CALL_CAM0);
    memcpy((char *)&gs + CALL_CAM1, (const char *)e + CALL_CAM1, sizeof gs - CALL_CAM1);
    bound_alpha_min = e[CALL_GS_WORDS], bound_alpha_max = e[CALL_GS_WORDS + 1];
    memcpy(&texture_w, &e[CALL_GS_WORDS + 2], 4);
    memcpy(&texture_h, &e[CALL_GS_WORDS + 3], 4);
    c->used = cache_frame;
    call_hits[kind]++;
    if (ge_work_on)
        work_words(ge, e + c->words - 1);
    mesh_draw_calls += c->draws;
    draws_since_stall += c->draws;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}
/* the words from..current just went into the list for this draw: keep them as c */
static void call_record(GeCall *c, const unsigned h[2], const unsigned *from, int kind)
{
    int n = gu_list->current - from, draws = 0;
    if (n <= 0 || n > CALL_MAX_WORDS || !gu_mesh_mode) {
        call_refused++;
        return;
    }
    for (int i = 0; i < n; i++) {
        unsigned cmd = from[i] >> 24;
        if (cmd == 0x08 || cmd == 0x0a || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) { /* JUMP, CALL, RET, END, FINISH */
            call_refused++;
            return;
        }
        draws += cmd == 0x04;
    }
    unsigned start, *e = call_alloc(CALL_HEAD + n + 1, &start);
    if (!e)
        return;
    memcpy(e, &gs, sizeof gs);
    e[CALL_GS_WORDS] = bound_alpha_min, e[CALL_GS_WORDS + 1] = bound_alpha_max;
    memcpy(&e[CALL_GS_WORDS + 2], &texture_w, 4);
    memcpy(&e[CALL_GS_WORDS + 3], &texture_h, 4);
    memcpy(e + CALL_HEAD, from, n * 4);
    e[CALL_HEAD + n] = 0x0Bu << 24; /* RET */
    sceKernelDcacheWritebackRange(e, (CALL_HEAD + n + 1) * 4);
    c->h[0] = h[0], c->h[1] = h[1];
    c->start = start, c->words = CALL_HEAD + n + 1, c->draws = draws, c->used = cache_frame;
    call_records[kind]++;
}
static void call_report(void)
{
    p4_log("ge call skin %d mesh %d per 256 frames: skin hits %u records %u misses %u; mesh hits %u records %u misses %u; refreshes %u refused %u full %u ring %u KB\n",
           p4_skin_call, p4_mesh_call, call_hits[0], call_records[0], call_misses[0], call_hits[1], call_records[1], call_misses[1], call_refreshes,
           call_refused, call_full, CALL_RING_WORDS / 256);
    memset(call_hits, 0, sizeof call_hits), memset(call_records, 0, sizeof call_records), memset(call_misses, 0, sizeof call_misses);
    call_refreshes = call_refused = call_full = 0;
}

static GeCall (*mesh_calls)[2]; /* heap, one pair per mesh_cache slot: .bss is full */
static void sl_note(MeshCache *slot);
static unsigned *mesh_model_begin; /* b258: where mesh_state's model matrix words start (static_list.h) */
static int sl_recording; /* b258: a static list is being recorded: mesh cache hits inline, noted */
static int sl_ge_uncut; /* certified prepared mesh: GE handles near clip and offscreen rejection */
#include "static_uv.h"
int p4_static_uv_pack = 1, p4_static_uv_check = 0;
static unsigned static_uv_vertices, static_uv_bytes, static_uv_refused, static_uv_checks, static_uv_diffs, static_uv_why[6];
/* b353: the temporary UV buffers (256 KiB, 4 frames) ran out while recording: the list records again 2 frames later
   instead of keeping float UVs for good (Heaven's load records ~58k vertices at once) */
static int sl_uv_short;
static unsigned static_uv_retries;
static int sl_note_uv(MeshCache *slot, const void *data, unsigned bytes);
static void *static_uv_alloc(unsigned bytes);

static int draw_static_uv(MeshCache *slot, int prim, const Material *material, int modulate, const SceneLight *light)
{
    if (!sl_recording || !p4_static_uv_pack) return 0;
    /* vertices left with float UVs, by reason: 0 under 16 or over 65536 vertices, 1 deferred translucent, 2 MatFX env,
       3 vertex format, 4 UV range (span over 8 or non-finite), 5 temporary memory */
    int why = slot->vertices < 16 || slot->vertices > 65536 ? 0 : translucent_material(material, modulate) ? 1 :
              matfx_env_entry(material) ? 2 :
              (slot->vtype & GU_TEXTURE_BITS) != GU_TEXTURE_32BITF ||
              (slot->vtype & GU_VERTEX_BITS) != GU_VERTEX_32BITF || (slot->vtype & GU_WEIGHT_BITS) ||
              (slot->vtype & GU_COLOR_BITS) != GU_COLOR_8888 ||
              ((slot->vtype & GU_NORMAL_BITS) && (slot->vtype & GU_NORMAL_BITS) != GU_NORMAL_8BIT) ? 3 : -1;
    if (why >= 0) { static_uv_why[why] += slot->vertices; return 0; }
    int stride = slot->vtype & GU_NORMAL_8BIT ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    int bytes = slot->vertices * (stride - 4);
    float decode[4];
    void *packed = static_uv_alloc(bytes);
    if (!packed || !static_uv_pack(slot->v, slot->vertices, stride, packed, decode)) {
        static_uv_why[packed ? 4 : 5] += slot->vertices;
        if (!packed && bytes <= 128 * 1024) sl_uv_short = 1; /* fits once the buffers in flight retire */
        static_uv_refused++;
        return 0;
    }
    if (p4_static_uv_check) {
        static_uv_checks++;
        if (!static_uv_same(slot->v, packed, slot->vertices, stride, decode)) {
            static_uv_diffs++;
            p4_static_uv_pack = 0;
            return 0;
        }
    }
    if (!sl_note_uv(slot, packed, bytes)) return 0;
    sceKernelDcacheWritebackRange(packed, bytes);
    bind_texscale(decode);
    draw_slot_v = packed, draw_slot_alpha = &slot->alpha_lo;
    int vtype = (slot->vtype & ~GU_TEXTURE_BITS) | GU_TEXTURE_16BIT;
    draw_mesh_vertices(prim, vtype, slot->indices, slot->idx, packed, material, modulate, light);
    draw_slot_v = 0;
    bind_texscale(0);
    static_uv_vertices += slot->vertices;
    static_uv_bytes += slot->vertices * 4;
    return 1;
}
static void draw_mesh_hit(MeshCache *slot, int prim, const Material *material, int modulate, const SceneLight *light)
{
    unsigned w[48], h[2];
    GeCall *call = 0, *rec = 0;
    if (sl_recording)
        sl_note(slot);
    int n = !sl_recording && p4_mesh_call && !translucent_material(material,modulate) && (mesh_calls || (mesh_calls = calloc(CACHE_SLOTS, sizeof *mesh_calls))) ? call_inputs(w, material, modulate, light) : -1;
    if (n >= 0) {
        w[n++] = (unsigned)slot, w[n++] = (unsigned)slot->v, w[n++] = (unsigned)slot->idx, w[n++] = slot->vtype, w[n++] = slot->indices;
        w[n++] = slot->key, w[n++] = slot->built, w[n++] = prim, w[n++] = slot->alpha_lo, w[n++] = slot->alpha_hi;
        call_key(w, n, h);
        call = call_find(mesh_calls[slot - mesh_cache], h, &rec);
        if (call && call_replay(call, 1)) {
            return;
        }
        if (call) /* found but overwritten or no room to refresh: record it again */
            rec = call;
        else if (rec)
            call_misses[1]++;
    }
    const unsigned *from = gu_list->current;
    void **texture = material->texture;
    prof_begin(PROF_HBIND);
    bind_mask(material->colour >> 24);
    bind_raster(texture ? *texture : 0, GU_REPEAT);
    prof_end(PROF_HBIND);
    if (draw_static_uv(slot, prim, material, modulate, light)) return;
    draw_slot_v = slot->v, draw_slot_alpha = &slot->alpha_lo;
    draw_mesh_vertices(prim, slot->vtype, slot->indices, slot->idx, slot->v, material, modulate, light);
    draw_slot_v = 0;
    if (rec)
        call_record(rec, h, from, 1);
}

int p4_instance_replay = 1; /* 0: every copy through draw_meshes_body, as b93 */
int p4_replay_census = 0; /* b207: 1 counts what instance replays send (replay_census) */
static unsigned *mesh_model_mark;
enum { REPLAY_MESHES = 8, REPLAY_WORDS = 512 };
static struct {
    int valid, count, modulate, draws;
    unsigned frame, tex_generation, pixel;
    const void *header, *position, *uv, *prelit, *normals, *camera;
    unsigned *from, *end;
    int render[MAX_STATE];
    unsigned sky[8], camera_view[19], material[REPLAY_MESHES][3];
    SceneLight light;
} replay;
static unsigned replay_draws, replay_records, replay_refusals[6];
/* b207: what an instance replay sends, its CALLs followed one level (BASE + CALL into the GeCall ring): words, draws,
   CLUT loads (LOADCLUT 0xC4), texture flushes (0xCB) and texture address words (0xA0). b201 Margaret: 84 flowers a
   frame, 32 vertices each, ~2.5 ms of GE with the timer: are they paying a CLUT load and a flush each? */
static unsigned replay_words, replay_prims, replay_cluts, replay_flushes, replay_texaddr;
static void replay_census(const unsigned *w, const unsigned *end, int depth)
{
    unsigned base = 0;
    for (; w < end; w++) {
        unsigned cmd = *w >> 24;
        replay_words++;
        replay_prims += cmd == 0x04, replay_cluts += cmd == 0xC4, replay_flushes += cmd == 0xCB, replay_texaddr += cmd == 0xA0;
        if (cmd == 0x10)
            base = *w & 0xffffff;
        else if (cmd == 0x0A && depth == 0) { /* CALL: its words up to RET */
            const unsigned *c = (const unsigned *)((base & 0xf0000) << 8 | (*w & 0xffffff)), *stop = c + CALL_MAX_WORDS + 1;
            const unsigned *r = c;
            while (r < stop && *r >> 24 != 0x0B)
                r++;
            replay_census(c, r, 1);
        } else if (cmd == 0x0B)
            return;
    }
}
static unsigned char gs_before[sizeof gs];
static int replay_inputs(MeshHeader *header, const Matrix *model, unsigned material[REPLAY_MESHES][3], unsigned view[19])
{
    if (!model || header->numMeshes > REPLAY_MESHES || p4_mesh_frustum || p4_probe_depth_mode)
        return 0;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    memset(material, 0, REPLAY_MESHES * 3 * 4);
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        material[i][0] = mesh->material->colour;
        memcpy(&material[i][1], &mesh->material->ambient, 4);
        memcpy(&material[i][2], &mesh->material->diffuse, 4);
    }
    memcpy(view, (char *)current_camera + CAMERA_VIEW, 16 * 4);
    memcpy(view + 16, (char *)current_camera + CAMERA_NEAR, 3 * sizeof *view); /* near, far, fog (b322) */
    return 1;
}
/* b308 p4_guard_big R (ratio, 0 off): a geometry whose longest triangle edge, scaled by its model matrix, is shorter
   than R times the camera depth of its sphere's nearest point skips the CPU guard-band cut and goes to the GE whole,
   also through the static lists (b307 used an absolute length: the battle arena's floor near the camera broke). The cut exists
   for triangles the GE would drop (screen XY past 0..4096); in Heaven with the cut off only the huge cloud-sea
   triangles broke (PPSSPP, 2026-11-16). The longest edge is measured once per geometry, keyed by its sphere.
   b388: on by default at 1.0 (b366 Margaret dialogue: 256 uncut, 0 cut, no frame change; docs/hardware_b366.md). */
float p4_guard_big = 1.0f;
/* b388 p4_guard_tiny L (world length, 0 off): a geometry whose longest edge, scaled by its model matrix, is shorter than L
   skips the guard cut at any depth, also next to the camera: a tiny triangle the GE drops at the near plane is a small
   hole (as characters with p4_skin_guard 0). An absolute length broke the arena floor near the camera in b308: pick L
   from the log's "guard cut ... longest edge" histogram, below the floors' edges. */
float p4_guard_tiny = 0;
static unsigned guard_tiny_hits;
enum { GUARD_EDGES = 1024 };
static struct { const float *sphere; float edge; } *guard_edges; /* heap: .bss must end below the data image */
static unsigned guard_small_hits, guard_big_hits;
static float guard_edge_top[4];
static float *guard_edge_slot(const float *sphere)
{
    if (!guard_edges && !(guard_edges = calloc(GUARD_EDGES, sizeof *guard_edges)))
        return 0;
    unsigned i = ((unsigned)sphere >> 4) * 2654435761u >> 22;
    if (guard_edges[i].sphere != sphere)
        return 0;
    return &guard_edges[i].edge;
}
static void guard_edge_note(const MeshHeader *header, const V3 *position, int count, const float *sphere)
{
    if (!(p4_guard_big > 0 || p4_guard_tiny > 0) || !sphere || guard_edge_slot(sphere) || (!guard_edges && !(guard_edges = calloc(GUARD_EDGES, sizeof *guard_edges))))
        return;
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP;
    const Mesh *mesh = (const Mesh *)((const char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    float worst = 0;
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        int n = mesh->numIndices;
        for (int t = 0; t + 2 < n; t += strip ? 1 : 3) {
            int v[3] = { mesh->indices[t], mesh->indices[t + 1], mesh->indices[t + 2] };
            for (int k = 0; k < 3; k++) {
                int a = v[k], b = v[(k + 1) % 3];
                if (a >= count || b >= count)
                    continue;
                float dx = position[a].x - position[b].x, dy = position[a].y - position[b].y, dz = position[a].z - position[b].z;
                float d = dx * dx + dy * dy + dz * dz;
                if (d > worst)
                    worst = d;
            }
        }
    }
    worst = sqrtf(worst);
    unsigned i = ((unsigned)sphere >> 4) * 2654435761u >> 22;
    guard_edges[i].sphere = sphere, guard_edges[i].edge = worst;
    for (int k = 0; k < 4; k++)
        if (worst > guard_edge_top[k]) {
            float t = guard_edge_top[k];
            guard_edge_top[k] = worst, worst = t;
        }
}
/* 1: this geometry's triangles are all short enough to go to the GE uncut */
static int guard_small(const Matrix *model, const float *sphere)
{
    if (!(p4_guard_big > 0 || p4_guard_tiny > 0) || !sphere)
        return 0;
    const float *edge = guard_edge_slot(sphere);
    if (!edge)
        return 0;
    float scale = 1, x = sphere[0], y = sphere[1], z = sphere[2];
    if (model) {
        const Matrix *m = model;
        float a = m->right.x * m->right.x + m->right.y * m->right.y + m->right.z * m->right.z;
        float b = m->up.x * m->up.x + m->up.y * m->up.y + m->up.z * m->up.z;
        float c = m->at.x * m->at.x + m->at.y * m->at.y + m->at.z * m->at.z;
        float big = a > b ? a : b;
        scale = sqrtf(big > c ? big : c);
    }
    if (*edge * scale < p4_guard_tiny) { /* b388: no depth test at all */
        guard_tiny_hits++;
        return 1;
    }
    if (!(p4_guard_big > 0))
        return 0;
    if (model) {
        const Matrix *m = model;
        float wx = x * m->right.x + y * m->up.x + z * m->at.x + m->pos.x;
        float wy = x * m->right.y + y * m->up.y + z * m->at.y + m->pos.y;
        float wz = x * m->right.z + y * m->up.z + z * m->at.z + m->pos.z;
        x = wx, y = wy, z = wz;
    }
    /* depth of the sphere's nearest point in the camera; touching the near plane always takes the cut */
    const Matrix *view = (const Matrix *)((const char *)current_camera + CAMERA_VIEW);
    float near = *(const float *)((const char *)current_camera + CAMERA_NEAR);
    float depth = x * view->right.z + y * view->up.z + z * view->at.z + view->pos.z - sphere[3] * scale;
    int small = depth > 2 * near && *edge * scale < p4_guard_big * depth;
    guard_small_hits += small, guard_big_hits += !small;
    return small;
}
static void guard_big_report(void)
{
    p4_log("guard big %d: uncut %u cut %u; tiny %d uncut %u; longest edges %d %d %d %d\n", (int)p4_guard_big, guard_small_hits,
           guard_big_hits, (int)p4_guard_tiny, guard_tiny_hits, (int)guard_edge_top[0], (int)guard_edge_top[1], (int)guard_edge_top[2],
           (int)guard_edge_top[3]);
    guard_small_hits = guard_big_hits = guard_tiny_hits = 0;
}
static int replay_unguarded(const Matrix *model, const float *sphere)
{
    if (!p4_guard_clip || !guard_code)
        return 1;
    if (guard_small(model, sphere))
        return 1;
    if (!p4_guard_world || !sphere || !(sphere[3] > 0))
        return 0;
    guard_world_planes();
    return guard_sphere_world(model, sphere, *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f);
}
static int replay_try(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    if (!replay.valid || replay.frame != cache_frame || gu_list->current != replay.end || !gu_mesh_mode)
        return 0;
    if (replay.header != header || replay.position != position || replay.uv != uv || replay.prelit != preLit || replay.count != count ||
        replay.modulate != modulate || replay.normals != normals || replay.camera != current_camera || replay.tex_generation != texture_generation)
        return 0;
    unsigned material[REPLAY_MESHES][3], view[19];
    if (!replay_inputs(header, model, material, view))
        return 0;
    int why = 0;
    if (memcmp(replay.render, render_state, sizeof replay.render) || memcmp(replay.sky, sky_state, sizeof replay.sky) || replay.pixel != pixel_mask)
        why = 1;
    else if (memcmp(replay.material, material, sizeof material))
        why = 2;
    else if (memcmp(replay.camera_view, view, sizeof view))
        why = 3;
    else if (normals) {
        prof_begin(PROF_RLIGHT);
        SceneLight light = scene_lights();
        prof_end(PROF_RLIGHT);
        if (memcmp(&light, &replay.light, sizeof light))
            why = 4;
    }
    prof_begin(PROF_RGUARD);
    if (!why && !replay_unguarded(model, sphere))
        why = 5;
    prof_end(PROF_RGUARD);
    if (why) {
        replay_refusals[why]++;
        return 0;
    }
    set_matrix(GU_MODEL, model);
    int words = replay.end - replay.from;
    unsigned *to = gu_list->current;
    memcpy(to, replay.from, words * 4);
    gu_list->current = to + words;
    replay.from = to;
    replay.end = to + words;
    if (p4_replay_census)
        replay_census(to, to + words, 0);
    if (ge_work_on)
        work_words(to, to + words);
    mesh_draw_calls += replay.draws;
    replay_draws++;
    draws_since_stall += replay.draws;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}
static void replay_record(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere, unsigned *start)
{
    replay.valid = 0;
    if (!gu_mesh_mode || !mesh_model_mark || mesh_model_mark < start || mesh_model_mark > gu_list->current || memcmp(gs_before, &gs, sizeof gs))
        return;
    int words = gu_list->current - mesh_model_mark, draws = 0;
    if (words <= 0 || words > REPLAY_WORDS)
        return;
    for (int i = 0; i < words; i++) {
        unsigned cmd = mesh_model_mark[i] >> 24;
        if (cmd == 0x08 || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) /* JUMP, RET, END, FINISH: inline data or flow (b190: a CALL to a GeCall ring entry copies fine) */
            return;
        draws += cmd == 0x04;
    }
    if (!replay_inputs(header, model, replay.material, replay.camera_view) || !replay_unguarded(model, sphere))
        return;
    replay.valid = 1;
    replay.frame = cache_frame, replay.tex_generation = texture_generation, replay.pixel = pixel_mask;
    replay.header = header, replay.position = position, replay.uv = uv, replay.prelit = preLit, replay.normals = normals;
    replay.count = count, replay.modulate = modulate, replay.camera = current_camera, replay.draws = draws;
    memcpy(replay.render, render_state, sizeof replay.render);
    memcpy(replay.sky, sky_state, sizeof replay.sky);
    if (normals)
        replay.light = scene_lights();
    replay.from = mesh_model_mark;
    replay.end = gu_list->current;
    replay_records++;
}
static SceneLight mesh_state(const Matrix *model, int lit);
/* the switches a recorded list depends on (instance replays, static and skin lists) */
static unsigned state_bits(void)
{
    return p4_screen_date | p4_rtt_date << 1 | p4_alpha_pixel << 2 | p4_gs_test << 3 | p4_gs_test_skip << 4 | p4_gs_mesh_alpha << 5 | p4_mipmaps << 6 |
           p4_mesh_call << 7 | p4_guard_clip << 8 | p4_ge_light << 9 | (p4_fog & 3) << 10 | (p4_material_state & 3) << 12;
}
static void replay_report(unsigned frames)
{
    p4_log("instance replay %d per frame: draws %u records %u refused state %u material %u camera %u light %u guard %u\n", p4_instance_replay,
           replay_draws / frames, replay_records / frames, replay_refusals[1] / frames, replay_refusals[2] / frames, replay_refusals[3] / frames,
           replay_refusals[4] / frames, replay_refusals[5] / frames);
    if (p4_replay_census && replay_draws)
        p4_log("instance replay sends per replay: words %u draws %u clut loads %u/100 flushes %u/100 texture address %u/100 (%u replays)\n",
               replay_words / replay_draws, replay_prims / replay_draws, replay_cluts * 100 / replay_draws, replay_flushes * 100 / replay_draws,
               replay_texaddr * 100 / replay_draws, replay_draws);
    replay_words = replay_prims = replay_cluts = replay_flushes = replay_texaddr = 0;
    replay_draws = replay_records = 0;
    memset(replay_refusals, 0, sizeof replay_refusals);
}
static void draw_meshes(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    prof_begin(PROF_MESH);
    int late = 0;
    if (p4_translucent_late && modulate && ((sky_state[3]>>12)&3)==3) {
        const Mesh *m = (const Mesh *)((const char *)header+sizeof *header+header->firstMeshOffset);
        for (unsigned i=0;i<header->numMeshes&&!late;++i) late=translucent_material(m[i].material,modulate);
    }
    prof_begin(PROF_RPLAY); /* b230: what a replay costs (b229: ~15k cycles each, 87 a frame in the dialogue) */
    int replayed = !late && p4_instance_replay && model && replay_try(header, position, uv, preLit, count, model, modulate, normals, sphere);
    prof_end(PROF_RPLAY);
    if (replayed) {
        prof_end(PROF_MESH);
        return;
    }
    unsigned *start = gu_list->current;
    int record = !late && p4_instance_replay && model && gu_mesh_mode;
    if (record)
        memcpy(gs_before, &gs, sizeof gs);
    mesh_model_mark = 0;
    draw_meshes_body(header, position, uv, preLit, count, model, modulate, normals, sphere);
    draw_material_ge = 0; /* the body sets it for its compact meshes and may return early */
    if (record)
        replay_record(header, position, uv, preLit, count, model, modulate, normals, sphere, start);
    else
        replay.valid = 0;
    prof_end(PROF_MESH);
}

/* RenderWare fog (rwRENDERSTATEFOGENABLE 14, FOGCOLOR 15 as A8R8G8B8, FOGTYPE 16), b322 from MeMeNT0xMoRii's fork
   (home19, P4-022: dungeon walls hold their colour to a hard edge instead of fading): from the camera's fog plane to
   the Sky far fog plane, or to the far clip plane when that is unset. The GE's clear-colour weight is
   (viewZ + FOG1) * FOG2. b323: static and skin lists carry the fog state (sl_forget) and key it (sl_fog_key). */
enum { GE_FOG1 = 0xcd, GE_FOG2 = 0xce, GE_FOG_COLOR = 0xcf };
int p4_fog = 1; /* b339: on by default */
static struct { int known; float end, slope; unsigned colour; } fog_sent;
static void bind_fog(void)
{
    int on = p4_fog && current_camera && render_state[RS_FOG_ENABLE] && render_state[RS_FOG_TYPE] == 1;
    float end = 0.0f, slope = 0.0f;
    if (on) {
        const char *camera = current_camera;
        memcpy(&end, &sky_state[SKY_FAR_FOG_PLANE], sizeof end);
        if (end == 0.0f)
            end = *(const float *)(camera + CAMERA_FAR);
        float span = end - *(const float *)(camera + CAMERA_FOG);
        if (isfinite(span) && span > 0.0f)
            slope = -1.0f / span;
        on = isfinite(slope) && slope < 0.0f;
    }
    gu_fog_active = on;
    st_enable(GU_FOG, on);
    if (!on)
        return;
    unsigned c = render_state[RS_FOG_COLOR];
    unsigned colour = (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
    if (gu_mesh_mode && fog_sent.known && fog_sent.end == end && fog_sent.slope == slope && fog_sent.colour == colour)
        return;
    fog_sent.known = gu_mesh_mode, fog_sent.end = end, fog_sent.slope = slope, fog_sent.colour = colour;
    if (p4_ge_raw) { /* b366 */
        union { float f; unsigned u; } e = { -end }, s = { slope };
        ge_word(GE_FOG1, e.u >> 8), ge_word(GE_FOG2, s.u >> 8), ge_word(GE_FOG_COLOR, colour);
        return;
    }
    sceGuSendCommandf(GE_FOG1, -end);
    sceGuSendCommandf(GE_FOG2, slope);
    sceGuSendCommandi(GE_FOG_COLOR, colour);
}

/* The state of every 3D mesh draw: depth, alpha test, blend, camera, the model matrix (0: the world). */
static Matrix translucent_model;
static SceneLight mesh_state(const Matrix *model, int lit)
{
    translucent_model = model ? *model : (Matrix){.right={1,0,0},.up={0,1,0},.at={0,0,1}};
    prof_begin(PROF_LIGHTS);
    SceneLight light = lit ? scene_lights() : (SceneLight){0};
    prof_end(PROF_LIGHTS);
    gu_mesh_begin();
    bind_depth();
    st_enable(GU_CLIP_PLANES, 1);
    st_enable(GU_ALPHA_TEST, 1);
    st_alpha_func(GU_GREATER, 8);
    st_enable(GU_BLEND, 1);
    unsigned blend = sky_state[2] ? sky_state[2] : BLEND_NORMAL;
    if (!gu_mesh_mode || !gs.blend_known || gs.blend != (blend & 0xff)) {
        set_blend(blend);
        gs.blend_known = gu_mesh_mode, gs.blend = blend & 0xff;
    }
    bind_texscale(0);
    bind_fog();
    set_camera();
    mesh_model_begin = gu_list->current;
    if (model) {
        set_matrix(GU_MODEL, model);
    } else {
        ScePspFMatrix4 identity;
        gumLoadIdentity(&identity);
        sceGuSetMatrix(GU_MODEL, &identity);
    }
    mesh_model_mark = gu_list->current;
    return light;
}

static void ib_boundary(void); /* far_lod.h: a pending instance batch is drawn before anything else */
#define TP_BOUNDARY ib_boundary
#include "translucent_pass.h"

static void draw_meshes_body(MeshHeader *header, const V3 *position, float (*uv)[2], const unsigned *preLit, int count, const Matrix *model, int modulate, const V3 *normals, const float *sphere)
{
    prof_begin(PROF_MFRUST);
    int frustum_ready = frustum_prepare(position, count, model);
    prof_end(PROF_MFRUST);
    prof_begin(PROF_SETUP);
    begin_frame();
    draw_prelit = preLit;
    draw_count = count;
    draw_prelit_min = -1;
    draw_alpha_known = 1;
    Vertex3D *vertices = 0;
    unsigned previous_colour = 0;
    const Material *previous_material = 0;
    SceneLight light = mesh_state(model, normals != 0);
    int prim = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP ? GU_TRIANGLE_STRIP : GU_TRIANGLES;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    /* Scrolled UVs (the station's cloud-shadow pass reached v = -969605) lose their fraction in the GE's float24 and
       sample in speckled streaks. Textures here repeat, so take the whole part of the smallest coordinate off. */
    float du = 0, dv = 0;
    /* b167 (p4_lock_memo): RenderWare on the PS2 re-instances a geometry only when lockedSinceLastInst is set, so the
       game must lock to change its arrays. Unlocked since we last drew it: reuse its sampled hash and UV shift (the
       samples were cache misses, ~0.9 Mcycles a frame in the room). Clearing the flag below is what instancing did. */
    LockMemo *lm = 0;
    int lm_hit = 0;
    if (p4_lock_memo && draw_geometry && p4_mesh_cache && p4_mesh_compact) {
        lm = &lock_memo[((unsigned)draw_geometry >> 4) * 2654435761u >> 22];
        lm_hit = lm->g == draw_geometry && !draw_geometry->locked && lm->pos == position && lm->uv == uv && lm->pre == preLit &&
                 lm->nrm == normals && lm->count == count;
        lock_memo_hits += lm_hit;
    }
    prof_begin(PROF_UVSCAN);
    if (lm_hit)
        du = lm->du, dv = lm->dv;
    else if (uv && p4_uv_rebase && count > 0) {
        /* b137: the shift only has to be whole (textures repeat), not the exact minimum, so it is kept per UV array
           and recomputed when 8 sampled coordinates change (scrolled UVs move all of them). b129 Heaven: the scan
           cost 0.68 Mcycles a frame. */
        static struct UvMemo { const void *uv; int count; unsigned sample; float du, dv; } *uv_memo; /* b145: 256 slots hit 74 of 140 in the room; b261: 1024 on the heap (.bss is full) */
        unsigned slot = ((unsigned)uv >> 3) * 2654435761u >> 22;
        /* b152: only large arrays (where the scan costs): the normal-attack target beam (a few vertices, UVs moved every
           frame) drew striped with a remembered shift; small arrays are scanned every time */
        if (!uv_memo && p4_uv_memo)
            uv_memo = calloc(1024, sizeof *uv_memo);
        int memo = p4_uv_memo && count >= 64 && uv_memo;
        unsigned sample = memo ? hash_sample(2166136261u, uv, count, 8) : 0;
        if (memo && uv_memo[slot].uv == uv && uv_memo[slot].count == count && uv_memo[slot].sample == sample) {
            du = uv_memo[slot].du, dv = uv_memo[slot].dv;
            uv_memo_hits++;
        } else {
            float mu = uv[0][0], mv = uv[0][1];
            for (int j = 1; j < count; j++) {
                if (uv[j][0] < mu) mu = uv[j][0];
                if (uv[j][1] < mv) mv = uv[j][1];
            }
            float step = p4_uv_shift_step > 1 ? (float)p4_uv_shift_step : 1.0f;
            if (mu < -16 || mu > 16) du = floorf(mu / step) * step;
            if (mv < -16 || mv > 16) dv = floorf(mv / step) * step;
            if (memo)
                uv_memo[slot].uv = uv, uv_memo[slot].count = count, uv_memo[slot].sample = sample, uv_memo[slot].du = du, uv_memo[slot].dv = dv;
        }
        if (du || dv) uv_rebased++;
    }
    prof_end(PROF_UVSCAN);
    prof_end(PROF_SETUP);
    /* with boxes the per-mesh boxes decide: the array is only asked whether its sphere is inside the band */
    int box_mode = p4_guard_boxes && p4_guard_compact && p4_guard_world && p4_mesh_compact && p4_mesh_cache && sphere && sphere[3] > 0 &&
                   header->numMeshes <= 256;
    unsigned guarded = 0;
    guard_edge_note(header, position, count, sphere);
    if (!sl_ge_uncut && p4_guard_clip && guard_code && count <= MAX_GUARDED && !guard_small(model, sphere)) {
        if (box_mode) {
            prof_begin(PROF_GUARD);
            guard_world_planes();
            guarded = !guard_sphere_world(model, sphere, near);
            guard_spheres += !guarded;
            prof_end(PROF_GUARD);
        } else
            guarded = guard_classify(position, count, model, near, sphere);
    }
    /* Profiled in the limousine (b66): 79% of the frame's CPU was here. A lit geometry re-shaded every vertex for each
       material, and every mesh copied the whole array. Shade each vertex once; give each mesh only the vertices its
       indices use, renumbered. */
    if (p4_mesh_compact && mesh_scratch(count)) {
        const Material *lit_for = 0;
        unsigned shade_generation = 0, full_generation = 0, full_colour = 0;
        Vertex3D *full = 0;
        unsigned geometry_key = 0;
        unsigned part[6] = { 0 }; /* the key after each input, for churn_why */
        int ge_lit = normals && p4_ge_light;
        int ge_colour = !normals && modulate && p4_material_state == 2;
        draw_material_ge = ge_colour;
        prof_begin(PROF_KEY);
        if (p4_mesh_cache) {
            unsigned h, nsample;
            if (lm_hit) {
                memcpy(part, lm->part, sizeof lm->part);
                h = part[3];
                nsample = lm->nsample;
            } else {
                h = 2166136261u;
                const void *pointers[5] = { position, uv, preLit, normals, (void *)modulate };
                h = hash_words(h, pointers, sizeof pointers);
                part[0] = h;
                h = hash_sample(h, position, count, sizeof *position);
                part[1] = h;
                if (uv)
                    h = hash_sample(h, uv, count, 8);
                part[2] = h;
                if (preLit)
                    h = hash_sample(h, preLit, count, 4);
                part[3] = h;
                nsample = normals ? hash_sample(2166136261u, normals, count, sizeof *normals) : 0;
            }
            h = hash_words(h, &count, 4);
            float shift[2] = { du, dv };
            h = hash_words(h, shift, sizeof shift);
            part[4] = h;
            if (normals && ge_lit) {
                h = hash_words(h, &ge_lit, 4); /* the GE lights it: light and rotation are not in the vertices */
                h = hash_words(h, &nsample, 4);
            } else if (normals) {
                h = hash_words(h, &light, sizeof light);
                if (model)
                    h = hash_words(h, model, 12 * 4); /* rotation rows; position does not light */
                h = hash_words(h, &nsample, 4);
            }
            part[5] = h;
            geometry_key = h | 1;
            if (lm && !lm_hit) {
                *lm = (LockMemo){ draw_geometry, position, uv, preLit, normals, count, du, dv, nsample, { part[0], part[1], part[2], part[3] } };
                draw_geometry->locked = 0;
            }
        }
        prof_end(PROF_KEY);
        /* which meshes touch a vertex beyond the band, decided before draw_guarded_compact overwrites guard_code */
        static unsigned char clip_flags[256];
        int compact_guard = guarded && p4_guard_compact && header->numMeshes <= 256;
        if (compact_guard && box_mode)
            memset(clip_flags, 1, header->numMeshes);
        else if (compact_guard) {
            const Mesh *mm = mesh;
            for (int i = 0; i < header->numMeshes; i++, mm++) {
                unsigned any = 0;
                const unsigned short *ix = mm->indices;
                for (int k = 0; k < mm->numIndices; k++)
                    any |= guard_code[ix[k]];
                clip_flags[i] = (any & GUARD_MASK) != 0;
            }
        }
        for (int i = 0; i < header->numMeshes; i++, mesh++) {
            if (probe_depth_reject(mesh, position, count, model))
                continue;
            if (frustum_reject(mesh, count, frustum_ready))
                continue;
            const Material *material = mesh->material;
            MeshCache *slot = 0;
            unsigned key = 0;
            int clip_this = 0;
            if (compact_guard)
                clip_this = clip_flags[i];
            else if (guarded) {
                unsigned any = 0;
                const unsigned short *ix = mesh->indices;
                for (int k = 0; k < mesh->numIndices; k++)
                    any |= guard_code[ix[k]];
                clip_this = (any & GUARD_MASK) != 0;
            }
            if (geometry_key && (!clip_this || compact_guard)) {
                /* b177: GE-lit vertices do not hold the material (bind_ge_light sends it): a fading or recoloured
                   material keeps its entry */
                unsigned mk[3] = { material->colour, *(unsigned *)&material->ambient, *(unsigned *)&material->diffuse };
                key = (ge_lit && p4_material_state) || ge_colour ? geometry_key : hash_words(geometry_key, mk, sizeof mk) | 1;
                int hit;
                prof_begin(PROF_MSLOT);
                slot = cache_slot(mesh, key, &hit);
                if (!hit && slot && slot->mesh == mesh && churn_why(slot - mesh_cache, part, count) == 1 && p4_churn_top)
                    churn_top_note(header, material, count);
                prof_end(PROF_MSLOT);
                if (!slot) /* every entry of the window drawn this frame: arrays rewritten per instance (Heaven's flowers) */
                    cache_noslot++;
                if (hit) {
                    prof_begin(PROF_HIT);
                    slot->frame = cache_frame;
                    cache_hits++;
                    if (p4_cache_audit && audit_hash && !(audit_tick++ & 7)) {
                        unsigned now[3];
                        audit_inputs(now, position, uv, preLit, count);
                        unsigned *was = audit_hash[slot - mesh_cache];
                        audit_checks++;
                        int stale = 0;
                        for (int a = 0; a < 3; a++)
                            if (now[a] != was[a]) {
                                audit_stale[a]++;
                                stale = 1;
                            }
                        audit_stale_vertices += stale ? count : 0;
                    }
                    prof_begin(PROF_HEMPTY); /* the profiler's own cost per section, to subtract */
                    prof_end(PROF_HEMPTY);
                    int sprim = prim;
                    if (clip_this && guard_slot_whole(slot, slot->key, slot->built, sprim, slot->idx, slot->indices, slot->v, slot->vertices, slot->vtype, model, near)) {
                        draw_mesh_hit(slot, sprim, material, modulate, &light); /* b199: no chunk needs the CPU */
                    } else if (clip_this) {
                        void **texture = material->texture;
                        prof_begin(PROF_HBIND);
                        bind_mask(material->colour >> 24);
                        bind_raster(texture ? *texture : 0, GU_REPEAT);
                        prof_end(PROF_HBIND);
                        draw_guarded_compact(sprim, slot->idx, slot->indices, slot->v, slot->vertices, slot->vtype, model, near, material, modulate, &light, &slot->alpha_lo, slot, slot->key, slot->built);
                    } else {
                        draw_mesh_hit(slot, sprim, material, modulate, &light);
                    }
                    prof_end(PROF_HIT);
                    continue;
                }
            }
            if (!ge_lit && (!lit_for || (normals && (material->ambient != lit_for->ambient || material->diffuse != lit_for->diffuse)))) {
                prof_begin(PROF_SHADE);
                ShadeSetup k;
                if (normals)
                    shade_setup(&k, model, material, &light);
                for (int j = 0; j < count; j++) {
                    unsigned base = preLit ? double_colour(preLit[j]) : normals ? 0xff000000 : 0xffffffff;
                    mesh_lit[j] = !normals ? base : p4_shade_fast ? shade_fast(base, &normals[j], &k) : shade_colour(base, &normals[j], model, material, &light);
                }
                PROF_COUNT(prof_shaded, count);
                if (normals)
                    work_shaded += count; /* only real lighting: without normals it is a colour copy */
                prof_end(PROF_SHADE);
                lit_for = material;
                shade_generation++;
            }
            if (clip_this && !compact_guard) { /* the whole array in its own numbering, for draw_guarded; shared while colour and shade hold */
                prof_begin(PROF_GDRAW);
                unsigned colour = modulate ? material->colour : 0xffffffff;
                int stride = ge_lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
                /* GE-lit: the array depends only on the geometry's inputs (geometry_key), not on the camera or the
                   material; keep it between frames like a compact mesh (b86 Heaven: 17 rebuilt a frame, sqrt each vertex) */
                MeshCache *full_slot = 0;
                if (!full && ge_lit && geometry_key && p4_guard_full_cache) {
                    int hit;
                    MeshCache *e = cache_slot(header, geometry_key, &hit);
                    if (hit && e->v && e->vertices == count) {
                        e->frame = cache_frame;
                        full = e->v;
                        guard_full_hits++;
                    } else if (e && !(e->mesh == header && e->built + 1 >= cache_frame)) { /* rebuilt last frame too: inputs move */
                        full_slot = e;
                    }
                }
                if (!full || (!ge_lit && (full_generation != shade_generation || full_colour != colour))) { /* GE-lit: one copy for every material */
                    int units = (count * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
                    if (vertex_used + units > MAX_VERTICES) {
                        p4_log("TRACE vertex pool full\n");
                        prof_end(PROF_GDRAW);
                        return;
                    }
                    full = vertex_pool + vertex_used;
                    vertex_used += units;
                    for (int j = 0; j < count; j++) {
                        if (ge_lit) {
                            put_lit_vertex((Vertex3DN *)full + j, position, uv, du, dv, preLit, normals, j);
                            continue;
                        }
                        full[j].u = uv ? uv[j][0] - du : 0;
                        full[j].v = uv ? uv[j][1] - dv : 0;
                        full[j].colour = modulate_colour(mesh_lit[j], colour);
                        full[j].x = position[j].x;
                        full[j].y = position[j].y;
                        full[j].z = position[j].z;
                    }
                    sceKernelDcacheWritebackRange(full, count * stride);
                    full_generation = shade_generation;
                    full_colour = colour;
                    guard_full_builds++;
                    if (full_slot) {
                        int need = count * stride;
                        if (full_slot->v && (full_slot->mesh != header || full_slot->capacity < need)) {
                            cache_bytes -= full_slot->capacity;
                            free(full_slot->v);
                            full_slot->v = 0;
                            full_slot->capacity = 0;
                        }
                        if (!full_slot->v && cache_bytes + need <= p4_cache_budget && (full_slot->v = memalign(16, need))) {
                            full_slot->capacity = need;
                            cache_bytes += need;
                        }
                        if (full_slot->v) {
                            memcpy(full_slot->v, full, need);
                            sceKernelDcacheWritebackRange(full_slot->v, need);
                            full_slot->mesh = header;
                            full_slot->key = geometry_key;
                            full_slot->vertices = count;
                            full_slot->indices = 0;
                            full_slot->idx = 0;
                            full_slot->churn = 0;
                        } else {
                            full_slot->mesh = header; /* remembered as built, so a moving array is not copied every frame */
                            full_slot->key = 0;
                        }
                        full_slot->frame = full_slot->built = cache_frame;
                        full_slot->alpha_lo = -1;
                    }
                }
                guard_mesh_draws++;
                void **texture = material->texture;
                bind_mask(material->colour >> 24);
                bind_raster(texture ? *texture : 0, GU_REPEAT);
                draw_material_ge = 0; /* this array has the material in its vertices */
                draw_guarded(prim, mesh->indices, mesh->numIndices, full, ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D, near, material, modulate, &light);
                draw_material_ge = ge_colour;
                prof_end(PROF_GDRAW);
                continue;
            }
            int n = mesh->numIndices;
            int stride = ge_lit ? sizeof(Vertex3DN) : sizeof(Vertex3D), vtype = ge_lit ? VT_3D | GU_NORMAL_8BIT : VT_3D;
            if (vertex_used + (n * stride + 23) / 24 > MAX_VERTICES || index_used + n > MAX_INDICES) {
                p4_log("TRACE vertex pool full\n");
                return;
            }
            unsigned colour = modulate && !ge_colour ? material->colour : 0xffffffff;
            char *out = (char *)(vertex_pool + vertex_used);
            unsigned short *remapped = index_pool + index_used;
            unsigned stamp = ++mesh_generation;
            int used = 0;
            const unsigned short *in = mesh->indices;
            prof_begin(PROF_EMIT);
            P4Topology *topology = p4_topology_cache ? p4_topology_get(&topology_cache, mesh, in, n, count,
                    cache_frame, p4_topology_budget, mesh_remap) : 0;
            const unsigned short *source = topology ? topology->data + n * 2 : 0;
            int iterations = topology ? topology->used : n;
            if (topology)
                memcpy(remapped, topology->data + n, n * 2);
            for (int k = 0; k < iterations; k++) {
                unsigned j = topology ? source[k] : in[k];
                if (topology || mesh_stamp[j] != stamp) {
                    if (!topology) {
                        mesh_stamp[j] = stamp;
                        mesh_remap[j] = used;
                    }
                    if (ge_lit) {
                        put_lit_vertex((Vertex3DN *)out + used, position, uv, du, dv, preLit, normals, j);
                    } else {
                        Vertex3D *v = (Vertex3D *)out + used;
                        v->u = uv ? uv[j][0] - du : 0;
                        v->v = uv ? uv[j][1] - dv : 0;
                        v->colour = modulate_colour(mesh_lit[j], colour);
                        v->x = position[j].x;
                        v->y = position[j].y;
                        v->z = position[j].z;
                    }
                    used++;
                }
                if (!topology)
                    remapped[k] = mesh_remap[j];
            }
            vertex_used += (used * stride + 23) / 24;
            index_used += n;
            PROF_COUNT(prof_emitted, used);
            if (ge_lit)
                work_ge_emitted += used;
            else
                work_cpu_emitted += used;
            prof_end(PROF_EMIT);
            if (slot && slot->mesh == mesh && slot->built + 1 >= cache_frame && ++slot->churn > 2) {
                slot->built = cache_frame; /* inputs change every frame: the copy would never be used */
                churn_remember(slot - mesh_cache, part);
                slot->key = key;
                slot->frame = cache_frame;
                cache_misses++;
                slot = 0;
            } else if (slot && slot->mesh == mesh && slot->built + 1 < cache_frame) {
                slot->churn = 0;
            }
            prof_begin(PROF_MCOPY);
            if (slot) {
                int need = used * stride + n * 2;
                if (slot->mesh != mesh)
                    slot->churn = 0;
                if (slot->mesh != mesh || slot->capacity < need) {
                    if (slot->v) {
                        cache_bytes -= slot->capacity;
                        free(slot->v);
                        slot->v = 0;
                        slot->capacity = 0;
                    }
                    if (cache_bytes + need <= p4_cache_budget && (slot->v = memalign(16, need))) {
                        slot->capacity = need;
                        cache_bytes += need;
                    }
                }
                if (slot->v) {
                    memcpy(slot->v, out, used * stride);
                    slot->idx = (unsigned short *)((char *)slot->v + used * stride);
                    slot->vtype = vtype;
                    slot->alpha_lo = -1; /* draw_tested scans it again */
                    memcpy(slot->idx, remapped, n * 2);
                    sceKernelDcacheWritebackRange(slot->v, need);
                }
                if (slot->v) {
                    slot->mesh = mesh;
                    slot->key = key;
                    slot->frame = cache_frame;
                    slot->built = cache_frame;
                    churn_remember(slot - mesh_cache, part);
                    if (p4_cache_audit && (audit_hash || (audit_hash = calloc(CACHE_SLOTS, sizeof *audit_hash))))
                        audit_inputs(audit_hash[slot - mesh_cache], position, uv, preLit, count);
                    slot->vertices = used;
                    slot->indices = n;
                    cache_builds++;
                } else {
                    slot->mesh = 0;
                    cache_misses++;
                }
            }
            prof_end(PROF_MCOPY);
            prof_begin(PROF_SUBMIT);
            sceKernelDcacheWritebackRange(out, used * stride);
            sceKernelDcacheWritebackRange(remapped, n * 2);
            void **texture = material->texture;
            bind_mask(material->colour >> 24);
            bind_raster(texture ? *texture : 0, GU_REPEAT);
            if (clip_this)
                draw_guarded_compact(prim, remapped, n, out, used, vtype, model, near, material, modulate, &light, 0, 0, 0, 0);
            else
                draw_mesh_vertices(prim, vtype, n, remapped, out, material, modulate, &light);
            prof_end(PROF_SUBMIT);
        }
        return;
    }
    for (int i = 0; i < header->numMeshes; i++, mesh++) {
        if (probe_depth_reject(mesh, position, count, model))
            continue;
        if (frustum_reject(mesh, count, frustum_ready))
            continue;
        unsigned colour = modulate ? mesh->material->colour : 0xffffffff;
        if (!vertices || colour != previous_colour || (normals && mesh->material != previous_material)) {
            if (vertex_used + count > MAX_VERTICES) {
                p4_log("TRACE vertex pool full\n");
                return;
            }
            vertices = vertex_pool + vertex_used;
            vertex_used += count;
            for (int j = 0; j < count; j++) {
                vertices[j].u = uv ? uv[j][0] - du : 0;
                vertices[j].v = uv ? uv[j][1] - dv : 0;
                /* lit without prelight: RenderWare starts from black and adds the lights */
                unsigned base = preLit ? double_colour(preLit[j]) : normals ? 0xff000000 : 0xffffffff;
                if (normals) base = shade_colour(base, &normals[j], model, mesh->material, &light);
                vertices[j].colour = modulate_colour(base, colour);
                vertices[j].x = position[j].x;
                vertices[j].y = position[j].y;
                vertices[j].z = position[j].z;
            }
            sceKernelDcacheWritebackRange(vertices, count * sizeof *vertices);
            previous_colour = colour;
            previous_material = mesh->material;
        }
        if (census3d_skip(mesh->material, VT_3D, mesh->numIndices, modulate))
            continue;
        void **texture = mesh->material->texture;
        bind_mask(mesh->material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        if (guarded) {
            draw_guarded(prim, mesh->indices, mesh->numIndices, vertices, VT_3D, near, mesh->material, modulate, &light);
            continue;
        }
        sceKernelDcacheWritebackRange(mesh->indices, mesh->numIndices * 2);
        draw_material_alpha = colour >> 24;
        draw_tested(prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D, mesh->numIndices, mesh->indices, vertices);
    }
}

const Matrix *p4r_003e9700(void *frame);

