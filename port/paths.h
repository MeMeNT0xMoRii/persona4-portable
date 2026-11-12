#ifndef P4_PATHS_H
#define P4_PATHS_H
#define P4_PATH_MAX 256
extern char p4_game_dir[160];
extern char p4_save_dir[176];
extern char p4_sound_dir[176];
/* Resolve once before starting any game threads; paths then stay immutable. */
int p4_paths_init(const char *executable, const char *working_directory);
#endif
