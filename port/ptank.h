/* RpPTank particles (moved from rwsky.c, b322; included once, at the same place): the tank record, the PS2
   register setter, blend factors, the spill pool, camera-facing and matrix quads. */
/* RpPTank: the atomic's plugin record (offset at gp b610) holds RpPTankData at +0x50; layout read off func_003a2770. */
static inline char *PTANK_OF_ATOMIC(void *atomic)
{
    int offset = G32(0xb610 - 0x10000);
    return offset > 0 ? *(char **)((char *)atomic + offset) : 0;
}
enum {
    PT_ACTIVE = 4, PT_CLUSTERS = 0x54, PT_USERDATA = 0x9c, PT_DATAFLAGS = 0xa8, PT_SRCBLEND = 0xac, PT_DSTBLEND = 0xb0,
    PT_CSIZE = 0xc0, PT_CROTATE = 0xc8, PT_CCOLOR = 0xcc, PT_CUV = 0xe0, PT_CMATRIX = 0x100, PT_INSTFLAGS = 0x40,
    PD_POSITION = 1, PD_COLOR = 2, PD_SIZE = 4, PD_MATRIX = 8, PD_ROTATE = 0x20, PD_UV2 = 0x80, PD_CNSMATRIX = 0x8000,
    PD_CNSROTATE = 0x20000,
    /* cluster slots follow RPPTANKSIZE*, not the flag bits */
    PC_POSITION = 0, PC_MATRIX = 1, PC_SIZE = 3, PC_COLOR = 4, PC_ROTATE = 6, PC_UV2 = 7,
    PI_ALPHABLENDING = 0x10000000, PS2_ALPHA = 184, PS2_TEST = 200,
};
typedef struct { unsigned char *data; int stride; } Cluster;

/* RpPTank's PS2 register setter: which 1 writes ALPHA_1 and takes over from srcBlend/dstBlend, 2 the TEST low half. */
int func_003a2950(void *atomic, int which, unsigned value)
{
    char *tank = PTANK_OF_ATOMIC(atomic);
    char *ps2 = tank ? *(char **)(tank + PT_USERDATA) : 0;
    if (!tank && fx_set_reg(atomic, which, value)) /* an effect's own atomic (fx_direct.h): kept for its draw */
        return which == 1 || which == 2;
    if (!ps2)
        return which == 1 || which == 2;
    if (which == 1) {
        *(unsigned *)(ps2 + PS2_ALPHA) = value & 0xff;
        *(unsigned *)(ps2 + PS2_ALPHA + 4) = value >> 24 & 0xff;
        *(unsigned *)(tank + PT_INSTFLAGS) &= ~PI_ALPHABLENDING;
        return 1;
    }
    if (which == 2) {
        unsigned *test = (unsigned *)(ps2 + PS2_TEST);
        *test = (*test & 0xffff0000) | (value & 0xffff);
        return 1;
    }
    return 0;
}

/* RwBlendFunction to a GE factor; in either slot 0/1 mean the other side's colour, as RenderWare's SRCCOLOR/DESTCOLOR do. */
static inline int in_ram(const void *p) { return (unsigned)p - 0x08800000u < 0x03800000u; }

static int gu_factor(int rw, unsigned *fix)
{
    switch (rw) {
    case 1: *fix = 0; return GU_FIX;
    case 2: *fix = 0xffffff; return GU_FIX;
    case 3: case 9: return GU_SRC_COLOR;
    case 4: case 10: return GU_ONE_MINUS_SRC_COLOR;
    case 6: return GU_ONE_MINUS_SRC_ALPHA;
    case 7: return GU_DST_ALPHA;
    case 8: return GU_ONE_MINUS_DST_ALPHA;
    default: return GU_SRC_ALPHA;
    }
}

/* Large particle tanks may exceed the fixed pool. Keep extra draw batches alive for
   four presented frames, as prepared-list retirement does, and bound their memory. */
