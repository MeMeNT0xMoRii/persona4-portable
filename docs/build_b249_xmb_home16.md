# Home16 investigation and qualification

The starting point and rollback are [home15](build_b249_xmb_home15.md)
at `84ad238704fe6a08135cc1d8be55145f54f6b8ba`. Keep the user’s original
saves and prepared US `SLUS_217.82` resources backed up. The supported
hardware is PSP Go and PSP-3000 with 64 MB, at 333 MHz with sound. The
optional `FPS` file beside `EBOOT.PBP` enables the overlay on the next boot.

## P4-057 · Persona fusion

Home15 crashes on the tester’s PSP Go after selecting the second Persona.
The tester reproduced it from source slot 10, an endgame save and a New
Game+ save. The original crash address and exact pairs were unavailable.

In PPSSPP 1.20.4, home15 loaded a copy of source slot 10, opened the blue
door in the Entrance Hall, and completed the following Normal Spread:
Izanagi (Lv 4), Ukobach (Lv 3), then the Apsaras (Lv 4) preview. Cross
confirmed the preview and the prompt about the unavailable Temperance
Social Link bonus. The animation ran, Apsaras appeared and the game
returned to the Velvet Room menu. Margaret’s Register Persona command
reported that no held Personas required registration. This run did not
independently check Apsaras in the Compendium list or reload a post-fusion
save. The strict invalid-address setting was enabled. A second replay
reached the same second-selection path while the debugger watched
alignment-sensitive instructions in the fusion and preview functions.
The inspected path did not identify the PSP exception.

The PSP Go crash remains open. Slot 15 loads to the 07/28 TV hub and
slot 16 to Yomotsu Hirasaka Path 9 on 03/20. A matching late fusion and
independently identified NG+ replay remain open. Use untouched copies for
both. Save to a free slot after acquiring and checking the resulting
Persona. On a crash, copy `p4.log` before booting again and record the pair,
fusion type and last screen. A PSP exception address, if available, lets
the PSP ELF distinguish the renderer, asset loading and game logic.

## P4-032 · Shuffle Time faces

The home15 PSP Go clip `IMG_7250.mp4` shows the blue backs at the prompt,
during selection and on the reward screen. A published capture of the
original PS2 game shows two cracked cards and two illustrated cards at
Start/Decline. The cracked design is the face of a blank card. [PS2 capture](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351286/)

In `CARD/PSC_BASIC.BIN` on the US disc, entry 7 is the blue back and entry
1 is the gold frame. `pscModelDraw` draws the frame and illustrated layers,
then uses `pscResGetBaseRaster` for the back. The back’s index winding is
opposite the face’s. The retail PS2 code requests `rwCULLMODECULLBACK`.
The PSP immediate-mode path ignored that state. The home10 visibility
guard affected the frame’s draw. The single-card bonus reveal also contains
separate non-indexed quads with opposing winding.

The local experiment restores the frame draw and applies culling during
the scoped Shuffle Time calls. A GE probe in PPSSPP rendered the expected
face before and back after a half turn on the same pair of oppositely wound
quads. The host test checks that culling stays scoped and clears after each
draw. The probe does not exercise a Shuffle Time reward. Gameplay testing
must show the faces, choice and reward with the candidate itself.

## Other comparisons

| ID | Observed | Remaining check |
| --- | --- | --- |
| P4-051 · Town Map | User confirmed home15 on PSP Go at 333 MHz. Original US PS2 ran in Play! from a copy of source slot 9. School and Shopping District show the original town, terrain and destinations. The home15 512 × 512 atlas path stays in the branch. | Reopen and switch destinations on the candidate, then replay on PSP-3000. |
| P4-022/P4-055 · Castle | Home15 PPSSPP entered the Gate, received Goho-M, reached 1F and entered a normal battle. `IMG_7249.mp4` still shows the distant white arch on PSP Go. | Match the long corridor and camera distance on US PS2. Repeat entry and exit across cold boots for the earlier intermittent crash. |
| P4-015 · Garu cut-in | `IMG_7251.jpeg` shows the eye strip low on the PSP. The English PS2 clip shows the purple ground ring and Garu effect against Lying Hablerie. It does not show the same weakness cut-in. | Match the enemy weakness and the frame while preserving the existing PSP aspect ratio. [PS2 Garu footage](https://www.jeuxvideo.com/extraits-videos-jeux/0002/00020458/persona-4-playstation-2-ps2-coup-special-garu-00001047.htm) |

## Test environment

The complete user-supplied US disc was read into the test installation.
Its original executable matches the project’s expected SHA-1. The archive
index covers 13,072 unique paths. All 17,375 indexed audio resources were
prepared locally and tested only where the specific replay says so. The
PSP run below uses PPSSPP 1.20.4 with OpenGL at 1×. The game’s reported
FPS describes the emulated presentation rate. PSP Go and PSP-3000 device
performance and new-build acceptance remain pending.

The local Shuffle Time probe linked 825 inputs and passed PSP import-range
validation for 19 libraries. Full host discovery for the probe reported 74
passed, two skipped and two inherited mock compilation errors. The VAG
signal-quality fixture used the native Linux encoder. The failures are
`test_field_noise_removal_keeps_thirty_hz_pacing` and
`test_render_state_and_frame_feedback`. The Shuffle dispatch, culling and
Town Map swizzle fixtures passed.

For the remaining playthrough coverage see the [central issue register](playtest.md#issue-and-regression-register)
and the [replay inventory](replay_saves.md). P4-057 and P4-032 are hardware
playthrough blockers.
