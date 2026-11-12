# PSP-3000 playtest backlog

Updated 2026-11-12. This records reports from PSP-3000 hardware. The latest
round covers 04/16–04/17, with ten photos and six short clips.

## Tested build and progress

- Hardware: PSP-3000. Exact firmware and active plugins are not yet recorded.
- Edition: original US Persona 4, SLUS_217.82; Windows preparation on the tester's PC.
- Earlier gameplay reports: b165 plus save recovery; save/load explicitly
  reported working. Follow-up on b176-xmb-home1 confirms HOME, save/load,
  all supplied XMB artwork and the exact title on PSP-3000. The installed
  EBOOT has not been rehashed; build attribution follows the update exchange.
- In the earlier run the tester reached Yosuke's Persona awakening, returned
  home and saved. In the new run the footage shows Yukiko's Castle Gate, 1F
  and 2F, Chie's Persona awakening, and the Fool Social Link starting on 04/17.
  A first crash was reported after leaving the TV world (P4-029).
- The exact build and launcher for the new run are unconfirmed. The latest
  delivered build is b176-xmb-home2. A 403 MHz setting was selected for the
  first time. The actual clock during the crash was not measured.
- The original delivered EBOOT SHA-256 is
  `3797d600c3dc29f62476d57f6b043c8bef23cc0119110ba11d204848e9af58cf`.
  The installed PSP file has not been rehashed.

## Open observations

| ID | Scene / trigger | Observation | Evidence / current status |
| --- | --- | --- | --- |
| P4-001 | Logo immediately after Atlus at startup | Large white rectangle instead of a recognizable logo | Repeated report; photo. Logo identity not yet established. |
| P4-002 | Igor's introduction and card animation | White screen flashes | Report; timing needs a replay or video. |
| P4-003 | HOME/PS button during gameplay | No visible response or normal exit in b165 | Resolved in the tester's b176-xmb-home1 follow-up: HOME now works. Separate cancel/confirm and save-in-flight scenarios were not individually described. |
| P4-004 | Calendar/day transition, including 04/15 to 04/16 | Background briefly disappears; large overlapping weather icons and changing background tiles | Repeated report and two photos; compare the full animation with the original. |
| P4-005 | First TV-world eerie-room visit, Yosuke's bathroom scene | Camera angle/foreground occlusion looks wrong | Photo with Chie's “Like I care…” dialogue; compare camera and occlusion separately. |
| P4-006 | Question-mark emotion on characters | Symbol appears at floor level | Previously photographed at Teddie's feet on 04/15. A second photo shows a question mark on the floor during a 04/17 daytime scene. |
| P4-007 | Leaving the TV world after the transition | Brief green screen | Two photos, including a green TV frame and a nearly full-screen green field. Duration unknown. |
| P4-008 | 04/15 school route and 04/16 evening scene with the young detective | Rain is reported missing despite the rainy weather indicator | Photos identify both scenes. Check the animation in a replay. |
| P4-009 | 04/15 school assembly | Protagonist body appears corrupted | Seen in the first run, absent in the replay. Keep open as intermittent; photos show different dialogue moments. |
| P4-010 | Battle action selection | Green tint/overlay across battle scene | Photo. |
| P4-011 | Defeated enemy disappears in a castle battle | Dissolve shows large orange-red rectangular patches | Visible about 4.6–4.9 s into the new clip. Compare the same defeat effect on US PS2. |
| P4-012 | Level-up screen | Black/white bars cover the character image | Level-2 screen photo. |
| P4-013 | Text containing inserted player name or stat value | Excessive spacing after the name or in “knowledge … increased” | Name spacing visible in photo; level text also has a large gap before punctuation. Cause unknown. |
| P4-014 | TV-world liquor-store scene | Briefly wrong camera angle/foreground obstruction | Photo. Keep separate from P4-005 until compared. |
| P4-015 | Attack hits an enemy weakness | Eye/face cut-in appears in the lower half of the screen | Comparison question: the tester is unsure whether this differs from the original. Not a confirmed bug. |
| P4-016 | Triangle-button tutorial in Dojima's living room | Large gap before the triangle symbol | Photo; text layout issue, not a report of triangle input failing. |
| P4-017 | Various early-game scenes | Slowdowns and stutters | Report; no FPS/frame-time measurements or precise triggers yet. |
| P4-018 | Rainy school route and 04/16 evening scene with the young detective | Missing or transparent parts of umbrella canopies | The pale umbrella shows gaps in the latest photo too. |
| P4-019 | Move installation into CAT_Ports and launch from normal XMB | Early exit loading data from fixed paths | Photo: HOME callback ready and boot-memory guard passed, then negative data/list results. b176-xmb-home2 replaces fixed runtime paths with the launch directory. Host checks pass; categorized XMB hardware retest pending. |
| P4-020 | Dojima's house, 04/17 early morning, talking to Nanako | Tall triangular surface beside the protagonist | Photo. Compare foreground objects at the same camera position. |
| P4-021 | Yukiko's Castle Gate, 04/17 daytime, approach the entrance | Red-and-black pattern appears over the side background as the camera moves closer | Two photos at different distances. Check the pattern against the same approach on PS2. |
| P4-022 | Yukiko's Castle 1F, look down the long hallway | Bright white area at the far end | Photo. Compare visibility and lighting from the same position on PS2. |
| P4-023 | Yukiko's Castle 1F, flying Shadow outside combat | Only purple hand or arm shapes are visible in the reported encounter | Photo. Check how early the Shadow is visible when approaching it. |
| P4-024 | Yukiko's Castle 1F, treasure chest near the circular carpet | Chest shows disconnected red panels | Photo. Chest interaction was not reported. |
| P4-025 | Yukiko's Castle 2F, walk around the room before Chie's awakening | Large bright panels appear at the sides near windows as the camera moves | Visible near 2.7 s and 5.7 s in the room clip. |
| P4-026 | All-Out Attack in Yukiko's Castle | Reported distortion in the sequence, with the attack effect washing out much of the scene | Clip captures the transition and attack. Compare the pattern and brightness with the same move on PS2. |
| P4-027 | Chie's Persona awakening | Blue and white light effects look wrong to the tester | Clip shows the aura and Chie taking on a bright cyan tint. Compare intensity and timing with PS2. |
| P4-028 | Fool Social Link starts, 04/17 daytime in the TV-world hub | Card height looks too low and the card overlaps Teddie | Photo. Compare the same animation frame with the US PS2 version. |
| P4-029 | 04/17, after returning from the TV world following Chie's awakening | First reported crash at the "Daytime" loading screen | Photo shows the loading icon. One occurrence with 403 MHz selected. Effective clock, response to input, and failure point are unconfirmed. Capture the session log and replay at 333 MHz. |

