/* QA07 deterministic interleaving model of pinned rwsky.c flip_main; HOST ONLY.
 * Frozen source: flip_main L650-663, producer L1498-1526/L1587-1600,
 * gate_poll L1890-1899. This is not the PSP implementation, a scheduler,
 * a hardware measurement or an executable P4P patch. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t queued, finished, shown, pub_seq, gate_seq;
    int pub_fb, gate_closed, watchdog;
    int accepted_count, failure_count;
    int display_last_fb;
} State;

typedef struct { uint32_t seq; int fb; } Snapshot;

static State initial(void) {
    State s = {0};
    s.queued = 7; s.finished = 7; s.shown = 6;
    s.pub_seq = 7; s.pub_fb = 0;
    s.gate_seq = 7; s.gate_closed = 1;
    s.display_last_fb = -1;
    return s;
}
static int can_publish(const State *s) {
    /* Real source permits producer advance only after shown==queued,
       UNLESS 1s pipeline watchdog abandoned the wait. */
    return s->shown == s->queued || s->watchdog;
}
static void publish_next(State *s, int new_fb) {
    assert(can_publish(s));
    s->queued++;
    /* Pinned producer writes flip_fb FIRST, then flip_seq. */
    s->pub_fb = new_fb;
    s->pub_seq = s->queued;
    s->gate_seq = s->queued;
    s->gate_closed = 1;
}
static int snapshot_seq_ready(const State *s, Snapshot *snap) {
    snap->seq = s->pub_seq;
    return snap->seq != s->shown && (int32_t)(s->finished - snap->seq) >= 0;
}
static void snapshot_fb(const State *s, Snapshot *snap) {
    /* Simulates the later independent flip_fb read. */
    snap->fb = s->pub_fb;
}
static void deliver(State *s, const Snapshot *snap, int api_result) {
    /* Exact T1: fps_presented only if result >=0, BUT shown_seq
       advances on BOTH success and failure. */
    if (api_result >= 0) {
        s->accepted_count++;
        s->display_last_fb = snap->fb;
    } else {
        s->failure_count++;
    }
    s->shown = snap->seq;
}
static int gate_poll(State *s) {
    if (s->gate_closed && (int32_t)(s->shown - s->gate_seq) >= 0)
        s->gate_closed = 0;
    return !s->gate_closed;
}
static int tests;
#define TEST(name, expr) do { assert((expr)); printf("PASS %02d %s\n", ++tests, name); } while (0)

int main(void) {
    State s = initial(); Snapshot snap = {0};
    TEST("normal producer cannot republish while old frame unshown", !can_publish(&s));
    TEST("consumer observes finished old seq", snapshot_seq_ready(&s, &snap) && snap.seq == 7);
    snapshot_fb(&s, &snap);
    TEST("normal seq and fb match", snap.seq == 7 && snap.fb == 0);
    deliver(&s, &snap, 0); /* success */
    TEST("successful display moves accepted counter and ack", s.shown == 7 && s.accepted_count == 1);
    TEST("matching gate opens on successful display", gate_poll(&s));

    s = initial();
    assert(snapshot_seq_ready(&s, &snap));
    snapshot_fb(&s, &snap);
    deliver(&s, &snap, -1); /* failed display API */
    TEST("failed display has zero successful accepted presents", s.accepted_count == 0 && s.failure_count == 1);
    TEST("failed display still incorrectly moves shown_seq", s.shown == 7);
    TEST("failed display incorrectly releases screen gate", gate_poll(&s));

    s = initial();
    assert(snapshot_seq_ready(&s, &snap)); /* consumer read OLD seq */
    s.watchdog = 1; /* user main broke 1s wait */
    TEST("timeout lets producer republish despite unshown seq", can_publish(&s));
    publish_next(&s, 1); /* producer writes NEW fb, NEW seq */
    snapshot_fb(&s, &snap); /* consumer reads new framebuffer */
    TEST("old seq/new framebuffer torn pair reachable", snap.seq == 7 && snap.fb == 1 && s.pub_seq == 8);
    deliver(&s, &snap, 0);
    TEST("torn pair can count accepted but wrong frame buffer", s.accepted_count == 1 && s.display_last_fb == 1 && s.shown == 7);
    TEST("newer frame gate correctly remains closed in torn case", !gate_poll(&s) && s.gate_closed);

    s = initial();
    s.watchdog = 1;
    publish_next(&s, 1);
    TEST("new seq GE incomplete cannot be displayed yet", !snapshot_seq_ready(&s, &snap) && s.pub_seq == 8 && s.finished == 7);
    s.finished = 8;
    TEST("new seq is eligible only after GE callback", snapshot_seq_ready(&s, &snap) && snap.seq == 8);

    s = initial();
    s.finished = 6; /* not done */
    TEST("old seq not ready when GE is incomplete", !snapshot_seq_ready(&s, &snap));
    TEST("no timeout/no completion leaves buffer identity intact", !can_publish(&s) && s.pub_fb == 0 && s.pub_seq == 7);

    printf("ALL %d HOST synthetic scheduler cases PASS; NO PPSSPP or PSP hardware tested.\n", tests);
    return 0;
}
