/* b376 cache arena. The port's caches (mesh, skin, static lists, far stand-ins, particle spill, guard boxes, CLUT8
   copies...) live in one block taken from the heap at boot, with their own allocator, so they never sit between the
   game's blocks. b373-b375: the caches held ~13 MB of a 38 MB heap, interleaved with the game's blocks; after every
   cache was freed the heap had 18.5 MB free but no run of 2 MB, and the game's chkmemAllocate wrote through the null
   block (bus error, the same crash as b360). With the arena the game's heap holds only the game's blocks, as on the
   PS2.

   Allocator: two-level segregated fit (TLSF-like). Blocks are 16-byte aligned with a 16-byte header (previous
   block's size, this size | flags, and for free blocks the free-list links); free neighbours merge at once. A failed
   arena allocation returns 0: every cache already copes with a refused block (it draws the slow path). p4a_free
   takes any pointer: outside the arena it goes to the heap's free. */
enum { ARENA_FL = 26, ARENA_SL = 8, ARENA_SL_SHIFT = 3, ARENA_MIN_BLOCK = 32, ARENA_SMALL = 128 };
enum { AB_FREE = 1, AB_PREV_FREE = 2 };
#include <pspsdk.h>
typedef struct ArenaBlock { unsigned prev_size, size; struct ArenaBlock *next, *prev; } ArenaBlock;
/* read once at boot (p4_arena_init); 0: no arena, the caches stay on the heap as before b376. b378: 0 by default.
   b376 on the PSP, field 20 room 1: 12 MB was full (10.8 MB used, largest free 4 KB, 7400 refusals a window), the
   static lists re-recorded every frame and the frame took 106 ms. Needs per-cache budgets that fit the arena first. */
int p4_arena_kb = 0;
static unsigned char *arena_base, *arena_end;
static unsigned arena_fl_bits, arena_sl_bits[ARENA_FL];
static ArenaBlock *arena_free[ARENA_FL][ARENA_SL];
static unsigned arena_used, arena_peak, arena_fails, arena_allocs;
static int arena_ready;

static int arena_fls(unsigned v) { return 31 - __builtin_clz(v); }
static int arena_ffs(unsigned v) { return __builtin_ctz(v); }
static unsigned ab_size(const ArenaBlock *b) { return b->size & ~15u; }
static ArenaBlock *ab_next(ArenaBlock *b) { return (ArenaBlock *)((unsigned char *)b + ab_size(b)); }

/* class of a size: sizes below ARENA_SMALL in 16-byte steps (fl 0), above in 8 steps per power of two */
static void arena_mapping(unsigned size, int *fl, int *sl)
{
    if (size < ARENA_SMALL) {
        *fl = 0, *sl = size / (ARENA_SMALL / ARENA_SL);
    } else {
        int f = arena_fls(size);
        *sl = (size >> (f - ARENA_SL_SHIFT)) & (ARENA_SL - 1);
        *fl = f - 6; /* 128 -> 1 */
    }
}
/* first class whose every block is at least size */
static void arena_mapping_search(unsigned size, int *fl, int *sl)
{
    if (size >= ARENA_SMALL)
        size += (1u << (arena_fls(size) - ARENA_SL_SHIFT)) - 1;
    arena_mapping(size, fl, sl);
}
static void arena_insert(ArenaBlock *b)
{
    int fl, sl;
    arena_mapping(ab_size(b), &fl, &sl);
    b->prev = 0, b->next = arena_free[fl][sl];
    if (b->next)
        b->next->prev = b;
    arena_free[fl][sl] = b;
    arena_fl_bits |= 1u << fl, arena_sl_bits[fl] |= 1u << sl;
}
static void arena_remove(ArenaBlock *b)
{
    int fl, sl;
    arena_mapping(ab_size(b), &fl, &sl);
    if (b->prev)
        b->prev->next = b->next;
    else
        arena_free[fl][sl] = b->next;
    if (b->next)
        b->next->prev = b->prev;
    if (!arena_free[fl][sl] && !(arena_sl_bits[fl] &= ~(1u << sl)))
        arena_fl_bits &= ~(1u << fl);
}
static ArenaBlock *arena_find(unsigned size)
{
    int fl, sl;
    arena_mapping_search(size, &fl, &sl);
    if (fl >= ARENA_FL)
        return 0;
    unsigned bits = arena_sl_bits[fl] & (~0u << sl);
    if (!bits) {
        unsigned f = arena_fl_bits & (fl + 1 < 32 ? ~0u << (fl + 1) : 0);
        if (!f)
            return 0;
        fl = arena_ffs(f);
        bits = arena_sl_bits[fl];
    }
    return arena_free[fl][arena_ffs(bits)];
}
/* b is free, outside the lists; mark it and its successor */
static void arena_mark_free(ArenaBlock *b)
{
    b->size |= AB_FREE;
    ArenaBlock *n = ab_next(b);
    n->prev_size = ab_size(b);
    n->size |= AB_PREV_FREE;
}
/* split a free block (outside the lists) to size; the rest goes back free */
static void arena_split(ArenaBlock *b, unsigned size)
{
    unsigned have = ab_size(b);
    if (have - size < ARENA_MIN_BLOCK)
        return;
    ArenaBlock *rest = (ArenaBlock *)((unsigned char *)b + size);
    rest->size = (have - size) | AB_FREE; /* its previous (b) is used once the caller takes it */
    b->size = size | (b->size & 15);
    arena_mark_free(rest);
    arena_insert(rest);
}
static void arena_take(ArenaBlock *b)
{
    b->size &= ~AB_FREE;
    ab_next(b)->size &= ~AB_PREV_FREE;
}

