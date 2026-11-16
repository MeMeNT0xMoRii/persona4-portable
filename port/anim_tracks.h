#ifndef P4_ANIM_TRACKS_H
#define P4_ANIM_TRACKS_H
/* b269 p4_anim_tracks: RtAnim compressed-key animations (RpHAnim's p4r_00415bc0) sampled from per-node tracks.

   Retail AddAnimTime walks one key stream in which the keys of every node are interleaved, hands each due key to its
   node (pointer chasing: the key's previous-key pointer, the previous key's time, a search of the node frames), then
   decodes two 16-bit-float keys per node and interpolates. In the dialogue that is ~1.5 M cycles a frame for ~740
   nodes (b263, `kadd`), mostly memory stall: each node reads two keys that lie a whole node-count of keys apart.

   Here each animation is converted once into tracks: per node its keys in time order, decoded to floats, 32 bytes a
   key, so a node's two keys share one or two cache lines and stay warm from tick to tick. A step has no walk: a
   node's current key is found from the key pointer its frame already holds, moved forward along its own track. The
   state the game and RtAnim read is written as retail leaves it - every frame's previous/next key pointers, its
   quaternion and translation, and the interpolator's next-key pointer (+8) - so retail calls (SubAnimTime, Blend,
   SetCurrentAnim, a looping step) continue from it. Nothing is kept per interpolator.

   Exactness: the linear case (dot at or above retail's 0x3f7fbe77) is the retail arithmetic in retail order on the
   decoded floats (the decode is exact). A slerp node calls retail p4r_00415bc0 on the original keys. The next-key
   pointer is derived rather than walked; that holds when the stream is ordered by previous-key time, which the build
   checks (else no tracks: retail path). p4_anim_lazy_check 1 runs retail on a copy and compares the words exactly.

   Assumes an animation's keys are not rewritten after its first use (the build samples a checksum; a check run
   catches it). 0 off, 1 on. Tracks share p4_anim_tracks_budget and are evicted least recently used.

   Memory (b272): the game allocates through the same heap (chkmemAllocate -> malloc) and writes through a null block
   when it runs out (b271: bus error loading the dialogue with 84 animations, 1.7 MB of tracks). A build needs
   p4_anim_tracks_reserve bytes of heap left free after it, and tracks not used for TRACK_IDLE ticks (an animation of
   the scene before) are freed. Last resort: a game allocation that fails on the animation thread frees every track
   and is retried (p4_anim_tracks_release, from main.c's malloc wrappers); builds then pause for 10 s. */
int p4_anim_tracks = 0; /* b272 dialogue, unprofiled: cpu 56.60 / 56.62 / 56.38 / 56.25 ms off/on/off/on: no gain, off */
int p4_anim_tracks_budget = 1 << 20;
int p4_anim_tracks_reserve = 3 << 19; /* 1.5 MB: above the mesh cache's 1 MB, so tracks give way first */
enum { TRACK_IDLE = 90 }; /* ticks, 3 s */

typedef struct { float time, q[4], t[3]; } TrackKey; /* 32 bytes: a key's time, its quaternion and translation */
typedef struct {
    const char *anim, *frames;
    int count, nodes;
    unsigned stamp, bytes, sum;
    int back;               /* every node's second key points back at its first: SubAnimTime can be followed */
    TrackKey *key;          /* per node, its keys in stream order; node i's run starts at first[i] */
    unsigned short *first;  /* nodes + 1 entries */
    unsigned short *pos;    /* stream index -> position in key[] */
    unsigned short *sidx;   /* position in key[] -> stream index */
} AnimTrack;
enum { ANIM_TRACKS = 128 };
static AnimTrack *anim_track;
static int track_building;
static SceUID track_thread = -1;
static unsigned track_pause_until, track_emergencies;
static unsigned track_back, track_back_refused, track_low_memory, track_idle_freed, track_bytes, track_builds, track_refused, track_steps, track_nodes, track_slerps, track_evictions, track_fallbacks;
void p4r_00415bc0(void *out, void *in1, void *in2, float time, void *data);

