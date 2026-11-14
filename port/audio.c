/* PSP sound service: SAS owns synthesis/mixing; Output2 plays one stereo bus.
 * One audio worker owns every SAS call. A separate loader fills VAG rings and
 * never holds a game-thread lock during I/O. Stream state handoffs use sync. */
#include <pspkernel.h>
#include <pspsascore.h>
#include <pspaudio.h>
#include <psputility.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include "audio.h"

#define DIR "ms0:/PSP/GAME/P4/SOUND/"
#define GRAIN 512
#define STREAMS 5
#define RING_BLOCKS 2048
#define CHUNK_BLOCKS 256
#define RING_BYTES (RING_BLOCKS * 16)
#define QUEUE 64
#define BARRIER() __asm__ volatile("sync" ::: "memory")
extern void p4_log(const char *fmt, ...);

enum { EMPTY, LOAD, PREFILL, READY, PLAY, CLOSE, FAILED };
enum { CMD_PATH=1, CMD_MEMORY, CMD_STOP, CMD_EFFECT, CMD_EFFECT_STOP, CMD_BANK };
typedef struct { unsigned key, signature, bytes, id, part, offset, signature2, selector; } Index;
typedef struct { unsigned char magic[4]; unsigned rate, channels, samples, loop, blocks, version, reserved; } Header;
typedef struct {
    volatile int state;
    unsigned id, pending_id, source_block, produced, consumed, source_samples, data_offset;
    volatile unsigned played;
    unsigned fraction, pitch, fade_total, fade_left, resume_block;
    Header header;
    int fd, pending;
    unsigned char ring[2][RING_BYTES] __attribute__((aligned(64)));
} Stream;
typedef struct { int op, channel, a, b, c; unsigned key; } Command;
typedef struct { unsigned char *hd, *bd, *sq; unsigned hsize, bsize, ssize; int owned; } Bank;
typedef struct {
    volatile int state; /* 0/3 idle/ready, 1 queued, 2 copying, 4 failed */
    int file, stage;
    unsigned position;
    const void *source[3];
    unsigned bytes[3];
    Bank next;
} BankRequest;
typedef struct { int active, bank, group, cue; unsigned offset, end, tick, next, scale, tempo; } Sequence;
typedef struct { int active, owner, bank, group, timbre; unsigned pitch, volume, pan; } Voice;

static SceSasCore sas __attribute__((aligned(64)));
static short output[2][GRAIN*2] __attribute__((aligned(64)));
static Stream streams[STREAMS];
static volatile int requested[STREAMS];
static unsigned char input[CHUNK_BLOCKS*16*2] __attribute__((aligned(64)));
static Index *index_rows;
static unsigned *signature_index;
static unsigned char *index_storage,*selectors;
static unsigned selector_bytes;
static unsigned index_count;
static Bank banks[6], default_banks[2];
static BankRequest bank_requests[6];
static volatile int banks_loaded;
static Sequence sequences[16];
static Voice voices[32];
static Command commands[QUEUE];
static volatile unsigned queue_in, queue_out;
static volatile int running, paused, pause_ack;
static int initialized, output_reserved;
static int codec_module, sas_module, suspended; /* module ownership: 1 ours, 2 borrowed */
static SceUID audio_thread=-1, loader_thread=-1;
/* Exposed to PSPLink. Times are cumulative microseconds of CPU execution. */
volatile unsigned p4_audio_cpu_us, p4_audio_loader_us, p4_audio_commands;
volatile unsigned p4_audio_underruns, p4_audio_errors, p4_audio_dropped;
volatile unsigned p4_audio_blocks, p4_audio_started, p4_audio_finished;
/* 1 SAS mix, 2 Output2, 3 pause commit, 4 output drain, 5 paused, 6 resume. */
volatile unsigned p4_audio_step;
volatile int p4_audio_last_error;

