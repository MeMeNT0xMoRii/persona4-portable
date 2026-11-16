# b249-xmb-home19 dungeon fog and fusion follow-up

Home19 builds on [home18](build_b249_xmb_home18.md), commit
`88ae6434713399e9ebdbe1a7dc7af437d7004755`. The three runtime files update
the prepared US `SLUS_217.82` installation.

## P4-022 · Dungeon distance fog

The original-game captures on the black Anbernic show distant walls and the
floor blending into the fog color as they approach the bright opening.
In the white PSP Go captures the geometry holds its color to a hard edge.
The camera positions differ between devices.

| Dungeon | PSP Go port | Original game |
| --- | --- | --- |
| Yukiko's Castle | `IMG_7260.mp4` at 0:03 | `IMG_7261.mp4` at 0:03 |
| Yomotsu Hirasaka Path 8 | `IMG_7262.mp4` at 0:04 | `IMG_7266.mp4` at 0:06 |

The native fog switch defaulted to off. Home19 uses the scene's RenderWare
fog state, color and camera start plane. The Sky far-plane override takes
precedence when set, and otherwise the camera far clip supplies the end.
The PSP Graphics Engine interpolates the fog factor from camera depth.
The renderer clears the fog state on leaving the 3D pass and includes the
fog plane in its cached command-list keys.

## P4-057 and P4-058 · Persona fusion

The PSP Go tester completed the home18 fusion and reached the Apsaras
acquisition dialogue in `IMG_7259.mp4`. Aligning consecutive video frames
shows an alternating light halo and shading change around Apsaras, with a
stable dialogue box. Home19 adds a short `FUSION visual` trace during this
dialogue. It samples up to eight completed framebuffers and records the
buffer index, clear flags, mask and blend activity, and four pixel colors.
The lighting cause and a visual fix are pending.

## Verification

Home19 compiled 1,416 units and checked 19 PSP import libraries. The address
audit covered 5,857 rebased entries and 168 switches with zero proof errors.
Full host discovery reported 75 passes, two skips and the three baseline
harness errors (the Windows VAG compiler and two renderer harness builds).
The new fog-state test and all six synthetic fusion preview cases passed.
The latter run the packaged PSP code with synthetic menu data.

A standalone PSP geometry fixture used the renderer's fog binder and camera
projection in PPSSPP 1.20.4. Four depths tested fog off, fog with the default
end, an earlier Sky override and fog off again. All 16 VRAM readings matched
the calculated colors to within two levels per channel. Replay of both
in-game corridors and the fusion lighting on PSP Go is pending.

## PSP Go replay

1. Replace `EBOOT.PBP`, `p4data.bin` and `p4str.bin` together and start from
   the XMB at 333 MHz with sound. The boot line should show `b249-xmb-home19`.
2. Use the Castle and Yomotsu Hirasaka saves to line up long corridor views.
   Check whether the walls and floor fade smoothly in the distance as the
   player moves toward and away from the opening.
3. Fuse Izanagi and Ukobach in Normal Spread. Leave the Apsaras acquisition
   dialogue open for two seconds and save the `p4.log` before restarting.
   It should include the `FUSION visual` entries.
4. Restore the three home18 runtime files together for rollback.
