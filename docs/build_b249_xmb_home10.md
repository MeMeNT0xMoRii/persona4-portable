# b249-xmb-home10 hardware test build

The source starts at b249-xmb-home9 commit
`2cdc087731289398e2d7e8010477a0067a2e793c`. The development branch
`playthrough/b176-xmb-home` previously ended at
`48552115e5d82fc2f575d07f73d178f34286606a`, an ancestor of home9.
See the [home9 notes](build_b249_xmb_home9.md) for its replay history.
The update's `BUILD/verification.json` pins the packaged revision and
the hashes of its three launch files.

## Changes and replay targets

| Issue | Home10 change or status | Replay |
| --- | --- | --- |
| P4-032 Shuffle Time | Corrects two renderer paths. The selected-card reveal calls the original non-indexed RenderWare immediate primitive entry, which the PSP dispatch omitted. Persona cards on the regular draw queue now omit the coplanar back after the card turns toward its front. The reward, selection and rotation tracks use the existing game logic. | Slot 10, Entrance Hall → Yukiko's Castle. Defeat a Shadow, accept Shuffle Time and observe the preview, shuffle and selected-card reveal. Compare the visible card with the actual reward. Include Persona, blank and Void outcomes if available. |
| P4-050 Equip | Three draw helpers now accept both packed screen coordinates for category highlights, inventory backgrounds and detail panels. The compiler previously discarded the high word carrying Y. The phone-photo bands and remaining decoration alignment still need a scene comparison. | Slot 8 or 9. Enter Equip and choose the protagonist. Move through Weapon, Armor and Accessory, enter Weapon, then compare Square/Show and back navigation. |
| P4-004 Calendar | Restricts the coloured date glyphs to the selected column and the common grey glyph pass to the space around it. Weather and other calendar primitives keep their earlier paths. | Slot 8, Dojima Residence → upstairs → sleep. Watch the full 04/17 → 04/18 transition and replay slot 4 for 04/14 → 04/15. The day-change video was unavailable. |
| P4-051 Town Map | Open. The screenshot shows intact destination labels over faulty 3D scenery. The US code loads field scene 1, room 1 with a secondary weather room and installs the scene camera and FOV. Verify the missing geometry, weather layer and depth passes against a matched capture before altering the shared renderer. | Slot 9, Yasogami High School, 04/18 after school. Open the location selector and move across destinations. |
| P4-052 Social Stats | Open. The status photo shows long orange lines behind the radar chart. Their intended appearance needs a US PS2 frame comparison. | Slot 8 or 9, Status → Square/Switch info. |
| P4-015 / P4-026 Cut-ins and All-Out | Open. Home9 footage shows all three party portraits. The two-person protagonist portrait, weak-hit eyes and impact brightness need matching shots. | Repeat weak-hit and two- and three-person All-Out sequences. Record the same animation phase and framing. |
| P4-053 Skill lighting | Open. The battle video identifies Zio against Lying Hablerie; it shows a blue effect during summoning and bright flashes in the ensuing combat. The clip alone does not establish a blend or alpha difference from US PS2. | Compare Zio and the All-Out impact separately. |

The six supplied Go photos cover Dojima Residence, both Equip screens, Social
Stats, the Town Map and Shuffle Time. Both videos were reviewed. The longer
fight shows three All-Out portraits, the bright impact and card backs through
the Shuffle Time result. Retail comparisons here use the pinned US executable
and draw routines. A frame-matched visual reference for these scenes was
unavailable.

## Verification

| Environment | Observed result |
| --- | --- |
| Host | Full PSP GCC build and link. 825 staged inputs, 19 PSP import libraries, zero missing native function-pointer targets. Address audit verified 5,857 entries and 168 switch targets with zero proof errors. 67 of 72 host tests passed, two skipped and three reported the same Windows VAG and renderer-mock errors as home9. The packed-coordinate and calendar clip tests passed. The new save files passed the retail summary checksum check. |
| PPSSPP | Home10 gameplay runs pending. The prepared retail game archives and the emulator from the earlier test environment were unavailable. There was no home10 replay for New Game, saves, graphics, audio, movies or exit in this sprint. |
| PSP Go | Home9 user report confirms the three previous menu fixes, save/load, HOME during a regular battle, visible dungeon Shadows with sounds, visible heads in the tested fights, improved All-Out portraits and calendar, and FPS display. Home10 has no hardware run yet. |
| PSP-3000 | Home10 hardware run pending. The PBP still requests extended RAM for the 64 MB devices. |

## Performance and FPS

Use an empty file named `FPS` (uppercase, no extension) beside `EBOOT.PBP`
before boot. Remove it and restart to disable the display. It counts
successful gameplay framebuffer presentations over real elapsed time. It
skips movie playback and does not produce an FPS log.

Home9 Go evidence shows 30.0 when standing at the Dojima save point and
27.0 in a sampled frame while moving in the living room. The user reports
roughly 30 FPS for stretches of the castle, and the battle clip shows FPS
variation between Zio, All-Out and Shuffle Time. Those readings describe
presentation cadence. A short dip can come from draw work, blending,
streaming or waits. Home10 contains no performance-policy change. Existing
`p4.log` summaries expose render/geometry counts, texture waits and skipped
frames. The separate diagnostic build can also measure CPU sections and GE
waits. Use comparable runs for slot 8 standing and moving, slot 9 town and
menus, and slot 10 corridors and fights, with the same clock and audio.
The [performance notes](perf_status.md) include older upstream measurements.

## Installation, rollback and first run

1. Close the game and back up the existing P4 installation, including
   `saves/`. Keep the original private replay ZIP separately.
2. On the prepared US setup (`SLUS_217.82`), replace `EBOOT.PBP`,
   `p4data.bin` and `p4str.bin` together in the existing P4 directory
   (for example `PSP/GAME/CAT_Ports/P4`). Keep its archives, index, audio
   and movies. Verify `b249-xmb-home10` in `p4.log`.
3. On PSP Go, use 333 MHz and sound enabled. Start New Game, check the
   opening including movies and audio, then save to a free slot and reload.
   Replay HOME once in the field and once in battle. Repeat the XMB,
   save/load and exit checks on PSP-3000.
4. To roll back, restore the three matching home9 files. Copy out any
   new saves before restoring the original backup. The save record and
   input mapping were untouched by the home10 changes.
