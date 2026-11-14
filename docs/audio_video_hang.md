# Long-session audio/video hang investigation (2026-11-14)

Historical result: b169/b172/b174 hung in the first Media Engine video call
after about ten minutes of gameplay with SAS running. b176 with `NOSOUND`
decoded a movie after 2560 seconds. Those older measurements used 423 MHz;
the current game and the new regression probe request 333 MHz.

The b246 boot log in `${P4_SESSION}/console.log` confirms
`audio off: NOSOUND present`. No sound-enabled fix had been validated there.

## b247 candidate

`p4_audio_pause` previously returned after 500 ms even without an acknowledgement,
and did not wait for resume. `mwPlayerUpdate` then unconditionally called AVC.
The worker also treated an exit as a successful pause. These are definite
handoff bugs; they are not yet proven to explain the historical firmware hang.

The candidate:

- Makes pause/resume return success only when the live worker acknowledges.
- Runs a SAS grain with all voices paused before acknowledging the pause.
  It discards that grain, preserves the previous voice pause mask, and resumes
  the same streams afterward.
- Drains/releases Output2 before acknowledging; bounds the output drain wait.
- Skips the movie if the handoff fails, so an unacknowledged SAS pause cannot
  lead into a blocking AVC call on the game thread.
- Logs failing SAS/Output2 operations, the exact error, and the current worker
  step (`p4_audio_step`, `p4_audio_last_error`).

This does not recover a Media Engine that has already stopped answering inside
AVC. A successful handoff followed by a decoder hang needs further diagnosis.

## Reproducing the hardware test

Build with the Windows PSPDEV toolchain:

```powershell
python tools/psp_audio_video_probe.py --out build/audio_video_candidate
```

The probe links the actual `audio.c` and `video.c`, runs at 333 MHz, ignores
`NOSOUND`, and reads the existing P4 SOUND/VIDEO installation. It first plays
P4CTOP2, then loops BGM/01.ADX with periodic effects for the requested interval,
plays P4CTOP2 again, and checks that audio blocks resume. The current probe skips
each movie after 90 displayed frames and shows a visible timer during SAS play.
The original baseline probe left the last movie frame on screen while waiting;
that static picture was not a decoder hang.

After copying the probe to the PSPLink host directory and resetting PSPLink:

```text
ldstart host0:/avtest_candidate/probe.prx 660 1
```

Arguments are seconds of SAS playback between movies and number of cycles.
Logs go to `ms0:/PSP/GAME/P4/avtest.log` and PSPLink stdout. Preserve the card log
before running another probe because it is truncated at startup. The source and
artifact hashes are recorded in the build's `manifest.json`.

This isolates the audio/video services. It does not exercise gameplay heap
pressure, scene changes or the complete set of sound-effect banks.

## Validation

- Host regression `tools/test_audio_handoff.py`: actual worker/handoff C code,
  mocked PSP calls; pause-before-ACK ordering, preserved mask including voice 31,
  resume acknowledgement, timeout, stopped worker, SAS failure, Output2 failure.
- Existing audio preparation tests: 3 passed. Video container tests: 5 passed.
- Audio probe and combined probe compile with `-Wall -Wextra -Werror`.
- Full diagnostic b247 builds; 19 PSP import ranges validated.
- Unmodified service control, 333 MHz: 660 seconds of looping stereo BGM/01.ADX
  and periodic effects, 57,027 audio blocks before the second movie, no audio
  errors/underruns. P4CTOP2 decoded all 3270 frames (average 4142 us/decode,
  zero late frames); audio resumed by 174 blocks in two seconds. DONE result 0.
  Raw: `build/audio_video_baseline/hardware.log`, card log mirrored at
  `${P4_HOST}/avtest_baseline/avtest.log`.
- Candidate services, 333 MHz: three 15-second intervals, four movies shown for
  90 frames each, all result 0; audio resumed by 174 blocks each time, no audio
  errors/underruns. `build/audio_video_candidate/hardware_short.log`.
- One b247 game startup/attract-movie attempt was confounded by a PSPLink
  `scrshot` requested while AVC was active. It stopped at video step 3/frame 0,
  audio step 5 (paused). This is not clean evidence for the original bug.
  `build/psplink_b247/game_scrshot_confounded.log`. The repeat does not request
  screenshots during video; it loaded save 3 in Heaven successfully.
