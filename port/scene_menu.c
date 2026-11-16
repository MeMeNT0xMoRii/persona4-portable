/* Scene launcher for testing: go to a field (room), a dungeon floor, an event or a battle without playing there.

   Two ways in, the same requests:
   - PSPLink: tools/psp_scene.py writes p4_scene_req (arguments first, the kind word last).
   - On the PSP: hold L + R + START. The game pauses under a menu (numbers, presets from scenes.txt next to the
     EBOOT, "save this place"); the frame on screen stays, the menu is drawn into it with the CPU.

   Every request repeats one of the game's own script commands (src/Kosaka/k_command_inferred_00175f70.c in the decomp):
     field  K_Cmd_CALL_FIELD      (0x176450): sequence 9, a 0x1C record {field, room, entry, 0, direction}
     floor  K_Cmd_CALL_DNG_FLOOR  (0x176770): sequence 0xB, {step, entry}, the field scene unloaded first
     event  K_Cmd_CALL_EVENT      (0x1765C0): sequence 0xA, words 3..5 = event id, sub id, value
     battle btlScriptStartEncounter (0x1932F0): sequence 6 with the 0x3C setup built from the current party
     day    the game's leftover debug date picker (calendarInputUpdate, 0x121310): set the date and time slot, then
            sequence 0x1A (calendarOpenFlagSet: calendarStepTimeSlot applies the date and cldSchedulerStart runs the
            day's script, "<month><day>_<AM_A..PM_D>" from the month's file). Date index 0 = April 1, 0x16C = March 31.
   They run from scePadRead (sdkPadUpdateAll, before kwlnTaskRunFrame): between task frames, never inside a task.
   A request waits until the player stands idle in a field or floor (root state 13, no event, menu or pending
   sequence, up to 60 s, then it is refused; a newer request replaces one still waiting); a day asked for on a
   dungeon floor first leaves it through field 7 room 2; an event or battle started from a field returns to that field (entry 0) when its sequence ends,
   reviving knocked-out party members to 1 HP like btlScriptResolvePartyDeath. Story flags are not touched: a field
   shows the day of the loaded save. The same gp/data anchors as Izanagi's src/platform/mods/game_services.c. */
/* Not a hot path: small code, the fixed .p4image above .bss leaves little room (b295 overlapped it by 348 bytes) */
#pragma GCC optimize("Os")
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R32(a) (*(volatile unsigned *)((a) + P4_K))
#define G32(off) (*(volatile unsigned *)(P4_GP + (off)))
#include "paths.h" /* b321: files in the folder the game was launched from */
#define SCENES_FILE p4_path("scenes.txt")
#define HISTORY_FILE p4_path("scene_history.txt") /* the last HISTORY_KEEP requests and their results, kept over reboots */
#define DAYS_FILE p4_path("days.txt")             /* tools/psp_days.py: what each day's story script starts */

void p4_log(const char *fmt, ...);
extern unsigned char msx[]; /* libpspdebug's 8x8 font, 8 bytes a character, MSB left */
extern volatile unsigned p4_pad_press, p4_pad_reads;

/* the game */
void sequenceRequest(int sequence, void *parameters, int size, int delay);
int mtSceneResListPrepareAll(void);
void sdlSchedulerEnable(void);
int kwlnGetCurrentWorkValue(void);
void fieldSceneUnload(void);
void hFadeTaskStart(short variant);
int datGetPartyId(int slot);
void *datUnitCreatePc(unsigned short pc);
int datUnitCreateEncounterGroup(int encounter);
unsigned datCalcIsDeadById(short unit);
void datSetUnitHp(short unit, short hp);
void datCalcClearBadStatusById(short unit, int mask);
short datGetDateIndex(void);
unsigned char datGetTimeSlot(void);
void datSetDateIndex(short date);
void datSetTimeSlot(unsigned char slot);
void datSetPendingDateIndex(short date);
void datSetPendingTimeSlot(unsigned char slot);
void datSetPendingFlag(unsigned flags);
int fadeBeginTransition(int mode, int duration);

enum { SEQ_NONE = -1, SEQ_BATTLE = 6, SEQ_FIELD = 9, SEQ_EVENT = 0xA, SEQ_FLOOR = 0xB, SEQ_DAY = 0x1A };
enum { TASK_WORK = 0x38, TASK_NEXT = 0x3C };
/* gp-relative anchors (iGpffffXXXX = gp + (s16)XXXX) and fixed data */
#define SEQUENCE_TASK G32(0xb1a8 - 0x10000) /* sequence.c: work {flags (1 = pending), current, previous, ...} */
#define CAMP_WORK G32(0xb1ec - 0x10000)     /* cmpMain work, 0 when Camp is closed */
#define EVENT_FLAG G32(0xb514 - 0x10000)    /* 0x764604: a message window or event holds the input */
#define ENCOUNTERS G32(0xb414 - 0x10000)    /* 0x18-byte encounter records */
#define BATTLE_CONTEXT G32(0xb3ac - 0x10000) /* 0x76449C: the battle state while a battle runs (btlTaskCreate) */
#define SCENE_STATE G32(0x9db0 - 0x10000)   /* FieldSceneState: field, room, resources, flags */
#define FIELD_ROOT R32(0x007D2510)          /* field root task (k_field.c FldRootCreate) */
#define STEP_TASK R32(0x00764334)           /* dungeon step task (K_SeqFldStepStart): work +4 step, +8 entry */
static const int task_lists[3] = { 0xb9ec - 0x10000, 0xb9f8 - 0x10000, 0xba04 - 0x10000 };

enum { K_NONE, K_FIELD, K_FLOOR, K_EVENT, K_BATTLE, K_MENU, K_DAY };
static const char *const kind_names[] = { "none", "field", "floor", "event", "battle", "menu", "day" };
static const char *const slot_names[6] = { "AM_A", "AM_B", "PM_A", "PM_B", "PM_C", "PM_D" };

/* from April: the game's date index 0 is April 1 (calSplitDayOfYear, month lengths at 0x5E45E0) */
static const int month_days[12] = { 30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 28, 31 };

