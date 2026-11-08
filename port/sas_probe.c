/* Standalone SAS benchmark. No game resources or game state are touched. */
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspsascore.h>
#include <psputility.h>
#include <psppower.h>
#include <pspdebug.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

PSP_MODULE_INFO("P4SasProbe", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);
#define GRAIN 1024
#define BLOCKS 96
static SceSasCore core __attribute__((aligned(64)));
static short output[2][GRAIN * 2] __attribute__((aligned(64)));
static short pcm[4096] __attribute__((aligned(64)));
static unsigned char vag[32 * 16] __attribute__((aligned(64)));
static int logfd = -1;
static int use_output2;
struct Sample {
    unsigned before, after;
    unsigned long long idle, monitor_before, monitor_after;
};
static struct Sample samples[4096];
static unsigned sample_count;
static volatile int monitoring, monitor_error;

static void logline(const char *fmt, ...)
{
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (logfd >= 0) sceIoWrite(logfd, line, strlen(line));
    printf("%s", line);
    pspDebugScreenPrintf("%s", line);
}

static int check(int result, const char *operation)
{
    if (result < 0) logline("ERROR %s %08x\n", operation, result);
    return result;
}
#define TRY(operation) do { if (check((operation), #operation) < 0) return -1; } while (0)

static unsigned long long clock_us(SceKernelSysClock clock)
{
    /* Despite SDK argument names, outputs are seconds and remaining usec. */
    unsigned seconds, usec;
    sceKernelSysClock2USec(&clock, &seconds, &usec);
    return (unsigned long long)seconds * 1000000 + usec;
}

static int snapshot(unsigned long long *cpu, unsigned long long *idle)
{
    SceKernelThreadRunStatus thread;
    SceKernelSystemStatus system;
    memset(&thread, 0, sizeof thread); thread.size = sizeof thread;
    memset(&system, 0, sizeof system); system.size = sizeof system;
    TRY(sceKernelReferThreadRunStatus(sceKernelGetThreadId(), &thread));
    TRY(sceKernelReferSystemStatus(&system));
    *cpu = clock_us(thread.runClocks);
    *idle = clock_us(system.idleClocks);
    return 0;
}

/* High priority, sleeping between samples. Its overhead is INCLUDED in busy
 * time. Bracket the idle-counter read to bound counter-snapshot timing error. */
static int monitor_main(SceSize size, void *arg)
{
    (void)size; (void)arg;
    while (monitoring && sample_count < 4096) {
        struct Sample *s = &samples[sample_count];
        SceKernelSystemStatus system;
        SceKernelThreadRunStatus self;
        memset(&system, 0, sizeof system); system.size = sizeof system;
        memset(&self, 0, sizeof self); self.size = sizeof self;
        s->before = sceKernelGetSystemTimeLow();
        if (sceKernelReferThreadRunStatus(sceKernelGetThreadId(), &self) < 0) { monitor_error = 1; break; }
        s->monitor_before = clock_us(self.runClocks);
        if (sceKernelReferSystemStatus(&system) < 0) { monitor_error = 1; break; }
        s->idle = clock_us(system.idleClocks);
        if (sceKernelReferThreadRunStatus(sceKernelGetThreadId(), &self) < 0) { monitor_error = 1; break; }
        s->monitor_after = clock_us(self.runClocks);
        s->after = sceKernelGetSystemTimeLow();
        ++sample_count;
        sceKernelDelayThread(1000);
    }
    return 0;
}

/* For each pair of adjacent start samples, find an end sample beyond every
 * possible 33.334ms window starting between them. Non-idle time over the
 * enclosing interval conservatively bounds those windows, without subtracting
 * the idle baseline or the measurement thread's own cost. */
static unsigned peak_busy_bound(unsigned *span, unsigned *without_monitor)
{
    unsigned peak = 0;
    *span = 0;
    *without_monitor = 0;
    for (unsigned i = 0, j = 2; i + 1 < sample_count; ++i) {
        if (j <= i + 1) j = i + 2;
        while (j < sample_count && samples[j].before - samples[i + 1].after < 33334u) ++j;
        if (j == sample_count) break;
        unsigned wall = samples[j].after - samples[i].before;
        unsigned long long idle = samples[j].idle - samples[i].idle;
        if (idle > wall) { monitor_error = 1; continue; }
        unsigned busy = wall - (unsigned)idle;
        if (busy > peak) { peak = busy; *span = wall; }
        unsigned long long monitor = samples[j].monitor_before - samples[i].monitor_after;
        if (monitor > busy) { monitor_error = 1; continue; }
        unsigned net = busy - (unsigned)monitor;
        if (net > *without_monitor) *without_monitor = net;
    }
    return peak;
}

