/* Skinned characters (moved from rwsky.c, b322; included once, at the same place): RpSkin and RpHAnim
   records, GE skinning batches, strip pack, skin LOD and far levels, bone LOD, CPU-skinned batches, skin lists. */
/* RpSkin and RpHAnimHierarchy prefixes */
typedef struct {
    int numBones, numBoneIds;
    unsigned char *boneIds;
    Matrix *skinToBone;
    int maxWeights;
    unsigned char (*indices)[4];
    float (*weights)[4];
} Skin;
typedef struct { int flags, numNodes; Matrix *matrices; } Hierarchy;

enum { HIERARCHY_NO_MATRICES = 2, HIERARCHY_LOCAL_SPACE = 0x4000, MAX_BONES = 256, MAX_SKINNED = 32 * 1024 };
#define SKIN_OF_GEOMETRY(g) (*(Skin **)((char *)(g) + R32(0x886498)))
#define HIERARCHY_OF_ATOMIC(a) (*(Hierarchy **)((char *)(a) + R32(0x886494)))

static inline V3 rotate(const V3 *v, const Matrix *m)
{
    V3 out = {
        v->x * m->right.x + v->y * m->up.x + v->z * m->at.x,
        v->x * m->right.y + v->y * m->up.y + v->z * m->at.y,
        v->x * m->right.z + v->y * m->up.z + v->z * m->at.z,
    };
    return out;
}

static inline V3 transform(const V3 *v, const Matrix *m)
{
    V3 out = rotate(v, m);
    out.x += m->pos.x;
    out.y += m->pos.y;
    out.z += m->pos.z;
    return out;
}

/* a, then b */
static void multiply(Matrix *out, const Matrix *a, const Matrix *b);
/* b182: does the shadow pass's flattened pose equal the normal pose times the shear it builds (mdlUpdateDrawState's
   identity: up row = -light.xz / light.y, 0.01)? phase 0 before its pose update (snapshot), 1 after it (plain update;
   11 the look-at pose), 2 after the pose is restored, 3 a shadow at alpha 0 (pose only). Every 16th frame. If 1 holds,
   the GE can draw the shadow from the body's bones with the shear as its world matrix. */
int p4_shadow_probe = 0; /* b182-b185 diagnostic: 1 checks the shadow pose every 16th frame */
/* b186: b185 on the PSP, every plain shadow pose (no look-at) was the body pose times the shear, to 1e-3. With
   p4_shadow_shear the shadow pass (census FIXES in mdlUpdateDrawState) poses the skeleton once, for the body, and
   draws the shadow from it: atomic models and world-space bones go through the shear (p4_shadow_shear_set). Look-at
   poses keep the game's own way (b187: blend tracks no longer do). 0: as b185. */
int p4_shadow_shear = 1;
static Matrix shadow_shear_copy __attribute__((aligned(16)));
static const Matrix *shadow_shear_m;
static unsigned shadow_shear_draws;
int p4_shadow_capsule = 1; /* b194: 0 original projected model, 1 projected capsule mask */
static unsigned capsule_draws, capsule_segments, capsule_fallbacks, capsule_rigid;
static unsigned capsule_alpha = 128;
static void capsule_report(unsigned frames);
static void capsule_begin(void);
static void capsule_end(void);
int p4_shadow_shear_on(void) { return p4_shadow_shear || p4_shadow_capsule; }
/* b192: look-at models drawn from the body pose too (census FIXES). mdlUpdateHierarchyPose slerps the stored look-at
   rotation toward its target by blend and keeps it; the game called it twice a tick (the shadow with the shear as
   parent, then the body). The one body call takes 1 - (1 - blend)^2: two steps, so a head turns as fast; once turned,
   the head points at the target without the shadow call's sheared one. 0: look-at shadows as b191. */
int p4_shadow_lookat = 1;
int p4_shadow_lookat_on(void) { return p4_shadow_lookat || p4_shadow_capsule; }
void p4_shadow_shear_set(const void *m)
{
    if (m) {
        memcpy(&shadow_shear_copy, m, sizeof shadow_shear_copy);
        shadow_shear_m = &shadow_shear_copy;
        capsule_begin();
        shadow_shear_draws++;
    } else {
        if (shadow_shear_m) capsule_end();
        shadow_shear_m = 0;
    }
}
void p4_shadow_shear_set_color(const void *m, unsigned alpha)
{
    capsule_alpha = alpha;
    p4_shadow_shear_set(m);
}
static Matrix *shadow_snap; /* heap, 8 KB: .bss must end below the data image */
static int shadow_snap_n, shadow_snap_ok;
static Matrix *shadow_pose, shadow_shear; /* b184: the shadow pose and its shear, checked against the restored pose */
static unsigned shadow_post_checked, shadow_post_match, shadow_post_bad;
static float shadow_post_worst;
static unsigned shadow_calls[4], shadow_checked[3], shadow_match[3], shadow_logged;
static float shadow_worst[3];
static float matrix_diff(const Matrix *a, const Matrix *b)
{
    const float *x = &a->right.x, *y = &b->right.x;
    float worst = 0;
    for (int k = 0; k < 16; k++) {
        if ((k & 3) == 3)
            continue;
        float d = fabsf(x[k] - y[k]) / (1 + fabsf(y[k]));
        if (!(d <= worst))
            worst = d == d ? d : 1e30f;
    }
    return worst;
}
void p4_shadow_check(void *hierarchy, const void *shear, int phase)
{
    const Hierarchy *h = hierarchy;
    int p = phase == 11 ? 1 : phase;
    if (p >= 0 && p < 4)
        shadow_calls[p]++;
    if (!p4_shadow_probe || !h || !h->matrices || (cache_frame & 15))
        return;
    int n = h->numNodes < 128 ? h->numNodes : 128;
    if (!shadow_snap && !(shadow_snap = memalign(16, 128 * sizeof *shadow_snap)))
        return;
    if (p == 0) {
        memcpy(shadow_snap, h->matrices, n * sizeof *shadow_snap);
        shadow_snap_n = n, shadow_snap_ok = 1;
        return;
    }
    if (p == 3 || !shadow_snap_ok || shadow_snap_n != n || !shear)
        return;
    if (!shadow_pose && !(shadow_pose = memalign(16, 128 * sizeof *shadow_pose)))
        return;
    if (p == 1) {
        memcpy(shadow_pose, h->matrices, n * sizeof *shadow_pose);
        shadow_shear = *(const Matrix *)shear;
    } else if (p == 2) {
        float post = 0;
        int bad = 0;
        for (int i = 0; i < n; i++) {
            Matrix want;
            multiply(&want, &h->matrices[i], &shadow_shear);
            float d = matrix_diff(&shadow_pose[i], &want);
            bad += !(d < 1e-3f);
            if (d > post)
                post = d;
        }
        shadow_post_checked++;
        shadow_post_match += post < 1e-3f;
        shadow_post_bad += bad;
        if (post > shadow_post_worst)
            shadow_post_worst = post;
    }
    float worst = 0;
    int worst_bone = 0, bad = 0, parked = 0;
    Matrix worst_want;
    for (int i = 0; i < n; i++) {
        Matrix want;
        if (p == 1)
            multiply(&want, &shadow_snap[i], (const Matrix *)shear);
        else
            want = shadow_snap[i];
        float d = matrix_diff(&h->matrices[i], &want);
        bad += !(d < 1e-3f);
        parked += !(fabsf(shadow_snap[i].pos.x) < 1e6f && fabsf(shadow_snap[i].pos.y) < 1e6f && fabsf(shadow_snap[i].pos.z) < 1e6f);
        if (d > worst)
            worst = d, worst_bone = i, worst_want = want;
    }
    int k = p == 1 ? (phase == 11 ? 2 : 0) : 1; /* 0 plain shadow pose, 1 restored, 2 look-at shadow pose */
    shadow_checked[k]++;
    shadow_match[k] += worst < 1e-3f;
    if (worst > shadow_worst[k])
        shadow_worst[k] = worst;
    if (worst >= 1e-3f && shadow_logged < 40) {
        shadow_logged++;
        /* b184: which bones differ, and how: the worst one's now / expected / snapshot (pos, then up x1000) */
        const Matrix *a = &h->matrices[worst_bone], *w = &worst_want, *b = &shadow_snap[worst_bone];
        p4_log("TRACE shadow check ph %d h %p n %d bad %d parked %d worst %d/1e6 bone %d now %d %d %d u %d %d %d want %d %d %d u %d %d %d snap %d %d %d\n",
               phase, hierarchy, n, bad, parked, (int)(worst > 1e3f ? 1e9f : worst * 1e6f), worst_bone, (int)a->pos.x, (int)a->pos.y, (int)a->pos.z,
               (int)(a->up.x * 1000), (int)(a->up.y * 1000), (int)(a->up.z * 1000), (int)w->pos.x, (int)w->pos.y, (int)w->pos.z, (int)(w->up.x * 1000),
               (int)(w->up.y * 1000), (int)(w->up.z * 1000), (int)b->pos.x, (int)b->pos.y, (int)b->pos.z);
    }
    if (p == 2)
        shadow_snap_ok = 0;
}
static void shadow_report(unsigned frames)
{
    capsule_report(frames);
    p4_log("shadow skip %d: %u calls per %u frames; shear %d: %u shadows\n", p4_shadow_skip_on, shadow_skips, frames, p4_shadow_shear,
           shadow_shear_draws), shadow_skips = 0, shadow_shear_draws = 0;
    p4_log("shadow pose vs restored x shear: checked %u match %u bad bones %u worst %d/1e6\n", shadow_post_checked, shadow_post_match,
           shadow_post_bad, (int)(shadow_post_worst > 1e3f ? 1e9f : shadow_post_worst * 1e6f));
    shadow_post_checked = shadow_post_match = shadow_post_bad = 0, shadow_post_worst = 0;
    p4_log("shadow probe %d per %u frames: calls before %u after %u restored %u invisible %u; checked/match/worst(1e-6) plain %u %u %d restored %u %u %d lookat %u %u %d\n",
           p4_shadow_probe, frames, shadow_calls[0], shadow_calls[1], shadow_calls[2], shadow_calls[3], shadow_checked[0], shadow_match[0],
           (int)(shadow_worst[0] > 1e3f ? 1e9f : shadow_worst[0] * 1e6f), shadow_checked[1], shadow_match[1],
           (int)(shadow_worst[1] > 1e3f ? 1e9f : shadow_worst[1] * 1e6f), shadow_checked[2], shadow_match[2],
           (int)(shadow_worst[2] > 1e3f ? 1e9f : shadow_worst[2] * 1e6f));
    memset(shadow_calls, 0, sizeof shadow_calls), memset(shadow_checked, 0, sizeof shadow_checked), memset(shadow_match, 0, sizeof shadow_match);
    memset(shadow_worst, 0, sizeof shadow_worst);
}
static void multiply(Matrix *out, const Matrix *a, const Matrix *b)
{
    out->right = rotate(&a->right, b);
    out->up = rotate(&a->up, b);
    out->at = rotate(&a->at, b);
    out->pos = transform(&a->pos, b);
}

/* ponytail: skinned on the CPU, every vertex every frame; the GE's 8 bone matrices need meshes split, when the device asks for it. */
static const V3 *skin_vertices_body(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals);
static const V3 *skin_vertices(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals)
{
    prof_begin(PROF_SKIN);
    const V3 *r = skin_vertices_body(g, skin, hierarchy, normals);
    prof_end(PROF_SKIN);
    return r;
}

static Matrix bones[MAX_BONES] __attribute__((aligned(16)));

/* bones[i] = skin-to-bone, then the bone's pose; returns how many, -1 when there are too many */
static int hierarchy_nodes_seen; /* the hierarchy of the geometry being built, for its log */
/* b155: a party member's head drawn as a line in battle, in some rounds (b150-b154: not the GE skin, the look-at, the
   scale hook, frameskip). Every bone matrix checked each frame: not finite, or a row length outside 0.01..100, is logged
   with the hierarchy's own matrix (first 24 cases). */
int p4_bone_check = 0; /* b169: off by default. With the profiler on, hanim_scan_after re-scanned every recent bone matrix after
   every task: a dungeon corridor 108 ms -> 33 ms a frame (b163 on the PSP, frameskip on). */
static unsigned bone_bad_logged;
static int bone_row_ok(const V3 *r)
{
    float l = r->x * r->x + r->y * r->y + r->z * r->z;
    return l == l && l > 1e-4f && l < 1e4f;
}
static int bone_finite(const Matrix *m)
{
    const float *f = &m->right.x;
    for (int k = 0; k < 16; k++)
        if (k % 4 != 3 && !(f[k] == f[k] && f[k] < 1e8f && f[k] > -1e8f))
            return 0;
    return 1;
}
static void bone_check(const void *skin_, const void *hierarchy_, int count);
static const void *bone_check_geometry;
int hanim_age(const void *h);
/* out = a x b (RenderWare rows: a's rows through b) on the VFPU: b's rows in M400, a's rows with w 0, 0, 0, 1 in
   M600, each row through E400 as guard_vfpu transforms points. 0 when a matrix is not 16-byte aligned. */
static int multiply_vfpu(Matrix *out, const Matrix *a, const Matrix *b)
{
    if (((unsigned)out | (unsigned)a | (unsigned)b) & 15)
        return 0;
    __asm__ volatile(
        "lv.q    C400, 0(%[b])\n lv.q C410, 16(%[b])\n lv.q C420, 32(%[b])\n lv.q C430, 48(%[b])\n"
        "lv.q    C600, 0(%[a])\n lv.q C610, 16(%[a])\n lv.q C620, 32(%[a])\n lv.q C630, 48(%[a])\n"
        "vzero.s S603\n vzero.s S613\n vzero.s S623\n vone.s S633\n"
        "vtfm4.q C700, E400, C600\n"
        "vtfm4.q C710, E400, C610\n"
        "vtfm4.q C720, E400, C620\n"
        "vtfm4.q C730, E400, C630\n"
        "sv.q    C700, 0(%[o])\n sv.q C710, 16(%[o])\n sv.q C720, 32(%[o])\n sv.q C730, 48(%[o])\n"
        : : [o] "r"(out), [a] "r"(a), [b] "r"(b) : "memory");
    return 1;
}
static void anim_drawn(const void *hierarchy);
static int skin_bones(const Skin *skin, const Hierarchy *hierarchy)
{
    anim_drawn(hierarchy);
    hierarchy_nodes_seen = hierarchy->numNodes;
    int count = skin->numBones < hierarchy->numNodes ? skin->numBones : hierarchy->numNodes;
    if (count > MAX_BONES)
        return -1;
    for (int i = 0; i < count; i++) {
        if (!p4_skin_vfpu || !multiply_vfpu(&bones[i], &skin->skinToBone[i], &hierarchy->matrices[i])) {
            skin_vfpu_unaligned += p4_skin_vfpu;
            multiply(&bones[i], &skin->skinToBone[i], &hierarchy->matrices[i]);
            continue;
        }
        if (p4_skin_vfpu_check) {
            Matrix ref;
            multiply(&ref, &skin->skinToBone[i], &hierarchy->matrices[i]);
            const float *x = &bones[i].right.x, *y = &ref.right.x;
            int differs = 0;
            for (int k = 0; k < 16; k++) {
                if ((k & 3) == 3)
                    continue;
                float d = fabsf(x[k] - y[k]) / (1 + fabsf(y[k]));
                if (!(d <= 1e-4f))
                    differs = 1;
                if (d > skin_vfpu_max_diff)
                    skin_vfpu_max_diff = d;
            }
            skin_vfpu_checks++;
            skin_vfpu_diffs += differs;
        }
    }
    if (p4_bone_check)
        bone_check(skin, hierarchy, count);
    return count;
}