static int date_index(int month, int day)
{
    int order = month >= 4 ? month - 4 : month + 8, index = 0;
    for (int m = 0; m < order; m++)
        index += month_days[m];
    return index + (day < 1 ? 0 : day > month_days[order] ? month_days[order] - 1 : day - 1);
}

static void date_split(int index, int *month, int *day)
{
    int m = 0;
    index = index < 0 ? 0 : index % 365;
    while (index >= month_days[m])
        index -= month_days[m++];
    *month = m < 9 ? m + 4 : m - 8;
    *day = index + 1;
}

/* PSPLink: write [1..4] first, then [0] = kind (K_FIELD f r entry direction, K_FLOOR step entry, K_EVENT id sub
   value, K_BATTLE encounter, K_MENU opens the menu, K_DAY date index (0 = April 1) and time slot 0..5). The PSP clears [0] when it takes the request. */
volatile int p4_scene_req[5];
/* result of the last request, readable with peekw: 0 idle, 1 busy, 2 done, 3 refused */
volatile int p4_scene_result;

static unsigned word_at(unsigned address, int offset)
{
    return address ? *(volatile unsigned *)(address + offset) : 0;
}

static int sequence_current(int *pending)
{
    unsigned work = word_at(SEQUENCE_TASK, TASK_WORK);
    *pending = work ? (word_at(work, 0) & 1) != 0 : 0;
    return work ? (int)word_at(work, 4) : SEQ_NONE;
}

/* Field root state (13 = idle) and its field/room, or -1 while the scene is changing. */
static int root_state(int *field, int *room)
{
    unsigned work = word_at(FIELD_ROOT, TASK_WORK), scene = SCENE_STATE;
    if (!work || !scene)
        return -1;
    unsigned ids = word_at(work, 0x18);
    /* An event root (sequence 10: what a day's script runs in, and the player walks the town in it, b293) keeps 0
       for its own ids: the loaded scene's ids stand for them there. */
    if (!ids && (word_at(scene, 0xc) & 1))
        ids = (word_at(scene, 0) & 0xffff) | (word_at(scene, 4) & 0xffff) << 16;
    if ((word_at(scene, 0xc) & 6) || (word_at(scene, 0) & 0xffff) != (ids & 0xffff) ||
        (word_at(scene, 4) & 0xffff) != (ids >> 16))
        return -1;
    *field = ids & 0xffff;
    *room = ids >> 16;
    return (int)word_at(work, 0);
}

static int task_named(const char *name)
{
    size_t length = strlen(name) + 1;
    for (int l = 0; l < 3; l++) {
        unsigned node = G32(task_lists[l]);
        for (int i = 0; node && i < 512; i++) {
            if (!memcmp((const void *)node, name, length))
                return 1;
            node = word_at(node, TASK_NEXT);
        }
    }
    return 0;
}

typedef struct {
    int sequence, pending, root, field, room, step, date, slot;
} Where;

static void where_now(Where *w)
{
    w->sequence = sequence_current(&w->pending);
    w->field = w->room = 0;
    w->root = root_state(&w->field, &w->room);
    /* while the scene changes: the loaded scene's ids, for the log and the menu header */
    unsigned scene = SCENE_STATE;
    if (w->root < 0 && scene && (word_at(scene, 0xc) & 1)) {
        w->field = word_at(scene, 0) & 0xffff;
        w->room = word_at(scene, 4) & 0xffff;
    }
    unsigned step_work = word_at(STEP_TASK, TASK_WORK);
    w->step = w->sequence == SEQ_FLOOR && step_work ? (int)word_at(step_work, 4) : -1;
    w->date = datGetDateIndex();
    w->slot = datGetTimeSlot();
}

/* NULL when the player stands idle in a field or floor, else why not */
static const char *not_ready(const Where *w)
{
    static char why[48];
    /* 9 a field, 0xB a dungeon floor, 10 a field under an event root: after a day start the town is walked in
       sequence 10 (b293: everything was refused there as "not in a field") */
    if (w->sequence != SEQ_FIELD && w->sequence != SEQ_FLOOR && w->sequence != SEQ_EVENT) {
        snprintf(why, sizeof why, "not in a field (sequence %d)", w->sequence);
        return why;
    }
    if (w->pending)
        return "a scene change is pending";
    /* a dungeon battle runs inside the floor's sequence: it held the root out of state 13 and read as "loading
       or running a script" (b295) */
    if (BATTLE_CONTEXT)
        return "in a battle: the request runs when it ends";
    if (w->root != 13)
        return "the field is loading or running a script";
    if (EVENT_FLAG)
        return "an event or message holds the input";
    if (CAMP_WORK)
        return "a menu is open";
    if (!task_named("rotate pc"))
        return "the player is not in control";
    return NULL;
}

/* ---- requests */

enum { R_IDLE, R_WAIT, R_LOADING, R_AWAY, R_RETURN };
static struct {
    int kind, state, a, b, c, d, timer, seen, via, back_field, back_room;
} rq;
static char last_result[96] = "";
static void history_add(const char *result);

static void finish(int refused, const char *why)
{
    history_add(why);
    snprintf(last_result, sizeof last_result, "%s %s", kind_names[rq.kind], why);
    p4_log("SCENE %s %d %d %d: %s\n", kind_names[rq.kind], rq.a, rq.b, rq.c, why);
    p4_scene_result = refused ? 3 : 2;
    rq.state = R_IDLE;
}

