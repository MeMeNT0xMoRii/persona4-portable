# Bringing P4 animation costs closer to Crisis Core

Source audit, 2026-11-17. This is a proposed implementation sequence, not a new benchmark or an implemented animation replacement.

## Conclusion

P4 can adopt a more specialized PSP pose path while retaining its quaternion animation. The first substantial target is **fewer materializations, less RW frame traffic and reuse of final pose/skin results**. Replacing quaternion math with Euler interpolation is not a prerequisite.

Crisis Core's inspected path has six prebound scalar channels, static-channel descriptors, an integer delta decoder and explicit result-valid flags. P4 currently traverses several generic interpolator/layer/frame representations before GE submission. Both engines still perform multiple loops and CPU animation work.

The appearance of Crisis Core's animations supports considering bounded visual approximation as a later option. It does not establish that its Euler curves and transition rule can replace P4's curves without changing results. We should preserve P4 motions and eliminate redundant work first, then evaluate a cheaper sampling representation on actual assets.

## What the evidence establishes

- Crisis Core USA: `build/crisis_core/animation.md`, its C function map and Ghidra assembly. CPU animation cycles, actual channel counts and update frequency have not been measured. There is no measured native-animation cost to claim parity with yet.
- Current P4: `port/hanim.h`, `port/anim_tracks.h`, `port/skin.h`; layer, pose, attachment and material-control behavior in `ext/Persona4-Decompilation/src/Graphics/Model/mdlManager.c`; active replacements in `tools/psp_census.py`.
- Historical PSP b272 dialogue A/B, from the actual JSON files: tracks off/on/off/on CPU **56.603 / 56.625 / 56.376 / 56.246 ms**. Reordering/decoding keys into tracks while maintaining the same downstream structures did not produce a useful gain.
- Historical PSP b240 hierarchy windows: 145,472 and 138,400 ordinary hierarchy node visits per 256-frame report; all marked as framed and callback nodes. Matrix-phase cost was **556.5 / 550.3 cycles per node**, and the separate NaN scan **152.1 / 151.3 cycles per node**. The frame-write timing field is truncated in the saved raw log; its exact duration cannot be recovered from that field. The older documentation's residual estimates are not a direct measurement of frame-write time.
- b270 documented inclusive costs: qmodel 3.49 Mcycles, kmat 0.96 Mcycles, actrl 0.76 Mcycles. At 333 MHz those correspond to approximately 10.5, 2.9 and 2.3 ms **in that old profiled run**; parts nest, so they must not be added as independent savings. actrl is primarily material/UV animation, not skeletal animation.
- New extraction of historical measurements and size examples: `build/crisis_core/p4_cost_evidence.json`. These numbers are not a current b358 baseline.

## Correction: P4 crossfades are already prepared snapshots

`mdlStartLayerAnimation` initializes `blendFrom` and `blendTo`, copies/seeks their pose at transition setup, and records the blend duration. In the ordinary transition branch, `mdlAdvanceLayer` blends those interpolators into the current one; that branch does not AddAnimTime to both sources every tick. It switches to the incoming animation when the transition completes.

Therefore the earlier suggestion that P4 necessarily samples two advancing clips throughout an ordinary crossfade was too broad. Crisis Core's current-output-to-target smoothing is still a different rule, but removing a presumed second advancing clip is not a demonstrated optimization for this P4 branch.

## Proposed changes, in order

### 1. Make bone RW frames a compatibility output

Current `p4_hanim_update` builds a local matrix and parent-composed matrix, then can write modelling at frame +16 and LTM at frame +80: up to **128 extra payload bytes per node per update**, in separate frame objects. `mdlUpdateHierarchyPose` also performs those frame writes. Skinning primarily consumes the hierarchy matrix array, so many of these writes may have no reader before the next update.

Proposed behavior:

- Retain compact local/final pose outputs as the authoritative render results.
- Publish modelling/LTM/flags for frames that actual gameplay, attachment, effect, scale-hook or controller readers require.
- A reader of an unpublished frame materializes the required frame/ancestor state for the current pose generation. Unhandled readers keep the original eager path.
- Cache frame-ID-to-node/attachment mappings when the model is bound, with lifetime/invalidation tied to that binding. Avoid repeated frame-tree/name/user-data searches in a frequently used attachment path.

