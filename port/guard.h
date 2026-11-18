/* Matrices, clip coordinates and the guard band (moved from rwsky.c, b322; included once, at the same
   place): classify, guard planes and spheres, TEST_1 passes, guarded and compact draws, box tables, guard results,
   p4_guard_big. */
/* ponytail: every vertex is copied every frame; a cache per geometry when the device shows this in its profile. */


/* b355: the triangles the CPU cuts at the guard band, by the longest model-space edge (log2 buckets: [0] < 2, [k]
   < 2^(k+1), [15] the rest) and by plane (near only, band sides only, both): how small an offline tessellation would
   have to make them for the GE to draw them uncut, as Burnout Legends draws its roads. */
/* b387 guard cut census (p4_guard_census 1): which geometries the CPU cuts, with the longest model-space edge of a
   cut triangle, the nearest view depth (clip w) of its vertices: what an offline tessellation must reach for each
   (Crisis Core: tessellated floor, GE BOUNDINGBOX + BJUMP, no CPU cut). Logged every 256 frames, biggest first. */
int p4_guard_census = 1;
static Geometry *draw_geometry; /* the atomic's geometry while its own arrays are drawn, else 0 (mesh.h sets it) */
enum { GC_SLOTS = 64 };
typedef struct { const Geometry *g; unsigned cuts, frames_seen, last_frame, verts, tris; float edge, depth; char name[24]; } GuardCensus;
static GuardCensus *guard_census;
static void guard_census_note(float edge, const ClipVertex tri[3])
{
    const Geometry *g = draw_geometry;
    if (!g || (!guard_census && !(guard_census = calloc(GC_SLOTS, sizeof *guard_census))))
        return;
    GuardCensus *e = 0, *spare = 0;
    for (int i = 0; i < GC_SLOTS && !e; i++)
        if (guard_census[i].g == g)
            e = &guard_census[i];
        else if (!guard_census[i].g && !spare)
            spare = &guard_census[i];
    if (!e) {
        if (!spare)
            return;
        e = spare;
        memset(e, 0, sizeof *e);
        e->g = g, e->depth = 1e30f, e->verts = g->numVertices, e->tris = g->numTriangles;
        const Mesh *m = (const Mesh *)((const char *)g->mesh + sizeof(MeshHeader) + g->mesh->firstMeshOffset);
        void **texture = m->material ? m->material->texture : 0;
        if (texture)
            memcpy(e->name, (const char *)texture + 0x10, sizeof e->name - 1);
    }
    e->cuts++;
    if (e->last_frame != cache_frame)
        e->last_frame = cache_frame, e->frames_seen++;
    if (edge > e->edge)
        e->edge = edge;
    for (int k = 0; k < 3; k++)
        if (tri[k].c[3] < e->depth)
            e->depth = tri[k].c[3];
}
static void guard_census_report(void)
{
    if (!guard_census)
        return;
    for (int k = 0; k < 16; k++) {
        GuardCensus *best = 0;
        for (int i = 0; i < GC_SLOTS; i++)
            if (guard_census[i].g && guard_census[i].cuts && (!best || guard_census[i].cuts > best->cuts))
                best = &guard_census[i];
        if (!best)
            break;
        p4_log("guard census %s v%u t%u: cuts %u in %u frames, longest edge %d, nearest depth %d\n", best->name[0] ? best->name : "-",
               best->verts, best->tris, best->cuts, best->frames_seen, (int)best->edge, (int)best->depth);
        best->cuts = 0;
    }
    memset(guard_census, 0, GC_SLOTS * sizeof *guard_census);
}

static void guard_cut_stats(const ClipVertex tri[3], unsigned planes)
{
    float longest = 0;
    for (int k = 0; k < 3; k++) {
        const Vertex3D *p = &tri[k].v, *q = &tri[(k + 1) % 3].v;
        float dx = p->x - q->x, dy = p->y - q->y, dz = p->z - q->z, d = dx * dx + dy * dy + dz * dz;
        if (d > longest)
            longest = d;
    }
    int b = 0;
    for (float lim = 4; b < 15 && longest >= lim; lim *= 4) /* squared: 2^(k+1) squared */
        b++;
    guard_cut_edge[b]++;
    if (p4_guard_census)
        guard_census_note(sqrtf(longest), tri);
    if (planes & 1)
        (planes & ~1u) ? guard_cut_both++ : guard_cut_near++;
    else
        guard_cut_side++;
}

typedef float Mat4[4][4]; /* rows; a row vector times the matrix, as sceGuSetMatrix takes it */
static void mat4_from(Mat4 out, const Matrix *m)
{
    const V3 *row[4] = { &m->right, &m->up, &m->at, &m->pos };
    for (int i = 0; i < 4; i++) {
        out[i][0] = row[i]->x; out[i][1] = row[i]->y; out[i][2] = row[i]->z; out[i][3] = i == 3;
    }
}

static void mat4_mul(Mat4 out, Mat4 a, Mat4 b)
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
    memcpy(out, r, sizeof r);
}

static void set_matrix(int which, const Matrix *m)
{
    if (p4_ge_raw && (which == GU_MODEL || which == GU_VIEW)) { /* b365: number word, then 12 floats >> 8 by rows */
        const V3 *row[4] = { &m->right, &m->up, &m->at, &m->pos };
        unsigned cmd = which == GU_MODEL ? 0x3A : 0x3C, *c = gu_list->current;
        *c++ = cmd << 24;
        for (int i = 0; i < 4; i++) {
            union { float f; unsigned u; } x = { row[i]->x }, y = { row[i]->y }, z = { row[i]->z };
            *c++ = (cmd + 1) << 24 | x.u >> 8;
            *c++ = (cmd + 1) << 24 | y.u >> 8;
            *c++ = (cmd + 1) << 24 | z.u >> 8;
        }
        gu_list->current = c;
        return;
    }
    ScePspFMatrix4 out = {
        { m->right.x, m->right.y, m->right.z, 0 }, { m->up.x, m->up.y, m->up.z, 0 },
        { m->at.x, m->at.y, m->at.z, 0 }, { m->pos.x, m->pos.y, m->pos.z, 1 },
    };
    sceGuSetMatrix(which, &out);
}

/* RenderWare's view gives x/z and y/z from 0 to 1, y going down the screen; a parallel camera (b328: Junes floor
   reflection, the "shadow map camera") gives x and y themselves, no divide. */
static void camera_projection(ScePspFMatrix4 *out)
{
    const float near = *(float *)((char *)current_camera + CAMERA_NEAR), far = *(float *)((char *)current_camera + CAMERA_FAR);
    if (*(int *)((char *)current_camera + CAMERA_PROJECTION) == 2) { /* rwPARALLEL */
        ScePspFMatrix4 parallel = {
            { 2, 0, 0, 0 }, { 0, -2, 0, 0 }, { 0, 0, 2 / (far - near), 0 }, { -1, 1, -(far + near) / (far - near), 1 },
        };
        *out = parallel;
        return;
    }
    ScePspFMatrix4 projection = {
        { 2, 0, 0, 0 }, { 0, -2, 0, 0 }, { -1, 1, (far + near) / (far - near), 1 }, { 0, 0, -2 * far * near / (far - near), 0 },
    };
    *out = projection;
}

static void set_camera(void)
{
    if (gu_mesh_mode) { /* the same camera, view and planes as the last mesh in this mode: nothing to send */
        unsigned key[14];
        key[0] = (unsigned)current_camera;
        memcpy(key + 1, (char *)current_camera + CAMERA_VIEW, 12 * 4); /* right, up, at rows (each + a flags/pad word) */
        memcpy(&key[13], (char *)current_camera + CAMERA_NEAR, 4);
        if (gs.camera_known && !memcmp(key, gs.camera_key, sizeof key) &&
            *(unsigned *)((char *)current_camera + CAMERA_FAR) == gs.camera_far &&
            !memcmp((char *)current_camera + CAMERA_VIEW + 48, gs.camera_pos, 12)) {
            kept_cameras++;
            return;
        }
        memcpy(gs.camera_key, key, sizeof key);
        gs.camera_far = *(unsigned *)((char *)current_camera + CAMERA_FAR);
        memcpy(gs.camera_pos, (char *)current_camera + CAMERA_VIEW + 48, 12);
        gs.camera_known = 1;
    }
    ScePspFMatrix4 projection;
    camera_projection(&projection);
    sceGuSetMatrix(GU_PROJECTION, &projection);
    set_matrix(GU_VIEW, (const Matrix *)((char *)current_camera + CAMERA_VIEW));
}

/* Clip coordinates and outcodes of an array's vertices; returns the OR of the outcodes (0: the GE takes it all). */
static unsigned guard_classify_body(const V3 *position, int count, const Matrix *model, float near, const float *sphere);
static unsigned guard_classify(const V3 *position, int count, const Matrix *model, float near, const float *sphere)
{
    prof_begin(PROF_GUARD);
    unsigned r = guard_classify_body(position, count, model, near, sphere) & GUARD_MASK;
    prof_end(PROF_GUARD);
    return r;
}

static unsigned hash_words(unsigned h, const void *data, int bytes);

/* Clip coordinates of count vertices into c (16-byte aligned), outcodes into code; returns their OR.
   M (columns m[0..3]) gives x y z w; D gives the four side distances G w -+ x, G w -+ y, so their sign bits are outcode
   bits 1-4; bit 0 compares w with near as integers (near > 0: a negative w has its sign bit, so it is smaller). */