static inline float track_half(unsigned short h) /* retail's decode (00415bc0): exact */
{
    union { unsigned u; float f; } v;
    v.u = (h & 0x8000u) << 16;
    if (h & 0x7fff)
        v.u |= (((h & 0x7800u) << 12) + 0x38000000u) | ((h & 0x7ffu) << 12);
    return v.f;
}
static unsigned track_checksum(const char *frames, int count)
{
    unsigned s = 2166136261u;
    for (int k = 0; k < count; k += 1 + count / 64) /* ~64 sampled keys, every word of each */
        for (int w = 1; w < 6; w++)
            s = (s ^ ((const unsigned *)(frames + k * 24))[w]) * 16777619u;
    return s;
}
static void track_free(AnimTrack *e)
{
    track_bytes -= e->bytes;
    free(e->key), free(e->first), free(e->pos), free(e->sidx);
    memset(e, 0, sizeof *e);
}
/* 0: this layout is not one tracks can follow exactly (the retail path stays) */
static int track_build(AnimTrack *e, const char *ip)
{
    const char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10);
    int count = *(int *)(anim + 4), n = *(int *)(ip + 0x2c);
    if (count < 2 * n || count >= 0xffff || n <= 0 || n >= 0xffff)
        return 0;
    unsigned bytes = count * (sizeof(TrackKey) + 4) + (n + 1) * 2;
    if ((int)(track_pause_until - p4_anim_tick) > 0)
        return 0;
    while (track_bytes + bytes > (unsigned)p4_anim_tracks_budget) { /* evict the least recently used */
        AnimTrack *old = 0;
        for (int i = 0; i < ANIM_TRACKS; i++)
            if (anim_track[i].anim && &anim_track[i] != e && (!old || (int)(anim_track[i].stamp - old->stamp) < 0))
                old = &anim_track[i];
        if (!old)
            return 0;
        track_free(old), track_evictions++;
    }
    {
        struct mallinfo heap = mallinfo();
        extern unsigned p4_heap_unclaimed(void);
        if (heap.fordblks + p4_heap_unclaimed() < bytes + (unsigned)p4_anim_tracks_reserve) {
            track_low_memory++;
            return 0;
        }
    }
    track_building = 1; /* an allocation failing in here must not free tracks under our feet */
    unsigned short *node = malloc(count * 2), *fill = calloc(n + 1, 2);
    e->key = memalign(64, count * sizeof(TrackKey));
    e->first = calloc(n + 1, 2), e->pos = malloc(count * 2), e->sidx = malloc(count * 2);
    int ok = node && fill && e->key && e->first && e->pos && e->sidx;
    /* node of each key: keys 0..2n-1 are node k mod n (SetCurrentAnim), a later key the node of its previous key; the
       stream must be ordered by the previous key's time from 2n on */
    float last = -1e30f;
    int back = 1;
    for (int k = 0; ok && k < count; k++) {
        if (k < 2 * n) {
            node[k] = k % n;
            continue;
        }
        const char *prev = *(char **)(frames + k * 24);
        int at = prev - frames;
        float pt = at >= 0 && at % 24 == 0 && at / 24 < k ? *(const float *)(prev + 4) : 0;
        if (at < 0 || at % 24 || at / 24 >= k || !(pt >= last)) {
            ok = 0;
            break;
        }
        last = pt;
        node[k] = node[at / 24];
    }
    for (int k = 0; ok && k < count; k++)
        e->first[node[k] + 1]++;
    for (int i = 0; ok && i < n; i++) {
        e->first[i + 1] += e->first[i];
        if (e->first[i + 1] - e->first[i] < 2) /* every node has its two first keys */
            ok = 0;
    }
    for (int k = 0; ok && k < count; k++) {
        int p = e->first[node[k]] + fill[node[k]]++;
        const char *src = frames + k * 24;
        TrackKey *d = &e->key[p];
        d->time = *(const float *)(src + 4);
        for (int c = 0; c < 4; c++)
            d->q[c] = track_half(*(const unsigned short *)(src + 8 + c * 2));
        for (int c = 0; c < 3; c++)
            d->t[c] = track_half(*(const unsigned short *)(src + 16 + c * 2));
        e->pos[k] = p, e->sidx[p] = k;
        /* a node's keys follow each other: each one's previous key is the one before it on the track */
        if (fill[node[k]] > 2 && *(char **)src != frames + e->sidx[p - 1] * 24)
            ok = 0;
        if (fill[node[k]] == 2 && *(char **)src != frames + e->sidx[p - 1] * 24)
            back = 0; /* retail's step back reads this pointer */
        if (fill[node[k]] > 1 && !(d->time >= e->key[p - 1].time))
            ok = 0;
    }
    free(node), free(fill);
    track_building = 0;
    if (!ok) {
        free(e->key), free(e->first), free(e->pos), free(e->sidx);
        memset(e, 0, sizeof *e);
        return 0;
    }
    e->anim = anim, e->frames = frames, e->count = count, e->nodes = n, e->bytes = bytes, e->back = back;
    e->sum = track_checksum(frames, count);
    track_bytes += bytes;
    track_builds++;
    return 1;
}
static AnimTrack *track_find(const char *ip)
{
    const char *anim = *(char **)ip, *frames = *(char **)(anim + 0x10);
    int count = *(int *)(anim + 4), n = *(int *)(ip + 0x2c);
    if (!anim_track && !(anim_track = calloc(ANIM_TRACKS, sizeof *anim_track)))
        return 0;
    track_thread = sceKernelGetThreadId();
    static unsigned swept;
    if (p4_anim_tick - swept >= 32) { /* free what the scene no longer plays, refusal markers included */
        swept = p4_anim_tick;
        for (int i = 0; i < ANIM_TRACKS; i++)
            if (anim_track[i].anim && p4_anim_tick - anim_track[i].stamp > TRACK_IDLE)
                track_idle_freed += anim_track[i].key != 0, track_free(&anim_track[i]);
    }
    unsigned h = ((unsigned)anim >> 4) * 2654435761u >> 25; /* 128 slots, then a short probe */
    AnimTrack *free_slot = 0, *oldest = 0;
    for (int i = 0; i < 4; i++) {
        AnimTrack *e = &anim_track[(h + i) & (ANIM_TRACKS - 1)];
        if (e->anim == anim) {
            if (e->frames == frames && e->count == count && e->nodes == n) {
                e->stamp = p4_anim_tick;
                return e;
            }
            track_free(e); /* reused address: rebuild */
            free_slot = e;
            break;
        }
        if (!e->anim && !free_slot)
            free_slot = e;
        if (e->anim && (!oldest || (int)(e->stamp - oldest->stamp) < 0))
            oldest = e;
    }
    AnimTrack *e = free_slot ? free_slot : oldest;
    if (!e)
        return 0;
    if (e->anim)
        track_free(e), track_evictions++;
    if (!track_build(e, ip)) {
        track_refused++;
        /* remember the refusal: a marker entry with no keys, so the build is not tried every tick */
        e->anim = anim, e->frames = frames, e->count = count, e->nodes = n, e->stamp = p4_anim_tick;
        return 0;
    }
    e->stamp = p4_anim_tick;
    return e;
}