Evidence already available in the current checkout: **b358 `p4_frame_census`** marks frames written by the ordinary hierarchy update and counts reads through both GetLTM names in profiling builds. Reuse it; do not add another getter census. Its direct-memory-read blind spot is explicitly documented, so zero observed GetLTM reads cannot by itself justify dropping a frame. The procedural pose writer and direct frame readers also need coverage before broadening eligibility.

This can preserve every original animation value. Start with hierarchies whose consumers are understood. Keep NaN/equal-key recovery; fold validation into output generation once that generation owns all writes, rather than simply disabling checks.

### 2. Evaluate the final ordinary-body pose with fewer intermediate buffers

Current path: key walk/interpolation -> interpolator frame buffer -> optional crossfade/layer blend -> optional rebase -> hierarchy/pose traversal -> RW frame outputs -> skin matrices -> GE bone words. Lazy handling defers work, but `p4_anim_materialize` still replays full interpolation, blend and rebase stages into those compatibility structures.

Build a binding-time descriptor containing parent indices, supported callback/layout, active/constant channel information, layer-node mappings, inverse-bind bindings and required consumer outputs. These are properties of the bound model/animation, not things to rediscover per bone each draw.

For the first supported body case:

1. Advance original clocks and process transitions/callbacks/events at their original boundary.
2. Obtain the node's current quaternion/translation using the existing interpolation math; when applicable, apply the original snapshot blend and rebase in their original order.
3. Construct the local transform and compose with its prepared parent. Store the final hierarchy result and only the compatibility outputs needed by known readers.
4. Reuse the resulting pose for repeated draws until one of its dependencies changes.

The first case should be an ordinary skeletal body layer with recognized layout, no procedural look flags `0x81E0`, no unhandled scale hook or intermediate reader; keep original fallback for the rest. Measure how much work this case actually covers. A fast path that processes zero or few nodes is not a successful optimization.

P4 already has fused quaternion-to-matrix/parent math; implementing that again is not the proposed change. The change must remove intermediate writes and repeated stage traversals. Likewise, per-node tracks already exist: reuse their knowledge where useful, but avoid making a full decoded-clip cache mandatory, since its isolated A/B did not help.

Look-at is a second stage: keep its stateful controller update and dependency order, reuse ordinary local transforms where possible, and recompute the affected subtree. Bone-frame dependencies and parent transforms can require a boundary between sampling, controller solving and final composition. Do not promise one literal loop for every character.

### 3. Reuse skin matrices for the same pose and skin

`draw_skinned_ge` calls `skin_bones` before ready-list replay. `skin_bones` calls `anim_drawn`, then multiplies each skin-to-bone matrix with the hierarchy matrix. The result is kept in a global scratch array; it is not automatically a persistent result of the hierarchy's current pose.

Candidate: cache that product per **skin binding and pose generation**, and reuse it across geometry pieces/passes where the input matrices are unchanged. Prepared palette/scatter mappings already exist in the skin-list path. Keep that machinery instead of adding another list recorder.

The validity key must include the actual hierarchy pose, skin/inverse-bind binding and relevant space/override changes. A frame number alone is insufficient: the same hierarchy can be modified within a frame. A reused hierarchy pointer after resource free is also insufficient. Scene/resource retirement must invalidate ownership safely.

Historical `sbones` includes more than pure matrix products, and reuse across parts has not been counted. Treat this as a concrete bounded candidate; do not claim the whole sbones section can disappear.

### 4. Specialize material/UV animation separately

`mdlAdvanceAnimControl` walks a material-object resource list. `mdlStepAnimObject` queries MatFX effects, selects up to two UV interpolators, uses SetCurrentTime, refreshes callbacks and applies matrices. This is separate from skeletal SLERP.

Candidate: bind the active animated-material slots and their layout once, then update their necessary UV/texture command fields when time/value changes. Preserve absolute-time, rewind/loop, copy/no-op callback and shared-clock behavior. The existing skipped-tick deferral already avoids some work; quantify the remainder before changing it.

