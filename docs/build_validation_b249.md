# b249 publication checks (2026-11-14)

The Windows source-build utility packages the current b249 port and builds launch
files locally from the user's original Persona 4 USA disc. It downloads the pinned
upstream source and the separately published, SHA256-verified PSP toolchain.
The archive contains no prebuilt EBOOT, game objects or converted game resources.

## Linker correction found by clean packaging validation

The first clean b249 build compiled all 1416 C files, then rejected the initial
static ELF: the old 16 MiB alignment advanced the data image to `0x0a00da00`.
BSS had crossed `0x09000000` but still ended before the required image address.
The linker now sets the location counter to `ABSOLUTE(0x0900da00)`, preserving
the existing runtime data map and the padding inside the same section. Relinking
the actual b249 objects confirmed `p4_image = 0x0900da00`; the address assertion
remains enabled. The source-build ZIP includes this correction.

## Host checks

- The frozen Windows runtime was rebuilt with Python 3.14 and PyInstaller.
- The final frozen EXE passed `--check --probe` on the original USA ISO: Tk 8.6,
  PyAV 17.1.0, all five disc containers, ADX and AHX samples, and a 30-frame
  H.264 conversion of P4CT020.
- The final corrected package completed a clean `--cli --build-only` run with exit
  code 0: all 1416 C files compiled, the initial ELF and release PRX linked, 19 PSP
  import libraries passed validation, and no function pointer lacked a native target.
  EBOOT and its matching data passed the builder and installation hash checks.
- The package inventory and every source payload hash are checked against the ZIP.
- All published Markdown is checked for absolute Windows paths, network paths and
  home-directory paths by `tools/test_public_docs.py`. Examples use relative paths
  or named environment variables; PSP device paths remain valid.
- Preparation, source-build, audio, video and audio-handoff regression suites pass:
  26 tests, plus the documentation path check.
- Broader discovery with the PSP SDK and host dependencies configured ran 65 tests:
  61 passed, one skipped and three errored. Two `test_render_state.py` C harnesses
  lack declarations for current renderer helpers; the `test_vu0.py` quad-memory
  harness lacks the `p4_vu0_site` instrumentation symbol. These are unresolved host
  harness failures; the complete regression suite is not passing.

## Hardware scope

The diagnostic b249 PRX passed one Heaven save -> more than ten minutes with sound
-> title-return movie -> resumed sound run on a PSP at 333 MHz. See
[the audio/video record](audio_video_hang.md) for timing, errors and log filenames.
That result does not validate the utility's generated XMB EBOOT or a complete
playthrough. The current screen-size culling default can make distant characters
appear late; see [performance status](perf_status.md).

## Artifact identity

The checked utility ZIP SHA256 is `bef43efbacf7564d338cc236fea59a76ebbfa8b054bc238e15416f35e1d1161b`. The source configuration
SHA256 used by the clean build is `5d70931483d5afa2910b0f8e8a110c4f5f4eba67a34468d413a7eb3b150df092`.

| Locally generated file | SHA256 |
| --- | --- |
| `EBOOT.PBP` | `9b11eb05925dc5b33d360b2b07264c0c09b7db549a7f4e33ae7b7cf39257aba7` |
| `p4data.bin` | `8a551ef0fe5409006a535c76d27dbc8538590769275c8b33cf1eec54e6ad7536` |
| `p4str.bin` | `4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4` |

The utility still reports these launch files as `unverified` because no b249
reference profile is embedded. That message is separate from successful host
compilation and hash validation. It correctly requires PSP testing. Full audio
and all-movie preparation were not repeated; only the frozen conversion probes
and launch-file build were run for this publication. Generated game outputs
remain local and are not release assets.