/* Every node at the interpolator's current time (+4), from tracks. back 0: AddAnimTime's plain step - each node's
   pair moves forward to the last key not after the time (also from SubAnimTime's state, whose next-key pointer is 0:
   retail restarts and walks from the first key, which ends in the same pairs), and the next-key pointer is derived.
   back 1: SubAnimTime - each pair steps back until its first key is not after the time; the next-key pointer is 0, as
   retail leaves it. 1: done; 0: nothing was written, the caller takes the retail or lazy path. */
static int anim_track_sample(char *ip, int back)
{
    if (!p4_anim_tracks)
        return 0;
    KInterp cb = *(KInterp *)(ip + 0x44);
#ifdef P4_HW_PROFILE
    if (cb == kinterp_timed)
        cb = kinterp_now;
#endif
    char *anim = *(char **)ip;
    int stride = *(int *)(ip + 0x24), n = *(int *)(ip + 0x2c);
    if (cb != (KInterp)func_00415bc0 || !anim || *(int *)(ip + 0x28) != 24 || stride < 36 || n <= 0)
        return 0;
    AnimTrack *e = track_find(ip);
    if (!e || !e->key)
        return 0;
    if (back && !e->back) {
        track_back_refused++;
        return 0;
    }
    if (p4_anim_lazy_check && track_checksum(e->frames, e->count) != e->sum) { /* the keys were rewritten: rebuild */
        p4_log("TRACE anim tracks: keys of %p changed after the build\n", (void *)e->anim);
        track_free(e);
        track_fallbacks++;
        return 0;
    }
    const char *frames = e->frames;
    const float time = *(float *)(ip + 4), *custom = *(float **)(anim + 0x14);
    char *f = ip + 0x4c;
    /* first, before anything is written: every frame holds a key of its own node in this animation */
    for (int i = 0; i < n; i++, f += stride) {
        unsigned at = *(const char **)f - frames;
        int p = at < (unsigned)e->count * 24 && !(at % 24) ? e->pos[at / 24] : -1;
        if (p < e->first[i] || p >= e->first[i + 1] - 1) {
            track_fallbacks++;
            return 0;
        }
    }
    int next = e->count; /* the interpolator's next stream key: the earliest key not yet handed to its node */
    f = ip + 0x4c;
    for (int i = 0; i < n; i++, f += stride) {
        int p = e->pos[(unsigned)(*(const char **)f - frames) / 24], begin = e->first[i], end = e->first[i + 1] - 1;
        if (back) {
            while (p > begin && time < e->key[p].time)
                p--;
        } else {
            while (p < end - 1 && e->key[p + 1].time <= time)
                p++;
            if (p + 2 <= end && e->sidx[p + 2] < next)
                next = e->sidx[p + 2];
        }
        const TrackKey *a = &e->key[p], *b = a + 1;
        char *k1 = (char *)frames + e->sidx[p] * 24, *k2 = (char *)frames + e->sidx[p + 1] * 24;
        *(char **)f = k1;
        *(char **)(f + 4) = k2;
        /* retail 00415bc0: f, the dot in its accumulator order, translation lerp scaled and offset; q2 negated for a
           negative dot; slerp below 0x3f7fbe77, else the weights 1 - f and f */
        float fr = (time - a->time) / (b->time - a->time);
        float dot = a->q[1] * b->q[1] + a->q[0] * b->q[0] + a->q[2] * b->q[2] + a->q[3] * b->q[3];
        float sign = 1.0f;
        if (dot < 0.0f)
            sign = -1.0f, dot = -dot;
        union { float f; unsigned u; } lim = { .u = 0x3f7fbe77u };
        if (!(dot >= lim.f)) { /* slerp (and a NaN dot): retail on the original keys */
            p4r_00415bc0(f, k1, k2, time, (void *)custom);
            track_slerps++;
            continue;
        }
        float *out = (float *)(f + 8);
        for (int c = 0; c < 3; c++)
            out[4 + c] = (fr * (b->t[c] - a->t[c]) + a->t[c]) * custom[3 + c] + custom[c];
        float s0 = 1.0f - fr, s1 = fr;
        for (int c = 0; c < 4; c++)
            out[c] = s1 * (sign * b->q[c]) + s0 * a->q[c];
    }
    *(char **)(ip + 8) = back ? 0 : (char *)frames + next * 24;
    track_steps++;
    track_back += back;
    track_nodes += n;
    return 1;
}
static int anim_track_step(char *ip) { return anim_track_sample(ip, 0); }
/* main.c's malloc wrappers, when an allocation fails: every track freed if the caller is the animation thread (no
   sample or build can be in progress there), else nothing. Returns the bytes freed. */