static unsigned guard_vfpu(const V3 *position, int count, const float m[4][4], float near, float (*c)[4], unsigned char *code, int stride)
{
    float mm[16] __attribute__((aligned(16))), dd[16] __attribute__((aligned(16)));
    static const float coef[4][4] = { { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            mm[i * 4 + k] = m[i][k];
            dd[i * 4 + k] = m[i][0] * coef[k][0] + m[i][1] * coef[k][1] + m[i][2] * coef[k][2] + m[i][3] * coef[k][3];
        }
    union { float f; int i; } nb = { near };
    unsigned any = 0;
    if (count <= 0)
        return 0;
    const V3 *pos = position;
    int n = count, t0, t1, t2;
    __asm__ volatile(
        ".set push\n.set noreorder\n"
        "lv.q    C400, 0(%[mm])\n"
        "lv.q    C410, 16(%[mm])\n"
        "lv.q    C420, 32(%[mm])\n"
        "lv.q    C430, 48(%[mm])\n"
        "lv.q    C500, 0(%[dd])\n"
        "lv.q    C510, 16(%[dd])\n"
        "lv.q    C520, 32(%[dd])\n"
        "lv.q    C530, 48(%[dd])\n"
        "vone.s  S613\n"
        "1:\n"
        "lv.s    S610, 0(%[pos])\n"
        "lv.s    S611, 4(%[pos])\n"
        "lv.s    S612, 8(%[pos])\n"
        "vtfm4.q C700, E400, C610\n"
        "vtfm4.q C710, E500, C610\n"
        "sv.q    C700, 0(%[c])\n"
        "mfv     %[t0], S703\n"
        "slt     %[t0], %[t0], %[nb]\n"
        "mfv     %[t1], S710\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 1\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t1], S711\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 2\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t1], S712\n"
        "srl     %[t1], %[t1], 31\n"
        "sll     %[t1], %[t1], 3\n"
        "or      %[t0], %[t0], %[t1]\n"
        "mfv     %[t2], S713\n"
        "srl     %[t2], %[t2], 31\n"
        "sll     %[t2], %[t2], 4\n"
        "or      %[t0], %[t0], %[t2]\n"
        "sb      %[t0], 0(%[code])\n"
        "or      %[any], %[any], %[t0]\n"
        "addu    %[pos], %[pos], %[st]\n"
        "addiu   %[c], %[c], 16\n"
        "addiu   %[n], %[n], -1\n"
        "bnez    %[n], 1b\n"
        "addiu   %[code], %[code], 1\n"
        ".set pop\n"
        : [pos] "+r"(pos), [c] "+r"(c), [code] "+r"(code), [n] "+r"(n), [any] "+r"(any), [t0] "=&r"(t0), [t1] "=&r"(t1), [t2] "=&r"(t2)
        : [mm] "r"(mm), [dd] "r"(dd), [nb] "r"(nb.i), [st] "r"(stride)
        : "memory");
    return any;
}

/* Inputs of the last all-inside classify per array: the same arrays, matrices and near plane give the same answer. */
enum { GUARD_MEMO = 1024 };
static struct { const V3 *position; unsigned key; } *guard_memo; /* b299: same 1024 entries, heap to leave fixed-image space */

/* model (0: the world) times view times projection: model space to clip space */
static void guard_matrix(Mat4 m, const Matrix *model)
{
    Mat4 view, projection;
    ScePspFMatrix4 p;
    camera_projection(&p);
    memcpy(projection, &p, sizeof projection);
    mat4_from(view, (const Matrix *)((char *)current_camera + CAMERA_VIEW));
    if (model) {
        mat4_from(m, model);
        mat4_mul(m, m, view);
    } else
        memcpy(m, view, sizeof(Mat4)); /* m is a parameter here: sizeof m would be a pointer's 4 bytes */
    mat4_mul(m, m, projection);
}

/* sphere (x y z r in model space) wholly in front of the near plane and inside the guard band */
static int guard_sphere_inside(Mat4 m, const float *sphere, float near)
{
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    float c[4];
    for (int k = 0; k < 4; k++)
        c[k] = sphere[0] * m[0][k] + sphere[1] * m[1][k] + sphere[2] * m[2][k] + m[3][k];
    int inside = 1;
    for (int plane = p4_guard_gnear ? 1 : 0; plane < 5 && inside; plane++) {
        float d = plane ? 0 : -near, g[3] = { 0, 0, 0 };
        for (int k = 0; k < 4; k++) {
            d += coef[plane][k] * c[k];
            for (int i = 0; i < 3; i++)
                g[i] += coef[plane][k] * m[i][k];
        }
        inside = d > sphere[3] * sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    }
    return inside;
}

/* guard_sphere_inside with the planes taken out: plane p as a 4-vector P (a point x is inside when x.P + P[3] > 0,
   the near plane carrying -near) and its normal's length. Built once per skinned draw instead of once per bone. */
static void guard_sphere_planes(Mat4 m, float near, float P[5][4], float len[5])
{
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    for (int p = 0; p < 5; p++) {
        for (int i = 0; i < 4; i++)
            P[p][i] = coef[p][0] * m[i][0] + coef[p][1] * m[i][1] + coef[p][2] * m[i][2] + coef[p][3] * m[i][3];
        if (!p)
            P[p][3] -= near;
        len[p] = sqrtf(P[p][0] * P[p][0] + P[p][1] * P[p][1] + P[p][2] * P[p][2]);
    }
}
static int guard_sphere_inside_planes(float P[5][4], const float len[5], const float *sphere)
{
    for (int p = p4_guard_gnear ? 1 : 0; p < 5; p++)
        if (!(sphere[0] * P[p][0] + sphere[1] * P[p][1] + sphere[2] * P[p][2] + P[p][3] > sphere[3] * len[p]))
            return 0;
    return 1;
}
static void anim_seen_at(const void *hierarchy, float depth, const Matrix *model);

/* The guard planes (near, then the four sides of GUARD x the view) as world-space 4-vectors: a world point p is inside
   plane k when W[k] . (p, 1) > 0 (near: > near). guard_sphere_inside builds model x view x projection for every array
   (b86 Heaven: 200 calls, 10.5 k cycles each); these change only with the camera. */
static struct { unsigned key[18]; float w[5][4], len[5]; Mat4 vp; int known; } guard_world;
static void guard_world_planes(void)
{
    unsigned key[18];
    key[0] = (unsigned)current_camera;
    memcpy(key + 1, (char *)current_camera + CAMERA_VIEW, 15 * 4); /* right, up, at (each + a pad word), pos */
    memcpy(key + 16, (char *)current_camera + CAMERA_NEAR, 4);
    memcpy(key + 17, (char *)current_camera + CAMERA_FAR, 4);
    if (guard_world.known && !memcmp(key, guard_world.key, sizeof key))
        return;
    memcpy(guard_world.key, key, sizeof key);
    guard_world.known = 1;
    static const float coef[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    Mat4 vp;
    guard_matrix(vp, 0);
    memcpy(guard_world.vp, vp, sizeof vp);
    for (int k = 0; k < 5; k++) {
        float *w = guard_world.w[k];
        for (int j = 0; j < 4; j++)
            w[j] = vp[j][0] * coef[k][0] + vp[j][1] * coef[k][1] + vp[j][2] * coef[k][2] + vp[j][3] * coef[k][3];
        guard_world.len[k] = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    }
}
/* The sphere (model space) wholly inside every plane. The model's largest row length scales the radius, so a
   non-uniform scale only makes the answer "not inside" more often (the vertices are then looked at). */
static int guard_sphere_world(const Matrix *model, const float *sphere, float near)
{
    float x = sphere[0], y = sphere[1], z = sphere[2], r = sphere[3];
    if (model) {
        const Matrix *m = model;
        float wx = x * m->right.x + y * m->up.x + z * m->at.x + m->pos.x;
        float wy = x * m->right.y + y * m->up.y + z * m->at.y + m->pos.y;
        float wz = x * m->right.z + y * m->up.z + z * m->at.z + m->pos.z;
        float a = m->right.x * m->right.x + m->right.y * m->right.y + m->right.z * m->right.z;
        float b = m->up.x * m->up.x + m->up.y * m->up.y + m->up.z * m->up.z;
        float c = m->at.x * m->at.x + m->at.y * m->at.y + m->at.z * m->at.z;
        /* b309: bound the squared stretch by the largest absolute row sum of the Gram matrix; row lengths alone
           underestimate a sheared model (fix from MeMeNT0xMoRii's fork) */
        float ab = fabsf(m->right.x * m->up.x + m->right.y * m->up.y + m->right.z * m->up.z);
        float ac = fabsf(m->right.x * m->at.x + m->right.y * m->at.y + m->right.z * m->at.z);
        float bc = fabsf(m->up.x * m->at.x + m->up.y * m->at.y + m->up.z * m->at.z);
        a += ab + ac;
        b += ab + bc;
        c += ac + bc;
        float big = a > b ? a : b;
        r *= sqrtf(big > c ? big : c);
        x = wx, y = wy, z = wz;
    }
    for (int k = p4_guard_gnear ? 1 : 0; k < 5; k++) {
        const float *w = guard_world.w[k];
        float d = w[0] * x + w[1] * y + w[2] * z + w[3] - (k ? 0 : near);
        if (!(d > r * guard_world.len[k]))
            return 0;
    }
    return 1;
}

/* guard_matrix with view x projection kept per camera (b97 Heaven: 91 classifies a frame rebuilt it each time). */
static void guard_matrix_cached(Mat4 m, const Matrix *model)
{
    guard_world_planes();
    if (model) {
        mat4_from(m, model);
        mat4_mul(m, m, guard_world.vp);
    } else
        memcpy(m, guard_world.vp, sizeof(Mat4));
}

static unsigned guard_classify_body(const V3 *position, int count, const Matrix *model, float near, const float *sphere)
{
    if (p4_guard_world && sphere && sphere[3] > 0) {
        guard_world_planes();
        int inside = guard_sphere_world(model, sphere, near);
        if (p4_guard_check && !(guard_world_checks++ & 63)) { /* the b88 test on the same sphere */
            Mat4 check;
            guard_matrix(check, model);
            int old = guard_sphere_inside(check, sphere, near);
            guard_world_unsafe += inside && !old;
            guard_world_stricter += !inside && old;
        }
        if (inside) {
            guard_spheres++;
            return 0;
        }
    }
    Mat4 m;
    guard_matrix_cached(m, model);
    unsigned memo_key = 0, memo_slot = 0;
    if (p4_guard_memo && sphere && (guard_memo || (guard_memo = calloc(GUARD_MEMO, sizeof *guard_memo)))) { /* static arrays only: skinned ones change in place */
        unsigned h = hash_words(2166136261u, m, sizeof m);
        h = hash_words(h, &near, 4);
        h = hash_words(h, &count, 4);
        memo_key = h | 1;
        memo_slot = ((unsigned)position >> 4) * 2654435761u >> 22;
        if (guard_memo[memo_slot].position == position && guard_memo[memo_slot].key == memo_key) {
            guard_memo_hits++;
            return 0;
        }
    }
    if (!p4_guard_world && sphere && sphere[3] > 0) { /* the bounding sphere wholly inside the band: no vertex needs a look */
        if (guard_sphere_inside(m, sphere, near)) {
            guard_spheres++;
            if (memo_key) {
                guard_memo[memo_slot].position = position;
                guard_memo[memo_slot].key = memo_key;
            }
            return 0;
        }
    }
    unsigned any = 0;
    guard_vertices += count;
    if (p4_guard_fast == 2) {
        any = guard_vfpu(position, count, (const float (*)[4])m, near, guard_clip, guard_code, sizeof *position);
        if (p4_guard_check && !(guard_check_runs++ & 63)) {
            for (int i = 0; i < count; i++) {
                const float *c = guard_clip[i];
                float x = position[i].x, y = position[i].y, z = position[i].z, w = x * m[0][3] + y * m[1][3] + z * m[2][3] + m[3][3];
                float cx = x * m[0][0] + y * m[1][0] + z * m[2][0] + m[3][0], cy = x * m[0][1] + y * m[1][1] + z * m[2][1] + m[3][1];
                float ref[4] = { cx, cy, 0, w };
                unsigned want = guard_outcode(ref, near);
                float err = fabsf(c[0] - cx) + fabsf(c[1] - cy) + fabsf(c[3] - w);
                if (want != guard_code[i] || err > 1e-3f * (fabsf(w) + fabsf(cx) + fabsf(cy) + 1))
                    guard_check_diffs++;
            }
        }
        if (!any && memo_key) {
            guard_memo[memo_slot].position = position;
            guard_memo[memo_slot].key = memo_key;
        }
        return any;
    }
    if (!p4_guard_fast) {
        for (int i = 0; i < count; i++) {
            float x = position[i].x, y = position[i].y, z = position[i].z, *c = guard_clip[i];
            for (int k = 0; k < 4; k++)
                c[k] = x * m[0][k] + y * m[1][k] + z * m[2][k] + m[3][k];
            any |= guard_code[i] = guard_outcode(c, near);
        }
        return any;
    }
    /* b72: 185 cycles a vertex through the loops and the per-plane switch. Same arithmetic, same order. */
    const float m00 = m[0][0], m01 = m[0][1], m02 = m[0][2], m03 = m[0][3];
    const float m10 = m[1][0], m11 = m[1][1], m12 = m[1][2], m13 = m[1][3];
    const float m20 = m[2][0], m21 = m[2][1], m22 = m[2][2], m23 = m[2][3];
    const float m30 = m[3][0], m31 = m[3][1], m32 = m[3][2], m33 = m[3][3];
    const V3 *pos = position;
    float (*c)[4] = guard_clip;
    unsigned char *code = guard_code;
    for (int i = 0; i < count; i++, pos++, c++) {
        float x = pos->x, y = pos->y, z = pos->z;
        float cx = x * m00 + y * m10 + z * m20 + m30;
        float cy = x * m01 + y * m11 + z * m21 + m31;
        float cz = x * m02 + y * m12 + z * m22 + m32;
        float cw = x * m03 + y * m13 + z * m23 + m33;
        (*c)[0] = cx; (*c)[1] = cy; (*c)[2] = cz; (*c)[3] = cw;
        float g = GUARD * cw;
        unsigned k = (cw - near < 0) | (g - cx < 0) << 1 | (g + cx < 0) << 2 | (g - cy < 0) << 3 | (g + cy < 0) << 4;
        code[i] = k;
        any |= k;
    }
    if (!any && memo_key) {
        guard_memo[memo_slot].position = position;
        guard_memo[memo_slot].key = memo_key;
    }
    return any;
}

/* The game's TEST_1 (RpSkyRenderStateSet 3): alpha test method and reference, and what a failing pixel still writes.
   P4 draws its fog with ATST GEQUAL 0x7F, AFAIL FB_ONLY: translucent texels paint colour but leave the depth alone.
   Drawn with depth writes everywhere, one fog layer hid the next in hard rectangles (first battle). Two passes split
   the pixels by the same test, so the fill stays about one pass. */
int p4_gs_test = 1; /* 0: fixed alpha test GREATER 8 for every 3D mesh, as b64 */
/* The second pass costs a whole second fill even when no pixel can fail: b76 Heaven 151 ms a frame with it, 101 ms
   with p4_gs_test 0 (GE wait 54 -> 9 ms). Skip it when it cannot change a pixel: every pixel passes (smallest
   texture alpha x smallest vertex alpha reaches the reference), or FB_ONLY without depth writes (both passes paint
   the same colour: one pass without the test), or ZB_ONLY without depth writes (pass 2 writes nothing). */
extern int p4_alpha_x2;
int p4_gs_test_raw_prelit = 0; /* 1: raw reference for prelit arrays (experiment, b93 Heaven sky: no visible change) */
int p4_gs_test_skip = 1; /* 0: always two passes, as b76 */
static const unsigned *draw_prelit; /* the mesh's source alpha for draw_tested; draw_alpha_known 0: not known */
static int draw_count, draw_alpha_known;
/* b177 (p4_material_state 2): an unlit compact mesh whose vertices leave the material colour out; draw_mesh_vertices
   sends it as GE state (lighting on, no light: vertex colour x ambient), as the GE-skinned unlit batches do */
static int draw_material_ge;
static int draw_prelit_min = -1;
static unsigned draw_material_alpha = 255;
static inline unsigned double_colour(unsigned c);
static int vertex_alpha_min(void)
{
    if (!draw_alpha_known)
        return 0;
    if (draw_prelit_min < 0) {
        unsigned m = 255;
        if (draw_prelit)
            for (int j = 0; j < draw_count && m; j++) {
                unsigned a = double_colour(draw_prelit[j]) >> 24;
                if (a < m)
                    m = a;
            }
        draw_prelit_min = m;
    }
    unsigned t = draw_prelit_min * draw_material_alpha + 128;
    return (t + (t >> 8)) >> 8; /* modulate_colour */
}
/* TEST_1 by the mesh's own vertices (b96 Heaven: the geometry-wide smallest prelit alpha was 0 for ~200 meshes a
   frame with opaque textures, so each drew twice). A pixel's alpha lies between texture min x vertex min and texture
   max x vertex max (Gouraud and filtering only interpolate): all pass -> one pass; all fail -> only what pass two
   draws. Exact, no picture change. */
int p4_gs_mesh_alpha = 1; /* 0: the geometry-wide prelit minimum only, as b96 */
static int draw_vertex_alpha(int vtype, int count, const void *indices, const void *vertices, unsigned *lo, unsigned *hi)
{
    if (draw_skin_alpha) {
        *lo = draw_skin_alpha[0], *hi = draw_skin_alpha[1];
        return 1;
    }
    if ((vtype & GU_VERTEX_BITS) != GU_VERTEX_32BITF || (vtype & GU_COLOR_BITS) != GU_COLOR_8888 || (vtype & GU_WEIGHT_BITS) ||
        ((vtype & GU_INDEX_BITS) && (vtype & GU_INDEX_BITS) != GU_INDEX_16BIT) || !vertices || count <= 0)
        return 0;
    /* the bounds of the vertex colours alone (b177: kept so in cache entries, whose key no longer has the material) */
    unsigned m = 255, big = 0;
    if (draw_slot_v == vertices && draw_slot_alpha[0] >= 0) {
        m = draw_slot_alpha[0], big = draw_slot_alpha[1];
    } else if (draw_fixed_alpha) { /* a subset (or cut pieces) of a cache entry whose bounds are known: they still bound it */
        m = draw_fixed_alpha[0], big = draw_fixed_alpha[1];
    } else {
        tested_scans++;
        int short_uv = (vtype & GU_TEXTURE_BITS) == GU_TEXTURE_16BIT;
        int stride = (vtype & GU_NORMAL_BITS ? sizeof(Vertex3DN) : sizeof(Vertex3D)) - (short_uv ? 4 : 0);
        const unsigned short *ix = (vtype & GU_INDEX_BITS) ? indices : 0;
        for (int k = 0; k < count; k++) {
            unsigned a = *(const unsigned *)((const char *)vertices + (ix ? ix[k] : k) * stride + (short_uv ? 4 : 8)) >> 24;
            if (a < m) m = a;
            if (a > big) big = a;
        }
        if (draw_slot_v == vertices)
            draw_slot_alpha[0] = m, draw_slot_alpha[1] = big;
    }
    if ((vtype & GU_NORMAL_BITS) || draw_material_ge) { /* GE-lit: the vertex colour is the ambient term, the material alpha comes on top */
        m = m * draw_material_alpha / 255;
        big = (big * draw_material_alpha + 254) / 255;
    }
    *lo = m, *hi = big;
    return 1;
}
static unsigned draw_alpha_reference(unsigned test)
{
    int raw = p4_gs_test_raw_prelit && draw_alpha_known && draw_prelit && !p4_alpha_x2;
    unsigned ref = (test >> 4 & 0xff) << !raw;
    return ref > 255 ? 255 : ref;
}
static void draw_tested(int prim, int vtype, int count, const void *indices, const void *vertices)
{
    unsigned test = sky_state[3];
    if (!p4_gs_test || !(test & 1)) {
        if (gu_mesh_mode)
            st_alpha_func(GU_GREATER, 8); /* mesh mode leaves the last tested draw's function in place */
        gu_draw(prim, vtype, count, indices, vertices);
        return;
    }
    tested_draws++;
    static const int method[8] = { GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_EQUAL, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL };
    static const int inverse[8] = { GU_ALWAYS, GU_NEVER, GU_GEQUAL, GU_GREATER, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_EQUAL };
    int atst = test >> 1 & 7, afail = test >> 12 & 3;
    /* The GS's 0x80 is our 0xff in texels and in lit or white vertices. Prelit alpha reaches the GE as the GS got it
       (p4_alpha_x2 0), so there the reference stays: doubled, Heaven's clouds (alpha 0x7f-0xfd) failed, wrote no
       depth, and the night01 dome drawn after them painted them out. */
    int aref = (int)draw_alpha_reference(test);
    st_enable(GU_ALPHA_TEST, 1);
    st_alpha_func(method[atst], aref);
    /* failing pixels: nothing to add when they are dropped, or are only the alpha-0 ones under a blend */
    int only_clear = (atst == 5 || atst == 6) && aref == 0;
    int second = afail != 0 && atst != 1 && !only_clear;
    if (second && p4_gs_test_skip) {
        int zwrite = render_state[RS_ZWRITE] != 0;
        if ((afail == 1 || afail == 2) && !zwrite) {
            tested_nodepth++;
            second = 0;
            if (afail == 1) { /* both halves painted: the whole mesh, untested */
                st_alpha_func(GU_ALWAYS, 0);
                gu_draw(prim, vtype, count, indices, vertices);
                if (!gu_mesh_mode)
                    sceGuAlphaFunc(GU_GREATER, 8, 0xff);
                return;
            }
        } else if (atst == 5 || atst == 6) {
            /* the geometry-wide prelit minimum scans every vertex of the geometry: only when the mesh's own bounds are
               not known (b122 Heaven: it was computed for every geometry, then replaced by them) */
            unsigned t = bound_alpha_min, v, vhi = 255;
            unsigned lo, hi;
            if (p4_gs_mesh_alpha && draw_alpha_known && draw_vertex_alpha(vtype, count, indices, vertices, &lo, &hi))
                v = lo, vhi = hi;
            else
                v = vertex_alpha_min();
            unsigned lowest = t == 255 && v == 255 ? 255 : t * v >> 8;
            unsigned highest = bound_alpha_max * vhi + 254;
            highest = (highest + (highest >> 8)) >> 8 > 255 ? 255 : (highest + (highest >> 8)) >> 8; /* rounded up */
            if (atst == 5 ? lowest >= (unsigned)aref : lowest > (unsigned)aref) {
                tested_opaque++;
                second = 0;
            } else if (p4_gs_mesh_alpha && afail == 1 && (atst == 5 ? highest < (unsigned)aref : highest <= (unsigned)aref)) {
                tested_allfail++; /* every pixel fails: FB_ONLY paints them all, no depth */
                st_alpha_func(GU_ALWAYS, 0);
                st_depth_mask(GU_TRUE);
                gu_draw(prim, vtype, count, indices, vertices);
                st_depth_mask(render_state[RS_ZWRITE] ? GU_FALSE : GU_TRUE);
                if (!gu_mesh_mode)
                    sceGuAlphaFunc(GU_GREATER, 8, 0xff);
                return;
            }
        }
    }
    gu_draw(prim, vtype, count, indices, vertices);
    if (second) {
        tested_second++;
        st_alpha_func(inverse[atst], aref);
        if (afail == 2) {
            st_pixel_mask(0xffffffff); /* ZB_ONLY: depth, no colour */
        } else {
            st_depth_mask(GU_TRUE); /* FB_ONLY, RGB_ONLY: colour, no depth */
            if (afail == 3)
                st_pixel_mask(pixel_mask | 0xff000000);
        }
        gu_draw(prim, 0, count, indices, vertices); /* the first pass set this vertex format (fork, home24) */
        st_pixel_mask(pixel_mask);
        st_depth_mask(render_state[RS_ZWRITE] ? GU_FALSE : GU_TRUE);
    }
    if (!gu_mesh_mode)
        sceGuAlphaFunc(GU_GREATER, 8, 0xff);
}

static void draw_mesh_vertices(int prim, int vtype, int count, const void *indices, const void *vertices, const Material *material, int modulate, const SceneLight *light);
static int census3d_skip(const Material *material, int vtype, int count, int modulate);
static inline signed char normal_s8(float c);

/* A mesh whose array has corners beyond the guard band: inside triangles stay indexed, the rest are cut on the CPU.
   vertices: Vertex3D (lit on the CPU) or, with GU_NORMAL_8BIT in vtype, Vertex3DN that the GE lights; the cut
   corners carry the interpolated normal, so cut triangles are lit by the GE like the rest of the mesh. */
/* Codes computed on demand (only the vertices of partly visible chunks): the FPU form of guard_vfpu. */
typedef struct { float m[4][4]; const char *pos; int stride; } GuardLazy;
static unsigned short *guard_stamp, guard_gen = 1;
static inline void guard_code_lazy(const GuardLazy *z, int i, float near)
{
    if (guard_stamp[i] == guard_gen)
        return;
    guard_stamp[i] = guard_gen;
    guard_lazy_vertices++;
    const float *p = (const float *)(z->pos + i * z->stride);
    float x = p[0], y = p[1], w = p[2], *c = guard_clip[i];
    for (int k = 0; k < 4; k++)
        c[k] = x * z->m[0][k] + y * z->m[1][k] + w * z->m[2][k] + z->m[3][k];
    float g = GUARD * c[3];
    guard_code[i] = (c[3] - near < 0) | (g - c[0] < 0) << 1 | (g + c[0] < 0) << 2 | (g - c[1] < 0) << 3 | (g + c[1] < 0) << 4;
}

/* chunk_class (one byte per run of chunk_tris triangles): 0 inside the band (kept without a look), 1 per triangle,
   2 off screen (dropped); 0 = no chunks: every triangle per vertex codes. lazy: codes not computed yet. */
/* One guarded draw's final geometry, captured before submission (b236). */
typedef struct { const unsigned short *idx; const void *cut; int nkept, ncut, valid; } GuardCapture;
static GuardCapture *guard_capture;
extern int p4_guard_loop2;
static void draw_guarded_chunks(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light,
                                const unsigned char *chunk_class, int chunk_tris, const GuardLazy *lazy);
static void draw_guarded(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light)
{
    draw_guarded_chunks(prim, idx, nidx, vertices, vtype, near, material, modulate, light, 0, 0, 0);
}
extern int p4_guard_loop2;
static void draw_guarded_chunks(int prim, const unsigned short *idx, int nidx, const void *vertices, int vtype, float near, const Material *material, int modulate, const SceneLight *light,
                                const unsigned char *chunk_class, int chunk_tris, const GuardLazy *lazy)
{
    int lit = (vtype & GU_NORMAL_8BIT) != 0, stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    int triangles = prim == GU_TRIANGLE_STRIP ? nidx - 2 : nidx / 3;
    if (triangles <= 0)
        return;
    /* b309 (fix from MeMeNT0xMoRii's fork): no worst-case reservation that refused the whole mesh. Each write is
       checked, and only when the worst case could overflow; a mesh that really runs out submits nothing. */
    int index_space = MAX_INDICES - index_used;
    int cut_space = (MAX_VERTICES - vertex_used) * (int)sizeof(Vertex3D) / stride;
    int check_space = triangles > index_space / 3 || triangles > cut_space / GUARD_OUT_MAX;
    unsigned short *kept = index_pool + index_used;
    char *cut = (char *)(vertex_pool + vertex_used);
    int nkept = 0, ncut = 0;
    guard_meshes++;
    prof_begin(PROF_GLOOP);
    /* b143: chunk by chunk (one class lookup, no divide per triangle); a chunk inside the band of a triangle list is
       its own index range, copied whole (p4_guard_loop2). The order of the kept indices is unchanged. */
    int per = chunk_class ? chunk_tris : triangles;
    for (int first = 0; first < triangles; first += per) {
    int last = first + per < triangles ? first + per : triangles;
    int cls = chunk_class ? chunk_class[first / per] : 1;
    if (cls == 2) /* the whole run off screen */
        continue;
    if (cls == 0 && prim != GU_TRIANGLE_STRIP && p4_guard_loop2) {
        int count = (last - first) * 3;
        if (check_space && count > index_space - nkept)
            goto pool_exhausted;
        memcpy(kept + nkept, idx + first * 3, count * sizeof *kept);
        nkept += count;
        continue;
    }
    for (int t = first; t < last; t++) {
        int a, b, c;
        if (prim == GU_TRIANGLE_STRIP) {
            a = idx[t]; b = idx[t + 1]; c = idx[t + 2];
            if (a == b || b == c || a == c)
                continue;
        } else {
            a = idx[t * 3]; b = idx[t * 3 + 1]; c = idx[t * 3 + 2];
        }
        if (cls == 0)
            goto keep_triangle;
        if (lazy) {
            guard_code_lazy(lazy, a, near);
            guard_code_lazy(lazy, b, near);
            guard_code_lazy(lazy, c, near);
        }
        unsigned ca = guard_code[a], cb = guard_code[b], cc = guard_code[c], mask = GUARD_MASK;
        if (!((ca | cb | cc) & mask))
            goto keep_triangle;
        if (ca & cb & cc & mask) { /* wholly behind the CPU clip plane or beyond one side of the band (b309: bits
                                      outside the mask no longer drop it; MeMeNT0xMoRii's fork) */
            guard_dropped++;
            continue;
        }
        { /* wholly beyond one edge of the screen (x or y past +-w): invisible, nothing to cut (PS2RC drops these too) */
            const float *pa = guard_clip[a], *pb = guard_clip[b], *pc = guard_clip[c];
            if ((pa[0] > pa[3] && pb[0] > pb[3] && pc[0] > pc[3]) || (pa[0] < -pa[3] && pb[0] < -pb[3] && pc[0] < -pc[3]) ||
                (pa[1] > pa[3] && pb[1] > pb[3] && pc[1] > pc[3]) || (pa[1] < -pa[3] && pb[1] < -pb[3] && pc[1] < -pc[3])) {
                guard_offscreen++;
                continue;
            }
        }
        if (p4_guard_near_ge && ((ca | cb | cc) & 1)) {
            guard_near_ge++;
            goto keep_triangle;
        }
        prof_begin(PROF_GCLIP);
        ClipVertex tri[3];
        int corner[3] = { a, b, c };
        for (int k = 0; k < 3; k++) {
            memcpy(tri[k].c, guard_clip[corner[k]], sizeof tri[k].c);
            if (lit) {
                const Vertex3DN *p = (const Vertex3DN *)vertices + corner[k];
                tri[k].v = (Vertex3D){ p->u, p->v, p->colour, p->x, p->y, p->z };
                tri[k].n[0] = p->nx; tri[k].n[1] = p->ny; tri[k].n[2] = p->nz;
            } else {
                tri[k].v = ((const Vertex3D *)vertices)[corner[k]];
                tri[k].n[0] = tri[k].n[1] = tri[k].n[2] = 0;
            }
        }
        Vertex3D out[GUARD_OUT_MAX];
        float normal[GUARD_OUT_MAX][3];
        unsigned planes = (ca | cb | cc) & mask;
        int n = p4_guard_clip_fast ? clip_triangle_guard_fast(tri, near, out, lit ? normal : 0, planes)
                                   : clip_triangle_guard(tri, near, out, lit ? normal : 0, planes);
        if (p4_guard_clip_fast && p4_guard_clip_check && !(guard_clip_checks++ & 63)) {
            Vertex3D ref[GUARD_OUT_MAX];
            float refn[GUARD_OUT_MAX][3];
            int rn = clip_triangle_guard(tri, near, ref, lit ? refn : 0, planes);
            if (rn != n || memcmp(ref, out, n * sizeof *out) || (lit && memcmp(refn, normal, n * sizeof *normal)))
                guard_clip_diffs++;
        }
        if (check_space && n > cut_space - ncut) {
            prof_end(PROF_GCLIP);
            goto pool_exhausted;
        }
        for (int k = 0; k < n; k++, ncut++) {
            if (lit) { /* the interpolated normal in s8 units again; the GE divides by its length */
                Vertex3DN *v = (Vertex3DN *)cut + ncut;
                *v = (Vertex3DN){ out[k].u, out[k].v, out[k].colour, normal_s8(normal[k][0] / 127), normal_s8(normal[k][1] / 127), normal_s8(normal[k][2] / 127), 0, out[k].x, out[k].y, out[k].z };
            } else {
                ((Vertex3D *)cut)[ncut] = out[k];
            }
        }
        guard_clipped++;
        guard_cut_stats(tri, planes);
        prof_end(PROF_GCLIP);
        continue;

    keep_triangle:
        if (check_space && index_space - nkept < 3)
            goto pool_exhausted;
        kept[nkept++] = a;
        kept[nkept++] = b;
        kept[nkept++] = c;
    }
    }
    prof_end(PROF_GLOOP);
    if (check_space && (nkept || ncut))
        guard_pool_recovered++;
    if (guard_capture) {
        guard_capture->idx = kept; guard_capture->cut = cut;
        guard_capture->nkept = nkept; guard_capture->ncut = ncut; guard_capture->valid = 1;
    }
    prof_begin(PROF_GSUB);
    if (nkept) {
        index_used += nkept;
        sceKernelDcacheWritebackRange(kept, nkept * 2);
        draw_mesh_vertices(GU_TRIANGLES, vtype, nkept, kept, vertices, material, modulate, light);
    }
    if (ncut) {
        vertex_used += (ncut * stride + sizeof(Vertex3D) - 1) / sizeof(Vertex3D);
        sceKernelDcacheWritebackRange(cut, ncut * stride);
        draw_mesh_vertices(GU_TRIANGLES, vtype & ~GU_INDEX_16BIT, ncut, 0, cut, material, modulate, light);
    }
    prof_end(PROF_GSUB);
    return;

pool_exhausted:
    /* nothing was submitted: the next mesh can still use the unused ranges of both pools */
    prof_end(PROF_GLOOP);
    guard_pool_exhausted++;
}

/* A guarded mesh drawn from its compact arrays (cached or just emitted): classify only the vertices it uses, in their
   own numbering, then draw it whole or cut what crosses the band. Overwrites guard_clip and guard_code. */
enum { GUARD_BOX_SLOTS = 256, GUARD_MAX_CHUNKS = 4096 };
/* triangles a chunk (b143: a switch; 32 before). b142 Heaven per frame: 65 chunks inside, 78 partial, 57 off screen */
int p4_guard_chunk = 8, p4_guard_loop2 = 1; /* b143 Heaven CPU: 32 -> 57.1 ms, 16 -> 56.6, 8 -> 56.1 */
static int guard_chunk_now(void) { return p4_guard_chunk < 8 ? 8 : p4_guard_chunk > 256 ? 256 : p4_guard_chunk; }
/* model-space boxes (min xyz, max xyz) of runs of GUARD_CHUNK triangles of a cache entry, built from the entry's own
   vertices, so they always match what the entry draws; valid while the entry keeps its key and build frame */
typedef struct { const void *slot; unsigned key, built, used; int n, cap, chunk; float *box, *rad; } GuardBoxes;
static unsigned guard_box_tick;
static GuardBoxes *guard_boxes;
static const float *guard_slot_boxes(const void *slot, unsigned key, unsigned built, int prim, const unsigned short *idx, int nidx,
                                     const void *vertices, int vtype, int *count, const float **rad)
{
    int chunk = guard_chunk_now();
    int tris = prim == GU_TRIANGLE_STRIP ? nidx - 2 : nidx / 3, n = (tris + chunk - 1) / chunk;
    if (tris <= 0 || n > GUARD_MAX_CHUNKS)
        return 0;
    if (!guard_boxes && !(guard_boxes = calloc(GUARD_BOX_SLOTS, sizeof *guard_boxes)))
        return 0;
    /* b199: 4-way sets (b169-b198 Heaven/dialogue: exactly 2 rebuilds a frame, maybe two meshes evicting each other
       in the direct-mapped table); a miss takes an empty way of the set, else the least recently used one */
    GuardBoxes *set = &guard_boxes[(((unsigned)slot >> 4) * 2654435761u >> 24) & ~3u], *e = 0;
    *count = n;
    guard_box_tick++;
    for (int w = 0; w < 4 && !e; w++)
        if (set[w].slot == slot)
            e = &set[w];
    if (e && e->key == key && e->built == built && e->n == n && e->chunk == chunk && e->box) {
        e->used = guard_box_tick;
        *rad = e->rad;
        return e->box;
    }
    if (e)
        guard_box_same++;
    else {
        e = set;
        for (int w = 0; w < 4; w++) {
            if (!set[w].slot) {
                e = &set[w];
                break;
            }
            if (set[w].used < e->used)
                e = &set[w];
        }
    }
    e->used = guard_box_tick;
    if (e->cap < n) {
        free(e->box);
        e->box = malloc(n * 7 * sizeof(float)); /* 6 box floats a chunk, then one radius a chunk */
        e->cap = e->box ? n : 0;
        if (!e->box) {
            e->slot = 0;
            return 0;
        }
    }
    e->rad = e->box + n * 6;
    int lit = (vtype & GU_NORMAL_8BIT) != 0, stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    const char *pos = (const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x));
    for (int j = 0; j < n; j++) {
        float *b = e->box + j * 6;
        b[0] = b[1] = b[2] = 1e30f;
        b[3] = b[4] = b[5] = -1e30f;
        int first = j * chunk, last = first + chunk < tris ? first + chunk : tris;
        int from = prim == GU_TRIANGLE_STRIP ? first : first * 3, to = prim == GU_TRIANGLE_STRIP ? last + 2 : last * 3;
        for (int k = from; k < to; k++) {
            const float *p = (const float *)(pos + idx[k] * stride);
            for (int a = 0; a < 3; a++) {
                if (p[a] < b[a]) b[a] = p[a];
                if (p[a] > b[3 + a]) b[3 + a] = p[a];
            }
        }
        /* b199 p4_guard_small: the largest triangle of the chunk, as the radius of a sphere round its centroid */
        float r2 = 0;
        for (int t = first; t < last; t++) {
            int at = prim == GU_TRIANGLE_STRIP ? t : t * 3;
            const float *c[3];
            for (int k = 0; k < 3; k++)
                c[k] = (const float *)(pos + idx[at + k] * stride);
            float m[3];
            for (int a = 0; a < 3; a++)
                m[a] = (c[0][a] + c[1][a] + c[2][a]) * (1.0f / 3);
            for (int k = 0; k < 3; k++) {
                float dx = c[k][0] - m[0], dy = c[k][1] - m[1], dz = c[k][2] - m[2], d = dx * dx + dy * dy + dz * dz;
                if (d > r2)
                    r2 = d;
            }
        }
        e->rad[j] = sqrtf(r2);
    }
    e->slot = slot, e->key = key, e->built = built, e->n = n, e->chunk = chunk;
    guard_box_builds++;
    *rad = e->rad;
    return e->box;
}

/* A box against plane q (clip-space coefficients; q[4] is subtracted): value at the centre +- the radius, with the
   plane taken to model space through m. Returns the classes of the chunks; *partial / *culled count them. */
/* b144: the same tests on the VFPU. M400/M500: screen planes 1-4 and their absolute values (columns = x y z w of the
   four planes, read through E like guard_vfpu); M600/M700 the band planes; C300/C310 the near plane and its absolute
   value. Per box: centre c (w 1) and half size h (w 0), d = Q.c and r = |Q|.h; culled when d + r < 0 for a screen plane
   or the near plane, partial when d - r < 0 for a band plane (or the near plane without gnear). -0 counts as 0. */
static void guard_boxes_fpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled);
static inline int guard_negative(unsigned bits) { return (bits & 0x80000000u) && (bits & 0x7fffffffu); }
static void guard_boxes_vfpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled)
{
    float ms[16] __attribute__((aligned(16))), as[16] __attribute__((aligned(16)));
    float mb[16] __attribute__((aligned(16))), ab[16] __attribute__((aligned(16)));
    float near_q[8] __attribute__((aligned(16)));
    union { float f[12]; unsigned u[12]; } out __attribute__((aligned(16)));
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) {
            ms[i * 4 + k] = sp[k + 1][i];
            mb[i * 4 + k] = bp[k + 1][i];
            as[i * 4 + k] = i < 3 ? fabsf(sp[k + 1][i]) : 0;
            ab[i * 4 + k] = i < 3 ? fabsf(bp[k + 1][i]) : 0;
        }
    for (int i = 0; i < 4; i++) {
        near_q[i] = sp[0][i];
        near_q[4 + i] = i < 3 ? fabsf(sp[0][i]) : 0;
    }
    static const float half = 0.5f;
    __asm__ volatile(
        "lv.q    C400, 0(%[ms])\n lv.q C410, 16(%[ms])\n lv.q C420, 32(%[ms])\n lv.q C430, 48(%[ms])\n"
        "lv.q    C500, 0(%[as])\n lv.q C510, 16(%[as])\n lv.q C520, 32(%[as])\n lv.q C530, 48(%[as])\n"
        "lv.q    C600, 0(%[mb])\n lv.q C610, 16(%[mb])\n lv.q C620, 32(%[mb])\n lv.q C630, 48(%[mb])\n"
        "lv.q    C700, 0(%[ab])\n lv.q C710, 16(%[ab])\n lv.q C720, 32(%[ab])\n lv.q C730, 48(%[ab])\n"
        "lv.q    C300, 0(%[nq])\n lv.q C310, 16(%[nq])\n"
        "lv.s    S320, 0(%[half])\n"
        :
        : [ms] "r"(ms), [as] "r"(as), [mb] "r"(mb), [ab] "r"(ab), [nq] "r"(near_q), [half] "r"(&half)
        : "memory");
    for (int i = 0; i < n; i++, box += 6) {
        __asm__ volatile(
            "lv.s    S200, 0(%[b])\n lv.s S201, 4(%[b])\n lv.s S202, 8(%[b])\n"
            "lv.s    S210, 12(%[b])\n lv.s S211, 16(%[b])\n lv.s S212, 20(%[b])\n"
            "vadd.t  C220, C200, C210\n"
            "vsub.t  C230, C210, C200\n"
            "vscl.t  C220, C220, S320\n"
            "vscl.t  C230, C230, S320\n"
            "vone.s  S223\n"
            "vzero.s S233\n"
            "vtfm4.q C000, E400, C220\n"
            "vtfm4.q C010, E500, C230\n"
            "vadd.q  C020, C000, C010\n"
            "vtfm4.q C030, E600, C220\n"
            "vtfm4.q C100, E700, C230\n"
            "vsub.q  C110, C030, C100\n"
            "vdot.q  S120, C300, C220\n"
            "vdot.q  S121, C310, C230\n"
            "vadd.s  S122, S120, S121\n"
            "vsub.s  S123, S120, S121\n"
            "sv.q    C020, 0(%[o])\n"
            "sv.q    C110, 16(%[o])\n"
            "sv.q    C120, 32(%[o])\n"
            :
            : [b] "r"(box), [o] "r"(out.f)
            : "memory");
        int c = 0;
        if (guard_negative(out.u[0]) || guard_negative(out.u[1]) || guard_negative(out.u[2]) || guard_negative(out.u[3]) ||
            guard_negative(out.u[10]))
            c = 2;
        else if (guard_negative(out.u[4]) || guard_negative(out.u[5]) || guard_negative(out.u[6]) || guard_negative(out.u[7]) ||
                 (!first_band && guard_negative(out.u[11])))
            c = 1;
        cls[i] = c;
        *partial += c == 1;
        *culled += c == 2;
    }
}

