# PSP video resources

## Conversion and border cropping

The converter reads your own Persona 4 USA ISO, finds all 23 `SFD/*.SFD` movies in `DATA.CVM` and prepares separate resources. The ISO and CVM are unchanged. Generated resources stay in `build/`; do not distribute them with the source.

By default, the converter scans the entire movie and crops persistent outer borders while preserving titles and subtitles that appear later. It considers corner-border color during fades to white, keeps a two-pixel margin and uses even alignment. Aspect ratio is preserved, so small side borders may remain. Use `--keep-black-bars` to retain the original framing. Nineteen of the 23 movies have persistent borders suitable for cropping.

The historical `build/video_crop_update/PSP` package contains only changed `.p4v` files (about 66 MiB), compatible with the b161 player and its existing EBOOT/PCM. This is a local update artifact, not a complete installation or public download.

## Build and copy to the card

Follow [Building from source](building.md) for a clean build and resource preparation. Once all movies and audio have been prepared:

```powershell
python tools/psp_release.py
```

The current build is **b164**, with the hardware profiler disabled in the release. The original movie integration was tested in b161. `build/release_b164/UPDATE/PSP/GAME/P4` contains the matching EBOOT/data files, packed `SOUND` and all movie resources. The video directory has 23 `.p4v` and 22 `.pcm` files, about 401 MiB.

Copy the `PSP` directory from `UPDATE` to the card root, merging directories and replacing files. Archives `DATA.*`, `BTL.0`, `BGM.0`, `ENV.0`, `ZZZZZ.0` and `p4files.idx` are required from an existing installation. Saves are not included in the update. For a clean installation, use the complete `build/psp_dist/PSP/GAME/P4` folder. PC preview MP4 and conversion JSON files are not needed on PSP. Start Persona 4 from XMB; Start or Circle skips a movie.

`COPY_TO_CARD.txt` contains deployment instructions; `release.json` records update-file hashes. `python tools/psp_release.py --no-build` repackages an existing release after checking source and artifact hashes.

## Game integration

`port/video.c` provides the shared streaming player. `port/video_game.c` replaces `mwPlayerUpdate`: it reads the movie number from the game task, resolves its path through the original game table and starts AVC/PCM playback. Completion or skipping ends the task normally; its destructor releases the work and the game event continues.

Before playback, the renderer finishes and displays its last GE list and releases rebuildable mesh/skin caches. The main game thread waits for the movie while audio runs separately. Afterward, the framebuffer, renderer state and frameskip clock are restored. If a resource is missing or the decoder cannot open it, the log identifies the file and the game continues.

MPEG is linked before the other PSP libraries, and the build verifies that import ranges do not overlap. The first integration violated this rule and crashed in `pspDebugScreenInit`; that layout is now rejected before EBOOT creation. The b158 bone NaN repair remains active without the profiler.

## PC conversion

```powershell
python -m pip install -r tools/requirements-video.txt
python tools/psp_video.py --list
python tools/psp_video.py --movie P4CTOP1 --seconds 10
python tools/psp_video_probe.py
python tools/psp_video.py --all
```

Short `--seconds` probes default to `build/video_probe/VIDEO`; complete movies default to `build/psp_dist/PSP/GAME/P4/VIDEO`. Override paths with `--iso` and `--out`. The default rate is 29.97 FPS; use `--fps 15000/1001` for 14.985 FPS. This converts the frame rate offline rather than discarding dependent frames during playback.

FFmpeg comes from PATH or imageio-ffmpeg; override it with `--ffmpeg ./helpers/ffmpeg.exe`. PyAV reads metadata and packages AVC. Listing movies does not require importing those conversion dependencies.

Each movie produces:

- `.p4v`: H.264 Baseline, level 3.0, YUV420P, one reference frame, no B-frames, 480 x 272. Borders preserve the original aspect ratio, including SAR.
- `.pcm`: stereo signed 16-bit little-endian PCM at 44100 Hz (176400 bytes/s). `P4SR` has no audio stream.
- `.mp4`: a silent PC video preview; audio is in the adjacent PCM. The PSP reads only `.p4v` and `.pcm`.
- `.json`: conversion options and SHA256 hashes of the original SFD and prepared files.

Repeated runs compare options and hashes and skip valid outputs. `--force` reconverts. The manifest is updated after writing resources, so interrupted conversion can be resumed.

## Verification

```powershell
python -m unittest discover -s tools -p test_video.py
python tools/psp_video_verify.py build/psp_dist/PSP/GAME/P4/VIDEO --report build/video_validation.json
```

