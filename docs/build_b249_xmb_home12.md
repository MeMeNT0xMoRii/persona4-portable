# b249-xmb-home12 hardware test build

Built from [home11](build_b249_xmb_home11.md) at
`6087fb5f3231d2999510a81402e5ac60e5e5a012` on `playthrough/b176-xmb-home`.
The ZIP records its own source commit and the hashes of all three launch files
in `BUILD/verification.json`.

## Field geometry (P4-043 and P4-054)

On PSP Go, `IMG_7232.mp4` shows dark polygonal strips moving across Shopping
District South as the camera follows the player. `IMG_7236.mp4` shows dark
wedges across parts of the Classroom Building 2F floor. The HUD stays visible
in both recordings. The available older logs lack a matching trace from
either scene. The media identify the device and locations, but the installed
EBOOT SHA is unavailable. Home9 already had the separate Town Map report,
and P4-043 records earlier movement-related breakup in Shopping District
North. The South and classroom videos do not establish when those cases
started.

The 3D guard-band shortcut underestimated the world-space bounds of
model-space spheres when the matrix rows were not orthogonal. A host fixture
shows the shortcut skipping clipping for a visible triangle with one corner
outside the GE's 4,096-pixel raster window. The radius now uses an upper bound
on the transformed sphere's extent from the model matrix's Gram matrix.
Potentially affected meshes follow the existing per-vertex or per-chunk
guard path. World-space spheres still follow their existing path. The patch
does not change the projection, depth test or writes, viewport, scissor,
texture addressing, or mesh ordering. Replay both scenes to determine if
the bound was responsible for the photographed breakup.

The home9-to-home10 renderer diff changes the calendar scissor and the
immediate-mode dispatch for Shuffle Time. Home11 changes the retail
secondary-room helper, with no guard-band edits in either step. Those changes
remain in home12.

## Town Map (P4-051)

`IMG_7234.jpeg` shows 04/18 After School with Central Shopping District
selected. The preview, menu labels and blue marker remain legible. The map
behind them has a large dark area, small white fragments and a bright green
right edge. The earlier home9 screenshot showed similar missing scenery with
Yasogami High School selected. The [PS2 screenshot](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351271/)
shows streets, buildings and terrain. Its date is 07/25, so it establishes
the expected scenery with a different camera and calendar state.

In the US source, `FldLmapUpdate` loads field 1, room 1 and its calendar
variant, then applies the resource FOV. `FldLmapCameraMoveTo` selects one of
five camera poses. The preview, selection labels and masks are a separate
queue from the underlying field scene. Home11 fixes the variant match when
the room is already loaded. The screenshot alone cannot isolate missing
geometry, occlusion, UV or texture data, or a GE state transition. An
04/18 comparison should capture the first map entry and a reopen with the
same destination, then compare scene geometry, material and depth state
before the UI draws. P4-051 remains open. Home12 changes no map-specific
render state.

## System Menu in Yukiko's Castle

`IMG_7238.jpeg` shows the left set of coloured stripes and a readable Quest
highlight. The home8 Dojima-house System screenshot shows the same band.
The US PS2 retail executable calls `nLineDrawSet` at `0x0035FE6C`, and
`nLineDrawRecords` emits the palette-coloured line quads, with full 448-pixel
height for the vertical layout. The photographed stripe band is consistent
with that authored layer. The exact colour and phase have yet to be compared
frame for frame. Keep the band and check for additional stray pixels, a
missing highlight, or a damaged 3D backdrop. There is no evidence tying the
authored band to texture uploads or to the field geometry defect.

## Verification

| Check | Result |
| --- | --- |
| Host regression | The new transformed-sphere fixture fails on home11 and passes on home12. The existing guarded draw pool, calendar, packed-coordinate, secondary-room and HOME lifecycle tests pass. |
| Host suite | 69 of 74 passed. Two skipped for Windows/MSVC. Three inherited errors remain in VAG roundtrip and two render-state harnesses. |
| PSP release | Linked 825 staged inputs, validated 19 PSP import libraries, zero function pointers without a native target. Address audit checked 5,857 rebased entries and 168 switch entries. |
| PPSSPP | Scene replays pending. PPSSPP and the prepared game archives were unavailable in the build workspace. |
| PSP Go and PSP-3000 | Home12 device tests pending. The four captures are prior PSP Go observations at 333 MHz. |

## Install, replay and rollback

Back up the complete P4 directory, including `saves/`. In a prepared US
installation (`SLUS_217.82`), replace `EBOOT.PBP`, `p4data.bin` and
`p4str.bin` together. Keep the game archives and the existing `SOUND` and
`VIDEO` assets. Launch from XMB and check `p4.log` for `b249-xmb-home12`.
An empty `FPS` file beside `EBOOT.PBP` enables the display on the next boot.
Remove it and restart to disable it.

Use the [replay saves](replay_saves.md) on PSP Go at 333 MHz with sound on.

1. Slot 9, 04/18 After School. Reach Classroom Building 2F and walk both
   ways along the corridor at normal speed. Capture where the camera starts
   to track the character and where the floor wedges appeared.
2. From slot 9, leave school and select Central Shopping District. Reach
   South and retrace the recorded path along the pavement in both directions.
   Slot 7 provides an alternate route to South on 04/17.
3. Open the Town Map from slot 9, hold on the initial destination, switch
   to Central Shopping District and inspect the background. Exit and reopen
   with the same destination. Keep the date, selection and build in each
   recording. Copy `p4.log` before the next boot if the map is still damaged.
4. Slot 10, enter Yukiko's Castle. Open System and move through Quest,
   Config and Title without selecting Title. Check Item, Equip and Persona.
   Trigger a regular encounter, replay Shuffle Time including the reward,
   and verify HOME exit during another encounter. Save in a free slot and
   reload it. On a separate cold boot, check XMB, New Game and sound.

For a rollback, restore all three matching home11 files together. Home9 is
the earlier hardware-confirmed baseline for the menu and HOME functions.
Keep newer saves backed up separately before restoring any old save folder.
Start the full playthrough after the four scene checks and the launch/save
checks on the intended device.