static unsigned word(const unsigned char *p) { unsigned v; memcpy(&v,p,4); return v; }
static unsigned half(const unsigned char *p) { return p[0] | (p[1]<<8); }
static unsigned long long runtime(void)
{
    SceKernelThreadRunStatus status;
    memset(&status,0,sizeof status); status.size=sizeof status;
    if (sceKernelReferThreadRunStatus(sceKernelGetThreadId(),&status)<0) return 0;
    /* ThreadMan runtime is a microsecond system clock, not CPU clock cycles. */
    return ((unsigned long long)status.runClocks.hi<<32) | status.runClocks.low;
}
unsigned p4_audio_hash(const char *path)
{
    unsigned h=0x811c9dc5u;
    while (*path=='/' || *path=='\\') ++path;
    for (;*path;++path) {
        unsigned char c=*path;
        if(c=='\\')c='/';
        if(c>='a'&&c<='z')c-=32;
        h=(h^c)*0x01000193u;
    }
    return h;
}
static const Index *find(unsigned key)
{
    unsigned lo=0,hi=index_count;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(index_rows[mid].key<key)lo=mid+1;else hi=mid;}
    return lo<index_count&&index_rows[lo].key==key?&index_rows[lo]:NULL;
}
static int compare_signatures(const void *left,const void *right)
{
    unsigned a=index_rows[*(const unsigned*)left].signature,b=index_rows[*(const unsigned*)right].signature;
    return a<b?-1:a>b;
}
static int enqueue(Command command)
{
    if (!initialized||!running) return 0;
    unsigned head=queue_in;
    if(head-queue_out>=QUEUE){++p4_audio_dropped;return 0;}
    commands[head&(QUEUE-1)]=command; BARRIER(); queue_in=head+1;
    return 1;
}
int p4_audio_play_path(int channel,const char *path)
{
    if(channel<0||channel>=STREAMS||!path)return 0;
    const char *p=strchr(path,':'); if(p)path=p+1;
    /* Retail BGM paths may have only the basename. */
    char canonical[256];
    if(!strchr(path,'/')&&!strchr(path,'\\'))snprintf(canonical,sizeof canonical,"BGM/%s",path);
    else snprintf(canonical,sizeof canonical,"%s",path);
    requested[channel]=1;
    int result=enqueue((Command){CMD_PATH,channel,0,0,0,p4_audio_hash(canonical)});
    if(!result)requested[channel]=0;
    return result;
}
int p4_audio_play_memory(int channel,const void *data,unsigned size)
{
    if(!data||size<24||channel<0||channel>=STREAMS)return 0;
    const unsigned char *p=data;
    if(p[0]!=0x80||p[1]!=0)return 0;
    unsigned h=0x811c9dc5u,n=size<1024?size:1024;
    for(unsigned i=0;i<n;++i)h=(h^p[i])*0x01000193u;
    unsigned lo=0,hi=index_count;
    while(lo<hi){unsigned mid=(lo+hi)/2;if(index_rows[signature_index[mid]].signature<h)lo=mid+1;else hi=mid;}
    for(unsigned i=lo;i<index_count;++i){
        const Index *r=&index_rows[signature_index[i]];
        if(r->signature!=h)break;
        if(r->bytes!=size)continue;
        if(r->selector!=0xffffffffu){
            if(r->selector>selector_bytes||selector_bytes-r->selector<4)continue;
            const unsigned char *s=selectors+r->selector;unsigned count=word(s),second=0x811c9dc5u;
            if(count>64||selector_bytes-r->selector-4<count*4)continue;
            for(unsigned k=0;k<count;++k){unsigned at=word(s+4+k*4);second=(second^(at<size?p[at]:0))*0x01000193u;}
            if(second!=r->signature2)continue;
        }
        requested[channel]=1;
        int result=enqueue((Command){CMD_PATH,channel,0,0,0,r->key});
        if(!result)requested[channel]=0;
        return result;
    }
    ++p4_audio_errors;return 0;
}
void p4_audio_stop(int channel,unsigned fade_ms)
{ if(channel>=0&&channel<STREAMS)enqueue((Command){CMD_STOP,channel,(int)fade_ms,0,0,0}); }
int p4_audio_playing(int channel)
{ return initialized&&running&&channel>=0&&channel<STREAMS&&(requested[channel]||streams[channel].pending||streams[channel].state==LOAD||streams[channel].state==PREFILL||streams[channel].state==READY||streams[channel].state==PLAY); }
void p4_audio_effect(int bank,int stream,int group,int cue)
{ if(bank>=0&&bank<6&&stream>=0&&stream<16)enqueue((Command){CMD_EFFECT,stream,bank,group,cue,0}); }
void p4_audio_effect_stop(int stream)
{ if(stream>=0&&stream<16)enqueue((Command){CMD_EFFECT_STOP,stream,0,0,0,0}); }
int p4_audio_bank_ready(int slot)
{ return slot>=0&&slot<6&&(!initialized||!running||(banks_loaded&&(bank_requests[slot].state==0||bank_requests[slot].state>=3))); }
static int bank_request(int slot,int file,const void *hd,unsigned hs,const void *bd,unsigned bs,const void *sq,unsigned ss)
{
    if(!initialized||slot<0||slot>=6)return 0;
    BankRequest *r=&bank_requests[slot];
    if(r->state==1||r->state==2)return 0;
    r->file=file;r->stage=0;r->position=0;
    r->source[0]=hd;r->source[1]=bd;r->source[2]=sq;
    r->bytes[0]=hs;r->bytes[1]=bs;r->bytes[2]=ss;
    memset(&r->next,0,sizeof r->next);BARRIER();r->state=1;
    if(!enqueue((Command){CMD_BANK,slot,0,0,0,0})){r->state=4;return 0;}
    return 1;
}
int p4_audio_bank_file(int slot,int file)
{ return bank_request(slot,file,NULL,0,NULL,0,NULL,0); }
int p4_audio_bank_memory(int slot,const void *hd,unsigned hs,const void *bd,unsigned bs,const void *sq,unsigned ss)
{ return bank_request(slot,-1,hd,hs,bd,bs,sq,ss); }

