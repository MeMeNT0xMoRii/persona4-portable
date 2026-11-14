# b249-xmb-home9 playthrough candidate

Branch `playthrough/b249-home9`. The home8 base is
`48552115e5d82fc2f575d07f73d178f34286606a` from `playthrough/b176-xmb-home`.
The [home8 integration record](build_b249_xmb_home8.md) contains the merge
decisions and previous replay results. The home9 package records its exact
source commit, source tree and file hashes in `BUILD/verification.json`.
The PBP SHA-256 is
`53078b859014c4b6bc1ae8d0cd849335cf88d2378bc4f37186193bf06d98cc88`.

## Installation and rollback

Use a prepared US Persona 4 installation (`SLUS_217.82`). On PSP Go or
PSP-3000, exit the game and back up the entire P4 directory, including
`saves/`. Copy the three files from the same home9 package into the existing
directory, such as `PSP/GAME/CAT_Ports/P4`:

- `EBOOT.PBP`
- `p4data.bin`
- `p4str.bin`

The prepared archives, index, `SOUND/` and `VIDEO/` supply game content.
Check for `b249-xmb-home9` in `p4.log` after boot. The PBP requests extended
RAM and the game requests 333/333/166 MHz. For a fresh playthrough choose New
Game, then use an empty slot. To roll back, restore the three matched files
from b249-xmb-home8. Back up any new slots separately before restoring the
original saves.

## FPS and performance

Place a file named `FPS` (uppercase, no extension, empty is fine) beside
`EBOOT.PBP` before launching. The display appears in the lower-left corner.
Remove the marker and relaunch to turn it off. The marker uses the resolved
launch path and is checked once at startup.

The counter samples the PSP system timer after successful gameplay
framebuffer flips. The asynchronous path measures publication after the
vblank wait. The synchronous path counts flips with a completed frame list.
The result is frames per second over windows of at least one second with one
decimal place. Display refreshes of the same frame and movie playback are
outside the count. The display measures game presentation cadence, including
idle and renderer waits. It cannot attribute a slowdown to CPU, GPU or audio
and does not measure the physical scanout. PPSSPP timings are not PSP
benchmarks.

The enabled overlay adds one timer read per counted flip and a small
untextured batch at the end of the gameplay list, with up to 1,824 bytes of
vertex data in the existing list. Device cost is unmeasured. The feature
has no per-frame file writes, extra worker or input binding. The first
window can show 0.0. Home8 already uses pipelined flips and geometry and
skin reuse. See [the performance record](perf_status.md) for the different
upstream timings and experimental switches. Home9 adds instrumentation
and keeps the home8 performance policy, including disabled size-based
culling and mesh simplification.

## Scope and open issues

| Issue | Status and reproduction |
| --- | --- |
| System selected text | The retail System draw first clears destination alpha and then marks the cursor before the selected-label pass. The PSP path now performs those two steps with a stencil mask scoped to this menu. |
| Item selection | The normal cursor helper accepts both packed coordinates. On 04/17 evening, move between Peach Seed and Medicine, then open and close Show with Square. |
| Persona layout | The port retains both coordinates for the name panel, frame, EXP, Arcana, level and background panel sprites. Use the protagonist's Persona status and toggle Skill info. |
| All-Out portrait and impact | Open. The PSP Go clip of the 1F battle shows the upper portrait clipped around 8.5 s and a bright impact cloud after 10 s. Use slot 7 to revisit the castle. |
| Day change | Open. Advance to the next day from slot 8 and capture the incoming numeral throughout the roll. The still date badge on 04/17 and the animated numeral are separate checks. |
| Social stats | Open comparison. The home8 photo shows diagonal orange lines across the graph. Verify whether the same lines exist in the original status screen before changing the background. |
| Skill lighting | Open comparison. The short PSP Go clip shows a saturated circular flash near 4 s. Compare the same skill and encounter before assigning the difference to alpha or blend state. |
| Slowdowns | FPS measurement available. Target hardware readings remain pending. |

The menu analysis used the home8 PSP Go photos and the original US PS2 draw
code. A matching US PS2 gameplay capture was unavailable, so the original
appearance of the Social Stats lines and skill flashes is still unverified.

## Test matrix

| Environment | Result |
| --- | --- |
| Host | PSP GCC release linked with 825 staged inputs and 19 validated PSP import libraries. The address audit checked 5,857 entries and 168 switch targets with zero proof errors. Of 71 host tests, 66 passed, two were skipped and three reported the same errors as home8. The errors are the Windows VAG compiler dependency and two renderer harness compile errors. The new packed-coordinate test passed. |
| PPSSPP 1.20.4, software renderer | Booted the packaged PBP from CAT_Ports with and without the FPS marker. With the marker enabled, the title showed 30.0 in PPSSPP. New Game reached the first Velvet Room conversation with Igor. In the priority-menu replay, slot 8 loaded in Dojima Residence and all three selected System labels were visible. The Item cursor followed Peach Seed and Medicine, and Square opened the description. Izanagi's name, level, Arcana, EXP and Skill info appeared in their panels. |
| PSP Go | Home8 reference captures reviewed. Home9 hardware validation is open, including a fresh playthrough at 333 MHz with sound, performance and HOME. |
| PSP-3000 | Home9 hardware validation is open, including XMB, saves and HOME. |

Home9 retains the home8 integration. The TV, umbrella, battle heads, level-up,
Shadow cues, calendar roll, fresh save write and in-firmware HOME exit still
need a home9 replay. The earlier individual pass results are in the
[home8 integration record](build_b249_xmb_home8.md).

## Priorities for the hardware replay

1. Back up the installation. On PSP Go, boot from XMB, start a new game
   with sound on at 333 MHz and confirm a save to a free slot, reload and
   HOME exit. Repeat the slot 8 menu checks shown in the reference photos.
2. Repeat on PSP-3000. Record scene, model, date, clock, audio setting and
   displayed FPS after it settles in the room, menus, rainy field, castle
   corridor and a battle. Use the same saves and setup for future comparisons.
3. Replay slot 7 for battle heads, Shadow cues, All-Out and level-up. Use
   slot 4 for umbrellas and slot 8 for the TV and day change.
