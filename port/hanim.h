/* Bones and animation (moved from rwsky.c, b322; included once, at the same place): RpHAnimHierarchy
   UpdateMatrices in C, NaN and equal-key fixes, look-at, bone-update decisions (anim cull/far), AddAnimTime with
   the held steps, lazy animation, key maps, anim tracks, the pose trace. */
/* RpHAnimHierarchyUpdateMatrices (func_00397c40) in C, read off the reassembled MIPS (b142). Hierarchy: 0 flags,
   4 numNodes, 8 pMatrixArray, 16 pNodeInfo (16 bytes: id, index, flags, pFrame), 20 parentFrame, 24 parentHierarchy,
   28 rootParentOffset, 32 currentAnim. Interpolator: 36 frame size, 60 keyFrameToMatrixCB, 76 first frame (quat at
   +8, translation at +24). Frame: 3 private flags, 4 parent, 8 dirty link, 16 modelling, 80 ltm, 160 root.
   b141 Heaven: 9 calls, 0.64 Mcycles. p4_hanim_c 1 uses this; p4_hanim_check compares it with the original. */
int p4_hanim_c = 1, p4_hanim_check = 0;
/* b188: the bone multiplies of p4_hanim_update on the VFPU (multiply_vfpu, flags as RwMatrixMultiply sets them);
   an unaligned matrix falls back to RwMatrixMultiply and is counted. b142 found this update memory-bound (~3 data
   misses a bone), so the gain is unproven until a device A/B. p4_hanim_c is chosen in hanim_update_now since b188:
   the dispatch called it directly and so skipped the frameskip, far-LOD and NaN checks of func_00397c40. */
int p4_hanim_vfpu = 1;
static unsigned hanim_vfpu_unaligned;
int p4_hanim_update(unsigned *h);
/* b148: on a skipped frame (frameskip) the bone matrices and bone frames are not rebuilt: nothing draws them, and the
   next drawn frame rebuilds them from the animation, whose time still advances (RtAnimInterpolatorAddAnimTime is not
   this). Callers reach the original as p4r_00397c40. */
int p4_frameskip_bones = 1;
int p4_pose_plain = 0; /* b153 experiment: 1 = the look-at pose (mdlUpdateHierarchyPose) replaced by the plain bone update */
/* b154: the hierarchy scale hook (mdlInstallHierarchyScaleHook, layer flag 0x10) scales one node's matrix by a vector
   from the layer. Logged for each new layer and whenever a component is near 0 or huge. */
void p4_scale_hook_note(void *layer, int node, const float *scale, float model_scale, float aspect, int speed_zero)
{
    static void *seen[16];
    static unsigned odd_logged;
    int known = 0;
    for (int i = 0; i < 16; i++)
        known |= seen[i] == layer;
    int odd = 0;
    for (int k = 0; k < 3; k++) {
        float a = scale[k] < 0 ? -scale[k] : scale[k];
        odd |= !(a > 0.05f && a < 20.0f);
    }
    if (known && !(odd && odd_logged < 32))
        return;
    if (!known)
        for (int i = 0; i < 16; i++)
            if (!seen[i]) {
                seen[i] = layer;
                break;
            }
    odd_logged += odd;
    p4_log("TRACE scale hook layer %p node %d scale %d %d %d /1000 model %d aspect %d /1000 speed0 %d%s\n", layer, node,
           (int)(scale[0] * 1000), (int)(scale[1] * 1000), (int)(scale[2] * 1000), (int)(model_scale * 1000), (int)(aspect * 1000),
           speed_zero, odd ? " ODD" : "");
}
int p4r_00397c40(void *hierarchy);
/* b156: the head of a party member drawn as a line in battle, in some rounds. b155 caught it: bones 38-43 of a 93-node
   hierarchy with NaN matrices. RpHAnimKeyFrameInterpolate (p4r_003955a0) divides by t2 - t1; two keys with the same
   time give 0/0. The EE's FPU gives the largest finite value there and huge x 0 = 0, so the result is key 1; the PSP
   gives NaN, and the bone and its children go with it. Each interpolated frame (RpHAnimInterpFrame: keyFrame1,
   keyFrame2, q, t) is checked before the matrices are built; one with a NaN or infinity takes key 1's q and t. */
int p4_hanim_nan_fix = 1;
static unsigned hanim_nan_logged;
/* b158: bones 38-43 bad at skinning but not right after any update: which hierarchy, updated when? */
static struct { const void *h; unsigned frame, count; } hanim_seen[64];
static void hanim_note(const void *h)
{
    unsigned slot = ((unsigned)h >> 4) * 2654435761u >> 26;
    for (int k = 0; k < 64; k++) {
        int i = (slot + k) & 63;
        if (hanim_seen[i].h == h || !hanim_seen[i].h) {
            hanim_seen[i].h = h, hanim_seen[i].frame = cache_frame, hanim_seen[i].count++;
            return;
        }
    }
}
/* b213 p4_head_trace: the headless party members (b155-b181: bones 38-43 of the hero, 34.. of Naoto, beyond 1e7 at
   skinning; b181 took it for a part the game parks). Right after each hierarchy update, the first node whose matrix
   is beyond 1e7: its interpolated frame (q, t), both keys (time, q, t), the interpolator's time, the node's flags and
   whether its local matrix or its parent is the bad one. One line per hierarchy and state change, 24 at most. */
int p4_head_trace = 0;
static unsigned head_trace_logged;
static const void *head_trace_last[16];
static int head_trace_node[16];
static int head_big(const float *f, int n)
{
    for (int k = 0; k < n; k++)
        if (!(f[k] == f[k] && f[k] < 1e7f && f[k] > -1e7f))
            return 1;
    return 0;
}
static void head_trace(unsigned *h)
{
    const float *m = (const float *)h[2];
    char *interp = (char *)h[8];
    int n = (int)h[1];
    if (!m || !interp || n <= 0 || n > 256 || (unsigned)interp - 0x08800000u >= 0x03800000u)
        return;
    int bad = -1;
    for (int i = 0; i < n && bad < 0; i++)
        if (head_big(m + i * 16, 3) || head_big(m + i * 16 + 4, 3) || head_big(m + i * 16 + 8, 3) || head_big(m + i * 16 + 12, 3))
            bad = i;
    int slot = ((unsigned)h >> 4) & 15;
    if (head_trace_last[slot] == h && head_trace_node[slot] == bad)
        return;
    head_trace_last[slot] = h, head_trace_node[slot] = bad;
    if (bad < 0 || head_trace_logged >= 24)
        return;
    head_trace_logged++;
    int stride = *(int *)(interp + 36);
    const char *f = interp + 76 + bad * stride, *k1 = *(const char **)f, *k2 = *(const char **)(f + 4);
    const float *q = (const float *)(f + 8);
    const unsigned *info = (const unsigned *)h[4] + bad * 4;
    #define K1000(x) (int)((x) > 2e6f ? 2e9f : (x) < -2e6f ? -2e9f : (x) * 1000)
    p4_log("TRACE head h %p node %d/%d flags %x anim %p time %d: frame q %d %d %d %d t %d %d %d (x1000)\n", (void *)h, bad, n, info[2],
           *(void **)interp, K1000(*(float *)(interp + 4)), K1000(q[0]), K1000(q[1]), K1000(q[2]), K1000(q[3]), K1000(q[4]), K1000(q[5]), K1000(q[6]));
    for (int j = 0; j < 2; j++) {
        const char *k = j ? k2 : k1;
        if (!k || (unsigned)k - 0x08800000u >= 0x03800000u || ((unsigned)k & 3)) {
            p4_log("TRACE head   key%d %p (not a key)\n", j + 1, (const void *)k);
            continue;
        }
        const float *kq = (const float *)(k + 8);
        p4_log("TRACE head   key%d %p prev %p time %d q %d %d %d %d t %d %d %d\n", j + 1, (const void *)k, *(void **)k, K1000(*(float *)(k + 4)),
               K1000(kq[0]), K1000(kq[1]), K1000(kq[2]), K1000(kq[3]), K1000(kq[4]), K1000(kq[5]), K1000(kq[6]));
    }
    const float *b = m + bad * 16;
    p4_log("TRACE head   matrix right %d %d %d up %d %d %d at %d %d %d pos %d %d %d; node before %s; frame %p\n", K1000(b[0]), K1000(b[1]), K1000(b[2]),
           K1000(b[4]), K1000(b[5]), K1000(b[6]), K1000(b[8]), K1000(b[9]), K1000(b[10]), K1000(b[12]), K1000(b[13]), K1000(b[14]),
           bad ? (head_big(b - 16, 3) || head_big(b - 4, 3) ? "bad" : "fine") : "-", (void *)info[3]);
    #undef K1000
}
/* b214 around mdlUpdateHierarchyPose (census FIXES): the head look-at (node id 0x1389). b213 on the PSP: the bones
   go bad inside mainDraw, after a clean hierarchy update, from bone 38 (the head) down. The look state (MdlLookState
   at controllerState: flags, blend, limits, rotation, limitedRotation, scale, target, offsets) keeps its rotation
   between ticks and slerps it toward the target, so one NaN stays for good: a headless member for the rest of the
   battle. p4_head_trace logs the call where the head goes bad (look state before and after). p4_look_fix 1: a look
   rotation that is not finite before the call is reseeded from the animation (flag 0x200, the game's own reset);
   one made non-finite by the call is put back; a body pose (no parent transform) with a bad head is redone as the
   plain bone update for that tick. */
