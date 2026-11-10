/* Streaming AVC/PCM player shared by the game and isolated hardware probe. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspdmac.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <pspmpeg.h>
#include <psputility.h>
#include <psppower.h>
#include <pspsuspend.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "video.h"

static const char *video_dir;
#define MAX_PACKET (256 * 1024)
#define FRAME_BYTES (512 * 272 * 4)
typedef struct {
    char magic[4];
    unsigned version, width, height, fps_num, fps_den, frames, max_packet;
    unsigned sps_size, pps_size, rate, channels, samples, reserved[3];
} Header;
typedef struct {
    void *sps; int sps_size; void *pps; int pps_size;
    int prefix; void *data; int size; int mode;
} Nal;
extern int sceMpegGetAvcNalAu(SceMpeg *, Nal *, SceMpegAu *);
/* The MP4 NAL path uses a 64-byte AU workspace, larger than the public PS AU. */
static union { SceMpegAu value; unsigned char workspace[64]; } au_storage __attribute__((aligned(64)));
#define au au_storage.value
static volatile int stop_audio, audio_error;
static volatile int audio_channel = -1;
static volatile unsigned audio_submitted, audio_end_wall;
static volatile unsigned started;
void (*p4_video_on_frame)(unsigned);
unsigned (*p4_video_buttons)(void);
static const char *stem = "P4CTOP1";
static int mute;
/* Firmware is initialized once for the game, not rebooted between cutscenes. */
static int codec_loaded, base_loaded, initialized;
volatile unsigned video_frame, video_shown, video_step;

extern void p4_log(const char *fmt, ...);
static void log_line(const char *fmt, ...)
{
    char line[512]; va_list ap;
    va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    p4_log("video %s\n", line);
}

static int exact(SceUID fd, void *buffer, unsigned size)
{
    unsigned done = 0;
    while (done < size) {
        int n = sceIoRead(fd, (char *)buffer + done, size - done);
        if (n <= 0) return -1;
        done += n;
    }
    return 0;
}

static int audio_main(SceSize size, void *arg)
{
    (void)size; (void)arg;
    char path[256]; snprintf(path, sizeof path, "%sVIDEO/%s.pcm", video_dir, stem);
    int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    int channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    short *buffers = memalign(64, 2 * 1024 * 4);
    if (fd < 0 || channel < 0 || !buffers) {
        audio_error = -1; goto finish;
    }
    audio_channel = channel;
    unsigned index = 0;
    while (!stop_audio) {
        short *b = buffers + (index++ & 1) * 2048;
        int n = sceIoRead(fd, b, 4096);
        if (n < 0) { audio_error = n; break; }
        if (!n) break;
        if (n < 4096) memset((char *)b + n, 0, 4096 - n);
        while (!started && !stop_audio) sceKernelDelayThread(100);
        if (stop_audio) break;
        int r = sceAudioOutputBlocking(channel, PSP_AUDIO_VOLUME_MAX, b);
        if (r < 0) { audio_error = r; break; }
        audio_submitted += 1024;
    }
    while (sceAudioGetChannelRestLen(channel) > 0) sceKernelDelayThread(1000);
    audio_end_wall = sceKernelGetSystemTimeLow();
finish:
    audio_channel = -1;
    if (channel >= 0) sceAudioChRelease(channel);
    if (fd >= 0) sceIoClose(fd);
    free(buffers);
    return 0;
}

/* Audio is the clock while playing. Read across submissions consistently;
 * callers clamp the short queue-update race to a monotonic value. */
static unsigned movie_time(int has_audio)
{
    if (!has_audio) return sceKernelGetSystemTimeLow() - started;
    if (audio_end_wall)
        return (unsigned)((unsigned long long)audio_submitted * 1000000 / 44100) +
               sceKernelGetSystemTimeLow() - audio_end_wall;
    unsigned submitted;
    int rest;
    do {
        submitted = audio_submitted;
        int channel = audio_channel;
        rest = channel >= 0 ? sceAudioGetChannelRestLen(channel) : 0;
    } while (submitted != audio_submitted);
    if (rest < 0 || (unsigned)rest > submitted) return 0;
    return (unsigned)((unsigned long long)(submitted - rest) * 1000000 / 44100);
}


/* The volatile block: ddr (2 MB, 4 MB aligned at its base), the three output frames, the packet, the MPEG work area,
   the parameter sets. 0 when it cannot be locked or is too small: the caller takes the heap as before. */
