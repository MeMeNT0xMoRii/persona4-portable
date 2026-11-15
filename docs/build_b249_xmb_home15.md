# b249-xmb-home15 validation

Base and rollback source: home14 at `2aaa1346efc6808a27e9b041483eddc6caa8a6b7`.
Use the [home14 package](https://chatgpt.com/library?file_id=libfile_cd62f4dfe0bc81919f757d3ea65b2c03&account_id=personal)
for rollback. Preserve the prepared US (`SLUS_217.82`) archives and back
up `saves/` before replacing all three launch files together. The test
targets PSP Go and PSP-3000 at 333 MHz with 64 MB RAM. The empty `FPS`
file next to the EBOOT continues to enable the overlay on startup.

## Changes and evidence

P4-051. The primary Inaba atlas `f001_001_01` in `F001_001.P01` is a
RenderWare version-1 indexed texture. Its chunk has 1,048,576 swizzled
indices and a 1,024-byte palette and no GIF upload header. Home14
explicitly filled the raster with zeroes. Home15 reads the original
palette and decodes the indices directly to 512×512, the maximum PSP GE
texture size per axis. Normalized UVs, geometry, camera, field of view,
the existing 480×272 layout and menu draw order are preserved. The
indexed raster is 768 KiB smaller before padding, allocator overhead,
transient buffers and any generated mip levels.

The controlled PPSSPP replay used the original US data, 1× internal
resolution and a copy of source slot 9. Space in the profile was
isolated from original saves. From Class 2-2, Square → Go into town
showed the home14 defect with a working destination UI. The patched
draw shows the town and terrain in the same camera pose and with
Central Shopping District selected. Palette colours, icon and
destination preview remained readable. The public
[original PS2 map capture](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351271/)
shows the corresponding scenery at a different calendar date. A
Play! 0.70 run booted the original US disc and rendered the intro.
Matched PS2 gameplay poses are still pending.

The labeled home15 EBOOT also passed School and Shopping District
views on PPSSPP 1.20.4, with the map still textured on reopening from
Shopping District South. The area transition and bidirectional walking
past the South bus stop were exercised. The sampled walk showed no
dark field polygons. The matching school corridor walk is pending.

The native-texture test runs as part of host discovery and checks the
1024→512 conversion, output stride and GE swizzle round trip. Host
discovery returned 72 passes, two skips and the same three inherited
errors as home14. The failing fixtures are the Windows-only VAG
compiler invocation and the two stale render/pacing mocks.
A separate native Linux encoder run passed the VAG signal-quality
fixture. The PSP build compiled from the pinned upstream sources and
linked 825 staged inputs with 19 PSP import libraries. Its address
audit covered 5,857 rebased entries and 168 switch entries with no
proof errors or unresolved native function-pointer targets.

## Regression matrix

The [central issue register](playtest.md#issue-and-regression-register)
holds the full history and detailed reproduction steps where known.
The priorities here determine hardware acceptance order.

| Priority / IDs | Replay and expected result | Latest evidence / next check |
| --- | --- | --- |
| P0 · P4-051 | Slot 9. In town map choose School then Shopping District, exit and reopen. Roads, buildings and landscape remain visible. | Home15 fixed in the PPSSPP A/B at both selections. The labeled candidate also passed reopening from Shopping District South. Check both selections, travel and reopen on PSP Go and PSP-3000. |
| P0 · P4-055/P4-022 | Slot 10. Ask Teddie to search for Yukiko. Enter Castle 1F, approach the white arch and repeat entry and exit after cold boot. | Home15 PPSSPP reached Castle Gate from the TV hub and read both gate packs. The prior PSP Go crash was intermittent. The corridor video shows a white arch that gains detail as the party nears it. Verify on the same path and match PS2 lighting. No crash-specific fix claimed. |
| P0 · P4-043/P4-054 | Slot 9. Move camera with the character near the South bus stop and in the Classroom Building 2F corridor. | Home13 PSP Go eliminated the prior black field polygons in the tested scenes. Home15 PPSSPP sampled movement near the South bus stop without dark polygons. Keep the school corridor and device checks open. |
| P1 · P4-032/P4-045 | Slot 10. Win a normal encounter, observe the preview, first turn and selected reward reveal. Check blank and Persona cards. | Home14 PSP Go report still has missing art. Retail PS2 screenshot shows face-up illustrated cards at Start/Decline. Surrounding display and reward logic need a matched replay. |
| P1 · P4-050/P4-052/P4-056 | Slots 8/9. Open System, Item, Equip and Persona and change selections. Compare Social Stats and System decor at matching phases. | Home14 emulator showed aligned System and Item highlights and Golf Club help. The US executable draws the System stripe layer. Social Stats and exact System framing remain open. |
| P1 · P4-004/P4-006/P4-007/P4-018 | Slots 3–6 and 8. Replay day-change, rainy school route, question-mark overlays and entry/return through the TV. | Home15 from slot 8 reached the 04/17 → 04/18 transition and showed a legible MON 18 in the sampled animation frame. Other phases, rain and TV transitions remain open. |
| P1 · P4-010–012/P4-015/P4-023–027/P4-030–031/P4-044/P4-046/P4-047/P4-053 | Slots 7/10. Field Shadows, fights, Zio weak point, two- and three-member All-Out, Persona and character levels. | Prior Shadow, portrait, umbrella and level-up results apply only to the reported builds and scenes. Repeat through a natural encounter and the boss. |
| P1 · P4-003/P4-019/P4-029/P4-033/P4-049 | Slot 8. Save to a new slot, restart and reload. Exit with HOME in a quiet scene and after a normal battle. | Home15 PPSSPP loaded Dojima, saved to menu slot 2 in an isolated profile, then selected slot 2 after a fresh boot and logged a successful load. Earlier PSP model results and the separate Shadow Chie HOME hang remain in the register. |
| P1 · New Game / late game | New Game to Dojima, school, town, TV and Castle in sequence. Slot 16 for Yomotsu Hirasaka. Slot 15 for the later TV hub. | Home15 PPSSPP ran Normal New Game into Igor's opening dialogue and loaded Slot 16 to Path 9. Confirming entry loaded the E433_001 scene packs. No full contiguous replay to Castle or NG+ lineage established. |
| P2 · remaining visual and story observations | [Earlier story route](replay_saves.md) and the issue register. Match scene, actor poses, effect phase and camera against the original. | Track per-scene observations through future runs. |

## Performance and qualification

The map format change reduces the retained indexed image size
without changing scene geometry, masks or timing. The observed
emulator `F30.0` is not a hardware performance measurement. Measure
field walk, menu, battle and dungeon transitions on PSP Go at 333 MHz
with sound to compare pacing with home14.

For a repeatable performance pass, capture 30 seconds of the same
movement and camera route per build. Use slot 8 at Dojima Residence,
slot 9 in the classroom corridor, South and the Town Map, slot 10 at
the TV hub and Castle, and slot 16 near the late dungeon boss gate.
Keep the audio, CPU clock and FPS marker settings identical. Record
minimum observed FPS and visible pacing interruptions for each route.

First-pass home15 emulator work was interrupted when the temporary
workspace reverted. The map shots had already been inspected. The
reconstructed code was saved to the branch and the labeled build was
replayed from separate saves. In the second environment the full disc
transfer had incomplete ranges. The replay index included only fully
available source-file sectors and used the checksum-verified original US
executable. Sound and movies were not prepared for that replay. Logs and
screens validate the specific visual and save tests listed above.
Audio, movies and restart/exit behaviour on PSP remain device checks.

The build remains a hardware test candidate while the Castle crash,
white arch and Shuffle Time issues are open. A continuous new-game run
to the Castle and the remaining menu, battle and late-game scenarios
are still outstanding.