int p4_look_fix = 1;
static unsigned look_calls, look_bad_before, look_bad_after, look_redone, look_logged;
static unsigned char look_before[0x50] __attribute__((aligned(4)));
static int look_head_node(const unsigned *h)
{
    const unsigned *info = (const unsigned *)h[4];
    int n = (int)h[1];
    if (!info || n <= 0 || n > 256)
        return -1;
    for (int i = 0; i < n; i++)
        if (info[i * 4] == 0x1389)
            return i;
    return -1;
}
static void look_print(const char *when, const unsigned char *c)
{
    const float *f = (const float *)c;
    #define L1000(x) (int)((x) == (x) ? (x) > 2e6f ? 2e9f : (x) < -2e6f ? -2e9f : (x) * 1000 : -1)
    p4_log("TRACE look %s flags %04x blend %d limits %d %d rot %d %d %d %d lim %d %d %d %d scale %d %d %d target %d %d %d off %d %d\n", when,
           *(const unsigned short *)c, L1000(f[1]), L1000(f[2]), L1000(f[3]), L1000(f[4]), L1000(f[5]), L1000(f[6]), L1000(f[7]), L1000(f[8]),
           L1000(f[9]), L1000(f[10]), L1000(f[11]), L1000(f[12]), L1000(f[13]), L1000(f[14]), L1000(f[15]), L1000(f[16]), L1000(f[17]),
           L1000(f[18]), L1000(f[19]));
    #undef L1000
}
/* b218: values inside the look-at call (census FIXES probes 0-7), cleared at its start, printed when it goes bad */
static float look_probe_v[8][12];
static unsigned char look_probe_n[8];
void p4_look_probe(int id, const float *v, int n)
{
    if (id < 0 || id >= 8 || !p4_head_trace)
        return;
    if (n > 12)
        n = 12;
    memcpy(look_probe_v[id], v, n * sizeof *v);
    look_probe_n[id] = n;
}
static void look_probe_print(void)
{
    static const char *const names[8] = { "angles p bp y by look3 wv3", "rot from matrix", "output matrix", "rot after limits",
                                          "slerp cache from to omega flag", "blended limit", "fallback angle q", "local matrix" };
    for (int id = 0; id < 8; id++) {
        if (!look_probe_n[id]) {
            p4_log("TRACE look   %d %s: not reached\n", id, names[id]);
            continue;
        }
        char line[200];
        int at = 0;
        for (int k = 0; k < look_probe_n[id] && at < 180; k++) {
            float x = look_probe_v[id][k];
            at += snprintf(line + at, sizeof line - at, " %d", x != x ? -999999999 : x > 2e6f ? 2000000000 : x < -2e6f ? -2000000000 : (int)(x * 1000));
        }
        p4_log("TRACE look   %d %s:%s\n", id, names[id], line);
    }
}
int func_00397c40(void *hierarchy);
void p4_look_check(void *hierarchy, void *controller, void *parent, int after)
{
    unsigned *h = hierarchy;
    unsigned char *c = controller;
    if (!h || !c)
        return;
    float *rot = (float *)(c + 16);
    if (!after) {
        look_calls++;
        memcpy(look_before, c, sizeof look_before);
        memset(look_probe_n, 0, sizeof look_probe_n);
        if (head_big(rot, 4)) {
            look_bad_before++;
            if (p4_look_fix) {
                rot[0] = rot[1] = rot[2] = 0.0f, rot[3] = 1.0f;
                *(unsigned short *)c |= 0x200;
            }
        }
        return;
    }
    int head = look_head_node(h);
    const float *m = (const float *)h[2];
    int bad_matrix = head >= 0 && m && (head_big(m + head * 16, 3) || head_big(m + head * 16 + 4, 3) || head_big(m + head * 16 + 8, 3) ||
                                        head_big(m + head * 16 + 12, 3));
    int bad_rot = head_big(rot, 4);
    if (!bad_matrix && !bad_rot)
        return;
    look_bad_after++;
    if (p4_head_trace && look_logged < 8) {
        look_logged++;
        p4_log("TRACE look went bad: hierarchy %p head node %d parent %p matrix %s rotation %s\n", (void *)h, head, parent,
               bad_matrix ? "bad" : "fine", bad_rot ? "bad" : "fine");
        look_print("before", look_before);
        look_print("after ", c);
        look_probe_print();
    }
    if (!p4_look_fix)
        return;
    if (bad_rot) {
        memcpy(rot, look_before + 16, 16);
        if (head_big(rot, 4))
            rot[0] = rot[1] = rot[2] = 0.0f, rot[3] = 1.0f;
        *(unsigned short *)c |= 0x200;
    }
    if (bad_matrix && !parent) {
        func_00397c40(h);
        look_redone++;
    }
}
static void look_report(void)
{
    extern unsigned p4_acos_clamped;
    extern unsigned p4_2d_nan_report(void);
    p4_log("look fix %d: calls %u bad rotation before %u bad after %u redone %u; acos inputs clamped %u; 2d draws with NaN corners dropped %u\n",
           p4_look_fix, look_calls, look_bad_before, look_bad_after, look_redone, p4_acos_clamped, p4_2d_nan_report());
    look_calls = look_bad_before = look_bad_after = look_redone = p4_acos_clamped = 0;
}
/* b159: after every task, the hierarchies updated in the last 2 frames are checked; the first task after which one has
   a bad bone matrix (it was clean right after its update) is logged: who writes over the bones. */
static unsigned char hanim_bad_seen[64];
static int hanim_matrix_bad(const float *m)
{
    for (int k = 0; k < 16; k++)
        if (k % 4 != 3 && !(m[k] == m[k] && m[k] < 1e8f && m[k] > -1e8f))
            return 1;
    return 0;
}
void hanim_scan_after(const char *task, unsigned fn)
{
    static unsigned logged;
    if (!p4_bone_check || logged >= 16)
        return;
    for (int i = 0; i < 64; i++) {
        const unsigned *h = hanim_seen[i].h;
        if (!h || cache_frame - hanim_seen[i].frame > 2)
            continue;
        const float *m = (const float *)h[2];
        int n = (int)h[1];
        if (!m || n <= 0 || n > 256 || (unsigned)m - 0x08800000u >= 0x03800000u)
            continue;
        int bad = -1;
        for (int b = 0; b < n; b++)
            if (hanim_matrix_bad(m + b * 16)) {
                bad = b;
                break;
            }
        if (bad < 0) {
            hanim_bad_seen[i] = 0;
            continue;
        }
        if (hanim_bad_seen[i])
            continue;
        hanim_bad_seen[i] = 1;
        logged++;
        char name[25];
        memcpy(name, task, 24);
        name[24] = 0;
        p4_log("TRACE bones went bad during task %s fn %08x: hierarchy %p bone %d/%d, updated %d frames ago, matrices at %p\n", name, fn,
               (const void *)h, bad, n, (int)(cache_frame - hanim_seen[i].frame), (const void *)m);
    }
}
int hanim_age(const void *h)
{
    for (int i = 0; i < 64; i++)
        if (hanim_seen[i].h == h)
            return (int)(cache_frame - hanim_seen[i].frame);
    return -1;
}
static inline int hanim_bad(const float *f, int n)
{
    for (int k = 0; k < n; k++)
        if (!(f[k] == f[k] && f[k] < 1e30f && f[k] > -1e30f))
            return 1;
    return 0;
}
/* RpHAnim keys: plain (prev, time, q[4], t[3] as floats, 36 bytes) and compressed (prev, time, then q[4] and t[3] as
   16-bit floats, t scaled by the animation's custom data: offset[3], scale[3]; 24 bytes). An interpolated frame is
   (key1, key2, q[4], t[3]). */
typedef struct { void *prev; float time; unsigned short q[4], t[3]; } CKey;
typedef struct { void *k1, *k2; float q[4], t[3]; } IFrame;
typedef struct { void *prev; float time, q[4], t[3]; } HAnimKey;
static inline float rw_half(unsigned short h) /* retail's decode (00415bc0) */
{
    union { unsigned u; float f; } v;
    v.u = (h & 0x8000u) << 16;
    if (h & 0x7fff)
        v.u |= (((h & 0x7800u) << 12) + 0x38000000u) | ((h & 0x7ffu) << 12);
    return v.f;
}
static void decode_hanim_key(float qt[7], const CKey *key, const float *custom)
{
    for (int i = 0; i < 4; i++)
        qt[i] = rw_half(key->q[i]);
    for (int i = 0; i < 3; i++)
        qt[4 + i] = rw_half(key->t[i]) * custom[3 + i] + custom[i];
}
static int hanim_address_valid(const void *address)
{
    return !((unsigned)address & 3) && (unsigned)address - 0x08800000u < 0x03800000u;
}
/* A frame with a NaN or infinity takes its key 1. Until b309 key 1 was always read as plain floats, but P4's skeletal
   keys are compressed: the halves read as floats gave a garbage pose (fix from MeMeNT0xMoRii's fork). */