enum { VOL_WORK_MAX = 0x10000 + 256 + 4096 };
static void *vol_ddr, *vol_output, *vol_packet, *vol_work, *vol_params;
/* b174: sceMpegCreate hung twice after a long stretch of play since the last movie (b169 P4CTOP2, b172 P4CT004, both
   waiting in sceMpeg), with the buffers in the heap and in the volatile block alike; back-to-back movies never did.
   p4_video_keep: the SceMpeg made for the first movie (and the volatile block it lives in) stays for the session;
   later movies reuse it, so Create runs once. A movie that fails drops it. */
int p4_video_keep = 1;
static int vol_held, kept;
static SceMpeg kept_mpeg;
static unsigned last_movie_end;
static int volatile_carve(unsigned params_bytes)
{
    if (vol_held)
        return 1;
    void *base = NULL;
    int size = 0;
    int r = sceKernelVolatileMemTryLock(0, &base, &size);
    if (r < 0) { log_line("volatile lock %08x", r); return 0; }
    unsigned at = (unsigned)base, end = at + (unsigned)size;
    vol_ddr = base; at += 2 * 1024 * 1024; /* 0x08400000: 4 MB aligned, as the heap's memalign gave it */
    vol_output = (void *)at; at += FRAME_BYTES * 3;
    vol_packet = (void *)at; at += MAX_PACKET;
    vol_work = (void *)at; at += VOL_WORK_MAX;
    vol_params = (void *)at; at = (at + params_bytes + 63) & ~63u;
    if (at > end) {
        log_line("volatile block %p size %x too small (need %x)", base, size, at - (unsigned)base);
        sceKernelVolatileMemUnlock(0);
        return 0;
    }
    vol_held = 1;
    return 1;
}

