# PSP hardware playtest backlog

Updated 2026-11-13. The [home4](build_b176_xmb_home4.md) playthrough reached
the 04/17 Shopping District save before Junes on PSP Go. The fifth reported
HOME exit on home4 succeeded after that save. The earlier
intermittent HOME freeze remains open (P4-033). The 333 MHz replay of the
04/17 TV-world return reached the next save on home2.

Use the [replay saves by in-game date](replay_saves.md) to revisit the
reported scenes. The next home4 replay starts from the 04/17 Shopping District save.

## Tested build and progress

- Hardware: PSP-3000 for the earlier reports, plus a PSP Go home3 exit test.
  Exact firmware and active plugins are not yet recorded.
- Edition: original US Persona 4, SLUS_217.82; Windows preparation on the tester's PC.
- Earlier gameplay reports: b165 plus save recovery; save/load explicitly
  reported working. Follow-up on b176-xmb-home1 confirms HOME, save/load,
  all supplied XMB artwork and the exact title on PSP-3000. The installed
  EBOOT has not been rehashed; build attribution follows the update exchange.
- The earlier playthrough reached Yosuke's Persona awakening, then Yukiko's
  Castle Gate, 1F and 2F, Chie's Persona awakening and the 04/17 Fool Social
  Link. A crash was reported after leaving the TV world (P4-029).
  The home4 new-game run has reached the 04/17 Shopping District save.
- The tester confirmed normal XMB launch from `CAT_Ports`. The PSP Go
  log identifies b176-xmb-home3 under `ms0:/PSP/GAME/CAT_Ports/P4/`.
  A 403 MHz setting was selected for the first time. The actual clock
  during the crash was not measured.
- The original delivered EBOOT SHA-256 is
  `3797d600c3dc29f62476d57f6b043c8bef23cc0119110ba11d204848e9af58cf`.
  The installed PSP file has not been rehashed.

## Open observations