static void submit(int kind, int a, int b, int c, int d)
{
    if (rq.state == R_WAIT) { /* not started yet: the newer choice wins */
        p4_log("SCENE %s %d %d replaces the waiting %s %d %d\n", kind_names[kind], a, b, kind_names[rq.kind], rq.a, rq.b);
    } else if (rq.state != R_IDLE) {
        p4_log("SCENE %s refused: another request is running\n", kind_names[kind]);
        snprintf(last_result, sizeof last_result, "%s refused: %s is still running", kind_names[kind], kind_names[rq.kind]);
        return;
    }
    memset(&rq, 0, sizeof rq);
    rq.kind = kind;
    rq.a = a, rq.b = b, rq.c = c, rq.d = d;
    if (kind == K_FIELD && (a <= 0 || a > 999 || b < 0 || b > 999)) {
        finish(1, "refused: field or room out of range");
        return;
    }
    if (kind == K_DAY && (a < 0 || a > 0x16C || b < 0 || b > 5)) {
        finish(1, "refused: date index 0..364, time slot 0..5");
        return;
    }
    if (kind == K_BATTLE) {
        const unsigned char *record = (const unsigned char *)ENCOUNTERS + a * 0x18;
        int populated = 0, bad = !ENCOUNTERS || a < 0 || a > 1023;
        for (int i = 0; !bad && i < 5; i++) {
            unsigned enemy = record[8 + 2 * i] | record[9 + 2 * i] << 8;
            bad |= enemy >= 0x150;
            populated |= enemy != 0;
        }
        if (bad || !populated) {
            finish(1, "refused: not a valid encounter");
            return;
        }
    }
    rq.state = R_WAIT;
    p4_scene_result = 1;
    snprintf(last_result, sizeof last_result, "%s %d %d: waiting", kind_names[kind], a, b);
    p4_log("SCENE request %s %d %d %d %d\n", kind_names[kind], a, b, c, d);
}

static void request_field(int field, int room, int entry, int direction)
{
    short words[14];
    memset(words, 0, sizeof words);
    words[0] = field, words[1] = room, words[2] = entry, words[4] = direction;
    sdlSchedulerEnable();
    sequenceRequest(SEQ_FIELD, words, 0x1C, 2);
}

static void start_battle(int encounter)
{
    /* btlScriptStartEncounter with the id from us instead of the script */
    unsigned setup[15];
    const unsigned char *record = (const unsigned char *)ENCOUNTERS + encounter * 0x18;
    memset(setup, 0, sizeof setup);
    hFadeTaskStart(1);
    setup[1] = (unsigned)datUnitCreatePc(1);
    int count = 1;
    for (int slot = 0; slot < 4; slot++) {
        int pc = datGetPartyId(slot) & 0xffff;
        if (pc && count < 4)
            setup[1 + count++] = (unsigned)datUnitCreatePc(pc);
    }
    setup[5] = (unsigned)datUnitCreateEncounterGroup(encounter);
    unsigned short formation = record[0x12] | record[0x13] << 8, variant = record[0x14] | record[0x15] << 8;
    if (!formation && !variant)
        formation = 0xF0, variant = 1;
    ((unsigned short *)setup)[0x10] = formation;
    ((unsigned short *)setup)[0x11] = variant;
    sequenceRequest(SEQ_BATTLE, setup, 0x3C, 4);
}

static void revive_party(void)
{
    short units[5] = { 1 };
    int count = 1;
    for (int slot = 0; slot < 4; slot++) {
        int id = datGetPartyId(slot) & 0xffff;
        if (id > 1 && id <= 10)
            units[count++] = id;
    }
    for (int i = 0; i < count; i++)
        if (datCalcIsDeadById(units[i])) {
            datSetUnitHp(units[i], 1);
            datCalcClearBadStatusById(units[i], 0x80000);
        }
}

