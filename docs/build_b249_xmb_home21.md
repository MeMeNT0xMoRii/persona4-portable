# b249-xmb-home21 effect-mask test build

Home21 builds from [home20](build_b249_xmb_home20.md), GitHub revision
`0dde6a97f994c2cd5a51a1b8ca2a1213221a066d`. Replace the three runtime files
together in the prepared US `SLUS_217.82` installation.

## Destination-alpha mask precision

The US PS2 executable (`SLUS_217.82`, SHA-1
`4eeec0360cf2715535d9f7e52eb69d786fb0158c`) and its census identify a mask
followed by a destination-alpha blend. `DistortParticleDrawSky` at `0x004AFE20`
submits a four-vertex strip with `TEST_1=0x31001` and RGB masked. It then
changes the UVs for the overlay. The reference and second passes of
`effBlurFilter` (`0x004A98D0`, `0x004AAD30`) submit the mask as an indexed quad.
The original index table at `0x00714368` reads `0, 1, 2, 0, 2, 3`. Those
passes use the previous mask alpha at the destination. The overlay keeps its
own texture and UVs.

Home20 represents the GE fragment alpha with four stencil values. That
creates visible steps when the next draw uses the destination alpha as a
blend factor. Home21 clears the alpha under a supported mask quad, then uses
the alpha test and the write mask to copy the eight fragment-alpha bits
into the existing framebuffer stencil. The write mask protects RGB and the
passes keep the active depth test and scissor. The original depth-write
setting is restored after the mask. Two triangles qualify when they share a
diagonal and their remaining vertices lie on opposite sides with room for
GE subpixel rounding. Folded, overlapping and other unverified layouts use
the home20 four-pass path.

The result preserves the PSP's sampled and modulated mask alpha at full byte
precision. Color, feedback capture and the blend functions use the existing
path. Home20's PTank write-mask fix applies independently. The supported
mask path submits nine draws per quad, up from four in home20. The cost of
scenes with a lot of mask coverage needs a PSP Go measurement. The project
uses this only for the existing screen effect-mask branch. See the
[GE raster pipeline measurements](https://www.ppsspp.org/docs/psp-hardware/gpu/raster-pipeline/)
for the register operations and raster coverage rules.

## Verification

| Test type | Result |
| --- | --- |
| PPSSPP 1.20.4, software, synthetic 2D masks | All 256 source alpha values matched in the strip. The indexed quad matched all 256 samples with vertex-alpha modulation, yielding 129 distinct alpha values. The scissor and depth controls preserved outside pixels and confirmed depth-write restoration. RGB stayed byte-identical during the alpha write. The folded strip matched home20. |
| PPSSPP, synthetic blend and particle controls | The `0x58` additive ramp followed the mask gradient. The isolated home20 PTank depth, normal particles, opaque foreground and four blend-mode comparisons matched byte for byte. The green, black and white 2D depth-mask controls passed. |
| PPSSPP, synthetic dungeon fog | All 16 samples matched home20, including the camera far clip, the Sky override and fog-off controls. |
| PPSSPP, packaged executable startup | Both data files loaded. The HOME marker and `kwlnBoot` logged, followed by the request for `font/size12/f11_001.tmx`. The prepared game archives were unavailable. |
| MIPS CPU fixture, packaged PRX | Six Normal Spread fixtures reached the Apsaras preview with the same color and trace results as home20. These exercise the menu code with synthetic inputs and stub platform/model callbacks. |
| Host test suite | 75 passed, two skipped, three baseline harness errors. The calendar scissor, Shuffle Time culling, legacy Town Map atlas/swizzle and fog tests passed. The remaining errors are the Windows VAG compiler and two existing renderer harness builds. |
| Release link and sanity checks | Native PSPDEV GCC 15.2.0 compiled the port sources and linked the 821 verified home20 generated objects. The link validated 19 import libraries. The recovered PRX and 5,857 data relocations and 168 switch targets passed the artifact audit. |

To reproduce the PSP mask and particle fixtures, set `PSPDEV` to a native
PSPSDK toolchain and run the same current script for both renderers.

```sh
python tools/psp_effect_probe.py build/effects-home20 --renderer path/to/home20/port/rwsky.c
python tools/psp_effect_probe.py build/effects-home21
```

Boot the generated PBPs in the PPSSPP software renderer. The emulated memory
stick reports `p4-effect-probe.txt` and `p4-mask-precision.txt`. The latter
contains four 256-pixel rows for the strip, indexed quad, scissor/depth check
and folded-strip control. The original additive row has alpha
`0 0 85 85 170 170 255 255` for inputs `0 32 64 127 128 160 224 255`.
Home21 records the input alpha at every sample.

## Device replay

Zio and All-Out still need paired game captures at the same animation phase.
The trace for Apsaras in the home18 video confirms a varying light region
around the model. The available device logs predate the `FUSION visual`
records needed to associate it with a buffer or a mask pass. The model
flicker and the effect IDs behind the reported light spheres remain open.
The mixed PTank `0x717FB` state also remains open.

1. Back up the existing runtime files and saves. Replace `EBOOT.PBP`,
   `p4data.bin` and `p4str.bin` together. Boot from the XMB with sound at
   333 MHz and check `b249-xmb-home21` in `p4.log`.
2. From slot 10, replay Zio on a Lying Hablerie and record the summon,
   impact and fade. Repeat Garu, a normal particle scene and an All-Out
   Attack at the same phases. Compare home20 using the same save.
3. From the same slot, fuse Izanagi and Ukobach, complete the acquisition
   and hold the Apsaras dialogue for two seconds. Keep `p4.log` with the
   `FUSION visual` lines and the capture. From the Castle and Yomotsu saves,
   compare the long fog corridor approaches. Use slots 8 and 9 for System,
   date changes and the Town Map.
4. Check PSP Go frame cadence during a sustained mask effect. For rollback
   exit to the XMB and restore all three files from home20 or home19.

Full gameplay and both PSP Go and PSP-3000 device runs are pending.
