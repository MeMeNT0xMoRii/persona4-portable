# b249-xmb-home16 hardware diagnosis

The starting point and rollback are [home15](build_b249_xmb_home15.md)
at `84ad238704fe6a08135cc1d8be55145f54f6b8ba`. Keep the user’s original
saves and prepared US `SLUS_217.82` resources backed up. The supported
hardware is PSP Go and PSP-3000 with 64 MB, at 333 MHz with sound. The
optional `FPS` file beside `EBOOT.PBP` enables the overlay on the next boot.

The branch is `playthrough/b249-home16`. This build is for hardware
diagnosis. P4-057 remains open on PSP Go and P4-032 needs device acceptance
before a full New Game playthrough. The package records the exact commit
and hashes for `EBOOT.PBP`, `p4data.bin` and `p4str.bin`.

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
reported that no held Personas required registration. An independent check
of the Apsaras Compendium entry remains open. The strict invalid-address
setting was enabled. A second replay
reached the same second-selection path while the debugger watched
alignment-sensitive instructions in the fusion and preview functions.
The inspected path did not identify the PSP exception.

The slot 10 replay with the prepared audio archive also reached Apsaras
acquisition and showed Apsaras in the held Persona list. It saved at the
Entrance Hall save point in a new row. A fresh PPSSPP process running
home16 loaded an isolated copy of that save and displayed Apsaras Lv 4
in the protagonist's Persona list. The original slot 10 bytes were verified
against the supplied replay. A separate
home15 replay from source slot 15 entered the Velvet Room from the TV hub
and fused Rakshasa (Lv 25) with Jack Frost (Lv 19). The Power preview,
animation, Power acquisition dialogue and several level-ups from the
Rank 10 bonus were visible. Slot 16 loads to Yomotsu Hirasaka Path 9 on
03/20. Its fusion route and an independently identified NG+ replay remain
open.

The two PPSSPP outcomes leave the PSP Go crash open. Reproduce with
untouched copies, then save to a free slot after checking the resulting
Persona. On a crash, copy `p4.log` before booting again and record the pair,
fusion type and last screen. A PSP exception address can help resolve the
failing instruction against the PSP ELF.

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

Home16 restores the frame draw and applies culling within the Shuffle Time
draw scope when the retail state requests it. The previous field-shadow
dispatch still applies outside that scope. A PPSSPP GE probe showed the face
at zero rotation and the back after a half turn. The host test checks the
indexed and non-indexed paths and the state at the next draw.

PPSSPP gameplay began with a copy of source slot 10. Enter Yukiko's Castle,
receive Goho-M, walk into 1F and fight the two Lying Hableries around the
first right turn. The test used PPSSPP 1.20.4 OpenGL at 1× and enabled the
strict invalid-address setting. One unmodified victory reached the results
with no Shuffle Time roll. To compare the two renderers in the same
encounter, the debugger set the five battle victory roll chances to 100 in
temporary guest RAM at `0x09243aa0`. The normal deck generator, inputs and
reward path ran. The adjustment was confined to the emulator session.

| Phase | Home15 in PPSSPP | Home16 renderer in PPSSPP |
| --- | --- | --- |
| Start/Decline | Six blue backs | Gold-framed Persona art, cracked blank card and red crossed card |
| Shuffle | Blue backs during the moving stop phase | Moving cards turn back-up during the stop phase |
| Stop and reveal | A blank reward still shows the blue back | Slime is visible on the selected card, with the matching reward dialogue |
| Accept reward | Blank outcome in the recorded run | Slime appeared in the Persona list after the acquisition message, battle results and return to 1F. |

The tested renderer build and the packaged build differ by one byte in the
EBOOT boot label. Their data and string images match byte for byte. PSP Go
and PSP-3000 replay and the rarer bonus-reveal sequence remain open.

## Other comparisons