static void run_request(void)
{
    Where w;
    where_now(&w);
    rq.timer++;
    switch (rq.state) {
    case R_WAIT: {
        const char *why = not_ready(&w);
        if (!why && rq.kind == K_DAY && w.sequence == SEQ_FLOOR && !rq.via && mtSceneResListPrepareAll()) {
            /* A day from a dungeon floor: out through a town field first (Dojima's house, 7/2, where most day
               scripts end), the way the game's own scripts leave a floor, then the day from there. b292 started the
               day straight from Heaven and the town after it looked wrong. */
            rq.via = 1;
            rq.back_field = 7, rq.back_room = 2;
            rq.timer = 0;
            request_field(7, 2, 0, 0);
            rq.state = R_LOADING;
            snprintf(last_result, sizeof last_result, "day: leaving the floor through field 7 room 2");
            p4_log("SCENE day %d %d: leaving the floor through field 7 room 2 first\n", rq.a, rq.b);
            return;
        }
        if (!why && (rq.kind == K_EVENT || rq.kind == K_BATTLE) && w.sequence == SEQ_FLOOR)
            why = "events and battles start from a field, not a dungeon floor";
        if (why) {
            if (rq.timer == 1) {
                p4_log("SCENE %s waiting (up to 60 s): %s\n", kind_names[rq.kind], why);
                snprintf(last_result, sizeof last_result, "%s waiting: %s", kind_names[rq.kind], why);
            }
            if (BATTLE_CONTEXT) /* a battle can take minutes: wait for its end, not 60 s */
                rq.timer = 2;
            if (rq.timer > 1800) {
                char text[96];
                snprintf(text, sizeof text, "refused: %s", why);
                finish(1, text);
            }
            return;
        }
        if (!mtSceneResListPrepareAll()) {
            if (rq.timer > 900)
                finish(1, "refused: scene resources stayed busy");
            return;
        }
        rq.back_field = w.field, rq.back_room = w.room;
        rq.timer = 0;
        if (rq.kind == K_FIELD) {
            request_field(rq.a, rq.b, rq.c, rq.d);
            rq.state = R_LOADING;
        } else if (rq.kind == K_FLOOR) {
            int parameters[2] = { rq.a, rq.b };
            if (kwlnGetCurrentWorkValue() != SEQ_FLOOR)
                fieldSceneUnload();
            sdlSchedulerEnable();
            sequenceRequest(SEQ_FLOOR, parameters, 8, 4);
            rq.state = R_LOADING;
        } else if (rq.kind == K_DAY) {
            /* the debug date picker's confirm, with the pending date set too: calendarStepTimeSlot applies a
               pending date over the current one */
            datSetDateIndex(rq.a);
            datSetTimeSlot(rq.b);
            datSetPendingDateIndex(rq.a);
            datSetPendingTimeSlot(rq.b);
            datSetPendingFlag(1);
            fadeBeginTransition(1, 1);
            sequenceRequest(SEQ_DAY, NULL, 0, 2);
            rq.state = R_AWAY;
        } else if (rq.kind == K_EVENT) {
            int parameters[7] = { 0 };
            parameters[3] = rq.a, parameters[4] = rq.b, parameters[5] = rq.c;
            sequenceRequest(SEQ_EVENT, parameters, 0x1C, 2);
            rq.state = R_AWAY;
        } else {
            start_battle(rq.a);
            rq.state = R_AWAY;
        }
        snprintf(last_result, sizeof last_result, "%s %d %d: started", kind_names[rq.kind], rq.a, rq.b);
        p4_log("SCENE started %s %d %d %d from field %d room %d\n", kind_names[rq.kind], rq.a, rq.b, rq.c,
               rq.back_field, rq.back_room);
        return;
    }
    case R_LOADING:
        if (rq.kind == K_FLOOR ? w.sequence == SEQ_FLOOR && !w.pending && w.root == 13
                               : w.sequence == SEQ_FIELD && w.root >= 12 && w.root <= 14 &&
                                     w.field == (rq.kind == K_FIELD ? rq.a : rq.back_field) &&
                                     w.room == (rq.kind == K_FIELD ? rq.b : rq.back_room)) {
            if (rq.kind == K_DAY && rq.via == 1) { /* out of the floor: now the day */
                rq.via = 2;
                rq.state = R_WAIT;
                rq.timer = 0;
                p4_log("SCENE day %d %d: in field 7 room 2, starting the day\n", rq.a, rq.b);
                return;
            }
            char text[64];
            snprintf(text, sizeof text, "%s field %d room %d step %d", rq.kind == K_FIELD || rq.kind == K_FLOOR ?
                     "arrived" : "returned", w.field, w.room, w.step);
            finish(0, text);
        } else if (rq.timer > 5400) {
            finish(0, "the scene did not report ready in 3 minutes");
        }
        return;
    case R_AWAY: {
        int expected = rq.kind == K_EVENT ? SEQ_EVENT : rq.kind == K_DAY ? SEQ_DAY : SEQ_BATTLE;
        if (w.sequence == expected) {
            rq.seen = 1;
            return;
        }
        if (rq.kind == K_DAY && rq.seen) { /* the day's script chooses the scenes from here */
            char text[64];
            int month, day;
            date_split(w.date, &month, &day);
            snprintf(text, sizeof text, "started %d/%d %s, sequence %d", month, day, slot_names[w.slot % 6], w.sequence);
            finish(0, text);
            return;
        }
        if (!rq.seen) {
            if (rq.timer > 600)
                finish(0, "the sequence did not start");
            return;
        }
        if (w.pending || w.sequence != SEQ_NONE) {
            finish(0, "done, the game chose the next scene");
            return;
        }
        if (rq.kind == K_BATTLE)
            revive_party();
        rq.state = R_RETURN;
        rq.timer = 0;
        return;
    }
    case R_RETURN:
        if (!mtSceneResListPrepareAll()) {
            if (rq.timer > 900)
                finish(0, "ended; resources stayed busy, no return");
            return;
        }
        request_field(rq.back_field, rq.back_room, 0, 0);
        rq.state = R_LOADING;
        rq.timer = 0;
        return;
    }
}

/* SCENE lines whenever the place changes: how the ids of a scene are found */
static void observe(void)
{
    static Where last = { -2, 0, 0, 0, 0, 0, 0, 0 };
    Where w;
    where_now(&w);
    int ready = w.root == 13 && !w.pending;
    if (w.sequence != last.sequence || (ready && (w.field != last.field || w.room != last.room || w.step != last.step))) {
        int month, day;
        date_split(w.date, &month, &day);
        p4_log("SCENE now sequence %d field %d room %d step %d root %d date %d (%d/%d %s)\n", w.sequence, w.field,
               w.room, w.step, w.root, w.date, month, day, slot_names[w.slot % 6]);
        last = w;
    }
}

/* ---- presets: scenes.txt, one per line, '#' comments
     field FIELD ROOM ENTRY name
     floor STEP ENTRY name        (ENTRY 65535: the floor's default)
     event ID SUB name
     battle ENCOUNTER name
     day MONTH DAY SLOT name      (SLOT 0..5: AM_A AM_B PM_A PM_B PM_C PM_D) */
enum { PRESET_MAX = 96, PRESET_NAME = 40 };
typedef struct {
    int kind, a, b, c;
    char name[PRESET_NAME];
} Preset;
static Preset *presets, *recent;
static int preset_count, recent_count;

/* the whole file, NUL-terminated (free it), or NULL */
static char *read_file(const char *path, int max, int *length)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;
    char *text = malloc(max);
    int n = text ? sceIoRead(fd, text, max - 1) : 0;
    sceIoClose(fd);
    if (text)
        text[n > 0 ? n : 0] = 0;
    if (length)
        *length = n > 0 ? n : 0;
    return text;
}

/* one scenes.txt line; the history uses the same form, its date and result as the name */
static int parse_line(const char *line, Preset *p)
{
    char word[8];
    int a = 0, b = 0, c = 0, used = 0, kind = K_NONE;
    if (sscanf(line, "%7s", word) != 1 || word[0] == '#')
        return 0;
    if (!strcmp(word, "field") && sscanf(line, "%*s %d %d %d %n", &a, &b, &c, &used) >= 3)
        kind = K_FIELD;
    else if (!strcmp(word, "floor") && sscanf(line, "%*s %d %d %n", &a, &b, &used) >= 2)
        kind = K_FLOOR;
    else if (!strcmp(word, "event") && sscanf(line, "%*s %d %d %n", &a, &b, &used) >= 2)
        kind = K_EVENT;
    else if (!strcmp(word, "battle") && sscanf(line, "%*s %d %n", &a, &used) >= 1)
        kind = K_BATTLE;
    else if (!strcmp(word, "day") && sscanf(line, "%*s %d %d %d %n", &a, &b, &c, &used) >= 3)
        kind = K_DAY, a = date_index(a, b), b = c, c = 0;
    if (kind == K_NONE)
        return 0;
    p->kind = kind, p->a = a, p->b = b, p->c = c;
    snprintf(p->name, PRESET_NAME, "%s", used ? line + used : "");
    if (!p->name[0])
        snprintf(p->name, PRESET_NAME, "%s %d %d", word, a, b);
    return 1;
}