static void hanim_sanitize(unsigned *h)
{
    char *interp = (char *)h[8];
    if (!hanim_address_valid(interp))
        return;
    int n = (int)h[1], stride = *(int *)(interp + 36);
    if (n <= 0 || n > 256 || stride < 36 || stride > 256)
        return;
    int key_size = *(int *)(interp + 40);
    char *frame = interp + 76;
    for (int i = 0; i < n; i++, frame += stride) {
        float *qt = (float *)(frame + 8); /* q x y z w, t x y z */
        if (!hanim_bad(qt, 7))
            continue;
        const void *key = *(const void **)frame;
        if (hanim_address_valid(key)) {
            if (key_size == sizeof(HAnimKey)) {
                memcpy(qt, ((const HAnimKey *)key)->q, 7 * sizeof(float));
            } else if (key_size == sizeof(CKey)) {
                const char *animation = *(const char **)interp;
                const float *custom = hanim_address_valid(animation) ? *(const float **)(animation + 20) : 0;
                if (hanim_address_valid(custom))
                    decode_hanim_key(qt, key, custom);
            }
        }
        if (hanim_bad(qt, 7)) {
            qt[0] = qt[1] = qt[2] = 0, qt[3] = 1; /* no usable key: no rotation, no offset */
            qt[4] = qt[5] = qt[6] = 0;
        }
        hanim_nan_fixed++;
    }
}
/* b309 p4_hanim_key_span: two keys with the same time (a zero-length span) give 0/0 in both interpolate callbacks;
   the EE's FPU made that key 1, the PSP makes NaN, which every reader before the bone update (layer blends, look-at)
   took as it was. The frame takes key 1 here instead (fix from MeMeNT0xMoRii's fork). 0: retail, as b308. */
int p4_hanim_key_span = 1;
unsigned p4_hanim_equal_keys[2];
void p4r_003955a0(void *out, void *in1, void *in2, float time, void *data);
void p4r_00415bc0(void *out, void *in1, void *in2, float time, void *data);
void func_003955a0(void *out, void *in1, void *in2, float time, void *data)
{
    const HAnimKey *a = in1, *b = in2;
    IFrame *frame = out;
    if (p4_hanim_key_span && a->time == b->time) {
        memcpy(frame->q, a->q, sizeof frame->q);
        memcpy(frame->t, a->t, sizeof frame->t);
        p4_hanim_equal_keys[0]++;
        return;
    }
    p4r_003955a0(out, in1, in2, time, data);
}
void func_00415bc0(void *out, void *in1, void *in2, float time, void *data)
{
    const CKey *a = in1, *b = in2;
    if (p4_hanim_key_span && a->time == b->time) {
        decode_hanim_key((float *)((char *)out + 8), a, data);
        p4_hanim_equal_keys[1]++;
        return;
    }
    p4r_00415bc0(out, in1, in2, time, data);
}
/* b171: bone-matrix updates by what the frame draws. A hierarchy whose skin is drawn (skin_bones) is "skinned"; its
   last drawn tick and clip w are kept. p4_anim_cull: a skinned hierarchy not drawn in the last 2 ticks is not updated;
   skin_bones rebuilds its matrices when it is drawn again. p4_anim_far: one last drawn farther than p4_anim_far_depth
   is updated every p4_anim_far_step ticks and drawn with the older matrices in between. Hierarchies never drawn as
   skins (rigid parts on bone frames) always update. The counters run with both off: what each would save. */
int p4_anim_cull = 0, p4_anim_far = 1, p4_anim_far_step = 2; /* b173: far on (b172 class, by eye: fine) */
float p4_anim_far_depth = 500.0f;
enum { ANIM_STATES = 128 };
typedef struct { const void *h, *matrices, *interp; int nodes; unsigned drawn, updated, advanced, moved, used[8]; float depth, pending, at[3]; unsigned char stale, skinned, lod; } AnimState;
static AnimState anim_state[ANIM_STATES];
/* b228: each state's interpolator in an array of its own. AddAnimTime looked an interpolator up by scanning the
   128 states (80 bytes each, a cache line apiece), ~10k cycles a call, ~100 calls a drawn tick, most of them for
   interpolators that are no character's (UV animations): kadd minus its interpolation was ~1.2 Mcycles a frame. */
static const void *anim_interp_of[ANIM_STATES];
static unsigned anim_updates, anim_unskinned, anim_unseen, anim_far_due, anim_culled, anim_far_skipped, anim_lazy, anim_full;
static unsigned anim_draws, anim_depth_bins[6];
static float anim_depth_max;
extern unsigned p4_anim_tick;
static AnimState *anim_find(const void *h)
{
    const unsigned *w = h;
    unsigned slot = ((unsigned)h >> 4) * 2654435761u >> 25;
    for (int k = 0; k < ANIM_STATES; k++) {
        AnimState *s = &anim_state[(slot + k) & (ANIM_STATES - 1)];
        if (s->h && s->h != h)
            continue;
        if (!s->h || s->nodes != (int)w[1] || s->matrices != (const void *)w[2]) { /* new, or another hierarchy here now */
            memset(s, 0, sizeof *s);
            anim_interp_of[s - anim_state] = 0;
            s->h = h, s->nodes = (int)w[1], s->matrices = (const void *)w[2];
        }
        return s;
    }
    anim_full++;
    return 0;
}
/* b175: the bone matrices carry the character's place in the world; held while it walked, the body lagged a step
   behind its shadow on every other tick (b174 Heaven: "my character doubles"). Only a character whose model has not
   moved for 8 ticks is held. */
static unsigned anim_moving_kept;
static int anim_far_hold(const AnimState *s)
{
    if (!p4_anim_far || !(s->depth > p4_anim_far_depth) || p4_anim_tick - s->updated >= (unsigned)p4_anim_far_step)
        return 0;
    if (p4_anim_tick - s->moved < 8) {
        anim_moving_kept++;
        return 0;
    }
    return 1;
}
static int hanim_update_now(void *hierarchy);
static void anim_drawn(const void *hierarchy)
{
    AnimState *s = anim_find(hierarchy);
    if (!s)
        return;
    anim_draws++;
    s->skinned = 1;
    s->drawn = p4_anim_tick;
    if (s->stale && !anim_far_hold(s)) {
        prof_begin(PROF_KMAT);
        hanim_update_now((void *)hierarchy);
        prof_end(PROF_KMAT);
        s->stale = 0, s->updated = p4_anim_tick;
        anim_lazy++;
    }
}
static void anim_seen_at(const void *hierarchy, float depth, const Matrix *model)
{
    AnimState *s = anim_find(hierarchy);
    if (!s)
        return;
    s->depth = depth;
    if (model) {
        const float *at = &model->pos.x;
        float d = 0;
        for (int k = 0; k < 3; k++)
            d += fabsf(at[k] - s->at[k]), s->at[k] = at[k];
        if (d > 0.01f)
            s->moved = p4_anim_tick;
    } else
        s->moved = p4_anim_tick;
    s->interp = (const void *)((const unsigned *)hierarchy)[8]; /* the hierarchy's interpolator (hanim_sanitize) */
    anim_interp_of[s - anim_state] = s->interp;
    static const float edge[5] = { 250, 500, 1000, 2000, 4000 };
    int bin = 0;
    while (bin < 5 && depth >= edge[bin])
        bin++;
    anim_depth_bins[bin]++;
    if (depth > anim_depth_max)
        anim_depth_max = depth;
}
void p4_anim_report(void)
{
    p4_log("anim cull %d far %d depth %d step %d: updates %u unskinned %u unseen %u far_due %u culled %u far_skipped %u moving_kept %u lazy %u draws %u full %u\n",
           p4_anim_cull, p4_anim_far, (int)p4_anim_far_depth, p4_anim_far_step, anim_updates, anim_unskinned, anim_unseen, anim_far_due,
           anim_culled, anim_far_skipped, anim_moving_kept, anim_lazy, anim_draws, anim_full);
    anim_moving_kept = 0;
    p4_log("anim depth of drawn skins <250 %u <500 %u <1000 %u <2000 %u <4000 %u more %u max %d\n", anim_depth_bins[0], anim_depth_bins[1],
           anim_depth_bins[2], anim_depth_bins[3], anim_depth_bins[4], anim_depth_bins[5], (int)anim_depth_max);
    anim_updates = anim_unskinned = anim_unseen = anim_far_due = anim_culled = anim_far_skipped = anim_lazy = anim_draws = anim_full = 0;
    memset(anim_depth_bins, 0, sizeof anim_depth_bins);
    anim_depth_max = 0;
    { void bone_census_report(void); bone_census_report(); }
}
/* b208 p4_bone_census: which nodes of a drawn skinned hierarchy anybody needs. Weighted: some skin drawn with it has a
   vertex weight on the node. Attached: the node's RwFrame holds objects (objectList at +144 not empty: a weapon, an
   atomic). Live: those and their ancestors (RpHAnim node flags: 2 push the parent, 1 pop it). Dead: the rest, which
   neither the interpolation nor the matrix build would need. A count only: the game may still read a dead node's
   matrix (effects on frames, mdlCopyIndexedMatrix), so nothing is skipped. */
