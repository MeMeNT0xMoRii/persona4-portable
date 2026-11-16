/* b258 p4_static_list: a static atomic drawn as one CALL to a list prepared earlier, as native PSP games do (Crisis
   Core: data in GE form at load, per object only matrices and a draw). Included by rwsky.c before atomic_render_body.

   b257 Heaven, profiled: the static mesh path took 5.8 M cycles a frame for 117 calls (~50 k each), and 146
   instruction-cache misses a call: the cost is walking the general path (state, keys, slots, lights, ring lookups),
   not the GE. Here a hit is: a small entry, a key of the inputs the list depends on, the guard-band sphere test, the
   model matrix and BASE + CALL.

   Recording: the first time (and after any input changes) the atomic is drawn through draw_meshes_body as usual, but
   from a forgotten state shadow (so every state the draw needs is in its words) and with the per-mesh CALL replays off
   (so the words are inline). Kept when the words hold no flow command, every vertex and index address lies in a
   mesh cache slot the draw used, and nothing went to the late pass: the words (minus the model matrix) and copies of
   those slots become the entry's own list, addresses moved to the copies, ending in RET. The cache can evict its
   slots afterwards; the list does not read them.

   The key: geometry and its arrays (and RenderWare's lock flag, as the PS2's instancing used it: a locked geometry
   goes the slow way, which clears the flag, and is recorded again), camera, render and sky state, pixel mask,
   materials (pointer, colour, light terms, raster, its pixels and palette), texture generation, the GE light when lit,
   and the switches the draw path reads. Camera matrices and the model matrix are sent outside the list.
   Check (p4_static_list_check 1): every 64th frame a hit is recorded again and its words compared with the list's
   (vertex and index addresses aside); differences are logged and the new recording kept. */
#include "ge_prepare.h"
int p4_static_list = 1, p4_static_list_check = 0, p4_static_list_budget = 4 << 20;
int p4_static_mesh_lists = 1; /* b264: independent lists/bounds per mesh; real-PSP battle A/B saves ~1 ms */
int p4_ge_static_clip = 0, p4_ge_clip_growth = 256; /* b267: enable after real-PSP image/performance checks */
static unsigned sl_ge_hits, sl_ge_fallbacks, sl_ge_builds, sl_ge_refused;
enum { SL_SLOTS = 2048, /* b277: 512 (256 pairs) thrashed in Heaven: ~21 atomics a frame evicted each other, "new" forever */ SL_MESHES = 16, SL_GRAVE = 128, SL_PART = 32, SL_PARTS = 256 };
typedef struct {
    const void *atomic;
    const Geometry *g;
    const void *verts, *uv, *prelit, *normals, *camera;
    unsigned state, materials, light, texgen, bits, stamp, retry, built, churn;
    int count, modulate, words, draws, bytes;
    unsigned *list;   /* own words + RET */
    void *data;       /* own copies of the mesh cache slots the words point into */
    float (*parts)[4]; /* b259: spheres of up to SL_PART triangles (model space), for an atomic whose own sphere crosses the guard band */
    int nparts;
    int split;
    struct SlMeshSet *meshes;
    unsigned *gview;  /* b288: recorded from guard results: the view (SL_VIEW words) the cut triangles are for */
    unsigned gchurn;  /* b289: guard lists dropped soon after recording, in a row */
} StaticList;
typedef struct {
    Geometry geometry;
    MorphTarget target;
    struct { MeshHeader header; Mesh mesh; } draw;
    Mesh source;
    void *prepared;
    unsigned prepared_bytes;
    float bounds[6], max_edge;
    StaticList entry;
} SlMeshPart;
typedef struct SlMeshSet {
    const Geometry *source;
    const MeshHeader *header;
    const void *verts, *uv, *prelit, *normals;
    unsigned flags, mesh_flags;
    int count, nverts, ge_mode;
    unsigned short serial;
    SlMeshPart part[];
} SlMeshSet;
static StaticList *sl_table;
static struct SlGrave { void *p; unsigned frame; } *sl_grave; /* heap: .bss is full */
static unsigned sl_bytes;
static int sl_nslots;
static MeshCache *sl_slot[SL_MESHES];
static unsigned sl_size_why[4], sl_size_logged;
static unsigned sl_evictions; /* b277: an entry drawn within 8 frames dropped for another atomic */
static unsigned sl_hits, sl_records, sl_refused[8], sl_checks, sl_check_diffs, sl_misses[8];
static unsigned sl_mesh_hits, sl_mesh_fallback, sl_mesh_builds;

static void sl_note(MeshCache *slot)
{
    if (sl_nslots < SL_MESHES)
        sl_slot[sl_nslots] = slot;
    sl_nslots++;
}
/* b288 p4_static_guard_lists: a static part near the camera is cut on the CPU against the guard band
   (draw_guarded_compact) every frame; with a still camera the cut is a cached guard result (b236), but its words
   went to the GE through the whole mesh path, and the list recorder refused it (no mesh cache slot: b286 Heaven, 21
   parts a frame). Now the recorder also copies the guard result (kept indices, cut vertices) and the list keeps the
   result's view key (model, camera, guard options): drawn only while that view is exactly the same, dropped when it
   changes and recorded again once the camera is still. Same triangles, same words as the guarded draw.
   0: as b287 (such parts are refused, drawn the long way). */
int p4_static_guard_lists = 1;
int p4_sl_trust = 0; /* b297 experiment, see sl_draw */
static unsigned sl_trusted;
enum { SL_VIEW = 38, SL_EXTRA = 16 }; /* GuardResultKey model[12] camera[18] options[8], contiguous */
static const void *sl_xp[SL_EXTRA];
static unsigned sl_xb[SL_EXTRA];
static int sl_nextra, sl_gbad, sl_gview_set;
static unsigned sl_gview_rec[SL_VIEW];
static unsigned sl_guard_lists, sl_guard_moved, sl_guard_refused, sl_guard_hits, sl_guard_churned, sl_guard_why[3];
/* b290: the camera key holds the view rows with their fourth words (RwMatrix flags and pads); the projection does not
   read them, and the flags may change while the matrix stays (b289 Heaven: every list dropped for "camera" while the
   guard results, keyed with those words too, hit from two entries). Zero them in a list's view. */
