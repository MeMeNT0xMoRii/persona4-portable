/*
 * RESONANCE01 QA07: independent C11 fusion-selection SESSION contract tests.
 * Test-only data; no game/mod code, no PSP MIPS, no CLPS2C/GPL source.
 * Contract derived from original Vanilla PS2 fclFuseRollPersona (max 6*8
 * parent skills, unique pool, original tier count, eligibility weights)
 * and fclCombineFuseSourceRecords (8 skill slots, inherited slot bitmask).
 * Real skill weights, table values, fusion menu hooks and save format are NOT
 * modeled. This must NEVER be linked directly into the PSP EBOOT.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define MAX_PARENTS 6
#define MAX_SLOTS 8
#define MAX_CANDIDATES 48
#define CHECK(expr) do { if(!(expr)){fprintf(stderr,"FAIL:%d: %s\n",__LINE__,#expr);exit(1);} }while(0)
typedef struct { uint16_t skill[MAX_SLOTS]; } Skills;
typedef struct {
    Skills before;
    uint16_t candidates[MAX_CANDIDATES], eligible[MAX_CANDIDATES];
    uint16_t picks[MAX_SLOTS];
    unsigned count, legal, needed, chosen;
    unsigned parent_generation;
    int mode;
    bool active, committed;
} FusionSession;
static int cases;

static bool has(const uint16_t *ids, unsigned n, uint16_t skill) {
    for (unsigned i=0;i<n;i++) if(ids[i]==skill) return true;
    return false;
}
static unsigned nskills(const Skills *s) {
    unsigned n=0;
    for(unsigned i=0;i<MAX_SLOTS;i++) if(s->skill[i])n++;
    return n;
}
/* TEST-ONLY eligibility predicate, not real P4 weights. */
static bool legal_skill(uint16_t id) { return id && id % 7 != 0; }
static bool prepare(FusionSession *s, const Skills *result,
                    const Skills *parents, unsigned parent_count,
                    unsigned source_generation, unsigned original_picks) {
    if(!s || !result || !parents || parent_count<2 || parent_count>MAX_PARENTS) return false;
    memset(s,0,sizeof(*s));
    s->before=*result; s->mode=(int)parent_count;
    s->parent_generation=source_generation;
    for(unsigned p=0;p<parent_count;p++)for(unsigned j=0;j<MAX_SLOTS;j++){
        uint16_t id=parents[p].skill[j];
        if(!id || has(result->skill,MAX_SLOTS,id) || has(s->candidates,s->count,id))continue;
        if(s->count==MAX_CANDIDATES)return false;
        s->candidates[s->count++]=id;
    }
    for(unsigned i=0;i<s->count;i++)if(legal_skill(s->candidates[i]))
        s->eligible[s->legal++]=s->candidates[i];
    unsigned free_slots=MAX_SLOTS-nskills(result);
    s->needed=original_picks;
    if(s->needed>free_slots)s->needed=free_slots;
    if(s->needed>s->legal)s->needed=s->legal;
    s->active=true;
    return true;
}
static bool select_skill(FusionSession *s,uint16_t id) {
    if(!s || !s->active || s->committed || s->chosen>=s->needed)return false;
    if(!has(s->eligible,s->legal,id) || has(s->picks,s->chosen,id))return false;
    s->picks[s->chosen++]=id;return true;
}
/* Never writes result until all selections have passed all checks. */
static bool commit(FusionSession *s,Skills *result,unsigned actual_generation,
                   uint8_t *inherited_mask) {
    if(!s || !result || !inherited_mask || !s->active || s->committed ||
       s->chosen!=s->needed || actual_generation!=s->parent_generation ||
       memcmp(result,&s->before,sizeof(*result))!=0)return false;
    Skills candidate=s->before;
    uint8_t mask=0;
    for(unsigned i=0;i<s->chosen;i++){
        unsigned slot=0;
        while(slot<MAX_SLOTS && candidate.skill[slot])slot++;
        if(slot==MAX_SLOTS)return false;
        candidate.skill[slot]=s->picks[i];
        mask=(uint8_t)(mask | (uint8_t)(1u<<slot));
    }
    *result=candidate; *inherited_mask=mask; s->committed=true;s->active=false;
    return true;
}
static void cancel(FusionSession *s){if(s)s->active=false;}
#define TEST(name,...) do {__VA_ARGS__;cases++;printf("PASS %02d %s\n",cases,name);}while(0)

