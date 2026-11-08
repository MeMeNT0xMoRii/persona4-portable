/* PS2 services the game expects, on PSP. A global here replaces the game's function of the same name:
 * tools/psp_link.py weakens the game's copy. Replace at the game's own wrapper layer where one exists. */
#include <pspkernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void p4_log(const char *fmt, ...);

/* T0_COUNT: 16-bit counter at bus clock / 256, 147.2 MHz / 256 = 576 kHz. The game reads and clears it. */
static unsigned timer0_base, timer0_view;

unsigned *p4_timer0(void)
{
    unsigned now = sceKernelGetSystemTimeLow() * 576 / 1000;
    if (timer0_view == 0 && timer0_base != 0)
        timer0_base = now; /* the game wrote 0 */
    timer0_view = (now - timer0_base) & 0xFFFF;
    if (timer0_base == 0)
        timer0_base = now | 1;
    return &timer0_view;
}

/* printf-style debug output */
void func_00440b68(const char *fmt, ...)
{
    char text[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    p4_log("game: %s\n", text);
}

void func_0043ed08(void *block)
{
    free(block);
}

/* No IOP on PSP: nothing to reboot and no IRX modules to load. */
void sdkPadInit(void);
void kwlnInitIop(void)
{
    sdkPadInit();
}

/* The retail probe measures the PS2's 32 MB from link addresses: nothing here reads the answer. */
unsigned char *kwlnProbeHeapBudget(void) { return 0; }

/* audio_game.c replaces the game's sound API; PS2-only CRI startup stays off. */
int func_00430e28(void) { return 0; }

/* The CRI start-up in kwlnInitSubsystems and the IOP sound in mdlRenderInitializeBuffers. */
int func_004d32f8(void) { return 0; }
int func_004cc218(void) { return 0; }
int criSetVoiceCounts(void) { return 0; }
int func_004ca720(void) { return 0; }
int func_004d8c78(void) { return 0; }
int func_004cc200(void) { return 0; }
int func_00418b18(void) { return 0; }
int func_004f34b0(void) { return 0; }
int func_004f5bf0(void) { return 0; }
int func_004c6eb0(void) { return 0; }
int func_004c6f50(void) { return 0; }
int func_004f2798(void) { return 0; }
int func_004c7188(void) { return 0; }
int mwPlyInitSfdFx(void) { return 0; }
int func_0043c008(void) { return 0; }
int func_0043c010(void) { return 0; }
int func_0043bb70(void) { return 0; }
int func_0043c0c0(void) { return 0; }
int func_00430f80(void) { return 0; }
int func_0043bcb8(void) { return 0; }

volatile unsigned p4_timer_control;

/* The memory card boot check: its task dies at once, its destroy callback starts the next sequence. */
int titleSoundStartStep(unsigned char *task) { return -1; }


/* RenderWare's string table: reassembly refuses them for their 64-bit character argument, one register here. */
#include <string.h>
#include <ctype.h>
int func_003e47c0(const char *a, const char *b) { return strcasecmp(a, b); }
char *func_003e4960(const char *s, int c) { return strrchr(s, c); }
char *func_003e4920(const char *s, int c) { return strchr(s, c); }
char *func_003e4880(char *s) { for (char *p = s; *p; p++) *p = toupper((unsigned char)*p); return s; }
char *func_003e48d0(char *s) { for (char *p = s; *p; p++) *p = tolower((unsigned char)*p); return s; }

#include <math.h>
float func_00449938(float a, float b) { return powf(a, b); }

/* Movie tasks are implemented by video_game.c using converted AVC/PCM resources. */
