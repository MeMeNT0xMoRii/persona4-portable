# Portrait masks and translucent 3D surfaces (b251 candidate)

## b253 correction: GE alpha is stencil, and small feedback contains menu text

The b251/b252 fragment-output assumption below was wrong. The GE does not write
fragment alpha when stencil is disabled. The old host recorder checked the
chosen calls but did not simulate this hardware rule. See PPSSPP's
[`ConvertMaskState`](https://github.com/hrydgard/ppsspp/blob/master/GPU/Common/GPUStateUtils.cpp):
alpha writes are disabled without stencil, and output alpha is the stencil value.
The earlier visual improvement with `p4_alpha_pixel` was not proof of a working
per-pixel mask.

On real b252, System -> Title was a blank black selected row. Disabling just
`p4_2d_alpha_masks` did not restore the label. With masks 0 and
`p4_dst_alpha_passes` 1 the white label returned; turning masks 1 on again made
it disappear even with destination-alpha passes enabled. Captures:
`${P4_HOST}/b252/menu_{report_before,masks_off,dst_on,masks_and_dst_on}.png`.

b253 restores stencil REPLACE for the base binder. `port/alpha_mask.h` handles
2D main-screen alpha-only masks with depth off, DATE off, and no ATEST colour
rejection (disabled/ALWAYS or FB_ONLY). Constant alpha, including zero, costs
one stencil draw. Varying/textured alpha uses a zero write and eight bit-masked
alpha-test/stencil draws per triangle. Completing triangles in order preserves
overlap semantics; all RGB bits remain masked. Thus all 256 GE fragment-alpha
values can be stored, including texture/interpolated alpha, without pretending
that the GE supports GL-style fragment-alpha output. Other mask classes retain
the previous constant-stencil path. The extra passes can cost fill/commands and
need measurement on scenes with many masks.

`p4_dst_alpha_passes` defaults to 1: the old size-based suppression also removed
the selected menu label's small display-raster reader. This restores the reader
together with its mask writer. Check field feedback effects/title/minimap and
level-up animation, in addition to the System menu; b253 is experimental until
those scenes pass. `tools/test_alpha_mask.py` models the GE rule and checks all
256 values, preserved RGB, overlapping triangles, zero/constant fast paths,
triangle strips/fans and unsupported-state fallback. Actual PSP checks remain
separate from that host test.

Izanagi reference checked at `bfb12b6896955b626cc45254c92e8b76789ae147`:
[`renderer_port.md`](https://github.com/Raikaru/Izanagi/blob/bfb12b6896955b626cc45254c92e8b76789ae147/docs/renderer_port.md)
documents the cut-in queue `0x795050` and independent backdrop/pattern/divider/
portrait/glint materials; `p4gl_atomic.c`, `p4gl_camera.c`, `p4gl_mdleffect.c`,
and `p4gl_state.c` provide useful comparisons for All-Out Attack and masks.
Their GL alpha output must be adapted to GE stencil operations on PSP.

## Historical b251/b252 implementation (superseded for 2D masks)

Prepared on 2026-11-14. **No PSP restart, deployment or launch command was issued
by this work.** The shared session subsequently showed an external b250 launch;
b251 was only built on the PC.
The candidate compiles on the PC and passes host regression checks; it still
needs the animation and scene checks below on the device.

## Level-up portrait: fragment alpha for 2D masks

In b249's held hero level-up screen, the portrait's 256x512 texture was present
but a white rectangle covered it. The draw sequence contains textured
alpha-only masks (FBMSK 0x00ffffff), followed by untextured destination-alpha
blend passes (0x54 and 0x58). At the settled screen the final mask and white
pass both have vertex alpha 0. The old small-mask suppression and coverage
alpha test prevented that zero-alpha mask from clearing destination alpha.

Live b249 checks, restored to their defaults afterward:

- `p4_dst_alpha_passes=1` alone: still white.
- Enabling those passes and disabling `p4_alpha_write_test`: portrait visible.
- `p4_alpha_pixel=1` alone: portrait visible.

Artifacts: `${P4_HOST}/b249/levelup_{alpha_passes,no_atest,pixel_alpha}.png`,
baseline `${P4_HOST}/b249/result_portrait.png`; census and findings in
`build/psplink_b249/experiments/levelup_*`.

b251 adds `p4_2d_alpha_masks=1`: the 2D path writes fragment alpha with RGB
masked, allows zero-alpha clearing, and retains alpha-only draws independently
of the legacy screen-feedback filtering. `p4_alpha_pixel` stays 0, so this
does not globally change 3D stencil/DATE policy or enable the previously
suppressed small framebuffer-copy effects. Set `p4_2d_alpha_masks=0` for the
old 2D behavior. The original effect geometry, timing and blend passes remain.

## Umbrella: render proved RGB_ONLY glass after its opaque background

The April 15 park scene's `kasa_toumei` canopy has material color 0x99ecebe5,
normal blend 0x44 and TEST_1 0x735fb (GEQUAL, reference 0x5f, RGB_ONLY on
failure). Its effective GE alpha cannot reach the converted reference 190,
so the existing renderer's failed-alpha pass paints RGB without writing depth.
Trees submitted afterward can overwrite the canopy.

Live b249 check: disabling `p4_gs_test` made the white canopy visible, but this
was only a diagnosis, not a faithful transparency fix. The experiment was
restored. `${P4_HOST}/umbrella_current.png` and
`${P4_HOST}/b249/umbrella_no_gs_test.png`; raw census/findings in
`build/psplink_b249/experiments/umbrella_*`.

b251 adds experimental `p4_translucent_late=1`. It preserves the alpha test,
opacity, blending and depth test. Only a narrow class is eligible:

- Unlit, unweighted standard 3D float vertices, normal blend, translucent
  material, main screen, depth testing, no pixel mask or DATE.
- ATEST GEQUAL/GREATER with AFAIL RGB_ONLY.
- Texture/vertex alpha upper bounds **prove every fragment fails ATEST**.

Those RGB-only draws are saved and emitted back-to-front after opaque 3D,
before 2D or the camera/frame ends. Their RGB is blended over the actual
background; alpha and depth stay masked as the original failed-alpha pass
requires. It does not force the umbrella opaque or turn off GS tests elsewhere.

The queue snapshots model/view/projection, texture and render state. Indexed
vertices are copied in primitive order to unindexed data in the current GE
list, so CPU scratch reuse and the next frame cannot mutate queued geometry.
Only potentially affected materials bypass command/instance/persistent replay;
opaque caches remain available. Texture mutation/destruction, clear and camera
boundaries flush the queue. It is bounded to 32 meshes/32 KiB of copied
vertices per frame; unsupported/full/allocation-failed cases draw immediately
through the original path, rather than dropping geometry. Logs report queued,
drawn and refused counts. Set `p4_translucent_late=0` to restore the old order.

Limits: only this unlit RGB_ONLY class is reordered; it is not a general
transparent renderer. Mesh-average depth sorting does not solve arbitrary
intersecting transparent geometry. The actual umbrella appearance and camera
pass boundaries have not yet been validated in b251 on hardware.

### PS2 reference (PCSX2 GS dump, 2026-11-15)

Dump: 04/12 early-morning school route, hero's clear umbrella
(`PCSX2/snaps/...20261115023639_(2).gs.zst`, first frame, 158 draw batches).
The canopy appears as draws 47 and 49 (screen 337..400 x 149..184):

- `TEST_1 0x535fb` (GEQUAL 0x5f, RGB_ONLY), `ALPHA_1 0x44`, vertex alpha
  **0x39** on every vertex. With TFX MODULATE, As <= 0x39 < 0x5f, so **no
  fragment passes**: on PS2 the canopy itself is RGB-only, at most 45% opaque,
  and writes no Z. The failing alpha test is correct, so the fork's raw-reference
  fix (`p4_glass_alpha_ref`, 0x5f against an alpha of 153) does not match the PS2.
- Each canopy draw is followed at once by a **second pass on the same
  vertices** (draws 48 and 50): context 2, its own 64x64 PSMT4 texture
  (TEX0_2 `0x2007f3859940bf80`), `ALPHA_2 0x80_00000068` = Cs*FIX/128 + Cd with
  FIX 0x80 (additive), `TEST_2 0x50000` (no alpha test, Z GEQUAL), **Z write on**.
  This pass gives the sheen and writes the canopy's depth, so the trees and
  wall drawn after it (draws 60-98, farther Z) cannot paint over the canopy.
- Only the two canopy materials carry plugin data (census: `0a0d7d40`,
  `0a0d7dd0`; the rib materials have 0): the MatFX environment-map setup
  (`mdlSetupMatFXMaterial` / `RpMatFXMaterialGetEffects` in the game). The
  port does not draw this pass at all. Draws 2-7 of the same frame show another
  material with the same dual pass (`ALPHA_2 0x48`).

So the faithful fix is the MatFX env-map second pass (additive, Z write, no
alpha test), not a reordered RGB-only pass. Once that pass exists,
`p4_translucent_late` should be unnecessary for the umbrella. Recheck before
removing the queue.

Implemented as `p4_matfx_env` (on; 0 restores the old drawing) in `rwsky.c`
(`matfx_env_pass`). Material + `iGpffffb5e0` points to the MatFX data: two
0x40-byte effect entries, flags at +0x80. An env entry (+0x20 == 2) holds
frame +0, texture +4, coefficient +8, frame-buffer-alpha flag +0xc (the game's
accessors 0039a090-0039a400). After the atomic's normal draw, each env mesh is
drawn again: UV = 0.5 + 0.5 * normal in view space (in the env frame's space when
it has one), colour = prelit x material x coefficient, blend Cs + Cd, alpha test
off, depth GEQUAL with write, alpha byte (stencil) masked. Env-mapped atomics skip the
instance batch, far proxy and static-list shortcuts, and are never queued by
`p4_translucent_late`. Skinned ones take the CPU skin path (`p4_matfx_env_skin`).
Not covered: world sectors, the frame-buffer-alpha variant (logged), and the
guard band (an env triangle far outside the screen is dropped whole by the GE).
`p4_probe_3d=1` logs each env material's texture, frame and coefficient. The
per-256-frame `matfx env` line counts atomics, draws and vertices. Open questions
for the device: does the coefficient give the dump's ~0.1 colour ratio, and is the
UV orientation of the sheen right?

## Validation and next device checks

`tools/test_translucent.py` executes the real mask binder and late-pass header
with a GE recorder: zero-alpha masks, DATE preservation, eligibility and alpha
threshold boundaries, copied indexed geometry after source mutation, far-to-near
order, restored state, disabled/unsupported cases and bounded storage fallback.
The full diagnostic link also checks PSP imports and fixed data-image placement.

On the next authorized restart:

1. Level-up: compare appearance, moving highlight and fade, then the settled
   portrait. Check other 2D UI/minimap and camera-texture masks.
2. April 15 park: compare both umbrella sides and camera angles. Trees should
   remain faintly visible through white plastic, not paint over it. Inspect
   near objects for correct depth rejection; check `late RGB pass` counters.
3. Toggle each new switch independently to compare against b249. Keep audio/
   video recycling and the existing experimental performance controls unchanged.
