/* Replace the CRI movie task at its scheduler boundary. The normal task destructor
 * still releases its work and wakes the event waiting for the cutscene to end. */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "video.h"
#include "audio.h"
#include "paths.h"

extern void p4_log(const char *fmt, ...);
extern int iGpffffbaf8;
extern volatile unsigned p4_pad_press, p4_pad_reads;
static void movie_frame(unsigned frame) { iGpffffbaf8 = frame; }
unsigned p4_route_buttons(void);
static unsigned movie_buttons(void)
{
    unsigned route = p4_route_buttons();
    if (!p4_pad_reads) return route;
    p4_pad_reads--;
    return route | p4_pad_press;
}

int mwPlayerUpdate(unsigned char *owner)
{
    unsigned char *work = *(unsigned char **)(owner + 0x38);
    int index = *(short *)(work + 0x1ea);
    if (index < 0 || index >= 23) { p4_log("video invalid movie %d\n", index); return -1; }
    const char *path = *(const char **)(P4_K + 0x712c54 + index * 0x28);
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    char stem[40]; int length = 0;
    while (base[length] && base[length] != '.' && length < (int)sizeof(stem) - 1) {
        stem[length] = toupper((unsigned char)base[length]); length++;
    }
    stem[length] = 0;
    char converted[P4_PATH_MAX]; SceIoStat stat;
    snprintf(converted, sizeof converted, "%sVIDEO/%s.p4v", p4_game_dir, stem);
    if (sceIoGetstat(converted, &stat) < 0) {
        p4_log("video missing %s; continuing\n", converted); return -1;
    }
    p4_log("video task begin index %d %s\n", index, stem);
    if (p4_video_renderer_begin() < 0) { p4_log("video GE did not drain\n"); return -1; }
    void *display = NULL; int stride = 0, format = 0;
    sceDisplayGetFrameBuf(&display, &stride, &format, PSP_DISPLAY_SETBUF_IMMEDIATE);
    p4_video_on_frame = movie_frame;
    p4_video_buttons = movie_buttons;
    int audio_paused = p4_audio_pause(1);
    {
        extern volatile unsigned p4_audio_blocks, p4_audio_errors, p4_audio_underruns, p4_audio_cpu_us;
        int p4_audio_state(void);
        p4_log("video audio state %d (1 running, 4 paused): blocks %u errors %u underruns %u cpu_us %u\n", p4_audio_state(),
               p4_audio_blocks, p4_audio_errors, p4_audio_underruns, p4_audio_cpu_us);
    }
    int result = -1;
    int audio_suspended = -1;
    if (audio_paused == 0) {
        p4_video_shutdown();
        audio_suspended = p4_audio_suspend_for_video();
    }
    if (audio_suspended >= 0) {
        result = p4_video_play(p4_game_dir, stem, 0);
        p4_video_shutdown();
    } else
        p4_log("video skipped: audio firmware could not be released; continuing game\n");
    if (p4_audio_resume_after_video() < 0)
        p4_log("video audio restart failed\n");
    if (audio_suspended < 0) p4_audio_pause(0);
    p4_video_on_frame = NULL; p4_video_buttons = NULL;
    if (display && stride > 0)
        sceDisplaySetFrameBuf(display, stride, format, PSP_DISPLAY_SETBUF_IMMEDIATE);
    p4_video_renderer_end();
    p4_log("video task end %s result %d; returning to game\n", stem, result);
    return -1;
}

/* The scheduler replacement presents directly; no native Sofdec draw nodes remain. */
void movieNativeTaskUpdate(void *context, void *work) { (void)context; (void)work; }