static int audio_output(int channel, void *buffer)
{
    return use_output2 ? sceAudioOutput2OutputBlocking(PSP_AUDIO_VOLUME_MAX, buffer)
                       : sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, buffer);
}

static int audio_drain(int channel)
{
    int remaining;
    while ((remaining = use_output2 ? sceAudioOutput2GetRestSample()
                                   : sceAudioGetChannelRestLen(channel)) > 0)
        sceKernelDelayThread(100);
    return remaining;
}

static int configure(int voices, int adpcm, int reverb)
{
    static int previous_voices;
    TRY(__sceSasSetPause(&core, 0xffffffffu, 0));
    for (int v = 0; v < previous_voices; ++v) {
        TRY(__sceSasSetKeyOff(&core, v));
    }
    if (previous_voices) { TRY(__sceSasCore(&core, output[0])); }
    TRY(__sceSasSetPause(&core, 0xffffffffu, 1));
    TRY(__sceSasRevType(&core, reverb ? PSP_SAS_EFFECT_TYPE_ROOM : PSP_SAS_EFFECT_TYPE_OFF));
    TRY(__sceSasRevVON(&core, 1, reverb));
    TRY(__sceSasRevEVOL(&core, 0x800, 0x800));
    for (int v = 0; v < voices; ++v) {
        if (adpcm) { TRY(__sceSasSetVoice(&core, v, vag, sizeof vag, 1)); }
        else { TRY(__sceSasSetVoicePCM(&core, v, pcm, 4096, 0)); }
        TRY(__sceSasSetPitch(&core, v, PSP_SAS_PITCH_BASE + v * 31));
        TRY(__sceSasSetVolume(&core, v, 64, 64, 64, 64));
        TRY(__sceSasSetADSRmode(&core, v, PSP_SAS_ADSR_ATTACK | PSP_SAS_ADSR_RELEASE,
            PSP_SAS_ADSR_CURVE_MODE_LINEAR_INCREASE, 0, 0, PSP_SAS_ADSR_CURVE_MODE_DIRECT));
        TRY(__sceSasSetADSR(&core, v, PSP_SAS_ADSR_ATTACK | PSP_SAS_ADSR_RELEASE,
            PSP_SAS_ENVELOPE_FREQ_MAX, 0, 0, 0));
        TRY(__sceSasSetPause(&core, 1u << v, 0));
        TRY(__sceSasSetKeyOn(&core, v));
    }
    TRY(__sceSasSetPause(&core, voices == 32 ? 0xffffffffu : (1u << voices) - 1u, 0));
    previous_voices = voices;
    sceKernelDcacheWritebackAll();
    return 0;
}

/* paced=0 measures background activity; paced=1 adds sceAudio; sas=1 adds SAS.
 * Idle deltas include system audio threads, interrupts and external activity.
 * All reporting and file writes are outside the measured interval. */