| ID | Observed | Remaining check |
| --- | --- | --- |
| P4-051 · Town Map | Home15 passed on the user's PSP Go at 333 MHz. Play! ran the original US PS2 from a copy of source slot 9 for the school and Shopping District. Home16 PPSSPP loaded slot 9, showed the terrain and Shopping District destination, travelled to Shopping District South and reopened the map from South. The destination panel also switched to Samegawa Flood Plain. | Repeat on both PSP models. |
| P4-022/P4-055 · Castle | Home15 PPSSPP entered the Gate, received Goho-M, reached 1F and entered a normal battle. `IMG_7249.mp4` still shows the distant white arch on PSP Go. | Match the long corridor and camera distance on US PS2. Repeat entry and exit across cold boots for the earlier intermittent crash. |
| P4-015 · Garu cut-in | `IMG_7251.jpeg` shows the eye strip low on the PSP. The English PS2 clip shows the purple ground ring and Garu effect against Lying Hablerie. It does not show the same weakness cut-in. | Match the enemy weakness and the frame while preserving the existing PSP aspect ratio. [PS2 Garu footage](https://www.jeuxvideo.com/extraits-videos-jeux/0002/00020458/persona-4-playstation-2-ps2-coup-special-garu-00001047.htm) |

## Test environment

The complete user-supplied US disc was read into the test installation.
Its original executable matches the project’s expected SHA-1. The archive
index covers 13,072 unique paths. All 17,375 indexed audio resources were
prepared locally. The tests ran with an emulated audio device. Video files
were skipped at boot for the repeatable map replay. The displayed FPS
describes emulated presentation. PSP Go timing and a continuous home16
playthrough remain open.

The PSP XMB build linked 825 inputs and validated 19 import libraries.
The link had zero function pointers without native targets. The address
audit checked 5,857 rebase entries and 168 switch entries without
mismatches. Host discovery reported 74 passed, two skipped and two
inherited mock compilation errors. The VAG signal-quality fixture used the
native Linux encoder. The errors also occurred on the home15 sources:
`test_field_noise_removal_keeps_thirty_hz_pacing` and
`test_render_state_and_frame_feedback`. The Shuffle dispatch, culling and
Town Map swizzle fixtures passed.

| Platform | Qualification |
| --- | --- |
| Host | Link, import/address checks and focused graphics regressions passed. Full-suite exclusions and errors are listed above. |
| PPSSPP | Slot 10 battle, three-member All-Out, Shuffle Time reveal and reward. After return to 1F, the Persona roster, Equip category and weapon list with Square help, System Quest text and both Status views opened. After the Apsaras reload, Item highlighted Peach Seed and showed the correct help. Slot 9 covered Town Map, travel and Shopping District South. |
| US PS2 | Town Map replay in Play! and published Shuffle Time prompt. No matched Castle corridor or Garu weakness cut-in. |
| PSP Go | Home15 user findings only. Home16 device acceptance pending. |
| PSP-3000 | Home16 device acceptance pending. |

## Install and roll back

1. Exit the game and make a copy of the existing P4 folder and `saves/`.
2. Copy the three build files together into the prepared US game folder.
   Keep the original game resources, sound and video archives beside them.
3. Start from the XMB at 333 MHz with sound. Check the home16 line in
   `p4.log`. Use an empty `FPS` file beside `EBOOT.PBP` when measuring.
4. On PSP Go, start with fusion from slot 10 and then use the same endgame
   and NG+ saves where the failure was seen. Then replay 1F Shuffle Time
   and record Start/Decline, the chosen face and the reward. Reopen Town
   Map from slot 9 and after travelling to Shopping District South.
5. For rollback, back up any new saves and replace the three build files
   together with the [home15 package](build_b249_xmb_home15.md).

For the remaining playthrough coverage see the [central issue register](playtest.md#issue-and-regression-register)
and the [replay inventory](replay_saves.md). P4-057 and P4-032 are hardware
playthrough blockers.
