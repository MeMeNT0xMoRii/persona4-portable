# b176-xmb-home1 test build

Built on 2026-11-12 for the existing US installation on PSP-3000.
This is a compiled test candidate, not a hardware-validated release.

## Source and changes

- Base: upstream b176, commit
  `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`.
- Pinned external decompilation: `363eab8a966d60a9773b45f330f2344329ec4f0a`;
  canonical source-tree SHA-256
  `b1a5681e7b30280b558c2deb438b2815b414eee4e6a59da55e551778ead45bc0`.
- Original US executable SHA-1:
  `4eeec0360cf2715535d9f7e52eb69d786fb0158c`.
- Preserve the custom slot-creation fix: a committed `.bak` save must not
  be hidden by a newly created empty primary file.
- Exact XMB title **Persona4 PORTABLE**, with the supplied ICON0, PIC0 and
  PIC1 PNGs embedded unchanged. Installation and saves remain under
  `PSP/GAME/P4`.
- Register a HOME exit callback on a dedicated callback-aware thread.
  Registration failures are reported at startup. After confirmed exit,
  reject new card commands and let the active synchronous command complete
  before syncing the memory stick and calling `sceKernelExitGame`.

The card guard covers individual file commands, not a whole multi-command
game save transaction. Finish saving before selecting HOME -> Yes.
HOME does not add autosave. No rendering fixes are claimed for this build.

## Why b176

The tester explicitly requested b176 after reviewing the
[upstream release notes](https://github.com/dreikk91/persona4-psp/releases/tag/b176).
It adds skinning optimizations, animation LOD for distant stationary
characters, a larger available mesh-cache budget, MPEG-session reuse and a
release-link fix. b175 also corrected body/shadow doubling introduced by
the b174 animation experiment; that is not proof that our b165 assembly-hall
report is fixed.

The quoted improvement from over 100 to about 50 ms/frame chiefly compares
profiling builds with an expensive debug bone check. It does not promise a
twofold speedup over the tester's b165 release build.

Known upstream issue: after roughly 10 minutes playing with sound, the next
movie can hang inside `sceMpeg`. The upstream `NOSOUND` workaround disables
SAS audio and is not enabled in this build. The release notes mention a fix
planned for b177; no such fix is imported or claimed here. Upstream tested
b176 profiling builds in selected PSP E1000 scenes, not the release EBOOT
produced by its preparation tool. Neither upstream nor this project has
validated a complete playthrough.

## Host build and verification

- Built with native Linux PSPDEV, PSP GCC 15.2.0. The build scripts' `.exe`
  tool names were mapped to native tools using local symlinks; no source
  compatibility workaround was needed. This is not a claim of binary
  equality with the upstream Windows toolchain or an older delivered EBOOT.
- All 1,416 decompilation units compiled with zero failed files. These
  objects were generated before the base switch; the census script, VU
  translator, compatibility headers and pinned input are identical between
  b165 and b176. Every PSP port module was then compiled from b176 plus our
  changes. No b165 renderer, audio or video objects were reused.
- Pointer proof and initial link completed. The address audit found zero
  mismatches in 5,857 rebased entries and 168 switch targets. Its remaining
  numeric/address candidates are investigation leads, not proof that all
  retained PS2 hardware accesses work on the PSP.
- Final PRX link validated 19 import libraries and reported zero function
  pointers without native targets. HOME registration, callback-aware wait
  and exit imports are present.
- 23 host tests passed; one Windows-process-flags check was skipped on
  Linux. The HOME test covers eight scenarios: six startup failure paths,
  idle exit and exit requested during a paused save-file rename. In the
  last case, exit waits, new I/O is rejected, the replacement completes and
  both the new primary save and previous backup retain the expected bytes.
- Actual EBOOT checks: title, MEMSIZE=1, all three image sections and the
  embedded PRX match the expected inputs. Source and artifact hashes match
  the build manifest. Sound is enabled by default.

The HOME test uses a pthread-backed kernel stub. It does not validate the
actual PSP HOME menu, firmware scheduling, cancelling exit, real memory-card
behavior, boot, graphics or save compatibility on the device. Those checks
remain for the tester. No PSP or emulator execution was performed here.

## Matching installation files

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| EBOOT.PBP | 5,012,610 | `ab90747856db59fc6fd25ae2979a587223efe676d6b81e140ada2540a888403c` |
| p4data.bin | 2,320,512 | `5a4f99fe5f86d1ddf57ca032b064aa326c425343e10d7f23ff196acbe2bd9eaa` |
| p4str.bin | 9,904 | `4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4` |

These three files are one set. The private update package requires the
tester's existing complete P4 installation; existing archives, SOUND and
VIDEO remain in use. Generated game-derived files are not committed here.

## Installation and first hardware test

1. Exit the game and back up the existing `PSP/GAME/P4` folder to the PC,
   including `saves` and the old three launch files.
2. Copy all three update files into `PSP/GAME/P4`, replacing that set together.
   Keep the folder name `P4` and existing resources. Do not delete the game
   folder or copy an empty save directory over the current saves.
3. Start **Persona4 PORTABLE** in XMB and check the icon/background/info image.
4. Load the current save, save in another slot and wait for saving to finish.
5. Press HOME and cancel once; gameplay should resume. Press HOME again and
   confirm exit; XMB should return. Restart and load the newly saved slot.
6. Continue toward Chie's Persona/Yukiko's Castle. Check a movie after more
   than ten minutes with sound; record the scene and preceding playtime if
   it hangs. Keep the matching log before starting the game again.

For rollback, restore the backed-up matching EBOOT/data trio. Keep the save
backup separately; backwards compatibility of saves written by the new build
has not been established on hardware. The b165 source baseline remains on
`playthrough/b165-tested`.
