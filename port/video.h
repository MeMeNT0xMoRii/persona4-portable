#ifndef P4_VIDEO_H
#define P4_VIDEO_H
int p4_video_play(const char *directory, const char *stem, int muted);
void p4_video_shutdown(void);
extern void (*p4_video_on_frame)(unsigned);
extern unsigned (*p4_video_buttons)(void);
int p4_video_renderer_begin(void);
void p4_video_renderer_end(void);
#endif