static int read_exact(int fd,void *data,unsigned size)
{
    unsigned pos=0;
    while(pos<size){int n=sceIoRead(fd,(char*)data+pos,size-pos);if(n<=0)return -1;pos+=n;}
    return 0;
}
static unsigned char *load_file(const char *name,unsigned *size)
{
    char path[128];snprintf(path,sizeof path,DIR "%s",name);
    int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return NULL;
    long long n=sceIoLseek(fd,0,PSP_SEEK_END);sceIoLseek(fd,0,PSP_SEEK_SET);
    unsigned char *p=n>0&&n<4*1024*1024?memalign(64,(unsigned)n):NULL;
    if(p&&read_exact(fd,p,(unsigned)n)<0){free(p);p=NULL;}
    sceIoClose(fd);if(p)*size=(unsigned)n;return p;
}
static void load_banks(void)
{
    const char *names[2]={"COMSE","BSE"};
    for(int i=0;i<2;++i){
        char name[32];Bank *b=&default_banks[i];
        snprintf(name,sizeof name,"%s.HD",names[i]);b->hd=load_file(name,&b->hsize);
        snprintf(name,sizeof name,"%s.BD",names[i]);b->bd=load_file(name,&b->bsize);
        snprintf(name,sizeof name,"%s.SQ",names[i]);b->sq=load_file(name,&b->ssize);
        if(!b->hd||!b->bd||!b->sq||b->hsize<80||b->ssize<64||memcmp(b->hd,"IECS",4)||memcmp(b->sq,"IECS",4)){
            ++p4_audio_errors;free(b->hd);free(b->bd);free(b->sq);memset(b,0,sizeof *b);
        }
    }
    for(int i=0;i<6;++i)banks[i]=default_banks[i&1];
    sceKernelDcacheWritebackAll();BARRIER();banks_loaded=1;
}

static void free_bank(Bank *b)
{ if(b->owned){free(b->hd);free(b->bd);free(b->sq);}memset(b,0,sizeof *b); }
static void bank_copy_step(int slot)
{
    BankRequest *r=&bank_requests[slot];Bank *next=&r->next;
    if(r->file>=0){
        if(r->file>2)goto error;
        free_bank(&banks[slot]);
        if(r->file)banks[slot]=default_banks[r->file-1];
        BARRIER();r->state=3;return;
    }
    if(!r->stage&&!r->position&&!next->hd){
        for(int i=0;i<3;++i)if(!r->source[i]||!r->bytes[i]||r->bytes[i]>4*1024*1024)goto error;
        next->owned=1;next->hsize=r->bytes[0];next->bsize=r->bytes[1];next->ssize=r->bytes[2];
        next->hd=memalign(64,next->hsize);next->bd=memalign(64,next->bsize);next->sq=memalign(64,next->ssize);
        if(!next->hd||!next->bd||!next->sq)goto error;
    }
    unsigned char *dest[3]={next->hd,next->bd,next->sq};
    unsigned n=r->bytes[r->stage]-r->position;if(n>8192)n=8192;
    memcpy(dest[r->stage]+r->position,(const char*)r->source[r->stage]+r->position,n);
    sceKernelDcacheWritebackRange(dest[r->stage]+r->position,n);
    r->position+=n;
    if(r->position==r->bytes[r->stage]){++r->stage;r->position=0;}
    if(r->stage==3){
        if(next->hsize<80||next->ssize<64||memcmp(next->hd,"IECS",4)||memcmp(next->sq,"IECS",4))goto error;
        free_bank(&banks[slot]);banks[slot]=*next;memset(next,0,sizeof *next);
        BARRIER();r->state=3;
    }
    return;
error:free_bank(next);++p4_audio_errors;BARRIER();r->state=4;
}

/* Producer writes only chunks at least half a ring ahead of the consumer.
 * Publish produced AFTER the writes have reached RAM for the Media Engine. */
