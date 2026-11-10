/* Atlus sound API compatibility. CRI/PS2 IOP implementation is not linked. */
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
extern void p4_log(const char *fmt,...);
static short current_cue=-1,current_track=-1;
static char partitions[256][192];
static unsigned slot_complete[6];

static const char *table_name(unsigned address,unsigned index)
{ return *(const char **)(P4_K+address+index*12); }

/* b176: ms0:/PSP/GAME/P4/NOSOUND present: the SAS player is never started (the game runs silent), to test whether
   movies still hang after ten minutes of play without it (b169, b172, b174: the Media Engine stops answering). */
void sdkSndInit(void)
{
    current_cue=current_track=-1;
    for(int i=0;i<6;++i)slot_complete[i]=1;
    SceIoStat st;
    if(sceIoGetstat("ms0:/PSP/GAME/P4/NOSOUND",&st)>=0){p4_log("audio off: NOSOUND present\n");return;}
    p4_audio_init();
}
void sdkSndUpdate(void)
{
    for(int i=0;i<6;++i)if(p4_audio_bank_ready(i)){
        slot_complete[i]=1;
        unsigned char *slot=(unsigned char*)(P4_K+0x8d3ed0+i*0x28);
        *(short*)slot=1;*(unsigned*)(slot+8)=1;
    }
}
void sdkSndUpdateDecodeSlots(void) {}
short hsndGetCurrentCue(void) { return current_cue; }
short hsndGetCurrentTrack(void) { return current_track; }
int sdkSndPlayCue(int cue,int unused)
{
    (void)unused;cue=(short)cue;
    if(cue<0||cue>=49)return 0;
    if(current_cue==cue&&p4_audio_playing(0))return 1;
    char path[128];snprintf(path,sizeof path,"BGM/%s",table_name(0x7118b4,cue));
    int result=p4_audio_play_path(0,path);if(result)current_cue=cue;
    return result;
}
int sdkSndRequestTrack(int track)
{
    if(track<0||track>=66)return 0;
    if(current_track==track&&p4_audio_playing(3))return 1;
    char path[128];snprintf(path,sizeof path,"ENV/%s",table_name(0x711f24,track));
    int result=p4_audio_play_path(3,path);if(result)current_track=track;
    return result;
}
int sdkSndStartChannel(short channel,const char *path)
{ return p4_audio_play_path(channel,path); }
int sdkSndStopChannelSlot(int channel)
{
    p4_audio_stop(channel,0);if(channel==0)current_cue=-1;if(channel==3)current_track=-1;return 1;
}
int sdkSndStopMainChannel(void) { return sdkSndStopChannelSlot(0); }
int sdkSndStopChannel(short frames)
{ p4_audio_stop(0,frames>0?(unsigned)frames*1000/30:0);current_cue=-1;return 1; }
int sdkSndFadeMainChannel(void) { return sdkSndStopChannel(30); }
int sdkSndFadeOutChannel(int channel,int unused)
{ (void)unused;p4_audio_stop(channel,1000);if(channel==0)current_cue=-1;if(channel==3)current_track=-1;return 1; }
int sdkSndFadeOutChannelFrames(short channel,short unused,int frames)
{ (void)unused;p4_audio_stop(channel,frames>0?(unsigned)frames*1000/30:0);return 1; }
int sdkSndResetChannel(short channel) { return sdkSndStopChannelSlot(channel); }
int sdkSndIsChannelPlaying(short channel) { return channel==4?0:p4_audio_playing(channel); }
int sdkSndStartStream(short index,short stream,short group,short cue)
{
    if(index<0||index>=6)return 0;
    p4_audio_effect(index,stream,group,cue);return 1;
}
void sdkSndStartStreamSlot(short index,short stream,short group,short cue)
{ sdkSndStartStream(index,stream,group,cue); }
int sdkSndStopStream(short index,short stream)
{ (void)index;p4_audio_effect_stop(stream);return 1; }
void sdkSndStopStreamSlot(short index,short stream) { sdkSndStopStream(index,stream); }
int hsndSlotIsStarting(int index) { return p4_audio_bank_ready(index); }
int sdkSndSetSlotTrack(int slot,short file)
{
    if(slot<0||slot>=6)return 0;
    slot_complete[slot]=0;return p4_audio_bank_file(slot,file);
}
void sdkSndLoadDecodeFiles(short slot,short file) { sdkSndSetSlotTrack(slot,file); }
int sdkSndIsDecodeSlotReady(short slot) { return p4_audio_bank_ready(slot); }
void sdkSndReleaseDecodeSlot(short slot) { if(slot>=0&&slot<6){p4_audio_bank_file(slot,1);slot_complete[slot]=0;} }
void sdkSndLoadDecodeBuffers(short slot,short file,void *hd,unsigned hs,void *bd,unsigned bs,void *sq,unsigned ss)
{ (void)file;if(slot>=0&&slot<6){slot_complete[slot]=0;p4_audio_bank_memory(slot,hd,hs,bd,bs,sq,ss);} }
int sdkSndQueueSlotRequest(int slot,void *hd,unsigned hs,void *bd,unsigned bs,void *sq,unsigned ss)
{
    if(slot<0||slot>=6)return 0;
    slot_complete[slot]=0;
    unsigned char *work=(unsigned char*)(P4_K+0x8d3ed0+slot*0x28);
    *(short*)work=3;*(unsigned*)(work+8)=0;
    return p4_audio_bank_memory(slot,hd,hs,bd,bs,sq,ss);
}

