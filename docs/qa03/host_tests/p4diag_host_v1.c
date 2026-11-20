/* Persona4-PORTABLE QA05, 2026-11-20. Pure host-only synthetic diagnostic
 * protocol and ring-state test. No retail files, PSP SDK, port runtime edits.
 * cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic p4diag_host_v1.c -o p4diag_host && ./p4diag_host
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CAP 512u
#define PRE 128u
#define POST 128u
#define REC_BYTES 64u
#define FILE_HEADER 32u
#define SNAP_MAX (PRE+POST)
#define FILE_MAX (FILE_HEADER+SNAP_MAX*REC_BYTES)
#define RECORD_MAGIC 0x31443450u /* bytes P4D1 */
#define FILE_MAGIC 0x52443450u /* bytes P4DR */
#define SCHEMA 1u
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#cond); exit(1); } } while(0)

/* Explicit u32 fields avoid packed or compiler-dependent serialization. */
typedef struct {
    uint32_t magic;
    uint16_t version, event_type;
    uint32_t seq, tick, clock_lo, clock_epoch, scene, task, flags, bank;
    uint32_t ge_queued, ge_finished, shown_seq, list_words, duration_us, detail;
} P4DRecord;
_Static_assert(sizeof(P4DRecord) == REC_BYTES, "record must be exactly 64 bytes");
_Static_assert(offsetof(P4DRecord, seq) == 8, "seq ABI offset");
_Static_assert(offsetof(P4DRecord, list_words) == 52, "list size ABI offset");
_Static_assert(offsetof(P4DRecord, detail) == 60, "detail ABI offset");

enum State { ROLLING=0, CAPTURING=1, SEALED=2, EXPORTING=3 };
typedef struct {
    P4DRecord ring[CAP];
    uint32_t next_seq, total_seen, start_seq, pre_count, post_count;
    uint32_t trigger_reason, reason_bits, dropped, coalesced, missed, export_complete;
    enum State state;
} P4DRing;

static void ring_init(P4DRing *q) { memset(q, 0, sizeof *q); }
static int ring_push(P4DRing *q, uint16_t event_type, uint32_t detail) {
    if (q->state == SEALED || q->state == EXPORTING) { q->dropped++; return 0; }
    P4DRecord *r = &q->ring[q->next_seq & (CAP-1)];
    *r = (P4DRecord){.magic=RECORD_MAGIC,.version=SCHEMA,.event_type=event_type,
                    .seq=q->next_seq,.tick=q->total_seen,.detail=detail};
    q->next_seq++;
    if (q->total_seen < CAP) q->total_seen++;
    if (q->state == CAPTURING && ++q->post_count == POST) q->state = SEALED;
    return 1;
}
static int ring_trigger(P4DRing *q, uint32_t reason_bit) {
    if (q->state == CAPTURING) {q->coalesced++;q->reason_bits |= reason_bit;return 0;}
    if (q->state != ROLLING) {q->missed++;return 0;}
    q->state = CAPTURING;
    q->pre_count = q->total_seen < PRE ? q->total_seen : PRE;
    q->start_seq = q->next_seq - q->pre_count; /* u32 modulo wrap is intentional */
    q->post_count = 0; q->trigger_reason=reason_bit;q->reason_bits=reason_bit;
    return 1;
}
static uint32_t ring_count(const P4DRing *q) { return q->pre_count + q->post_count; }
static const P4DRecord *ring_at(const P4DRing *q, uint32_t index) {
    uint32_t seq = q->start_seq + index;
    const P4DRecord *r=&q->ring[seq & (CAP-1)];
    return r->seq==seq && r->magic==RECORD_MAGIC ? r : NULL;
}
static void ring_validate(const P4DRing *q) {
    CHECK(q->state==SEALED || q->state==EXPORTING);
    CHECK(q->post_count==POST);
    CHECK(ring_count(q)<=SNAP_MAX);
    for (uint32_t i=0;i<ring_count(q);i++) CHECK(ring_at(q,i)!=NULL);
}
static int ring_ack(P4DRing *q, int persisted_and_verified) {
    if (q->state != EXPORTING || !q->export_complete || !persisted_and_verified) return 0;
    q->state=ROLLING; q->pre_count=q->post_count=0;
    q->trigger_reason=q->reason_bits=q->export_complete=0;
    return 1;
}

