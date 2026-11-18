# b325: exact static position packing candidate

This extends b324's UV packing with s16 positions in retained GE lists. It uses the compact integer
vertex strategy observed in P3P, not a replacement for P4's geometry loader. Hardware correctness,
coverage in actual scenes and FPS benefit remain unverified.

Only a single unlit UV-packed stream qualifies. Mixed lists, normals/weights, guard-clipped streams,
GE bounding-box lists and extra model-matrix commands retain float positions. Every coordinate must
fit exactly in signed 16-bit GE units with a common positive power-of-two scale. Inexact, non-finite
or out-of-range coordinates reject the whole conversion. No positions are rounded.

The retained vertex stride becomes 16 bytes, versus 20 for b324's unlit UV packing and 24 for the
original float layout. UVs and colours remain byte-identical to b324. The list owns its packed copy
and original indices; no additional temporary allocation is needed. Original geometry arrays, CPU
clipping/bounds and the initial submitted draw keep their existing float positions.

Both normal replay and same-frame instance replay multiply only the model's three basis rows by
the decode scale. Translation remains in world units. Power-of-two scaling preserves the exact
object coordinates; this still needs device verification of GE transforms and image boundaries.
The nine extra basis multiplies per replay can offset the reduced vertex traffic on small meshes.

`p4_static_position_pack=1` enables the candidate; `0` retains b324's float positions. The switch
participates in list keys and instance memoization. Keep `p4_sl_trust=0` when switching. The log's
`static position pack` counters report converted vertices and payload bytes saved during recording,
not replayed vertex counts or measured FPS. Zero vertices means this scene has no eligible records
yet; try a fresh scene with packing enabled.

Host tests exercise 5,120 exact positions over 40 binary scales, unchanged UV/colour bytes, invalid
and inexact inputs, real list copying/relocation, restricted-path rejection, replay matrix scaling,
instance translation and switch invalidation. Existing UV, alpha, buffer and static-state tests pass.

PSP acceptance: fresh boot, same scene/camera at 333 MHz, check nonzero conversion counters, then
compare off/on/off with `p4_static_list_check=0` and `p4_static_uv_check=0` for timing. Inspect adjacent
mesh seams, repeated instances, camera movement, transitions into clipped views and transparent
overlays. Separately enable list checking to inspect rebuild differences. No measured speedup is claimed.
