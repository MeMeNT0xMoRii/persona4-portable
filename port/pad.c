/* libpad on sceCtrl: port 0 is a DualShock 2 in analog mode, port 1 is empty. */
#include <pspctrl.h>
#include <pspthreadman.h>
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>

enum { PAD_STABLE = 6, PAD_DISCONNECTED = 0, MODE_ANALOG = 0x73 };

/* PS2 report bytes 2 and 3 are active low; {PSP button, byte, bit}. SELECT + L/R stand for L2/R2, SELECT + the
   right-hand buttons for L3 and R3, which the PSP lacks. */
static const struct { unsigned psp; unsigned char bit; } byte2[] = {
    { PSP_CTRL_START, 0x08 }, { PSP_CTRL_UP, 0x10 }, { PSP_CTRL_RIGHT, 0x20 }, { PSP_CTRL_DOWN, 0x40 }, { PSP_CTRL_LEFT, 0x80 },
};
static const struct { unsigned psp; unsigned char bit; } byte3[] = {
    { PSP_CTRL_TRIANGLE, 0x10 }, { PSP_CTRL_CIRCLE, 0x20 }, { PSP_CTRL_CROSS, 0x40 }, { PSP_CTRL_SQUARE, 0x80 },
};

int scePadGetState(int port, int slot) { return port == 0 ? PAD_STABLE : PAD_DISCONNECTED; }
int scePadSetMainMode(int port, int slot, int mode, int lock) { return 1; }
int scePadInfoAct(int port, int slot, int actuator, int term) { return 0; }
int scePadSetActAlign(int port, int slot, void *data) { return 1; }
int scePadSetActDirect(int port, int slot, void *data) { return 1; }
int scePadGetReqState(int port, int slot) { return 0; }

/* Debug: PSP buttons ORed into every read, poked over PSPLink to drive menus while nobody holds the console. */
volatile unsigned p4_pad_inject;
/* or a press for a number of reads (frames): poke the buttons, then the count */
volatile unsigned p4_pad_press, p4_pad_reads;

/* route.txt next to the EBOOT, timed by the PSP clock (not the host's), so a run reaches the same place every time:
     "seconds hex"      a press held 100 ms (the PAD lines of p4.log, old routes)
     "S seconds hex"    the buttons held from then on (PADS lines)
     "A seconds x y"    the left stick from then on (STICK lines)
   tools/psp_route.py turns a p4.log into a route. */
enum { ROUTE_MAX = 1024 };
static struct RouteStep { unsigned char kind, x, y; unsigned at_ms, buttons; } *route; /* heap: .bss is full */
static int route_count;
enum { ROUTE_TEXT = 16384 };

