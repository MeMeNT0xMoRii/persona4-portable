# b249-xmb-home13-p0 · PSP Go diagnostic build

This build branches from `23efc3539ceffbc354bd921988425e7c7b6133c5`
(home12) on `diagnostics/b249-home13-p0`. The ZIP records its exact commit
and file hashes in `BUILD/verification.json`.

## P4-043 and P4-054 · dark polygons while walking

On the same PSP Go at 333 MHz, the tester saw no black strips in home8 and
home9 and reproduced them in home12. `IMG_7232.mp4` shows dark strips across
the road in Shopping District South. `IMG_7236.mp4` shows dark wedges across
the Classroom Building 2F floor. The shapes change during movement while
the HUD stays visible. The report establishes home9 as the last comparison
with no strips observed. A matched home10 or home11 result is unavailable.

The renderer diff from home9 to home12 has three changes. Home10 added the
calendar glyph scissor and broadened `func_004114d0` to accept the shared
non-indexed RenderWare path `func_004106a0` for Shuffle Time. It also moved
the immediate-mode check ahead of the atomic object check. Home11 left
`port/rwsky.c` alone and corrected the secondary-room comparison in a
separate game-code shim. Home12 changed the bound of sheared spheres in
the mesh guard. The home12 hardware retest still showed the strips.

The US executable calls `func_004114d0` from `0x0041079C` in the non-indexed
entry. This entry is used by Shuffle Time and by field shadow projection.
`FldShadow_ProjectTriangle` and the corresponding dynamic geometry routine
in `k_spipe.c` submit triangle lists built from collision surfaces through
that entry. The home10 change lets those lists reach the generic PSP
`im3d_draw`, which applies its shared mesh state and submits them without
the mesh-guard path. This confirms a change in normal field rendering.
The draw path is a candidate for the recorded artifacts. Device replay
must establish whether it accounts for both scenes.

Home13 restores the home9 dispatch for ordinary field shadow submissions
and atomic objects. Shuffle Time's non-indexed calls use a scoped adapter.
Its indexed card rendering and coplanar back handling remain available.
The Equip coordinate and calendar scissor fixes, the corrected room
comparison and the home12 sphere bound stay in place. The guard limits,
FPS overlay, save, and HOME paths have no functional changes in this build.
The Town Map (P4-051) and the System-menu stripe appearance remain separate
open reports.

## Validation

| Platform | Result |
| --- | --- |
| Host | Dispatch fixture exercises field shadows, indexed and atomic handling, Shuffle Time, and scope restoration on frame skip. It fails when the global non-indexed dispatch is restored. Calendar, packed-coordinate, HOME lifecycle and mesh-guard tests pass. Full suite: 70 passed, two skipped, three inherited errors in VAG and render-state harnesses. |
| PSP build | Linked release with 19 validated import libraries and zero missing native function-pointer targets. Address audit checked 5,857 rebased entries and 168 switch entries. PSP object inspection confirmed that Shuffle Time uses the adapter and field shadow projection still uses the retail entry directly. |
| PPSSPP | Scene replay pending. PPSSPP and the prepared game archives were unavailable in this workspace. |
| PSP Go and PSP-3000 | Home13 device tests pending. Home12 black polygons were confirmed by the tester on PSP Go. |

## Install, reproduce and roll back

Back up the full P4 directory including `saves/`. On the existing prepared
US installation (`SLUS_217.82`), replace `EBOOT.PBP`, `p4data.bin` and
`p4str.bin` together. Keep the game archives, index, sound and video assets.
Check for `b249-xmb-home13-p0` in `p4.log` after starting from XMB. Keep
the empty `FPS` file alongside the EBOOT if used for comparison.

1. At 333 MHz with sound on, load slot 9 (04/18 After School). Walk the
   Classroom Building 2F corridor both ways, including past the stairwell.
2. Leave school, reach Shopping District South and walk both ways along
   the road from the video. Compare the same route and camera movement.
3. If those scenes are clean, use slot 10 to check one Shuffle Time reveal
   and reward. Check Equip from slot 9 and the date roll from slot 8.
   Verify load, save to a free slot, FPS and HOME. Keep the full playthrough
   pending until the two field scenes have passed.

If a strip returns, record the scene, route and build, and preserve
`p4.log` before another boot. Restore all three matching home12 files for
the immediate rollback, or the three home9 files to compare with the
previous field rendering. Back up new saves before using any older save
folder. The diagnostic branch leaves the development branch at home12.
