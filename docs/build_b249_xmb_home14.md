# b249-xmb-home14 test build

Home14 is based on the [home13 P0 diagnostic](build_b249_xmb_home13_p0.md)
at `c1c778c5e80938be35cbed7db2168667ac0224ba`. The exact home14 commit,
branch, and launch-file hashes are in the ZIP's `BUILD/verification.json`.
Use this build for focused verification. Full-playthrough approval is open
while the Castle crash and the Town Map and Shuffle Time defects remain.

## Changes and evidence

- The disc reader now reports `READEND` only after receiving the full
  sector range. The old code rounded a partial final sector up and
  reported success. The game's CDVD worker compares the returned sector
  count with its request, then parses the archive if the status is
  `READEND`. Consequently the old port could hand it missing tail bytes.
  The new reader checks the seek result and missing split parts, and
  reports incomplete reads as `ERROR`. The log identifies the container,
  sector and received length. The host test injects a one-byte-short
  last sector at a split, then tests a successful retry, seek failure and
  semaphore failure. The short-sector assertion fails on home13.
- The PSP Go report establishes that the black strips seen in home12
  were absent in the tested scenes on home13. Home14 retains the entire
  renderer from home13. The earlier System, Item, Persona, Equip,
  calendar, TV, battle, HOME, audio and FPS changes are retained.
- The reported home13 Castle-entry crash happened once, and the next
  entry succeeded on the same PSP Go. The supplied corridor video starts
  after entry and contains no crash. The sector correction addresses a
  demonstrated loader failure. The relationship to that incident is open.
- `IMG_7239.mp4` shows an opaque white arch ahead on 1F at the start
  and door/wall detail when near it around 2 seconds. It is a distance-
  dependent effect in that recorded corridor. The clip alone is
  insufficient to establish its source or how the same pose looks on PS2.
- Town Map geometry (P4-051), System stripes (P4-056) and Shuffle Time
  faces (P4-032) remain open. The US retail binary deliberately draws
  the System stripe layer. Measure placement and palette at the same
  menu phase before altering the layer. The original
  [PS2 Shuffle Time capture](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351286/)
  shows illustrated face-up cards at the Start/Decline stage. The original
  [PS2 Town Map capture](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351271/)
  establishes visible roads and buildings on a different date and pose.

## Reproducible setup

Use the verified original US executable (`SLUS_217.82`) and matched
`EBOOT.PBP`, `p4data.bin` and `p4str.bin`. In PPSSPP use a private copy
of the prepared game directory with the archive parts, `p4files.idx`,
`SOUND/` and the copied `saves/`. See [replay slots](replay_saves.md).
Keep genuine save backups outside the running installation.

With the repository's host dependencies and PSPSDK on `PSPDEV`, prepare
the US inputs using the [PSP build guide](../README.md), then run this
from the repository root.

```sh
PYTHONPATH=tools python3 tools/psp_link.py
PYTHONPATH=tools python3 tools/psp_hw_debug.py --release --out build/home14
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_*.py'
```

The run used PPSSPP 1.20.4 in OpenGL at 1× internal resolution. The
disc index contained 13,072 entries and the prepared audio bank contained
17,375 streams. The rounded sector ranges for all 13,070 nonempty index
entries fit within the prepared archive parts. The first home13 run with videos present logged an
unimplemented `sceMpegGetAvcNalAu` followed by a decode error and a
return to the game. For the remaining replay runs, the prepared `VIDEO/`
directory was set aside in the emulator environment. The emulator's
host audio sink was dummy. SAS initialization was observed in the game
log. Audible output and full film playback remain device checks. Keep
`VIDEO/` and `SOUND/` in the real PSP installation.

## Performance

The home9 PSP Go captures showed 30 FPS near the Dojima save point, 27
in part of the walk, and the user reported near-30 stretches in the
Castle. The Town Map photo showed 20 FPS. These samples do not isolate
CPU, GE, I/O or presentation waits. For comparable Go measurements,
use 333 MHz and sound with the same saved state and a fixed 30-second
path. Capture Dojima (slot 8), school and town (slot 9), and hub, Castle
and battle (slot 10). Use existing `p4.log` summaries to correlate low
FPS with waits and draw activity. The PPSSPP log gives work counters for the
route through school, map and South. In the first reported windows after
the respective secondary archive loads, draw-buffer peaks were 15,061,
7,641 and 17,063 vertices. There were zero texture waits in those windows.
These windows can include transitions. They describe emulator workload and
cannot attribute PSP frame times to a subsystem. No performance change is
included.

## Verification

| Platform | Result |
| --- | --- |
| Host | 71 of 76 passed, two skipped and three inherited harness errors. The sector-read, HOME lifecycle, Shuffle dispatch, bounds and calendar regressions passed. The VAG signal roundtrip also passed separately with the native encoder used to prepare the private replay data. Windows VAG and two renderer mock errors remain in normal discovery. |
| PSP build | Linked 825 staged inputs and 19 PSP import libraries. Address audit checked 5,857 rebased entries and 168 switches. Zero proof errors and zero missing native function-pointer targets. |
| PS2 reference | Retail US code and original PS2 captures used for the System stripes and the Town Map/Shuffle Time expectations. A local Play! run failed during offscreen initialization. Matching PS2 scene replay is open. |
| PSP Go | Home13 field-strip result, intermittent Castle-entry crash, missing Shuffle visuals and white arch are tester evidence. Home14 device replay is pending. |
| PSP-3000 | Home14 device replay is pending. |

