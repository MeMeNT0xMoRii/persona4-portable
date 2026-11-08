/* Hardware validation of the actual game audio service and streaming rings. */
#include <pspkernel.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "audio.h"
PSP_MODULE_INFO("P4AudioProbe",0,1,0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
PSP_HEAP_SIZE_KB(8*1024);
extern volatile unsigned p4_audio_cpu_us,p4_audio_loader_us,p4_audio_commands;
extern volatile unsigned p4_audio_underruns,p4_audio_errors,p4_audio_blocks,p4_audio_started;
static int fd=-1;
void p4_log(const char *format,...)
{
    char text[512];va_list ap;va_start(ap,format);vsnprintf(text,sizeof text,format,ap);va_end(ap);
    if(fd>=0)sceIoWrite(fd,text,strlen(text));
    printf("%s",text);pspDebugScreenPrintf("%s",text);
}
int main(void)
{
    pspDebugScreenInit();scePowerSetClockFrequency(333,333,166);
    fd=sceIoOpen("ms0:/PSP/GAME/P4SAS/audio.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
    if(p4_audio_init()<0)return -1;
    p4_audio_play_path(0,"BGM/01.ADX");
    sceKernelDelayThread(2000000);
    unsigned cpu=p4_audio_cpu_us,loader=p4_audio_loader_us,start=sceKernelGetSystemTimeLow();
    for(int i=0;i<20;++i){
        p4_audio_effect(0,i%4,0,i%10);
        sceKernelDelayThread(1000000);
    }
    unsigned wall=sceKernelGetSystemTimeLow()-start;
    p4_log("audio probe wall=%u cpu=%u loader=%u ms_per_tick=%.4f blocks=%u starts=%u commands=%u underruns=%u errors=%u\n",
        wall,p4_audio_cpu_us-cpu,p4_audio_loader_us-loader,
        (double)((p4_audio_cpu_us-cpu)+(p4_audio_loader_us-loader))*1000/(30.0*wall),
        p4_audio_blocks,p4_audio_started,p4_audio_commands,p4_audio_underruns,p4_audio_errors);
    p4_audio_pause(1);sceKernelDelayThread(500000);p4_audio_pause(0);
    p4_audio_stop(0,500);sceKernelDelayThread(1000000);
    p4_audio_shutdown();p4_log("audio probe DONE\n");
    if(fd>=0)sceIoClose(fd);
    sceKernelExitDeleteThread(0);return 0;
}
