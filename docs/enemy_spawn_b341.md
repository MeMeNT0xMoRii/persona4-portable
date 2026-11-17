# Enemy appearance alpha masks (b341 and b342)

b344 replaces supported DistortParticle noise pairs with one draw; see
[the implementation and validation](cloud_b344.md). b341/b342 below describe
the fallback mask corrections.

## b342: crowded clouds

The next supplied state still showed rectangles after redraw with b341. Its
many small clouds triggered `p4_dst_alpha_many`: the old screen-feedback
optimization skipped their alpha writers, while their noise-texture readers
continued drawing. b342 retains enabled 2D alpha masks even when that threshold
is exceeded. Screen-feedback readers retain their existing skip behavior.

Both `test_alpha_mask.py` and `test_ui_draw.py` pass. The latter exercises 8192
draw configurations plus crowded-mask retention and legacy skip behavior.
The diagnostic b342 build validates 19 import libraries and has no unresolved
function-pointer targets. Installed EBOOT/data match `build/psplink_b342`.

Runtime evidence is in `build/enemy_spawn_crowded_b342/fixed_live.png`. The
supplied b341 state was repaired by setting `p4_dst_alpha_many=0`; it then
rendered soft clouds with the damage numbers visible. The separate
`enemy_spawn_crowded_fixed.ppst` retains b341 code with that broader workaround.
New b342 launches use the narrower source fix. Loading the original state
restores its old code and settings. Physical PSP and a fresh b342 reproduction
of this battle remain unchecked.

## b341: depth-tested masks

The enemy appearance cloud drew as orange and black rectangles. Its textured
alpha-only passes enable depth testing and disable depth writes (TEST 0x31001,
alpha NEVER with FB_ONLY). `draw_2d_alpha_mask` rejected every depth-tested draw,
so the fallback replaced the stencil with one constant alpha across each quad.
The following ALPHA 0x54 noise pass then read that rectangular mask.

Allow depth testing when reconstructing alpha. All bit passes see the same
unchanged depth buffer; stencil KEEP on depth failure preserves occluded pixels.
Depth writes, DATE, render textures and unsupported alpha rejection still use
the existing fallback.

Validation:

- `python -m unittest discover -s tools -p test_alpha_mask.py` passes, including
  textured masks, overlapping triangles, depth failure and unsupported states.
- Diagnostic b341 builds with 19 valid import libraries and no unresolved
  function-pointer targets; artifacts are in `build/psplink_b341`.
- The supplied PPSSPP state reproduced the rectangles. Applying the equivalent
  instruction change to its b340 code produced a soft cloud. The newer supplied
  slot contains b341 code and also renders the soft cloud on subsequent frames.
  A loaded state's saved framebuffer can still show the old rectangles until
  the renderer finishes another frame.
- A fresh b341 process loaded a native card save into the living room. The
  appearance effect itself was checked from the supplied states, not reproduced
  from that native save. Physical PSP performance and other depth-tested masks
  have not been checked.

Screenshots and the previous installed executable/data backup are under
`build/enemy_spawn_validation`; the original supplied state files are preserved.
