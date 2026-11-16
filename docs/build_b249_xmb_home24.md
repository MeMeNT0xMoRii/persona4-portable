# b249-xmb-home24

Test build for the prepared US installation (`SLUS_217.82`), based on home23
at `93f11710de2d3ae70395ec7a5b835b3acceb9e1d`. The sampler change,
GE command reduction and clipping optimization each have their own commit.

## Particle texture addressing

The US PS2 `_rpPTankSkySetupRenderStateAddress` (`0x003A2AD0`) reads two
address fields from `RwTexture` at offset `0x50`. Value 1 repeats and
value 3 clamps. The call sites at `0x003A6720`, `0x003A7B04` and
`0x003A890C` are present in the retail executable. Home23 clamped both
axes in the PTank renderer. Home24 repeats each requested power-of-two
axis independently. Padded and unsupported axes use the existing clamp
behavior. The texture cache key includes both axes.

A/B in PPSSPP 1.20.4 used 10 fractional-UV cases with 576 RGB pixels each,
covering RGBA8888, T8, address changes on a cached raster and padded axes.
Home24 matched the explicit GE wrap/clamp references at all 5,760 pixels
on both Software and OpenGL. Home23 differed at 1,782 pixels in the
cases requesting repeat. Clamp controls and the cached repeat controls
matched. The command list uses 4,228 bytes for 80 binds in either build.
Median guest CPU time per bind was 1.071 to 1.109 µs at 333 MHz.
The extra CPU work selects the texture state. Real particle UVs and the
affected skill frames are still pending on the device.

## Less work for alpha passes and particle clipping

- **Vertex format.** The first draw sets the vertex layout. Subsequent
  draws of the same vertices reuse it in the PSP SDK, the 3D direct
  command writer and the overlapping effect-mask path. The depth-only
  case still sets its own layout when it is the first draw. The A/B lists
  use 15 to 14 words for 2D split alpha, 20 to 19 for indexed 3D direct,
  21 to 20 for indexed 3D SDK and 34 to 31 for the overlapping mask.
  Draw counts are identical. Removing duplicate vertex format words made
  the full command streams identical. The measured CPU gain is small.
  The depth-mask and exact 8-bit alpha controls match home23.
- **Particle clipping.** Precompute the four signed near-plane distances.
  A quad wholly in front of the plane emits the same two triangles
  directly to the vertex buffer. A quad wholly behind it emits zero
  vertices. Crossing quads use the same edge interpolation and fan order.
  All 16,386 tested quads produced identical counts and bytes on the
  host and in PPSSPP, including a vertex on the near plane.

| Clipping only, 115 quads at 333 MHz guest clock | Home23 µs/quad | Home24 µs/quad |
|---|---:|---:|
| Entirely in front | 1.0964 | 0.4909 |
| Entirely behind | 0.3978 | 0.1568 |
| All crossing | 1.4033 | 1.3686 |
| Mixed | 0.9441 | 0.6599 |

The PPSSPP runs used seven alternating A/B samples and reproduced these
medians in separate profiles. Batch sizes 1, 5 and 115 use volumes observed
in earlier PSP Go particle logs. Relative distributions and depth inputs
are synthetic. Host timings included a slower 115-quad crossing-only run.
The timings measure a CPU kernel. Full frame time and physical GE cost
remain to be measured on a PSP.

## Regression checks and device status

- **PPSSPP Software/OpenGL** compared the particle sampler and the changed
  alpha pass outputs to their references. The 8-bit mask control also
  passed in Software. The standalone release starts, loads the data and
  strings and reaches the first font request.
- **Previous synthetic renderer probes** match home23 for PTank depth
  and blending, soft-alpha precision, 16 dungeon-fog samples, full-size
  screen feedback and raster read-lock controls. The 2D depth-mask smoke
  test passes.
- **Packaged MIPS CPU fixture** reaches the same Apsaras preview in six
  synthetic Normal Spread cases with platform and model stubs.
- **Host suite** records 75 passed, two skipped and the three existing
  fixture errors. The Town Map atlas/swizzle, calendar clipping, Shuffle
  Time and fog checks pass.

This build has yet to run on physical PSP hardware. The prepared game
archives needed for gameplay replays were unavailable here. Apsaras
flicker, Zio's bright circles, All-Out brightness and the mixed PTank
alpha/depth state `0x717FB` remain open. The existing particle capture
and historical shadow and arena-TV reports helped scope this sprint.
Exact scene comparisons for those effects require their game assets and
matching PS2 and PSP playback. The ZIP carries the device checklist,
raw A/B results, hashes and reproducible probes.