| ID | Scene / trigger | Observation | Evidence / current status |
| --- | --- | --- | --- |
| P4-001 | CRI logo immediately after Atlus at startup | Large white rectangle instead of the logo | Photo. The US startup task loads `title/cri_logo01.tmx` from `title/logo.bin` for the second logo. Reproduced in home4 with a photo. Check its texture decoding and draw state. |
| P4-002 | Igor's introduction and card animation | White screen flashes | Report; timing needs a replay or video. |
| P4-003 | HOME/PS button during gameplay | No visible response or normal exit in b165 | Resolved in the tester's b176-xmb-home1 follow-up: HOME now works. Separate cancel/confirm and save-in-flight scenarios were not individually described. |
| P4-004 | Calendar/day transition, including 04/15 to 04/16 | Background briefly disappears; large overlapping weather icons and changing background tiles | Reproduced after going to bed on 04/12 in home4. Photo shows overlapping weather symbols and rectangular tiles. The transitions into 04/15, 04/16 and 04/17 looked wrong again. The last two clips show overlapping weather symbols. Compare the full transition with the original. |
| P4-005 | First TV-world eerie-room visit, Yosuke's bathroom scene | Camera angle/foreground occlusion looks wrong | Reproduced on 04/14: foreground wall surfaces obscure part of the room and characters. Earlier photo shows Chie's “Like I care…” dialogue. Compare the same camera cut with US PS2. |
| P4-006 | Question-mark emotion on characters | Symbol appears at floor level | Previously photographed at Teddie's feet on 04/15 and seen again on 04/17 and 04/12. In the home4 clip of the 04/13 after-school classroom, the question mark appears at floor height near Chie at about 7.1–7.6 s. The exclamation mark appears above Yosuke at about 3.0–3.3 s. On 04/14 and 04/15, photos show the question mark below Teddie. The tester also reports mispositioned !? on 04/15, a question mark during Yosuke's school-route scene on 04/16 and repeated misplaced question marks at the 04/17 police station after the weapons are confiscated. |
| P4-007 | TV-world entry and return on 04/14 and 04/15 | Green tint or patch in the nested-TV transition | On home4, the entry clip shows green edging during the first second. The return clip shows a bright green center around 1 s, then a white flash. Both transitions show green again on 04/15 and complete. The b176 movie hang is tracked separately. |
| P4-008 | Rainy field scenes, including 04/12, 04/15 and 04/16 | Rain previously missing despite the rainy weather indicator | Visible rain reported on 04/12 and on the 04/15 school route and Samegawa Flood Plain. The 04/16 detective scene was reported rainy. Rain during Yosuke's morning scene and frame pacing remain to be checked. Umbrella geometry is tracked in P4-018. |
| P4-009 | 04/15 school assembly | Protagonist body appears corrupted | Seen in the first run, absent in the replay. Keep open as intermittent; photos show different dialogue moments. |
| P4-010 | Battle action selection, including the opening dream | Cyan-green tint/overlay across battle scene | Reproduced in home4. On 04/15 the green background appears by about 3.5–4 s in the tutorial-battle clip as action selection opens. Also reported during Shadow Yosuke. Compare the same selection state with US PS2. |
| P4-011 | Shadows appear at battle entry or dissolve on defeat | Large rectangular patches in the effect | Defeat shown about 4.6–4.9 s into the earlier clip. Blocky defeat effects recur on 04/15 in the tutorial fight and Shadow Yosuke. Compare battle entry and defeat on US PS2. |
| P4-012 | Level-up screen | Black/white bars cover the character image | Reproduced after both 04/15 fights. The result clips show the cover over the portrait region at about 22.5–24 s and 12–13 s, respectively. |
| P4-013 | Text containing an inserted player name or a stat notification | Excessive spacing around parts of the line | The 04/13 and 04/14 morning classroom photos on home4 show wide gaps on both sides of “Knowledge” in “Your Knowledge has increased.” The 04/15 photo shows a wide gap after the inserted protagonist name. The Persona stat increase text also shows excessive gaps in the 04/15 result clip. |
| P4-014 | TV-world liquor-store scene | Briefly wrong camera angle/foreground obstruction | 04/15 photo shows large foreground surfaces obscuring the view on entry. Compare the shot with US PS2. |
| P4-015 | Attack hits an enemy weakness | Eye/face cut-in appears in the lower half of the screen | Comparison question: the tester is unsure whether this differs from the original. Not a confirmed bug. |
| P4-016 | Triangle-button tutorial in Dojima's living room | Large gap before the triangle symbol | Photo; text layout issue, not a report of triangle input failing. |
| P4-017 | Various early-game scenes | Slowdowns and stutters | Report; no FPS/frame-time measurements or precise triggers yet. |
| P4-018 | Rainy school route, Samegawa Flood Plain and 04/16 evening scene | Missing or transparent parts of umbrella canopies | Intermittent transparency seen on 04/12 and again on 04/15 on the school route and after leaving the TV world. The Flood Plain photo shows exposed ribs and incomplete-looking canopy surfaces. The 04/16 evening photo of Dojima and the young detective shows gaps at the edge of the pale umbrella. |
| P4-019 | Move installation into CAT_Ports and launch from normal XMB | Early exit loading data from fixed paths in home1 | Resolved for the tested PSP-3000 setup in b176-xmb-home2. The tester confirmed XMB launch and gameplay. The log confirms the categorized directory, successful boot data reads and 13,072 indexed files. |
| P4-020 | Dojima's house, 04/17 early morning, talking to Nanako | Tall triangular surface beside the protagonist | Reproduced on home4 in the early-morning Nanako scene. Photo and clips show the surface in front of the protagonist. Compare foreground objects at the same camera position. |
| P4-021 | Yukiko's Castle Gate, 04/17 daytime, approach the entrance | Red-and-black pattern appears over the side background as the camera moves closer | Earlier photos show two distances. The tester reports a visible improvement on home3. |
| P4-022 | Yukiko's Castle 1F, look down the long hallway | Bright white area at the far end | Photo. Compare visibility and lighting from the same position on PS2. |
| P4-023 | Yukiko's Castle 1F, flying Shadow outside combat | Only purple hand or arm shapes are visible in the reported encounter | Photo. Check how early the Shadow is visible when approaching it. |
| P4-024 | Yukiko's Castle 1F, treasure chest near the circular carpet | Chest shows disconnected red panels | Earlier photo. Chest appearance was unchanged in the home3 retest. |
| P4-025 | Yukiko's Castle 2F, walk around the room before Chie's awakening | Large bright panels appear at the sides near windows as the camera moves | Visible near 2.7 s and 5.7 s in the earlier clip. The tester reports an improvement at the door before the Chie fight on home3. |
| P4-026 | All-Out Attack in Yukiko's Castle | Missing character cut-ins and washed-out attack effects | New clip shows a black wedge in the prompt around 7 s and no portraits in the orange transition and exclamation sequence around 8.3–9.6 s. Compare the same sequence on PS2. |
| P4-027 | Chie's Persona awakening | Blue and white light effects look wrong to the tester | Clip shows the aura and Chie taking on a bright cyan tint. Compare intensity and timing with PS2. |
| P4-028 | Fool Social Link starts, 04/17 daytime in the TV-world hub | Card height looks too low and the card overlaps Teddie | Photo. Compare the same animation frame with the US PS2 version. |
| P4-029 | 04/17, after returning from the TV world following Chie's awakening | First reported crash at the "Daytime" loading screen | The 403 MHz setting run logged three completed movies, then setup of `E175_001` / `F007_002`. The tester reached the next save point at 333 MHz on the next attempt. Cause and effective clock during the failed run remain open. |
| P4-030 | Ground Shadows outside combat | Face or mask absent | The home3 retest reports no visible improvement for Shadows. |
| P4-031 | Party members during battle | Heads intermittently missing | Earlier photo of the battle menu. Heads still disappear occasionally in home3. |
| P4-032 | First Shuffle Time | Cards appeared blank | Blank cards and identical backs during shuffling are normal in the PS2 release. Verify whether a front is missing for a card that grants a Persona. |
| P4-033 | HOME exit to XMB | Intermittent freeze | Seen on PSP-3000/home2 and PSP Go/home3. The home3 Go log at a 333 MHz setting reaches frame 21,760. The tester reports a freeze after Yes. home4 waits for an active save to finish and calls the firmware exit. Five successful exits reported on home4, including after the 04/17 Shopping District save. Continue checking after longer sessions. |
| P4-034 | Opening gas station and phone call, 04/12 breakfast and classroom, 04/13 evening at Dojima's | Character motions look wrong when exiting the car, handling the phone and sitting down | The protagonist briefly stands again between sitting poses in class. Other reported cases: Dojima sitting on 04/13; the protagonist reaching into the Junes TV and switching between standing and sitting; Nanako rising on 04/14 evening; the protagonist moving oddly around TV-world entry and return on 04/15. The 04/15 and 04/16 bedroom clips add hand motion before watching TV. On 04/16, Yosuke's handshake and reaching into and withdrawing from the Junes TV were reported. On 04/17, Nanako's hand with the remote and her movement after putting it down were reported. Compare the scripted motions with US PS2. |
| P4-035 | 04/11 evening, Dojima Residence, Nanako dialogue | Dojima stays visible after the sound of his car leaving | Photo shows him still in the room during Nanako's dialogue. Audio timing comes from the tester. Compare actor visibility through the departure with US PS2. |
| P4-036 | 04/13 after school, classroom dialogue | Yukiko's head movement looks wrong | Reported in the clip around the motion when Yukiko stands and turns, about 3.8–4.6 s. Reported again on 04/14 after school when Yukiko stands up. Compare the same sequence with US PS2. |
| P4-037 | First TV-world visit on 04/14, room with posters | Bright area on the right persists through the scene according to the tester | Photos show a yellow-lit room with a sharp bright edge near the bed and wall. Compare the same shots and lighting with US PS2. Foreground obstruction is recorded in P4-005. |
| P4-038 | 04/15 first fight after acquiring the Persona, during Teddie's tutorial | Shadows briefly look flat | The tutorial photo shows flattened silhouettes. In the clip the models look fuller once action selection starts, at about 3.5–4 s. Compare the tutorial pose and rendering with US PS2. |
| P4-039 | 04/15 Shadow Yosuke, battle opening | Boss model or textures look distorted | Startup clip shows overlapping or irregular surfaces as the boss appears, around 10.5–15 s. Compare the same animation with US PS2. |
| P4-040 | 04/15 and 04/16 evenings, bedroom before the Midnight Channel | Curtain rendering or movement looks wrong | Reported in both bedroom clips. Compare the same cuts and curtain motion with US PS2. |
| P4-041 | 04/15 evening, Velvet Room, Igor hands over an item | Margaret's 3D model looks muted or blue-green | Photo shows the model beside Igor. Compare the model colours and scene lighting with US PS2. |
| P4-042 | 04/16 morning, Magician Social Link with Yosuke | The scene washes out around the establishment effect | Bright frames visible at about 26 s and 39 s in the clip. Compare intensity and timing with US PS2. Handshake motion is tracked in P4-034. |