typedef struct PtankSpill { struct PtankSpill *next; unsigned frame, bytes, kind; } __attribute__((aligned(16))) PtankSpill;
static PtankSpill *ptank_spills;
int p4_ptank_spill_budget = 2 << 20;
static unsigned ptank_spill_bytes, ptank_spill_batches, ptank_spill_refused;
static unsigned static_uv_temp_bytes;
static void ptank_spill_collect(int all)
{
    PtankSpill **p = &ptank_spills;
    while (*p) {
        PtankSpill *s = *p;
        if (all || cache_frame - s->frame > 4) {
            *p = s->next;
            if (s->kind) static_uv_temp_bytes -= s->bytes; else ptank_spill_bytes -= s->bytes;
            free(s);
        } else p = &s->next;
    }
}
static void *frame_spill_alloc(unsigned payload, int kind)
{
    unsigned budget = kind ? 256 * 1024 : p4_ptank_spill_budget > 0 ? (unsigned)p4_ptank_spill_budget : 0;
    unsigned used = kind ? static_uv_temp_bytes : ptank_spill_bytes;
    if (payload > budget || sizeof(PtankSpill) > budget - payload || used > budget - payload - sizeof(PtankSpill)) {
        if (!kind) ptank_spill_refused++;
        return 0;
    }
    unsigned bytes = sizeof(PtankSpill) + payload;
    PtankSpill *s = memalign(16, bytes);
    if (!s) { if (!kind) ptank_spill_refused++; return 0; }
    s->next = ptank_spills; s->frame = cache_frame; s->bytes = bytes; s->kind = kind; ptank_spills = s;
    if (kind) static_uv_temp_bytes += bytes;
    else { ptank_spill_bytes += bytes; ptank_spill_batches++; }
    return s + 1;
}
static Vertex3D *ptank_spill_alloc(int capacity)
{
    if (capacity <= 0 || (unsigned)capacity > 0x7fffffffu / sizeof(Vertex3D)) return 0;
    return frame_spill_alloc(capacity * sizeof(Vertex3D), 0);
}
static void *static_uv_alloc(unsigned bytes)
{
    return frame_spill_alloc(bytes, 1);
}
static int ptank_submit(Vertex3D *vertices, int count, int pooled)
{
    if (!count) return 0;
    sceKernelDcacheWritebackRange(vertices, count * sizeof *vertices);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, count, 0, vertices);
    if (pooled) vertex_used += count;
    return 1;
}
/* GS TEST 0x17fb (GeometryBlockSetPartState(0)): alpha >= 0x7f writes depth, the rest only colour (AFAIL FB_ONLY).
   Each run of particles is drawn as the bound texture's and its tint's alpha bounds allow: all pass, all fail, or
   both passes back to back (mixed), so a later particle still sees the depth before it (persona4-portable, home25). */
enum { PTANK_ALPHA_REF = 0x7f * 2, PTANK_DEPTH_FULL = 0, PTANK_DEPTH_READ_ONLY, PTANK_DEPTH_MIXED };
static int ptank_depth_mode(unsigned alpha)
{
    if (p4_alpha_product_ceiling(alpha, bound_alpha_max) < PTANK_ALPHA_REF)
        return PTANK_DEPTH_READ_ONLY;
    return alpha * bound_alpha_min / 255 >= PTANK_ALPHA_REF ? PTANK_DEPTH_FULL : PTANK_DEPTH_MIXED;
}
static int ptank_submit_mode(Vertex3D *vertices, int count, int pooled, int mode)
{
    if (mode < 0 || !count)
        return ptank_submit(vertices, count, pooled);
    sceGuDepthMask(mode == PTANK_DEPTH_READ_ONLY);
    if (mode == PTANK_DEPTH_MIXED)
        sceGuAlphaFunc(GU_GEQUAL, PTANK_ALPHA_REF, 0xff);
    int drawn = ptank_submit(vertices, count, pooled);
    if (mode == PTANK_DEPTH_MIXED) {
        sceGuDepthMask(GU_TRUE);
        sceGuAlphaFunc(GU_LESS, PTANK_ALPHA_REF, 0xff);
        sceGuDrawArray(GU_TRIANGLES, 0, count, 0, vertices);
        sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    }
    sceGuDepthMask(GU_FALSE);
    return drawn;
}
int p4_ptank_matrix = 1; /* b269: particles placed by their own matrix (the field rain); 0: skipped, as b268 */
static unsigned ptank_matrix_particles;
static void ptank_spill_report(void)
{
    p4_log("ptank spill: batches %u refused %u live %u bytes budget %d per 256 frames; matrix particles %u\n",
           ptank_spill_batches, ptank_spill_refused, ptank_spill_bytes, p4_ptank_spill_budget, ptank_matrix_particles);
    ptank_spill_batches = ptank_spill_refused = ptank_matrix_particles = 0;
}

