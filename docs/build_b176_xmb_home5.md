# b176-xmb-home5

The PSP Go home4 playthrough reached the 04/17 evening save at Dojima
Residence after the first visit to Yukiko's Castle, Shadow Chie and the
Fool Social Link. HOME returned to XMB after the save. The daytime
Shopping District save before Junes is the short route back to the
castle. The [playtest backlog](playtest.md) records the individual reports
and the [save plan](replay_saves.md) covers the subsequent dungeon tests.

## Changes in home5

| Reports | Change | Evidence |
| --- | --- | --- |
| P4-001, blank CRI logo | The indexed 1024 × 512 CRI texture in the US startup archive exceeds the PSP's 512-pixel texture dimension limit. Both TMX paths now limit the upload dimensions. The file decoder reduces the linear image before its upload. The packed uploader reduces the indexed or direct-colour data before swizzling, preserving the palette and normalized coordinates. | PPSSPP 1.20.4 reproduced the white rectangle on home4. On home5 it displayed Atlus followed by the readable CRIWARE logo with both the software renderer and OpenGL. The next PSP cold boot is the hardware check. |
| P4-010 and P4-012, battle menu tint and level-up portrait cover | `btlPanelDrawBackdrop` and the results renderer use GS `TEST_1 = 0x32801`, an alpha failure that writes depth only. The PSP 2D path had painted the RGB of these rectangles. It now implements those writes and the other GS alpha-failure modes, preserving the existing masked alpha writer for the title. | A host GU trace checks the masks, pass state and restoration. A PSP test program built with the same helper produced identical results on PPSSPP software and OpenGL. The green, black and white rectangles left the existing colour intact and clipped the boxes drawn behind them. The 04/17 scenes still need replay. |
| P4-013, gaps in stat messages and around inserted names | Font-list offsets and glyphs used different horizontal scales. The font-list patch now advances on the same scale as glyphs, including fractional positions at run boundaries. | A host regression checks mixed-width runs and positive and negative anchors. Replay Teddie's item gift, the Fool message and Persona stat gains on 04/17, or the classroom Knowledge message from an earlier save. |
| P4-034 and P4-036, scripted poses | Keep animation updates active for distant actors and when rendering skips frames. This removes two ways for a scripted pose to hold. | The emulator trace shows no held distant animations or skipped bone steps. Compare the gas-station exit, Nanako's sitting and remote motion, and Yukiko's classroom turn. Measure frame pacing on PSP at 333 MHz. |

## Build and checks

- The release uses PSP GCC 15.2.0 and the pinned US decompilation snapshot.
  The font and TMX objects were regenerated for the new shims before linking
  with 825 staged inputs. The link checked 19 PSP import libraries and
  found zero function pointers without native targets.
- The selected host suite ran 60 tests, with 58 passes and two
  platform skips. Two standalone C clipping checks also passed. Two
  pre-existing renderer cases failed to compile their stale mocks on
  the home4 baseline, and the VAG round-trip case requires the Windows C#
  compiler. Those three cases are outside the selected run.
- The emulator used the prepared US archives and a fresh save. It booted
  through the logos, presented the title and Load menus and entered the
  New Game introduction. PPSSPP returned an unimplemented
  `sceMpegGetAvcNalAu` call on the first staged movie and the game proceeded.
  Audio and the 04/17 comparison saves were unavailable in this run.
- The update contains the three matching PSP files, the ELF, build
  provenance and checksums. It uses the existing prepared game directory.

## Repeat the depth-mask check

With a native Linux PSPSDK toolchain and PPSSPP 1.20.4 installed, run the
standalone software-renderer test. The Python runner also needs Pillow
for its shared debugger module.

```bash
PSPDEV=/path/to/pspdev python3 tools/psp_render_smoke.py /path/to/PPSSPP/AppRun
```

The test compiles the port's 2D draw helper into a small PSP program and
checks nine framebuffer pixels. It covers the green command-panel quad,
the black and white result quads, and the depth cutout. It uses an
isolated emulator configuration and needs no game archives or saves.
The game route also reached the gas station on 04/11 using scripted
inputs. The 04/17 game scenes still need a replay.

## Next replay

1. Cold boot on the PSP and watch the second logo. Use 333 MHz and the
   same categorized installation as the home4 comparison.
2. Load 04/17 daytime in Shopping District. Check the tint while choosing
   an action and the portrait during level-up. The most useful text checks
   are Teddie's item gift, Persona stat gains and the Fool message.
3. Compare the first castle visit for battle entry and defeat, missing
   heads, All-Out Attack portraits and models, command labels, chest and
   Shadows. The green TV transition is tracked separately as P4-007.
   Question marks, calendar graphics, umbrellas and the light effects
   remain open.
4. Keep the daytime slot for replays and continue the main playthrough
   from 04/17 evening. Save at the TV-world hub butterfly on the next
   free castle visit and at the last save before the boss.
