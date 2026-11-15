# b249-xmb-home11 hardware test build

Home11 starts from [home10](build_b249_xmb_home10.md) at
`72fa483439af40e0186f40adef2b799288f982af` on `playthrough/b176-xmb-home`.
The update records its source commit and launch-file hashes in
`BUILD/verification.json`.

## Town Map (P4-051)

The PSP Go photo at Yasogami High School on 04/18 shows the destination
list, preview and marker over large terrain surfaces with missing scenery.
The [original PS2 map capture](https://www.mobygames.com/game/38259/shin-megami-tensei-persona-4/screenshots/ps2/351271/)
shows detailed buildings, streets and terrain behind the same destination
menu. It is from 07/25, so it establishes the intended scenery rather than a
matching 04/18 camera and weather comparison.

The pinned US executable loads field 1, room 1, requests the secondary
environment variant and sets the resource FOV and one of five camera poses
(`FldLmapUpdate`, `FldLmapCameraMoveTo`). The load path has a confirmed
porting error at retail address `0x00144ED0`. The PS2 routine compares
the current secondary room against the signed low 16 bits of the requested
room. The source mirror narrowed the incoming argument to 32 bits but
left a 48-bit shift in the expression. The home10 PSP ELF consequently
compares the current room against zero. This can skip a requested change
away from zero or reload an already-selected nonzero variant when the
scene is loaded. Home11 restores the signed halfword comparison in the
secondary-room helper. The pending-load path, the primary room selector,
camera setup and renderer are unchanged.

The **missing scenery remains open**. The photographed result alone cannot
distinguish variant loading, geometry submission and depth or clipping
problems. On a fresh load the helper's pending-load branch already stored
the requested variant on home10. The corrected comparison therefore does
not establish the cause of the initial 04/18 screenshot. A matched render
or log from the scene is needed before altering camera or renderer behavior.

## Verification and replay

- **Host**. Standalone regression checks the source mirror keeps the requested
  halfword when it narrows the argument. The PSP ELF is checked for a
  comparison between the requested and current room values. The release
  links with 825 staged inputs and 19 PSP libraries. The address check covers
  5,857 rebased entries and 168 switch targets. 68 of the 73 host tests pass,
  two skip and the three inherited VAG and renderer-harness errors remain.
- **PPSSPP**. No home11 scene replay. The emulator and prepared game archives
  are unavailable in the build workspace.
- **PSP Go and PSP-3000**. Home11 hardware status is open. Start with slot 9
  (04/18, high school), record the initial Town Map and switch through its
  destinations. Leave and reopen the map, capturing `p4.log` if the scenery
  still breaks. Compare the same location and phase with US PS2 if available.
  Check Equip and Persona in the same session, slot 8 for the date roll, and
  slot 10 for Shuffle Time. The home10 changes to these paths remain present.
- **Other open reports**. Social Stats diagonals, weakness cut-in centering,
  the two-person All-Out portrait, and Skill lighting remain pending a
  room- or animation-matched comparison. No changes were made for them.

## Installation and rollback

Back up the existing P4 directory with `saves/`. On the prepared US setup
(`SLUS_217.82`), replace `EBOOT.PBP`, `p4data.bin` and `p4str.bin` together.
Check for `b249-xmb-home11` in `p4.log`. On PSP Go, use 333 MHz with sound
enabled for the acceptance test. For a new run, start New Game and use a
free slot. The empty `FPS` file beside the EBOOT still enables the display
at boot. Delete it and restart to turn the overlay off.

To roll back, restore all three matching home10 files. Back up any new slots
before restoring an earlier save directory. No save format changes.