int p4_bone_census = 0;
static void bone_census_note(const void *hierarchy, const unsigned *used)
{
    AnimState *st = anim_find(hierarchy);
    if (st)
        for (int k = 0; k < 8; k++)
            st->used[k] |= used[k];
}
void bone_census_report(void)
{
    if (!p4_bone_census)
        return;
    unsigned hier = 0, nodes = 0, weighted = 0, attached = 0, live = 0, shown = 0;
    for (int i = 0; i < ANIM_STATES; i++) {
        AnimState *st = &anim_state[i];
        const unsigned *h = st->h;
        int used_any = 0;
        for (int k = 0; k < 8; k++)
            used_any |= st->used[k] != 0;
        if (!h || !st->skinned || !used_any || p4_anim_tick - st->drawn > 300)
            continue;
        int n = (int)h[1];
        const char *info = (const char *)h[4];
        if (n <= 0 || n > 256 || !info)
            continue;
        short parent[256], stack[256];
        unsigned char keep[256];
        int cur = -1, depth = 0, w = 0, a = 0, l = 0;
        for (int j = 0; j < n; j++) {
            unsigned flags = *(const unsigned *)(info + j * 16 + 8);
            parent[j] = cur;
            if ((flags & 2) && depth < 256)
                stack[depth++] = cur;
            cur = (flags & 1) && depth > 0 ? stack[--depth] : j;
            const char *frame = *(char *const *)(info + j * 16 + 12);
            int weighted_j = st->used[j >> 5] >> (j & 31) & 1;
            int attached_j = frame && (unsigned)frame - 0x08800000u < 0x03800000u && *(char *const *)(frame + 144) != frame + 144;
            keep[j] = weighted_j || attached_j;
            w += weighted_j, a += attached_j && !weighted_j;
        }
        for (int j = n - 1; j >= 0; j--) /* parents come before their children */
            if (keep[j] && parent[j] >= 0)
                keep[parent[j]] = 1;
        for (int j = 0; j < n; j++)
            l += keep[j];
        hier++, nodes += n, weighted += w, attached += a, live += l;
        if (shown++ < 4)
            p4_log("bone census hierarchy %p: nodes %d weighted %d attached only %d live %d dead %d\n", (const void *)h, n, w, a, l, n - l);
        memset(st->used, 0, sizeof st->used);
    }
    if (hier)
        p4_log("bone census: %u drawn skinned hierarchies, nodes %u, weighted %u, attached only %u, live with ancestors %u, dead %u\n", hier, nodes,
               weighted, attached, live, nodes - live);
}
/* The interpolate callbacks (RpHAnim's compressed-key slerp+lerp p4r_00415bc0 among them) run as retail. Measured and
   removed in the 2026-11-15 cleanup (archive/perf_history.md): a VFPU slerp (b172), the whole node on the VFPU (b239),
   batches per callback kind (b234) and decoded key pairs (b235): the node's work is memory-bound, none gained. */
typedef void (*KInterp)(void *out, void *in1, void *in2, float time, void *data);
static unsigned interp_calls, interp_other;
void p4r_00415bc0(void *out, void *in1, void *in2, float time, void *data);
KInterp p4_interp_pick(KInterp cb)
{
    if (cb == (KInterp)func_00415bc0)
        interp_calls++;
    else
        interp_other++;
    return cb;
}

/* AddAnimTime (RtAnimInterpolatorAddAnimTime). The interpolate callback is swapped for the call (a pointer at +0x44);
   p4_anim_far_kadd: the interpolator of a hierarchy held by p4_anim_far (or culled by p4_anim_cull) keeps its time
   step for later and is advanced by the sum when it is due. */
int p4_anim_far_kadd = 1;
/* b201 p4_anim_skip_kadd: in a tick frameskip leaves undrawn the bones are not updated (p4_frameskip_bones), so the
   interpolated pose of a drawn skinned character is not used either: its time step is kept and added with the drawn
   tick's (at most a second kept back), as p4_anim_far_kadd does for far characters. Layer time and the animation's
   callbacks then move on the drawn tick, at most 2 ticks late. */
int p4_anim_skip_kadd = 1;
static unsigned kadd_calls, kadd_mapped, kadd_held, kadd_skip_held, kadd_near_end;
int p4r_003d5bc0(void *interpolator, float time);
static AnimState *anim_by_interp(const void *interp)
{
    if (!interp)
        return 0;
    for (int i = 0; i < ANIM_STATES; i++)
        if (anim_interp_of[i] == interp && anim_state[i].h && anim_state[i].interp == interp)
            return &anim_state[i];
    return 0;
}
/* b202 p4_anim_lazy: in a tick frameskip leaves undrawn a layer's own interpolators (its hierarchy's currentAnim and
   its rebase input, inside mdlAdvanceLayer) only move their clock. RtAnim's AddAnimTime adds the step, walks the key
   stream up to the new time and interpolates every node; walk and interpolation depend on the final time alone, so
   both wait for the first reader. A step that crosses the callback time or loops takes the retail call. The layer
   blend and the rebase of such a tick read the frames, so they are recorded and replayed after the interpolation.
   Readers that materialise: the bone update (hanim_update_now), look-at, a blend or rebase outside a deferral and the
   RtAnim calls SubAnimTime, SetCurrentAnim, Blend, Destroy. A retail AddAnimTime catches the walk up by itself.
   0 off, 1 undrawn ticks, 2 every tick (PPSSPP test: same picture). p4_anim_lazy_check: a copy of each deferred
   interpolator runs the retail calls and is compared when materialised. */
int p4_anim_lazy = 1, p4_anim_lazy_check = 0;
int p4_anim_native = 1; /* b204: plain AddAnimTime steps in C (func_003d5bc0) */
static unsigned native_adds, retail_adds;
enum { LAZY_SLOTS = 128 }; /* b203: material UV interpolators too */
typedef struct { char *ip, *first, *layer, *input, *rotation, *clone; unsigned stamp; float blend; } LazyAnim;
static LazyAnim lazy_slot[LAZY_SLOTS];
static const void *lazy_ip[LAZY_SLOTS]; /* b228: lazy_slot[i].ip, scanned instead of the 32-byte slots */
static int lazy_used, lazy_paused;
static char *lazy_cur, *lazy_input, *lazy_replay_input;
static unsigned lazy_deferred, lazy_refused, lazy_blends, lazy_rebases, lazy_materialized, lazy_obsolete, lazy_full, lazy_checks, lazy_diffs;
int func_003d5bc0(void *interpolator, float time);
unsigned p4_mdlBlendAnimInterpolators(void *destination, void *first, void *second, float blend);
void p4_mdlRebaseLayerPose(void *layer);
static LazyAnim *lazy_find(const void *ip)
{
    if (!lazy_used || !ip)
        return 0;
    for (int i = 0; i < LAZY_SLOTS; i++)
        if (lazy_ip[i] == ip)
            return &lazy_slot[i];
    return 0;
}
static void lazy_drop(LazyAnim *e)
{
    free(e->clone);
    memset(e, 0, sizeof *e);
    lazy_ip[e - lazy_slot] = 0;
    lazy_used--;
}
/* b204: the node of every key, once per animation. SetCurrentAnim (3d5840) gives node i keys i and i + n, so key k < 2n
   belongs to node k mod n, and a later key to the node of its previous key. The walk checks each answer (the node's
   second key must be the previous key) and searches as retail does when it is wrong, so a stale map costs time only. */
typedef struct { const char *anim, *frames; int count, size, nodes; unsigned short *node; } KeyMap;
enum { KEY_MAPS = 128 };
static KeyMap key_map[KEY_MAPS];
static unsigned key_maps_built, key_map_misses, key_walked;
int p4_anim_keymap = 1;
static const unsigned short *key_nodes(const char *ip)
{
    const char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10);
    int count = *(int *)(anim + 4), size = *(int *)(ip + 0x28), n = *(int *)(ip + 0x2c);
    KeyMap *m = &key_map[((unsigned)anim >> 4) * 2654435761u >> 25];
    if (m->anim == anim && m->frames == frames && m->count == count && m->size == size && m->nodes == n)
        return m->node;
    free(m->node);
    memset(m, 0, sizeof *m);
    if (!p4_anim_keymap || count <= 0 || n <= 0 || n > 0xffff || size <= 0)
        return 0;
    unsigned short *node = malloc(count * sizeof *node);
    if (!node)
        return 0;
    for (int k = 0; k < count; k++) {
        if (k < 2 * n) {
            node[k] = k % n;
            continue;
        }
        const char *prev = *(char **)(frames + k * size);
        int at = prev - frames;
        if (at < 0 || at % size || at / size >= k) { /* not this layout: no map */
            free(node);
            return 0;
        }
        node[k] = node[at / size];
    }
    m->anim = anim, m->frames = frames, m->count = count, m->size = size, m->nodes = n, m->node = node;
    key_maps_built++;
    return node;
}
/* retail AddAnimTime's key walk (func_003d5bc0 at 3d5cac): every key whose previous key is due goes to the node
   whose second key that is, searched from where the last one was found */
