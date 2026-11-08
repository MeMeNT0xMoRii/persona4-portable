/* Test the actual CPU topology cache, including all-index invalidation, bounded
   memory, frame-pool ownership, allocation failures, and randomized meshes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_malloc, fail_calloc;
static unsigned allocations;
typedef struct { size_t bytes; uint64_t magic; } Allocation;
static const uint64_t canary = UINT64_C(0x72182ccbe4f36a19);
static void *checked_malloc(size_t bytes)
{
    if (fail_malloc) return 0;
    Allocation *a = malloc(sizeof *a + bytes + sizeof canary);
    assert(a);
    a->bytes = bytes; a->magic = canary;
    memcpy((char *)(a + 1) + bytes, &canary, sizeof canary);
    allocations++;
    return a + 1;
}
static void *checked_calloc(size_t n, size_t bytes)
{
    if (fail_calloc) return 0;
    void *p = checked_malloc(n * bytes);
    if (p) memset(p, 0, n * bytes);
    return p;
}
static void checked_free(void *p)
{
    if (!p) return;
    Allocation *a = (Allocation *)p - 1;
    uint64_t end;
    memcpy(&end, (char *)p + a->bytes, sizeof end);
    assert(a->magic == canary && end == canary);
    assert(allocations);
    allocations--;
    free(a);
}
#define P4_TOPO_MALLOC checked_malloc
#define P4_TOPO_CALLOC checked_calloc
#define P4_TOPO_FREE checked_free
#include "mesh_topology.h"

static void verify(const P4Topology *t, const unsigned short *input, int n)
{
    assert(t && t->count == n && !memcmp(t->data, input, n * 2));
    const unsigned short *remap = t->data + n, *source = remap + n;
    int seen[256] = {0}, used = 0;
    for (int i = 0; i < n; i++) {
        assert(remap[i] < t->used && source[remap[i]] == input[i]);
        if (!seen[input[i]]) {
            assert(source[used++] == input[i]);
            seen[input[i]] = 1;
        }
    }
    assert(used == t->used);
}

int main(void)
{
    P4TopologyCache cache = {0};
    unsigned short inverse[65535];
    unsigned short input[] = {3,1,3,2,1,0}, other[] = {3,1,3,2,1,0};
    const void *mesh = (const void *)(uintptr_t)0x1000;
    fail_calloc = 1;
    assert(!p4_topology_get(&cache,mesh,input,6,4,1,1024,inverse));
    fail_calloc = 0; fail_malloc = 1;
    assert(!p4_topology_get(&cache,mesh,input,6,4,1,1024,inverse));
    fail_malloc = 0;
    P4Topology *t = p4_topology_get(&cache,mesh,input,6,4,1,1024,inverse);
    verify(t,input,6);
    unsigned builds = cache.builds;
    assert(p4_topology_get(&cache,mesh,input,6,4,2,1024,inverse) == t);
    assert(cache.builds == builds && cache.hits == 1);
    input[3] = 0; /* in-place edit at an interior index */
    t = p4_topology_get(&cache,mesh,input,6,4,3,1024,inverse);
    verify(t,input,6); assert(cache.builds == builds + 1);
    t = p4_topology_get(&cache,mesh,other,6,4,4,1024,inverse);
    verify(t,other,6); /* pointer recycling/new source with the same mesh */
    other[0] = 4;
    assert(!p4_topology_get(&cache,mesh,other,6,4,5,1024,inverse));
    assert(!p4_topology_get(&cache,mesh,input,0,4,5,1024,inverse));
    assert(!p4_topology_get(&cache,mesh,input,6,0,5,1024,inverse));
    assert(!p4_topology_get(&cache,mesh,input,6,65536,5,1024,inverse));
    assert(!p4_topology_get(&cache,mesh,input,6,4,5,1,inverse));
    p4_topology_clear(&cache); assert(allocations == 0);

    t = p4_topology_get(&cache,mesh,input,6,4,1,40,inverse);
    assert(t && cache.bytes == 32);
    unsigned short frame_copy[6]; memcpy(frame_copy,t->data+6,sizeof frame_copy);
    const void *mesh2 = (const void *)(uintptr_t)0x2000;
    assert(!p4_topology_get(&cache,mesh2,input,6,4,1,40,inverse));
    assert(cache.bytes == 32); /* do not thrash entries used in the same frame */
    assert(p4_topology_get(&cache,mesh2,input,6,4,2,40,inverse));
    assert(cache.bytes <= 40 && cache.evictions == 1);
    assert(frame_copy[0] == 0 && frame_copy[1] == 1); /* GPU's copy outlives eviction */
    p4_topology_clear(&cache); assert(allocations == 0);

    unsigned seed = 19;
    unsigned short random_indices[512];
    for (unsigned frame = 1; frame <= 10000; frame++) {
        seed = seed * 1664525u + 1013904223u;
        int vertices = 1 + (seed >> 24), n = 1 + (seed >> 12 & 511);
        for (int i = 0; i < n; i++) {
            seed = seed * 1664525u + 1013904223u;
            random_indices[i] = (unsigned short)((seed >> 8) % vertices);
        }
        const void *key = (const void *)(uintptr_t)(0x1000 + (frame % 79) * 16);
        t = p4_topology_get(&cache,key,random_indices,n,vertices,frame,4096,inverse);
        if (t) verify(t,random_indices,n);
        assert(cache.bytes <= 4096);
    }
    p4_topology_clear(&cache); assert(allocations == 0);
    puts("topology: 10000 randomized meshes, mutation, lifetime, budget and failure checks passed");
    return 0;
}