void p4_arena_init(void)
{
    extern void *__real_memalign(size_t, size_t);
    unsigned bytes = (unsigned)p4_arena_kb << 10;
    if (arena_ready || !bytes)
        return;
    unsigned char *p = __real_memalign(64, bytes);
    if (!p) {
        p4_log("arena: no %u KB at boot, caches stay on the heap\n", bytes >> 10);
        return;
    }
    arena_base = p, arena_end = p + bytes;
    ArenaBlock *first = (ArenaBlock *)p, *end = (ArenaBlock *)(arena_end - sizeof(ArenaBlock));
    first->prev_size = 0, first->size = (bytes - sizeof(ArenaBlock)) | AB_FREE;
    end->prev_size = ab_size(first), end->size = 0 | AB_PREV_FREE; /* sentinel: size 0, never free */
    arena_insert(first);
    arena_ready = 1;
    p4_log("arena: %u KB at %x for the caches\n", bytes >> 10, (unsigned)p);
}

static int arena_owns(const void *p) { return arena_ready && (const unsigned char *)p >= arena_base && (const unsigned char *)p < arena_end; }
int p4_arena_owns(const void *p) { return arena_owns(p); }
int p4_arena_active(void) { return arena_ready; }

void *p4a_memalign(unsigned align, unsigned n)
{
    extern void *__wrap_malloc(size_t);
    if (!arena_ready)
        return align <= 16 ? malloc(n) : memalign(align, n);
    if (align < 16)
        align = 16;
    unsigned size = ((n ? n : 1) + 15 & ~15u) + 16, want = size + (align > 16 ? align + ARENA_MIN_BLOCK : 0);
    int intr = pspSdkDisableInterrupts();
    ArenaBlock *b = arena_find(want);
    if (!b) {
        arena_fails++;
        pspSdkEnableInterrupts(intr);
        return 0;
    }
    arena_remove(b);
    if (align > 16) { /* move to an aligned payload; the gap before it becomes a free block of its own */
        unsigned payload = (unsigned)b + 16, aligned = (payload + align - 1) & ~(align - 1);
        while (aligned != payload && aligned - payload < ARENA_MIN_BLOCK)
            aligned += align;
        if (aligned != payload) {
            unsigned gap = aligned - payload;
            ArenaBlock *a = (ArenaBlock *)((unsigned char *)b + gap);
            a->size = (ab_size(b) - gap) | AB_FREE | AB_PREV_FREE;
            b->size = gap | AB_FREE | (b->size & AB_PREV_FREE);
            arena_mark_free(b); /* sets a->prev_size and a's PREV_FREE */
            arena_mark_free(a);
            arena_insert(b);
            b = a;
        }
    }
    arena_split(b, size);
    arena_take(b);
    arena_used += ab_size(b), arena_allocs++;
    if (arena_used > arena_peak)
        arena_peak = arena_used;
    pspSdkEnableInterrupts(intr);
    return (unsigned char *)b + 16;
}
void *p4a_malloc(unsigned n) { return p4a_memalign(16, n); }
void *p4a_calloc(unsigned a, unsigned b)
{
    unsigned n = a * b;
    void *p = p4a_memalign(16, n);
    if (p)
        memset(p, 0, n);
    return p;
}
void p4a_free(void *p)
{
    extern void __wrap_free(void *);
    if (!p)
        return;
    if (!arena_owns(p)) {
        __wrap_free(p);
        return;
    }
    int intr = pspSdkDisableInterrupts();
    ArenaBlock *b = (ArenaBlock *)((unsigned char *)p - 16);
    arena_used -= ab_size(b);
    if (b->size & AB_PREV_FREE) { /* merge with the free block before */
        ArenaBlock *before = (ArenaBlock *)((unsigned char *)b - b->prev_size);
        arena_remove(before);
        before->size = (ab_size(before) + ab_size(b)) | AB_FREE | (before->size & AB_PREV_FREE);
        b = before;
    }
    ArenaBlock *n = ab_next(b);
    if (n->size & AB_FREE) { /* and with the one after */
        arena_remove(n);
        b->size = (ab_size(b) + ab_size(n)) | (b->size & AB_PREV_FREE);
    }
    arena_mark_free(b);
    arena_insert(b);
    pspSdkEnableInterrupts(intr);
}
void *p4a_realloc(void *old, unsigned n)
{
    extern void *__wrap_realloc(void *, size_t);
    if (old && !arena_owns(old))
        return __wrap_realloc(old, n);
    if (!n) {
        p4a_free(old);
        return 0;
    }
    void *p = p4a_malloc(n);
    if (p && old) {
        unsigned have = ab_size((ArenaBlock *)((unsigned char *)old - 16)) - 16;
        memcpy(p, old, have < n ? have : n);
        p4a_free(old);
    }
    return p;
}
/* free bytes and the largest free block, for budgets and the log */
static unsigned arena_largest(void)
{
    if (!arena_ready || !arena_fl_bits)
        return 0;
    int fl = arena_fls(arena_fl_bits), sl = arena_fls(arena_sl_bits[fl]);
    unsigned best = 0;
    for (ArenaBlock *b = arena_free[fl][sl]; b; b = b->next)
        if (ab_size(b) > best)
            best = ab_size(b);
    return best > 16 ? best - 16 : 0;
}
static unsigned arena_free_bytes(void) { return arena_ready ? (unsigned)(arena_end - arena_base) - arena_used : 0; }
static void arena_report(void)
{
    if (!arena_ready)
        return;
    p4_log("arena %u KB: used %u KB peak %u KB largest free %u KB; allocs %u refused %u\n", (unsigned)(arena_end - arena_base) >> 10,
           arena_used >> 10, arena_peak >> 10, arena_largest() >> 10, arena_allocs, arena_fails);
    arena_allocs = arena_fails = 0;
    arena_peak = arena_used;
}