static void guard_boxes_classify(const float *box, int n, const float m[4][4], float near, unsigned char *cls, int *partial, int *culled)
{
    static const float band[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    static const float screen[5][4] = { { 0, 0, 0, 1 }, { -1, 0, 0, 1 }, { 1, 0, 0, 1 }, { 0, -1, 0, 1 }, { 0, 1, 0, 1 } };
    float bp[5][4], sp[5][4];
    for (int k = 0; k < 5; k++)
        for (int j = 0; j < 4; j++) {
            bp[k][j] = m[j][0] * band[k][0] + m[j][1] * band[k][1] + m[j][2] * band[k][2] + m[j][3] * band[k][3];
            sp[k][j] = m[j][0] * screen[k][0] + m[j][1] * screen[k][1] + m[j][2] * screen[k][2] + m[j][3] * screen[k][3];
        }
    bp[0][3] -= near;
    /* The 1% CPU clipping margin must not cull cut-ins just inside the real near plane when the GE clips near
       (fix from MeMeNT0xMoRii's fork: the all-out attack portraits). */
    float camera_near = *(const float *)((const char *)current_camera + CAMERA_NEAR);
    sp[0][3] -= p4_guard_gnear ? camera_near : near;
    int first_band = p4_guard_gnear ? 1 : 0;
    *partial = *culled = 0;
    if (p4_guard_box_vfpu) {
        guard_boxes_vfpu(box, n, bp, sp, first_band, cls, partial, culled);
        if (!p4_guard_box_check)
            return;
        int p2 = 0, c2 = 0;
        static unsigned char ref[GUARD_MAX_CHUNKS];
        guard_boxes_fpu(box, n, bp, sp, first_band, ref, &p2, &c2);
        guard_box_checks += n;
        for (int i = 0; i < n; i++)
            guard_box_diffs += ref[i] != cls[i];
        return;
    }
    guard_boxes_fpu(box, n, bp, sp, first_band, cls, partial, culled);
}

static void guard_boxes_fpu(const float *box, int n, const float bp[5][4], const float sp[5][4], int first_band, unsigned char *cls, int *partial, int *culled)
{
    for (int i = 0; i < n; i++, box += 6) {
        float cx = (box[0] + box[3]) * 0.5f, cy = (box[1] + box[4]) * 0.5f, cz = (box[2] + box[5]) * 0.5f;
        float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
        int c = 0;
        for (int k = 0; k < 5; k++) { /* wholly behind the near plane or beyond one screen edge */
            const float *q = sp[k];
            if (q[0] * cx + q[1] * cy + q[2] * cz + q[3] + fabsf(q[0]) * hx + fabsf(q[1]) * hy + fabsf(q[2]) * hz < 0) {
                c = 2;
                break;
            }
        }
        if (!c)
            for (int k = first_band; k < 5; k++) { /* any corner possibly outside a plane the CPU cuts */
                const float *q = bp[k];
                if (q[0] * cx + q[1] * cy + q[2] * cz + q[3] - fabsf(q[0]) * hx - fabsf(q[1]) * hy - fabsf(q[2]) * hz < 0) {
                    c = 1;
                    break;
                }
            }
        cls[i] = c;
        *partial += c == 1;
        *culled += c == 2;
    }
}

/* b199 p4_guard_small: a chunk that crosses the band but whose triangles are all small next to their distance cannot
   hold a triangle that is both on screen and past the band. Band plane k is screen plane k plus (GUARD - 1) w, so a
   point on screen has band value >= (GUARD - 1) w >= (GUARD - 1) w_min, and a corner past the band (value < 0) of a
   triangle of radius r lies within 2 r of it: |grad| 2 r > (GUARD - 1) w_min. When that cannot hold, every triangle
   of the chunk is either inside the band (the GE draws it) or wholly off screen (the GE drops it past its window, or
   draws nothing visible), so the chunk goes to the GE uncut. Needs gnear (the GE clips the near plane) and a box
   wholly in front of the near plane. *front: 1 when every culled chunk is also in front of the near plane (mode 2 may
   then send the whole mesh to the GE as a recorded CALL; the GE transforms the off-screen chunks too). */
static void guard_small_fix(const float *box, const float *rad, int n, const float m[4][4], float near, unsigned char *cls, int *partial,
                            int *front)
{
    static const float band[4][4] = { { -1, 0, 0, GUARD }, { 1, 0, 0, GUARD }, { 0, -1, 0, GUARD }, { 0, 1, 0, GUARD } };
    float g2 = 0, wq[4];
    for (int k = 0; k < 4; k++) {
        float q2 = 0;
        for (int j = 0; j < 3; j++) {
            float q = m[j][0] * band[k][0] + m[j][1] * band[k][1] + m[j][2] * band[k][2] + m[j][3] * band[k][3];
            q2 += q * q;
        }
        if (q2 > g2)
            g2 = q2;
    }
    for (int j = 0; j < 4; j++)
        wq[j] = m[j][3];
    float need = 2 * sqrtf(g2) * 1.01f / (GUARD - 1); /* a chunk is safe when w_min >= need x radius */
    *front = 1;
    for (int i = 0; i < n; i++, box += 6) {
        if (!cls[i])
            continue;
        float cx = (box[0] + box[3]) * 0.5f, cy = (box[1] + box[4]) * 0.5f, cz = (box[2] + box[5]) * 0.5f;
        float hx = (box[3] - box[0]) * 0.5f, hy = (box[4] - box[1]) * 0.5f, hz = (box[5] - box[2]) * 0.5f;
        float wmin = wq[0] * cx + wq[1] * cy + wq[2] * cz + wq[3] - fabsf(wq[0]) * hx - fabsf(wq[1]) * hy - fabsf(wq[2]) * hz;
        if (cls[i] == 2) {
            if (!(wmin > near))
                *front = 0;
            continue;
        }
        if (wmin > near && wmin > 0 && wmin >= need * rad[i]) {
            cls[i] = 0;
            --*partial;
            guard_small_chunks++;
        }
    }
}

/* the chunk classes of a cached guarded mesh, computed once a draw: by guard_slot_whole at the call site (then
   draw_guarded_compact reuses them) or inside draw_guarded_compact */
static struct { const void *slot; unsigned key, built; const Matrix *model; float near; const float *box; int n, partial, culled, front; GuardLazy lazy; } guard_pre;
static unsigned char guard_cls[GUARD_MAX_CHUNKS];
static const float *guard_classify_slot(const void *slot, unsigned slot_key, unsigned slot_built, int prim, const unsigned short *idx, int nidx,
                                        const void *vertices, int vtype, const Matrix *model, float near)
{
    if (guard_pre.slot == slot && guard_pre.key == slot_key && guard_pre.built == slot_built && guard_pre.model == model && guard_pre.near == near)
        return guard_pre.box;
    prof_begin(PROF_GCLASS);
    const float *rad = 0;
    int n = 0, partial = 0, culled = 0, front = 0;
    const float *box = guard_slot_boxes(slot, slot_key, slot_built, prim, idx, nidx, vertices, vtype, &n, &rad);
    if (box) {
        guard_matrix_cached(guard_pre.lazy.m, model);
        guard_boxes_classify(box, n, (const float (*)[4])guard_pre.lazy.m, near, guard_cls, &partial, &culled);
        if (p4_guard_small && p4_guard_gnear && rad)
            guard_small_fix(box, rad, n, (const float (*)[4])guard_pre.lazy.m, near, guard_cls, &partial, &front);
    }
    prof_end(PROF_GCLASS);
    guard_pre.slot = slot, guard_pre.key = slot_key, guard_pre.built = slot_built, guard_pre.model = model, guard_pre.near = near;
    guard_pre.box = box, guard_pre.n = n, guard_pre.partial = partial, guard_pre.culled = culled, guard_pre.front = front;
    return box;
}

/* mode 2: a cached guarded mesh none of whose chunks needs the CPU: the caller draws it as a plain hit (CALL) */
static int guard_slot_whole(const void *slot, unsigned key, unsigned built, int prim, const unsigned short *idx, int nidx,
                            const void *vertices, int nverts, int vtype, const Matrix *model, float near)
{
    if (p4_guard_small < 2 || !p4_guard_boxes || !p4_guard_gnear || nverts > MAX_GUARDED)
        return 0;
    if (!guard_classify_slot(slot, key, built, prim, idx, nidx, vertices, vtype, model, near))
        return 0;
    if (guard_pre.partial || (guard_pre.culled && !guard_pre.front))
        return 0; /* draw_guarded_compact reuses the classes */
    guard_pre.slot = 0;
    guard_small_whole++;
    return 1;
}

static void draw_guarded_compact_body(int prim, const unsigned short *idx, int nidx, const void *vertices, int nverts, int vtype,
                                 const Matrix *model, float near, const Material *material, int modulate, const SceneLight *light,
                                 const int *alpha, const void *slot, unsigned slot_key, unsigned slot_built)
{
    prof_begin(PROF_GDRAW);
    int lit = (vtype & GU_NORMAL_8BIT) != 0;
    unsigned any = 1;
    if (slot && p4_guard_boxes && nverts <= MAX_GUARDED &&
        (guard_stamp || (guard_stamp = calloc(MAX_GUARDED, sizeof *guard_stamp)))) {
        unsigned char *cls = guard_cls;
        const float *box = guard_classify_slot(slot, slot_key, slot_built, prim, idx, nidx, vertices, vtype, model, near);
        int n = guard_pre.n, partial = guard_pre.partial, culled = guard_pre.culled;
        GuardLazy lazy = guard_pre.lazy;
        guard_pre.slot = 0;
        if (box) {
            guard_mesh_draws++;
            guard_box_inside += n - partial - culled, guard_box_partial += partial, guard_box_culled += culled;
            if (!partial && !culled) { /* the whole mesh inside the band: as an unguarded hit */
                guard_box_whole++;
                draw_slot_v = vertices, draw_slot_alpha = (int *)alpha;
                draw_mesh_vertices(prim, vtype, nidx, idx, vertices, material, modulate, light);
                draw_slot_v = 0;
            } else {
                if (!++guard_gen) { /* the stamps wrapped: forget them all */
                    memset(guard_stamp, 0, MAX_GUARDED * sizeof *guard_stamp);
                    guard_gen = 1;
                }
                lazy.pos = (const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x));
                lazy.stride = lit ? sizeof(Vertex3DN) : sizeof(Vertex3D);
                draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0;
                draw_guarded_chunks(prim, idx, nidx, vertices, vtype, near, material, modulate, light, cls, guard_chunk_now(), &lazy);
                draw_fixed_alpha = 0;
            }
            prof_end(PROF_GDRAW);
            return;
        }
    }
    if (nverts <= MAX_GUARDED) {
        Mat4 m;
        guard_matrix_cached(m, model);
        const V3 *position = (const V3 *)((const char *)vertices + (lit ? offsetof(Vertex3DN, x) : offsetof(Vertex3D, x)));
        prof_begin(PROF_GCLASS);
        any = guard_vfpu(position, nverts, (const float (*)[4])m, near, guard_clip, guard_code, lit ? sizeof(Vertex3DN) : sizeof(Vertex3D)) & GUARD_MASK;
        prof_end(PROF_GCLASS);
        guard_vertices += nverts;
    }
    guard_mesh_draws++;
    if (!any || nverts > MAX_GUARDED) /* nothing crosses (or too big to classify: drawn as without the guard) */
        draw_mesh_vertices(prim, vtype, nidx, idx, vertices, material, modulate, light);
    else {
        draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0; /* cut corners interpolate between kept ones */
        draw_guarded(prim, idx, nidx, vertices, vtype, near, material, modulate, light);
        draw_fixed_alpha = 0;
    }
    prof_end(PROF_GDRAW);
}

