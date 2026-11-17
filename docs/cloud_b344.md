# Single-pass enemy clouds (b344)

`DistortParticleDrawSky`, mode 0, used two consecutive draws: a soft texture
wrote scratch framebuffer alpha, then the noise texture blended by that alpha
(GS ALPHA 0x54). On PSP the first draw needed a clear plus several stencil bit
passes. b344 combines the P4 noise RGB with the soft mask alpha in an RGBA8888
texture and draws that pair once with ordinary source-alpha blending.

This follows the resource approach found in P3P; it uses P4's own textures.

## Scope and fallback

- Only `DistortParticleDrawSky` mode 0 is hooked by the census adaptation. The
  adjacent subtractive mode 1 stays unchanged.
- The fast path requires matching geometry, depth and uniform tint; the fixed
  64x64 T8 mask and 128x128 T4 noise; no render texture or depth writes; TEST
  0x31001 and ALPHA 0x54. All eight UV reflections/transpositions are supported.
- Unsupported pairs retain the complete original writer/reader path, including
  render-state restoration. Rejection counters distinguish size/format, state,
  geometry and cache exhaustion in the existing periodic log.
- Source pixels and palettes are compared when the frame or texture generation
  changes. Four cache slots retain old data for more than four presented frames
  before reuse; a busy cache falls back. Renderer teardown waits for GE and
  releases the slots.
- One cached pair costs 537664 bytes, including eight swizzled images and source
  snapshots. Four slots bound the allocation to 2150656 bytes.

The hook treats this mode's alpha writer as scratch for its immediate noise
reader. That contract was checked against the observed draw sequence; absence
of later framebuffer-alpha consumers is **not** a general geometry guard.
Do not extend this hook to arbitrary Im2D pairs, screen-feedback fog or Field
Shadow without auditing those consumers.

The mask is resampled at noise-texture resolution. Filtered edges are close to
the original path rather than bit-identical. No precision reduction is added
to the fallback; the parallel b343 coarse-mask experiment is preserved.

## Validation

Final diagnostic artifacts: `build/psplink_b344_final`. The installed EBOOT,
data image and strings are checked against these artifacts. The build validates
19 PSP import libraries and has no unresolved function-pointer targets.

Host checks pass:

- `test_cloud.py`: all eight UV transforms, RGB/alpha output, linear/swizzled
  inputs, palette mutation, queued-texture lifetime, allocation failure and
  guarded fallback.
- `test_census.py`: includes the retained two-pass noise fallback.
- `test_ui_draw.py`: 8192 renderer configurations plus crowded-mask retention.
- `test_alpha_mask.py`: existing alpha/depth behavior.

Runtime started a fresh final-b344 process, loaded native card slot 3 (03/20,
Heaven), moved through the scene launcher to field 7/2 and started encounter 1.
Only then was a new matching b344 state saved for repeatable comparisons. Old
user states were not used to establish the new executable's behavior. Native
card file hashes are checked unchanged.

The same 20 presented frame numbers were run new/reference/new. Reference:
`p4_cloud=0`, `p4_alpha_mask_coarse=1`; both paths used frameskip 0. Each new run
handled 373 pairs with one cached texture build and zero recorded refusals.
The two new-path screenshots are byte-identical. Compared with the reference,
the final screenshot's mean absolute RGB differences are 0.426/0.200/0.157 on
the 0..255 scale; maximum differences are 9/5/6.

| Captured frame | Reference | New path |
|---|---:|---:|
| Mask bit/clear draws | 337 | 0 |
| Noise pair draws | 40 | 40 |
| Separate subtractive cloud draws | 40 | 40 |
| All GE draws | 923 | 586 |

The scripted 20-frame emulator runs took 0.947 / 1.363 / 0.929 seconds
(new/reference/new). These include debugger stops and are not an unrestricted
FPS benchmark. No physical PSP speed or memory-pressure result is claimed.

Evidence, images, GE captures and `comparison.json` are under
`build/cloud_b344_final`. A separate `enemy_spawn_b344.ppst` demonstration state
is saved alongside the emulator states; original user state slots are preserved.
Loading an older state restores its older executable code and settings.