static int refill(Stream *s)
{
    unsigned remaining=s->header.blocks-s->source_block;
    unsigned count=remaining<CHUNK_BLOCKS?remaining:CHUNK_BLOCKS;
    if(!count){
        if(s->header.loop!=0xffffffffu){
            s->source_block=s->header.loop;
            if(sceIoLseek(s->fd,s->data_offset+32+(long long)s->source_block*16*s->header.channels,PSP_SEEK_SET)<0)return -1;
            remaining=s->header.blocks-s->source_block;count=remaining<CHUNK_BLOCKS?remaining:CHUNK_BLOCKS;
        }else count=CHUNK_BLOCKS;
    }
    int silence=s->source_block==s->header.blocks&&s->header.loop==0xffffffffu;
    if(silence)memset(input,0,count*16*s->header.channels);
    else if(read_exact(s->fd,input,count*16*s->header.channels)<0)return -1;
    for(unsigned i=0;i<count;++i){
        unsigned dst=(s->produced+i)&(RING_BLOCKS-1);
        for(unsigned c=0;c<s->header.channels;++c){
            unsigned char *p=s->ring[c]+dst*16;
            memcpy(p,input+(i*s->header.channels+c)*16,16);
            p[1]=dst==0?6:(dst==RING_BLOCKS-1?3:2);
        }
    }
    /* Flush AFTER writing the whole range (the loop above may begin mid-chunk). */
    for(unsigned c=0;c<s->header.channels;++c)sceKernelDcacheWritebackRange(s->ring[c],RING_BYTES);
    s->source_block+=silence?0:count;BARRIER();s->produced+=count;
    return 0;
}
static int open_stream(Stream *s)
{
    const Index *row=find(s->id);if(!row)return -1;
    char name[128];
    if(row->part==0xffffffffu)snprintf(name,sizeof name,DIR "%08x.p4a",s->id);
    else snprintf(name,sizeof name,DIR "AUDIO.%u",row->part);
    s->fd=sceIoOpen(name,PSP_O_RDONLY,0);if(s->fd<0)return -1;
    s->data_offset=row->offset;
    if(sceIoLseek(s->fd,s->data_offset,PSP_SEEK_SET)<0)return -1;
    if(read_exact(s->fd,&s->header,sizeof s->header)<0)return -1;
    Header *h=&s->header;
    if(memcmp(h->magic,"P4A1",4)||h->version!=1||h->channels<1||h->channels>2||h->rate<8000||h->rate>48000||
       !h->samples||!h->blocks||h->blocks!=(h->samples+27)/28||(h->loop!=0xffffffffu&&h->loop>=h->blocks))return -1;
    unsigned block=s->resume_block<h->blocks?s->resume_block:0;
    s->source_block=block;s->source_samples=block*28;s->resume_block=0;
    s->produced=s->consumed=s->played=s->fraction=0;
    if(block&&sceIoLseek(s->fd,s->data_offset+32+(long long)block*16*h->channels,PSP_SEEK_SET)<0)return -1;
    s->pitch=(h->rate*4096u+22050)/44100;
    return 0;
}
static int loader_main(SceSize size,void *arg)
{
    (void)size;(void)arg;
    if(!banks_loaded)load_banks();
    unsigned long long start=runtime();
    unsigned cpu_base=p4_audio_loader_us;
    unsigned cursor=0,bank_cursor=0;
    while(running){
        if(paused){p4_audio_loader_us=cpu_base+(unsigned)(runtime()-start);sceKernelDelayThread(20000);continue;}
        int did_work=0;
        for(int n=0;n<STREAMS;++n){
            int i=(cursor+n)%STREAMS;
            Stream *s=&streams[i];int state=s->state;
            if(state==CLOSE){if(s->fd>=0)sceIoClose(s->fd);s->fd=-1;BARRIER();s->state=EMPTY;}
            else if(state==LOAD){
                int result=open_stream(s);
                if(result<0){if(s->fd>=0)sceIoClose(s->fd);s->fd=-1;++p4_audio_errors;}
                BARRIER();if(s->state==LOAD)s->state=result<0?FAILED:PREFILL;
                cursor=(i+1)%STREAMS;did_work=1;break;
            }else if(state==PREFILL){
                int result=refill(s);
                if(result<0){++p4_audio_errors;if(s->state==PREFILL)s->state=FAILED;}
                else if(s->produced>=RING_BLOCKS/2&&s->state==PREFILL){BARRIER();s->state=READY;}
                cursor=(i+1)%STREAMS;did_work=1;break;
            }else if(state==PLAY&&s->produced-s->consumed<RING_BLOCKS-CHUNK_BLOCKS*2){
                if(refill(s)<0){++p4_audio_errors;if(s->fd>=0)sceIoClose(s->fd);s->fd=-1;BARRIER();if(s->state==PLAY)s->state=FAILED;}
                cursor=(i+1)%STREAMS;did_work=1;break;
            }
        }
        if(!did_work)for(int n=0;n<6;++n){int i=(bank_cursor+n)%6;if(bank_requests[i].state==2){bank_copy_step(i);bank_cursor=(i+1)%6;break;}}
        p4_audio_loader_us=cpu_base+(unsigned)(runtime()-start);
        sceKernelDelayThread(10000);
    }
    for(int i=0;i<STREAMS;++i)if(streams[i].fd>=0)sceIoClose(streams[i].fd);
    return 0;
}

