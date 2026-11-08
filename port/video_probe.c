/* Isolated entry point for the same AVC/PCM player used by the game. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <psppower.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "video.h"
PSP_MODULE_INFO("P4VideoProbe", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(12 * 1024);
void p4_log(const char *fmt, ...)
{
    char line[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    printf("%s", line);
    int fd = sceIoOpen("ms0:/PSP/GAME/P4VIDEO/video.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd >= 0) { sceIoWrite(fd, line, strlen(line)); sceIoClose(fd); }
}
int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "P4CTOP1";
    int mute = argc > 2 && !strcmp(argv[2], "mute");
    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    int result = p4_video_play("ms0:/PSP/GAME/P4VIDEO/", name, mute);
    p4_video_shutdown();
    sceKernelExitDeleteThread(result);
    return result;
}