unsigned p4_anim_tracks_release(void)
{
    if (!anim_track || track_building || sceKernelGetThreadId() != track_thread)
        return 0;
    unsigned before = track_bytes;
    for (int i = 0; i < ANIM_TRACKS; i++)
        if (anim_track[i].anim)
            track_free(&anim_track[i]);
    track_pause_until = p4_anim_tick + 300;
    track_emergencies++;
    return before - track_bytes;
}
static void anim_track_report(void)
{
    int used = 0;
    for (int i = 0; anim_track && i < ANIM_TRACKS; i++)
        used += anim_track[i].key != 0;
    p4_log("anim tracks %d: steps %u (back %u, refused %u) nodes %u slerps %u fallbacks %u; builds %u refused %u (low memory %u) "
           "evictions %u idle %u emergencies %u; %d animations %u KB\n", p4_anim_tracks, track_steps, track_back, track_back_refused, track_nodes,
           track_slerps, track_fallbacks, track_builds, track_refused, track_low_memory, track_evictions, track_idle_freed, track_emergencies, used,
           track_bytes >> 10);
    track_back_refused = track_low_memory = track_idle_freed = 0;
    track_steps = track_back = track_nodes = track_slerps = track_fallbacks = track_builds = track_refused = track_evictions = 0;
}
#endif