static void load_presets(void)
{
    preset_count = 0;
    if (!presets && !(presets = malloc(PRESET_MAX * sizeof *presets)))
        return;
    char *text = read_file(SCENES_FILE, 8192, NULL);
    if (!text)
        return;
    for (char *line = strtok(text, "\r\n"); line && preset_count < PRESET_MAX; line = strtok(NULL, "\r\n"))
        preset_count += parse_line(line, &presets[preset_count]);
    free(text);
}

/* scene_history.txt: "<scenes.txt form> | <date> from field F room R | <result>", oldest first, the last
   HISTORY_KEEP kept. The menu lists the newest RECENT_SHOWN; tools/psp_scene.py history reads it back. */
enum { HISTORY_KEEP = 30, RECENT_SHOWN = 8, HISTORY_BYTES = 16384 };

static void history_add(const char *result)
{
    Where w;
    where_now(&w);
    int month, day, rmonth, rday;
    date_split(w.date, &month, &day);
    char line[192], what[48];
    if (rq.kind == K_DAY) {
        date_split(rq.a, &rmonth, &rday);
        snprintf(what, sizeof what, "day %d %d %d", rmonth, rday, rq.b);
    } else if (rq.kind == K_FIELD) {
        snprintf(what, sizeof what, "field %d %d %d", rq.a, rq.b, rq.c);
    } else if (rq.kind == K_BATTLE) {
        snprintf(what, sizeof what, "battle %d", rq.a);
    } else {
        snprintf(what, sizeof what, "%s %d %d", kind_names[rq.kind], rq.a, rq.b);
    }
    snprintf(line, sizeof line, "%s | %d/%d %s from field %d room %d | %s\n", what, month, day,
             slot_names[w.slot % 6], rq.back_field, rq.back_room, result);
    int length = 0;
    char *old = read_file(HISTORY_FILE, HISTORY_BYTES, &length);
    const char *keep = old ? old : "";
    if (old) { /* drop the oldest lines beyond HISTORY_KEEP - 1 */
        int lines = 0;
        for (const char *q = old; *q; q++)
            lines += *q == '\n';
        while (lines-- >= HISTORY_KEEP && strchr(keep, '\n'))
            keep = strchr(keep, '\n') + 1;
    }
    SceUID fd = sceIoOpen(HISTORY_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, keep, strlen(keep));
        sceIoWrite(fd, line, strlen(line));
        sceIoClose(fd);
    }
    free(old);
}

static void load_recent(void)
{
    recent_count = 0;
    if (!recent && !(recent = malloc(RECENT_SHOWN * sizeof *recent)))
        return;
    char *text = read_file(HISTORY_FILE, HISTORY_BYTES, NULL);
    if (!text)
        return;
    char *lines[HISTORY_KEEP + 8];
    int count = 0;
    for (char *line = strtok(text, "\r\n"); line && count < HISTORY_KEEP + 8; line = strtok(NULL, "\r\n"))
        lines[count++] = line;
    for (int i = count - 1; i >= 0 && recent_count < RECENT_SHOWN; i--) /* newest first */
        recent_count += parse_line(lines[i], &recent[recent_count]);
    free(text);
}

static int save_here(char *out, int size)
{
    Where w;
    where_now(&w);
    char line[96];
    if (w.sequence == SEQ_FLOOR && w.step >= 0)
        snprintf(line, sizeof line, "floor %d 65535 floor step %d (field %d room %d)\n", w.step, w.step, w.field, w.room);
    else if ((w.sequence == SEQ_FIELD || w.sequence == SEQ_EVENT) && w.root >= 0)
        snprintf(line, sizeof line, "field %d %d 0 f%03d_%03d\n", w.field, w.room, w.field, w.room);
    else
        return snprintf(out, size, "not in a field or floor"), 0;
    SceUID fd = sceIoOpen(SCENES_FILE, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0)
        return snprintf(out, size, "cannot open scenes.txt"), 0;
    sceIoWrite(fd, line, strlen(line));
    sceIoClose(fd);
    p4_log("SCENE saved: %s", line);
    snprintf(out, size, "saved: %.*s", (int)strcspn(line, "\n"), line);
    return 1;
}

/* ---- the menu: drawn with the CPU into the frame on screen */

static unsigned *screen;
static int screen_width;

static int screen_get(void)
{
    void *top = 0;
    int width = 0, format = -1;
    sceDisplayGetFrameBuf(&top, &width, &format, PSP_DISPLAY_SETBUF_IMMEDIATE);
    if (!top || format != PSP_DISPLAY_PIXEL_FORMAT_8888 || width < 480)
        return 0;
    screen = (unsigned *)((unsigned)top | 0x40000000); /* uncached: the display reads RAM */
    screen_width = width;
    return 1;
}

static void fill(int x, int y, int w, int h, unsigned color)
{
    for (int j = y; j < y + h && j < 272; j++)
        for (int i = x; i < x + w && i < 480; i++)
            screen[j * screen_width + i] = color;
}

static void text(int x, int y, unsigned color, const char *s)
{
    for (; *s && x <= 480 - 8; s++, x += 8) {
        const unsigned char *glyph = &msx[(unsigned char)*s * 8];
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++)
                if (glyph[j] & (0x80 >> i))
                    screen[(y + j) * screen_width + x + i] = color;
    }
}