int main(void) {
    const Skills base={{100,0,0,0,0,0,0,0}};
    const Skills parents[2]={{{100,101,102,7,0,0,0,0}},{{101,103,14,0,0,0,0,0}}};
    FusionSession s;Skills r;uint8_t mask=0;
    TEST("two parents dedup and existing excluded",{
        CHECK(prepare(&s,&base,parents,2,100,3));
        CHECK(s.count==5 && s.legal==3 && s.needed==3);
    });
    TEST("illegal weight=zero not selectable",{
        CHECK(!select_skill(&s,7));CHECK(!select_skill(&s,14));
    });
    TEST("duplicate selected skill rejected",{
        CHECK(select_skill(&s,101));CHECK(!select_skill(&s,101));CHECK(s.chosen==1);
    });
    TEST("no early result mutation",{
        r=base;CHECK(!commit(&s,&r,100,&mask));
        CHECK(memcmp(&r,&base,sizeof r)==0);
    });
    TEST("valid three picks commit exactly once",{
        CHECK(select_skill(&s,102));CHECK(select_skill(&s,103));
        CHECK(commit(&s,&r,100,&mask));
        CHECK(mask==0x0E && r.skill[0]==100 && r.skill[1]==101 &&
              r.skill[2]==102 && r.skill[3]==103);
        CHECK(!commit(&s,&r,100,&mask));
    });
    TEST("cancel with selection changes no result",{
        CHECK(prepare(&s,&base,parents,2,102,2));
        r=base;CHECK(select_skill(&s,101));cancel(&s);
        CHECK(!commit(&s,&r,102,&mask));CHECK(memcmp(&r,&base,sizeof r)==0);
    });
    TEST("parent change invalidates stale session",{
        CHECK(prepare(&s,&base,parents,2,200,1));
        r=base;CHECK(select_skill(&s,101));CHECK(!commit(&s,&r,201,&mask));
        CHECK(memcmp(&r,&base,sizeof r)==0);
    });
    TEST("unselected invalid skill and zero denied",{
        CHECK(prepare(&s,&base,parents,2,50,1));
        CHECK(!select_skill(&s,0));CHECK(!select_skill(&s,999));
    });
    TEST("original requested count limited to free slots",{
        Skills full={{1,2,3,4,5,6,7,0}};
        CHECK(prepare(&s,&full,parents,2,1,8));CHECK(s.needed<=1);
    });
    TEST("full skill result requests no extras",{
        Skills full={{1,2,3,4,5,6,7,8}};
        CHECK(prepare(&s,&full,parents,2,1,8));
        CHECK(s.needed==0);r=full;mask=255;CHECK(commit(&s,&r,1,&mask));
        CHECK(mask==0 && memcmp(&r,&full,sizeof r)==0);
    });
    TEST("zero legal choices leaves result intact",{
        const Skills illegal[2]={{{7,14,21,0,0,0,0,0}},{{28,35,42,0,0,0,0,0}}};
        CHECK(prepare(&s,&base,illegal,2,300,3));
        CHECK(s.legal==0 && s.needed==0);
        r=base;CHECK(commit(&s,&r,300,&mask));
        CHECK(mask==0 && memcmp(&r,&base,sizeof r)==0);
    });
    TEST("six parents can fill exactly 48-unique pool",{
        Skills many[6]={0};
        for(unsigned p=0;p<6;p++)for(unsigned i=0;i<8;i++)
            many[p].skill[i]=(uint16_t)(p*8+i+1);
        Skills empty={{0}};
        CHECK(prepare(&s,&empty,many,6,50,8));CHECK(s.count==48);
    });
    TEST("seven parents rejected before scanning",{
        Skills many[7]={0};
        CHECK(!prepare(&s,&base,many,7,12,1));
    });
    TEST("one parent rejected",{
        CHECK(!prepare(&s,&base,parents,1,12,1));
    });
    TEST("triangle and six-way share validated bounds",{
        Skills triangle[3]={parents[0],parents[1],{{200,201,0,0,0,0,0,0}}};
        CHECK(prepare(&s,&base,triangle,3,9,5));CHECK(s.mode==3 && s.needed<=5);
        Skills many[6]={0};
        CHECK(prepare(&s,&base,many,6,9,5));CHECK(s.mode==6);
    });
    TEST("maximum inherited bitmask fits 8 slots",{
        Skills empty={{0}}, eight[2]={{{1,2,3,4,0,0,0,0}},{{8,9,10,11,0,0,0,0}}};
        CHECK(prepare(&s,&empty,eight,2,100,8));
        CHECK(s.needed==8);r=empty;mask=0;
        for(unsigned i=0;i<8;i++)CHECK(select_skill(&s,s.eligible[i]));
        CHECK(commit(&s,&r,100,&mask));CHECK(mask==255&&nskills(&r)==8);
    });
    TEST("unexpected external result mutation blocks commit",{
        CHECK(prepare(&s,&base,parents,2,55,1));CHECK(select_skill(&s,101));
        r=base;r.skill[4]=900;CHECK(!commit(&s,&r,55,&mask));
        CHECK(r.skill[4]==900);
    });
    TEST("selection count never exceeds original budget",{
        CHECK(prepare(&s,&base,parents,2,55,1));CHECK(select_skill(&s,101));
        CHECK(!select_skill(&s,102));CHECK(s.chosen==1);
    });
    printf("ALL %d standalone fusion SESSION contract host tests passed. NOT a PSP integration.\n",cases);
    return 0;
}