static void lazy_walk(char *ip)
{
    char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10), *end = frames + *(int *)(anim + 4) * *(int *)(ip + 0x28);
    int nodes = *(int *)(ip + 0x2c), a = 0;
    const unsigned short *map = 0;
    while (*(char **)(ip + 8) < end) {
        char *next = *(char **)(ip + 8), *prev = *(char **)next, *f;
        if (!(*(float *)(prev + 4) <= *(float *)(ip + 4)))
            break;
        if (!map && p4_anim_keymap)
            map = key_nodes(ip);
        key_walked++;
        if (map && (f = ip + 0x4c + map[(next - frames) / *(int *)(ip + 0x28)] * *(int *)(ip + 0x24), *(char **)(f + 4) == prev)) {
            a = map[(next - frames) / *(int *)(ip + 0x28)] + 1; /* where retail's search would go on */
            if (a >= nodes)
                a = 0;
        } else {
            if (map)
                key_map_misses++;
            do {
                f = ip + 0x4c + a * *(int *)(ip + 0x24);
                if (++a >= nodes)
                    a = 0;
            } while (*(char **)(f + 4) != prev);
        }
        *(char **)f = prev;
        *(char **)(f + 4) = next;
        *(char **)(ip + 8) = next + *(int *)(ip + 0x28);
    }
}
/* and its interpolation (3d5dd0): every node from its two keys at the current time */
static void lazy_interpolate(char *ip)
{
    char *f = ip + 0x4c;
    for (int i = 0; i < *(int *)(ip + 0x2c); i++, f += *(int *)(ip + 0x24)) {
        KInterp cb = *(KInterp *)(ip + 0x44);
        p4_interp_pick(cb)(f, *(void **)f, *(void **)(f + 4), *(float *)(ip + 4), *(void **)(*(char **)ip + 0x14));
    }
}
#include "anim_tracks.h"
static int lazy_bytes(const char *ip) { return 0x4c + *(int *)(ip + 0x2c) * *(int *)(ip + 0x24); }
static int lazy_compare(const char *ip, const char *clone);
void p4_anim_materialize(void *interp)
{
    LazyAnim *e = lazy_find(interp), copy;
    if (!e)
        return;
    copy = *e;
    e->clone = 0;
    lazy_drop(e); /* first: the replays below may reach this interpolator again */
    lazy_materialized++;
    if (!anim_track_step(copy.ip)) {
        lazy_walk(copy.ip);
        lazy_interpolate(copy.ip);
    }
    if (copy.clone) { /* the header's callbacks may hold the profiler's trampoline: lazy_compare skips them */
        lazy_compare(copy.ip, copy.clone);
        free(copy.clone);
    }
    if (copy.first) {
        p4_anim_materialize(copy.first);
        p4_mdlBlendAnimInterpolators(copy.ip, copy.first, copy.ip, copy.blend);
    }
    if (copy.layer) {
        p4_anim_materialize(copy.input);
        p4_anim_materialize(copy.rotation);
        lazy_replay_input = copy.input; /* the rebase's own AddAnimTime of its input happened when it was recorded */
        p4_mdlRebaseLayerPose(copy.layer);
        lazy_replay_input = 0;
    }
}
/* the frames are rebuilt from the keys by the caller: what was waiting is obsolete */
static void lazy_obsolete_now(const void *ip)
{
    LazyAnim *e = lazy_find(ip);
    if (e)
        lazy_obsolete++, lazy_drop(e);
}
/* census FIXES around mdlAdvanceLayer in mdlAdvanceModel: this layer's interpolators, or 0 after it */
void p4_anim_lazy_scope(void *cur, void *input)
{
    int on = cur && (p4_anim_lazy == 2 || (p4_anim_lazy == 1 && skip_frame && p4_frameskip_bones));
    lazy_cur = on ? cur : 0;
    lazy_input = on ? input : 0;
}
/* b203: around mdlAdvanceAnimControl (census FIXES) every interpolator it steps is the material UV animation's own;
   in an undrawn tick their apply (func_003d7cf0, the material's UV matrices) is left out too: nothing draws them, and
   the drawn tick steps and applies again. 2 = the test mode: the apply runs and materialises. */
static int lazy_all;
void p4_anim_lazy_materials(int begin)
{
    lazy_all = !begin ? 0 : p4_anim_lazy == 2 ? 2 : p4_anim_lazy == 1 && skip_frame && p4_frameskip_bones;
}
static int lazy_owned(const void *ip) { return !lazy_paused && ip && (lazy_all || ip == lazy_cur || ip == lazy_input); }
static unsigned lazy_uv_skipped;
void *func_003d8130(void *material, int slot); /* the material's UV interpolator, slot 0 or 1 */
int p4r_003d7cf0(void *material);
int func_003d7cf0(void *material) /* RpMaterialUVAnimApplyUpdate: UV matrices from both slots' frames */
{
    if (lazy_all == 1) {
        lazy_uv_skipped++;
        return 1;
    }
    p4_anim_materialize(func_003d8130(material, 0));
    p4_anim_materialize(func_003d8130(material, 1));
    return p4r_003d7cf0(material);
}
/* an AddAnimTime step where retail fires no callback (3d5bc0 start), does not loop (3d5c30) and has a next key:
   then it is the clock, the walk and the interpolation only; *t is the new time */