int p4_video_play(const char *directory, const char *name, int muted)
{
    video_dir = directory; stem = name; mute = muted;
    stop_audio = audio_error = 0;
    audio_channel = -1; audio_submitted = audio_end_wall = started = 0;
    video_frame = video_shown = video_step = 0;
    int result = -1, created = 0, rb_created = 0;
    int decoder_started = 0, drained = 0, volatile_locked = 0, heap_work = 0;
    SceUID audio_thread = -1, fd = -1;
    int audio_running = 0;
    void *work = NULL, *ddr = NULL, *packet = NULL, *params = NULL, *output = NULL;
    SceMpeg mpeg = NULL;
    static SceMpegRingbuffer rb; /* static: a kept SceMpeg points at it */
    Header h;
    char path[256]; snprintf(path, sizeof path, "%sVIDEO/%s.p4v", video_dir, stem);
    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0 || exact(fd, &h, sizeof h) < 0) { log_line("cannot read %s", path); goto done; }
    if (memcmp(h.magic, "P4V1", 4) || h.version != 1 || h.width != 480 || h.height != 272 ||
        !h.frames || !h.fps_num || !h.fps_den || h.max_packet > MAX_PACKET || !h.max_packet ||
        !h.sps_size || h.sps_size > 1024 || !h.pps_size || h.pps_size > 1024 ||
        (h.samples && (h.rate != 44100 || h.channels != 2))) {
        log_line("unsupported P4V header"); goto done;
    }
    /* b170: the player's buffers live in the 4 MB volatile block (0x08400000), locked for the movie. From the
       heap, a long session put the MPEG work area at 0x0a31b040 and sceMpegCreate never returned (b169, back to the
       title after Heaven); the movies that played had it below 0x0a000000. */
    volatile_locked = volatile_carve(h.sps_size + h.pps_size + 256);
    if (!volatile_locked) {
        params = memalign(64, h.sps_size + h.pps_size + 256);
        packet = memalign(64, MAX_PACKET);
    } else
        params = vol_params, packet = vol_packet;
    if (!params || !packet || exact(fd, params, h.sps_size + h.pps_size) < 0) goto done;
    int r;
    if (!codec_loaded) {
        r = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);
        log_line("load avcodec %08x", r);
        if (r < 0 && r != (int)0x80111102u && r != (int)0x80110f02u) goto done;
        codec_loaded = r < 0 ? 2 : 1;
    }
    if (!base_loaded) {
        r = sceUtilityLoadAvModule(PSP_AV_MODULE_MPEGBASE);
        log_line("load mpegbase %08x", r); if (r < 0) goto done; base_loaded = 1;
    }
    if (!initialized) {
        r = sceMpegInit(); log_line("mpeg init %08x", r); if (r < 0) goto done; initialized = 1;
    }
    int work_size = sceMpegQueryMemSize(4);
    if (work_size <= 0) goto done;
    if (volatile_locked && work_size + 256 <= VOL_WORK_MAX) {
        work = vol_work, ddr = vol_ddr, output = vol_output;
    } else {
        if (volatile_locked) log_line("volatile work too small for %x", work_size);
        work = memalign(64, work_size + 256);
        ddr = memalign(4 * 1024 * 1024, 2 * 1024 * 1024);
        output = memalign(64, FRAME_BYTES * 3);
        heap_work = 1;
    }
    if (!work || !ddr || !output) { log_line("allocation failed work %p ddr %p output %p", work, ddr, output); goto done; }
    memset((char *)work + work_size, 0xa5, 256);
    memset((char *)params + h.sps_size + h.pps_size, 0xa5, 256);
    log_line("work %p size %x packet %p params %p ddr %p output %p volatile %d mute %d", work, work_size, packet, params, ddr, output, volatile_locked, mute);
    log_line("start: %u ms since the last movie, keep %d kept %d", last_movie_end ? (sceKernelGetSystemTimeLow() - last_movie_end) / 1000 : 0,
             p4_video_keep, kept);
    if (kept && volatile_locked && !heap_work) {
        mpeg = kept_mpeg; created = rb_created = 1;
        log_line("mpeg reused");
    } else {
        memset(&rb, 0, sizeof rb);
        r = sceMpegRingbufferConstruct(&rb, 0, NULL, 0, NULL, NULL);
        if (r < 0) { log_line("ringbuffer %08x", r); goto done; } rb_created = 1;
        log_line("mpeg create begin");
        r = sceMpegCreate(&mpeg, work, work_size, &rb, 512, 4, (int)ddr);
        log_line("mpeg create %08x ddr %p", r, ddr); if (r < 0) goto done; created = 1;
        if (p4_video_keep && volatile_locked && !heap_work) kept = 1, kept_mpeg = mpeg;
    }
    memset(&au_storage, 0xff, sizeof au_storage);
    r = sceMpegInitAu(&mpeg, (char *)ddr + 0x10000, &au);
    log_line("init au %08x es %08x", r, au.iEsBuffer); if (r < 0) goto done;
    SceMpegAvcMode mode = {-1, SCE_MPEG_AVC_FORMAT_8888};
    r = sceMpegAvcDecodeMode(&mpeg, &mode);
    if (r < 0) { log_line("mode %08x", r); goto done; }
    if (h.samples && !mute) {
        audio_thread = sceKernelCreateThread("p4video_audio", audio_main, 0x18, 32 * 1024, PSP_THREAD_ATTR_USER, NULL);
        if (audio_thread < 0 || sceKernelStartThread(audio_thread, 0, NULL) < 0) goto done;
        audio_running = 1;
    }
    unsigned total_decode = 0, shown = 0, late = 0, clock_last = 0, max_pictures = 0;
    for (unsigned frame = 0; frame <= h.frames; frame++) {
        video_frame = frame; video_step = 1;
        unsigned record[2] = {0, 0};
        if (frame < h.frames && (exact(fd, record, sizeof record) < 0 || !record[0] || record[0] > h.max_packet ||
            exact(fd, packet, record[0]) < 0)) { log_line("bad packet %u", frame); goto done; }
        sceKernelDcacheWritebackInvalidateAll();
        unsigned begin = sceKernelGetSystemTimeLow();
        void *buffer[4];
        for (unsigned i = 0; i < 4; i++) buffer[i] = (char *)output + (i % 3) * FRAME_BYTES;
        SceInt32 pictures = 0;
        if (frame < h.frames) {
            Nal nal = {params, h.sps_size, (char *)params + h.sps_size, h.pps_size,
                       4, packet, record[0], frame == 0 ? 3 : 0};
            video_step = 2;
            r = sceMpegGetAvcNalAu(&mpeg, &nal, &au);
            if (r < 0) { log_line("NAL frame %u error %08x size %u", frame, r, record[0]); goto done; }
            video_step = 3;
            decoder_started = 1;
            r = sceMpegAvcDecode(&mpeg, &au, 512, buffer, &pictures);
        } else {
            video_step = 6;
            r = sceMpegAvcDecodeStop(&mpeg, 512, buffer, &pictures);
            drained = r >= 0;
            log_line("drain r %08x pictures %ld", r, pictures);
        }
        total_decode += sceKernelGetSystemTimeLow() - begin;
        if (frame < 3) log_line("decode %u r %08x pictures %ld es %08x au_size %u guard %02x/%02x", frame, r, pictures,
            au.iEsBuffer, au.iAuSize, ((unsigned char *)work)[work_size], ((unsigned char *)params)[h.sps_size+h.pps_size]);
        if (r < 0 || pictures < 0 || pictures > 3 || shown + pictures > h.frames) goto done;
        if ((unsigned)pictures > max_pictures) max_pictures = pictures;
        if (!pictures) continue;
        if (!started) started = sceKernelGetSystemTimeLow();
        for (int picture = 0; picture < pictures; picture++) {
            unsigned due = (unsigned)((unsigned long long)shown * h.fps_den * 1000000 / h.fps_num);
            unsigned elapsed = movie_time(audio_running);
            if (elapsed > clock_last) clock_last = elapsed;
            if (clock_last > due + 33400) late++;
            while (clock_last < due && !audio_error) {
                video_step = 4;
                unsigned wait = due - clock_last;
                sceKernelDelayThread(wait > 2000 ? 2000 : wait);
                elapsed = movie_time(audio_running);
                if (elapsed > clock_last) clock_last = elapsed;
            }
            void *display = (void *)(0x04000000u + (shown & 1) * FRAME_BYTES);
            video_step = 5;
            /* DMA, not a CPU memcpy: PPSSPP only sees VRAM writes it is told about (a memcpy showed black, 0 FPS). */
            sceKernelDcacheWritebackInvalidateRange(buffer[picture], FRAME_BYTES);
            if (sceDmacMemcpy(display, buffer[picture], FRAME_BYTES) < 0)
                memcpy((void *)((unsigned)display | 0x40000000u), buffer[picture], FRAME_BYTES);
            sceDisplayWaitVblankStart();
            sceDisplaySetFrameBuf(display, 512,
                                 PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
            shown++;
            video_shown = shown;
            if (p4_video_on_frame) p4_video_on_frame(shown);
            SceCtrlData pad; sceCtrlPeekBufferPositive(&pad, 1);
            if (p4_video_buttons) pad.Buttons |= p4_video_buttons();
            if (pad.Buttons & (PSP_CTRL_START | PSP_CTRL_CIRCLE)) goto playback_end;
            if (audio_error) { log_line("audio error %08x", audio_error); goto done; }
        }
        if (frame && frame % 300 == 0) log_line("progress packet %u shown %u clock_us %u", frame, shown, clock_last);
    }
    if (shown != h.frames) { log_line("missing frames shown %u expected %u", shown, h.frames); goto done; }
    /* Keep the last image for its full duration and allow the PCM tail to finish. */
    while (!audio_error && movie_time(audio_running) <
           (unsigned)((unsigned long long)h.frames * h.fps_den * 1000000 / h.fps_num))
        sceKernelDelayThread(1000);
playback_end:
    for (unsigned i = 0; i < 256; i++)
        if (((unsigned char *)work)[work_size + i] != 0xa5 ||
            ((unsigned char *)params)[h.sps_size + h.pps_size + i] != 0xa5) {
            log_line("decoder buffer overrun at %u", i); goto done;
        }
    log_line("played %u frames decode_avg_us %u late %u elapsed_us %u", shown,
             shown ? total_decode / shown : 0, late, sceKernelGetSystemTimeLow() - started);
    log_line("max pictures %u audio submitted %u expected %u error %08x", max_pictures,
             audio_submitted, h.samples, audio_error);
    result = 0;
done:
    stop_audio = 1;
    if (audio_thread >= 0) {
        if (audio_running) sceKernelWaitThreadEnd(audio_thread, NULL);
        sceKernelDeleteThread(audio_thread);
    }
    if (created && decoder_started && !drained) {
        void *buffers[4];
        for (int i = 0; i < 4; i++) buffers[i] = (char *)output + (i % 3) * FRAME_BYTES;
        SceInt32 pictures = 0;
        r = sceMpegAvcDecodeStop(&mpeg, 512, buffers, &pictures);
        log_line("close drain r %08x pictures %ld", r, pictures);
        if (r < 0) result = -1;
    }
    if (kept && result < 0) { log_line("dropping the kept mpeg after a failure"); kept = 0; }
    int keep_now = kept && created && mpeg == kept_mpeg;
    if (created && !keep_now) sceMpegDelete(&mpeg);
    if (rb_created && !keep_now) sceMpegRingbufferDestruct(&rb);
    if (heap_work || !volatile_locked) { free(ddr); free(work); free(output); }
    if (!volatile_locked) { free(packet); free(params); }
    if (volatile_locked && !kept) { sceKernelVolatileMemUnlock(0); vol_held = 0; }
    last_movie_end = sceKernelGetSystemTimeLow();
    if (fd >= 0) sceIoClose(fd);
    log_line("probe finished result %d", result);
    return result;
}

void p4_video_shutdown(void)
{
    if (kept) { sceMpegDelete(&kept_mpeg); kept = 0; }
    if (vol_held) { sceKernelVolatileMemUnlock(0); vol_held = 0; }
    if (initialized) sceMpegFinish();
    if (base_loaded) sceUtilityUnloadAvModule(PSP_AV_MODULE_MPEGBASE);
    if (codec_loaded == 1) sceUtilityUnloadAvModule(PSP_AV_MODULE_AVCODEC);
    initialized = base_loaded = codec_loaded = 0;
}
