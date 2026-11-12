#include <stdio.h>
#include <string.h>
#include "paths.h"

char p4_game_dir[160];
char p4_save_dir[176];
char p4_sound_dir[176];

int p4_paths_init(const char *executable, const char *working_directory)
{
    char path[P4_PATH_MAX];
    int length;
    while (executable && !strncmp(executable, "./", 2)) executable += 2;
    if (executable && *executable && strncmp(executable, "ms0:/", 5) &&
        (executable[0] == '/' || strchr(executable, ':'))) return -1;
    if (executable && !strncmp(executable, "ms0:/", 5))
        length = snprintf(path, sizeof path, "%s", executable);
    else if (working_directory && !strncmp(working_directory, "ms0:/", 5))
        length = snprintf(path, sizeof path, "%s/%s", working_directory,
                          executable && *executable ? executable : "EBOOT.PBP");
    else
        return -1;
    if (length < 0 || length >= (int)sizeof path || strchr(path, '\\') ||
        strchr(path + 4, ':') || strstr(path, "/../") || strstr(path, "/./"))
        return -1;
    char *last = strrchr(path, '/');
    if (!last || !last[1]) return -1;
    last[1] = 0;
    /* Collapse repeated separators from the working-directory fallback. */
    for (char *p = path + 5; *p; p++)
        while (*p == '/' && p[1] == '/') memmove(p, p + 1, strlen(p));
    if (strlen(path) >= sizeof p4_game_dir) return -1;
    strcpy(p4_game_dir, path);
    snprintf(p4_save_dir, sizeof p4_save_dir, "%ssaves", p4_game_dir);
    snprintf(p4_sound_dir, sizeof p4_sound_dir, "%sSOUND/", p4_game_dir);
    return 0;
}