static int add_plain_at(char *ip, float time, float *t, int restart)
{
    char *anim = *(char **)ip;
    if (!(time > 0.0f) || !anim || !*(char **)(ip + 8) != restart)
        return 0;
    float now = *(float *)(ip + 4), at = *(float *)(ip + 0x14);
    *t = now + time;
    return *t <= *(float *)(anim + 0xc) && !(now < at && !(*t < at));
}
static int add_plain(char *ip, float time, float *t) { return add_plain_at(ip, time, t, 0); }
static int lazy_compare(const char *ip, const char *clone) /* time, next key and the frames */
{
    int at = -1, n = lazy_bytes(ip);
    for (int k = 4; k < n && at < 0; k += 4)
        if ((k < 0xc || k >= 0x4c) && *(unsigned *)(ip + k) != *(unsigned *)(clone + k))
            at = k;
    lazy_checks++;
    if (at >= 0 && lazy_diffs++ < 8)
        p4_log("TRACE anim check: interpolator %p offset %x: %08x, retail copy %08x (time %d/1000, nodes %d, frame %d)\n", (void *)ip, at,
               *(unsigned *)(ip + at), *(unsigned *)(clone + at), (int)(*(float *)(ip + 4) * 1000), *(int *)(ip + 0x2c), *(int *)(ip + 0x24));
    return at < 0;
}
static int lazy_defer(char *ip, float time)
{
    float t;
    if (!add_plain(ip, time, &t))
        return 0;
    LazyAnim *e = lazy_find(ip);
    if (!e) {
        for (int i = 0; i < LAZY_SLOTS && !e; i++)
            if (!lazy_ip[i])
                e = &lazy_slot[i];
        if (!e) {
            lazy_full++;
            return 0;
        }
        lazy_used++;
        e->ip = ip;
        lazy_ip[e - lazy_slot] = ip;
        if (p4_anim_lazy_check && (e->clone = malloc(lazy_bytes(ip))))
            memcpy(e->clone, ip, lazy_bytes(ip));
    }
    if (e->clone)
        p4r_003d5bc0(e->clone, time);
    e->first = e->layer = 0; /* this tick interpolates afresh: an older blend or rebase is overwritten */
    e->stamp = p4_anim_tick;
    *(float *)(ip + 4) = t;
    lazy_deferred++;
    return 1;
}
static LazyAnim *lazy_fresh(const void *ip)
{
    LazyAnim *e = lazy_find(ip);
    return e && e->stamp == p4_anim_tick ? e : 0;
}
/* mdlBlendAnimInterpolators (census FIXES): 1 = recorded, the caller returns */
int p4_anim_lazy_blend(void *destination, void *first, void *second, float blend)
{
    LazyAnim *e;
    if (lazy_owned(destination) && destination == second && (e = lazy_fresh(destination)) && !e->first && !e->layer) {
        e->first = first, e->blend = blend;
        lazy_blends++;
        return 1;
    }
    p4_anim_materialize(first);
    p4_anim_materialize(second);
    p4_anim_materialize(destination);
    return 0;
}
/* mdlRebaseLayerPose (census FIXES); active: it has an input and flag 0x80. 1 = recorded, the caller returns */
int p4_anim_lazy_rebase(void *layer, void *cur, void *input, void *rotation, int active, float step)
{
    LazyAnim *e;
    if (lazy_replay_input || !active)
        return 0;
    if (lazy_owned(cur) && input == lazy_input && (e = lazy_fresh(cur)) && !e->layer) {
        func_003d5bc0(input, step);
        e->layer = layer, e->input = input, e->rotation = rotation;
        lazy_rebases++;
        return 1;
    }
    p4_anim_materialize(cur);
    p4_anim_materialize(input);
    p4_anim_materialize(rotation);
    return 0;
}
/* mdlUpdateHierarchyPose (census FIXES) reads the frames and runs RtAnim calls of its own */
void p4_anim_lazy_pause(void *cur, int pause)
{
    if (pause)
        p4_anim_materialize(cur);
    lazy_paused += pause ? 1 : -1;
}
int p4r_003d59d0(void *interpolator, float time);
static unsigned sub_tracks, sub_retail, restart_tracks;
int func_003d59d0(void *interpolator, float time) /* SubAnimTime: walks back from the keys, then interpolates all */
{
    LazyAnim *e = lazy_find(interpolator);
    if (e) {
        lazy_walk(interpolator);
        lazy_obsolete++, lazy_drop(e);
    }
    /* b271: the step back from tracks (anim_track_sample) when retail would fire no callback and not wrap below 0 */
    char *ip = interpolator;
    float *w = (float *)ip;
    if (!e && p4_anim_tracks && *(char **)ip && !(w[5] < w[1] && w[1] - time <= w[5] && *(void **)(ip + 0xc)) && w[1] - time >= 0.0f) {
        float old = w[1];
        char *clone = p4_anim_lazy_check ? malloc(lazy_bytes(ip)) : 0;
        if (clone)
            memcpy(clone, ip, lazy_bytes(ip));
        w[1] = old - time;
        if (anim_track_sample(ip, 1)) {
            sub_tracks++;
            if (clone) {
                p4r_003d59d0(clone, time);
                lazy_compare(ip, clone);
                free(clone);
            }
            return 1;
        }
        w[1] = old;
        free(clone);
    }
    sub_retail++;
    return p4r_003d59d0(interpolator, time);
}
int p4r_003d5840(void *interpolator, void *animation);
int func_003d5840(void *interpolator, void *animation) /* SetCurrentAnim */
{
    lazy_obsolete_now(interpolator);
    /* b338: time held by p4_anim_far_kadd/p4_anim_skip_kadd belongs to the old animation (retail starts the new one
       at 0). Added to the new one it skipped ahead, or ran past a short animation's end and looped for a moment. */
    AnimState *s = anim_by_interp(interpolator);
    if (s)
        s->pending = 0;
    return p4r_003d5840(interpolator, animation);
}
int p4r_003d5e90(void *out, void *in1, void *in2, float alpha);
int func_003d5e90(void *out, void *in1, void *in2, float alpha) /* Blend: every node of out from in1 and in2 */
{
    p4_anim_materialize(in1);
    p4_anim_materialize(in2);
    lazy_obsolete_now(out);
    return p4r_003d5e90(out, in1, in2, alpha);
}
void p4r_003d5830(void *interpolator);
void func_003d5830(void *interpolator) /* Destroy */
{
    lazy_obsolete_now(interpolator);
    for (int i = 0; i < LAZY_SLOTS; i++) { /* a recorded blend or rebase that reads it cannot be replayed */
        LazyAnim *e = &lazy_slot[i];
        if (e->ip && e->first == interpolator)
            e->first = 0;
        if (e->ip && (e->input == interpolator || e->rotation == interpolator))
            e->layer = 0;
    }
    p4r_003d5830(interpolator);
}
int func_003d5bc0(void *interpolator, float time)
{
    kadd_calls++;
    if (interpolator == lazy_replay_input)
        return 1;
    int owned = lazy_owned(interpolator);
    if (owned) {
        if (lazy_defer(interpolator, time))
            return 1;
        lazy_refused++;
    }
    if ((p4_anim_far_kadd && (p4_anim_far || p4_anim_cull)) || (p4_anim_skip_kadd && skip_frame && p4_frameskip_bones && !owned)) {
        AnimState *s = anim_by_interp(interpolator);
        if (s && s->skinned) {
            kadd_mapped++;
            int unseen = p4_anim_tick - s->drawn > 2;
            int hold = p4_anim_far_kadd && ((p4_anim_cull && unseen) ||
                       (p4_anim_far && !unseen && s->depth > p4_anim_far_depth && p4_anim_tick - s->advanced < (unsigned)p4_anim_far_step &&
                        p4_anim_tick - s->moved >= 8));
            int skip = !hold && p4_anim_skip_kadd && skip_frame && p4_frameskip_bones && !owned;
            /* b306: hold only while the kept time and the next tick's step stay before the animation's end and its
               callback. The game clamps an animation at its end by the time it reads each tick; a held sum overshot
               the end and looped (Margaret's book lift after the Heaven battle showed its first pose again). */
            float ahead;
            if ((hold || skip) && !add_plain(interpolator, s->pending + 2.0f * time, &ahead))
                hold = skip = 0, kadd_near_end++;
            if ((hold || skip) && s->pending < 1.0f) { /* at most a second kept back */
                s->pending += time;
                kadd_held += hold;
                kadd_skip_held += skip;
                return 1;
            }
            time += s->pending;
            s->pending = 0;
            s->advanced = p4_anim_tick;
        }
    }
    /* b204 p4_anim_native: a plain step (no callback, no loop) in C: clock, keyed walk, interpolation. Exactly
       retail's work in retail's order; p4_anim_lazy_check runs retail on a copy and compares. */
    float t;
    int restart = 0;
    if (p4_anim_native && (add_plain(interpolator, time, &t) || (p4_anim_tracks && (restart = add_plain_at(interpolator, time, &t, 1))))) {
        char *ip = interpolator, *clone = 0;
        if (p4_anim_lazy_check && (clone = malloc(lazy_bytes(ip))))
            memcpy(clone, ip, lazy_bytes(ip));
        float before = *(float *)(ip + 4);
        *(float *)(ip + 4) = t;
        if (!anim_track_step(ip)) {
            if (restart) { /* b271: only tracks can follow a restart; retail does it */
                *(float *)(ip + 4) = before;
                free(clone);
                goto retail;
            }
            lazy_obsolete_now(ip); /* the walk catches up and every node is interpolated */
            lazy_walk(ip);
            lazy_interpolate(ip);
        } else
            lazy_obsolete_now(ip);
        restart_tracks += restart;
        native_adds++;
        if (clone) {
            p4r_003d5bc0(clone, time);
            lazy_compare(ip, clone);
            free(clone);
        }
        return 1;
    }
retail:;
    KInterp *cb = (KInterp *)((char *)interpolator + 0x44), saved = *cb;
    KInterp pick = saved ? p4_interp_pick(saved) : saved;
    if (pick != saved)
        *cb = pick;
    retail_adds++;
    int r = p4r_003d5bc0(interpolator, time);
    if (pick != saved)
        *cb = saved;
    if (time > 0.0f) /* walked and interpolated: nothing waits any more */
        lazy_obsolete_now(interpolator);
    return r;
}
void p4_interp_report(void)
{
    anim_track_report();
    p4_log("interp calls %u other %u; kadd far %d calls %u mapped %u held %u skip %d held %u near end %u\n", interp_calls, interp_other,
           p4_anim_far_kadd, kadd_calls, kadd_mapped, kadd_held, p4_anim_skip_kadd, kadd_skip_held, kadd_near_end);
    p4_log("anim lazy %d: deferred %u refused %u blends %u rebases %u materialized %u obsolete %u full %u waiting %d check %u diffs %u uv skipped %u\n",
           p4_anim_lazy, lazy_deferred, lazy_refused, lazy_blends, lazy_rebases, lazy_materialized, lazy_obsolete, lazy_full, lazy_used,
           lazy_checks, lazy_diffs, lazy_uv_skipped);
    p4_log("anim tracks sub: tracks %u retail %u; restarts by tracks %u\n", sub_tracks, sub_retail, restart_tracks);
    sub_tracks = sub_retail = restart_tracks = 0;
    p4_log("anim native %d: adds %u retail %u; keymap %d: keys walked %u maps built %u misses %u\n", p4_anim_native, native_adds, retail_adds,
           p4_anim_keymap, key_walked, key_maps_built, key_map_misses);
    native_adds = retail_adds = key_walked = key_maps_built = key_map_misses = 0;
    lazy_deferred = lazy_refused = lazy_blends = lazy_rebases = lazy_materialized = lazy_obsolete = lazy_full = lazy_checks = lazy_diffs = 0;
    lazy_uv_skipped = 0;
    interp_calls = interp_other = kadd_calls = kadd_mapped = kadd_held = kadd_skip_held = kadd_near_end = 0;
}
int func_00397c40(void *hierarchy)
{
    if (skip_frame && p4_frameskip_bones) {
        skipped_bone_updates++;
        return 1;
    }
    AnimState *s = anim_find(hierarchy);
    if (s && s->skinned) {
        int unseen = p4_anim_tick - s->drawn > 2;
        anim_unseen += unseen;
        if (!unseen && s->depth > p4_anim_far_depth)
            anim_far_due++;
        if (p4_anim_cull && unseen) {
            s->stale = 1;
            anim_culled++;
            return 1;
        }
        if (!unseen && anim_far_hold(s)) {
            s->stale = 1;
            anim_far_skipped++;
            return 1;
        }
    } else
        anim_unskinned++;
    anim_updates++;
    if (s)
        s->stale = 0, s->updated = p4_anim_tick;
    return hanim_update_now(hierarchy);
}
/* b240 p4_hanim_time (profiling build): cycles of the hierarchy update by part, read from the cycle counter around
   each part (4 reads a node, ~15 cycles each): waiting frames interpolated, the NaN scan, the prologue (parent LTM,
   dirty list), per node the matrix (callback or conversion, multiply) and the RwFrame writes. */