static void bone_check(const void *skin_, const void *hierarchy_, int count)
{
    const Skin *skin = skin_;
    const Hierarchy *hierarchy = hierarchy_;
    for (int i = 0; i < count; i++) {
        const Matrix *b = &bones[i], *h = &hierarchy->matrices[i];
        if (bone_finite(b) && bone_row_ok(&b->right) && bone_row_ok(&b->up) && bone_row_ok(&b->at))
            continue;
        bone_bad_count++;
        if (bone_bad_logged >= 24)
            continue;
        bone_bad_logged++;
        p4_log("TRACE bad bone geometry %p skin %p bones %d hierarchy %p age %d flags %x nodes %d bone %d/%d: hier right %d %d %d up %d %d %d at %d %d %d pos %d %d %d (x1000)\n",
               bone_check_geometry, (void *)skin, skin->numBones, (void *)hierarchy, hanim_age(hierarchy), hierarchy->flags, hierarchy->numNodes, i, count,
               (int)(h->right.x * 1000), (int)(h->right.y * 1000), (int)(h->right.z * 1000), (int)(h->up.x * 1000), (int)(h->up.y * 1000),
               (int)(h->up.z * 1000), (int)(h->at.x * 1000), (int)(h->at.y * 1000), (int)(h->at.z * 1000), (int)h->pos.x, (int)h->pos.y,
               (int)h->pos.z);
    }
}

static const V3 *skin_vertices_body(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, int normals)
{
    static V3 (*skinned)[MAX_SKINNED]; /* heap, 768 KB: .bss must end below the data image */
    if (!skinned && !(skinned = malloc(2 * sizeof *skinned)))
        return 0;
    bone_check_geometry = g;
    if (g->numVertices > MAX_SKINNED || skin_bones(skin, hierarchy) < 0)
        return 0;
    const V3 *in = normals ? g->morphTarget->normals : g->morphTarget->verts;
    for (int i = 0; i < g->numVertices; i++) {
        if (skin->maxWeights == 1) { /* RenderWare leaves the single weight out of the stream */
            skinned[normals][i] = normals ? rotate(&in[i], &bones[skin->indices[i][0]]) : transform(&in[i], &bones[skin->indices[i][0]]);
            continue;
        }
        V3 sum = { 0, 0, 0 };
        for (int w = 0; w < skin->maxWeights && w < 4 && skin->weights[i][w] > 0; w++) {
            V3 moved = normals ? rotate(&in[i], &bones[skin->indices[i][w]]) : transform(&in[i], &bones[skin->indices[i][w]]);
            float weight = skin->weights[i][w];
            sum.x += moved.x * weight;
            sum.y += moved.y * weight;
            sum.z += moved.z * weight;
        }
        skinned[normals][i] = sum;
    }
    return skinned[normals];
}

/* Skinning on the GE. Built once per geometry: each mesh's triangles in batches of at most 8 bones (the GE's bone
   matrices), every vertex with its weights for that batch's palette as u8 / 128 summing to 128 (the GE does not
   normalise them), UV, prelight, s8 normal when lit and the bind-pose position. Per frame only the bone matrices
   go to the GE, which skins positions and normals and lights them (bind_ge_light; unlit meshes get the material
   colour through the same path with no light). A skeleton whose sphere may leave the guard band stays on the CPU. */
int p4_ge_skin = 1; /* 0: every skinned atomic on the CPU, as b76 */
typedef struct { int mesh, bones, vtype, indices, vertices, wreal, rigid, strip_count, strip_n; unsigned char palette[8]; unsigned char *v; unsigned short *idx, *src, *strip, *strip_lens; int alpha[2]; GeCall call[2]; float uvs[4]; } SkinBatch;
typedef struct { const Geometry *g; const V3 *verts; int vertices, lit, nrm, batches, nradius, packed, nmap; unsigned sample, frame, bytes, used[8]; SkinBatch *batch; float *radius, *box; unsigned short regroup[8]; float pack[4]; unsigned char *bone_map; float *bone_len; void *list; } SkinGeometry;
enum { SKIN_GEOMETRIES = 64, SKIN_PALETTE = 8 };
static SkinGeometry *skin_cache; /* heap: .bss is full */
/* b385 p4_skin_guard_box: a bone whose sphere leaves the guard band is tested again with the box (bone space) round
   the vertices it carries. Each vertex is a weighted mean of its bones' images of it, each inside that bone's box, and
   the band is convex: every box inside means every vertex inside. b383 Velvet Room, Satan after a fusion: his blades
   made the spheres huge, every frame went to the CPU skin path (31 batches, ~12 ms) and nothing was ever cut. */
int p4_skin_guard_box = 1;
static unsigned skin_box_saved;
unsigned skin_box_saved_count(void) { unsigned n = skin_box_saved; skin_box_saved = 0; return n; }
static int skin_box_inside(float P[5][4], const Matrix *pose, const float *bx)
{
    if (!(bx[0] <= bx[3] && bx[1] <= bx[4] && bx[2] <= bx[5]))
        return 1; /* no vertex on this bone */
    if (!(bx[0] > -1e30f && bx[3] < 1e30f && bx[1] > -1e30f && bx[4] < 1e30f && bx[2] > -1e30f && bx[5] < 1e30f))
        return 0;
    for (int p = p4_guard_gnear ? 1 : 0; p < 5; p++) {
        float ax = P[p][0] * pose->right.x + P[p][1] * pose->right.y + P[p][2] * pose->right.z;
        float ay = P[p][0] * pose->up.x + P[p][1] * pose->up.y + P[p][2] * pose->up.z;
        float az = P[p][0] * pose->at.x + P[p][1] * pose->at.y + P[p][2] * pose->at.z;
        float least = P[p][0] * pose->pos.x + P[p][1] * pose->pos.y + P[p][2] * pose->pos.z + P[p][3];
        least += ax > 0 ? ax * bx[0] : ax * bx[3];
        least += ay > 0 ? ay * bx[1] : ay * bx[4];
        least += az > 0 ? az * bx[2] : az * bx[5];
        if (!(least > 0))
            return 0;
    }
    return 1;
}

/* b205 P3P-shaped skin vertices (P3P's battle frame: s16 positions and u16 UVs on every 3D draw). Position: s16 over
   the geometry's bind-pose box, pack = centre xyz and half extent s, the GE reading q / 32768; draw_skinned_ge folds
   p = centre + s q / 32768 into each bone matrix it sends (rotation x s, translation + rotation . centre). The
   skinned normal grows by s too: the GE divides its lighting dot products by the normal's length (PPSSPP's
   software GE, from hardware probes), so a uniform s does not change the light. UV: u16 over the batch's range,
   TEXSCALE/TEXOFFSET (batch uvs) mapping it back; the offset loses its whole part (skins bind GU_REPEAT), as a
   float24 offset of 100.3 would move texels. 8 bones lit: 36 -> 28 bytes a vertex, unlit 32 -> 24. */
int p4_skin_pack = 1; /* 0: float positions and UVs, as b204 */
/* b208 p4_skin_palette: at most this many bones a batch (1-8; the GE blends that many weights a vertex). A first
   triangle with more bones than the cap still starts a batch of its own. In the cache key. */
int p4_skin_palette = 8;
typedef struct { int uv, colour, normal, position, stride; } SkinLayout;
static SkinLayout skin_layout(int bones, int lit, int packed)
{
    SkinLayout l;
    if (packed) { /* GE order u8 weights, u16 UV, 8888 colour, s8 normal, s16 position; each at its own size, stride x4 */
        l.uv = (bones + 1) & ~1;
        l.colour = (l.uv + 4 + 3) & ~3;
        l.normal = l.colour + 4;
        l.position = l.normal + (lit ? 4 : 0);
        l.stride = (l.position + 6 + 3) & ~3;
    } else {
        l.uv = (bones + 3) & ~3;
        l.colour = l.uv + 8;
        l.normal = l.colour + 4;
        l.position = l.normal + (lit ? 4 : 0);
        l.stride = l.position + 12;
    }
    return l;
}
/* a batch vertex's bind position and UV as floats (pack 0: stored as floats) */
static void skin_vertex_read(const unsigned char *in, const SkinLayout *l, const float *pack, const float *uvs, V3 *bind, float uv[2])
{
    if (!pack) {
        memcpy(bind, in + l->position, 12);
        memcpy(uv, in + l->uv, 8);
        return;
    }
    short q[3];
    unsigned short t[2];
    memcpy(q, in + l->position, 6);
    memcpy(t, in + l->uv, 4);
    float k = pack[3] * (1.0f / 32768);
    *bind = (V3){ pack[0] + q[0] * k, pack[1] + q[1] * k, pack[2] + q[2] * k };
    uv[0] = t[0] * (1.0f / 32768) * uvs[0] + uvs[2];
    uv[1] = t[1] * (1.0f / 32768) * uvs[1] + uvs[3];
}
static inline int clamp_round(float x, int lo, int hi)
{
    if (!(x > lo)) return lo; /* NaN too */
    if (!(x < hi)) return hi;
    return (int)lrintf(x);
}

/* the bones a vertex hangs from, with their weights (RenderWare leaves a single weight out of the stream) */
static int vertex_bones(const Skin *skin, int nbones, int i, unsigned char bone[4], float weight[4]);
static void mk_tri_bones(const Skin *skin, int nbones, const int *c, unsigned char *set)
{
    set[0] = 0;
    for (int k = 0; k < 3; k++) {
        unsigned char bone[4];
        float weight[4];
        int n = vertex_bones(skin, nbones, c[k], bone, weight);
        for (int w = 0; w < n; w++) {
            int have = 0;
            for (int s = 1; s <= set[0] && !have; s++)
                have = set[s] == bone[w];
            if (!have && set[0] < 12)
                set[++set[0]] = bone[w];
        }
    }
}
static const unsigned char *vertex_bone_map; /* b254: set while a far skin entry is built (bone_lod.h) */
static int vertex_bones_raw(const Skin *skin, int nbones, int i, unsigned char bone[4], float weight[4])
{
    if (skin->maxWeights == 1) {
        bone[0] = skin->indices[i][0];
        weight[0] = 1;
        return bone[0] < nbones;
    }
    int n = 0;
    for (int k = 0; k < skin->maxWeights && k < 4; k++)
        if (skin->weights[i][k] > 0 && skin->indices[i][k] < nbones) {
            bone[n] = skin->indices[i][k];
            weight[n++] = skin->weights[i][k];
        }
    return n;
}
static int vertex_bones(const Skin *skin, int nbones, int i, unsigned char bone[4], float weight[4])
{
    int n = vertex_bones_raw(skin, nbones, i, bone, weight);
    if (!vertex_bone_map)
        return n;
    int m = 0; /* each weight to its kept bone; weights that land on one bone are summed */
    for (int k = 0; k < n; k++) {
        unsigned char b = vertex_bone_map[bone[k]];
        int s = 0;
        while (s < m && bone[s] != b)
            s++;
        if (s < m)
            weight[s] += weight[k];
        else
            bone[m] = b, weight[m++] = weight[k];
    }
    return m;
}

/* b241 p4_skin_dummy: the word 0xFF000000 after each GE-skinned PRIM (PS2RC does this in its skinned sub-lists and
   calls it "the dummy word the GE's skinning needs"; PSPSDK's sceGuDrawArray does not). A lead for the strip hang,
   not an established cause: 0 by default. */
static void sk_free(void *list);
static void skin_free(SkinGeometry *e)
{
    sk_free(e->list), e->list = 0;
    for (int b = 0; b < e->batches; b++)
        free(e->batch[b].v), free(e->batch[b].src), free(e->batch[b].strip), free(e->batch[b].strip_lens);
    free(e->batch);
    free(e->radius);
    free(e->box);
    free(e->bone_map), free(e->bone_len);
    skin_ge_bytes -= e->bytes;
    memset(e, 0, sizeof *e);
}

/* one batch: the triangles in tri (corner indices), their own vertex numbering */
/* b209 p4_skin_strip: each GE batch's triangle list also as one triangle strip (greedy: from a triangle, on through the
   neighbour across the last edge while its winding fits the strip's alternation), strips joined by zero-area
   triangles (repeat the last index and the next first one, plus one more when the next must start on an even
   index). The GE then reads ~1 index a triangle, not 3 (b206: indices ~2.9x the vertices); whether it also
   transforms fewer vertices is the device's question. The strip is unrolled and compared with the list (same
   triangles, same winding) before it is kept; idx stays the list for the CPU paths and checks. In the cache key. */
int p4_skin_strip = 1; /* b243: on again. b227-b229 hung at the battle start (flicker, black scene); b241 put the strip address and count into the recorded CALL key (a rebuilt geometry replayed a record pointing at a freed strip) and b242 went through the boss battle with strips on. One pass: repeat before calling it closed. */
int p4_strip_why = 0, p4_strip_check = 0;
/* b250 p4_strip_pack 1: a batch whose index list is in p4strips.bin takes its strips from there instead of building
   one (tools/psp_strips.py: swaps, both ends, several runs: ~1.8 indices a triangle joined against ~2.33, and no
   build time when a party appears). The file is only a hint: sp_same compares what the strips draw with the batch's
   list (same corners, same winding, same multiplicity) and a batch that is not found or fails takes skin_stripify.
   p4_strip_prims 1: the strips of a found batch are not joined by zero-area triangles but drawn one PRIM each (the
   GE goes on reading indices where the PRIM before stopped): ~1.5 indices a triangle, one command word a strip.
   Both are in the skin cache key. Default 0 until measured on the device. */
