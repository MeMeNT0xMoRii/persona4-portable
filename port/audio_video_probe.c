/* Long-session SAS -> AVC -> SAS regression on real hardware. */
#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "video.h"
PSP_MODULE_INFO("P4AudioVideoProbe",0,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(16*1024);
extern volatile unsigned p4_audio_blocks,p4_audio_errors,p4_audio_underruns;
static int fd=-1;
extern volatile unsigned video_shown;
static unsigned movie_buttons(void) { return video_shown>=90?0x8u:0; }
void p4_log(const char *format,...)
{
    char text[512];va_list ap;va_start(ap,format);vsnprintf(text,sizeof text,format,ap);va_end(ap);
    if(fd>=0)sceIoWrite(fd,text,strlen(text));
    printf("%s",text);
}
static int movie(void)
{
    unsigned before=p4_audio_blocks;
    int suspended=p4_audio_suspend_for_video();
    if(suspended<0){p4_audio_resume_after_video();p4_audio_pause(0);return -3;}
    p4_log("AVTEST pause blocks=%u errors=%u underruns=%u\n",before,p4_audio_errors,p4_audio_underruns);
    p4_video_buttons=movie_buttons;
    int r=p4_video_play("ms0:/PSP/GAME/P4/","P4CTOP2",0);
    p4_video_buttons=NULL;
    p4_video_shutdown();
    if(p4_audio_resume_after_video()<0)return -4;
    sceKernelDelayThread(2000000);
    p4_log("AVTEST movie result=%d resumed_blocks=%u\n",r,p4_audio_blocks-before);
    if(p4_audio_blocks==before)return -5;
    pspDebugScreenInit();
    return r;
}
int main(int argc,char **argv)
{
    unsigned seconds=argc>1?(unsigned)atoi(argv[1]):660;
    unsigned cycles=argc>2?(unsigned)atoi(argv[2]):2;
    if(seconds>3600||!cycles||cycles>10)return -1;
    scePowerSetClockFrequency(333,333,166);
    fd=sceIoOpen("ms0:/PSP/GAME/P4/avtest.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    p4_log("AVTEST start clock=%d seconds=%u cycles=%u\n",scePowerGetCpuClockFrequencyInt(),seconds,cycles);
    int r=p4_audio_init();
    if(r<0)goto done;
    /* Match the game: the opening movie creates a decoder before long SAS use. */
    if((r=movie())<0)goto done;
    for(unsigned cycle=0;cycle<cycles;++cycle){
        if(!p4_audio_play_path(0,"BGM/01.ADX")){r=-2;goto done;}
        unsigned begin=sceKernelGetSystemTimeLow();
        for(unsigned second=0;second<seconds;++second){
            if(second%5==0)p4_audio_effect(0,(second/5)%4,0,(second/5)%10);
            sceKernelDelayThread(1000000);
            pspDebugScreenSetXY(0,0);
            pspDebugScreenPrintf("Audio/video test at 333 MHz\nCycle %u/%u: %u / %u seconds\nAudio blocks %u, errors %u, underruns %u\n\nThe next movie starts when the timer finishes.\n",
                cycle+1,cycles,second+1,seconds,p4_audio_blocks,p4_audio_errors,p4_audio_underruns);
            if(second%30==29)p4_log("AVTEST cycle=%u seconds=%u wall_ms=%u blocks=%u errors=%u underruns=%u\n",
                cycle+1,second+1,(sceKernelGetSystemTimeLow()-begin)/1000,p4_audio_blocks,p4_audio_errors,p4_audio_underruns);
        }
        if((r=movie())<0)goto done;
    }
done:
    /* Stop the worker before deleting the decoder: no concurrent ME calls. */
    p4_audio_shutdown();p4_video_shutdown();
    p4_log("AVTEST DONE result=%d\n",r);
    pspDebugScreenInit();pspDebugScreenPrintf("Audio/video test finished. Result: %d\n",r);
    if(fd>=0)sceIoClose(fd);
    sceKernelExitDeleteThread(r);return r;
}