/* The menu has pages, each a list of rows: Main (Calendar, Manual, Save, Recent, Presets), Calendar (one date at a
   time, its time slots with what they start), Manual (numbers). X or START chooses, O goes back (closes on Main).
   The pages are redrawn only when something changed, band by band (b294 cleared the panel and redrew it every
   vblank into the frame on screen: the top flickered and its help line could not be read). */

static unsigned buttons_now(void)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    unsigned b = pad.Buttons;
    if (p4_pad_reads) { /* psp_press.py can drive the menu too */
        p4_pad_reads--;
        b |= p4_pad_press;
    }
    return b;
}

/* newly pressed buttons, with auto-repeat for the d-pad and the triggers */
static unsigned input(unsigned *held, unsigned *repeat)
{
    const unsigned keys = PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_LTRIGGER |
                          PSP_CTRL_RTRIGGER;
    unsigned b = buttons_now(), pressed = b & ~*held;
    if (b & *held & keys) {
        if (++*repeat > 15 && !(*repeat & 3))
            pressed |= b & keys;
    } else {
        *repeat = 0;
    }
    *held = b;
    return pressed;
}

/* ---- days.txt, parsed once */
typedef struct {
    short date, a, b, c;
    unsigned char slot, kind;
} DayItem;
static DayItem *day_items;
static int day_item_count;

static int next_number(const char **q)
{
    while (**q == ' ' || **q == '\t')
        ++*q;
    return (int)strtol(*q, (char **)q, 10);
}

static void load_days(void)
{
    if (day_items)
        return;
    int length = 0;
    char *text_buffer = read_file(DAYS_FILE, 48 * 1024, &length);
    if (!text_buffer)
        return;
    int lines = 1;
    for (const char *q = text_buffer; *q; q++)
        lines += *q == '\n';
    day_items = malloc(lines * sizeof *day_items);
    for (const char *q = text_buffer; day_items && q && *q; q = strchr(q, '\n') ? strchr(q, '\n') + 1 : NULL) {
        if (*q < '0' || *q > '9')
            continue;
        int month = next_number(&q), day = next_number(&q), slot = next_number(&q);
        while (*q == ' ')
            q++;
        int kind = !strncmp(q, "field", 5) ? K_FIELD : !strncmp(q, "floor", 5) ? K_FLOOR :
                   !strncmp(q, "event", 5) ? K_EVENT : !strncmp(q, "battle", 6) ? K_BATTLE : K_NONE;
        if (kind == K_NONE || month < 1 || month > 12 || slot < 0 || slot > 5)
            continue;
        while (*q && *q != ' ')
            q++;
        DayItem *item = &day_items[day_item_count++];
        item->date = date_index(month, day), item->slot = slot, item->kind = kind;
        item->a = next_number(&q), item->b = next_number(&q), item->c = next_number(&q);
    }
    free(text_buffer);
    p4_log("SCENE days.txt: %d items\n", day_item_count);
}

static int day_has_events(int date)
{
    for (int i = 0; i < day_item_count; i++)
        if (day_items[i].date == date && day_items[i].kind == K_EVENT)
            return 1;
    return 0;
}

/* ---- rows and pages */
enum { PAGE_MAIN, PAGE_CALENDAR, PAGE_MANUAL };
enum { A_HEADER = -1, A_CALENDAR = -2, A_MANUAL = -3, A_SAVE = -4, A_RELOAD = -5, A_VALUE = -6, A_MAN_FIELD = -7,
       A_MAN_FLOOR = -8, A_MAN_EVENT = -9, A_MAN_BATTLE = -10, A_MAN_DAY = -11 };
/* action: a K_* request with a, b, c, or one of A_* */
typedef struct {
    char label[64];
    int action, a, b, c, *value, low, high;
} Row;
enum { ROWS_MAX = 128 };
static Row *rows;
static int row_count;

static Row *add_row(int action, const char *label)
{
    if (row_count >= ROWS_MAX)
        return NULL;
    Row *r = &rows[row_count++];
    memset(r, 0, sizeof *r);
    r->action = action;
    snprintf(r->label, sizeof r->label, "%s", label);
    return r;
}

static void add_request(int kind, int a, int b, int c, const char *label)
{
    Row *r = add_row(kind, label);
    if (r)
        r->a = a, r->b = b, r->c = c;
}

static void add_value(const char *label, int *value, int low, int high)
{
    Row *r = add_row(A_VALUE, label);
    if (r)
        r->value = value, r->low = low, r->high = high;
}

static const char *const month_names[13] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
                                             "Oct", "Nov", "Dec" };
static const char *const weekday_names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
/* April 1 2011, the game's day 0, was a Friday */
static const char *weekday(int date) { return weekday_names[(date + 5) % 7]; }

static void item_label(char *out, int size, int kind, int a, int b, int c)
{
    if (kind == K_FIELD)
        snprintf(out, size, "  field  %d/%d  entry %d", a, b, c);
    else if (kind == K_FLOOR)
        snprintf(out, size, "  floor  step %d", a);
    else if (kind == K_EVENT)
        snprintf(out, size, "  event  E%03d_%03d", a, b);
    else
        snprintf(out, size, "  battle %d", a);
}

static int manual[12] = { 1, 1, 0, 0, 100, 1, 1, 4, 11, 0 };
enum { M_FIELD, M_ROOM, M_ENTRY, M_STEP, M_EVENT, M_SUB, M_ENCOUNTER, M_MONTH, M_DAY, M_SLOT };
static int calendar_date;