int p4_hanim_time = 1;
static unsigned long long hanim_t_wait, hanim_t_scan, hanim_t_pre, hanim_t_math, hanim_t_frame;
static unsigned hanim_t_nodes, hanim_t_framed, hanim_t_callback, hanim_t_calls;
#ifdef P4_HW_PROFILE
#define HANIM_NOW() (p4_hanim_time && prof_state > 0 ? prof_cycles() : 0)
#else
#define HANIM_NOW() 0u
#endif
extern int p4_pose_trace, p4_pose_trace_from, p4_pose_trace_len;
static void pose_trace(const unsigned *h);
static int hanim_update_now(void *hierarchy)
{
    unsigned t_w = HANIM_NOW();
    p4_anim_materialize((void *)((unsigned *)hierarchy)[8]); /* b202: currentAnim's frames, if they wait */
    unsigned t_s = HANIM_NOW();
    if (p4_hanim_nan_fix)
        hanim_sanitize(hierarchy);
    hanim_t_wait += t_s - t_w, hanim_t_scan += HANIM_NOW() - t_s;
    int r = p4_hanim_c ? p4_hanim_update(hierarchy) : p4r_00397c40(hierarchy);
    if (p4_pose_trace_from && cache_frame >= (unsigned)p4_pose_trace_from) /* game frames: PPSSPP's Tab cannot move it */
        p4_pose_trace = p4_pose_trace_len, p4_pose_trace_from = 0;
    if (p4_pose_trace)
        pose_trace(hierarchy);
    hanim_note(hierarchy);
    if (p4_head_trace)
        head_trace(hierarchy);
    if (p4_bone_check) { /* b157: are the matrices already bad right after the update, or made bad later? */
        unsigned *h = hierarchy;
        const float *m = (const float *)h[2];
        int n = (int)h[1];
        for (int i = 0; m && i < n && i < 256; i++, m += 16)
            if (hanim_bad(m, 3) || hanim_bad(m + 4, 3) || hanim_bad(m + 8, 3) || hanim_bad(m + 12, 3)) {
                hanim_nan_after_update++;
                if (hanim_nan_logged < 12) {
                    hanim_nan_logged++;
                    const char *frame = (const char *)h[8] + 76 + i * *(int *)((char *)h[8] + 36);
                    const float *qt = (const float *)(frame + 8);
                    p4_log("TRACE nan after update hierarchy %p node %d/%d: frame q %d %d %d %d t %d %d %d (x1000)\n", (void *)h, i, n,
                           (int)(qt[0] * 1000), (int)(qt[1] * 1000), (int)(qt[2] * 1000), (int)(qt[3] * 1000), (int)qt[4], (int)qt[5], (int)qt[6]);
                }
            }
    }
    return r;
}
static unsigned hanim_checks, hanim_diffs, hanim_calls;
static float hanim_max_diff;
extern char func_003954b0[];
int p4r_00397480(void *hierarchy);
int func_003e9240(void *frame);
void *func_003e9680(void *frame);
void *RwMatrixMultiply(void *out, const void *a, const void *b);

static void hanim_identity(Matrix *m)
{
    memset(m, 0, sizeof *m);
    m->right.x = m->up.y = m->at.z = 1;
    m->flags = 0x20003;
}

/* the inline RpHAnimKeyFrameToMatrix, same operations in the same order */
static void hanim_key_to_matrix(Matrix *m, const char *key)
{
    const float *q = (const float *)(key + 8), *t = (const float *)(key + 24);
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float xx = x * x, zz = z * z, yz = y * z, zx = z * x, xy = x * y, wx = w * x, yy = y * y, wy = w * y, wz = w * z;
    float one = 0.0f + 1.0f;
    m->right.x = one - 2.0f * (yy + zz);
    m->right.y = 2.0f * (xy + wz);
    m->right.z = 2.0f * (zx - wy);
    m->flags = 3;
    m->up.x = 2.0f * (xy - wz);
    m->up.y = one - 2.0f * (xx + zz);
    m->up.z = 2.0f * (yz + wx);
    m->at.x = 2.0f * (zx + wy);
    m->at.y = 2.0f * (yz - wx);
    m->at.z = one - 2.0f * (xx + yy);
    m->pos.x = t[0], m->pos.y = t[1], m->pos.z = t[2];
}

static int multiply_vfpu(Matrix *out, const Matrix *a, const Matrix *b);
static void hanim_mul(void *out, const void *a, const void *b)
{
    if (p4_hanim_vfpu && multiply_vfpu(out, a, b)) {
        ((Matrix *)out)->flags = ((const Matrix *)a)->flags & ((const Matrix *)b)->flags;
        return;
    }
    hanim_vfpu_unaligned += p4_hanim_vfpu;
    RwMatrixMultiply(out, a, b);
}

/* b239 p4_hanim_vfpu 2: one bone on the VFPU: the unit quaternion to the local matrix (the values of
   RpHAnimKeyFrameToMatrix: 1 - (2yy + 2zz) and so on) built in M600, where multiply_vfpu wants the rows of a, then
   through the parent as multiply_vfpu does. No FPU work and no call per bone. local and out get the rows; flags as
   the C path sets them. out, local and parent must be 16-byte aligned. */
static unsigned hanim_fused_nodes, hanim_hook_nodes;
static inline void hanim_node_vfpu(Matrix *out, Matrix *local, const char *key, const Matrix *parent)
{
    __asm__ volatile(
        "lv.s    S000, 8(%[k])\n lv.s S001, 12(%[k])\n lv.s S002, 16(%[k])\n lv.s S003, 20(%[k])\n"
        "lv.s    S630, 24(%[k])\n lv.s S631, 28(%[k])\n lv.s S632, 32(%[k])\n vone.s S633\n"
        "lv.q    C400, 0(%[b])\n lv.q C410, 16(%[b])\n lv.q C420, 32(%[b])\n lv.q C430, 48(%[b])\n"
        "vadd.q  C010, C000, C000\n"       /* 2x 2y 2z 2w */
        "vscl.t  C020, C010, S000\n"       /* 2xx 2xy 2xz */
        "vscl.t  C030, C010, S001\n"       /* 2xy 2yy 2yz */
        "vmul.s  S100, S002, S012\n"       /* 2zz */
        "vscl.t  C110, C010, S003\n"       /* 2wx 2wy 2wz */
        "vadd.s  S600, S031, S100\n vocp.s S600, S600\n"
        "vadd.s  S601, S021, S112\n"
        "vsub.s  S602, S022, S111\n"
        "vsub.s  S610, S021, S112\n"
        "vadd.s  S611, S020, S100\n vocp.s S611, S611\n"
        "vadd.s  S612, S032, S110\n"
        "vadd.s  S620, S022, S111\n"
        "vsub.s  S621, S032, S110\n"
        "vadd.s  S622, S020, S031\n vocp.s S622, S622\n"
        "vzero.s S603\n vzero.s S613\n vzero.s S623\n"
        "vtfm4.q C700, E400, C600\n"
        "vtfm4.q C710, E400, C610\n"
        "vtfm4.q C720, E400, C620\n"
        "vtfm4.q C730, E400, C630\n"
        "sv.q    C600, 0(%[l])\n sv.q C610, 16(%[l])\n sv.q C620, 32(%[l])\n sv.q C630, 48(%[l])\n"
        "sv.q    C700, 0(%[o])\n sv.q C710, 16(%[o])\n sv.q C720, 32(%[o])\n sv.q C730, 48(%[o])\n"
        : : [o] "r"(out), [l] "r"(local), [k] "r"(key), [b] "r"(parent) : "memory");
    local->flags = 3;
    out->flags = 3 & parent->flags;
}
/* The per-node hook of the game (mdlHierarchyScaleHook, installed for layers with flag 0x10) scales the key
   translation of one node, calls the saved callback and scales the matrix of one node: for every other node it is
   the saved callback. 1 when that is the stock conversion; *ka and *kb are the two keys the hook treats specially. */
void mdlHierarchyScaleHook(unsigned char *matrix, int key);
/* b241: the apply callback the characters really carry is the compressed-key scheme's p4r_00415ad0 (read from a live
   interpolator on the PSP, b240: +0x3c = 0x0889513c), not RpHAnim's func_003954b0. Its code is the same conversion
   (1 - 2(yy + zz) ..., pos = t, flags 3), so the fused node may stand in for either. */
void p4r_00415ad0(void *matrix, void *key);
static int hanim_convertible(const void *cb) { return cb == (const void *)func_003954b0 || cb == (const void *)p4r_00415ad0; }
static int hanim_hook_stock(const void *cb, const char **ka, const char **kb)
{
    if (cb != (const void *)mdlHierarchyScaleHook || !hanim_convertible((const void *)R32(0x922ba0)))
        return 0;
    *ka = (const char *)R32(0x922ba4), *kb = (const char *)R32(0x922ba8);
    return 1;
}
static inline void copy64(void *d, const void *s) /* 4-byte aligned words, no call */
{
    unsigned *x = d;
    const unsigned *y = s;
    for (int k = 0; k < 16; k++)
        x[k] = y[k];
}