static void sl_view_mask(unsigned *view)
{
    view[12 + 4] = view[12 + 8] = view[12 + 12] = 0;
}
static void sl_guard_note(const void *slot, const void *data, unsigned bytes, const GuardResultKey *k)
{
    if (!p4_static_guard_lists || !sl_recording)
        return;
    sl_note((MeshCache *)slot);
    if (sl_nextra < SL_EXTRA)
        sl_xp[sl_nextra] = data, sl_xb[sl_nextra] = bytes;
    sl_nextra++;
    unsigned view[SL_VIEW];
    memcpy(view, k->model, sizeof view);
    sl_view_mask(view);
    if (!sl_gview_set)
        memcpy(sl_gview_rec, view, sizeof sl_gview_rec), sl_gview_set = 1;
    else if (memcmp(sl_gview_rec, view, sizeof sl_gview_rec))
        sl_gbad = 1;
}
static void sl_guard_unowned(void)
{
    if (p4_static_guard_lists && sl_recording)
        sl_gbad = 1;
}
static void sl_view_now(unsigned out[SL_VIEW], const Matrix *model)
{
    GuardResultKey k;
    memset(&k, 0, sizeof k);
    guard_key_view(&k, model);
    memcpy(out, k.model, SL_VIEW * 4);
    sl_view_mask(out);
}
/* b291: the same view for a guard list. b290 Heaven, still camera: the camera position alternates by 2 ULP from frame
   to frame (pos.x 0x44145a79 / 0x44145a7b, ~593.4), so an exact key dropped every list on the next frame (the guard
   results kept one entry per value). The cut lies on the guard planes, 8 x the view (1920 px across, 1088 px down
   from the centre, inside the GE's 2048 px each side) and at 1.01 x near: a view within a few ULP moves those cut
   edges by far less than a pixel and keeps them inside the window, and every on-screen pixel is the same. Exact: model, camera pointer, near,
   far, options. Rotation rows within 1e-6 (~8 ULP), position within min(1e-4 x (|p| + 1), 1e-3 x near). */
static int sl_view_near(const unsigned *a, const unsigned *b)
{
    for (int i = 0; i < SL_VIEW; i++) {
        if (a[i] == b[i])
            continue;
        int rotation = (i >= 13 && i <= 15) || (i >= 17 && i <= 19) || (i >= 21 && i <= 23), position = i >= 25 && i <= 27;
        if (!rotation && !position)
            return 0;
        float x, y, near;
        memcpy(&x, a + i, 4), memcpy(&y, b + i, 4), memcpy(&near, b + 28, 4);
        float tol = rotation ? 1e-6f : fminf(1e-4f * (fabsf(y) + 1.0f), 1e-3f * fabsf(near));
        if (!(fabsf(x - y) <= tol))
            return 0;
    }
    return 1;
}
_Static_assert(offsetof(GuardResultKey, options) + sizeof(((GuardResultKey *)0)->options) - offsetof(GuardResultKey, model) == SL_VIEW * 4,
               "the view words of GuardResultKey are contiguous");
/* a list the GE may still be reading (up to 2 frames late): freed after 4 frames */
static void sl_bury(void *p)
{
    if (!p)
        return;
    if (!sl_grave && !(sl_grave = calloc(SL_GRAVE, sizeof *sl_grave))) {
        pipe_texture_wait();
        free(p);
        return;
    }
    for (unsigned i = 0; i < SL_GRAVE; i++)
        if (sl_grave[i].p && cache_frame - sl_grave[i].frame > 4)
            free(sl_grave[i].p), sl_grave[i].p = 0;
    for (unsigned i = 0; i < SL_GRAVE; i++)
        if (!sl_grave[i].p) {
            sl_grave[i].p = p, sl_grave[i].frame = cache_frame;
            return;
        }
    pipe_texture_wait(); /* full: wait for the GE, then everything buried can go */
    for (unsigned i = 0; i < SL_GRAVE; i++)
        free(sl_grave[i].p), sl_grave[i].p = 0;
    free(p);
}
static void sl_drop(StaticList *e)
{
    if (e->meshes) {
        for (int i = 0; i < e->meshes->count; i++) {
            sl_drop(&e->meshes->part[i].entry);
            if (e->meshes->part[i].prepared) {
                sl_bytes -= e->meshes->part[i].prepared_bytes;
                sl_bury(e->meshes->part[i].prepared);
            }
        }
        sl_bytes -= sizeof(SlMeshSet) + e->meshes->count * sizeof(SlMeshPart);
        free(e->meshes);
        e->meshes = 0;
    }
    free(e->parts), e->parts = 0, e->nparts = 0; /* CPU only */
    free(e->gview), e->gview = 0;
    if (e->list) {
        sl_bytes -= e->bytes;
        sl_bury(e->list), sl_bury(e->data);
    }
    e->list = 0, e->data = 0, e->words = 0, e->bytes = 0;
}
static unsigned sl_state_key_full(void)
{
    unsigned h = hash_words(2166136261u, render_state, sizeof render_state);
    h = hash_words(h, sky_state, sizeof sky_state);
    return hash_words(h, &pixel_mask, 4);
}
/* The hash remains exactly the old hash; repeated setters of the same value do
   not invalidate it. Pixel-mask writes are tracked by value at this boundary. */