/* b236: persistent results of guarded cached meshes. Exact camera/model/geometry/options key; material,
   lighting and textures are applied live by draw_mesh_vertices. Admit after the same key is seen in two frames.
   Buffers are CPU-built, GE-read: eviction waits for the list's finish sequence, never just a camera change. */
int p4_guard_result = 1, p4_guard_result_check = 0; /* b242: on by default (user, 2026-11-14); moving scenes still to be checked with the check switch */
enum { GUARD_RESULT_SLOTS = 128, GUARD_RESULT_BUDGET = 256 * 1024 };
typedef struct {
    const void *slot, *v, *idx;
    unsigned key, built;
    int prim, vtype, nv, ni;
    float near, model[12];
    unsigned camera[18];
    int options[8];
} GuardResultKey;
typedef struct {
    GuardResultKey key;
    void *data;
    unsigned bytes, cut_off, seq, used, seen;
    int nkept, ncut, kept_alpha[2], cut_alpha[2];
} GuardResult;
static GuardResult *guard_results;
static unsigned gr_bytes, gr_hits, gr_misses, gr_builds, gr_pending, gr_refused, gr_checks, gr_diffs, gr_evictions;
static int guard_result_busy(const GuardResult *e)
{
    return e->data && e->seq && (int)(ge_finished - e->seq) < 0;
}
static void guard_result_free(GuardResult *e)
{
    gr_bytes -= e->bytes; free(e->data); memset(e, 0, sizeof *e);
}
static void guard_result_clear(void) /* caller has waited for the GE */
{
    if (guard_results) {
        for (int i = 0; i < GUARD_RESULT_SLOTS; i++) guard_result_free(&guard_results[i]);
        free(guard_results); guard_results = 0;
    }
}
static GuardResult *guard_result_set(const GuardResultKey *k)
{
    unsigned h = (((unsigned)k->slot >> 4) * 2654435761u >> 25) & ~3u;
    return guard_results + h;
}
static GuardResult *guard_result_find(const GuardResultKey *k)
{
    GuardResult *set = guard_result_set(k);
    for (int i = 0; i < 4; i++)
        if (set[i].key.slot == k->slot && !memcmp(&set[i].key, k, sizeof *k)) return &set[i];
    return 0;
}
static void guard_result_store(const GuardResultKey *k, const GuardCapture *cap)
{
    GuardResult *e = guard_result_find(k);
    if (!e) {
        GuardResult *set = guard_result_set(k), *take = 0;
        for (int i = 0; i < 4; i++) {
            GuardResult *p = set + i;
            if (guard_result_busy(p)) continue;
            if (!take || !p->key.slot || (!p->data && p->key.slot == k->slot) || p->used < take->used) take = p;
            if (!p->key.slot) break;
        }
        if (!take) { gr_refused++; return; }
        if (take->data) gr_evictions++;
        guard_result_free(take); take->key = *k; take->seen = take->used = cache_frame;
        gr_pending++; return; /* moving cameras incur no buffer allocations/copies */
    }
    e->used = cache_frame;
    if (e->data || e->seen == cache_frame) return;
    if (cap->nkept > 65535 || cap->ncut > 65535) { gr_refused++; return; }
    unsigned stride = k->vtype & GU_NORMAL_8BIT ? sizeof(Vertex3DN) : sizeof(Vertex3D);
    unsigned off = (cap->nkept * 2u + 15u) & ~15u, bytes = off + cap->ncut * stride;
    if (!bytes) bytes = 16; /* valid empty result */
    if (bytes > GUARD_RESULT_BUDGET) { gr_refused++; return; }
    while (gr_bytes + bytes > GUARD_RESULT_BUDGET) {
        GuardResult *old = 0;
        for (int i = 0; i < GUARD_RESULT_SLOTS; i++) {
            GuardResult *p = guard_results + i;
            if (p != e && p->data && !guard_result_busy(p) && (!old || p->used < old->used)) old = p;
        }
        if (!old) { gr_refused++; return; }
        guard_result_free(old); gr_evictions++;
    }
    void *data = memalign(16, bytes);
    if (!data) { gr_refused++; return; }
    if (cap->nkept) memcpy(data, cap->idx, cap->nkept * 2u);
    if (cap->ncut) memcpy((char *)data + off, cap->cut, cap->ncut * stride);
    sceKernelDcacheWritebackRange(data, bytes);
    e->data = data; e->bytes = bytes; e->cut_off = off; e->nkept = cap->nkept; e->ncut = cap->ncut;
    e->kept_alpha[0] = e->cut_alpha[0] = -1; gr_bytes += bytes; gr_builds++;
}
/* The view part of a guard result's key: model rows, camera (view matrix, near, far), guard options. b288: a static
   list recorded from guard results holds a copy of it and is drawn only while it is the same. */
