#ifndef P4_AUDIO_H
#define P4_AUDIO_H

/* All requests are queued; the game thread never reads the memory stick. */
int p4_audio_init(void);
int p4_audio_play_path(int channel, const char *path);
int p4_audio_play_memory(int channel, const void *data, unsigned size);
void p4_audio_stop(int channel, unsigned fade_ms);
int p4_audio_playing(int channel);
void p4_audio_effect(int bank, int stream, int group, int cue);
void p4_audio_effect_stop(int stream);
int p4_audio_bank_file(int slot, int file);
int p4_audio_bank_memory(int slot, const void *hd, unsigned hs, const void *bd, unsigned bs, const void *sq, unsigned ss);
int p4_audio_bank_ready(int slot);
void p4_audio_pause(int pause);
void p4_audio_shutdown(void);
unsigned p4_audio_hash(const char *path);

#endif