static void load_route(void)
{
    SceUID fd = sceIoOpen("route.txt", PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    char *route_text = malloc(ROUTE_TEXT);
    route = malloc(ROUTE_MAX * sizeof *route);
    if (!route_text || !route) {
        sceIoClose(fd);
        free(route_text);
        return;
    }
    int n = sceIoRead(fd, route_text, ROUTE_TEXT - 1);
    sceIoClose(fd);
    route_text[n > 0 ? n : 0] = 0;
    for (char *p = route_text; *p && route_count < ROUTE_MAX;) {
        while (*p == ' ' || *p == '\r' || *p == '\n')
            p++;
        char kind = 'P';
        if (*p == 'S' || *p == 'A')
            kind = *p++;
        char *end;
        double at = strtod(p, &end);
        if (end == p)
            break;
        route[route_count].kind = kind;
        route[route_count].at_ms = (unsigned)(at * 1000);
        if (kind == 'A') {
            route[route_count].x = strtoul(end, &p, 10);
            route[route_count].y = strtoul(p, &p, 10);
        } else {
            route[route_count].buttons = strtoul(end, &p, 16);
        }
        route_count++;
    }
    free(route_text);
}

/* ORs the route's buttons into pad and, once the route has moved the stick, sets the stick */
static void route_apply(SceCtrlData *pad)
{
    unsigned now = sceKernelGetSystemTimeLow() / 1000, held = 0;
    int stick = -1;
    for (int i = 0; i < route_count && route[i].at_ms <= now; i++) {
        if (route[i].kind == 'P' && now < route[i].at_ms + 100)
            pad->Buttons |= route[i].buttons;
        else if (route[i].kind == 'S')
            held = route[i].buttons;
        else if (route[i].kind == 'A')
            stick = i;
    }
    pad->Buttons |= held;
    if (stick >= 0) {
        pad->Lx = route[stick].x;
        pad->Ly = route[stick].y;
    }
}

static void pad_start(void)
{
    static int started;
    if (!started) {
        started = 1;
        sceCtrlSetSamplingCycle(0);
        sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
        load_route();
    }
}

/* the route's buttons now, for the movie player (it reads the pad itself): a recorded START that skipped a movie
   skips it again when replayed */
unsigned p4_route_buttons(void)
{
    SceCtrlData pad = { 0 };
    pad_start();
    if (route_count)
        route_apply(&pad);
    return pad.Buttons;
}

int scePadRead(int port, int slot, unsigned char *out)
{
    SceCtrlData pad;
    pad_start();
    memset(out, 0, 32);
    if (port != 0)
        return 0;
    sceCtrlPeekBufferPositive(&pad, 1);
    if (route_count)
        route_apply(&pad);
    /* p4.log: PAD = a press (for reading), PADS = the buttons held after a change, STICK = the left stick in 16 steps */
    {
        static unsigned last, last_x = 0x80, last_y = 0x80;
        unsigned state = pad.Buttons & 0xffff, now = sceKernelGetSystemTimeLow();
        void p4_log(const char *fmt, ...);
        if (state != last) {
            if (state & ~last)
                p4_log("PAD %u.%02u %04x\n", now / 1000000, now / 10000 % 100, state & ~last);
            p4_log("PADS %u.%02u %04x\n", now / 1000000, now / 10000 % 100, state);
            last = state;
        }
        if ((pad.Lx >> 4) != (last_x >> 4) || (pad.Ly >> 4) != (last_y >> 4)) {
            p4_log("STICK %u.%02u %u %u\n", now / 1000000, now / 10000 % 100, pad.Lx, pad.Ly);
            last_x = pad.Lx;
            last_y = pad.Ly;
        }
    }
    pad.Buttons |= p4_pad_inject;
    if (p4_pad_reads) {
        p4_pad_reads--;
        pad.Buttons |= p4_pad_press;
    }
    unsigned char low = 0, high = 0;
    for (unsigned i = 0; i < sizeof byte2 / sizeof *byte2; i++)
        if (pad.Buttons & byte2[i].psp)
            low |= byte2[i].bit;
    for (unsigned i = 0; i < sizeof byte3 / sizeof *byte3; i++)
        if (pad.Buttons & byte3[i].psp)
            high |= byte3[i].bit;
    if (pad.Buttons & PSP_CTRL_SELECT) {
        if (pad.Buttons & PSP_CTRL_LTRIGGER)
            high |= 0x01; /* L2 */
        else if (pad.Buttons & PSP_CTRL_RTRIGGER)
            high |= 0x02; /* R2 */
        else
            low |= 0x01; /* SELECT */
    } else {
        if (pad.Buttons & PSP_CTRL_LTRIGGER)
            high |= 0x04; /* L1 */
        if (pad.Buttons & PSP_CTRL_RTRIGGER)
            high |= 0x08; /* R1 */
    }
    out[1] = MODE_ANALOG;
    out[2] = ~low;
    out[3] = ~high;
    out[4] = out[5] = 0x80; /* right stick */
    out[6] = pad.Lx;
    out[7] = pad.Ly;
    return 1;
}

#define AT(address, name) __typeof__(name) func_##address __attribute__((alias(#name)));
AT(00430018, scePadGetState)
AT(00430578, scePadSetMainMode)
AT(00430210, scePadInfoAct)
AT(004306f8, scePadSetActAlign)
AT(00430630, scePadSetActDirect)
AT(00430180, scePadGetReqState)
AT(0042ffa0, scePadRead)