static int measure(int channel, int paced, int sas, int voices, int adpcm, int reverb)
{
    unsigned long long cpu0, idle0, cpu1, idle1;
    unsigned max_sas_wall = 0;
    if (sas) { TRY(configure(voices, adpcm, reverb)); }
    for (int b = 0; b < 8; ++b) {
        if (sas) { TRY(__sceSasCore(&core, output[b & 1])); }
        else memset(output[b & 1], 0, sizeof output[0]);
        if (paced) { TRY(audio_output(channel, output[b & 1])); }
    }
    if (paced) { TRY(audio_drain(channel)); }
    sample_count = 0; monitor_error = 0; monitoring = 1;
    int monitor = sceKernelCreateThread("sas_cpu_monitor", monitor_main, 0x10, 8192, PSP_THREAD_ATTR_USER, NULL);
    TRY(monitor);
    TRY(sceKernelStartThread(monitor, 0, NULL));
    TRY(snapshot(&cpu0, &idle0));
    unsigned start = sceKernelGetSystemTimeLow();
    if (!paced) sceKernelDelayThread(2200000);
    else for (int b = 0; b < BLOCKS; ++b) {
        if (sas) {
            unsigned before = sceKernelGetSystemTimeLow();
            TRY(__sceSasCore(&core, output[b & 1]));
            unsigned elapsed = sceKernelGetSystemTimeLow() - before;
            if (elapsed > max_sas_wall) max_sas_wall = elapsed;
        }
        TRY(audio_output(channel, output[b & 1]));
    }
    /* Drain to account for the final submitted buffers. */
    if (paced) { TRY(audio_drain(channel)); }
    unsigned elapsed = sceKernelGetSystemTimeLow() - start;
    TRY(snapshot(&cpu1, &idle1));
    monitoring = 0;
    TRY(sceKernelWaitThreadEnd(monitor, NULL));
    TRY(sceKernelDeleteThread(monitor));
    unsigned peak_span, peak_without_monitor;
    unsigned peak_busy = peak_busy_bound(&peak_span, &peak_without_monitor);
    if (monitor_error || sample_count < 100 || sample_count == 4096) {
        logline("ERROR incomplete CPU monitor; discard measurement\n");
        return -1;
    }
    if (cpu1 < cpu0 || idle1 < idle0 || cpu1 - cpu0 > elapsed + 1000u || idle1 - idle0 > elapsed + 1000u) {
        logline("ERROR invalid runtime counters; discard measurement\n");
        return -1;
    }
    long long busy = (long long)elapsed - (long long)(idle1 - idle0);
    unsigned long long monitor_cpu = samples[sample_count - 1].monitor_after - samples[0].monitor_before;
    logline("RESULT paced=%d sas=%d format=%s voices=%d reverb=%d wall_us=%u "
        "thread_us=%llu busy_us=%lld thread_us_per_tick=%.2f busy_us_per_tick=%.2f "
        "max_sas_wall_us=%u end_flags=%08x peak_busy_bound_us=%u peak_span_us=%u samples=%u peak_without_monitor_us=%u backend=%s monitor_cpu_us=%llu\n", paced, sas, adpcm ? "VAG" : "PCM",
        voices, reverb, elapsed, cpu1 - cpu0, busy,
        (double)(cpu1 - cpu0) * 1000000.0 / (30.0 * elapsed),
        (double)busy * 1000000.0 / (30.0 * elapsed), max_sas_wall,
        sas ? (unsigned)__sceSasGetEndFlag(&core) : 0, peak_busy, peak_span, sample_count, peak_without_monitor,
        use_output2 ? "output2" : "channel", monitor_cpu);
    return 0;
}

int main(int argc, char **argv)
{
    use_output2 = argc > 1 && !strcmp(argv[1], "output2");
    pspDebugScreenInit();
    logfd = sceIoOpen("ms0:/PSP/GAME/P4SAS/sas.log", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    TRY(scePowerSetClockFrequency(333, 333, 166));
    logline("P4 SAS probe v4 grain=%d blocks=%d clock=%dMHz; averages per 1/30s; raw peak includes monitor\n",
        GRAIN, BLOCKS, scePowerGetCpuClockFrequencyInt());
    TRY(sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC));
    TRY(sceUtilityLoadModule(PSP_MODULE_AV_SASCORE));
    TRY(__sceSasInit(&core, GRAIN, 32, PSP_SAS_OUTPUTMODE_STEREO, 44100));
    for (int i = 0; i < 4096; ++i) pcm[i] = (short)(((i & 63) < 32 ? (i & 31) : 31 - (i & 31)) * 256 - 4096);
    for (int b = 0; b < 32; ++b) {
        vag[b * 16] = 0x28; /* predictor 2, shift 8 */
        vag[b * 16 + 1] = b == 0 ? 6 : (b == 31 ? 3 : 2);
        for (int i = 2; i < 16; ++i) vag[b * 16 + i] = (i & 1) ? 0x1f : 0xf1;
    }
    sceKernelDcacheWritebackAll();
    int channel = use_output2 ? sceAudioOutput2Reserve(GRAIN)
                             : sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, GRAIN, PSP_AUDIO_FORMAT_STEREO);
    TRY(channel);
    int result = measure(channel, 0, 0, 0, 0, 0);
    if (!result) result = measure(channel, 1, 0, 0, 0, 0);
    const int counts[] = {0, 8, 16, 32};
    for (int format = 0; !result && format < 2; ++format)
        for (int fx = 0; !result && fx < 2; ++fx)
            for (int n = 0; !result && n < 4; ++n)
                result = measure(channel, 1, 1, counts[n], format, fx);
    if (!result) result = measure(channel, 1, 0, 0, 0, 0);
    if (!result) result = measure(channel, 0, 0, 0, 0, 0);
    if (use_output2) sceAudioOutput2Release();
    else sceAudioChRelease(channel);
    logline("DONE result=%d\n", result);
    if (logfd >= 0) sceIoClose(logfd);
    sceKernelExitDeleteThread(result);
    return result;
}