The verifier reads the actual `.p4v` packets, decodes all frames with an independent PC H.264 decoder, and checks timestamps, dimensions, frame counts, PCM and hashes. This does not replace PSP hardware-decoder testing.

## Hardware probe

`port/video_probe.c` is an isolated entry point using the same `port/video.c` as the game. `tools/psp_video_probe.py` creates `build/video_probe/EBOOT.PBP` and `video.prx`. Copy the EBOOT and VIDEO folder to `ms0:/PSP/GAME/P4VIDEO/`. The default movie is `P4CTOP1`. With PSPLink, pass another name using `ldstart .../video.prx P4CT019`, or add `mute` for a silent probe. Start or Circle stops playback.

Logs go to `ms0:/PSP/GAME/P4VIDEO/video.log` and PSPLink stdout. `build/video_probe/manifest.json` records source/build hashes. The probe builds with `-Wall -Wextra -Werror`.

The player uses `sceMpegGetAvcNalAu` (NID `0x11F95CF1`), whose import was absent from the installed PSPSDK. Its ABI was checked against [PMPlayer Advance](https://github.com/DavisDev/pmplayer-advance/tree/master/ppa/mod). MP4 AVC uses decoder mode 4 and a separate DDR area.

The initial memory corruption came from `sceMpegAvcDecode` returning **two pictures in one call**, even without B-frames. Passing one output-buffer address let firmware read a second address from the stack and overwrite unrelated memory. The player now supplies an address array and handles every returned picture; `sceMpegAvcDecodeStop` may return two more at EOF. AU workspace is 64 bytes, with guard bytes checked after playback. Skipping also drains the decoder. MPEG firmware remains initialized between movies; per-movie contexts are deleted.

Three RGB buffers live in RAM and are copied to two VRAM display buffers, allowing multiple decoded pictures without overwriting the displayed frame. Audio uses a separate thread and two PCM buffers. Played sample count drives video timing; silent movies use the system clock. The complete movie is never loaded into memory.

Historical hardware results:

| Probe | Frames / duration | Decoder time | Result |
| --- | --- | --- | --- |
| Ten-second clip | 300 frames | Not recorded here | Completed; sound confirmed |
| Full `P4CTOP1`, mute | 2520 / 84.085 s | 4.060 ms/frame | No delays longer than a frame |
| Full `P4CTOP1`, audio | 2520 / 84.087 s | 4.107 ms/frame | No audio errors or delays longer than a frame |
| 14.985 FPS, audio | 45 / 3.004 s | 4.378 ms/frame | No late frames |
| `P4SR`, no audio stream | 60 / 2.003 s | 4.235 ms/frame | No late frames |

Raw logs are local at `build/video_probe/experiments/{full_mute,full_audio,15fps,silent}.log`. The final audio block is padded with silence to 1024 samples. The full introductory movie was installed in `ms0:/PSP/GAME/P4VIDEO/`; running another `--seconds` probe replaces that local example without changing the full distribution resources.

Playback previously hung during a long test with simultaneous PSPLink `scrshot` and file copying. Full tests without those operations completed. Take screenshots after the movie and copy files before starting it. Do not use PSPLink screenshots during AVC playback or dump VRAM directly to host0; use RAM snapshots for diagnostics. Coordinate PSPLink access with any other active hardware session.

Main buffers use 2 MiB DDR aligned to 4 MiB, about 1.59 MiB RGB, 256 KiB input packets, 64 KiB decoder state and 8 KiB PCM. The standalone probe reserves a 12 MiB heap including alignment; the game frees caches for playback. Integration currently handles full-screen game movie tasks. Looped video on 3D textures would require separate work.

## P4V1 format

All numbers are little endian except NAL lengths inside AVCC packets. The 64-byte header is `"P4V1"` followed by 15 `uint32` values:

| Field | Value |
| --- | --- |
| version | 1 |
| width, height | 480, 272 |
| fps_num, fps_den | 30000/1001 or 15000/1001 |
| frames | Packet/frame count |
| max_packet | Maximum packet size, at most 256 KiB |
| sps_bytes, pps_bytes | SPS and PPS lengths |
| sample_rate, channels, audio_samples | 44100, 2, stereo sample count; zero without audio |
| reserved[3] | Zero |

Raw SPS and PPS follow without prefixes, then `frames` records: `uint32 packet_size`, `uint32 pts_90khz`, and `packet_size` bytes of AVCC. Each NAL has a four-byte big-endian length. Separate PCM starts at time zero; initial gaps are filled with silence.