static void build_page(int page)
{
    char label[64];
    row_count = 0;
    if (page == PAGE_MAIN) {
        add_row(A_CALENDAR, "Calendar: choose a day, see and start its events  >");
        add_row(A_MANUAL, "Manual: field / floor / event / battle / day by number  >");
        add_row(A_SAVE, "Save this place as a preset");
        add_row(A_HEADER, recent_count ? "-- Recent (newest first)" : "-- Recent: none yet");
        for (int i = 0; i < recent_count; i++)
            add_request(recent[i].kind, recent[i].a, recent[i].b, recent[i].c, recent[i].name);
        add_row(A_HEADER, preset_count ? "-- Presets (scenes.txt)" : "-- Presets: scenes.txt is empty");
        for (int i = 0; i < preset_count; i++)
            add_request(presets[i].kind, presets[i].a, presets[i].b, presets[i].c, presets[i].name);
        add_row(A_RELOAD, "Reload scenes.txt");
    } else if (page == PAGE_CALENDAR) {
        int month, day;
        date_split(calendar_date, &month, &day);
        for (int slot = 0; slot < 6; slot++) {
            snprintf(label, sizeof label, "%s: start %s %d here", slot_names[slot], month_names[month], day);
            add_request(K_DAY, calendar_date, slot, 0, label);
            for (int i = 0; i < day_item_count; i++) {
                const DayItem *item = &day_items[i];
                if (item->date != calendar_date || item->slot != slot)
                    continue;
                item_label(label, sizeof label, item->kind, item->a, item->b, item->c);
                add_request(item->kind, item->a, item->b, item->c, label);
            }
        }
    } else {
        add_row(A_HEADER, "-- Field");
        add_value("Field", &manual[M_FIELD], 0, 999);
        add_value("Room", &manual[M_ROOM], 0, 999);
        add_value("Entry", &manual[M_ENTRY], 0, 99);
        add_row(A_MAN_FIELD, "Go to field");
        add_row(A_HEADER, "-- Dungeon floor");
        add_value("Step", &manual[M_STEP], 0, 9999);
        add_row(A_MAN_FLOOR, "Go to floor");
        add_row(A_HEADER, "-- Event");
        add_value("Event id", &manual[M_EVENT], 0, 999);
        add_value("Event sub", &manual[M_SUB], 0, 999);
        add_row(A_MAN_EVENT, "Play event");
        add_row(A_HEADER, "-- Battle");
        add_value("Encounter", &manual[M_ENCOUNTER], 0, 1023);
        add_row(A_MAN_BATTLE, "Start battle");
        add_row(A_HEADER, "-- Day");
        add_value("Month", &manual[M_MONTH], 1, 12);
        add_value("Day", &manual[M_DAY], 1, 31);
        add_value("Time slot", &manual[M_SLOT], 0, 5);
        add_row(A_MAN_DAY, "Start this day");
    }
}

static int selectable(int row) { return row >= 0 && row < row_count && rows[row].action != A_HEADER; }

static int step_cursor(int cursor, int direction)
{
    for (int tries = 0; tries < row_count; tries++) {
        cursor = (cursor + direction + row_count) % row_count;
        if (selectable(cursor))
            break;
    }
    return cursor;
}

/* one 10-pixel band of the panel: its background, then its text (no frame shows the band empty for long) */
enum { PANEL_X = 8, PANEL_W = 464, LINE = 10, BANDS = 27, TOP = 1 };
static void band(int index, unsigned background, unsigned color, const char *s)
{
    int y = TOP + index * LINE;
    fill(PANEL_X, y, PANEL_W, LINE, background);
    if (s)
        text(PANEL_X + 8, y + 1, color, s);
}

static void draw_page(int page, int cursor, const Where *w, const char *status)
{
    const unsigned BACK = 0xff281810, HEAD = 0xff503018, SEL = 0xff805020, FOOT = 0xff383838;
    const int VISIBLE = BANDS - 5;
    char line[80];
    int month, day;
    if (page == PAGE_CALENDAR) {
        date_split(calendar_date, &month, &day);
        snprintf(line, sizeof line, "Calendar   <  %s %s %d  >   %s", weekday(calendar_date), month_names[month], day,
                 day_has_events(calendar_date) ? "story events" : "");
    } else {
        date_split(w->date, &month, &day);
        snprintf(line, sizeof line, "%s   now: %s %d %s, field %d/%d, seq %d", page == PAGE_MAIN ? "P4 scenes" : "Manual",
                 month_names[month], day, slot_names[w->slot % 6], w->field, w->room, w->sequence);
    }
    band(0, HEAD, 0xff80ffff, line);
    const char *why = not_ready(w);
    snprintf(line, sizeof line, "%s", why ? why : "ready: a choice starts at once");
    band(1, BACK, why ? 0xff8080ff : 0xff80ff80, line);
    band(2, BACK, 0xffc0c0c0, status[0] ? status : last_result[0] ? last_result : "");
    int first = cursor - VISIBLE / 2;
    if (first > row_count - VISIBLE)
        first = row_count - VISIBLE;
    if (first < 0)
        first = 0;
    for (int i = 0; i < VISIBLE; i++) {
        int r = first + i;
        if (r >= row_count) {
            band(3 + i, BACK, 0, NULL);
            continue;
        }
        const Row *row = &rows[r];
        if (row->action == A_VALUE) {
            if (row->value == &manual[M_SLOT])
                snprintf(line, sizeof line, "  %-12s %5d  %s", row->label, *row->value, slot_names[*row->value % 6]);
            else
                snprintf(line, sizeof line, "  %-12s %5d", row->label, *row->value);
        } else {
            snprintf(line, sizeof line, "%s%s", row->action == A_HEADER ? "" : "  ", row->label);
        }
        unsigned color = row->action == A_HEADER ? 0xffa0a0a0 : row->action == K_DAY ? 0xffa0ffa0 : 0xffffffff;
        band(3 + i, r == cursor ? SEL : BACK, r == cursor ? 0xff40ffff : color, line);
    }
    band(BANDS - 2, FOOT, 0xffd0d0d0,
         page == PAGE_CALENDAR ? "<> day   L/R month   /\\ next story day   ^v choose" :
         page == PAGE_MANUAL ? "<> 1  L/R 10  []/\\ 100 on a number" : "^v move");
    band(BANDS - 1, FOOT, 0xffd0d0d0, page == PAGE_MAIN ? "X/START choose   O close" : "X/START choose   O back");
}