- b247 **failed** the full-game title-return scenario, with no PSPLink screenshot
  during video: save 3 in Heaven, 906855 ms since the previous movie, audio block
  78965, zero errors/underruns, acknowledged pause state 5. Stuck at video step 3,
  frame 0, audio step 5 and last audio error 0. `user_main` waits in sceMpeg at
  EPC 0x0B9C6F1C. The handoff changes alone do not fix the original problem.
  Raw: `build/psplink_b247/title_return_hang.log`; counters over an 11-minute
  gameplay window: `build/psplink_b247/experiments/title_return_11min_audio.json`.

## b248 experiment

Default `p4_video_keep` to 0. Delete the MPEG decoder/ringbuffer, release its
volatile buffers, finish MPEG and unload MPEGBASE after each movie, before the
caller resumes SAS. Keep shared AVCODEC ownership rules intact. This differs
from the pre-b174 decoder recreation: it also finishes the MPEG library and
unloads MPEGBASE between movies. The hypothesis is that a complete video
session teardown avoids carrying Media Engine state across gameplay. This is
an experiment until the full-game title return has passed.

b248 full-game sound window: 600.134 seconds, 51,687 mixed blocks, zero errors,
underruns or dropped commands. Audio service + loader CPU cost 0.999 ms per
30 Hz tick. Raw counters:
`build/psplink_b248/experiments/title_return_10min_audio.json`.
The title return **failed**: block 62622, zero audio errors/underruns, acknowledged
pause state 5. AVCODEC was still shared (`80110f02`), MPEGBASE loaded successfully,
then `sceMpegInit` did not return. `build/psplink_b248/title_return_hang.log`.

## b249: release the audio firmware too (title-return test passed)

After the acknowledged pause, stop/join the audio and loader workers and unload
owned SASCORE followed by owned AVCODEC. Video now loads its own AVCODEC, closes
its decoder/library/modules completely, then audio reloads SASCORE/AVCODEC and
restarts its workers. This releases the shared firmware between the two users;
b248 left the audio-owned AVCODEC loaded across the whole session.

Keep the audio index, loaded effect banks and queued commands. Active streams
reopen at the current ADPCM block (loop position folded into the file's loop
range), refill and restart. Old active effect voices/sequences are stopped at
the movie boundary; new queued effects are retained. CPU counters accumulate
across worker restarts.

Short isolated hardware test: three five-second intervals, four 90-frame movie
runs. Every SASCORE/AVCODEC unload returned 0, each movie freshly loaded AVCODEC
with result 0, each movie returned 0 and audio resumed by 174 blocks in two
seconds. Zero audio errors/underruns.

Full game, 333 MHz, user loaded a Heaven save and returned through the game's
title-return menu: **passed**, confirmed visually by the user. The movie began
686047 ms after the preceding movie, at audio block 59990. SASCORE and AVCODEC
unloads returned 0, video loaded fresh AVCODEC/MPEGBASE (both 0), initialized and
created MPEG successfully, and decoded **all 3270 frames**, average 4129 us per
decode, zero late frames. Playback returned 0, audio workers restarted and the
normal movie task ended. No PSPLink screenshots were requested during playback.

The preceding 600.242-second sound window mixed 51,696 blocks with zero new
errors, underruns or dropped commands. A subsequent 5.115-second title-screen
window mixed 446 blocks, again with zero new errors/underruns/drops. Aggregate
audio errors were already 4 at the long window's start and 5 at the post-movie
window's start; this counter also covers unresolved game sound requests, and
these tests do not establish full audio fidelity. No SAS/Output2 SDK failure
was logged in the successful transition.

Artifacts in `build/psplink_b249/experiments/`:

- `title_return_10min_audio.json` and `_audio.log`: long sound window.
- `title_return_pass.log`: complete successful return, playback and restart.
- `title_resume_audio.json` and `_audio.log`: sound progression after playback.

b249's source/artifact manifest is in `build/psplink_b249/manifest.json`; PRX,
data image and string table were copied to the PSP and roundtrip SHA256 checked.
It is running through PSPLink; the card's XMB EBOOT was not replaced. `NOSOUND`
was renamed to `NOSOUND.b247-backup`, so game sound is enabled. This validates
the reported title-return scenario once; broader cutscene/scene coverage remains
open.