int p4_strip_pack = 1, p4_strip_prims = 0;
#include "strippack.h"
#include "paths.h"
#define STRIP_PACK_FILE p4_path("p4strips.bin") /* b321: the launch folder */
enum { PACK_WINDOW = 16384 }; /* a batch's entry is ~2 bytes a triangle; a geometry's entries lie together in the file */
static SpEntry *pack_index;
static int pack_state; /* 0 not tried, 1 loaded, -1 no usable file */
static unsigned short *pack_window;
static unsigned pack_window_at;
static int pack_window_bytes;
static int pack_load(void)
{
    if (pack_state)
        return pack_state > 0;
    pack_state = -1;
    unsigned head[4] = { 0 };
    pack_fd = sceIoOpen(STRIP_PACK_FILE, PSP_O_RDONLY, 0);
    if (pack_fd < 0 || sceIoRead(pack_fd, head, sizeof head) != sizeof head || head[0] != SP_MAGIC || head[1] != SP_VERSION || !head[2] ||
        head[2] > 1u << 20 || head[3] != 16 + head[2] * sizeof(SpEntry)) {
        p4_log("strip pack: no usable %s (open %x magic %x version %u count %u)\n", STRIP_PACK_FILE, (unsigned)pack_fd, head[0], head[1], head[2]);
    } else {
        int bytes = head[2] * sizeof(SpEntry);
        pack_index = malloc(bytes);
        pack_window = memalign(64, PACK_WINDOW);
        if (pack_index && pack_window && sceIoRead(pack_fd, pack_index, bytes) == bytes) {
            sceIoClose(pack_fd), pack_fd = -1;
            pack_count = head[2];
            pack_state = 1;
            p4_log("strip pack: %d batches, index %d bytes\n", pack_count, bytes);
            return 1;
        }
        p4_log("strip pack: index of %d bytes not read\n", bytes);
        free(pack_index), free(pack_window), pack_index = 0, pack_window = 0;
    }
    if (pack_fd >= 0)
        sceIoClose(pack_fd);
    pack_fd = -1;
    return 0;
}
/* bytes of the file through one window; 0 when they cannot be had */
static const unsigned short *pack_bytes(unsigned offset, int bytes)
{
    if (bytes > PACK_WINDOW || (offset & 1))
        return 0;
    if (offset < pack_window_at || offset + bytes > pack_window_at + pack_window_bytes) {
        pack_window_bytes = 0;
        /* b263: the handle is held only for this read. Held between reads it was the one handle too many: from
           b252 the save check's sceIoOpen failed with 80020320 (too many open files) while audio streams were open. */
        if (pack_fd < 0 && (pack_fd = sceIoOpen(STRIP_PACK_FILE, PSP_O_RDONLY, 0)) < 0)
            return 0;
        int got = sceIoLseek32(pack_fd, offset, PSP_SEEK_SET) == (int)offset ? sceIoRead(pack_fd, pack_window, PACK_WINDOW) : -1;
        sceIoClose(pack_fd), pack_fd = -1;
        pack_window_at = offset;
        pack_window_bytes = got > 0 ? got : 0;
        if (pack_window_bytes < bytes)
            return 0;
    }
    return (const unsigned short *)((const char *)pack_window + (offset - pack_window_at));
}
/* the file's entry for a batch's index list (u16 strips, total, lengths, indices), or 0 */
static const unsigned short *pack_entry(const unsigned short *list, int corners)
{
    if (!pack_load())
        return 0;
    const SpEntry *e = sp_find(pack_index, pack_count, sp_key(list, corners), corners);
    const unsigned short *head = e ? pack_bytes(e->offset, 4) : 0;
    int words = head ? sp_entry_words(head) : 0;
    return words ? pack_bytes(e->offset, words * 2) : 0;
}
/* b211 meshkit (port/meshkit.h), one simplifier for both ways of reshaping a skinned geometry's triangles:
   p4_skin_simplify N (percent of triangles kept, 0 off): online, at skin cache build, many collapses a pass.
   p4_mesh_capture 1: every geometry built writes capture/<key>.bin (positions, dominant bone per vertex, each mesh's
   triangles) next to the EBOOT; tools/meshkit_offline.c simplifies those on the PC (one collapse a pass) into
   override/<key>.bin, triangle lists only. p4_mesh_override 1: a geometry with such a file takes its lists.
   Keys: a hash of the bind positions, vertex and mesh counts. Files hold game data: they stay on the user's card.
   Vertices are never made or moved (half-edge collapse), so UVs, normals and weights stay as they are. */
int p4_skin_simplify = 0, p4_mesh_override = 0, p4_mesh_capture = 0;
typedef struct { unsigned char *data; int size, cap; } MkBuf;
static void mk_put(MkBuf *b, const void *p, int n)
{
    if (!b->data && b->cap < 0)
        return;
    if (b->size + n > b->cap) {
        int cap = (b->size + n) * 2 + 256;
        unsigned char *grown = realloc(b->data, cap);
        if (!grown) {
            free(b->data), b->data = 0, b->cap = -1;
            return;
        }
        b->data = grown, b->cap = cap;
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
}
static void mk_write_file(const char *dir, unsigned key, const MkBuf *b)
{
    char path[48];
    if (!b->data)
        return;
    sceIoMkdir(dir, 0777);
    snprintf(path, sizeof path, "%s/%08x.bin", dir, key);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0)
        return;
    sceIoWrite(fd, b->data, b->size);
    sceIoClose(fd);
}
/* override/<key>.bin: 'P4MO' 1 key meshes, then per mesh ntri and ntri x 3 u16; 0 if missing or not this geometry */
static unsigned char *mk_read_override(unsigned key, int meshes, int *size_out)
{
    char path[48];
    snprintf(path, sizeof path, "override/%08x.bin", key);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    unsigned char *d = size >= 16 ? malloc(size) : 0;
    if (d && sceIoRead(fd, d, size) != size)
        free(d), d = 0;
    sceIoClose(fd);
    unsigned *h = (unsigned *)d;
    if (d && (h[0] != 0x4f4d3450u || h[1] != 1 || h[2] != key || (int)h[3] != meshes))
        free(d), d = 0;
    *size_out = d ? size : 0;
    return d;
}
/* the bone set of one triangle (set[0] the count), as the batcher wants it */
static void mk_tri_bones(const Skin *skin, int nbones, const int *c, unsigned char *set);
typedef struct { unsigned key; int tri; } StripEdge;
static int strip_edge_cmp(const void *a, const void *b)
{
    unsigned x = ((const StripEdge *)a)->key, y = ((const StripEdge *)b)->key;
    return x < y ? -1 : x > y;
}
static unsigned strip_key(int a, int b) { return a < b ? (unsigned)a << 16 | b : (unsigned)b << 16 | a; }
/* a triangle by its corners, rotated so the smallest comes first (keeps the winding) */
static unsigned long long strip_canon(int a, int b, int c)
{
    if (b < a && b < c) { int t = a; a = b; b = c; c = t; }
    else if (c < a && c < b) { int t = c; c = b; b = a; a = t; }
    return (unsigned long long)a << 32 | (unsigned long long)b << 16 | (unsigned)c;
}
static int strip_ull_cmp(const void *a, const void *b)
{
    unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
    return x < y ? -1 : x > y;
}
typedef struct { int *t, n, cap; } StripHeap; /* b213: min-heap of triangle numbers */
static int strip_heap_push(StripHeap *h, int t)
{
    if (h->n == h->cap) {
        int cap = h->cap * 2 + 64, *grown = realloc(h->t, cap * sizeof *grown);
        if (!grown)
            return 0;
        h->t = grown, h->cap = cap;
    }
    int i = h->n++;
    for (; i && h->t[(i - 1) / 2] > t; i = (i - 1) / 2)
        h->t[i] = h->t[(i - 1) / 2];
    h->t[i] = t;
    return 1;
}
static void strip_heap_pop(StripHeap *h)
{
    int last = h->t[--h->n], i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n)
            break;
        if (c + 1 < h->n && h->t[c + 1] < h->t[c])
            c++;
        if (h->t[c] >= last)
            break;
        h->t[i] = h->t[c], i = c;
    }
    if (h->n)
        h->t[i] = last;
}
/* b213: triangle t is used: each unused neighbour has one unused neighbour fewer per shared edge */
static int strip_take(int t, const StripEdge *edges, const int *range, const unsigned char *used, int *count, StripHeap *heap)
{
    for (int k = 0; k < 3; k++)
        for (int e = range[t * 6 + k * 2]; e < range[t * 6 + k * 2 + 1]; e++) {
            int u = edges[e].tri;
            if (u == t || used[u])
                continue;
            if (--count[u] < 4 && !strip_heap_push(&heap[count[u]], u))
                return 0;
        }
    return 1;
}
/* out: strip indices (room for 6 n + 6: b229, a strip of one triangle and its join take 6; 5 n + 4 overran the heap on
   batches of lone triangles and crashed the PSP in free() as a battle loaded); returns their count, or 0 when it does not check out. slow: b209's linear
   start search (the reference for p4_strip_check); else (b213) per-triangle counts of unused neighbours in buckets
   0..3 give the same choice (fewest, then the lowest triangle number) without rescanning every triangle per strip:
   b212 on the PSP, 10 characters' strips 6.7-7.9 s. */