int p4_static_state_cache = 0, p4_static_state_check = 0; /* b285 Heaven A/B: no measurable gain */
static unsigned sl_state_cached, sl_state_mask, sl_state_reuses, sl_state_rebuilds, sl_state_diffs;
static unsigned sl_state_key(void)
{
    if (!p4_static_state_cache) {
        sl_state_dirty = 1; /* turning the cache back on must recompute */
        return sl_state_key_full();
    }
    if (sl_state_dirty || sl_state_mask != pixel_mask) {
        sl_state_cached = sl_state_key_full();
        sl_state_mask = pixel_mask;
        sl_state_dirty = 0;
        sl_state_rebuilds++;
    } else {
        sl_state_reuses++;
        if (p4_static_state_check) {
            unsigned reference = sl_state_key_full();
            if (reference != sl_state_cached) {
                sl_state_diffs++;
                sl_state_cached = reference;
            }
        }
    }
    return sl_state_cached;
}
static unsigned sl_material_key(const Geometry *g)
{
    const Mesh *mesh = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    unsigned h = 2166136261u;
    for (int m = 0; m < g->mesh->numMeshes; m++) {
        const Material *mat = mesh[m].material;
        Raster *r = mat->texture ? *(Raster **)mat->texture : 0;
        unsigned w[8] = { (unsigned)mat, mat->colour, 0, 0, (unsigned)r, r ? (unsigned)r->originalPixels : 0, r ? (unsigned)r->palette : 0,
                          (unsigned)mesh[m].indices };
        memcpy(&w[2], &mat->ambient, 4), memcpy(&w[3], &mat->diffuse, 4);
        h = hash_words(h, w, sizeof w);
    }
    return h;
}
static unsigned sl_fog_key(void);
static unsigned sl_bits(void)
{
    return state_bits() ^ (unsigned)p4_light_prelit << 20 ^ (unsigned)p4_light_unprelit << 21 ^ sl_fog_key();
}
static StaticList *sl_find(const void *atomic)
{
    if (!sl_table && !(sl_table = calloc(SL_SLOTS, sizeof *sl_table)))
        return 0;
    unsigned h = ((unsigned)atomic >> 4) * 2654435761u;
    h ^= h >> 15;
    StaticList *a = &sl_table[(h & (SL_SLOTS / 2 - 1)) * 2], *b = a + 1;
    if (a->atomic == atomic)
        return a;
    if (b->atomic == atomic)
        return b;
    StaticList *e = !a->atomic ? a : !b->atomic ? b : (int)(a->stamp - b->stamp) <= 0 ? a : b;
    if (e->atomic && e->stamp == cache_frame)
        return 0; /* both used this frame: leave them */
    if (e->atomic && cache_frame - e->stamp < 8)
        sl_evictions++;
    sl_drop(e);
    memset(e, 0, sizeof *e);
    e->atomic = atomic;
    return e;
}
/* the words of a GE address: BASE (bits 24-27) and the 24-bit low part */
static inline unsigned sl_address(unsigned base, unsigned low) { return (base & 0xf0000) << 8 | (low & 0xffffff); }

/* words from..to (the draw just made) into e; 0 refused (reason in sl_refused) */
static int sl_keep(StaticList *e, const unsigned *from, const unsigned *to, int check, const Geometry *geometry)
{
    const unsigned *m0 = mesh_model_begin, *m1 = mesh_model_mark;
    if (!m0 || !m1 || m0 < from || m1 < m0 || m1 > to) {
        sl_refused[0]++;
        return 0;
    }
    int prefix = sl_ge_uncut ? 6 : 0;
    int n = (m0 - from) + (to - m1) + prefix, draws = 0;
    if (n <= 0 || n > 4096 || sl_nslots <= 0 || sl_nslots > SL_MESHES || sl_nextra > SL_EXTRA) {
        e->split = 1;
        sl_refused[1]++;
        sl_size_why[n <= 0 ? 0 : n > 4096 ? 1 : sl_nslots <= 0 ? 2 : 3]++;
        if (sl_size_logged++ < 32)
            p4_log("static record refused g %p verts %d meshes %u indices %u words %d slots %d uncut %d\n",
                   geometry, geometry->numVertices, geometry->mesh->numMeshes, geometry->mesh->totalIndices, n, sl_nslots, sl_ge_uncut);
        return 0;
    }
    /* the copies of the slots (and b288 of the guard results): one block */
    enum { SL_REGIONS = SL_MESHES + SL_EXTRA };
    const char *rp[SL_REGIONS];
    unsigned rb[SL_REGIONS];
    int nreg = 0, bytes = 0, offset[SL_REGIONS];
    for (int i = 0; i < sl_nslots; i++) {
        if (!sl_slot[i]->v || sl_slot[i]->capacity <= 0) {
            sl_refused[2]++;
            return 0;
        }
        rp[nreg] = sl_slot[i]->v, rb[nreg++] = sl_slot[i]->capacity;
    }
    for (int i = 0; i < sl_nextra; i++) {
        if (!sl_xp[i] || !sl_xb[i]) {
            sl_refused[2]++;
            return 0;
        }
        rp[nreg] = sl_xp[i], rb[nreg++] = sl_xb[i];
    }
    for (int i = 0; i < nreg; i++) {
        offset[i] = bytes;
        bytes += (rb[i] + 15) & ~15;
    }
    int box_offset = bytes;
    if (prefix) bytes += 8 * 3 * sizeof(float);
    if (!check && sl_bytes + bytes + (n + 1) * 4 > (unsigned)p4_static_list_budget) {
        sl_refused[3]++;
        return 0;
    }
    unsigned *list = memalign(16, (n + 1) * 4);
    char *data = memalign(16, bytes);
    if (!list || !data) {
        free(list), free(data);
        sl_refused[3]++;
        return 0;
    }
    int k = prefix, base_at = -1;
    for (const unsigned *w = from; w < to; w++) {
        if (w == m0)
            w = m1;
        if (w >= to)
            break;
        unsigned cmd = *w >> 24;
        if (cmd == 0x08 || cmd == 0x0a || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) { /* JUMP, CALL, RET, END, FINISH */
            free(list), free(data);
            sl_refused[4]++;
            return 0;
        }
        list[k] = *w;
        if (cmd == 0x10)
            base_at = k;
        if (cmd == 0x01 || cmd == 0x02) { /* VADDR, IADDR: must be in a slot the draw used */
            if (base_at < 0) {
                free(list), free(data);
                sl_refused[5]++;
                return 0;
            }
            unsigned at = sl_address(list[base_at], *w), moved = 0;
            int found = 0;
            for (int i = 0; i < nreg && !found; i++) {
                unsigned lo = (unsigned)rp[i] & 0x0fffffff;
                if (at >= lo && at < lo + rb[i])
                    found = 1, moved = (((unsigned)data + offset[i]) & 0x0fffffff) + (at - lo);
            }
            if (!found) {
                free(list), free(data);
                sl_refused[5]++;
                return 0;
            }
            list[base_at] = 0x10u << 24 | (moved >> 8 & 0xf0000);
            list[k] = cmd << 24 | (moved & 0xffffff);
        }
        draws += cmd == 0x04;
        k++;
    }
    list[k] = 0x0Bu << 24; /* RET */
    if (prefix) {
        float lo[3] = {1e30f,1e30f,1e30f}, hi[3] = {-1e30f,-1e30f,-1e30f};
        const V3 *v = geometry->morphTarget->verts;
        for (int i = 0; i < geometry->numVertices; i++) {
            float xyz[3] = {v[i].x,v[i].y,v[i].z};
            for (int j=0;j<3;j++) { if(xyz[j]<lo[j])lo[j]=xyz[j]; if(xyz[j]>hi[j])hi[j]=xyz[j]; }
        }
        float (*box)[3] = (float (*)[3])(data + box_offset);
        for (int i=0;i<8;i++) for (int j=0;j<3;j++) box[i][j] = i>>j&1 ? hi[j] : lo[j];
        unsigned at = (unsigned)box, end = (unsigned)(list+k);
        list[0] = 0x12u<<24 | GU_VERTEX_32BITF;
        list[1] = 0x10u<<24 | (at>>8 & 0xf0000);
        list[2] = 0x01u<<24 | (at & 0xffffff);
        list[3] = 0x07u<<24 | 8; /* BOUNDINGBOX */
        list[4] = 0x10u<<24 | (end>>8 & 0xf0000);
        list[5] = 0x09u<<24 | (end & 0xffffff); /* BJUMP to RET when outside */
    }
    if (check) { /* the old list against this recording, addresses aside */
        int same = e->list && e->words == k + 1;
        for (int i = 0; same && i < k; i++) {
            unsigned c = list[i] >> 24;
            if (c != 0x10 && c != 0x01 && c != 0x02 && c != 0x09 && list[i] != e->list[i])
                same = 0;
        }
        sl_checks++;
        if (!same && sl_check_diffs++ < 8)
            p4_log("TRACE static list differs: atomic %p geometry %p words %d recorded %d\n", e->atomic, (void *)e->g, k + 1, e->words);
    }
    for (int i = 0; i < nreg; i++)
        memcpy(data + offset[i], rp[i], rb[i]);
    sceKernelDcacheWritebackRange(data, bytes);
    sceKernelDcacheWritebackRange(list, (k + 1) * 4);
    sl_drop(e);
    e->list = list, e->data = data, e->words = k + 1, e->draws = draws, e->bytes = bytes + (k + 1) * 4;
    e->split = 0;
    sl_bytes += e->bytes;
    sl_records++;
    return 1;
}