static void guard_key_view(GuardResultKey *key, const Matrix *model)
{
    if (model) for (int i = 0; i < 4; i++) memcpy(key->model + i * 3, (const char *)model + i * 16, 12);
    else memset(key->model, 0, sizeof key->model), key->model[0] = key->model[4] = key->model[8] = 1;
    guard_world_planes(); memcpy(key->camera, guard_world.key, sizeof key->camera);
    int opts[8] = {p4_guard_boxes, p4_guard_gnear, p4_guard_near_ge, p4_guard_clip_fast,
                   p4_guard_small, p4_guard_chunk, p4_guard_loop2, p4_guard_box_vfpu};
    memcpy(key->options, opts, sizeof opts);
}
static int sl_recording; /* b258: a static list is being recorded (defined again below) */
static void sl_guard_note(const void *slot, const void *data, unsigned bytes, const GuardResultKey *k);
static void sl_guard_unowned(void);
static void draw_guarded_compact(int prim, const unsigned short *idx, int nidx, const void *vertices, int nverts, int vtype,
                                 const Matrix *model, float near, const Material *material, int modulate, const SceneLight *light,
                                 const int *alpha, const void *slot, unsigned slot_key, unsigned slot_built)
{
    if (!p4_guard_result || !slot || !current_camera || nverts > MAX_GUARDED ||
        (!guard_results && !(guard_results = calloc(GUARD_RESULT_SLOTS, sizeof *guard_results)))) {
        if (sl_recording)
            sl_guard_unowned(); /* cut into buffers of this frame: a list cannot keep them */
        draw_guarded_compact_body(prim, idx, nidx, vertices, nverts, vtype, model, near, material, modulate, light, alpha, slot, slot_key, slot_built);
        return;
    }
    prof_begin(PROF_GDRAW);
    GuardResultKey key; memset(&key, 0, sizeof key);
    key.slot = slot; key.v = vertices; key.idx = idx; key.key = slot_key; key.built = slot_built;
    key.prim = prim; key.vtype = vtype; key.nv = nverts; key.ni = nidx; key.near = near;
    guard_key_view(&key, model);
    GuardResult *e = guard_result_find(&key);
    if (e && e->data && !p4_guard_result_check) {
        if (sl_recording)
            sl_guard_note(slot, e->data, e->bytes, &key);
        gr_hits++; guard_mesh_draws++; e->used = cache_frame; e->seq = ge_queued + 1;
        const void *saved_v = draw_slot_v; int *saved_alpha = draw_slot_alpha;
        const int *saved_fixed = draw_fixed_alpha;
        draw_fixed_alpha = alpha && alpha[0] >= 0 ? alpha : 0;
        prof_begin(PROF_GSUB);
        if (e->nkept) {
            draw_slot_v = vertices; draw_slot_alpha = e->kept_alpha;
            draw_mesh_vertices(GU_TRIANGLES, vtype, e->nkept, e->data, vertices, material, modulate, light);
        }
        if (e->ncut) {
            const void *cut = (char *)e->data + e->cut_off;
            draw_slot_v = cut; draw_slot_alpha = e->cut_alpha;
            draw_mesh_vertices(GU_TRIANGLES, vtype & ~GU_INDEX_16BIT, e->ncut, 0, cut, material, modulate, light);
        }
        prof_end(PROF_GSUB);
        draw_slot_v = saved_v; draw_slot_alpha = saved_alpha; draw_fixed_alpha = saved_fixed;
        guard_pre.slot = 0;
        prof_end(PROF_GDRAW); return;
    }
    gr_misses++;
    if (sl_recording)
        sl_guard_unowned();
    GuardCapture cap = {0}, *saved_capture = guard_capture; guard_capture = &cap;
    draw_guarded_compact_body(prim, idx, nidx, vertices, nverts, vtype, model, near, material, modulate, light, alpha, slot, slot_key, slot_built);
    guard_capture = saved_capture;
    if (e && e->data && p4_guard_result_check) {
        unsigned stride = vtype & GU_NORMAL_8BIT ? sizeof(Vertex3DN) : sizeof(Vertex3D);
        int bad = !cap.valid || cap.nkept != e->nkept || cap.ncut != e->ncut;
        if (!bad && cap.nkept) bad = memcmp(cap.idx, e->data, cap.nkept * 2u) != 0;
        if (!bad && cap.ncut) bad = memcmp(cap.cut, (char *)e->data + e->cut_off, cap.ncut * stride) != 0;
        gr_checks++; gr_diffs += bad;
        if (bad && gr_diffs <= 8) p4_log("TRACE guard result mismatch slot %p kept %d/%d cut %d/%d\n", slot, cap.nkept, e->nkept, cap.ncut, e->ncut);
    } else if (cap.valid) guard_result_store(&key, &cap);
    prof_end(PROF_GDRAW);
}
static void guard_result_report(void)
{
    p4_log("guard result %d check %d hits %u misses %u builds %u pending %u refused %u evictions %u bytes %u checks %u diffs %u\n",
           p4_guard_result, p4_guard_result_check, gr_hits, gr_misses, gr_builds, gr_pending, gr_refused, gr_evictions, gr_bytes, gr_checks, gr_diffs);
    gr_hits = gr_misses = gr_builds = gr_pending = gr_refused = gr_evictions = gr_checks = gr_diffs = 0;
}

