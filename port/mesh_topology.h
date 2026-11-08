/* CPU-only topology cache. GPU submissions copy the remap into their frame's
   index pool, so eviction never frees memory that an outstanding GE list uses.
   The full original index array is retained and compared on EVERY lookup;
   pointer reuse and in-place edits cannot reuse an obsolete remap. */
#ifndef P4_MESH_TOPOLOGY_H
#define P4_MESH_TOPOLOGY_H
#include <stdlib.h>
#include <string.h>

#ifndef P4_TOPO_MALLOC
#define P4_TOPO_MALLOC malloc
#define P4_TOPO_CALLOC calloc
#define P4_TOPO_FREE free
#endif
enum { P4_TOPO_SLOTS = 256, P4_TOPO_PROBES = 8 };
typedef struct {
    const void *mesh;
    const unsigned short *input;
    int count, vertices, used;
    unsigned frame, bytes;
    unsigned short *data;
} P4Topology;
typedef struct {
    P4Topology *entries;
    unsigned bytes, hits, builds, misses, evictions;
} P4TopologyCache;

static void p4_topology_drop(P4TopologyCache *cache, P4Topology *entry)
{
    if (entry->data) {
        cache->bytes -= entry->bytes;
        P4_TOPO_FREE(entry->data);
        cache->evictions++;
    }
    memset(entry, 0, sizeof *entry);
}

static P4Topology *p4_topology_get(P4TopologyCache *cache, const void *mesh,
        const unsigned short *input, int count, int vertices, unsigned frame,
        unsigned budget, unsigned short *inverse)
{
    if (!mesh || !input || !inverse || count <= 0 || vertices <= 0 || vertices > 65535)
        return 0;
    /* Bound arithmetic before sizing; a larger entry uses the original path. */
    unsigned capacity = count < vertices ? (unsigned)count : (unsigned)vertices;
    if ((unsigned)count > budget / 4 || capacity > (budget - (unsigned)count * 4) / 2) {
        cache->misses++;
        return 0;
    }
    unsigned bytes = (unsigned)count * 4 + capacity * 2;
    if (!cache->entries && !(cache->entries = P4_TOPO_CALLOC(P4_TOPO_SLOTS, sizeof *cache->entries))) {
        cache->misses++;
        return 0;
    }
    unsigned bucket = (unsigned)(((size_t)mesh >> 4) * 2654435761u) >> 24;
    P4Topology *same = 0, *spare = 0, *oldest = 0;
    for (unsigned i = 0; i < P4_TOPO_PROBES; i++) {
        P4Topology *e = &cache->entries[(bucket + i) & (P4_TOPO_SLOTS - 1)];
        if (e->data && e->mesh == mesh) {
            if (e->input == input && e->count == count && e->vertices == vertices &&
                !memcmp(e->data, input, (unsigned)count * 2)) {
                e->frame = frame;
                cache->hits++;
                return e;
            }
            same = e;
        }
        if (!e->data && !spare)
            spare = e;
        if (e->data && e->frame != frame && (!oldest || frame - e->frame > frame - oldest->frame))
            oldest = e;
    }
    P4Topology *entry = same ? same : spare ? spare : oldest;
    if (!entry) {
        cache->misses++;
        return 0;
    }
    for (int i = 0; i < count; i++)
        if (input[i] >= vertices) {
            cache->misses++;
            return 0;
        }
    p4_topology_drop(cache, entry);
    while (cache->bytes + bytes > budget) {
        P4Topology *victim = 0;
        for (unsigned i = 0; i < P4_TOPO_SLOTS; i++) {
            P4Topology *e = &cache->entries[i];
            if (e->data && e->frame != frame && (!victim || frame - e->frame > frame - victim->frame))
                victim = e;
        }
        if (!victim) {
            cache->misses++;
            return 0;
        }
        p4_topology_drop(cache, victim);
    }
    unsigned short *data = P4_TOPO_MALLOC(bytes);
    if (!data) {
        cache->misses++;
        return 0;
    }
    unsigned short *remap = data + count, *source = remap + count;
    memcpy(data, input, (unsigned)count * 2);
    memset(inverse, 0xff, (unsigned)vertices * 2);
    int used = 0;
    for (int i = 0; i < count; i++) {
        unsigned j = input[i];
        if (inverse[j] == 0xffff) {
            inverse[j] = (unsigned short)used;
            source[used++] = (unsigned short)j;
        }
        remap[i] = inverse[j];
    }
    entry->mesh = mesh, entry->input = input;
    entry->count = count, entry->vertices = vertices, entry->used = used;
    entry->frame = frame, entry->bytes = bytes, entry->data = data;
    cache->bytes += bytes;
    cache->builds++;
    return entry;
}

static void p4_topology_clear(P4TopologyCache *cache)
{
    if (cache->entries) {
        for (unsigned i = 0; i < P4_TOPO_SLOTS; i++)
            if (cache->entries[i].data)
                P4_TOPO_FREE(cache->entries[i].data);
        P4_TOPO_FREE(cache->entries);
    }
    memset(cache, 0, sizeof *cache);
}
#endif