Changing skeletal key compression will not remove this measured section. Keep its before/after measurements separate from body pose work.

### 5. If necessary, compile animations to a cheaper PSP sampling format

After reducing the surrounding traffic, a visual path can use offline-resampled **quaternions and translation** with constant channels removed. Uniform or block-indexed samples eliminate generic key searches; sign-continuous quaternion pairs can use normalized linear interpolation, with sufficient sample density for the desired trajectory. There is no need to convert everything to Euler angles.

This is an approximation candidate, not an exact replacement for original SLERP/rebase. Select sample density and quantization by measured final bone/skinned-point error over original key intervals, near-camera screen-space error, and visual checks of faces, hands and attachments. Loop seams, fractional/negative speeds and animation transitions require explicit checks. Game events and end conditions retain their original clock; gameplay-critical transforms remain exact or use a separately validated path.

Prefer compact on-disk records with a bounded resident window or selected active clips. Do not cache every clip as float TRS or world matrices. Illustrative upper examples, **not actual asset census**, for 100 bones over 10 seconds:

| Representation | 30 samples/s | 60 samples/s |
|---|---:|---:|
| 16-byte packed quaternion/translation sample, excluding tables | 480,000 bytes | 960,000 bytes |
| 48-byte float matrix per node/sample | 1,440,000 bytes | 2,880,000 bytes |

The old tracks experiment exhausted the game heap with about 1.7 MB of additional tracks. Any compiled format needs an explicit resident-memory budget and resource lifetime; count remaining original animation data too. Constant-channel omission, variable rates and disk compression may reduce size, but have not been measured on these assets.

## Validity and correctness boundary

Keep simulation clocks, callbacks, root motion, attachment dependencies and animation completion on every required logical tick. Expensive visual output can wait for the first real consumer. The current port already defers some skipped-tick work; the remaining opportunity is consumer-driven materialization, not changing a 60 Hz simulation into one doubled step.

A pose generation depends on sampled times, clip bindings, layer weights, rebase state, procedural state/targets, scale and required parent/root space. Publish/reuse only a completed compatible generation. Moving the model can invalidate world matrices without requiring a new local key sample. Preserve that distinction.

Dead-node pruning requires the union of visible skin influences, their ancestors, attachments, gameplay/effect readers and procedural dependencies. The old bone census is a clue, not proof that an unweighted node can be deleted.

## Measurement and acceptance

1. Fresh baseline at 333 MHz on the exact current binary, with profiler off for presented-frame/CPU measurements. Use dialogue, multi-character field and battle with moving weapons/effects. Record quality switches and hashes.
2. Reuse existing `p4_anim_freeze` diagnostic and `p4_frame_census` to localize remaining work before writing a broad replacement. Freeze changes behavior/cache/overlap and can stall game progression; it is a diagnostic comparison, not a shipping solution or an additive mathematical ceiling.
3. Count actual pose generations, repeated identical requests, node visits, frame writes/readers, skin-product calculations and supported/fallback nodes. Existing counters should be reused; add only missing distinctions.
4. For each implementation step, compare final skin matrices, required RW frames, controller state, attachment positions and events. A same-input numerical test alone does not validate real reader ordering. Compare reference and candidate rendered/gameplay behavior from a fresh process/native save.
5. Measure one change at a time using held-scene hardware A/B/A. Report CPU time, presentation deadlines, GE time/overlap, memory high-water and fallback coverage. Keep changes only when the actual CPU/presented-frame result improves.

To claim cost parity with Crisis Core, measure its animation call frequency, active bones/channels and actual CPU cycles on comparable hardware. Until then the useful target is matching its **amount of necessary work per produced pose**, then proving the P4 reduction. Animation improvements alone do not establish whole-game 30 FPS: static/render preparation, effects and UI remain separate costs.

## Scope of this audit

No port implementation or device state was changed. Existing unrelated worktree edits, including the b358 frame census, were read and preserved. The recommended first production experiment is frame-output materialization for a measured eligible body case, followed by end-to-end ordinary-body evaluation; it is not another isolated VFPU interpolation rewrite.