/* b259: the atomic's triangles in runs of SL_PART, each run's bounding sphere (model space). b258 Heaven: 19 atomics a
   frame (the floor, the stairs) have a sphere past the guard band while every chunk the body tests is inside it. */
static void sl_parts(StaticList *e, const Geometry *g)
{
    const MeshHeader *header = g->mesh;
    const Mesh *mesh = (const Mesh *)((const char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, n = 0;
    const V3 *v = g->morphTarget->verts;
    float (*parts)[4] = malloc(SL_PARTS * sizeof *parts);
    if (!parts)
        return;
    for (int m = 0; m < header->numMeshes; m++) {
        const unsigned short *ix = mesh[m].indices;
        int ni = mesh[m].numIndices, tris = strip ? ni - 2 : ni / 3;
        for (int t0 = 0; t0 < tris; t0 += SL_PART) {
            if (n >= SL_PARTS) {
                free(parts);
                return;
            }
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            for (int t = t0; t < tris && t < t0 + SL_PART; t++)
                for (int c = 0; c < 3; c++) {
                    int j = strip ? ix[t + c] : ix[t * 3 + c];
                    const float *p = &v[j].x;
                    for (int k = 0; k < 3; k++) {
                        if (p[k] < lo[k]) lo[k] = p[k];
                        if (p[k] > hi[k]) hi[k] = p[k];
                    }
                }
            float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
            parts[n][0] = (lo[0] + hi[0]) * 0.5f, parts[n][1] = (lo[1] + hi[1]) * 0.5f, parts[n][2] = (lo[2] + hi[2]) * 0.5f;
            parts[n][3] = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz) * 1.001f + 1e-3f;
            if (!(parts[n][3] < 1e30f)) {
                free(parts);
                return;
            }
            n++;
        }
    }
    e->parts = parts, e->nparts = n;
}
static int sl_inside(const StaticList *e, const Matrix *model, const float *sphere)
{
    if (sl_ge_uncut) return 1;
    if (replay_unguarded(model, sphere))
        return 1;
    if (!e->parts || !p4_guard_clip || !guard_code)
        return 0;
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    for (int i = 0; i < e->nparts; i++)
        if (!guard_sphere_world(model, e->parts[i], near))
            return 0;
    return 1;
}

/* forget the non-camera part of the state shadow: what follows sends every state again. b323: the fog parameters
   too (a list being recorded must carry FOG1/FOG2/colour, not rely on what was sent before it), and a list called
   may leave GU_FOG on, so leaving the 3D pass turns it off. */
static void sl_forget(void)
{
    memset(&gs, 0, CALL_CAM0);
    memset((char *)&gs + CALL_CAM1, 0, sizeof gs - CALL_CAM1);
    fog_sent.known = 0;
    if (p4_fog)
        gu_fog_active = 1;
}
/* b323: what a recorded list's fog depends on beyond the render and Sky state of the key: the camera's fog and far
   planes (bind_fog). 0 with fog off. */
static unsigned sl_fog_key(void)
{
    if (!p4_fog || !current_camera)
        return 0;
    return hash_words(2166136261u, (const char *)current_camera + CAMERA_FAR, 2 * 4) | 1;
}

/* b264 mesh preparation. Each header retains the source primitive and index order; only its sphere is tighter.
   There is no geometry reduction or clipping bypass. Keep the wrappers alive while their cache keys are in use. */
static int sl_mesh_same(const SlMeshSet *s, const Geometry *g, const V3 *normals)
{
    if (s->ge_mode != p4_ge_static_clip || g->locked || s->source != g || s->header != g->mesh || s->count != g->mesh->numMeshes || s->serial != g->mesh->serial ||
        s->mesh_flags != g->mesh->flags || s->flags != g->flags || s->nverts != g->numVertices || s->verts != g->morphTarget->verts ||
        s->uv != (g->numTexCoordSets ? g->texCoords[0] : 0) || s->prelit != g->preLit || s->normals != normals)
        return 0;
    const Mesh *m = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    for (int i = 0; i < s->count; i++)
        if (s->part[i].source.indices != m[i].indices || s->part[i].source.numIndices != m[i].numIndices ||
            s->part[i].source.material != m[i].material || translucent_material(m[i].material, g->flags & 0x40))
            return 0;
    return 1;
}
static float sl_ge_limit(const Matrix *model, const float bounds[6])
{
    if (!p4_guard_gnear || !current_camera) return 0;
    Mat4 m;
    guard_world_planes();
    if (model) { mat4_from(m, model); mat4_mul(m, m, guard_world.vp); }
    else memcpy(m, guard_world.vp, sizeof m);
    return p4_ge_edge_limit((const float (*)[4])m, bounds, *(float *)((char *)current_camera + CAMERA_NEAR), GUARD);
}
static void sl_ge_prepare_part(SlMeshPart *p, const Matrix *model, const V3 *normals)
{
    Geometry *g = &p->geometry;
    float limit = sl_ge_limit(model, p->bounds) * 0.75f;
    P4GePrepared prep;
    if (!p4_ge_prepare(&prep, (const float *)p->target.verts, g->numTexCoordSets ? g->texCoords[0] : 0,
                       normals ? (const float *)normals : 0, g->preLit, g->numVertices,
                       p->source.indices, p->source.numIndices, (p->draw.header.flags & MESH_PRIM_MASK)==MESH_TRISTRIP,
                       limit, p4_ge_clip_growth)) { sl_ge_refused++; return; }
    unsigned bytes = prep.nv * (sizeof(V3) + (normals ? sizeof(V3) : 0) + (g->numTexCoordSets ? 8 : 0) + (g->preLit ? 4 : 0)) + prep.ni * 2;
    char *data = sl_bytes + bytes <= (unsigned)p4_static_list_budget ? malloc(bytes) : 0;
    if (!data) { p4_ge_prepared_free(&prep); sl_ge_refused++; return; }
    p->prepared = data; p->prepared_bytes = bytes; p->max_edge = prep.edge; sl_bytes += bytes;
    p->target.verts = (V3 *)data; data += prep.nv * sizeof(V3);
    if (normals) { p->target.normals = (V3 *)data; data += prep.nv * sizeof(V3); }
    if (g->numTexCoordSets) { g->texCoords[0] = (float (*)[2])data; data += prep.nv * 8; }
    if (g->preLit) { g->preLit = (unsigned *)data; data += prep.nv * 4; }
    p->draw.mesh.indices = (unsigned short *)data;
    memcpy(data, prep.idx, prep.ni * 2);
    for (int i=0;i<prep.nv;i++) {
        memcpy(&p->target.verts[i], prep.v[i].p, sizeof(V3));
        if (normals) memcpy(&p->target.normals[i], prep.v[i].normal, sizeof(V3));
        if (g->numTexCoordSets) memcpy(g->texCoords[0][i], prep.v[i].uv, 8);
        if (g->preLit) g->preLit[i] = prep.v[i].colour;
    }
    g->numVertices = prep.nv; g->numTriangles = prep.ni/3;
    p->draw.mesh.numIndices = p->draw.header.totalIndices = prep.ni;
    p->draw.header.flags &= ~MESH_PRIM_MASK;
    p4_ge_prepared_free(&prep); sl_ge_builds++;
}
static SlMeshSet *sl_mesh_build(const Geometry *g, const V3 *normals, const Matrix *model)
{
    int n = g->mesh->numMeshes;
    if (g->locked || n < 1 || n > 128 || g->numVertices <= 0 || !g->morphTarget->verts)
        return 0;
    unsigned bytes = sizeof(SlMeshSet) + n * sizeof(SlMeshPart);
    if (sl_bytes + bytes > (unsigned)p4_static_list_budget)
        return 0;
    SlMeshSet *s = calloc(1, bytes);
    if (!s)
        return 0;
    s->source = g, s->header = g->mesh, s->count = n, s->nverts = g->numVertices;
    s->ge_mode = p4_ge_static_clip;
    s->serial = g->mesh->serial, s->mesh_flags = g->mesh->flags, s->flags = g->flags;
    s->verts = g->morphTarget->verts, s->uv = g->numTexCoordSets ? g->texCoords[0] : 0, s->prelit = g->preLit, s->normals = normals;
    const Mesh *m = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
    for (int i = 0; i < n; i++) {
        SlMeshPart *p = &s->part[i];
        /* Late passes and moving arrays stay together on the original path. */
        if (!m[i].indices || m[i].numIndices <= 0 || !m[i].material || translucent_material(m[i].material, g->flags & 0x40))
            goto refused;
        float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
        for (int j = 0; j < m[i].numIndices; j++) {
            unsigned v = m[i].indices[j];
            if (v >= (unsigned)g->numVertices)
                goto refused;
            const V3 *point = &g->morphTarget->verts[v];
            float xyz[3] = { point->x, point->y, point->z };
            for (int k = 0; k < 3; k++) {
                if (!isfinite(xyz[k]))
                    goto refused;
                if (xyz[k] < lo[k]) lo[k] = xyz[k];
                if (xyz[k] > hi[k]) hi[k] = xyz[k];
            }
        }
        p->geometry = *g;
        /* A freed wrapper's address can be reused. Force the first body draw to refresh LockMemo rather than
           trusting the previous wrapper's sampled hashes/UV shift at that same address. */
        p->geometry.locked = 1;
        p->target = *g->morphTarget;
        p->geometry.mesh = &p->draw.header;
        p->geometry.morphTarget = &p->target;
        p->target.parentGeom = &p->geometry;
        p->draw.header = *g->mesh;
        p->draw.header.numMeshes = 1;
        p->draw.header.totalIndices = m[i].numIndices;
        p->draw.header.firstMeshOffset = (char *)&p->draw.mesh - (char *)(&p->draw.header + 1);
        p->draw.mesh = m[i];
        p->source = m[i];
        memcpy(p->bounds,lo,12);memcpy(p->bounds+3,hi,12);
        float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
        for (int k = 0; k < 3; k++)
            p->target.sphere[k] = (lo[k] + hi[k]) * 0.5f;
        p->target.sphere[3] = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz) * 1.001f + 1e-3f;
        if (!(p->target.sphere[3] < 1e30f))
            goto refused;
        p->entry.atomic = &p->geometry;
        p->entry.stamp = cache_frame - 1; /* parent already passed the first-sight rule */
    }
    sl_bytes += bytes;
    if (p4_ge_static_clip) for (int i=0;i<n;i++) sl_ge_prepare_part(&s->part[i],model,normals);
    sl_mesh_builds++;
    return s;
refused:
    free(s);
    return 0;
}