static int skin_stripify(const unsigned short *list, int corners, unsigned short *out, int slow)
{
    int n = corners / 3, m = 0, len = 0;
    StripEdge *edges = malloc(n * 3 * sizeof *edges);
    unsigned char *used = calloc(n ? n : 1, 1);
    unsigned long long *want = malloc(n * sizeof *want), *got = malloc(n * sizeof *got);
    if (!edges || !used || !want || !got) {
        free(edges), free(used), free(want), free(got);
        return 0;
    }
    for (int t = 0; t < n; t++) {
        const unsigned short *c = list + t * 3;
        if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]) { /* draws nothing: left out */
            used[t] = 1;
            continue;
        }
        want[m++] = strip_canon(c[0], c[1], c[2]);
        for (int k = 0; k < 3; k++)
            edges[(t * 3 + k)] = (StripEdge){ strip_key(c[k], c[(k + 1) % 3]), t };
    }
    int ne = 0;
    for (int t = 0; t < n; t++)
        if (!used[t])
            for (int k = 0; k < 3; k++)
                edges[ne++] = edges[t * 3 + k];
    qsort(edges, ne, sizeof *edges, strip_edge_cmp);
    /* b213: each triangle's edges as ranges of the sorted edges, its unused-neighbour count, buckets by count */
    int *range = slow ? 0 : malloc((n ? n : 1) * 6 * sizeof *range), *count = slow ? 0 : malloc((n ? n : 1) * sizeof *count);
    StripHeap heap[4] = { { 0 } };
    if (!slow && (!range || !count))
        slow = 1;
    for (int t = 0; t < n && !slow; t++) {
        count[t] = 0;
        if (used[t])
            continue;
        for (int k = 0; k < 3; k++) {
            const unsigned short *c = list + t * 3;
            StripEdge key = { strip_key(c[k], c[(k + 1) % 3]), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            range[t * 6 + k * 2] = hit ? hit - edges : 0;
            for (; hit && hit < edges + ne && hit->key == key.key; hit++)
                count[t] += hit->tri != t;
            range[t * 6 + k * 2 + 1] = hit ? hit - edges : 0;
        }
        if (count[t] < 4 && !strip_heap_push(&heap[count[t]], t))
            slow = 1;
    }
    for (;;) {
        /* start where the fewest unused neighbours are (an edge of what is left), so strips do not cut it in two */
        int start = -1, fewest = 4;
        for (int b = 0; !slow && b < 4 && start < 0; b++) { /* stale entries (used, or moved to a lower bucket) go */
            while (heap[b].n && (used[heap[b].t[0]] || count[heap[b].t[0]] != b))
                strip_heap_pop(&heap[b]);
            if (heap[b].n)
                start = heap[b].t[0];
        }
        for (int t = 0; slow && t < n && fewest > 0; t++) {
            if (used[t])
                continue;
            int free_n = 0;
            for (int k = 0; k < 3; k++) {
                const unsigned short *c = list + t * 3;
                StripEdge key = { strip_key(c[k], c[(k + 1) % 3]), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
                while (hit && hit > edges && hit[-1].key == key.key)
                    hit--;
                for (; hit && hit < edges + ne && hit->key == key.key; hit++)
                    free_n += hit->tri != t && !used[hit->tri];
            }
            if (free_n < fewest)
                fewest = free_n, start = t;
        }
        if (start < 0)
            break;
        const unsigned short *c = list + start * 3;
        int v[3] = { c[0], c[1], c[2] }, best = 0;
        for (int r = 0; r < 3 && !best; r++) { /* a rotation whose last edge leads on */
            int p = c[(r + 1) % 3], q = c[(r + 2) % 3];
            StripEdge key = { strip_key(p, q), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            for (; hit && hit < edges + ne && hit->key == key.key && !best; hit++)
                if (hit->tri != start && !used[hit->tri])
                    best = 1, v[0] = c[r], v[1] = p, v[2] = q;
        }
        used[start] = 1;
        if (!slow && !strip_take(start, edges, range, used, count, heap))
            break; /* out of memory: the check below fails and the batch stays a list */
        if (len) { /* join: zero-area triangles, the next strip starting on an even index */
            out[len] = out[len - 1], len++;
            out[len++] = v[0];
            if (len & 1)
                out[len++] = v[0];
        }
        int first = len;
        out[len++] = v[0], out[len++] = v[1], out[len++] = v[2];
        for (;;) {
            int p = out[len - 2], q = out[len - 1], odd = (len - 2 - first) & 1, next = -1, r = 0;
            StripEdge key = { strip_key(p, q), 0 }, *hit = bsearch(&key, edges, ne, sizeof *edges, strip_edge_cmp);
            while (hit && hit > edges && hit[-1].key == key.key)
                hit--;
            for (; hit && hit < edges + ne && hit->key == key.key && next < 0; hit++) {
                if (used[hit->tri])
                    continue;
                const unsigned short *u = list + hit->tri * 3;
                int third = u[0] != p && u[0] != q ? u[0] : u[1] != p && u[1] != q ? u[1] : u[2];
                /* triangle number len - 2 - first of the strip: even (p, q, r), odd (q, p, r) */
                unsigned long long need = odd ? strip_canon(q, p, third) : strip_canon(p, q, third);
                if (need == strip_canon(u[0], u[1], u[2]))
                    next = hit->tri, r = third;
            }
            if (next < 0)
                break;
            used[next] = 1;
            out[len++] = r;
            if (!slow && !strip_take(next, edges, range, used, count, heap))
                break;
        }
    }
    /* unroll and compare */
    int k = 0, ok = 1;
    for (int i = 0; i + 2 < len && ok; i++) {
        int a = out[i], b = out[i + 1], c = out[i + 2];
        if (a == b || b == c || a == c)
            continue;
        if (k >= m)
            ok = 0;
        else
            got[k++] = i & 1 ? strip_canon(b, a, c) : strip_canon(a, b, c);
    }
    if (ok && k == m) {
        qsort(want, m, sizeof *want, strip_ull_cmp);
        qsort(got, k, sizeof *got, strip_ull_cmp);
        ok = !memcmp(want, got, m * sizeof *want);
    } else
        ok = 0;
    free(edges), free(used), free(want), free(got), free(range), free(count);
    for (int b = 0; b < 4; b++)
        free(heap[b].t);
    if (!ok)
        strip_failed++;
    return ok ? len : 0;
}

static const V3 *weld_verts;
static int weld_cmp(const void *a, const void *b) /* by position bits, then index (stable: the first is the lowest) */
{
    int i = *(const int *)a, j = *(const int *)b, c = memcmp(&weld_verts[i], &weld_verts[j], sizeof *weld_verts);
    return c ? c : i - j;
}

static int skin_batch(SkinGeometry *e, int mesh, const unsigned char *palette, int bones, const int *tri, int corners,
                      const Geometry *g, const Skin *skin, int nbones)
{
    unsigned stamp = ++mesh_generation;
    int used = 0;
    for (int k = 0; k < corners; k++)
        if (mesh_stamp[tri[k]] != stamp) {
            mesh_stamp[tri[k]] = stamp;
            mesh_remap[tri[k]] = used++;
        }
    SkinLayout l = skin_layout(bones, e->nrm, e->packed);
    int stride = l.stride, bytes = stride * used + corners * 2;
    unsigned char *v = memalign(16, bytes);
    SkinBatch *grown = realloc(e->batch, (e->batches + 1) * sizeof *grown);
    if (!v || !grown) {
        free(v);
        if (grown)
            e->batch = grown;
        return 0;
    }
    e->batch = grown;
    SkinBatch *b = &e->batch[e->batches++];
    memset(b, 0, sizeof *b);
    b->mesh = mesh;
    b->bones = bones;
    memcpy(b->palette, palette, bones);
    b->vtype = GU_WEIGHTS(bones) | GU_WEIGHT_8BIT | (e->packed ? GU_TEXTURE_16BIT | GU_VERTEX_16BIT : GU_TEXTURE_32BITF | GU_VERTEX_32BITF) | GU_COLOR_8888 |
               (e->nrm ? GU_NORMAL_8BIT : 0) | GU_INDEX_16BIT | GU_TRANSFORM_3D;
    b->v = v;
    b->idx = (unsigned short *)(v + stride * used);
    b->src = malloc(used * 2); /* b178: the geometry vertex of each batch vertex, for p4_skin_diff */
    b->indices = corners;
    b->vertices = used;
    const V3 *verts = g->morphTarget->verts, *normals = g->morphTarget->normals;
    float (*uv)[2] = g->numTexCoordSets ? g->texCoords[0] : 0;
    float lo[2] = { 0, 0 }, range[2] = { 0, 0 }; /* packed: the batch's UV box */
    if (e->packed && uv) {
        float hi[2] = { uv[tri[0]][0], uv[tri[0]][1] };
        lo[0] = hi[0], lo[1] = hi[1];
        for (int k = 1; k < corners; k++)
            for (int c = 0; c < 2; c++) {
                float x = uv[tri[k]][c];
                if (x < lo[c]) lo[c] = x;
                if (x > hi[c]) hi[c] = x;
            }
        for (int c = 0; c < 2; c++) {
            range[c] = hi[c] - lo[c];
            if (!(range[c] < 1e6f) || !(lo[c] > -1e6f)) /* not finite: every vertex at 0 */
                lo[c] = range[c] = 0;
            b->uvs[c] = range[c] * (32768.0f / 65535);
            b->uvs[2 + c] = lo[c] - floorf(lo[c]);
        }
    }
    b->alpha[0] = 255, b->alpha[1] = 0; /* the vertex colours' alpha; the material's comes on top when drawn */
    stamp = ++mesh_generation;
    for (int k = 0; k < corners; k++) {
        int j = tri[k];
        b->idx[k] = mesh_remap[j];
        if (mesh_stamp[j] == stamp)
            continue;
        mesh_stamp[j] = stamp;
        if (b->src)
            b->src[mesh_remap[j]] = j;
        unsigned char *out = v + stride * mesh_remap[j];
        memset(out, 0, stride);
        unsigned char bone[4];
        float weight[4], slot[SKIN_PALETTE] = { 0 };
        int n = vertex_bones(skin, nbones, j, bone, weight);
        for (int w = 0; w < n; w++)
            for (int s = 0; s < bones; s++)
                if (palette[s] == bone[w])
                    slot[s] += weight[w];
        int sum = 0, largest = 0;
        for (int s = 0; s < bones; s++) {
            int q = (int)(slot[s] * 128 + 0.5f);
            out[s] = q > 128 ? 128 : q;
            sum += out[s];
            if (slot[s] > slot[largest])
                largest = s;
        }
        out[largest] += 128 - sum; /* exactly 128: the GE sums weight x bone as given */
        for (int s = 0; s < bones; s++)
            b->wreal += out[s] != 0;
        if (e->packed) {
            unsigned short t[2];
            for (int c = 0; c < 2; c++)
                t[c] = uv && range[c] > 0 ? clamp_round((uv[j][c] - lo[c]) / range[c] * 65535, 0, 65535) : 0;
            memcpy(out + l.uv, t, 4);
            float k = 32768 / e->pack[3];
            short q[3] = { clamp_round((verts[j].x - e->pack[0]) * k, -32768, 32767), clamp_round((verts[j].y - e->pack[1]) * k, -32768, 32767),
                           clamp_round((verts[j].z - e->pack[2]) * k, -32768, 32767) };
            memcpy(out + l.position, q, 6);
        } else {
            float f[2] = { uv ? uv[j][0] : 0, uv ? uv[j][1] : 0 };
            memcpy(out + l.uv, f, 8);
            memcpy(out + l.position, &verts[j], 12);
        }
        unsigned base = g->preLit ? double_colour(g->preLit[j]) : e->lit ? 0xff000000 : 0xffffffff;
        memcpy(out + l.colour, &base, 4);
        if ((int)(base >> 24) < b->alpha[0]) b->alpha[0] = base >> 24;
        if ((int)(base >> 24) > b->alpha[1]) b->alpha[1] = base >> 24;
        if (e->nrm && normals) {
            const V3 *nv = &normals[j];
            float len = sqrtf(nv->x * nv->x + nv->y * nv->y + nv->z * nv->z), inv = len > 0 ? 1.0f / len : 0;
            out[l.normal] = (unsigned char)normal_s8(nv->x * inv);
            out[l.normal + 1] = (unsigned char)normal_s8(nv->y * inv);
            out[l.normal + 2] = (unsigned char)normal_s8(nv->z * inv);
        }
    }
    for (int t = 0; t + 2 < corners; t += 3) { /* b206: rigid triangles, every corner on one and the same bone */
        int slot = -1, rigid = 1;
        for (int c = 0; c < 3 && rigid; c++) {
            const unsigned char *w = v + stride * b->idx[t + c];
            int only = -1;
            for (int k = 0; k < bones; k++)
                if (w[k])
                    only = only < 0 ? k : -2;
            rigid = only >= 0 && (slot < 0 || only == slot);
            slot = only;
        }
        b->rigid += rigid;
    }
    if (p4_skin_strip && corners >= 6) {
        int cap = 6 * (corners / 3) + 6, n = 0, ns = 0;
        unsigned short *strip = malloc(cap * sizeof *strip), *lens = 0;
        unsigned t0 = sceKernelGetSystemTimeLow();
        if (strip && p4_strip_pack) { /* b250: the PC's strips for this very list, if the file has them and they check out */
            const unsigned short *entry = pack_entry(b->idx, corners);
            if (entry) {
                if (p4_strip_prims && entry[0] > 1) {
                    lens = malloc(entry[0] * sizeof *lens);
                    ns = lens ? sp_split(entry, strip, cap, lens, entry[0]) : 0;
                    n = ns ? entry[1] : 0;
                } else
                    n = sp_join(entry, strip, cap);
                if (n && n < corners && sp_same(b->idx, corners, strip, n, ns ? lens : 0, ns, used))
                    pack_hits++, pack_prims += ns ? ns : 1;
                else {
                    pack_refused++;
                    free(lens);
                    lens = 0, n = ns = 0;
                }
            } else
                pack_misses++;
            pack_us += sceKernelGetSystemTimeLow() - t0;
        }
        int packed_strip = n != 0;
        if (!packed_strip)
            n = strip ? skin_stripify(b->idx, corners, strip, 0) : 0;
        strip_us += sceKernelGetSystemTimeLow() - t0;
        static unsigned overlong_logged;
        if (n > 5 * (corners / 3) + 4 && overlong_logged++ < 4) /* b229: what overran the old 5 n + 4 buffer */
            p4_log("TRACE skin strip longer than 5n+4: %d indices for %d triangles (mesh %d)\n", n, corners / 3, mesh);
        if (n && p4_strip_check && !packed_strip) { /* b213: the linear search must give the very same strip */
            unsigned short *ref = malloc((6 * (corners / 3) + 6) * sizeof *ref);
            if (ref) {
                int rn = skin_stripify(b->idx, corners, ref, 1);
                strip_checks++;
                strip_check_diffs += rn != n || memcmp(ref, strip, n * sizeof *ref);
                free(ref);
            }
        }
        if (n && n < corners) {
            b->strip = strip, b->strip_count = n;
            b->strip_lens = lens, b->strip_n = ns;
            sceKernelDcacheWritebackRange(b->strip, n * sizeof *b->strip);
            strip_batches++, strip_list_idx += corners, strip_idx += n;
            bytes += n * sizeof *b->strip;
        } else
            free(strip), free(lens);
    }
    sceKernelDcacheWritebackRange(v, bytes - b->strip_count * (int)sizeof *b->strip);
    e->bytes += bytes;
    skin_ge_bytes += bytes;
    return 1;
}

/* b199 PSP-shaped skin batches. p4_skin_sticky: a batch puts a bone the GE already holds (from the batch before)
   back in the same slot, so draw_skinned_ge does not send its matrix again. p4_skin_regroup: an opaque mesh's
   triangles are batched first-fit by bone set (b193 counts: battle body 152 -> 99 batches, dialogue 228 -> 157);
   a mesh that may blend keeps the PS2 triangle order. 1: no texture or a texture with no translucent texel, opaque
   vertex and material alpha when built; 2: every mesh (to compare). Both act when a geometry's cache is built. */
int p4_skin_sticky = 1, p4_skin_regroup = 1;
static unsigned skin_regrouped;
static int raster_opaque(const Raster *r)
{
    if (!r || !r->originalPixels)
        return 1;
    const SwizzleHead *h = swizzle_head(r->originalPixels);
    if (h)
        return h->alpha_min >= 255;
    unsigned char used[32];
    unsigned max;
    raster_used_entries(r, used);
    return raster_alpha_min(r, used, &max) >= 255;
}
static int skin_mesh_regroup(const Mesh *mesh, const Geometry *g, const int *corner, int ntri)
{
    if (p4_skin_regroup >= 2)
        return 1;
    if (!p4_skin_regroup || ntri < 2 || mesh->material->colour >> 24 != 255)
        return 0;
    void **texture = mesh->material->texture;
    if (!raster_opaque(texture ? (const Raster *)*texture : 0))
        return 0;
    for (int k = 0; k < ntri * 3 && g->preLit; k++)
        if (double_colour(g->preLit[corner[k]]) >> 24 != 255)
            return 0;
    return 1;
}

/* the cached GE form of a skinned geometry, built on first use; 0: it cannot go to the GE */
/* b193 p4_batch_stats: what other batchings would give, counted once per built geometry (regroup[]) and summed per
   draw and pass (body / shadow) in p4_batch_stats_report. First-fit: a batch takes any later triangle of its group
   whose bones still fit, not only the next one. Groups: each mesh; meshes sharing a texture (the shadow pass draws
   one colour); all meshes. Alpha order and blend/depth state are not told apart, so these are lower bounds. */
int p4_batch_stats = 0; /* b260: 0 by default (a diagnostic; the skin lists are off while it counts) */
enum { BS_DRAWS, BS_BATCHES, BS_MESHES, BS_FF_MESH, BS_FF_TEX, BS_FF_ALL, BS_PAL, BS_PAL_MESH, BS_PAL_TEX, BS_PAL_ALL, BS_SENT, BS_N };
static unsigned *batch_stats; /* [2][BS_N], heap: .bss is full */

static void first_fit(const unsigned char (*set)[13], const int *group, int n, unsigned char *done, unsigned short *batches, unsigned short *palettes)
{
    memset(done, 0, n);
    *batches = *palettes = 0;
    for (int start = 0; start < n; start++) {
        if (done[start])
            continue;
        unsigned char palette[SKIN_PALETTE];
        int np = 0;
        for (int t = start; t < n; t++) {
            if (done[t] || group[t] != group[start])
                continue;
            unsigned char extra[12];
            int add = 0;
            for (int k = 1; k <= set[t][0]; k++) {
                int have = 0;
                for (int s = 0; s < np && !have; s++)
                    have = palette[s] == set[t][k];
                if (!have)
                    extra[add++] = set[t][k];
            }
            if (np + add > SKIN_PALETTE)
                continue;
            memcpy(palette + np, extra, add);
            np += add;
            done[t] = 1;
        }
        ++*batches;
        *palettes += np;
    }
}

static void batch_regroup(SkinGeometry *e, const Geometry *g, const Skin *skin, int nbones)
{
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, n = 0, meshes = 0;
    for (int m = 0; m < header->numMeshes; m++)
        n += strip ? (mesh[m].numIndices > 2 ? mesh[m].numIndices - 2 : 0) : mesh[m].numIndices / 3;
    unsigned char (*set)[13] = malloc((n ? n : 1) * sizeof *set), *done = malloc(n ? n : 1);
    int *group = malloc(3 * (n ? n : 1) * sizeof *group);
    if (!set || !done || !group)
        goto out;
    n = 0;
    for (int m = 0; m < header->numMeshes; m++) {
        const unsigned short *idx = mesh[m].indices;
        int triangles = strip ? mesh[m].numIndices - 2 : mesh[m].numIndices / 3, used = 0;
        for (int t = 0; t < triangles; t++) {
            int c[3] = { strip ? idx[t] : idx[t * 3], strip ? idx[t + 1] : idx[t * 3 + 1], strip ? idx[t + 2] : idx[t * 3 + 2] };
            if (strip && (c[0] == c[1] || c[1] == c[2] || c[0] == c[2]))
                continue;
            set[n][0] = 0;
            for (int k = 0; k < 3; k++) {
                unsigned char bone[4];
                float weight[4];
                int nb = vertex_bones(skin, nbones, c[k], bone, weight);
                for (int w = 0; w < nb; w++) {
                    int have = 0;
                    for (int s = 1; s <= set[n][0] && !have; s++)
                        have = set[n][s] == bone[w];
                    if (!have)
                        set[n][++set[n][0]] = bone[w];
                }
            }
            group[n++] = m;
            used = 1;
        }
        meshes += used;
    }
    /* group ids per variant: mesh (group), first mesh with the same texture (by_tex), one group (all) */
    int *by_tex = group + n, *all = group + 2 * n;
    for (int t = 0; t < n; t++) {
        int gm = group[t], tex = gm;
        for (int o = 0; o < gm; o++)
            if (mesh[o].material->texture == mesh[gm].material->texture) {
                tex = o;
                break;
            }
        by_tex[t] = tex;
        all[t] = 0;
    }
    e->regroup[0] = meshes;
    first_fit(set, group, n, done, &e->regroup[1], &e->regroup[5]);
    first_fit(set, by_tex, n, done, &e->regroup[2], &e->regroup[6]);
    first_fit(set, all, n, done, &e->regroup[3], &e->regroup[7]);
    e->regroup[4] = 0;
    for (int b = 0; b < e->batches; b++)
        e->regroup[4] += e->batch[b].bones;
out:
    free(set), free(done), free(group);
}

static void batch_stats_add(const SkinGeometry *e, int shadow, unsigned sent)
{
    if (!batch_stats && !(batch_stats = calloc(2 * BS_N, sizeof *batch_stats)))
        return;
    unsigned *s = batch_stats + (shadow ? BS_N : 0);
    s[BS_DRAWS]++;
    s[BS_BATCHES] += e->batches;
    s[BS_MESHES] += e->regroup[0];
    s[BS_FF_MESH] += e->regroup[1];
    s[BS_FF_TEX] += e->regroup[2];
    s[BS_FF_ALL] += e->regroup[3];
    s[BS_PAL] += e->regroup[4];
    s[BS_PAL_MESH] += e->regroup[5];
    s[BS_PAL_TEX] += e->regroup[6];
    s[BS_PAL_ALL] += e->regroup[7];
    s[BS_SENT] += sent;
}

void p4_batch_stats_report(void)
{
    p4_log("skin batches sticky %d regroup %d regrouped batches built %u\n", p4_skin_sticky, p4_skin_regroup, skin_regrouped);
    skin_regrouped = 0;
    if (!p4_batch_stats || !batch_stats)
        return;
    for (int p = 0; p < 2; p++) {
        unsigned *s = batch_stats + p * BS_N;
        p4_log("batch stats %s per 256 frames: draws %u batches %u meshes %u first-fit mesh %u texture %u all %u\n", p ? "shadow" : "body",
               s[BS_DRAWS], s[BS_BATCHES], s[BS_MESHES], s[BS_FF_MESH], s[BS_FF_TEX], s[BS_FF_ALL]);
        p4_log("batch stats %s palette bones now %u mesh %u texture %u all %u; bones sent %u\n", p ? "shadow" : "body", s[BS_PAL],
               s[BS_PAL_MESH], s[BS_PAL_TEX], s[BS_PAL_ALL], s[BS_SENT]); /* one line ran past p4_log's buffer */
    }
    memset(batch_stats, 0, 2 * BS_N * sizeof *batch_stats);
}

/* b245 p4_skin_lod_depth D (clip w, 0 off), p4_skin_lod_keep K (percent of triangles): a skinned character whose root
   was farther than D in the last frame is drawn from a second cache entry simplified to K% (meshkit, as
   p4_skin_simplify, built once when it first goes far: 0.2-0.5 s for a party). It comes back to the full mesh below
   0.9 D, so it does not flip at the edge. The user (2026-11-14): far characters and scenery may lose detail the PSP
   screen does not show. In the dialogue the party stands at 650-1300; b211 found 60% visibly damaged up close. */
static int skin_simplify_now; /* the percent this skin_geometry call builds and keys with */
static int skin_bones_now; /* b254: 0, or the weighted bones a far entry keeps (bone_lod.h) */
static const Hierarchy *skin_hierarchy_now; /* its parents, for the far entry */
#include "bone_lod.h"
/* the far entry's bone map: weighted bones collapsed to skin_bones_now, 0 when that is not possible */
static unsigned char *skin_far_map(const Geometry *g, const Skin *skin, int nbones)
{
    const unsigned *h = (const unsigned *)skin_hierarchy_now;
    const char *info = h ? (const char *)h[4] : 0;
    if (!info || (int)h[1] < nbones || nbones > BL_MAX_BONES || g->numVertices <= 0)
        return 0;
    short parent[BL_MAX_BONES];
    unsigned char *map = malloc(BL_MAX_BONES), *bone = malloc(g->numVertices * 4);
    float *weight = malloc(g->numVertices * 16);
    int kept = -1;
    if (map && bone && weight && bl_parents(nbones, (const unsigned *)(info + 8), 4, parent)) {
        int before = 0;
        unsigned char seen[BL_MAX_BONES] = { 0 };
        for (int j = 0; j < g->numVertices; j++) {
            unsigned char b[4];
            float w[4];
            int n = vertex_bones_raw(skin, nbones, j, b, w);
            for (int k = 0; k < 4; k++) {
                bone[j * 4 + k] = k < n ? b[k] : 0;
                weight[j * 4 + k] = k < n ? w[k] : 0;
                if (k < n && !seen[b[k]])
                    seen[b[k]] = 1, before++;
            }
        }
        kept = bl_collapse(nbones, parent, &skin->skinToBone[0].right.x, g->numVertices, (const float (*)[3])g->morphTarget->verts,
                           bone, weight, skin_bones_now, map);
        if (kept > 0)
            skin_far_bones_kept += kept, skin_far_bones_in += before;
    }
    free(bone), free(weight);
    if (kept <= 0)
        free(map), map = 0;
    return map;
}
static SkinGeometry *skin_geometry(const Geometry *g, const Skin *skin, int nbones, int lit, int nrm)
{
    if (!skin_cache && !(skin_cache = calloc(SKIN_GEOMETRIES, sizeof *skin_cache)))
        return 0;
    const V3 *verts = g->morphTarget->verts;
    unsigned sample = hash_sample(2166136261u, verts, g->numVertices, sizeof *verts);
    sample ^= (unsigned)(p4_skin_sticky | p4_skin_regroup << 1 | (p4_skin_pack != 0) << 3 | (unsigned)p4_skin_palette << 12 | (unsigned)p4_skin_strip << 16 | (unsigned)skin_simplify_now << 18 | (p4_mesh_override != 0) << 25 | (p4_strip_pack != 0) << 26 | (p4_strip_prims != 0) << 27) * 0x9e3779b1u; /* b199: a switch poke builds the new shape */
    sample ^= skin_simplify_now ? (unsigned)p4_skin_lod_min * 0xc2b2ae35u : 0;
    sample ^= (unsigned)skin_bones_now * 0x85ebca6bu; /* b254: the far entry is another shape */
    SkinGeometry *spare = 0;
    for (int i = 0; i < SKIN_GEOMETRIES; i++) {
        SkinGeometry *e = &skin_cache[i];
        if (e->g == g && e->verts == verts && e->vertices == g->numVertices && e->lit == lit && e->nrm == nrm && e->sample == sample) {
            e->frame = cache_frame;
            return e->batches ? e : 0;
        }
        if (e->frame != cache_frame && (!spare || !e->g || (spare->g && e->frame < spare->frame)))
            spare = e; /* empty, or the longest unused; never one the GE still reads this frame */
    }
    if (!spare || g->numVertices > MAX_GUARDED || !mesh_scratch(g->numVertices))
        return 0;
    /* b241: with the pipeline the GE may still be drawing the previous frame, which read this entry's buffers */
    if (p4_skin_free_wait && spare->g && cache_frame - spare->frame <= 1) {
        pipe_texture_wait();
        skin_free_waits++;
    }
    skin_free(spare);
    SkinGeometry *e = spare;
    e->g = g;
    e->verts = verts;
    e->vertices = g->numVertices;
    e->lit = lit;
    e->nrm = nrm;
    e->sample = sample;
    e->frame = cache_frame;
    skin_ge_builds++;
    unsigned build_t0 = sceKernelGetSystemTimeLow(); /* b212: what a build costs (a hitch when a character appears) */
    unsigned char *far_map = skin_bones_now > 0 ? skin_far_map(g, skin, nbones) : 0;
    vertex_bone_map = far_map; /* every vertex_bones below sees the kept bones only */
    skin_far_builds += far_map != 0;
    if (p4_skin_pack && g->numVertices > 0) { /* the bind-pose box for s16 positions; not finite: floats */
        float lo[3], hi[3];
        memcpy(lo, &verts[0], 12), memcpy(hi, &verts[0], 12);
        for (int j = 1; j < g->numVertices; j++) {
            const float *p = &verts[j].x;
            for (int c = 0; c < 3; c++) {
                if (p[c] < lo[c]) lo[c] = p[c];
                if (p[c] > hi[c]) hi[c] = p[c];
            }
        }
        float half = 0;
        for (int c = 0; c < 3; c++) {
            e->pack[c] = (lo[c] + hi[c]) * 0.5f;
            if ((hi[c] - lo[c]) * 0.5f > half)
                half = (hi[c] - lo[c]) * 0.5f;
        }
        e->pack[3] = half > 0 ? half : 1;
        e->packed = half < 1e6f && lo[0] > -1e6f && lo[1] > -1e6f && lo[2] > -1e6f;
    }
    /* b151: vertices whose bones fall outside the hierarchy (index >= nbones) lose those weights in vertex_bones; with
       none left a GE batch gives the whole weight to palette slot 0 and the vertex flies to that bone (a battle: a long
       line from one party member, in some poses only). Logged once per geometry. */
    {
        int lost = 0, partly = 0, top = 0;
        for (int j = 0; j < g->numVertices; j++) {
            int k_max = skin->maxWeights < 4 ? skin->maxWeights : 4, dropped = 0, kept = 0;
            for (int k = 0; k < k_max; k++) {
                int b = skin->indices[j][k];
                float w = skin->maxWeights == 1 ? 1 : skin->weights[j][k];
                if (w <= 0)
                    continue;
                if (b > top) top = b;
                if (b < nbones) kept++; else dropped++;
            }
            lost += dropped && !kept;
            partly += dropped && kept;
        }
        if (lost || partly || skin->numBones != hierarchy_nodes_seen)
            p4_log("TRACE skin geometry %x v%d bones %d ids %d nodes %d used %d maxw %d top index %d: vertices lost %d partly %d\n", (unsigned)g,
                   g->numVertices, skin->numBones, skin->numBoneIds, hierarchy_nodes_seen, nbones, skin->maxWeights, top, lost, partly);
    }
    /* how far from its joint each bone carries a vertex (bone space): the per-frame guard test */
    if ((e->radius = calloc(nbones, sizeof *e->radius)))
        e->nradius = nbones;
    /* b385: and the box (bone space) round them, for p4_skin_guard_box: min xyz, max xyz; empty = min > max */
    if ((e->box = malloc(nbones * 6 * sizeof *e->box)))
        for (int i = 0; i < nbones; i++)
            e->box[i * 6] = e->box[i * 6 + 1] = e->box[i * 6 + 2] = INFINITY, e->box[i * 6 + 3] = e->box[i * 6 + 4] = e->box[i * 6 + 5] = -INFINITY;
    for (int j = 0; j < g->numVertices && e->radius; j++) {
        unsigned char bone[4];
        float weight[4];
        int n = vertex_bones(skin, nbones, j, bone, weight);
        for (int w = 0; w < n; w++) {
            e->used[bone[w] >> 5] |= 1u << (bone[w] & 31); /* b208 bone census */
            V3 local = transform(&verts[j], &skin->skinToBone[bone[w]]);
            float d = sqrtf(local.x * local.x + local.y * local.y + local.z * local.z);
            if (!(d <= 1e30f))
                d = INFINITY; /* b180: NaN was skipped by the compare below and left radius 0, which skips the guard test */
            if (d > e->radius[bone[w]])
                e->radius[bone[w]] = d;
            if (e->box) {
                float *bx = e->box + bone[w] * 6, c[3] = { local.x, local.y, local.z };
                for (int k = 0; k < 3; k++) {
                    if (!(c[k] >= bx[k])) bx[k] = c[k] == c[k] ? c[k] : -INFINITY;
                    if (!(c[k] <= bx[k + 3])) bx[k + 3] = c[k] == c[k] ? c[k] : INFINITY;
                }
            }
        }
    }
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int strip = (header->flags & MESH_PRIM_MASK) == MESH_TRISTRIP, ok = 1;
    /* b199: the GE bone slots as the batches built so far leave them (draw_skinned_ge keeps a slot whose bone is
       already there); a new batch puts a bone it shares with them back in the same slot (p4_skin_sticky) */
    unsigned char held[SKIN_PALETTE];
    memset(held, 0xff, sizeof held);
    int *weld = 0; /* b210: each vertex -> the first vertex at the same position */
    if (p4_skin_strip && p4_strip_why && g->numVertices > 0 && g->numVertices <= 65535 && (weld = malloc(g->numVertices * sizeof *weld))) {
        int *order = malloc(g->numVertices * sizeof *order);
        if (order) {
            for (int j = 0; j < g->numVertices; j++)
                order[j] = j;
            weld_verts = verts;
            qsort(order, g->numVertices, sizeof *order, weld_cmp);
            for (int j = 0; j < g->numVertices; j++)
                weld[order[j]] = j && !memcmp(&verts[order[j]], &verts[order[j - 1]], sizeof *verts) ? weld[order[j - 1]] : order[j];
            free(order);
        } else
            free(weld), weld = 0;
    }
    /* b211 meshkit: the geometry's key, dominant bones, an override file, a capture */
    unsigned mk_key_g = hash_sample(2166136261u, verts, g->numVertices, sizeof *verts) ^ (unsigned)g->numVertices * 0x9e3779b1u ^ (unsigned)header->numMeshes;
    int reshape = (skin_simplify_now > 0 || p4_mesh_override || p4_mesh_capture) && g->numVertices > 0 && g->numVertices <= 65535;
    unsigned char *mk_group = reshape ? malloc(g->numVertices) : 0, *mk_over = 0, *mk_next = 0;
    int mk_over_size = 0;
    MkBuf mk_cap = { 0, 0, 0 };
    if (mk_group) {
        for (int j = 0; j < g->numVertices; j++) {
            unsigned char bone[4];
            float weight[4];
            int n = vertex_bones(skin, nbones, j, bone, weight), best = 0;
            for (int w = 1; w < n; w++)
                if (weight[w] > weight[best])
                    best = w;
            mk_group[j] = n ? bone[best] : 0xff;
        }
        if (p4_mesh_override && (mk_over = mk_read_override(mk_key_g, header->numMeshes, &mk_over_size)))
            mk_next = mk_over + 16, mk_override_hits++;
        if (p4_mesh_capture) {
            unsigned head[5] = { 0x4b4d3450u, 1, mk_key_g, (unsigned)g->numVertices, (unsigned)header->numMeshes }; /* 'P4MK' */
            mk_put(&mk_cap, head, sizeof head);
            mk_put(&mk_cap, verts, g->numVertices * sizeof *verts);
            mk_put(&mk_cap, mk_group, g->numVertices);
        }
    } else
        reshape = 0;
    for (int m = 0; m < header->numMeshes && ok; m++) {
        const unsigned short *idx = mesh[m].indices;
        int triangles = strip ? mesh[m].numIndices - 2 : mesh[m].numIndices / 3;
        if (triangles < 0)
            triangles = 0;
        int cap = triangles > 0 ? triangles : 1, corners = 0, bones = 0, ntri = 0;
        int *tri = malloc(cap * 3 * sizeof *tri), *corner = malloc(cap * 3 * sizeof *corner);
        unsigned char (*set)[13] = malloc(cap * sizeof *set), *done = malloc(cap);
        unsigned char palette[SKIN_PALETTE];
        if (!tri || !corner || !set || !done) {
            free(tri), free(corner), free(set), free(done);
            ok = 0;
            break;
        }
        /* the mesh's triangles (strips unrolled, degenerate ones left out) and the bones of each */
        for (int t = 0; t < triangles; t++) {
            int *c = corner + ntri * 3;
            if (strip) { /* odd strip triangles are wound the other way */
                c[0] = idx[t + (t & 1)]; c[1] = idx[t + 1 - (t & 1)]; c[2] = idx[t + 2];
                if (c[0] == c[1] || c[1] == c[2] || c[0] == c[2])
                    continue;
            } else {
                c[0] = idx[t * 3]; c[1] = idx[t * 3 + 1]; c[2] = idx[t * 3 + 2];
            }
            set[ntri][0] = 0;
            for (int k = 0; k < 3; k++) {
                unsigned char bone[4];
                float weight[4];
                int n = vertex_bones(skin, nbones, c[k], bone, weight);
                for (int w = 0; w < n; w++) {
                    int have = 0;
                    for (int s = 1; s <= set[ntri][0] && !have; s++)
                        have = set[ntri][s] == bone[w];
                    if (!have)
                        set[ntri][++set[ntri][0]] = bone[w];
                }
            }
            if (set[ntri][0] > SKIN_PALETTE) /* one triangle with more bones than the GE has: the CPU keeps it */
                ok = 0;
            ntri++;
        }
        if (reshape && ok) { /* b211: capture, then the override's or the simplified triangles */
            unsigned short *list = malloc((ntri ? ntri : 1) * 3 * sizeof *list);
            int n = ntri;
            for (int k = 0; list && k < ntri * 3; k++)
                list[k] = corner[k];
            if (list && mk_cap.cap >= 0 && p4_mesh_capture) {
                unsigned count = ntri;
                mk_put(&mk_cap, &count, 4);
                mk_put(&mk_cap, list, ntri * 3 * sizeof *list);
            }
            if (list && mk_next) {
                unsigned count = 0, fits;
                if (mk_next + 4 <= mk_over + mk_over_size)
                    memcpy(&count, mk_next, 4);
                fits = count <= (unsigned)ntri && mk_next + 4 + count * 6 <= mk_over + mk_over_size;
                for (unsigned k = 0; fits && k < count * 3; k++) {
                    unsigned short x;
                    memcpy(&x, mk_next + 4 + k * 2, 2);
                    fits = x < g->numVertices;
                }
                if (fits) {
                    memcpy(list, mk_next + 4, count * 6);
                    n = count, mk_override_meshes++;
                    mk_next += 4 + count * 6;
                } else
                    mk_next = 0; /* not this geometry's file after all: the rest of it is ignored */
            } else if (list && skin_simplify_now > 0 && skin_simplify_now < 100 && ntri >= 8 && ntri >= p4_skin_lod_min) {
                unsigned t0 = sceKernelGetSystemTimeLow();
                n = mk_simplify((const float (*)[3])verts, g->numVertices, 0, mk_group, list, ntri, ntri * skin_simplify_now / 100, INFINITY, 1 << 30, 0);
                mk_us += sceKernelGetSystemTimeLow() - t0;
                mk_meshes++, mk_tri_in += ntri, mk_tri_out += n;
            }
            if (list && n != ntri) {
                for (int k = 0; k < n * 3; k++)
                    corner[k] = list[k];
                ntri = n;
                for (int t = 0; t < ntri; t++) {
                    mk_tri_bones(skin, nbones, corner + t * 3, set[t]);
                    if (set[t][0] > SKIN_PALETTE)
                        ok = 0;
                }
            }
            free(list);
        }
        int regroup = ok && skin_mesh_regroup(&mesh[m], g, corner, ntri);
        if (ok && p4_skin_strip && p4_strip_why && ntri >= 2 && weld) { /* b210 strip diagnosis (b213: on request) */
            unsigned short *list = malloc(ntri * 3 * sizeof *list), *out = malloc((6 * ntri + 6) * sizeof *out);
            if (list && out) {
                if (strip)
                    sd_src_idx += mesh[m].numIndices, sd_src_tri += ntri;
                for (int k = 0; k < ntri * 3; k++)
                    list[k] = corner[k];
                sd_whole_idx += skin_stripify(list, ntri * 3, out, 0);
                for (int k = 0; k < ntri * 3; k++)
                    list[k] = weld[corner[k]];
                sd_weld_idx += skin_stripify(list, ntri * 3, out, 0);
                sd_tri += ntri;
            }
            free(list), free(out);
        }
        memset(done, 0, cap);
        /* a batch takes the triangles in order while their bones fit; with regroup (first-fit) it also takes any later
           triangle of the mesh whose bones still fit, so the triangle order changes (opaque meshes only) */
        for (int first = 0; first < ntri && ok; first++) {
            if (done[first])
                continue;
            bones = corners = 0;
            for (int t = first; t < ntri; t++) {
                if (done[t])
                    continue;
                unsigned char extra[12];
                int add = 0;
                for (int k = 1; k <= set[t][0]; k++) {
                    int have = 0;
                    for (int s = 0; s < bones && !have; s++)
                        have = palette[s] == set[t][k];
                    for (int s = 0; s < add && !have; s++)
                        have = extra[s] == set[t][k];
                    if (!have)
                        extra[add++] = set[t][k];
                }
                if (bones + add > SKIN_PALETTE || (bones > 0 && bones + add > p4_skin_palette)) {
                    if (regroup)
                        continue;
                    break;
                }
                memcpy(palette + bones, extra, add);
                bones += add;
                done[t] = 1;
                memcpy(tri + corners, corner + t * 3, 3 * sizeof *tri);
                corners += 3;
            }
            if (p4_skin_sticky && bones > 1) { /* bones already held in a slot below the batch's count keep it */
                unsigned char sorted[SKIN_PALETTE], taken[SKIN_PALETTE] = { 0 }, placed[SKIN_PALETTE] = { 0 };
                for (int k = 0; k < bones; k++)
                    for (int s = 0; s < bones; s++)
                        if (held[s] == palette[k] && !taken[s]) {
                            sorted[s] = palette[k];
                            taken[s] = placed[k] = 1;
                            break;
                        }
                for (int k = 0, s = 0; k < bones; k++) {
                    if (placed[k])
                        continue;
                    while (taken[s])
                        s++;
                    sorted[s] = palette[k];
                    taken[s] = 1;
                }
                memcpy(palette, sorted, bones);
            }
            if (corners && !skin_batch(e, m, palette, bones > 0 ? bones : 1, tri, corners, g, skin, nbones))
                ok = 0;
            memcpy(held, palette, bones);
            skin_regrouped += regroup;
        }
        free(tri), free(corner), free(set), free(done);
    }
    free(weld);
    if (mk_cap.data && p4_mesh_capture) {
        mk_write_file("capture", mk_key_g, &mk_cap);
        mk_captured++;
    }
    free(mk_cap.data), free(mk_over), free(mk_group);
    if (!ok) /* packed stays 0: no batches */
        skin_free(e), e->g = g, e->verts = verts, e->vertices = g->numVertices, e->lit = lit, e->nrm = nrm, e->sample = sample, e->frame = cache_frame;
    else if (p4_batch_stats)
        batch_regroup(e, g, skin, nbones);
    vertex_bone_map = 0;
    if (far_map && e->batches && (e->bone_len = malloc(nbones * sizeof *e->bone_len))) {
        /* b254: per dropped weighted bone, its joint's bind distance from the bone it follows (far_bones_sane) */
        unsigned char weighted[BL_MAX_BONES] = { 0 };
        for (int j = 0; j < g->numVertices; j++) {
            unsigned char b[4];
            float w[4];
            int n = vertex_bones_raw(skin, nbones, j, b, w);
            for (int k = 0; k < n; k++)
                weighted[b[k]] = 1;
        }
        for (int i = 0; i < nbones; i++) {
            e->bone_len[i] = -1;
            if (far_map[i] == i || !weighted[i])
                continue;
            float ji[3], ja[3];
            for (int pass = 0; pass < 2; pass++) { /* the joint is where p * skinToBone = 0: p_k = -pos . row_k */
                const Matrix *m = &skin->skinToBone[pass ? far_map[i] : i];
                float *out = pass ? ja : ji;
                out[0] = -(m->pos.x * m->right.x + m->pos.y * m->right.y + m->pos.z * m->right.z);
                out[1] = -(m->pos.x * m->up.x + m->pos.y * m->up.y + m->pos.z * m->up.z);
                out[2] = -(m->pos.x * m->at.x + m->pos.y * m->at.y + m->pos.z * m->at.z);
            }
            float d = sqrtf((ji[0] - ja[0]) * (ji[0] - ja[0]) + (ji[1] - ja[1]) * (ji[1] - ja[1]) + (ji[2] - ja[2]) * (ji[2] - ja[2]));
            e->bone_len[i] = d <= 1e30f ? d : 0;
        }
        e->bone_map = far_map, e->nmap = nbones;
    } else
        free(far_map);
    unsigned build_us = sceKernelGetSystemTimeLow() - build_t0;
    skin_build_us += build_us;
    if (skin_bones_now > 0)
        skin_far_us += build_us;
    if (build_us > skin_build_max_us)
        skin_build_max_us = build_us, skin_build_max_v = g->numVertices;
    return e->batches ? e : 0; /* a failed build stays as an empty entry: the CPU path, without retrying every frame */
}

/* One batch skinned on the CPU from its own GE vertices (u8 weights / 128 over the palette, bind pose), into the
   frame pool in the space the GE batches use, then cut against the guard band like a static mesh. Unlit: the
   material colour goes into the vertices, as on the CPU path; lit: the GE lights it (draw_mesh_vertices). The batch's
   alpha bounds (draw_skin_alpha) still hold: skinning moves vertices, not colours. */
static unsigned char bone_parked[MAX_BONES]; /* b180: this frame's bones with an insane matrix (draw_skinned_ge) */
static int bones_parked;
static void skin_batch_cpu(const SkinBatch *b, int lit, int nrm, const float *pack, const Material *material, int modulate, const SceneLight *light, const Matrix *space, float near)
{
    SkinLayout l = skin_layout(b->bones, nrm, pack != 0);
    int stride = l.stride, n = b->vertices, out_stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    int units = (n * out_stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
    if (n > MAX_GUARDED || vertex_used + units > MAX_VERTICES) {
        p4_log("TRACE vertex pool full\n");
        return;
    }
    char *out = (char *)(vertex_pool + vertex_used);
    vertex_used += units;
    unsigned c = modulate ? material->colour : 0xffffffff;
    for (int j = 0; j < n; j++) {
        const unsigned char *in = b->v + stride * j;
        V3 bind, normal = { 0, 0, 0 }, p = { 0, 0, 0 }, q = { 0, 0, 0 };
        float uv[2];
        unsigned colour;
        skin_vertex_read(in, &l, pack, b->uvs, &bind, uv);
        memcpy(&colour, in + l.colour, 4);
        if (lit)
            normal = (V3){ (signed char)in[l.normal], (signed char)in[l.normal + 1], (signed char)in[l.normal + 2] };
        for (int s = 0; s < b->bones; s++) {
            if (!in[s])
                continue;
            float w = in[s] * (1.0f / 128);
            const Matrix *m = &bones[b->palette[s]];
            V3 t = transform(&bind, m);
            p.x += t.x * w, p.y += t.y * w, p.z += t.z * w;
            if (lit) {
                V3 r = rotate(&normal, m);
                q.x += r.x * w, q.y += r.y * w, q.z += r.z * w;
            }
        }
        if (lit) {
            float len = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z), inv = len > 0 ? 1.0f / len : 0;
            ((Vertex3DN *)out)[j] = (Vertex3DN){ uv[0], uv[1], colour, normal_s8(q.x * inv), normal_s8(q.y * inv), normal_s8(q.z * inv), 0, p.x, p.y, p.z };
        } else {
            ((Vertex3D *)out)[j] = (Vertex3D){ uv[0], uv[1], modulate_colour(colour, c), p.x, p.y, p.z };
        }
    }
    sceKernelDcacheWritebackRange(out, n * out_stride);
    if (!lit)
        st_enable(GU_LIGHTING, 0); /* the GE batches before left it on */
    skin_cpu_batches++;
    const unsigned short *idx = b->idx;
    int indices = b->indices;
    if (bones_parked) { /* leave out the triangles with a corner on a parked bone */
        int parked = 0;
        for (int s = 0; s < b->bones; s++)
            parked |= bone_parked[b->palette[s]];
        if (parked && index_used + b->indices <= MAX_INDICES) {
            unsigned short *kept = index_pool + index_used; /* the GE reads it after this returns: the frame's pool */
            int k = 0;
            for (int t = 0; t + 2 < b->indices; t += 3) {
                int drop = 0;
                for (int c = 0; c < 3 && !drop; c++) {
                    const unsigned char *in = b->v + stride * b->idx[t + c];
                    for (int s = 0; s < b->bones; s++)
                        drop |= in[s] && bone_parked[b->palette[s]];
                }
                if (!drop)
                    kept[k] = b->idx[t], kept[k + 1] = b->idx[t + 1], kept[k + 2] = b->idx[t + 2], k += 3;
            }
            index_used += k;
            sceKernelDcacheWritebackRange(kept, k * 2);
            idx = kept, indices = k;
        }
    }
    if (!indices)
        return;
    draw_guarded_compact(GU_TRIANGLES, idx, indices, out, n, lit ? VT_3D | GU_NORMAL_8BIT : VT_3D, space, near, material, modulate, light, 0, 0, 0, 0);
}

/* 1: drawn on the GE; 0: the caller skins on the CPU */
/* b178: a battle shows a large flat triangle from Chie's hand with GE skinning (also with p4_skin_split 2, the batch
   data skinned on the CPU), none with p4_ge_skin 0. Every 32 frames each batch vertex is skinned from its u8 weights
   and palette and compared with the CPU path's sum over the RenderWare weights; differences over 4 units logged. */
int p4_skin_diff = 0; /* 1: compare GE batch vertices with the CPU sum every 128 frames (b179 diagnostic) */
static unsigned skin_diff_logged, skin_diff_count, skin_diff_checked;
static void skin_diff(const Geometry *g, const Skin *skin, const SkinGeometry *e, int nbones)
{
    const V3 *verts = g->morphTarget->verts;
    for (int bi = 0; bi < e->batches; bi++) {
        const SkinBatch *b = &e->batch[bi];
        if (!b->src)
            continue;
        SkinLayout l = skin_layout(b->bones, e->nrm, e->packed);
        float step = e->packed ? e->pack[3] * (1.0f / 32768) : 0, moved = 3 * step * step + 1e-6f; /* s16 rounding */
        for (int vi = 0; vi < b->vertices; vi++) {
            const unsigned char *in = b->v + l.stride * vi;
            int j = b->src[vi];
            V3 bind;
            float uv[2];
            skin_vertex_read(in, &l, e->packed ? e->pack : 0, b->uvs, &bind, uv);
            V3 ge = { 0, 0, 0 }, ref = { 0, 0, 0 };
            for (int s = 0; s < b->bones; s++) {
                if (!in[s])
                    continue;
                V3 p = transform(&bind, &bones[b->palette[s]]);
                float w = in[s] / 128.0f;
                ge.x += p.x * w, ge.y += p.y * w, ge.z += p.z * w;
            }
            if (skin->maxWeights == 1)
                ref = transform(&verts[j], &bones[skin->indices[j][0]]);
            else
                for (int w = 0; w < skin->maxWeights && w < 4 && skin->weights[j][w] > 0; w++) {
                    V3 p = transform(&verts[j], &bones[skin->indices[j][w]]);
                    float k = skin->weights[j][w];
                    ref.x += p.x * k, ref.y += p.y * k, ref.z += p.z * k;
                }
            skin_diff_checked++;
            float dx = ge.x - ref.x, dy = ge.y - ref.y, dz = ge.z - ref.z, bx = bind.x - verts[j].x, by = bind.y - verts[j].y, bz = bind.z - verts[j].z;
            float d2 = dx * dx + dy * dy + dz * dz, b2 = bx * bx + by * by + bz * bz;
            if (!(d2 <= 16) || !(b2 <= moved)) {
                skin_diff_count++;
                if (skin_diff_logged < 40) {
                    skin_diff_logged++;
                    const unsigned char *ix = skin->indices[j];
                    const float *wt = skin->weights[j];
                    p4_log("TRACE skin diff geometry %p v%d j %d batch %d bones %d idx %d w %d u8 %d %d ge %d %d %d ref %d %d %d bind_moved %d locked %x\n", g, g->numVertices, j, bi, b->bones, skin->indices[j][0], (int)(skin->weights[j][0] * 1000), in[0], b->bones > 1 ? in[1] : 0, (int)ge.x, (int)ge.y, (int)ge.z, (int)ref.x, (int)ref.y, (int)ref.z, !(b2 <= moved), g->locked);
                }
            }
        }
    }
}
/* b180: in a battle bone 38 of Chie's 1829-vertex model (vertices 583-716, weight 1) skinned to coordinates past 2^31
   on the CPU and the GE alike (p4_skin_diff: both INT_MAX) - by its look the game parks a hidden part of the model
   there. The CPU path's guard clipper drops those triangles; the GE drew them as large flat triangles from her hand
   and head. A bone whose matrix is not finite or is beyond 1e7 sends its batches through skin_batch_cpu. */
int p4_skin_finite = 1; /* 0: as b179 */
static int matrix_sane(const Matrix *m)
{
    const float *f = &m->right.x;
    for (int k = 0; k < 15; k++) {
        if ((k & 3) == 3)
            continue;
        if (!(f[k] < 1e7f && f[k] > -1e7f))
            return 0;
    }
    return 1;
}
/* b254: a far entry draws each dropped bone's vertices rigidly with the bone it follows. The game hides parts by
   moving or shrinking their bones (b180: a bone parked past 1e7); followed rigidly, they would show. This frame's
   pose must keep every dropped bone sane, near its scale and within twice its bind distance (+1) of its follower;
   otherwise the character is drawn from an entry with every bone. */

static int far_bones_sane(const SkinGeometry *e, const Hierarchy *hierarchy, int nbones)
{
    for (int i = 0; i < nbones && i < e->nmap; i++) {
        if (!e->bone_len || e->bone_len[i] < 0)
            continue;
        int a = e->bone_map[i];
        if (!matrix_sane(&bones[i]) || !matrix_sane(&bones[a]))
            return 0;
        const Matrix *mi = &hierarchy->matrices[i], *ma = &hierarchy->matrices[a];
        float si = mi->right.x * mi->right.x + mi->right.y * mi->right.y + mi->right.z * mi->right.z;
        float sa = ma->right.x * ma->right.x + ma->right.y * ma->right.y + ma->right.z * ma->right.z;
        if (!(si > sa * 0.25f && si < sa * 4)) /* scale within 2x (squared lengths) */
            return 0;
        float dx = mi->pos.x - ma->pos.x, dy = mi->pos.y - ma->pos.y, dz = mi->pos.z - ma->pos.z, limit = 2 * e->bone_len[i] * sqrtf(sa) + 1;
        if (!(dx * dx + dy * dy + dz * dz <= limit * limit))
            return 0;
    }
    return 1;
}
static int skin_call_key(const SkinBatch *b, const Material *m, int modulate, const SceneLight *light, unsigned h[2])
{
    unsigned w[48];
    int n = p4_skin_call ? call_inputs(w, m, modulate, light) : -1;
    if (n < 0)
        return 0;
    w[n++] = (unsigned)b, w[n++] = (unsigned)b->v, w[n++] = (unsigned)b->idx, w[n++] = b->vtype, w[n++] = b->indices;
    /* b241: a recorded draw holds the strip's address and count. Without them in the key, a geometry rebuilt at the
       same batch, vertex and list addresses (free then malloc of the same sizes, as when a battle loads) replayed
       the old record with the address of a strip that had been freed. */
    w[n++] = (unsigned)b->strip, w[n++] = b->strip_count | b->strip_n << 16; /* b250: and how many PRIMs it was recorded as */
    call_key(w, n, h);
    return 1;
}

/* b259 p4_skin_list: a skinned character as one CALL, as static_list.h does for scenery. The batch loop is recorded
   once from a forgotten state, with the per-batch CALL replays off: the words (state, bone matrix blocks, textures,
   lights, draws) become the character's own list, kept in 3 copies (the GE reads a frame up to 2 frames late). Each
   frame the bone matrices are written into the next copy at the places the recording noted, then model matrix +
   CALL. The vertices are the skin cache entry's own (freed with it, after the GE). Key: materials and rasters,
   render/sky state, pixel mask, light, texture generation, camera, lit/modulate/space and the switches. Not with any
   bone outside the guard band or parked (those batches go through the CPU), not twice in one frame, not in the
   shadow pass. */
int p4_skin_list = 1;
typedef struct {
    unsigned key[6];
    const void *camera;
    unsigned last, retry;
    int words, draws, nbones;
    unsigned *buf[3];
    unsigned short (*bone)[2]; /* word offset of a block's first data word, bone index */
} SkinList;
static int sk_recording, sk_nbones;
enum { SK_BONES = 512 };
static unsigned **sk_bone_at; /* heap: .bss is full */
static unsigned char *sk_bone_of;
static unsigned sk_hits, sk_records, sk_refused[4], sk_misses[3];
static void sl_bury(void *p);
static unsigned sl_state_key(void);
static unsigned sl_material_key(const Geometry *g);
static void sl_forget(void);
static unsigned sl_fog_key(void);
static void sk_free(void *list)
{
    SkinList *l = list;
    if (!l)
        return;
    for (int i = 0; i < 3; i++)
        sl_bury(l->buf[i]);
    free(l->bone);
    free(l);
}
/* the 12 BONE_MATRIX_DATA words of bone i, folded for packed positions as the batch loop does */
static void skin_bone_words(const SkinGeometry *e, int i, unsigned *out)
{
    const Matrix *bm = &bones[i];
    Matrix folded;
    if (e->packed) {
        const float *c = e->pack, s = e->pack[3];
        folded = *bm;
        folded.right = (V3){ bm->right.x * s, bm->right.y * s, bm->right.z * s };
        folded.up = (V3){ bm->up.x * s, bm->up.y * s, bm->up.z * s };
        folded.at = (V3){ bm->at.x * s, bm->at.y * s, bm->at.z * s };
        folded.pos = (V3){ bm->pos.x + c[0] * bm->right.x + c[1] * bm->up.x + c[2] * bm->at.x,
                           bm->pos.y + c[0] * bm->right.y + c[1] * bm->up.y + c[2] * bm->at.y,
                           bm->pos.z + c[0] * bm->right.z + c[1] * bm->up.z + c[2] * bm->at.z };
        bm = &folded;
    }
    const AliasWord *f = (const AliasWord *)bm;
    for (int row = 0; row < 4; row++)
        for (int col = 0; col < 3; col++)
            out[row * 3 + col] = 43u << 24 | f[row * 4 + col] >> 8;
}
static void sk_key(unsigned key[6], const Geometry *g, int lit, int modulate, const Matrix *space)
{
    SceneLight light = scene_lights();
    key[0] = sl_state_key();
    key[1] = sl_material_key(g);
    key[2] = hash_words(2166136261u, &light, sizeof light);
    key[3] = texture_generation;
    key[4] = state_bits() ^ (unsigned)p4_skin_alpha << 16 ^ (unsigned)p4_skin_dummy << 17 ^ (unsigned)p4_bone_direct << 18 ^ sl_fog_key();
    key[5] = (unsigned)lit | (unsigned)modulate << 1 | (space != 0) << 2;
}
/* the recorded words from..to into e's list; model matrix words left out, bone blocks noted */
static void sk_keep(SkinGeometry *e, const unsigned *from, const unsigned *to, const unsigned key[6])
{
    const unsigned *m0 = mesh_model_begin, *m1 = mesh_model_mark;
    if (!m0 || !m1 || m0 < from || m1 < m0 || m1 > to || sk_nbones > SK_BONES || !sk_bone_at || !sk_bone_of) {
        sk_refused[0]++;
        return;
    }
    int n = (m0 - from) + (to - m1), draws = 0;
    for (const unsigned *w = from; w < to; w++) {
        if (w == m0)
            w = m1;
        if (w >= to)
            break;
        unsigned cmd = *w >> 24;
        if (cmd == 0x08 || cmd == 0x0a || cmd == 0x0b || cmd == 0x0c || cmd == 0x0f) {
            sk_refused[1]++;
            return;
        }
        draws += cmd == 0x04;
    }
    SkinList *l = calloc(1, sizeof *l);
    unsigned short (*bone)[2] = malloc((sk_nbones ? sk_nbones : 1) * sizeof *bone);
    int ok = l && bone && n > 0 && n < 65535;
    for (int i = 0; ok && i < 3; i++)
        ok = (l->buf[i] = memalign(16, (n + 1) * 4)) != 0;
    if (!ok) {
        if (l)
            for (int i = 0; i < 3; i++)
                free(l->buf[i]);
        free(l), free(bone);
        sk_refused[2]++;
        return;
    }
    int k = 0;
    for (const unsigned *w = from; w < to; w++) {
        if (w == m0)
            w = m1;
        if (w >= to)
            break;
        l->buf[0][k++] = *w;
    }
    l->buf[0][k] = 0x0Bu << 24; /* RET */
    for (int i = 0; i < sk_nbones; i++) {
        const unsigned *at = sk_bone_at[i];
        int off = (at - from) - (at > m0 ? m1 - m0 : 0);
        if (at < from || at + 13 > to || (at >= m0 && at < m1) || off < 0 || off + 13 > k || l->buf[0][off] >> 24 != 42) {
            for (int j = 0; j < 3; j++)
                free(l->buf[j]);
            free(l), free(bone);
            sk_refused[3]++;
            return;
        }
        bone[i][0] = off + 1, bone[i][1] = sk_bone_of[i];
    }
    memcpy(l->buf[1], l->buf[0], (k + 1) * 4);
    memcpy(l->buf[2], l->buf[0], (k + 1) * 4);
    memcpy(l->key, key, sizeof l->key);
    l->camera = current_camera, l->words = k + 1, l->draws = draws, l->nbones = sk_nbones, l->bone = bone, l->last = cache_frame;
    sk_free(e->list);
    e->list = l;
    sk_records++;
}
/* b357 p4_skin_guard 0 (experiment): characters are small triangles, so no bone-sphere test against the guard band;
   only non-finite (parked) bones still send their batches to the CPU. A triangle crossing the near plane with a corner
   behind the eye is dropped by the GE (b101): small holes when the camera is inside a character. 1: as before.
   b388: 0 by default (PPSSPP Margaret: sguard 241k -> 185k cycles a frame). */
int p4_skin_guard = 0;
static unsigned sb_bones_us, sb_geom_us, sb_draws; /* b356: draw_skinned_ge's sbones split: skin_bones vs skin_geometry */
static void sk_report(void)
{
    p4_log("skin sbones split per 256 frames: draws %u bones %u us geometry %u us\n", sb_draws, sb_bones_us, sb_geom_us);
    sb_bones_us = sb_geom_us = sb_draws = 0;
    p4_log("skin list %d per 256 frames: hits %u records %u; misses key %u twice %u retry %u; refused marks %u flow %u memory %u bones %u\n",
           p4_skin_list, sk_hits, sk_records, sk_misses[0], sk_misses[1], sk_misses[2], sk_refused[0], sk_refused[1], sk_refused[2],
           sk_refused[3]);
    sk_hits = sk_records = 0;
    memset(sk_misses, 0, sizeof sk_misses), memset(sk_refused, 0, sizeof sk_refused);
}
/* b359 p4_matfx_env_ge 1: a GE-skinned character's MatFX env map drawn by the GE, as Burnout Legends draws its car
   reflections: right after each env batch (its bones still in the GE), the same vertices again with the texture
   coordinates generated from the normalized normal through the texture matrix (TMAP 0x201): rotation into the env
   space x 0.5 + 0.5, the formula of matfx_env_pass in rwsky.c. Vertex colour (prelit or white) x env texture, and
   the material colour x coefficient as the blend FIX: (Cs x fix) + Cd. The batches carry s8 normals for this even
   unlit (SkinGeometry.nrm). Only unlit characters (a lit stream has black vertex colour); no skin list for them,
   since the texture matrix follows the camera. 0: as b358, such a character takes the CPU skin path
   (p4_matfx_env_skin). */
int p4_matfx_env_ge = 0;
static unsigned matfx_env_ge_draws;
static void skin_env_ge(const unsigned char *e, const Material *material, int modulate, const Matrix *space, const SkinBatch *batch)
{
    float coef = *(const float *)(e + 8);
    void **texture = *(void ***)(e + 4);
    if (!(coef > 0) || !texture)
        return;
    if (coef > 1)
        coef = 1;
    const Matrix identity = { .right = { 1, 0, 0 }, .up = { 0, 1, 0 }, .at = { 0, 0, 1 } };
    const Matrix *m = space ? space : &identity;
    const V3 *mrow[3] = { &m->right, &m->up, &m->at };
    const void *frame = *(void *const *)e;
    const Matrix *env = frame ? p4r_003e9700((void *)frame) : (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    float r[3][2];
    for (int a = 0; a < 3; a++) {
        const V3 *row = mrow[a];
        if (frame) { /* orthonormal LTM: its inverse rotation is the transpose */
            r[a][0] = row->x * env->right.x + row->y * env->right.y + row->z * env->right.z;
            r[a][1] = row->x * env->up.x + row->y * env->up.y + row->z * env->up.z;
        } else {
            r[a][0] = row->x * env->right.x + row->y * env->up.x + row->z * env->at.x;
            r[a][1] = row->x * env->right.y + row->y * env->up.y + row->z * env->at.y;
        }
    }
    ScePspFMatrix4 t = { { 0.5f * r[0][0], 0.5f * r[0][1], 0, 0 }, { 0.5f * r[1][0], 0.5f * r[1][1], 0, 0 },
                         { 0.5f * r[2][0], 0.5f * r[2][1], 0, 0 }, { 0.5f, 0.5f, 1, 1 } };
    unsigned mc = modulate ? material->colour : 0xffffffff, fix = 0;
    for (int s = 0; s < 24; s += 8)
        fix |= (unsigned)((mc >> s & 0xff) * coef + 0.5f) << s;
    st_enable(GU_LIGHTING, 0);
    st_enable(GU_ALPHA_TEST, 0);
    st_enable(GU_DEPTH_TEST, 1);
    st_depth_func(GU_GEQUAL);
    st_depth_mask(GU_FALSE); /* depth write on, as matfx_env_pass */
    st_enable(GU_STENCIL_TEST, 0);
    st_pixel_mask(pixel_mask | 0xff000000); /* our alpha byte is the stencil: leave it */
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, fix, 0xffffff);
    gs.blend_known = 0;
    bind_raster(*texture, GU_REPEAT);
    bind_texscale(0);
    sceGuSetMatrix(GU_TEXTURE, &t);
    sceGuTexProjMapMode(GU_NORMALIZED_NORMAL);
    sceGuTexMapMode(GU_TEXTURE_MATRIX, 0, 0);
    if (batch->strip) {
        draw_strip_lens = batch->strip_lens, draw_strip_n = batch->strip_n;
        gu_draw(GU_TRIANGLE_STRIP, batch->vtype, batch->strip_count, batch->strip, batch->v);
        draw_strip_n = 0;
    } else
        gu_draw(GU_TRIANGLES, batch->vtype, batch->indices, batch->idx, batch->v);
    sceGuTexProjMapMode(GU_POSITION);
    sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
    matfx_env_ge_draws++;
}
static int draw_skinned_ge(const Geometry *g, const Skin *skin, const Hierarchy *hierarchy, const Matrix *model, int lit, int modulate, int env_ge)
{
    prof_begin(PROF_SBONES);
    bone_check_geometry = g;
    unsigned sb_t0 = sceKernelGetSystemTimeLow();
    int nbones = skin_bones(skin, hierarchy);
    unsigned sb_t1 = sceKernelGetSystemTimeLow();
    skin_simplify_now = p4_skin_simplify;
    skin_bones_now = 0;
    skin_hierarchy_now = hierarchy;
    int far_on = p4_skin_far_depth > 0 && p4_skin_far_bones > 0 && p4_skin_far_keep > 0 && p4_skin_far_keep <= 100;
    int near_on = p4_skin_lod_depth > 0 && p4_skin_lod_keep > 0 && p4_skin_lod_keep < 100;
    if (near_on || far_on) {
        AnimState *as = anim_find(hierarchy); /* its depth is the last frame's (anim_seen_at, below) */
        if (as) {
            /* 0 full, 1 simplified, 2 far (fewer bones too); back one level only below 0.9 of its edge */
            int level = 0;
            if (far_on && (as->depth > p4_skin_far_depth || (as->lod == 2 && as->depth > p4_skin_far_depth * 0.9f)))
                level = 2;
            else if (near_on && (as->depth > p4_skin_lod_depth || (as->lod >= 1 && as->depth > p4_skin_lod_depth * 0.9f)))
                level = 1;
            as->lod = level;
            if (level == 2) {
                skin_simplify_now = p4_skin_far_keep < 100 ? p4_skin_far_keep : 0;
                skin_bones_now = p4_skin_far_bones;
            } else if (level == 1)
                skin_simplify_now = p4_skin_lod_keep;
            skin_lod_draws += level == 1, skin_full_draws += level == 0, skin_far_draws += level == 2;
        }
    }
    SkinGeometry *e = nbones > 0 ? skin_geometry(g, skin, nbones, lit, lit || env_ge) : 0;
    if (e && e->bone_map && !far_bones_sane(e, hierarchy, nbones)) {
        skin_far_fallbacks++;
        skin_bones_now = 0;
        skin_simplify_now = p4_skin_lod_depth > 0 && p4_skin_lod_keep > 0 && p4_skin_lod_keep < 100 ? p4_skin_lod_keep : p4_skin_simplify;
        e = skin_geometry(g, skin, nbones, lit, lit || env_ge);
    }
    sb_bones_us += sb_t1 - sb_t0, sb_geom_us += sceKernelGetSystemTimeLow() - sb_t1, sb_draws++;
    prof_end(PROF_SBONES);
    if (!e || e->nradius < nbones)
        return 0;
    if (p4_bone_census)
        bone_census_note(hierarchy, e->used);
    /* A skinned vertex is a weighted mean of points within radius[i] of joint i; the guard band is convex, so it is
       inside when every joint's sphere is (one sphere round the whole skeleton crossed the near plane: b76 r 520). */
    const Matrix *space = (hierarchy->flags & HIERARCHY_LOCAL_SPACE) ? model : shadow_shear_m; /* world bones; b186 shadow: sheared */
    { /* b177 probe: the battle's character shadows (ATEST 0x7F06B) flatten the clump's frame before the hierarchy update */
        static unsigned shown;
        unsigned t = sky_state[3] & 0xfffff;
        if ((t == 0x7f06b || t == 0x717fb) && shown < 48 && !(cache_frame & 31)) {
            shown++;
            const Matrix *h0 = &hierarchy->matrices[0];
            p4_log("TRACE %s h %p flags %x bone0 up %d %d %d pos %d %d %d space %p up %d %d %d colour %08x\n", t == 0x7f06b ? "shadowpass" : "mainpass", hierarchy,
                   hierarchy->flags, (int)(h0->up.x * 1000), (int)(h0->up.y * 1000), (int)(h0->up.z * 1000), (int)h0->pos.x,
                   (int)h0->pos.y, (int)h0->pos.z, space, space ? (int)(space->up.x * 1000) : 0, space ? (int)(space->up.y * 1000) : 0,
                   space ? (int)(space->up.z * 1000) : 0, ((const Material *)0) ? 0 : 0);
        }
    }
    if (p4_skin_diff && !(cache_frame & 127))
        skin_diff(g, skin, e, nbones);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    Mat4 m;
    prof_begin(PROF_SGUARD);
    guard_matrix(m, space);
    {
        const Matrix *root = &hierarchy->matrices[0]; /* its clip w: how far the character is, for p4_anim_far */
        anim_seen_at(hierarchy, root->pos.x * m[0][3] + root->pos.y * m[1][3] + root->pos.z * m[2][3] + m[3][3], model);
    }
    float planes[5][4], plane_len[5];
    if (p4_skin_guard_fast && p4_skin_guard)
        guard_sphere_planes(m, near, planes, plane_len);
    static unsigned char bone_out[MAX_BONES];
    int any_out = 0;
    bones_parked = 0;
    for (int i = 0; i < nbones; i++) {
        bone_out[i] = 0;
        bone_parked[i] = 0;
        if (p4_skin_finite && !matrix_sane(&bones[i])) {
            bone_parked[i] = 1;
            bones_parked = 1;
            bone_out[i] = 1; /* its vertices reach no sane place: the CPU batch path drops them, as the PS2's clipper did */
            any_out = 1;
            skin_nonfinite_bones++;
            continue;
        }
        if (e->radius[i] <= 0 || !p4_skin_guard)
            continue;
        const Matrix *pose = &hierarchy->matrices[i];
        float scale = sqrtf(pose->right.x * pose->right.x + pose->right.y * pose->right.y + pose->right.z * pose->right.z);
        float sphere[4] = { pose->pos.x, pose->pos.y, pose->pos.z, e->radius[i] * (scale > 1 ? scale : 1) };
        int inside = p4_skin_guard_fast ? guard_sphere_inside_planes(planes, plane_len, sphere) : guard_sphere_inside(m, sphere, near);
        if (!inside && p4_skin_guard_box && p4_skin_guard_fast && e->box) { /* b385: the bone's box, tighter than its sphere */
            inside = skin_box_inside(planes, pose, e->box + i * 6);
            skin_box_saved += inside;
        }
        if (p4_skin_guard_fast && p4_skin_guard_check) {
            skin_guard_checks++;
            skin_guard_diffs += inside != guard_sphere_inside(m, sphere, near);
        }
        if (!inside && !p4_skin_split) {
            skin_ge_fallbacks++;
            prof_end(PROF_SGUARD);
            return 0;
        }
        bone_out[i] = !inside || p4_skin_split == 2; /* 2: every batch through skin_batch_cpu, to check it against the GE */
        any_out |= bone_out[i];
    }
    skin_ge_fallbacks += any_out;
    prof_end(PROF_SGUARD);
    prof_begin(PROF_SDRAW);
    unsigned sk[6], *sk_from = 0;
    int sk_rec = 0;
    if (p4_skin_list && !env_ge && !any_out && !bones_parked && !shadow_shear_m && p4_bone_direct && p4_state_cache && p4_draw_batch > 1 && !p4_probe_3d &&
        !p4_hide_material && !rtt_target && !p4_gs_test_raw_prelit && !p4_batch_stats) { /* b323: fog is in the key */
        sk_key(sk, g, lit, modulate, space);
        SkinList *l = e->list;
        if (l && l->last == cache_frame) {
            sk_misses[1]++; /* a second character with this geometry this frame: its list holds the first one's bones */
        } else if (l && l->camera == current_camera && !memcmp(l->key, sk, sizeof sk)) {
            unsigned *buf = l->buf[cache_frame % 3];
            for (int i = 0; i < l->nbones; i++)
                skin_bone_words(e, l->bone[i][1], buf + l->bone[i][0]);
            sceKernelDcacheWritebackRange(buf, l->words * 4);
            l->last = cache_frame;
            begin_frame();
            gu_mesh_begin();
            set_camera();
            if (space)
                set_matrix(GU_MODEL, space);
            else {
                ScePspFMatrix4 identity;
                gumLoadIdentity(&identity);
                sceGuSetMatrix(GU_MODEL, &identity);
            }
            unsigned *w = gu_list->current;
            w[0] = 0x10u << 24 | ((unsigned)buf >> 8 & 0xf0000); /* BASE */
            w[1] = 0x0Au << 24 | ((unsigned)buf & 0xffffff);      /* CALL */
            gu_list->current = w + 2;
            sl_forget();
            replay.valid = 0;
            sk_hits++;
            bones_sent += l->nbones;
            mesh_draw_calls += l->draws;
            draws_since_stall += l->draws;
            if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
                stall_flush();
            prof_end(PROF_SDRAW);
            skin_ge_draws++;
            skin_ge_batches += e->batches;
            return 1;
        } else if (!l || (int)(l->retry - cache_frame) <= 0) {
            sk_misses[0] += l != 0;
            begin_frame();
            gu_mesh_begin();
            set_camera();
            sl_forget();
            replay.valid = 0;
            if (!sk_bone_at) {
                sk_bone_at = malloc(SK_BONES * sizeof *sk_bone_at);
                sk_bone_of = malloc(SK_BONES);
            }
            sk_rec = 1, sk_recording = 1, sk_nbones = 0;
            mesh_model_begin = mesh_model_mark = 0;
            gu_object_stack_depth++;
            sk_from = gu_list->current;
            if (l)
                l->retry = cache_frame + 8; /* a key that changes every frame records at most every 8th */
        } else
            sk_misses[2]++;
    }
    begin_frame();
    SceneLight light = mesh_state(space, lit);
    MeshHeader *header = g->mesh;
    Mesh *mesh = (Mesh *)((char *)header + sizeof(MeshHeader) + header->firstMeshOffset);
    int slot_bone[SKIN_PALETTE]; /* bones[] index in each GE bone slot; bones[] is this character's */
    for (int k = 0; k < SKIN_PALETTE; k++)
        slot_bone[k] = -1;
    unsigned sent_before = bones_sent;
    for (int b = 0; b < e->batches; b++) {
        SkinBatch *batch = &e->batch[b];
        const Material *material = mesh[batch->mesh].material;
        int cpu = 0;
        for (int k = 0; k < batch->bones && any_out; k++)
            cpu |= bone_out[batch->palette[k]];
        prof_begin(PROF_SMAT);
        for (int k = 0; k < batch->bones && !cpu; k++) {
            const Matrix *bm = &bones[batch->palette[k]];
            if (p4_bone_direct && slot_bone[k] == batch->palette[k])
                continue;
            Matrix folded;
            if (e->packed) { /* p = centre + s q / 32768 (skin_layout): rotation x s, translation + rotation . centre */
                const float *c = e->pack, s = e->pack[3];
                folded = *bm;
                folded.right = (V3){ bm->right.x * s, bm->right.y * s, bm->right.z * s };
                folded.up = (V3){ bm->up.x * s, bm->up.y * s, bm->up.z * s };
                folded.at = (V3){ bm->at.x * s, bm->at.y * s, bm->at.z * s };
                folded.pos = (V3){ bm->pos.x + c[0] * bm->right.x + c[1] * bm->up.x + c[2] * bm->at.x,
                                   bm->pos.y + c[0] * bm->right.y + c[1] * bm->up.y + c[2] * bm->at.y,
                                   bm->pos.z + c[0] * bm->right.z + c[1] * bm->up.z + c[2] * bm->at.z };
                bm = &folded;
            }
            if (p4_bone_direct) {
                /* BONE_MATRIX_NUMBER then 12 x BONE_MATRIX_DATA (float >> 8), as sceGuBoneMatrix sends them; b123
                   Heaven: sceGuBoneMatrix cost ~1450 cycles a matrix, 1.78 Mcycles a frame. A slot that already holds
                   this bone (same character, earlier batch) keeps it. */
                slot_bone[k] = batch->palette[k];
                const AliasWord *f = (const AliasWord *)bm;
                unsigned *c = gu_list->current;
                if (sk_recording && sk_nbones < SK_BONES && sk_bone_at)
                    sk_bone_at[sk_nbones] = c, sk_bone_of[sk_nbones] = batch->palette[k];
                sk_nbones++;
                c[0] = 42u << 24 | (unsigned)(k * 12);
                for (int row = 0; row < 4; row++)
                    for (int col = 0; col < 3; col++)
                        c[1 + row * 3 + col] = 43u << 24 | f[row * 4 + col] >> 8;
                gu_list->current = c + 13;
                bones_sent++;
                continue;
            }
            ScePspFMatrix4 out = {
                { bm->right.x, bm->right.y, bm->right.z, 0 }, { bm->up.x, bm->up.y, bm->up.z, 0 },
                { bm->at.x, bm->at.y, bm->at.z, 0 }, { bm->pos.x, bm->pos.y, bm->pos.z, 1 },
            };
            sceGuBoneMatrix(k, &out);
            bones_sent++;
        }
        prof_end(PROF_SMAT);
        /* lighting is on for every batch: the lit alpha is the vertex alpha x the material's (bind_ge_light's ambient).
           Bounded like a mesh's, draw_tested skips the second TEST_1 pass when every pixel passes (b128 Heaven: all
           153 batches a frame drew twice). */
        int bounds[2];
        if (p4_skin_alpha && batch->alpha[1] >= batch->alpha[0]) {
            unsigned ma = modulate ? material->colour >> 24 : 255;
            bounds[0] = batch->alpha[0] * ma / 255;
            bounds[1] = (batch->alpha[1] * ma + 254) / 255;
            draw_alpha_known = 1;
            draw_skin_alpha = bounds;
        } else {
            draw_alpha_known = 0; /* the batch's colours are not scanned */
        }
        if (census3d_skip(material, batch->vtype, batch->indices, modulate)) {
            draw_skin_alpha = 0; /* b309: a skipped batch's alpha must not reach the next draw (MeMeNT0xMoRii's fork) */
            continue;
        }
        prof_begin(PROF_SBIND);
        unsigned key[2];
        if (ge_work_on && !cpu)
            work_batches++, work_wreal += batch->wreal, work_wslotv += batch->bones * batch->vertices, work_rigid += batch->rigid, work_tris += batch->indices / 3;
        const unsigned char *env_entry = env_ge && !cpu ? matfx_env_entry(material) : 0;
        GeCall *slot = 0, *call = !cpu && !env_entry && !sk_recording && skin_call_key(batch, material, modulate, &light, key) ? call_find(batch->call, key, &slot) : 0;
        if (call && call_replay(call, 0)) {
            prof_end(PROF_SBIND);
            draw_skin_alpha = 0;
            continue;
        }
        if (call) /* found but overwritten or no room to refresh: record it again */
            slot = call;
        else if (slot)
            call_misses[0]++;
        const unsigned *from = gu_list->current;
        void **texture = material->texture;
        bind_mask(material->colour >> 24);
        bind_raster(texture ? *texture : 0, GU_REPEAT);
        if (!cpu)
            bind_ge_light(material, modulate, &light);
        bind_texscale(e->packed && !cpu ? batch->uvs : 0); /* the CPU path draws float UVs */
        prof_end(PROF_SBIND);
        prof_begin(PROF_STEST);
        if (cpu)
            skin_batch_cpu(batch, e->lit, e->nrm, e->packed ? e->pack : 0, material, modulate, &light, space, near);
        else if (batch->strip) {
            draw_strip_lens = batch->strip_lens, draw_strip_n = batch->strip_n; /* b250: one PRIM a strip (gu_draw) */
            draw_tested(GU_TRIANGLE_STRIP, batch->vtype, batch->strip_count, batch->strip, batch->v);
            draw_strip_n = 0;
        }
        else
            draw_tested(GU_TRIANGLES, batch->vtype, batch->indices, batch->idx, batch->v);
        if (env_entry) {
            skin_env_ge(env_entry, material, modulate, space, batch);
            light = mesh_state(space, lit); /* blend, alpha test, depth, texture scale for the next batch */
        }
        if (p4_skin_dummy && !cpu)
            *gu_list->current++ = 0xff000000u, skin_dummies++;
        if (slot)
            call_record(slot, key, from, 0);
        prof_end(PROF_STEST);
        draw_skin_alpha = 0;
    }
    bind_texscale(0); /* draws outside mesh mode take (1, 1, 0, 0) for granted */
    st_enable(GU_LIGHTING, 0);
    if (sk_rec) {
        gu_object_stack_depth--;
        sk_recording = 0;
        sk_keep(e, sk_from, gu_list->current, sk);
        if (draws_since_stall >= (unsigned)p4_draw_batch && !gu_object_stack_depth)
            stall_flush();
    }
    prof_end(PROF_SDRAW);
    if (p4_batch_stats) /* the shadow pass: sheared (b186/b192) or the battle shadow's ATEST */
        batch_stats_add(e, shadow_shear_m || (sky_state[3] & 0xfffff) == 0x7f06b, bones_sent - sent_before);
    skin_ge_draws++;
    skin_ge_batches += e->batches;
    return 1;
}

float __sqrt(float x)
{
    float root;
    __asm__ volatile("sqrt.s %0, %1" : "=f"(root) : "f"(x));
    return root;
}