/* AFS partition registration retains the original ordinal numbering without
 * loading PS2 CRI structures or allocating their IOP transfer buffers. */
int H_Cdvd_StartStreamLoad(int partition,const char *path,int flags,void *completion)
{
    (void)flags;(void)completion;
    if(partition<0||partition>=256||!path)return -1;
    const char *p=strchr(path,':');if(p)path=p+1;
    while(*path=='/'||*path=='\\')++path;
    if(!strchr(path,'/')&&!strchr(path,'\\'))snprintf(partitions[partition],sizeof partitions[partition],"SOUND/%s",path);
    else snprintf(partitions[partition],sizeof partitions[partition],"%s",path);
    return 0;
}
int func_004c7ef8(int partition) { return partition>=0&&partition<256?3:4; }
int sdkSndStartIndexedSource(int partition,int channel,int cue)
{
    if(partition<0||partition>=256||!partitions[partition][0])return 0;
    char path[256];snprintf(path,sizeof path,"%s#%d",partitions[partition],cue);
    return p4_audio_play_path(channel,path);
}
static unsigned be32(const unsigned char *p) { return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|(p[2]<<8)|p[3]; }
int sdkSndStartSourceStream(short channel,const void *source,short index)
{
    if(!source||index<0||(unsigned)source<0x08800000u||(unsigned)source>0x0c000000u-8)return 0;
    const unsigned char *p=source;
    /* ACX is an uncompressed big-endian table of byte ranges. */
    if(be32(p)!=0||be32(p+4)>10000||(unsigned)index>=be32(p+4)||be32(p+4)*8>0x0c000000u-(unsigned)source-8)return 0;
    unsigned offset=be32(p+8+index*8),size=be32(p+12+index*8);
    if(offset<8+be32(p+4)*8||offset>16*1024*1024||size>16*1024*1024||offset>0x0c000000u-(unsigned)source||size>0x0c000000u-(unsigned)source-offset)return 0;
    return p4_audio_play_memory(channel,p+offset,size);
}
int sdkSndStartMonoBuffer(short channel,const void *data,unsigned size)
{ return p4_audio_play_memory(channel,data,size); }
int sdkSndStartStereoBuffer(short channel,const void *data,unsigned size)
{ return p4_audio_play_memory(channel,data,size); }
void sdkSndSetMainChannelData(void *data) { (void)data; }