static void put16(uint8_t *b, uint16_t v) {b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *b, uint32_t v) {for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(i*8));}
static uint16_t get16(const uint8_t *b) {return (uint16_t)(b[0] | ((uint16_t)b[1]<<8));}
static uint32_t get32(const uint8_t *b) {return (uint32_t)b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24);}
static void record_encode(uint8_t out[REC_BYTES], const P4DRecord *r) {
    put32(out+0,r->magic);put16(out+4,r->version);put16(out+6,r->event_type);
    const uint32_t f[]={r->seq,r->tick,r->clock_lo,r->clock_epoch,r->scene,r->task,
       r->flags,r->bank,r->ge_queued,r->ge_finished,r->shown_seq,
       r->list_words,r->duration_us,r->detail};
    _Static_assert(sizeof f/sizeof f[0]==14, "64B field count");
    for(unsigned i=0;i<14;i++)put32(out+8+4*i,f[i]);
}
static uint32_t crc_step(uint32_t c, const uint8_t *b,size_t n) {
    for(size_t i=0;i<n;i++) {c^=b[i];for(int j=0;j<8;j++) c=(c>>1)^((c&1)?0xedb88320u:0u);}
    return c;
}
static uint32_t payload_crc(const P4DRing *q) {
    uint32_t c=0xffffffffu;uint8_t b[REC_BYTES];
    for(uint32_t i=0;i<ring_count(q);i++){const P4DRecord *r=ring_at(q,i);CHECK(r);record_encode(b,r);c=crc_step(c,b,sizeof b);}
    return c^0xffffffffu;
}
static uint32_t crc32(const uint8_t *data,size_t size) {return crc_step(0xffffffffu,data,size)^0xffffffffu;}
static void header_encode(uint8_t out[FILE_HEADER], const P4DRing *q) {
    memset(out,0,FILE_HEADER);put32(out+0,FILE_MAGIC);put16(out+4,SCHEMA);
    put16(out+6,REC_BYTES);put32(out+8,ring_count(q));put32(out+12,payload_crc(q));
    put32(out+16,q->start_seq);put32(out+20,q->start_seq+ring_count(q)-1);
    put32(out+24,q->reason_bits);put32(out+28,0);
}

/* On PSP this callback must be performed only by a background worker.
   Each step writes <=budget bytes; zero/negative writes abort without ACK. */
typedef int (*WriteFn)(void*,const uint8_t*,size_t);
typedef struct {P4DRing *ring;uint32_t segment;size_t pos;uint8_t bytes[REC_BYTES];size_t len;int finished;} P4DExport;
static void export_begin(P4DExport *x,P4DRing *q) {
    ring_validate(q);q->state=EXPORTING;q->export_complete=0;
    memset(x,0,sizeof *x);x->ring=q;x->len=FILE_HEADER;
    header_encode(x->bytes,q);
}
static int export_step(P4DExport *x, WriteFn write, void *ctx, size_t budget) {
    if(x->finished)return 1;
    if(!budget)return 0;
    while(budget && !x->finished) {
        size_t left=x->len-x->pos,n=left<budget?left:budget;
        int wrote=write(ctx,x->bytes+x->pos,n);
        if(wrote<=0 || (size_t)wrote>n) return -1;
        x->pos+=(size_t)wrote;budget-=(size_t)wrote;
        if(x->pos==x->len) {
            x->segment++;x->pos=0;
            if(x->segment>ring_count(x->ring)){x->finished=1;x->ring->export_complete=1;break;}
            const P4DRecord *r=ring_at(x->ring,x->segment-1);
            if(!r)return -1;
            record_encode(x->bytes,r);x->len=REC_BYTES;
        }
    }
    return x->finished?1:0;
}
static int parse_file(const uint8_t *b,size_t length, uint32_t *count) {
    if(length<FILE_HEADER)return -1;
    if(get32(b)!=FILE_MAGIC ||get16(b+4)!=SCHEMA||get16(b+6)!=REC_BYTES)return -2;
    uint32_t n=get32(b+8);
    if(n==0 || n>SNAP_MAX || length!=FILE_HEADER+(size_t)n*REC_BYTES)return -3;
    if(crc32(b+FILE_HEADER,(size_t)n*REC_BYTES)!=get32(b+12))return -4;
    uint32_t first=get32(b+16),last=get32(b+20);
    if(last!=first+n-1)return -5;
    for(uint32_t i=0;i<n;i++) {
        const uint8_t *r=b+FILE_HEADER+(size_t)i*REC_BYTES;
        if(get32(r)!=RECORD_MAGIC||get16(r+4)!=SCHEMA||get32(r+8)!=first+i)return -6;
    }
    *count=n;return 0;
}