## Next development checks

1. On home4 at 333 MHz, load and save from `CAT_Ports`. Check HOME after
   confirming Yes shortly after loading and after playing and saving. If it
   stalls, keep the log from that session and record the screen state.
2. Use the 04/16 evening scene to compare the umbrella gaps with US PS2.
   For further rain tests record visibility and frame pacing. Look for `TRACE ptank matrix drawn`.
3. Compare All-Out Attack, battle entry and defeat at the same animation
   frames with the US PS2 version. Inspect the skipped destination-alpha
   passes and portrait draws. Replay 04/15 to 04/16 for the calendar and
   capture the second logo after a cold start. Check Shuffle Time fronts
   when a card grants a Persona.
4. Reproduce the same Shadow, chest and missing battle head views. Correlate
   the affected draws with geometry and material state.
5. Compare the opening motions (P4-034) and Dojima's departure (P4-035)
   in the US PS2 scenes. Use the 04/11 evening calendar save for breakfast
   and classroom if available, and New Game for earlier scenes.
6. Replay 04/13 from a save before the school day. Compare the morning
   stat message, Yukiko standing and turning, and the exclamation and
   question marks in the after-school classroom (P4-006, P4-013, P4-036).
7. Replay 04/15 from the previous evening's save. Compare the tutorial
   before and after the action menu appears, Shadow Yosuke's reveal, and
   the results screens (P4-010–P4-012, P4-038, P4-039).
