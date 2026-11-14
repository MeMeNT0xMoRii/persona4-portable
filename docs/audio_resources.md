# PSP game audio

The game keeps the Atlus `sdkSnd`/`hsnd` interface. `port/audio_game.c` replaces
the PS2/CRI playback boundary; `port/audio.c` uses firmware SAS synthesis and
one `sceAudioOutput2` stereo output. PS2 IOP/CRI modules remain excluded.

## Resources

```
python -m pip install imageio-ffmpeg
python tools/psp_audio.py --all --vgmstream <vgmstream-cli.exe> --pack build/audio_pack/SOUND
```

Inputs come from the user's own USA ISO. Do not distribute generated assets.
The catalogue has 17,375 streams: 17,363 ADX and 12 AHX entries, including AFS
and ACX members with their original ordinal numbers. AHX decoding uses the
upstream vgmstream CLI; the tested Windows x64 tool was release r2168 from
https://github.com/vgmstream/vgmstream/releases/tag/r2168.

Loose conversion intermediates and the catalogue belong in `build/audio_loose`.
The final installation needs only the packed `SOUND` directory. Each stream is
decoded offline, encoded to PSX/VAG ADPCM, and keeps its sample rate, channel
count and authored ADX loop interval (rounded to a 28-sample VAG boundary).
Stereo channels remain separate SAS voices. The encoder resets its predictor
at independent chunk boundaries and at the source loop start.

The streaming file header (`P4A1`, 32 bytes, little endian) contains rate,
channels, source samples, loop-start block (FFFFFFFF means no loop), block
count, version 1 and a reserved word. Data consists of 16-byte VAG blocks,
interleaved by channel. This is an intermediate format; packed files contain
the same headers and payloads at 64-byte-aligned offsets.

`audio.idx` uses `P4I3`, followed by count, record stride (32), and selector-blob
size. Eight-word records are sorted by path hash: path hash, source-prefix
signature, original size, resource ID, pack number, byte offset, secondary
signature, selector offset (FFFFFFFF for none). The prefix is FNV-1a over at
most 1024 original bytes. Duplicated prefixes are audited against full SHA256;
actual distinguishing byte positions disambiguate different files. The USA
catalogue needs at most three such bytes per ambiguous family. The source
lookup has a separate sorted index, so it does not scan thousands of entries
on a playback request. The selector blob stores count + original byte offsets.

`AUDIO.0`, `AUDIO.1`, ... are capped at 1 GiB. Identical converted streams share
one payload. Original COMSE/BSE/TEST HD/BD/SQ files are also copied to SOUND.
`audio_manifest.json` records all packed file hashes and completeness counts.

## Runtime

- Five streaming channels reserve SAS voices 0-9; effects use voices 10-31.
- One audio worker owns all SAS calls and the command queue. Grain: 512 samples
  at 44100 Hz. The game thread does no sound-file I/O.
- Each stream has a 2048-block ring per channel. A loader fills 256-block chunks,
  publishes them after cache writeback, and stays ahead of the SAS consumer.
- Prefill is spread across loader iterations. The loader runs above the main
  game thread's priority to avoid starvation in CPU-bound scenes, with at most
  one streaming I/O chunk per iteration.
- HD/BD/SQ memory banks are copied in bounded pieces. The original caller's
  completion state is set only after the audio service owns a complete copy.
- Music stop/fade, same-track resume, finite playback completion, path streams,
  AFS partition/index requests and ACX/memory-source requests use the game API.
- Movie playback pauses SAS and releases Output2 before AVC playback, then
  resumes it. AVCODEC firmware can be shared without treating already-loaded
  as an error or unloading another subsystem's reference.
- b247 requires an acknowledged pause before calling AVC. The audio worker
  submits one grain with all voices paused, drains/releases Output2, and only
  then acknowledges. Resume also waits for acknowledgement, preventing a stale
  pause acknowledgement from being reused by an immediate second movie.
  A timeout or a stopped worker skips the movie and logs the audio step/error.
  This hardens the handoff; it does not establish the cause of the historical
  long-session Media Engine hang. See [audio_video_hang.md](audio_video_hang.md).
- b249 also stops the workers and releases owned SASCORE/AVCODEC around movies,
  preserving loaded banks/queued commands and restoring music at its current
  ADPCM block. The full-game 10+ minute title-return test passed on b249; b247's
  acknowledged pause and b248's full MPEG teardown alone both failed it.

Effect timbres preserve original VAG samples, pitch, volume, pan and ADSR data.
SQ note-on/off timing is interpreted by the audio worker. Parameter automation
and SQ branch/loop controls still require fidelity comparison against PS2;
the current backend does not reproduce every original synthesizer feature.

## Verification and deployment

```
python -m unittest discover -s tools -p test_audio.py
python tools/psp_audio_probe.py
python tools/psp_audio_measure.py room --build 164 --release --base 0x08804000
python tools/psp_release.py --no-build
python tools/psp_copy.py --release build/release_b164
```

The release packager refuses an incomplete catalogue or an older index format.
The deployment helper reads 64 KiB blocks and verifies source SHA1 and card
readback SHA1 for each file; staged release files are checked against SHA256.
Stop/reset the game before replacing packed audio. Publish `audio.idx` last.
Keep PSPLink's PC USB server running across resets; wait for the console's new
prompt before sending the next commands.

Never take PSPLink screenshots during AVC playback. The screenshot helper now
checks movie task logs and refuses that operation. A screenshot during b163's
second opening movie reproduced the known decoder hang; it was restarted and
subsequent movie skip/return checks completed without screenshots during video.

Measured on the real PSP at 333 MHz:

| Test | Audio + loader CPU per 33.333 ms tick | Result |
|---|---:|---|
| Isolated real backend: music + 20 original effects | 0.331 ms average | No errors/underruns; user confirmed clean sound |
| b163 load menu | 0.398 ms average | No new errors/underruns |
| b163 Dojima's room | 0.644 ms average | No new errors/underruns; game ~30 FPS |

These are worker CPU averages, including their I/O calls, not a complete
worst-case bound for every game-triggered request. The earlier isolated SAS
feasibility probe's peak figures are documented separately in `sas_audio.md`.
Final b164 release deployment and validation artifacts belong under
`build/release_b164/validation`.

## Deployment handoff (2026-11-08)

- b164 EBOOT.PBP, p4data.bin and p4str.bin were installed and card-readback
  verified. The EBOOT's embedded PRX matches the separately tested release PRX.
- All 17,375 streams are converted and packed. Complete authoritative package:
  `build/audio_pack/SOUND` (12 files including manifest;
  AUDIO.0 is exactly 929,387,008 bytes; audio.idx is 556,520 bytes).
- The earlier on-card test pack had only 173 music/environment streams and
  no voices. The full pack copy through PSPLink was reset at ~208 MiB at the
  user's request to switch to USB mass-storage copying. **The on-card AUDIO.0
  is incomplete until that manual copy finishes.** Copy the entire SOUND folder,
  including the matching index, over `ms0:/PSP/GAME/P4/SOUND`.
- PSPLink acknowledged reset and returned to `host0:/>`; no copy is running.
  User will copy through XMB USB mode. Wait for completion before gameplay
  validation. Then verify an actual voiced dialogue and scene music.
- Full intermediate resources were moved within the workspace from the
  installation directory to `build/audio_loose` after conversion completed.
  Do not deploy loose .p4a files or conversion metadata.