/* b281 p4_inst_call: the next atomic of the same geometry, drawn right after a verified hit in the same state, takes the
   same list with its own matrix: no keys, no lookup. The list is in model space and starts from a forgotten GE state,
   so it serves every instance (Heaven: 84 flowers a frame, before b281 copied to world space every frame by
   p4_inst_batch). The guard-band test stays per instance. Same frame, camera, render and sky state, pixel mask,
   texture generation and no normals (lights), else the usual path. 0: as b280. */
int p4_inst_call = 1;
static struct {
    const Geometry *g;
    const StaticList *e;
    const void *camera;
    unsigned frame, texgen, mask, draws;
    unsigned *list;
    int render[MAX_STATE];
    unsigned sky[8];
    int lit;
    SceneLight light;
} sl_memo;
static unsigned sl_memo_hits, sl_memo_why[8]; /* b282: why the memo was not taken (same geometry only) */
static void sl_memo_set(const StaticList *e, const Geometry *g, const V3 *normals)
{
    if (!p4_inst_call)
        return;
    if (e->gview) { /* b288: cut for this model's view: no other instance may take it */
        sl_memo.g = 0;
        return;
    }
    sl_memo.lit = normals != 0;
    if (normals) /* b282: lit instances too, while the scene's lights stay (GE lights are in the world) */
        sl_memo.light = scene_lights();
    sl_memo.g = g, sl_memo.e = e, sl_memo.camera = current_camera, sl_memo.frame = cache_frame;
    sl_memo.texgen = texture_generation, sl_memo.mask = pixel_mask, sl_memo.list = e->list, sl_memo.draws = e->draws;
    memcpy(sl_memo.render, render_state, sizeof sl_memo.render);
    memcpy(sl_memo.sky, sky_state, sizeof sl_memo.sky);
}
/* 1: drawn from the memo */
static int sl_memo_try(const Geometry *g, const Matrix *model, const V3 *normals)
{
    if (!p4_inst_call || sl_memo.g != g)
        return 0;
    int why = sl_memo.lit != (normals != 0) ? 1 : sl_memo.frame != cache_frame || sl_memo.camera != current_camera ? 2 :
              sl_memo.texgen != texture_generation || sl_memo.mask != pixel_mask ? 3 : sl_memo.e->list != sl_memo.list || !sl_memo.list ? 4 :
              shadow_shear_m || rtt_target || draw_fixed_alpha || draw_skin_alpha ? 5 : memcmp(sl_memo.sky, sky_state, sizeof sl_memo.sky) ? 6 :
              memcmp(sl_memo.render, render_state, sizeof sl_memo.render) ? 7 : 0;
    if (!why && normals) {
        SceneLight now = scene_lights();
        why = memcmp(&now, &sl_memo.light, sizeof now) ? 1 : 0;
    }
    if (!why && !sl_inside(sl_memo.e, model, g->morphTarget->sphere))
        why = 8;
    if (why) {
        sl_memo_why[why & 7]++;
        return 0;
    }
    begin_frame();
    gu_mesh_begin();
    set_camera();
    set_matrix(GU_MODEL, model);
    unsigned *l = gu_list->current;
    l[0] = 0x10u << 24 | ((unsigned)sl_memo.list >> 8 & 0xf0000); /* BASE */
    l[1] = 0x0Au << 24 | ((unsigned)sl_memo.list & 0xffffff);      /* CALL */
    gu_list->current = l + 2;
    sl_forget();
    replay.valid = 0;
    sl_memo_hits++;
    mesh_draw_calls += sl_memo.draws;
    draws_since_stall += sl_memo.draws;
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}