/* RwTexture::filterAddressing (+0x50; U address at bit 8, V at bit 12), as _rpPTankSkySetupRenderStateAddress reads
   it: wrap (1) repeats, all else clamps. The GE repeats over the power-of-two upload, so a padded axis stays clamped
   (from the persona4-portable fork, home24). */
static int ptank_axis_wrap(int size, unsigned address)
{
    return address == 1 && size > 0 && size <= 512 && !(size & (size - 1)) ? GU_REPEAT : GU_CLAMP;
}
static int ptank_wrap(void **texture)
{
    Raster *r = texture ? *texture : 0;
    if (!r)
        return GU_CLAMP;
    unsigned mode = *(const unsigned *)((const char *)texture + 0x50);
    return TEX_WRAP_UV(ptank_axis_wrap(r->width, mode >> 8 & 15), ptank_axis_wrap(r->height, mode >> 12 & 15));
}

/* Camera-facing quads, streamed in their original order. */
static void draw_ptank_body(void *atomic, char *tank, Geometry *g, void *frame);
static void draw_ptank(void *atomic, char *tank, Geometry *g, void *frame)
{
    if (!list_room()) /* b325 */
        return;
    if (p4_probe_flags & 4)
        return;
    prof_begin(PROF_PTANK);
    int timed = ge_time_begin();
    draw_ptank_body(atomic, tank, g, frame);
    ge_time_end(timed, GT_PTANK);
    prof_end(PROF_PTANK);
}

