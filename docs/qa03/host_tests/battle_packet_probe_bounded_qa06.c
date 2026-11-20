/* Persona4-PORTABLE QA06 (2026-11-20): host-only validation of the
 * original perf06 256-frame BtlPacket diagnostic traversal risk.
 * Source proof: port/rwsky.c L1800..1828 @29b0fcf and original
 * src/Battle/btlPacket.c BtlPacket::next (PS2 packet offset 0x7c).
 * This mock does NOT read actual PSP RAM, compile a PSP patch, or prove a
 * damaged battle exists in a user's save.
 *
 * cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic battle_packet_probe_bounded_qa06.c -o pktprobe && ./pktprobe
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSP_RAM_BASE 0x08800000u
#define PSP_RAM_SPAN 0x03800000u
#define PACKET_NEXT_END 0x80u /* reads packet.next at +0x7c as u32 */
#define MAX_DIAG_NODES 64u
#define NODE_CAP 72u
#define CHECK(expr) do { if (!(expr)) {fprintf(stderr, "FAIL %u: %s\n", (unsigned)__LINE__, #expr);exit(1);} } while(0)

typedef struct { uint32_t addr, next; } MockPacket;
typedef struct { MockPacket nodes[NODE_CAP];unsigned n; } MockMap;
typedef enum {WALK_OK, WALK_CYCLE, WALK_TOO_LONG, WALK_BAD_RANGE, WALK_UNMAPPED} Result;

static int original_probe_ok(uint32_t p) {
    return !(p&3u) && (uint32_t)(p-PSP_RAM_BASE) < PSP_RAM_SPAN;
}
static int safe_span(uint32_t p,uint32_t bytes) {
    if (!p || (p&3u) || bytes>PSP_RAM_SPAN || p<PSP_RAM_BASE) return 0;
    return (p-PSP_RAM_BASE)<=PSP_RAM_SPAN-bytes;
}
static void add(MockMap *m,uint32_t p,uint32_t next) {
    CHECK(m->n < NODE_CAP);
    m->nodes[m->n++] = (MockPacket){p,next};
}
static int mock_next(const MockMap *m,uint32_t p,uint32_t *next) {
    for (unsigned i=0;i<m->n;i++) if(m->nodes[i].addr==p) { *next=m->nodes[i].next;return 1; }
    return 0;
}
/* Mocked graph version of the baseline loop. A cycle returns "not
 * terminated in 100 iterations", never actually hangs the host test. */
static int original_bounded_observation(const MockMap *m,uint32_t p,unsigned limit) {
    for (unsigned i=0;p && i<limit;i++) {
        uint32_t next;
        if (!original_probe_ok(p) || !mock_next(m,p,&next)) return 0;
        p=next;
    }
    return p!=0;
}
/* Proposed DEBUG-ONLY strategy: validate the full span before a member
 * dereference, stop cycles and cap log work; do not mutate the list.
 * Exact allocation ownership and sentinel semantics require PSP review. */
static Result walk(const MockMap *m,uint32_t head,unsigned *visited_count) {
    uint32_t seen[MAX_DIAG_NODES];
    unsigned n=0;uint32_t p=head;
    while(p) {
        if (!safe_span(p,PACKET_NEXT_END)) { *visited_count=n;return WALK_BAD_RANGE; }
        for(unsigned i=0;i<n;i++) if(seen[i]==p) {*visited_count=n;return WALK_CYCLE;}
        if(n==MAX_DIAG_NODES) {*visited_count=n;return WALK_TOO_LONG;}
        uint32_t next;
        if(!mock_next(m,p,&next)) {*visited_count=n;return WALK_UNMAPPED;}
        seen[n++]=p;p=next;
    }
    *visited_count=n;return WALK_OK;
}
#define TEST(title, ...) do {__VA_ARGS__;++tests;printf("PASS %02u %s\n",tests,title);} while(0)

int main(void) {
    const uint32_t A=PSP_RAM_BASE+0x1000u,B=A+0x100u,C=B+0x100u;
    MockMap m;unsigned tests=0,n=0;
    TEST("empty packet list harmless",{
        memset(&m,0,sizeof m);CHECK(walk(&m,0,&n)==WALK_OK&&n==0);
    });
    TEST("one null-terminated packet",{
        memset(&m,0,sizeof m);add(&m,A,0);CHECK(walk(&m,A,&n)==WALK_OK&&n==1);
    });
    TEST("three packet chain",{
        memset(&m,0,sizeof m);add(&m,A,B);add(&m,B,C);add(&m,C,0);
        CHECK(walk(&m,A,&n)==WALK_OK&&n==3);
        CHECK(!original_bounded_observation(&m,A,100));
    });
    TEST("original per-pointer RAM check accepts self-cycle forever",{
        memset(&m,0,sizeof m);add(&m,A,A);
        CHECK(original_bounded_observation(&m,A,100));
        CHECK(walk(&m,A,&n)==WALK_CYCLE&&n==1);
    });
    TEST("two-node cycle detected without logging forever",{
        memset(&m,0,sizeof m);add(&m,A,B);add(&m,B,A);
        CHECK(original_bounded_observation(&m,A,100));
        CHECK(walk(&m,A,&n)==WALK_CYCLE&&n==2);
    });
    TEST("three-node tail entering two-node cycle",{
        memset(&m,0,sizeof m);add(&m,A,B);add(&m,B,C);add(&m,C,B);
        CHECK(original_bounded_observation(&m,A,100));
        CHECK(walk(&m,A,&n)==WALK_CYCLE&&n==3);
    });
    TEST("last aligned RAM word passes old check but not +0x7c read",{
        uint32_t last=PSP_RAM_BASE+PSP_RAM_SPAN-4u;
        memset(&m,0,sizeof m);add(&m,last,0);
        CHECK(original_probe_ok(last));
        CHECK(!safe_span(last,PACKET_NEXT_END));
        CHECK(walk(&m,last,&n)==WALK_BAD_RANGE&&n==0);
    });
    TEST("packet at exact last in-bounds offset accepted",{
        uint32_t last=PSP_RAM_BASE+PSP_RAM_SPAN-PACKET_NEXT_END;
        memset(&m,0,sizeof m);add(&m,last,0);
        CHECK(safe_span(last,PACKET_NEXT_END));
        CHECK(walk(&m,last,&n)==WALK_OK&&n==1);
    });
    TEST("unaligned pointer fails before dereference",{
        memset(&m,0,sizeof m);CHECK(walk(&m,A+1,&n)==WALK_BAD_RANGE);
    });
    TEST("outside range fails before dereference",{
        memset(&m,0,sizeof m);CHECK(walk(&m,PSP_RAM_BASE+PSP_RAM_SPAN,&n)==WALK_BAD_RANGE);
    });
    TEST("valid-looking RAM address may not be owned allocated packet",{
        memset(&m,0,sizeof m);CHECK(original_probe_ok(A));
        CHECK(walk(&m,A,&n)==WALK_UNMAPPED&&n==0);
    });
    TEST("bounded 64 nodes stops on long list",{
        memset(&m,0,sizeof m);
        for(unsigned i=0;i<65;i++)
            add(&m,PSP_RAM_BASE+0x2000u+i*0x100u,i==64?0:PSP_RAM_BASE+0x2000u+(i+1)*0x100u);
        CHECK(walk(&m,PSP_RAM_BASE+0x2000u,&n)==WALK_TOO_LONG&&n==64);
        CHECK(!original_bounded_observation(&m,PSP_RAM_BASE+0x2000u,100));
    });
    printf("ALL %u host-only synthetic packet-probe checks passed (NO PSP/build).\n",tests);
    return 0;
}