int p4_hanim_update(unsigned *h)
{
    unsigned flags = h[0];
    unsigned t_in = HANIM_NOW();
    Matrix ltm_parent __attribute__((aligned(16))), local_root __attribute__((aligned(16)));
    const Matrix *parent = &ltm_parent;
    char *frame = 0;
    int have_frame = 0;
    if (flags & 2)
        return p4r_00397480(h);
    if ((flags & 1) && (int)h[7] != -1) {
        unsigned *ph = (unsigned *)h[6];
        parent = (const Matrix *)(ph[2] + (h[7] << 6));
        if ((flags & 0x2000) && (flags & 0x4000))
            frame = (char *)ph[5], have_frame = 1;
    } else if (flags & 0x4000) {
        hanim_identity(&local_root);
        parent = &local_root;
        if (flags & 0x2000)
            frame = (char *)h[5], have_frame = 1;
    } else {
        frame = (char *)h[5], have_frame = 1;
    }
    if (have_frame) {
        char *above = frame ? *(char **)(frame + 4) : 0;
        if (!above) {
            hanim_identity(&ltm_parent);
        } else if (!func_003e9240(above)) {
            ltm_parent = *(const Matrix *)p4r_003e9700(above);
        } else {
            ltm_parent = *(const Matrix *)(above + 16);
            for (char *p = *(char **)(above + 4); p; p = *(char **)(p + 4)) {
                Matrix t = ltm_parent;
                RwMatrixMultiply(&ltm_parent, &t, p + 16);
            }
        }
    }
    int update_ltm = flags & 0x2000;
    if (update_ltm) {
        unsigned char *root = *(unsigned char **)((char *)h[5] + 160);
        if (!(root[3] & 3)) { /* rwLinkListAddLLLink(dirty frames, &root->inDirtyListLink) */
            unsigned *head = (unsigned *)(0x88739C + P4_K);
            *(unsigned *)(root + 8) = head[0];
            *(unsigned *)(root + 12) = (unsigned)head;
            ((unsigned *)head[0])[1] = (unsigned)(root + 8);
            head[0] = (unsigned)(root + 8);
            root[3] |= 2;
        }
    }
    char *interp = (char *)h[8];
    int n = (int)h[1], stride = *(int *)(interp + 36);
    void (*key_to_matrix)(Matrix *, const char *) = *(void (**)(Matrix *, const char *))(interp + 60);
    const char *key = interp + 76;
    Matrix *m = (Matrix *)h[2];
    unsigned *info = (unsigned *)h[4];
    const Matrix *stack[64];
    int depth = 0;
    /* b239: stock conversion, or the scale hook over it (every node but its two special keys), on the VFPU */
    const char *hook_a = 0, *hook_b = 0;
    int stock = (char *)key_to_matrix == func_003954b0;
    int fused = p4_hanim_vfpu >= 2 && !((unsigned)m & 15) && (hanim_convertible(key_to_matrix) || hanim_hook_stock(key_to_matrix, &hook_a, &hook_b));
    hanim_t_pre += HANIM_NOW() - t_in;
    hanim_t_calls++, hanim_t_nodes += n, hanim_t_callback += stock ? 0 : n;
    for (int i = 0; i < n; i++, m++, key += stride, info += 4) {
        Matrix local __attribute__((aligned(16)));
        unsigned t_a = HANIM_NOW();
        if (fused && key != hook_a && key != hook_b && !((unsigned)parent & 15)) {
            hanim_node_vfpu(m, &local, key, parent);
            hanim_fused_nodes++, hanim_hook_nodes += !stock;
        } else {
            if (stock)
                hanim_key_to_matrix(&local, key);
            else
                key_to_matrix(&local, key);
            hanim_mul(m, &local, parent);
        }
        unsigned t_b = HANIM_NOW();
        hanim_t_math += t_b - t_a;
        unsigned char *f = (unsigned char *)info[3];
        hanim_t_framed += f != 0;
        if (f) {
            if (flags & 0x1000) {
                if (fused && !((unsigned)f & 3))
                    copy64(f + 16, &local);
                else
                    memcpy(f + 16, &local, sizeof local);
                if (!update_ltm)
                    func_003e9680(f);
            }
            if (update_ltm) {
                if (flags & 0x4000)
                    hanim_mul(f + 80, m, &ltm_parent);
                else if (fused && !((unsigned)f & 3))
                    copy64(f + 80, m);
                else
                    memcpy(f + 80, m, sizeof *m);
                f[3] = (f[3] & ~4) | 8;
            }
        }
        hanim_t_frame += HANIM_NOW() - t_b;
        switch (info[2] & 3) {
        case 0: parent = m; break;
        case 1: if (depth > 0) parent = stack[--depth]; break;
        case 2: if (depth < 64) stack[depth++] = parent; parent = m; break;
        default: break;
        }
    }
    return 1;
}

/* b273 p4_anim_freeze (measurement only: the upper bound of an animation rewrite): bit 1 no layer advance
   (mdlAdvanceLayer), 2 no bone matrices (func_00397c40, mdlUpdateHierarchyPose: the last pose stays), 4 no anim
   control (mdlAdvanceAnimControl). Poke it in a steady scene: logic waiting for an animation's end stalls. 0: normal. */
int p4_anim_freeze = 0;
/* p4_pose_trace N (diagnostic, poke it): for the next N frames, every bone update of a hierarchy with 20+ nodes logs
   "POSE frame hierarchy nodes anim time" and three nodes' positions relative to node 0 (x100), to find a pose that
   steps back while the animation time moves on. 0: off. */
int p4_pose_trace = 0;
/* p4_pose_trace_from F: start the trace (p4_pose_trace_len frames) at game frame F. 0: off. */
int p4_pose_trace_from = 0, p4_pose_trace_len = 3000;
static void pose_trace(const unsigned *h)
{
    static unsigned last_frame;
    if (cache_frame != last_frame) {
        last_frame = cache_frame;
        if (--p4_pose_trace <= 0) {
            p4_pose_trace = 0;
            return;
        }
    }
    int n = (int)h[1];
    const char *interp = (const char *)h[8];
    if (n < 1 || n > 256 || !interp || (unsigned)interp - 0x08800000u >= 0x03800000u)
        return;
    const Matrix *m = (const Matrix *)h[2];
    int k[3] = { n / 4, n / 2, 3 * n / 4 };
    int p[9];
    for (int i = 0; i < 3; i++) {
        p[i * 3] = (int)((m[k[i]].pos.x - m[0].pos.x) * 100);
        p[i * 3 + 1] = (int)((m[k[i]].pos.y - m[0].pos.y) * 100);
        p[i * 3 + 2] = (int)((m[k[i]].pos.z - m[0].pos.z) * 100);
    }
    p4_log("POSE %u %x %d %x %d %d %d %d %d %d %d %d %d %d\n", cache_frame, (unsigned)h, n, *(const unsigned *)interp,
           (int)(*(const float *)(interp + 4) * 1000), p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8]);
}
/* every 8th call with p4_hanim_check: the original, then this one on the same hierarchy; the bone matrices compared */
int p4_hanim_dispatch(unsigned *h, int (*original)(void *))
{
    if (p4_anim_freeze & 2)
        return 1;
    hanim_calls++;
    if (p4_hanim_check && !(hanim_calls & 7) && !(h[0] & 2) && (int)h[1] > 0 && (int)h[1] <= 256) {
        static Matrix saved[256];
        int n = (int)h[1];
        p4r_00397c40(h); /* the retail update, not original: that is func_00397c40, which may take the C path now */
        memcpy(saved, (void *)h[2], n * sizeof(Matrix));
        p4_hanim_update(h);
        const Matrix *now = (const Matrix *)h[2];
        hanim_checks++;
        for (int i = 0; i < n; i++) {
            const float *a = &saved[i].right.x, *b = &now[i].right.x;
            for (int k = 0; k < 16; k++) {
                if (k % 4 == 3)
                    continue; /* flags and pads */
                float d = a[k] - b[k];
                if (d < 0) d = -d;
                if (d > 0) hanim_diffs++;
                if (d > hanim_max_diff) hanim_max_diff = d;
            }
        }
        return 1;
    }
    return original(h);
}
static void hanim_report(void)
{
    p4_log("hanim c %d vfpu %d check %d: calls %u checks %u differing floats %u max diff %d/1000000 unaligned %u\n", p4_hanim_c,
           p4_hanim_vfpu, p4_hanim_check, hanim_calls, hanim_checks, hanim_diffs, (int)(hanim_max_diff * 1000000), hanim_vfpu_unaligned);
    p4_log("hanim fused nodes %u of them not through func_003954b0 %u\n", hanim_fused_nodes, hanim_hook_nodes);
    p4_log("hanim time %d kcycles: calls %u nodes %u framed %u callback %u; waiting frames %u nan scan %u prologue %u matrices %u frame writes %u\n",
           p4_hanim_time, hanim_t_calls, hanim_t_nodes, hanim_t_framed, hanim_t_callback, (unsigned)(hanim_t_wait / 1000), (unsigned)(hanim_t_scan / 1000),
           (unsigned)(hanim_t_pre / 1000), (unsigned)(hanim_t_math / 1000), (unsigned)(hanim_t_frame / 1000));
    hanim_t_wait = hanim_t_scan = hanim_t_pre = hanim_t_math = hanim_t_frame = 0;
    hanim_t_nodes = hanim_t_framed = hanim_t_callback = hanim_t_calls = 0;
    p4_log("skin free waits %u (wait %d); skin dummy words %u (dummy %d)\n", skin_free_waits, p4_skin_free_wait, skin_dummies, p4_skin_dummy);
    p4_log("skin lod depth %d keep %d: draws simplified %u full %u\n", (int)p4_skin_lod_depth, p4_skin_lod_keep, skin_lod_draws, skin_full_draws);
    p4_log("skin far depth %d keep %d bones %d: draws %u fallbacks %u; builds %u bones %u of %u, %u ms\n", (int)p4_skin_far_depth, p4_skin_far_keep,
           p4_skin_far_bones, skin_far_draws, skin_far_fallbacks, skin_far_builds, skin_far_bones_kept, skin_far_bones_in, skin_far_us / 1000);
    skin_lod_draws = skin_full_draws = skin_far_draws = skin_far_builds = skin_far_bones_kept = skin_far_bones_in = skin_far_us = skin_far_fallbacks = 0;
    skin_free_waits = skin_dummies = 0;
    hanim_fused_nodes = hanim_hook_nodes = 0;
    hanim_calls = hanim_checks = hanim_diffs = hanim_vfpu_unaligned = 0;
    hanim_max_diff = 0;
}

