/* Optional XMB pipeline timing. Only the finish callback writes finish_us.
   All window data belongs to the drawing thread. Unsigned deltas handle
   the PSP's wrapping microsecond clock. */
#ifndef P4_FRAME_TIMING_H
#define P4_FRAME_TIMING_H

enum { TIMING_WINDOW = 32 };
typedef struct {
    unsigned frame, end, total, cpu, wait, ge_tail, post, list_bytes, writeback, copy_bytes;
    int pipe;
} TimingFrame;
static volatile unsigned timing_finish_us;
static unsigned timing_previous, timing_writeback, timing_copy_bytes;
static int timing_started, timing_first;
static struct {
    unsigned count, totals[TIMING_WINDOW], last, piped;
    uint64_t cpu, wait, ge_tail, post;
    TimingFrame peak;
} timing_window;

static int timing_compare(const void *a, const void *b)
{
    unsigned x = *(const unsigned *)a, y = *(const unsigned *)b;
    return (x > y) - (x < y);
}

static void timing_report(void)
{
    unsigned n = timing_window.count;
    if (!n) return;
    qsort(timing_window.totals, n, sizeof(unsigned), timing_compare);
    p4_log("hwframe last=%u n=%u pipe=%u p50=%u p95=%u cpu=%u wait=%u ge_tail=%u post=%u\n",
           timing_window.last, n, timing_window.piped, timing_window.totals[(n-1)/2],
           timing_window.totals[(95*n+99)/100-1], (unsigned)(timing_window.cpu/n),
           (unsigned)(timing_window.wait/n), (unsigned)(timing_window.ge_tail/n), (unsigned)(timing_window.post/n));
    const TimingFrame *f = &timing_window.peak;
    p4_log("hwpeak f=%u t=%u pipe=%d wall=%u cpu=%u wait=%u ge_tail=%u post=%u listB=%u rasterWB=%u copyB=%u\n",
           f->frame, f->end, f->pipe, f->total, f->cpu, f->wait, f->ge_tail, f->post,
           f->list_bytes, f->writeback, f->copy_bytes);
    memset(&timing_window, 0, sizeof timing_window);
}

static unsigned timing_ge_tail(unsigned enter, unsigned elapsed, int pending, int finished, unsigned finish)
{
    if (!pending) return 0;
    if (!finished) return elapsed;
    int32_t delta = (int32_t)(finish - enter);
    if (delta <= 0) return 0;
    return (unsigned)delta < elapsed ? (unsigned)delta : elapsed;
}

static void timing_record(unsigned frame, unsigned enter, unsigned synced, unsigned end,
                          unsigned ge_tail, unsigned list_bytes, int pipe)
{
    if (timing_first) {
        p4_log("hwfirst f=%u t=%u wall=%u listB=%u pipe=%d\n", frame, end, end - timing_previous, list_bytes, pipe);
        timing_first = 0;
    }
    if (timing_started) {
        TimingFrame f = {frame, end, end - timing_previous, enter - timing_previous,
                         synced - enter, ge_tail, end - synced, list_bytes, timing_writeback, timing_copy_bytes, pipe};
        unsigned i = timing_window.count++;
        timing_window.totals[i] = f.total;
        timing_window.last = frame;
        timing_window.piped += pipe;
        timing_window.cpu += f.cpu;
        timing_window.wait += f.wait;
        timing_window.ge_tail += f.ge_tail;
        timing_window.post += f.post;
        if (!i || f.total > timing_window.peak.total)
            timing_window.peak = f;
        if (timing_window.count == TIMING_WINDOW)
            timing_report();
    }
    timing_started = 1;
    timing_previous = end;
    timing_writeback = timing_copy_bytes = 0;
}

static void timing_movie(int resuming, unsigned now)
{
    timing_report();
    p4_log("hwvideo %s t=%u\n", resuming ? "end" : "begin", now);
    timing_previous = now;
    timing_started = timing_first = resuming;
    timing_writeback = timing_copy_bytes = 0;
}
/* One manually requested frame. Only the timing build inspects the button
   edge or logs Im2D submissions. Keep ordinary timing runs free of this I/O. */
enum { TIMING_UI_LIMIT = 64 };
static int timing_ui_active, timing_ui_held;
static unsigned timing_ui_count;

static void timing_ui_poll(int pressed, int had_list, unsigned frame)
{
    if (timing_ui_active && had_list) {
        p4_log("hwui end f=%u inputs=%u logged=%u\n", frame, timing_ui_count,
               timing_ui_count < TIMING_UI_LIMIT ? timing_ui_count : TIMING_UI_LIMIT);
        timing_ui_active = 0;
    }
    if (pressed && !timing_ui_held) {
        timing_ui_active = 1;
        timing_ui_count = 0;
        p4_log("hwui armed after f=%u\n", frame);
    }
    timing_ui_held = pressed;
}
#endif