/* 1: the entry is drawn (from its list, or through the body while recording). */
static int sl_draw(StaticList *e, const void *atomic, const Geometry *g, const Matrix *model, const V3 *normals)
{
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    int modulate = (g->flags & 0x40) != 0;
    const float *sphere = g->morphTarget->sphere;
    unsigned last = e->stamp;
    if (!e->list && last + 1 != cache_frame) { /* b259: recorded from the second frame in a row it is seen */
        e->stamp = cache_frame;
        sl_misses[1]++;
        return 0;
    }
    /* b297 p4_sl_trust (EXPERIMENT, picture may break): how fast the short path is without our checks. Bit 1: an
       entry with a list is drawn without its keys (state, materials, light, arrays, camera); bit 2: also without the
       guard-band test (a part near the camera goes uncut: the GE drops its crossing triangles). 0: as b296. */
    if (e->list && (p4_sl_trust & 1) && ((p4_sl_trust & 2) || (!e->gview && sl_inside(e, model, sphere)))) {
        e->stamp = cache_frame;
        sl_trusted++;
        goto draw_list;
    }
    if (e->list && e->gview) { /* b288: cut for one view; another view takes the body again */
        unsigned now[SL_VIEW];
        sl_view_now(now, model);
        if (!p4_static_guard_lists || !sl_view_near(now, e->gview)) {
            sl_guard_why[memcmp(now, e->gview, 12 * 4) ? 0 : memcmp(now + 12, e->gview + 12, 18 * 4) ? 1 : 2]++;
            static unsigned logged;
            if (logged < 12) {
                int w = 0;
                while (w < SL_VIEW - 1 && now[w] == e->gview[w])
                    w++;
                logged++;
                p4_log("TRACE guard list view: entry %p word %d was %08x now %08x; camera %08x/%08x frame %u built %u\n", (void *)e, w,
                       e->gview[w], now[w], e->gview[12], now[12], cache_frame, e->built);
            }
            /* the guard result needs the new view still for two frames first; a view that keeps changing (a camera
               that drifts, a moving part) goes the long way for a while instead of recording again and again */
            e->gchurn = cache_frame - e->built < 32 ? e->gchurn + 1 : 0;
            e->retry = cache_frame + (e->gchurn >= 2 ? 300 : 2);
            sl_guard_churned += e->gchurn >= 2;
            sl_drop(e);
            e->stamp = cache_frame;
            sl_guard_moved++;
            return 0;
        }
        sl_guard_hits++;
    } else if (e->list && !sl_inside(e, model, sphere)) { /* near the camera: the body cuts it on the CPU */
        e->split = 1;
        e->stamp = cache_frame;
        sl_misses[0]++;
        return 0;
    }
    unsigned state = sl_state_key(), materials = sl_material_key(g), texgen = texture_generation, bits = sl_bits(), light = 0;
    if (normals) {
        SceneLight l = scene_lights();
        light = hash_words(2166136261u, &l, sizeof l);
    }
    int same = e->g == g && e->verts == g->morphTarget->verts && e->uv == uv && e->prelit == g->preLit && e->normals == normals &&
               e->count == g->numVertices && e->modulate == modulate && e->camera == current_camera;
    int hit = e->list && same && !g->locked && e->state == state && e->materials == materials && e->texgen == texgen && e->bits == bits &&
              e->light == light;
    e->stamp = cache_frame;
    if (hit && !(p4_static_list_check && !(cache_frame & 63))) {
    draw_list:
        begin_frame();
        gu_mesh_begin();
        set_camera();
        set_matrix(GU_MODEL, model);
        unsigned *l = gu_list->current;
        l[0] = 0x10u << 24 | ((unsigned)e->list >> 8 & 0xf0000); /* BASE */
        l[1] = 0x0Au << 24 | ((unsigned)e->list & 0xffffff);      /* CALL */
        gu_list->current = l + 2;
        sl_forget(); /* the list left the GE in its own state */
        replay.valid = 0;
        sl_hits++;
        if (e->atomic) /* a whole atomic's entry, not a mesh part's */
            sl_memo_set(e, g, normals);
        mesh_draw_calls += e->draws;
        draws_since_stall += e->draws;
        if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
            stall_flush();
        return 1;
    }
    if (!hit) {
        sl_misses[!e->list ? 1 : !same ? 2 : g->locked ? 3 : e->state != state ? 4 : e->materials != materials ? 5 : e->texgen != texgen ? 6 : 7]++;
        if ((int)(e->retry - cache_frame) > 0)
            return 0; /* refused or changing lately: the usual path for a while */
        if (e->list && cache_frame - e->built < 8 && ++e->churn >= 3) { /* inputs that keep changing */
            sl_drop(e);
            e->churn = 0, e->retry = cache_frame + 256;
            return 0;
        }
    }
    /* record: the body from a forgotten state, inline */
    begin_frame();
    gu_mesh_begin();
    set_camera();
    sl_forget();
    replay.valid = 0;
    unsigned late = late_queued;
    unsigned *from = gu_list->current;
    sl_recording = 1, sl_nslots = 0;
    sl_nextra = 0, sl_gbad = 0, sl_gview_set = 0;
    mesh_model_begin = mesh_model_mark = 0;
    gu_object_stack_depth++; /* no stall-address update inside the words */
    draw_geometry = (Geometry *)g;
    draw_meshes_body(g->mesh, g->morphTarget->verts, uv, g->preLit, g->numVertices, model, modulate, normals, sphere);
    draw_geometry = 0;
    draw_material_ge = 0;
    gu_object_stack_depth--;
    sl_recording = 0;
    unsigned view[SL_VIEW];
    if (sl_gview_set && !sl_gbad) { /* the cut was made for the matrix this entry is drawn with */
        sl_view_now(view, model);
        if (memcmp(view, sl_gview_rec, sizeof view))
            sl_gbad = 1;
    }
    unsigned *gview = sl_gview_set && !sl_gbad ? malloc(sizeof view) : 0;
    if (sl_gview_set && !sl_gbad && !gview)
        sl_gbad = 1;
    int kept = late_queued == late && !sl_gbad && sl_keep(e, from, gu_list->current, hit, g);
    if (late_queued != late)
        sl_refused[6]++;
    else if (sl_gbad)
        sl_guard_refused++;
    if (kept && gview) {
        memcpy(gview, view, sizeof view);
        e->gview = gview, gview = 0;
        sl_guard_lists++;
    }
    free(gview);
    if (kept) {
        e->built = cache_frame;
        sl_parts(e, g); /* checked only when the atomic's own sphere is not inside */
        e->g = g, e->verts = g->morphTarget->verts, e->uv = uv, e->prelit = g->preLit, e->normals = normals, e->count = g->numVertices;
        e->modulate = modulate, e->camera = current_camera, e->state = state, e->materials = materials, e->texgen = texgen;
        e->bits = bits, e->light = light;
    } else if (!hit) {
        sl_drop(e);
        /* try again later (a mesh not in the cache yet records on a later frame); b288: a guard result not ready yet
           (moving camera, or the result's two frames) soon */
        e->retry = cache_frame + (sl_gbad && late_queued == late ? 2 : 32);
    }
    if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
        stall_flush();
    return 1;
}