### PPSSPP gameplay and replay matrix

The home13 and home14 runs used private save copies. Source slots 9, 10 and
16 were exposed as menu slot 1 in separate home14 runs. That
changed the destination filename only. Full gameplay data was loaded.

| Replay | Trigger and expected result | Actual result |
| --- | --- | --- |
| Cold boot / intro | Boot an unmodified release EBOOT to the title menu. ATLUS and CRIWARE appear, followed by the title and load/new game controls. Select New Game, Normal and Yes. Expect the opening Velvet Room scene. A working movie path should also play the opening. | Home13 showed both logos and reached the title. The prepared opening movie failed at the PPSSPP MPEG call. Home14 reached the title, loaded saves and ran New Game through difficulty confirmation into Igor's opening Velvet Room scene with movies set aside. Later intro events remain open. |
| Slot 10, Entrance Hall | Load, acknowledge the confirmation and wait for the TV-world scenery and party. | Passed on home13 and home14. The home14 SAS bank initialized and the FPS overlay displayed. Home13 also entered the Velvet Room, advanced the first visit and returned to the hub. |
| Slot 10, System | Triangle, System, move from Quest to Config. Expect a readable highlight, label and return action. | Home14 passed the highlight and text switch over the TV entrance. The coloured stripe layer was visible. The exact Castle and PS2 appearance comparison remains open. |
| Slot 9, school | Load Class 2-2 and use Square. Expect readable characters, classroom geometry and the travel list. | Home14 loaded the room and opened the travel options. The movement replay in Classroom Building 2F remains separate. |
| Slot 9, Town Map | Square, Go into town, keep school selected, then switch to Central Shopping District. Expect detailed terrain, a destination image, marker and labels. | Reproduced P4-051 on home14. Both destinations have a preview, marker and labels, with flat cloud-coloured regions and fragments in place of the detailed 3D scenery. Resources for field 1/1 and its map script finished loading. The rendering defect remains open. |
| Slot 9, Shopping District South | Confirm Central Shopping District, then walk both directions at the bus stop. Check for abrupt dark surfaces over the pavement. | Reached South from the Town Map. No black strips were visible during the sampled walk. |
| Slot 9, Item and Equip | At Shopping District South, open Item, choose a different item, and use Square for details. Back out into Equip, select Weapon, and press Square. | Item showed the selected row and attached description for Revival Bead. Equip displayed all three categories and their items, with the highlight aligned when Accessory was selected. The Golf Club row, attack/hit values and Square help rendered in Weapon. Multi-item scrolling is open. |
| Slot 10, Castle Gate (P4-055) | From the TV-world hub, approach Teddie on the right, choose Search for Yukiko, and approach the gate. Expect the gate and Teddie's 1F instructions. | Home14 loaded both gate packs and reached Teddie's instructions without crashing in one sequence. A completed 1F entry and repeat entries are needed to assess the intermittent crash. |
| Slots 4/8, Dojima and date | Use the evening save, save to a spare slot, reload, then advance the night. Check weather, both date glyphs and save persistence. | Pending on home14. The load confirmation and complete 171,112-byte reads were observed for the existing slots 9 and 10. |
| Slot 10, battle | Reach 1F, approach a ground and flying Shadow, attack a weakness, perform two- and three-person All-Outs, win, check Level Up and Shuffle Time through the reward, then exercise HOME. | Pending on home14. The hardware Shuffle Time report remains open. |
| Slots 15/16, later replay | Load the late saves and confirm displayed route, then record a late fight/transition and verify unlocks before claiming New Game+ coverage. | Source slot 16 loaded into Yomotsu Hirasaka Path 9 on 03/20 daytime in PPSSPP, with the party and entrance visible. Slot 15 summary metadata checked. Save lineage and late combat are open. |

Extended replays for Equip, Persona, Social Stats and their detailed
states; the school corridor route; the early
story, calendar and TV transitions; and the remaining
[playtest reports](playtest.md) are open. None of those rows count as a
home14 runtime pass on the basis of a previous build or a host fixture.

The remaining visual checks are grouped in the backlog. Slots 2–6 cover
story animation, question-mark overlays, calendar, rainy-school umbrellas,
TV transitions and early boss effects. Slots 7/10 cover Castle and battle
geometry, heads, field Shadows and audio, All-Out, level-up and Shuffle Time.
A matched retail PS2 frame is still needed for Social Stats backgrounds,
the eye cut-in and the reported lighting and skill effects.

## Hardware check and rollback

1. Back up the existing P4 directory, including `saves/`. Replace the
   three launch files together, then check `b249-xmb-home14` in `p4.log`.
   An empty `FPS` file beside the EBOOT enables the overlay on restart.
2. On PSP Go at 333 MHz with sound, load slot 10. Approach Teddie from
   his right, press Cross at the Check prompt and choose Search for
   Yukiko. Enter the Castle. Repeat first entry and
   re-entry across cold boots. Copy `p4.log` immediately after a crash
   if available. Record the exact transition and last visible frame.
3. On 1F, film the far arch through approach from the same corridor.
   Trigger a normal battle and Shuffle Time. Record the preview, reveal
   and actual reward. Check System over the Castle backdrop.
4. Use slot 9 for the school corridor, Shopping District South and the
   Town Map. Walk both directions and switch destinations. Check Equip,
   Item and Persona. Use slot 8 for save/load and calendar. Check HOME in
   a normal battle and after saving in an unused slot.
5. For rollback, restore all three home13 files together. Back up any new
   save slots separately first.