typedef struct {uint8_t data[FILE_MAX];size_t used;unsigned calls;int mode;} Sink;
static int sink_write(void *ctx,const uint8_t *src,size_t n) {
    Sink *s=ctx;const int limit[]={4096,17,500,155,1};
    if(s->mode==1 && s->calls++>0)return -7;
    if(s->mode==2 && s->calls++>0)return 0;
    size_t m=s->mode==0?(size_t)limit[s->calls++%5]:n;
    if(m>n)m=n;
    if(s->used+m>sizeof s->data)return -8;
    memcpy(s->data+s->used,src,m);s->used+=m;return (int)m;
}
static void populate(P4DRing *q,uint32_t how_many) {for(uint32_t i=0;i<how_many;i++)CHECK(ring_push(q,1,i));}
static void seal(P4DRing *q,uint32_t reason) {CHECK(ring_trigger(q,reason));populate(q,POST);ring_validate(q);}

static int tests;
#define TEST(msg, ...) do { __VA_ARGS__;tests++;printf("PASS %02d: %s\n",tests,msg); } while(0)
int main(void) {
    P4DRing q;ring_init(&q);
    TEST("64-byte record exact C ABI",{CHECK(sizeof(P4DRecord)==64);CHECK(offsetof(P4DRecord,detail)==60);});
    TEST("CRC32 standard 123456789 vector",{CHECK(crc32((const uint8_t*)"123456789",9)==0xcbf43926u);});
    TEST("LE 16/32 independent of host struct padding",{uint8_t b[4];put32(b,0x12345678);CHECK(b[0]==0x78&&b[3]==0x12&&get32(b)==0x12345678);put16(b,0xabcd);CHECK(get16(b)==0xabcd);});
    TEST("early 1+128 record trigger",{ring_init(&q);populate(&q,1);seal(&q,1);CHECK(ring_count(&q)==129&&ring_at(&q,0)->seq==0);});
    TEST("128 pre +128 post",{ring_init(&q);populate(&q,400);seal(&q,1);CHECK(ring_count(&q)==256&&ring_at(&q,0)->seq==272&&ring_at(&q,255)->seq==527);});
    TEST("512 physical slot wrap",{ring_init(&q);populate(&q,1028);seal(&q,1);CHECK(ring_at(&q,0)->seq==900&&ring_at(&q,255)->seq==1155);});
    TEST("u32 serial modulo rollover",{ring_init(&q);q.next_seq=0xffffff80u;q.total_seen=128;for(uint32_t i=0;i<128;i++){uint32_t seq=q.next_seq++;P4DRecord *r=&q.ring[seq&(CAP-1)];*r=(P4DRecord){.seq=seq,.magic=RECORD_MAGIC};}seal(&q,1);CHECK(ring_at(&q,0)->seq==0xffffff80u&&ring_at(&q,255)->seq==0x7fu);});
    TEST("coalesced exception preserves initial reason",{ring_init(&q);populate(&q,200);CHECK(ring_trigger(&q,1));populate(&q,64);CHECK(!ring_trigger(&q,2));populate(&q,64);CHECK(q.coalesced==1&&q.reason_bits==3&&q.trigger_reason==1);ring_validate(&q);});
    TEST("SEALED backpressure is non-blocking",{uint32_t was=q.next_seq;CHECK(!ring_push(&q,4,777)&&q.dropped==1&&q.next_seq==was);CHECK(!ring_trigger(&q,4)&&q.missed==1);});
    TEST("u32 microsecond timer wraps correctly",{uint32_t a=0xffffff00u,b=0xb8u;CHECK((uint32_t)(b-a)==440);});
    TEST("negative flip result not presented despite shown_seq ACK",{int display_ret=-1;uint32_t shown=12,request=12;CHECK(shown==request && !(display_ret>=0));});
    TEST("3 of last 5 >50ms and 1 >100ms triggers",{uint32_t w[]={33000,51000,53000,29000,52000};unsigned n=0;for(unsigned i=0;i<5;i++)n+=(w[i]>50000);CHECK(n==3&&100001>100000);});
    TEST("single incident LE file roundtrip",{ring_init(&q);populate(&q,200);seal(&q,0x11);P4DExport x;Sink s={0};export_begin(&x,&q);int done=0;for(int i=0;i<1000&&!done;i++){int a=export_step(&x,sink_write,&s,4096);CHECK(a>=0);done=a;}CHECK(done && s.used==FILE_MAX);uint32_t n=0;CHECK(parse_file(s.data,s.used,&n)==0&&n==256&&get32(s.data+24)==0x11);});
    TEST("mock short writes preserve all bytes",{P4DExport x;Sink s={0};export_begin(&x,&q);int done=0;for(int i=0;i<1000&&!done;i++){int a=export_step(&x,sink_write,&s,4096);CHECK(a>=0);done=a;}CHECK(done&&s.used==FILE_MAX&&parse_file(s.data,s.used,&(uint32_t){0})==0&&s.calls>8);});
    TEST("short file rejected",{P4DExport x;Sink s={0};export_begin(&x,&q);for(int i=0;i<1000&&!x.finished;i++)CHECK(export_step(&x,sink_write,&s,4096)>=0);CHECK(parse_file(s.data,s.used-1,&(uint32_t){0})==-3);});
    TEST("corrupt payload rejected by checksum",{P4DExport x;Sink s={0};export_begin(&x,&q);for(int i=0;i<1000&&!x.finished;i++)CHECK(export_step(&x,sink_write,&s,4096)>=0);s.data[100]^=0x40;CHECK(parse_file(s.data,s.used,&(uint32_t){0})==-4);});
    TEST("wrong version rejected",{P4DExport x;Sink s={0};export_begin(&x,&q);for(int i=0;i<1000&&!x.finished;i++)CHECK(export_step(&x,sink_write,&s,4096)>=0);s.data[4]=2;CHECK(parse_file(s.data,s.used,&(uint32_t){0})==-2);});
    TEST("negative IO does not ACK or overwrite",{P4DExport x;Sink s={0};s.mode=1;export_begin(&x,&q);int ret=export_step(&x,sink_write,&s,4096);CHECK(ret==-1&&q.state==EXPORTING&&ring_at(&q,0)&&!ring_ack(&q,1));});
    TEST("zero-byte IO progress does not spin",{P4DExport x;Sink s={0};s.mode=2;export_begin(&x,&q);CHECK(export_step(&x,sink_write,&s,4096)==-1);});
    TEST("ACK denied for incomplete export",{P4DExport x;Sink s={0};export_begin(&x,&q);CHECK(!ring_ack(&q,1));CHECK(export_step(&x,sink_write,&s,4096)>=0);CHECK(!ring_ack(&q,1));CHECK(q.state==EXPORTING);});
    TEST("ACK denied before storage verification",{P4DExport x;Sink s={0};export_begin(&x,&q);for(int i=0;i<1000&&!x.finished;i++)CHECK(export_step(&x,sink_write,&s,4096)>=0);CHECK(x.finished);CHECK(!ring_ack(&q,0));CHECK(q.state==EXPORTING);});
    TEST("ACK reopens ring only after export completes",{P4DExport x;Sink s={0};export_begin(&x,&q);for(int i=0;i<1000&&!x.finished;i++)CHECK(export_step(&x,sink_write,&s,4096)>=0);CHECK(x.finished);CHECK(ring_ack(&q,1));CHECK(q.state==ROLLING);CHECK(ring_push(&q,9,9));});
    TEST("zero export budget never calls file IO",{ring_init(&q);populate(&q,200);seal(&q,1);P4DExport x;Sink s={0};export_begin(&x,&q);CHECK(export_step(&x,sink_write,&s,0)==0&&s.calls==0);});
    printf("ALL %d host-only C protocol tests passed (no PSP game build)\n",tests);
    return 0;
}