static int sl_mesh_draw(SlMeshSet *s, const Matrix *model, const V3 *normals)
{
    begin_frame();
    gu_mesh_begin();
    for (int i = 0; i < s->count; i++) {
        SlMeshPart *p = &s->part[i];
        const V3 *part_normals = normals && p->prepared ? p->target.normals : normals;
        sl_ge_uncut = p4_ge_static_clip && p->prepared && p->max_edge < sl_ge_limit(model,p->bounds);
        if (sl_ge_uncut) sl_ge_hits++; else if (p4_ge_static_clip) sl_ge_fallbacks++;
        unsigned hits = sl_hits;
        if (!sl_draw(&p->entry, &p->geometry, &p->geometry, model, part_normals)) {
            draw_geometry = &p->geometry;
            draw_meshes(p->geometry.mesh, p->target.verts, p->geometry.numTexCoordSets ? p->geometry.texCoords[0] : 0,
                        p->geometry.preLit, p->geometry.numVertices, model, p->geometry.flags & 0x40, part_normals, p->target.sphere);
            draw_geometry = 0;
        }
        sl_mesh_hits += sl_hits != hits;
        sl_mesh_fallback += sl_hits == hits;
        sl_ge_uncut = 0;
    }
    return 1;
}

/* The complete atomic stays the first choice. Split only entries refused while recording or crossing the band. */
static int sl_try(const void *atomic, const Geometry *g, const Matrix *model, const V3 *normals)
{
    if (!p4_static_list || !p4_state_cache || p4_draw_batch <= 1 || shadow_shear_m || rtt_target || p4_mesh_frustum || p4_probe_depth_mode ||
        p4_probe_3d || p4_hide_material || p4_gs_test_raw_prelit || (normals && !p4_ge_light) ||
        !(g->morphTarget->sphere[3] > 0) || !current_camera || draw_fixed_alpha || draw_skin_alpha)
        return 0;
    if (g->mesh->numMeshes > SL_MESHES && !p4_static_mesh_lists)
        return 0;
    StaticList *e = sl_find(atomic);
    if (!e)
        return 0;
    if (e->meshes && (!p4_static_mesh_lists || !sl_mesh_same(e->meshes, g, normals))) {
        sl_drop(e);
        e->retry = 0;
    }
    if (p4_static_mesh_lists && !e->meshes && !g->locked && e->stamp + 1 == cache_frame &&
        (e->split || g->mesh->numMeshes > SL_MESHES)) {
        SlMeshSet *s = sl_mesh_build(g, normals, model);
        if (s) {
            sl_drop(e);
            e->meshes = s;
        }
    }
    if (e->meshes) {
        e->stamp = cache_frame;
        return sl_mesh_draw(e->meshes, model, normals);
    }
    if (g->mesh->numMeshes > SL_MESHES) {
        e->stamp = cache_frame;
        return 0;
    }
    return sl_draw(e, atomic, g, model, normals);
}
static void sl_report(void)
{
    p4_log("static state cache %d check %d: reuse %u rebuild %u diffs %u per 256 frames\n", p4_static_state_cache, p4_static_state_check, sl_state_reuses, sl_state_rebuilds, sl_state_diffs);
    sl_state_reuses = sl_state_rebuilds = sl_state_diffs = 0;
    p4_log("static list %d check %d per 256 frames: hits %u records %u, %u KB; misses new %u changed %u guard %u locked %u state %u material %u texture %u other %u\n",
           p4_static_list, p4_static_list_check, sl_hits, sl_records, sl_bytes / 1024, sl_misses[1], sl_misses[2], sl_misses[0], sl_misses[3],
           sl_misses[4], sl_misses[5], sl_misses[6], sl_misses[7]);
    p4_log("static list refused: marks %u size %u slot %u memory %u flow %u address %u late %u; checks %u diffs %u; evictions %u\n", sl_refused[0], sl_refused[1],
           sl_refused[2], sl_refused[3], sl_refused[4], sl_refused[5], sl_refused[6], sl_checks, sl_check_diffs, sl_evictions);
    p4_log("static record size reasons: empty %u words %u no_slots %u many_slots %u per 256 frames\n",
           sl_size_why[0], sl_size_why[1], sl_size_why[2], sl_size_why[3]);
    p4_log("static guard lists %d per 256 frames: hits %u records %u view changed %u (model %u camera %u options %u) held %u not ready %u\n",
           p4_static_guard_lists, sl_guard_hits, sl_guard_lists, sl_guard_moved, sl_guard_why[0], sl_guard_why[1], sl_guard_why[2],
           sl_guard_churned, sl_guard_refused);
    sl_guard_hits = sl_guard_lists = sl_guard_moved = sl_guard_refused = sl_guard_churned = 0;
    p4_log("static trust %d: trusted draws %u per 256 frames\n", p4_sl_trust, sl_trusted);
    sl_trusted = 0;
    memset(sl_guard_why, 0, sizeof sl_guard_why);
    memset(sl_size_why, 0, sizeof sl_size_why);
    sl_evictions = 0;
    p4_log("inst call %d: instances %u per 256 frames; not taken: guard %u lit %u frame %u texture %u list %u pass %u sky %u render %u\n",
           p4_inst_call, sl_memo_hits, sl_memo_why[0], sl_memo_why[1], sl_memo_why[2], sl_memo_why[3], sl_memo_why[4], sl_memo_why[5],
           sl_memo_why[6], sl_memo_why[7]);
    sl_memo_hits = 0;
    memset(sl_memo_why, 0, sizeof sl_memo_why);
    p4_log("static mesh lists %d per 256 frames: hits %u fallback %u builds %u\n", p4_static_mesh_lists, sl_mesh_hits, sl_mesh_fallback, sl_mesh_builds);
    p4_log("GE static clip %d growth %d: certified %u fallback %u prepared %u refused %u per 256 frames\n",
           p4_ge_static_clip,p4_ge_clip_growth,sl_ge_hits,sl_ge_fallbacks,sl_ge_builds,sl_ge_refused);
    sl_ge_hits=sl_ge_fallbacks=sl_ge_builds=sl_ge_refused=0;
    sl_mesh_hits = sl_mesh_fallback = sl_mesh_builds = 0;
    sl_hits = sl_records = sl_checks = 0;
    memset(sl_refused, 0, sizeof sl_refused);
    memset(sl_misses, 0, sizeof sl_misses);
}