## Next development checks

1. P4-029: copy the `p4.log` from the affected session if available. Disable the
   403 MHz override and retry from the same save at 333 MHz with sound on,
   keeping the same build and launcher. Record whether the load finishes,
   the loading icon still moves, or the system exits the game. The port
   requests 333 MHz at startup. Treat the effective clock as unmeasured.
2. Inspect the final log entries from that session. A `video task begin`
   followed by `video init au` and no `video decode 0` or `video task end`
   makes the known b176 decoder hang worth checking. If the session log
   contains no movie start near the failure, inspect the scene transition and
   resource loading first. The log writer buffers entries, so a missing line
   alone cannot establish the failure point.
3. Verify categorized-folder startup (P4-019) with b176-xmb-home2 and check
   HOME and save/load on that setup. The home2 log records the launch directory.
4. Compare the 1F Shadow, chest and the 2F room with the same US PS2 scenes.
   Check object visibility and interaction first, then the placement and
   rendering of battle, light and card effects.

## Branches and prepared XMB change

- `playthrough/b165-tested`: source baseline matching the tested save-fix code.
- `playthrough/b165-xmb`: baseline plus host-test repair, supplied XMB artwork,
  exact release title **Persona4 PORTABLE**, and these notes.
- `main`: upstream b176, commit `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`
  at fork setup; retained unchanged.
- `playthrough/b176-xmb-home`: b176 plus save recovery, supplied artwork,
  **Persona4 PORTABLE** title and HOME exit support. Selected explicitly by
  the tester for the next build. Sound remains enabled. Earlier rendering
  reports are from b165. The new 04/16–04/17 run has no confirmed build label.

XMB packaging was checked with actual PSPSDK packaging tools: title and
MEMSIZE verified, all three PNGs recovered byte-for-byte from the expected
PBP sections, synthetic executable payload unchanged. The 16 passing
build/preparation tests and one Windows-only skip are host checks. The XMB
change was initially a packaging-only probe, not a playable build.

On 2026-11-12 the actual **b176-xmb-home1** release EBOOT and matching data
were built. Title, images, executable section, imports and source hashes
were checked again against those real artifacts. The [build record](build_b176_xmb_home1.md)
records 23 passing host tests and one Windows-only skip. HOME exit ordering
was tested during a blocked save-file replacement; this does not exercise
the PSP firmware's HOME dialog. The later tester report confirms working
HOME (P4-003); the rendering reports remain open, with P4-019 added separately.

b176 can hang in `sceMpeg` when starting a movie after around 10 minutes
of play with sound. See the [upstream test notes](next_steps_b112.md#movie-hang-traced-to-the-sas-player-b174-b176-2026-11-10).
The cause of the 04/17 loading failure remains open.

See [XMB presentation](xmb.md) for artwork dimensions and packaging details.

## Latest follow-up: categorized installation

The tester used the ARK5 custom launcher while P4 was under GAME because of
their Categories Lite arrangement. After moving it into CAT_Ports, it appears
in the normal XMB but exits by itself. The startup photo shows the HOME callback
and boot-memory check succeeded, followed by negative data/list load results.
The data error is 0x80010002. Source inspection confirms fixed old install paths.

b176-xmb-home2 resolves the installation directory once from argv[0], with a
working-directory fallback, before starting game threads. Boot data, archive
index/parts, SOUND, VIDEO, NOSOUND, logs and saves use that directory.
Move the entire installation, including saves. No fallback reads an old copy.
Memory-stick paths only (ms0:); unsupported or overlong paths fail explicitly.
Retest XMB startup, existing save load, fresh save/reload, sound, movies and HOME.
The known upstream long-session movie/audio issue is unchanged.