/* Returns when the menu closes; a chosen request is submitted. */
static void menu(void)
{
    const unsigned combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
    char status[80] = "";
    Where w;
    where_now(&w);
    if (w.root >= 0)
        manual[M_FIELD] = w.field, manual[M_ROOM] = w.room;
    if (w.step >= 0)
        manual[M_STEP] = w.step;
    date_split(w.date, &manual[M_MONTH], &manual[M_DAY]);
    manual[M_SLOT] = w.slot % 6;
    calendar_date = w.date;
    if (!rows && !(rows = malloc(ROWS_MAX * sizeof *rows)))
        return;
    load_presets();
    load_recent();
    load_days();
    p4_log("SCENE menu open, %d presets, %d recent, %d day items\n", preset_count, recent_count, day_item_count);
    int page = PAGE_MAIN, cursor = 0, main_cursor = 0, dirty = 1, frames = 0;
    build_page(page);
    unsigned held = buttons_now() | combo, repeat = 0, *shown = NULL;
    for (;;) {
        sceDisplayWaitVblankStart();
        unsigned pressed = input(&held, &repeat);
        if (pressed)
            dirty = 1, status[0] = 0;
        if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN))
            cursor = step_cursor(cursor, pressed & PSP_CTRL_UP ? -1 : 1);
        if (page == PAGE_CALENDAR) {
            int move = (pressed & PSP_CTRL_RIGHT ? 1 : 0) - (pressed & PSP_CTRL_LEFT ? 1 : 0);
            int month, day;
            date_split(calendar_date, &month, &day);
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) { /* a month: the same day, or the month's end */
                int order = (month >= 4 ? month - 4 : month + 8) + (pressed & PSP_CTRL_RTRIGGER ? 1 : -1);
                order = (order + 12) % 12;
                calendar_date = date_index(order < 9 ? order + 4 : order - 8, day);
            }
            if (pressed & PSP_CTRL_TRIANGLE) /* the next day whose script starts an event */
                for (int i = 1; i < 365; i++)
                    if (day_has_events((calendar_date + i) % 365)) {
                        calendar_date = (calendar_date + i) % 365;
                        break;
                    }
            if (move || (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_TRIANGLE))) {
                calendar_date = (calendar_date + move + 365) % 365;
                build_page(page);
                cursor = 0;
            }
        } else if (selectable(cursor) && rows[cursor].action == A_VALUE) {
            Row *r = &rows[cursor];
            int step = (pressed & PSP_CTRL_RIGHT ? 1 : 0) - (pressed & PSP_CTRL_LEFT ? 1 : 0) +
                       (pressed & PSP_CTRL_RTRIGGER ? 10 : 0) - (pressed & PSP_CTRL_LTRIGGER ? 10 : 0) +
                       (pressed & PSP_CTRL_TRIANGLE ? 100 : 0) - (pressed & PSP_CTRL_SQUARE ? 100 : 0);
            int range = r->high - r->low + 1;
            *r->value = r->low + ((*r->value - r->low + step) % range + range) % range;
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            if (page == PAGE_MAIN)
                break;
            page = PAGE_MAIN, cursor = main_cursor;
            build_page(page);
        } else if ((pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) && selectable(cursor)) {
            const Row *r = &rows[cursor];
            int submitted = 1;
            switch (r->action) {
            case A_CALENDAR:
            case A_MANUAL:
                main_cursor = cursor;
                page = r->action == A_CALENDAR ? PAGE_CALENDAR : PAGE_MANUAL;
                cursor = 0;
                build_page(page);
                if (!selectable(cursor))
                    cursor = step_cursor(cursor, 1);
                submitted = 0;
                break;
            case A_SAVE:
                save_here(status, sizeof status);
                load_presets();
                build_page(page);
                submitted = 0;
                break;
            case A_RELOAD:
                load_presets();
                build_page(page);
                snprintf(status, sizeof status, "%d presets", preset_count);
                submitted = 0;
                break;
            case A_VALUE:
                submitted = 0;
                break;
            case A_MAN_FIELD: submit(K_FIELD, manual[M_FIELD], manual[M_ROOM], manual[M_ENTRY], 0); break;
            case A_MAN_FLOOR: submit(K_FLOOR, manual[M_STEP], 0xFFFF, 0, 0); break;
            case A_MAN_EVENT: submit(K_EVENT, manual[M_EVENT], manual[M_SUB], 0, 0); break;
            case A_MAN_BATTLE: submit(K_BATTLE, manual[M_ENCOUNTER], 0, 0, 0); break;
            case A_MAN_DAY:
                submit(K_DAY, date_index(manual[M_MONTH], manual[M_DAY]), manual[M_SLOT], 0, 0);
                break;
            default:
                submit(r->action, r->a, r->b, r->c, 0);
                break;
            }
            if (submitted)
                break;
        }
        if (!screen_get())
            continue;
        /* redraw on a change, when the shown buffer changes, and every second */
        if (screen != shown || ++frames >= 60)
            dirty = 1;
        if (dirty) {
            draw_page(page, cursor, &w, status);
            shown = screen, dirty = 0, frames = 0;
            where_now(&w);
        }
    }
    while (buttons_now() & 0xffff) /* nothing pressed here reaches the game */
        sceDisplayWaitVblankStart();
    p4_log("SCENE menu closed\n");
}

/* scePadRead (port 0), once a game frame: the buttons the game gets */
unsigned p4_scene_frame(unsigned buttons)
{
    static unsigned last;
    const unsigned combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
    int kind = p4_scene_req[0];
    if (kind) {
        int a = p4_scene_req[1], b = p4_scene_req[2], c = p4_scene_req[3], d = p4_scene_req[4];
        p4_scene_req[0] = 0;
        if (kind == K_MENU)
            buttons |= combo, last = 0;
        else if (kind > K_NONE && kind <= K_DAY)
            submit(kind, a, b, c, d);
    }
    observe();
    if (rq.state != R_IDLE)
        run_request();
    int open = (buttons & combo) == combo && (last & combo) != combo;
    last = buttons;
    if (open && screen_get()) {
        menu();
        last = combo;
        return 0;
    }
    return buttons;
}