static void stop_voices(int first,int count)
{
    for(int i=first;i<first+count;++i){
        if(voices[i].active){__sceSasSetKeyOff(&sas,i);voices[i].active=0;}
        __sceSasSetPause(&sas,1u<<i,1);
    }
}
static void effect_stop(int owner)
{
    sequences[owner].active=0;
    for(int v=10;v<32;++v)if(voices[v].active&&voices[v].owner==owner)stop_voices(v,1);
}
static int offset_ok(unsigned offset,unsigned bytes,unsigned total)
{ return offset<=total&&bytes<=total-offset; }
static int effect_start(int owner,int bank,int group,int cue)
{
    effect_stop(owner);
    Bank *b=&banks[bank];if(!banks_loaded||!b->sq||!p4_audio_bank_ready(bank))return -1;
    unsigned base=word(b->sq+0x28);
    if(!offset_ok(base,36,b->ssize)||group<0||(unsigned)group>word(b->sq+base+12)||
       !offset_ok(base+32+group*4,4,b->ssize))return -1;
    unsigned g=base+word(b->sq+base+32+group*4);
    if(!offset_ok(g,16,b->ssize)||cue<0||(unsigned)cue>word(b->sq+g)||!offset_ok(g+16+cue*4,4,b->ssize))return -1;
    unsigned leaf=base+word(b->sq+g+16+cue*4);
    if(!offset_ok(leaf,16,b->ssize))return -1;
    unsigned offset=leaf+word(b->sq+leaf),bytes=word(b->sq+leaf+8);
    if(!offset_ok(offset,bytes,b->ssize)||!bytes)return -1;
    Sequence *s=&sequences[owner];memset(s,0,sizeof *s);
    s->active=1;s->bank=bank;s->group=group;s->cue=cue;s->offset=offset;s->end=offset+bytes;
    s->scale=(unsigned)b->sq[base+20]*b->sq[g+8]*b->sq[leaf+4];
    s->tempo=half(b->sq+leaf+6); /* authored microseconds per SQ tick */
    return 0;
}
static void note_on(int owner,int group,int timbre,int velocity)
{
    Sequence *seq=&sequences[owner];Bank *b=&banks[seq->bank];
    unsigned c=word(b->hd+0x34);
    if(!offset_ok(c,28,b->hsize)||group<0||(unsigned)group>word(b->hd+c+12)||!offset_ok(c+24+group*4,4,b->hsize))goto bad;
    unsigned g=c+word(b->hd+c+24+group*4);
    if(!offset_ok(g,8,b->hsize)||timbre<0||timbre>b->hd[g+6])goto bad;
    unsigned t=g+word(b->hd+g)+timbre*half(b->hd+g+4);
    if(!offset_ok(t,50,b->hsize))goto bad;
    unsigned sample=half(b->hd+t),vag=word(b->hd+0x30);
    if(!offset_ok(vag,16,b->hsize)||sample>word(b->hd+vag+12)||!offset_ok(vag+16+sample*4,4,b->hsize))goto bad;
    unsigned vi=vag+word(b->hd+vag+16+sample*4);
    if(!offset_ok(vi,8,b->hsize))goto bad;
    unsigned start=word(b->hd+vi),end=start;
    if(start%16||!offset_ok(start,16,b->bsize))goto bad;
    while(offset_ok(end,16,b->bsize)){unsigned flags=b->bd[end+1];end+=16;if(flags&1)break;}
    if(end<=start||end>b->bsize)goto bad;
    unsigned done=(unsigned)__sceSasGetEndFlag(&sas);int v=-1;
    for(int i=10;i<32;++i)if(!voices[i].active||(done&(1u<<i))){v=i;break;}
    if(v<0){static unsigned steal;v=10+(steal++%22);stop_voices(v,1);}
    unsigned pitch=(unsigned)(4096.0*(half(b->hd+vi+4)/44100.0)*pow(2.0,((signed char)b->hd[t+6]+(signed char)b->hd[t+2]/128.0)/12.0));
    if(pitch<1)pitch=1;
    if(pitch>0x4000)pitch=0x4000;
    unsigned volume=(unsigned)((double)4096*velocity*b->hd[t+3]*seq->scale/(127.0*127*127*127*127));
    if(volume>4096)volume=4096;
    unsigned pan=b->hd[t+4];if(pan>127)pan=64;
    int result=__sceSasSetVoice(&sas,v,b->bd+start,end-start,b->hd[vi+6]!=0);
    if(result<0)goto bad;
    __sceSasSetPitch(&sas,v,pitch);
    __sceSasSetSimpleADSR(&sas,v,half(b->hd+t+14),half(b->hd+t+16));
    unsigned left=volume*(127-pan)/64,right=volume*pan/64;
    __sceSasSetVolume(&sas,v,left>4096?4096:left,right>4096?4096:right,0,0);
    __sceSasSetPause(&sas,1u<<v,0);
    if(__sceSasSetKeyOn(&sas,v)<0)goto bad;
    voices[v]=(Voice){1,owner,seq->bank,group,timbre,pitch,volume,pan};
    return;
bad: ++p4_audio_errors;
}
static int varlen(const unsigned char *p,unsigned *offset,unsigned end,unsigned *value)
{
    *value=0;
    for(int n=0;n<4&&*offset<end;++n){unsigned c=p[(*offset)++];*value=(*value<<7)|(c&127);if(!(c&128))return 0;}
    return -1;
}
static void sequence_step(int owner)
{
    Sequence *s=&sequences[owner];if(!s->active)return;
    const unsigned char *p=banks[s->bank].sq;
    s->tick+=GRAIN*1000000u/44100;
    for(int count=0;count<64&&s->active;++count){
        unsigned at=s->offset,delta;
        if(varlen(p,&at,s->end,&delta)<0){s->active=0;++p4_audio_errors;break;}
        unsigned time=s->next+delta*s->tempo;
        if(time>s->tick)break;
        s->next=time;
        if(at>=s->end){s->active=0;break;}
        unsigned cmd=p[at++];
        if(cmd==0xff){s->active=0;break;}
        unsigned n=cmd==0xc0?1:3;
        if(!offset_ok(at,n,s->end)){s->active=0;++p4_audio_errors;break;}
        int group=p[at],timbre=n>1?p[at+1]:0,value=n>2?p[at+2]:0;at+=n;
        if(cmd==0x90)note_on(owner,group,timbre,value);
        else if(cmd==0xa0){
            for(int v=10;v<32;++v)if(voices[v].active&&voices[v].owner==owner&&voices[v].group==group&&voices[v].timbre==timbre){
                __sceSasSetKeyOff(&sas,v);voices[v].active=0;
            }
        }else if(cmd==0xb0){
            /* Parameter automation: parameter byte followed by a VLQ duration
             * and a signed 16-bit target. Retain event timing, never desync. */
            unsigned duration;
            if(varlen(p,&at,s->end,&duration)<0||!offset_ok(at,2,s->end)){s->active=0;++p4_audio_errors;break;}
            at+=2;
        }else if(cmd!=0xc0&&cmd!=0x11){s->active=0;++p4_audio_errors;break;}
        s->offset=at;
    }
}

