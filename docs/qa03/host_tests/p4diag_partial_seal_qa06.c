/* P4P QA06 20 Nov 2026: synthetic host-only regression design for D1.
 * The historical p4diag_host_v1.c 23/23 must be run separately.
 * This is NOT a patched PSP runtime nor actual game-clock/storage testing.
 * gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic p4diag_partial_seal_qa06.c -o p4diag_partial && ./p4diag_partial
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(p) do { if (!(p)) {fprintf(stderr,"FAIL:%d: %s\n",__LINE__,#p);exit(1);} } while(0)
#define CAP 512u
#define PRE 128u
#define POST 128u
#define PARTIAL_STALL 0xf01u
#define TIMEOUT_US 250000u /* synthetic threshold: NOT calibrated on PSP */
typedef struct {uint32_t seq, event, detail, stamp;uint8_t reserved[48];} Record;
_Static_assert(sizeof(Record)==64,"record size");
typedef enum {ROLLING=0,CAPTURING=1,SEALED=2,EXPORTING=3} State;
typedef struct {
    Record ring[CAP];uint32_t next_seq,seen,start,pre,post,reasons,dropped,missed;
    uint32_t last_game_heartbeat;State state;int exported,verified;
} Recorder;
static void init(Recorder *r){memset(r,0,sizeof *r);}
static uint32_t count(const Recorder *r){return r->pre+r->post;}
static const Record *lookup(const Recorder *r,uint32_t i){
    uint32_t seq=r->start+i;
    const Record *e=&r->ring[seq&(CAP-1)];
    return e->seq==seq?e:NULL;
}
static int push(Recorder *r,uint32_t event,uint32_t detail){
    if(r->state==SEALED||r->state==EXPORTING){r->dropped++;return 0;}
    Record *e=&r->ring[r->next_seq&(CAP-1)];*e=(Record){.seq=r->next_seq,.event=event,.detail=detail};
    r->next_seq++;if(r->seen<CAP)r->seen++;
    if(r->state==CAPTURING&&++r->post==POST)r->state=SEALED;
    return 1;
}
static void populate(Recorder *r,uint32_t n){for(uint32_t i=0;i<n;i++)CHECK(push(r,2u,i));}
static int trigger(Recorder *r,uint32_t reason){
    if(r->state==CAPTURING){r->reasons|=reason;return 0;}
    if(r->state!=ROLLING){r->missed++;return 0;}
    r->state=CAPTURING;r->pre=r->seen<PRE?r->seen:PRE;
    r->start=r->next_seq-r->pre;r->post=0;r->reasons=reason;return 1;
}
/* New proposal: a separately running worker can seal 0..127 post events.
 * It inserts an explicit record if the incident otherwise contains none. */
static int partial_seal(Recorder *r){
    if(r->state!=CAPTURING)return 0;
    if(!count(r))CHECK(push(r,PARTIAL_STALL,r->reasons));
    CHECK(count(r)>=1&&count(r)<=PRE+POST);
    for(uint32_t i=0;i<count(r);i++)CHECK(lookup(r,i)!=NULL);
    r->state=SEALED;return 1;
}
static int worker_poll(Recorder *r,uint32_t now){
    if(r->state!=CAPTURING)return 0;
    if((uint32_t)(now-r->last_game_heartbeat)<TIMEOUT_US)return 0;
    return partial_seal(r);
}
/* This only models ACK preconditions; real PSP file writes, rename, sync,
 * reopen/readback and crash recovery MUST be independently tested later. */
static int ack(Recorder *r){
    if(r->state!=EXPORTING||!r->exported||!r->verified)return 0;
    r->state=ROLLING;r->pre=r->post=r->reasons=0;r->exported=r->verified=0;return 1;
}
#define TEST(name, ...) do { __VA_ARGS__;tests++;printf("PASS %02u %s\n",tests,name); } while(0)
int main(void){
    unsigned tests=0;Recorder r;
    TEST("legacy 128 POST rule leaves immediate hang CAPTURING",{
        init(&r);populate(&r,190);CHECK(trigger(&r,1));CHECK(r.state==CAPTURING&&r.post==0);
    });
    TEST("worker can seal zero-POST partial after game heartbeat stall",{
        r.last_game_heartbeat=0;CHECK(!worker_poll(&r,TIMEOUT_US-1));
        CHECK(worker_poll(&r,TIMEOUT_US));CHECK(r.state==SEALED&&count(&r)==PRE);
    });
    TEST("seven-POST partial retains correct chronological IDs",{
        init(&r);populate(&r,256);CHECK(trigger(&r,2));populate(&r,7);
        CHECK(partial_seal(&r));CHECK(count(&r)==135);
        CHECK(lookup(&r,0)->seq==128&&lookup(&r,134)->seq==262);
    });
    TEST("full 128 POST automatically seals without worker",{
        init(&r);populate(&r,190);CHECK(trigger(&r,4));populate(&r,128);
        CHECK(r.state==SEALED&&r.post==POST&&count(&r)==256);
    });
    TEST("empty pre/post gets one typed partial-stall event",{
        init(&r);CHECK(trigger(&r,8));CHECK(count(&r)==0);CHECK(partial_seal(&r));
        CHECK(count(&r)==1&&lookup(&r,0)->event==PARTIAL_STALL);
    });
    TEST("512-slot wrap remains chronological across snapshot",{
        init(&r);populate(&r,1028);CHECK(trigger(&r,16));populate(&r,3);
        CHECK(partial_seal(&r));CHECK(lookup(&r,0)->seq==900);
        CHECK(lookup(&r,130)->seq==1030);
    });
    TEST("short monotonic u32 wrap uses unsigned age",{
        init(&r);populate(&r,120);CHECK(trigger(&r,1));r.last_game_heartbeat=0xffffff80u;
        CHECK(!worker_poll(&r,0x00000080u));
        CHECK(worker_poll(&r,0x00040000u));CHECK(r.state==SEALED);
    });
    TEST("heartbeats prevent accidental early sealing",{
        init(&r);populate(&r,120);CHECK(trigger(&r,1));
        r.last_game_heartbeat=640000u;CHECK(!worker_poll(&r,880000u));
        CHECK(r.state==CAPTURING);
    });
    TEST("trigger coalescing preserves reason and partial length",{
        init(&r);populate(&r,120);CHECK(trigger(&r,1));CHECK(!trigger(&r,4));
        populate(&r,7);CHECK(r.reasons==5);CHECK(partial_seal(&r));CHECK(count(&r)==127);
    });
    TEST("sealed backpressure and ACK gate do not fake persistence",{
        uint32_t seq=r.next_seq;CHECK(!push(&r,3,1));CHECK(r.dropped==1&&r.next_seq==seq);
        r.state=EXPORTING;CHECK(!ack(&r));r.exported=1;CHECK(!ack(&r));
        r.verified=1;CHECK(ack(&r));CHECK(push(&r,3,1));
    });
    printf("ALL %u QA06 synthetic partial-stall host checks passed (NO PSP run)\n",tests);
    return 0;
}
