# b176-xmb-home6

This PSP test update follows [home5](build_b176_xmb_home5.md). The
[replay saves](replay_saves.md) now identify all eight slots from the
home4 playthrough.

| Report | Change | Verification |
| --- | --- | --- |
| P4-049, blank save-slot dials | The slot numbers use two masked draws in `mcDrawSlotDial`. The port now lets these draws clear destination alpha where the original sprite's alpha tests pass. Both 2D sprite rotation paths now initialize the Z coordinate before the point transform. | PPSSPP 1.20.4 shows the load dials in OpenGL and software. OpenGL shows populated slots, the two-digit dial for empty slot 10, the full confirmation dialog and numbered dials in the save menu. Slot 7 loaded and entered the field. |
| P4-006, emotion icons | Preserve the position and rotation copies when adapting the model-effect callbacks to PSP. The same source fix restores five other effect copies. | The final ELF copies all four words in each of the position and rotation setters. Use the supplied saves to check a specific question mark in the same scene. |

The trial that applied strict depth comparison to all 2D draws with GS
`ZTST = GREATER` removed parts of the load-confirmation dialog and the
field HUD. The final build scopes the destination-alpha change to the
two save-dial tests. The 04/14 → 04/15 transition still shows overlapping
weather sprites with the rotation fix. P4-004 remains open.

## Build and tests

- The release was rebuilt from the pinned US snapshot with Linux PSP GCC
  15.2.0. All 1,416 compiler census units built. The release linked 825
  staged inputs, validated 19 PSP import libraries and resolved all native
  function pointers.
- Host suite results match the pre-home6 baseline: 59 passes, two platform
  skips and three errors. The VAG round-trip requires Windows C# and two
  older renderer tests have stale C mocks. Both standalone clipping tests
  and the PPSSPP depth-mask pixel test passed.
- The PPSSPP run used a private copy of the saves and the prepared US game
  archives. The final build continued from slot 7 through Junes, the TV
  plaza and the castle gate. The home5 comparison reached 1F and opened the Soul
  Drop chest. Its disconnected red surfaces still appeared. The audio
  pack is unavailable in this setup. PSP hardware, XMB return and
  performance at 333 MHz require hardware testing.

## Hardware replay

Install `EBOOT.PBP`, `p4data.bin` and `p4str.bin` together in the existing
P4 directory. The startup log identifies `b176-xmb-home6`. With the same
333 MHz setting, check the save and load dials and load slot 7 before
Junes for the TV plaza and castle. Use slot 8 at Dojima Residence for
normal progress or slot 4 for the 04/15 calendar and rain replay. See the
[playtest backlog](playtest.md) for the remaining reports.