8. Resume on 04/17 from Shopping District. Compare the gate and both floors
   through Chie's awakening, the Fool card and the return to the next save.
   At the return, watch for the previous Daytime loading stall (P4-029).

## Branches and prepared XMB change

- `playthrough/b165-tested`: source baseline matching the tested save-fix code.
- `playthrough/b165-xmb`: baseline plus host-test repair, supplied XMB artwork,
  exact release title **Persona4 PORTABLE**, and these notes.
- `main`: upstream b176, commit `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`
  at fork setup; retained unchanged.
- `playthrough/b176-xmb-home`: b176 plus save recovery, supplied artwork,
  **Persona4 PORTABLE** title and HOME exit support. Selected explicitly by
  the tester for the next build. Sound remains enabled. Earlier rendering
  reports are from b165. home3 has follow-up reports for the castle and
  home4 is the current test build.

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
The tester confirmed XMB startup and gameplay from `CAT_Ports` with home2.
The log records `loaded data=2320512 list=2476` and `disc index 13072 files`
after resolving that directory.

## P4-029 log review

In the final home2 session, `P4CTOP1`, `P4CTOP2` and `P4CT007` each log
`video task end ... result 0`. No further `video task begin` appears before
the log ends. The final section records reads of `E175_001.PM1`–`.PM3`,
`F007_002.P00` and `.P01`, then NPC and texture setup. This puts the recorded
activity in the in-game scene transition. The log ends without an exception
address or a recorded file error, so it cannot identify the failing operation.

The same session logged 18,218 `TRACE guard pool full` messages. The renderer
skips a mesh when the worst-case clipped output would exceed the remaining
per-frame vertex or index space. This is a concrete path that can omit
geometry. Matching it to individual scenes still needs a test. See
[`draw_guarded_chunks`](../port/rwsky.c).

## 333 MHz replay and geometry test build

The 333 MHz replay passed the 04/17 return once. HOME then hung once
after the tester reached the next save point. Relaunching and quitting
worked. The follow-up log appends two home2 sessions. The longer session
records the `E175_001` / `F007_002` transition, a successful save, and
continued frame updates from 18,176 to 20,480 after saving. The HOME callback
does not log entry, acquisition of the card lock, or the result of device
sync. The log therefore cannot locate the exit stall. The successful
relaunch and quit are confirmed by the tester's report.

[b176-xmb-home3](build_b176_xmb_home3.md) tests the renderer change. For each
mesh the guard path now checks what it actually emits when space is tight.
The summary log distinguishes recovered meshes from actual exhaustion.
The same camera and encounter are needed to verify an improvement on PSP.
The longer home2 replay logged 9,996 `TRACE guard pool full` messages as a
baseline for the renderer comparison.

The first home3 exit attempt on PSP Go froze. The next exit succeeded.
This is a second device report for P4-033. The tester did not retain the
failed attempt's log.

In a subsequent home3 retest, the castle entrance and the door before the
Chie fight looked better. Shadows and chests looked the same, and heads
were still missing in some battle views. The matching home3 PSP Go log
records 4,299 recovered draw submissions and zero pool exhaustions in its
16 reported intervals. This verifies that the guard recovered draws during
the session. Identifying individual Shadow, chest or head draws requires
a scene capture.

The same Go session reached frame 21,760 and ended with a battle trace.
There are no HOME phase markers. The 333 MHz setting and freeze after Yes
come from the tester. The log cannot tell whether the callback reached the
card lock, device sync or firmware exit. See the
[home4 build notes](build_b176_xmb_home4.md) for the test change.
