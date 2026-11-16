# b249-xmb-home20 particle depth test build

Home20 builds on [home19](build_b249_xmb_home19.md), revision
`d75fd2cfb18eec2f41f250a53eccda34aa8eb50d`. Use the three runtime
files together for the prepared US `SLUS_217.82` installation.

## Particle depth writes

Several original PTank states set `TEST_1` to `0x71801`. The setter retains
its low half. With `NEVER` and `FB_ONLY`, fragments that pass the depth
test affect color without writing depth. Home19 used the global depth
write setting for these particles, so a transparent foreground layer
could obscure a later, farther effect layer. Home20 applies a depth write
mask for this state during the particle draw and restores the global mask
afterward. It uses the existing textures, alpha cutoff and blend functions.
The runtime diff from home19 consists of this draw change and the boot marker.

The four-level destination-alpha mask can still produce visible rings.
The exact contributors to the recorded Zio burst (P4-053), All-Out impact
(P4-026) and Apsaras halo flicker (P4-058) need matched gameplay captures.
The mixed alpha-test state `0x717FB` still uses the established PTank path.

## Regression coverage

| Area | Test result | Gameplay check |
| --- | --- | --- |
| PTank depth and normal particles | PPSSPP 1.20.4 software renderer used matched home19 and home20 fixtures. Untagged particles, `0x717FB`, global depth writes off and an opaque foreground occluder produced identical colors. A draw after the particle correctly wrote depth. Removing the restoration in a test copy makes that last control fail. | Replay ordinary weather and combat particles, checking layering against nearby opaque geometry. |
| Zio and All-Out | The fixture checks the four blend states used by the retail particle block (`0x44`, `0x48`, `0x42`, `0x06`) with and without the color-only state. Existing mask-ramp pixels match home19 exactly. These are shared render-path checks with synthetic geometry. | Compare the summon and Zio hit on a Lying Hablerie, and the All-Out impact with two and three party members. Compare matching animation phases with home19 and, where available, US PS2. |
| Fusion | Six alignment-checking MIPS fixtures executed the new EBOOT's relocated PRX through Normal Spread and reached the Apsaras preview with the same digit colors as home19. | Complete the result animation and acquisition. Hold the Apsaras message for two seconds and retain the `FUSION visual` lines in `p4.log`. |
| Dungeon fog | All 16 VRAM samples from the home20 fog binder and camera match the home19 PPSSPP fixture, including fog off, the camera far clip and the earlier Sky far-plane override. | Look down the long lanes in Yukiko's Castle and Yomotsu Hirasaka Path 8. Verify the fade on approach and retreat. |

The full PSP build compiled 1,416 units, validated 19 import libraries,
and audited 5,857 rebased entries and 168 switch targets with zero proof
errors. All 821 generated object hashes match home19. PPSSPP booted the
release EBOOT with its two data files, logged the home20 marker and
entered `kwlnBoot` up to the first font asset request. Host discovery
reported 75 passes, two skips and the same three
existing harness errors as home19 (Windows VAG compiler and two renderer
test builds). The prepared game archives were unavailable in this test
environment. Full gameplay replay and PSP Go validation are pending.

### Reproduce the particle comparison

Use a native PSPSDK and a copy of home19 `port/rwsky.c` as the baseline.
Build the same fixture with each source and launch the respective PBP in
PPSSPP software rendering. The report is `p4-effect-probe.txt` on the
virtual memory stick. The first row tests depth, the second is the mask
ramp and the third is pairs for the four blend modes.

```sh
python tools/psp_effect_probe.py build/particle-home19 --renderer path/to/home19/rwsky.c
python tools/psp_effect_probe.py build/particle-home20
```

In the first row the color-only case changes from `00081848` to `00583848`
in ABGR, showing the farther layer. The depth restoration control is
`00081868` in home20. It becomes `00583868` when restoration is omitted.

## Installation and device replay

1. Exit the game. Replace `EBOOT.PBP`, `p4data.bin` and `p4str.bin` together
   in the existing P4 directory. Start from the XMB at 333 MHz with sound
   and check the `b249-xmb-home20` marker in `p4.log`.
2. Use the Castle battle and Velvet Room from replay slot 10. Record Zio,
   the All-Out impact and the Apsaras acquisition. Use both dungeon routes
   for the fog comparison, and a route with normal particles for the
   foreground and background checks.
3. For rollback, exit and restore the three files from home19 together.