static void draw_ptank_body(void *atomic, char *tank, Geometry *g, void *frame)
{
    gu_dirty(); /* raw sceGu below: the mesh state cache no longer holds */
    int count = *(int *)(tank + PT_ACTIVE);
    unsigned flags = *(unsigned *)(tank + PT_DATAFLAGS);
    const Cluster *cluster = (const Cluster *)(tank + PT_CLUSTERS);
    static unsigned traced[8];
    for (int i = 0; i < 8; i++) {
        if (traced[i] == flags)
            break;
        if (!traced[i]) {
            traced[i] = flags;
            p4_log("TRACE ptank flags %x n%d blend %d/%d inst %x\n", flags, count, *(int *)(tank + PT_SRCBLEND), *(int *)(tank + PT_DSTBLEND), *(unsigned *)(tank + PT_INSTFLAGS));
            break;
        }
    }
    /* b269: a particle is placed by its position, or by its own matrix (row 3). The field rain is a matrix tank
       (FldRainUpdate: data flags colour | matrix, 0x2008000a): before, the port skipped it, so no rain was drawn. */
    const unsigned char *matrices = flags & PD_MATRIX && cluster[PC_MATRIX].data ? cluster[PC_MATRIX].data : 0;
    int matrix_stride = matrices ? cluster[PC_MATRIX].stride : 0;
    if (!matrices && (flags & PD_CNSMATRIX))
        matrices = (const unsigned char *)tank + PT_CMATRIX;
    int positioned = (flags & PD_POSITION) && cluster[PC_POSITION].data;
    if (count <= 0 || (!positioned && !(p4_ptank_matrix && matrices)) || !in_ram(frame) || *(unsigned char *)frame != 0 /* rwFRAME */)
        return;
    begin_frame();
    bind_depth();
    sceGuEnable(GU_CLIP_PLANES);
    Material **materials = g ? g->matList[0] : 0;
    Material *material = materials && (int)g->matList[1] > 0 ? materials[0] : 0;
    unsigned material_colour = material ? material->colour : 0xffffffff;
    bind_mask(material_colour >> 24); /* a stale stencil test from the last draw would hide every particle */
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuEnable(GU_BLEND);
    char *ps2 = *(char **)(tank + PT_USERDATA);
    if (ps2 && !(*(unsigned *)(tank + PT_INSTFLAGS) & PI_ALPHABLENDING) && *(unsigned *)(ps2 + PS2_ALPHA))
        set_blend(*(unsigned *)(ps2 + PS2_ALPHA)); /* the game wrote ALPHA_1 itself, see func_003a2950 */
    else {
        unsigned src_fix = 0, dst_fix = 0;
        int src = gu_factor(*(int *)(tank + PT_SRCBLEND), &src_fix), dst = gu_factor(*(int *)(tank + PT_DSTBLEND), &dst_fix);
        sceGuBlendFunc(GU_ADD, src, dst, src_fix, dst_fix);
    }
    /* TEST_1 with alpha NEVER and FB_ONLY (0x71801 and the like) draws colour for every fragment that passes depth and
       writes no depth, even with the scene's depth writes on: a transparent front layer no longer hides a later, farther
       effect layer (fix from MeMeNT0xMoRii's fork, home20: Zio burst, All-Out impact, Apsaras halo). */
    int colour_only = ps2 && render_state[RS_ZWRITE] && (*(unsigned *)(ps2 + PS2_TEST) & 0x300f) == 0x1001;
    if (colour_only)
        sceGuDepthMask(GU_TRUE);
    sceGuTexScale(1, 1);
    sceGuTexOffset(0, 0);
    set_camera();
    ScePspFMatrix4 identity;
    gumLoadIdentity(&identity);
    sceGuSetMatrix(GU_MODEL, &identity);
    void **texture = material ? material->texture : 0;
    bind_raster(texture ? *texture : 0, ptank_wrap(texture));

    /* World axes of the view's x and of screen up (the view's y runs down the screen). */
    const Matrix *view = (const Matrix *)((char *)current_camera + CAMERA_VIEW);
    V3 right = { view->right.x, view->up.x, view->at.x }, up = { -view->right.y, -view->up.y, -view->at.y };
    /* The view matrix carries the view window's scale; particle sizes are in world units. */
    float rl = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z), ul = sqrtf(up.x * up.x + up.y * up.y + up.z * up.z);
    if (rl > 0) { right.x /= rl; right.y /= rl; right.z /= rl; }
    if (ul > 0) { up.x /= ul; up.y /= ul; up.z /= ul; }
    const Matrix *model = p4r_003e9700(frame);
    float near = *(float *)((char *)current_camera + CAMERA_NEAR) * 1.01f;
    static const float whole[4] = { 0, 0, 1, 1 };
    const float *cuv = (const float *)(tank + PT_CUV);
    if (!cuv[0] && !cuv[1] && !cuv[2] && !cuv[3])
        cuv = whole;
    Vertex3D *vertices = vertex_pool + vertex_used, *out = vertices, *run = vertices;
    int capacity = MAX_VERTICES - vertex_used, pooled = 1, submitted = 0, processed = 0;
    int split_depth = ps2 && render_state[RS_ZWRITE] && (*(unsigned *)(ps2 + PS2_TEST) & 0xffff) == 0x17fb;
    int run_mode = split_depth ? PTANK_DEPTH_FULL : -1;
    for (int i = 0; i < count; i++) {
        const Matrix *pm = matrices && p4_ptank_matrix ? (const Matrix *)(matrices + i * matrix_stride) : 0;
        V3 centre = transform(positioned ? (const V3 *)(cluster[PC_POSITION].data + i * cluster[PC_POSITION].stride) : &pm->pos, model);
        const float *size = flags & PD_SIZE ? (const float *)(cluster[PC_SIZE].data + i * cluster[PC_SIZE].stride) : (const float *)(tank + PT_CSIZE);
        unsigned colour = flags & PD_COLOR ? *(unsigned *)(cluster[PC_COLOR].data + i * cluster[PC_COLOR].stride) : *(unsigned *)(tank + PT_CCOLOR);
        float angle = flags & PD_ROTATE ? *(float *)(cluster[PC_ROTATE].data + i * cluster[PC_ROTATE].stride) : flags & PD_CNSROTATE ? *(float *)(tank + PT_CROTATE) : 0;
        const float *uv = flags & PD_UV2 ? (const float *)(cluster[PC_UV2].data + i * cluster[PC_UV2].stride) : cuv;
        unsigned tint = modulate_colour(colour, material_colour);
        float c = cosf(angle), s = sinf(angle), w = size[0] * 0.5f, h = size[1] * 0.5f;
        V3 x = { (right.x * c + up.x * s) * w, (right.y * c + up.y * s) * w, (right.z * c + up.z * s) * w };
        V3 y = { (up.x * c - right.x * s) * h, (up.y * c - right.y * s) * h, (up.z * c - right.z * s) * h };
        if (pm) { /* the matrix rows are the quad's full width and height (the size cluster is unused then) */
            V3 hr = { pm->right.x * 0.5f, pm->right.y * 0.5f, pm->right.z * 0.5f }, hu = { pm->up.x * 0.5f, pm->up.y * 0.5f, pm->up.z * 0.5f };
            x = rotate(&hr, model);
            y = rotate(&hu, model);
            ptank_matrix_particles++;
        }
        static const signed char corner[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } }; /* around the edge */
        Vertex3D quad[4];
        float depth[4];
        for (int k = 0; k < 4; k++) {
            int sx = corner[k][0], sy = corner[k][1];
            quad[k].u = sx < 0 ? uv[0] : uv[2];
            quad[k].v = sy > 0 ? uv[1] : uv[3];
            quad[k].colour = tint;
            quad[k].x = centre.x + x.x * sx + y.x * sy;
            quad[k].y = centre.y + x.y * sx + y.y * sy;
            quad[k].z = centre.z + x.z * sx + y.z * sy;
            depth[k] = quad[k].x * view->right.z + quad[k].y * view->up.z + quad[k].z * view->at.z + view->pos.z;
        }
        /* Reserve the actual clipped quad, rather than nine vertices for every particle
           including invisible ones. Large Shuffle Time tanks used to be refused whole. */
        Vertex3D cut[CLIP_MAX_OUT];
        int emitted = clip_quad_near(quad, depth, near, cut);
        if (emitted > capacity - (out - vertices)) {
            submitted |= ptank_submit_mode(run, out - run, pooled, run_mode);
            capacity = (count-i > 256 ? 256 : count-i) * CLIP_MAX_OUT;
            vertices = ptank_spill_alloc(capacity);
            if (!vertices) {
                p4_log("TRACE ptank spill exhausted: processed %d of %d particles\n", i, count);
                out = vertices = 0;
                break;
            }
            out = run = vertices; pooled = 0;
        }
        if (split_depth && emitted) {
            int mode = ptank_depth_mode(tint >> 24);
            if (mode != run_mode || mode == PTANK_DEPTH_MIXED) {
                submitted |= ptank_submit_mode(run, out - run, pooled, run_mode);
                run = out, run_mode = mode;
            }
        }
        memcpy(out, cut, emitted * sizeof *out);
        out += emitted;
        processed++;
    }
    if (vertices) submitted |= ptank_submit_mode(run, out - run, pooled, run_mode);
    if (colour_only)
        sceGuDepthMask(GU_FALSE);
    if (submitted) ptanks_drawn++;
    particles_drawn += processed;
}