static void process_commands(void)
{
    for(unsigned n=0;n<QUEUE&&queue_out!=queue_in;++n){
        BARRIER();Command cmd=commands[queue_out&(QUEUE-1)];++queue_out;++p4_audio_commands;
        if(cmd.op==CMD_BANK){
            for(int i=0;i<16;++i)if(sequences[i].bank==cmd.channel)effect_stop(i);
            for(int v=10;v<32;++v)if(voices[v].active&&voices[v].bank==cmd.channel)stop_voices(v,1);
            BARRIER();bank_requests[cmd.channel].state=2;continue;
        }
        if(cmd.op==CMD_EFFECT){if(effect_start(cmd.channel,cmd.a,cmd.b,cmd.c)<0)++p4_audio_errors;continue;}
        if(cmd.op==CMD_EFFECT_STOP){effect_stop(cmd.channel);continue;}
        Stream *s=&streams[cmd.channel];
        if(cmd.op==CMD_STOP){
            s->pending=0;requested[cmd.channel]=0;
            if(cmd.a>0&&s->state==PLAY){s->fade_total=s->fade_left=(unsigned)cmd.a*44100/1000;}
            else {stop_voices(cmd.channel*2,2);if(s->state!=EMPTY&&s->state!=FAILED)s->state=CLOSE;}
            continue;
        }
        const Index *row=NULL;
        if(cmd.op==CMD_PATH)row=find(cmd.key);
        else for(unsigned i=0;i<index_count;++i)if(index_rows[i].signature==cmd.key&&
            (!index_rows[i].signature2||(index_rows[i].signature2==(unsigned)cmd.a&&index_rows[i].bytes==(unsigned)cmd.b))){row=&index_rows[i];break;}
        if(!row){++p4_audio_errors;requested[cmd.channel]=0;continue;}
        if(s->state==PLAY&&s->id==row->id&&!s->pending){
            s->fade_left=0;requested[cmd.channel]=1;
            for(unsigned c=0;c<s->header.channels;++c)__sceSasSetVolume(&sas,cmd.channel*2+c,
                s->header.channels==1||c==0?4096:0,s->header.channels==1||c==1?4096:0,0,0);
            continue;
        }
        s->pending_id=row->id;s->pending=1;s->resume_block=0;requested[cmd.channel]=1;
        stop_voices(cmd.channel*2,2);
        if(s->state!=EMPTY&&s->state!=FAILED)s->state=CLOSE;
    }
}
static void stream_step(int channel)
{
    Stream *s=&streams[channel];int first=channel*2;
    if(s->pending&&(s->state==EMPTY||s->state==FAILED)){
        s->id=s->pending_id;s->pending=0;s->fade_left=0;BARRIER();s->state=LOAD;
    }
    if(s->state==FAILED){stop_voices(first,2);requested[channel]=0;return;}
    if(s->state==READY){
        for(unsigned c=0;c<s->header.channels;++c){
            int v=first+c;
            if(__sceSasSetVoice(&sas,v,s->ring[c],RING_BYTES,1)<0){++p4_audio_errors;stop_voices(first,2);s->state=CLOSE;requested[channel]=0;return;}
            __sceSasSetPitch(&sas,v,s->pitch);
            __sceSasSetADSRmode(&sas,v,PSP_SAS_ADSR_EVERYTHING,0,1,0,5);
            __sceSasSetADSR(&sas,v,PSP_SAS_ADSR_EVERYTHING,0x7fffffff,0,0,0);
            unsigned left=s->header.channels==1||c==0?4096:0,right=s->header.channels==1||c==1?4096:0;
            __sceSasSetVolume(&sas,v,left,right,0,0);
            __sceSasSetPause(&sas,1u<<v,0);
            if(__sceSasSetKeyOn(&sas,v)<0){++p4_audio_errors;stop_voices(first,2);s->state=CLOSE;requested[channel]=0;return;}
            voices[v].active=1;
        }
        ++p4_audio_started;BARRIER();s->state=PLAY;
    }
    if(s->state!=PLAY)return;
    unsigned advance=(s->fraction+GRAIN*s->pitch)>>12;
    if(s->produced*28u<s->played+advance){++p4_audio_underruns;stop_voices(first,2);s->state=CLOSE;requested[channel]=0;return;}
    if(s->header.loop==0xffffffffu&&s->played+s->source_samples>=s->header.samples){
        stop_voices(first,2);s->state=CLOSE;requested[channel]=0;++p4_audio_finished;return;
    }
    if(s->fade_left){
        if(s->fade_left<=GRAIN){stop_voices(first,2);s->state=CLOSE;requested[channel]=0;return;}
        s->fade_left-=GRAIN;unsigned volume=4096*s->fade_left/s->fade_total;
        for(unsigned c=0;c<s->header.channels;++c)__sceSasSetVolume(&sas,first+c,
            s->header.channels==1||c==0?volume:0,s->header.channels==1||c==1?volume:0,0,0);
    }
    s->played+=advance;s->fraction=(s->fraction+GRAIN*s->pitch)&4095;
    BARRIER();s->consumed=s->played/28;
}
static int audio_failure(const char *operation,int result)
{
    ++p4_audio_errors;p4_audio_last_error=result;
    p4_log("audio %s failed %08x step %u\n",operation,result,p4_audio_step);
    return result;
}
static int release_output(void)
{
    if(!output_reserved)return 0;
    p4_audio_step=4;
    unsigned start=sceKernelGetSystemTimeLow();
    int rest;
    while((rest=sceAudioOutput2GetRestSample())>0){
        if(sceKernelGetSystemTimeLow()-start>=500000)
            return audio_failure("Output2 drain timeout",-1);
        sceKernelDelayThread(1000);
    }
    if(rest<0)return audio_failure("Output2 rest",rest);
    int r=sceAudioOutput2Release();
    if(r<0)return audio_failure("Output2 release",r);
    output_reserved=0;return 0;
}
static int audio_main(SceSize size,void *arg)
{
    (void)size;(void)arg;
    unsigned long long start=runtime();unsigned counter=0,pause_mask=0;
    unsigned cpu_base=p4_audio_cpu_us;
    while(running){
        if(paused){
            if(!pause_ack){
                pause_mask=(unsigned)__sceSasGetPauseFlag(&sas);
                int r=__sceSasSetPause(&sas,0xffffffffu,1);
                if(r<0){audio_failure("SAS pause",r);break;}
                /* SetPause updates the context. Submit one silent grain before
                 * acknowledging so the last SAS operation has completed before AVC. */
                p4_audio_step=3;
                r=__sceSasCore(&sas,output[counter&1]);
                if(r<0){audio_failure("SAS pause commit",r);break;}
                if(release_output()<0)break;
                p4_audio_step=5;BARRIER();pause_ack=1;
            }
            sceKernelDelayThread(1000);continue;
        }
        if(!output_reserved){int r=sceAudioOutput2Reserve(GRAIN);if(r<0){audio_failure("Output2 reserve",r);break;}output_reserved=1;}
        if(pause_ack){
            p4_audio_step=6;
            int r=__sceSasSetPause(&sas,~pause_mask,0);
            if(r<0){audio_failure("SAS resume",r);break;}
            BARRIER();pause_ack=0;
        }
        process_commands();
        for(int i=0;i<STREAMS;++i)stream_step(i);
        for(int i=0;i<16;++i)sequence_step(i);
        p4_audio_step=1;
        int r=__sceSasCore(&sas,output[counter&1]);
        if(r<0){audio_failure("SAS core",r);break;}
        p4_audio_step=2;
        r=sceAudioOutput2OutputBlocking(PSP_AUDIO_VOLUME_MAX,output[counter&1]);
        if(r<0){audio_failure("Output2 output",r);break;}
        ++counter;++p4_audio_blocks;
        if(!(counter&63))p4_audio_cpu_us=cpu_base+(unsigned)(runtime()-start);
    }
    running=0;BARRIER();
    release_output();
    for(int i=0;i<STREAMS;++i)requested[i]=0;
    return 0;
}
static int audio_modules_start(void);
static int audio_workers_start(void);
int p4_audio_init(void)
{
    if(initialized)return 0;
    unsigned bytes=0;unsigned char *raw=load_file("audio.idx",&bytes);
    if(!raw){p4_log("audio missing SOUND/audio.idx\n");return -1;}
    unsigned packed=bytes>=16&&(!memcmp(raw,"P4I1",4)||!memcmp(raw,"P4I2",4)||!memcmp(raw,"P4I3",4));
    unsigned stride=packed?word(raw+8):16,skip=packed?16:0;
    index_count=packed?word(raw+4):bytes/16;
    selector_bytes=packed?word(raw+12):0;
    if((stride!=16&&stride!=24&&stride!=28&&stride!=32)||(!packed&&stride!=16)||index_count>100000||selector_bytes>bytes-skip||bytes-skip-selector_bytes!=index_count*stride){free(raw);index_count=0;return -1;}
    index_rows=calloc(index_count,sizeof(Index));
    if(!index_rows){free(raw);index_count=0;return -1;}
    for(unsigned i=0;i<index_count;++i){
        Index *r=&index_rows[i];memcpy(r,raw+skip+i*stride,stride);
        if(!packed){r->part=0xffffffffu;r->offset=0;}
        if(stride<32)r->selector=0xffffffffu;
        if(i&&r->key<=index_rows[i-1].key){free(raw);free(index_rows);index_rows=NULL;index_count=0;return -1;}
    }
    index_storage=raw;selectors=raw+skip+index_count*stride;
    signature_index=malloc(index_count*sizeof(unsigned));
    if(!signature_index){p4_audio_shutdown();return -1;}
    for(unsigned i=0;i<index_count;++i)signature_index[i]=i;
    qsort(signature_index,index_count,sizeof(unsigned),compare_signatures);
    int result=audio_modules_start();
    if(result<0)goto error;
    for(int i=0;i<STREAMS;++i)streams[i].fd=-1;
    initialized=1;
    result=audio_workers_start();if(result<0)goto error;
    p4_log("audio SAS ready: %u streams indexed, grain %d, Output2\n",index_count,GRAIN);return 0;
error:p4_log("audio init error %08x\n",result);p4_audio_shutdown();return -1;
}
static int audio_modules_start(void)
{
    int result;
    if(!codec_module){
        result=sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
        if(result<0&&result!=(int)0x80111102u)return result;
        codec_module=result<0?2:1;
    }
    if(!sas_module){
        result=sceUtilityLoadModule(PSP_MODULE_AV_SASCORE);
        if(result<0&&result!=(int)0x80111102u)return result;
        sas_module=result<0?2:1;
    }
    memset(&sas,0,sizeof sas);
    if((result=__sceSasInit(&sas,GRAIN,32,PSP_SAS_OUTPUTMODE_STEREO,44100))<0)return result;
    __sceSasRevType(&sas,PSP_SAS_EFFECT_TYPE_OFF);__sceSasSetPause(&sas,0xffffffffu,1);
    return 0;
}
static int audio_workers_start(void)
{
    paused=pause_ack=0;running=1;BARRIER();
    loader_thread=sceKernelCreateThread("p4_audio_loader",loader_main,0x1c,32*1024,PSP_THREAD_ATTR_USER,NULL);
    audio_thread=sceKernelCreateThread("p4_audio",audio_main,0x18,32*1024,PSP_THREAD_ATTR_USER,NULL);
    if(loader_thread<0||audio_thread<0){running=0;return -1;}
    int result=sceKernelStartThread(loader_thread,0,NULL);
    if(result<0){running=0;return result;}
    result=sceKernelStartThread(audio_thread,0,NULL);
    if(result<0){running=0;return result;}
    return 0;
}
static void audio_workers_stop(void)
{
    running=0;BARRIER();
    SceUID ids[2]={audio_thread,loader_thread};
    for(int i=0;i<2;++i)if(ids[i]>=0){sceKernelWaitThreadEnd(ids[i],NULL);sceKernelDeleteThread(ids[i]);}
    audio_thread=loader_thread=-1;
}
static int audio_modules_stop(void)
{
    if(sas_module==1){
        int r=sceUtilityUnloadModule(PSP_MODULE_AV_SASCORE);
        p4_log("audio unload SASCORE %08x\n",r);if(r<0)return r;
    }
    sas_module=0;
    if(codec_module==1){
        int r=sceUtilityUnloadModule(PSP_MODULE_AV_AVCODEC);
        p4_log("audio unload AVCODEC %08x\n",r);if(r<0)return r;
    }
    codec_module=0;return 0;
}
int p4_audio_state(void) { return initialized ? (running ? 1 : 2) | (pause_ack ? 4 : 0) : 0; }
int p4_audio_pause(int pause)
{
    if(!initialized)return 0;
    paused=!!pause;BARRIER();
    unsigned start=sceKernelGetSystemTimeLow();
    while(running){
        if(pause_ack==paused){BARRIER();return 0;}
        if(sceKernelGetSystemTimeLow()-start>=500000)break;
        sceKernelDelayThread(1000);
    }
    p4_log("audio %s not acknowledged: running %d ack %d step %u last_error %08x blocks %u\n",
        pause?"pause":"resume",running,pause_ack,p4_audio_step,p4_audio_last_error,p4_audio_blocks);
    return -1;
}
int p4_audio_suspend_for_video(void)
{
    if(!initialized)return 0;
    if(suspended)return 1;
    if(p4_audio_pause(1)<0)return -1;
    unsigned ids[STREAMS],blocks[STREAMS];
    for(int i=0;i<STREAMS;++i){
        Stream *s=&streams[i];ids[i]=blocks[i]=0;
        if(s->pending)ids[i]=s->pending_id;
        else if(s->state==LOAD||s->state==PREFILL||s->state==READY||s->state==PLAY){
            ids[i]=s->id;
            if(s->state==PLAY){
                unsigned block=(s->source_samples+s->played)/28;
                if(block>=s->header.blocks){
                    if(s->header.loop==0xffffffffu){ids[i]=0;continue;}
                    block=s->header.loop+(block-s->header.blocks)%(s->header.blocks-s->header.loop);
                }
                blocks[i]=block;
            }
        }
    }
    audio_workers_stop();
    /* Keep banks, queued requests and stream positions; only firmware is recycled. */
    for(int i=0;i<STREAMS;++i){
        Stream *s=&streams[i];s->fd=-1;s->state=EMPTY;
        s->pending_id=ids[i];s->pending=!!ids[i];s->resume_block=blocks[i];requested[i]=!!ids[i];
    }
    memset(voices,0,sizeof voices);memset(sequences,0,sizeof sequences);
    paused=pause_ack=0;suspended=1;
    if(audio_modules_stop()<0)return -1;
    p4_log("audio suspended: workers stopped, firmware released\n");return 1;
}
int p4_audio_resume_after_video(void)
{
    if(!suspended)return 0;
    int r=audio_modules_start();
    if(r>=0)r=audio_workers_start();
    if(r<0){audio_failure("restart after video",r);return r;}
    suspended=0;p4_log("audio resumed: firmware and workers restarted\n");return 0;
}
void p4_audio_shutdown(void)
{
    audio_workers_stop();
    audio_modules_stop();
    for(int i=0;i<6;++i){free_bank(&banks[i]);free_bank(&bank_requests[i].next);memset(&bank_requests[i],0,sizeof bank_requests[i]);}
    for(int i=0;i<2;++i){free(default_banks[i].hd);free(default_banks[i].bd);free(default_banks[i].sq);memset(&default_banks[i],0,sizeof default_banks[i]);}
    banks_loaded=0;
    free(index_rows);free(index_storage);free(signature_index);
    index_rows=NULL;index_storage=selectors=NULL;signature_index=NULL;index_count=selector_bytes=0;initialized=0;
    paused=pause_ack=suspended=0;queue_in=queue_out=0;
    memset(streams,0,sizeof streams);memset(voices,0,sizeof voices);memset(sequences,0,sizeof sequences);
}
