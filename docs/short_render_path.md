# Prepared PSP rendering: investigation, 2026-11-15

Target: sustained 30 FPS at 333 MHz without reducing visual quality. This is an architecture proposal, not a measured speedup or completed implementation.

## Fresh live Crisis Core evidence

Connected to PPSSPP debugger 56779: ULES01044 v1.00, running. New artifacts are in ignored `build/crisis_core_shortpath/`; scripts `build/cc_shortpath_probe.py`, `cc_shortpath_compare.py`, `cc_shortpath_watch.py`. Temporary CPU and memory breakpoints were removed and execution resumed.

Two CPU command-stream snapshots: 171 candidate CALLs to 115 targets; 94 compared prefixes unchanged. Changed words across compared prefixes: 1,645 BONE_DATA and one TEXOFFSETU. Prefixes are capped at 512 words, and the walker ignores BJUMP conditions, so these are neither whole-block immutability proofs nor executed draw totals. Six floor payloads still match the earlier captured data byte for byte.

Write watches: floor vertex range 0x0929864c, 34,360 bytes; floor command block 0x09d919c0, 4,960 bytes. Neither was written during 1.0013 seconds, with CPU ticks advancing 333,332,940. A watch on the main command buffer did trigger, validating the mechanism. The floor starts with VTYPE, bounding-box address, BOUNDINGBOX 8, BJUMP, then texture/material/draw commands. This proves persistent prepared floor data in this scene. It does not establish the cost of the game's CPU object traversal, animation, or checks before CALL, nor when the data was originally prepared.

## Port findings

`static_list.h:sl_draw` still tests guard eligibility, hashes render/sky state and each material, optionally collects/hashes lights, and compares source arrays, camera, texture generation and switches before emitting BASE/CALL. `sl_find` is a two-way associative lookup; current source has a b277 enlargement addressing b276 churn. Historical b276 miss counts must be remeasured before attributing all misses to clipping.

`rwsky.c:atomic_render_body` reaches prepared lists after frame synchronization and routing through other render paths. `p4_skin_list` already records character commands and patches bones into three buffers; adding another recording layer alone will not remove pose, palette, guard or validation costs.

Per-node animation tracks have already failed to improve unprofiled dialogue CPU time (b272). Optimize the number of traversals and outputs, not just key decoding. `ge_prepare.h` already implements subdivision and a camera-dependent edge bound; moving preparation offline does not by itself remove camera-dependent classification or prove correctness for every camera.

## Proposed representation

PC asset compiler emits versioned, content-identified bundles: unchanged-detail geometry, GE-ready vertex/index layouts, spatial bounds, command templates, relocations, material dependencies, patch offsets, skeleton parent order, attachment dependencies, bone palette maps and animation key indices. Prefer exact formats first; quantization, weight pruning, LOD and texture recompression require separate visual approval and are not assumed savings.

On scene load, resolve relocations and texture residency once, bind each source object directly to a compact render record, and allocate persistent scene-owned storage. Avoid replay records competing in a general per-frame mesh cache. Account for original RW data, prepared copies, texture storage and in-flight buffers together; PC preprocessing does not enlarge PSP RAM.

Drawn frame: update view and pass state; evaluate needed model poses; update only dirty material/UV/texture patches; traverse compact visible render records in required order; write model matrix and CALL. Use a scene or spatial-group list where transforms/order are stable. Preserve transparent ordering, outlines, shadows, alpha/DATE reconstruction, framebuffer feedback and pass boundaries. One giant scene list is not required.

Move invalidation to known mutation sites. Use per-resource versions/dependency queues for geometry, materials, rasters and lights; setters must detect actual value changes. Direct game stores require identified producer hooks or a conservative path. One global texture generation must not invalidate every unrelated object. Dynamic UV/light/bone values should patch commands, not rebuild geometry. Establish and invalidate the CPU state shadow at prepared-list boundaries. Never patch a buffer the GE may still read; publish data with appropriate cache writeback and retain resources through completion.

Static clipping: prepare spatial subdivisions against supported near plane, FOV, camera range and object scale. Hardware bounding-box rejection is culling, not a cure for visible triangles crossing the PSP guard window. Validate bounds with real-device image sweeps; retain a correct fallback for uncovered cameras. Splitting can alter rounded colors, lit normals and seams, so tessellation also requires visual comparison. Budget increased geometry against GE time and RAM.

Character path: precompute palette/scatter mappings and parent order. Sample and combine required layers, apply procedural look/pose in correct dependency order, compute bone matrices, then scatter into known palette command offsets. Write RW frames only where readers need them, retaining an exact materialization path for unexpected readers. Attachments, gameplay bone queries, shadows and layer rebasing can require intermediate results; do not assume a literal single loop is valid for every character.

Skipped simulation tick: keep clocks, transitions, events, root motion, physics, spawning and any observed transforms. Defer only unobserved visual pose/materialization, projection and queue construction. Preserve the game's original interpolation/blending semantics and event crossings; replacing two updates with one doubled delta is not generally equivalent.

## Measurement and acceptance

333 MHz gives 11.1 million cycles per 33.33 ms presented frame, including simulation ticks between presents. CPU and GE overlap, so do not add their inclusive times blindly. Aim for headroom (e.g. CPU <=27 ms, GE <=28 ms) as engineering targets, not predictions; validate the actual completion timeline and missed-vblank rate.

First obtain a fresh unprofiled baseline for current source/build and exact quality settings. Separate queue time from model advance, frame synchronization, cache miss reasons, pose/palette preparation and submission. Historical documentation contains stale builds and enabled visual compromises; these cannot define the requested full-quality baseline.

Implement in independently measurable stages: (1) persistent directly bound static records, (2) dirty-driven patching, (3) offline spatial preparation and guard coverage, (4) fused character preparation, (5) audited skipped-tick deferral, (6) remaining effects/2D/GE bottlenecks. Every stage needs reference-path image/state comparison and unprofiled device A/B. Compare simulation events and attachments as well as images.

Acceptance scenes: Heaven with moving camera, close arena floor, multi-character dialogue, combat effects and shadows, transparent umbrella/rain, animated textures, Shuffle Time, scene/resource transitions and long sessions. Track worst and percentile frame times, deadlines, cache fallback counts, heap high-water and visual diffs. The older ~188 ms Shuffle Time report is a historical independent effect bottleneck; b266 includes a subsequent particle optimization, so remeasure instead of claiming it remains unchanged.

Stable whole-game 30 FPS cannot be promised from this scene or from display-list evidence. Prepared rendering removes a major structural source of CPU overhead; it must be paired with model-update and effect work, and proven on hardware.


## Implementation checkpoint: b285

First incremental change implemented: `sl_state_key()` caches the existing hash until a render/sky setter changes a value. Saved-state restores in `far_lod.h` and `translucent_pass.h` invalidate it; pixel-mask changes are compared directly. This benefits callers of the shared state-key helper, without skipping material, geometry, lighting or guard validation. It does not yet implement persistent scene records or offline asset bundles.

Controls: `p4_static_state_cache` (1; 0 uses the original hash), `p4_static_state_check` (0; 1 recomputes on reuse, counts mismatches and repairs the cached key). The `static state cache` log records reuse/rebuild/diff counts. Both controls are supported by `psp_probe_ab.py`.

Validation: `tools/test_static_state.py` compiles actual renderer helpers and checks all render/sky fields, unchanged setters, invalid indices, pixel masks, cache off/on, missed-write detection and saved state restoration. It passed, as did `tools/test_static_meshes.py`. PSP cross-build b285 passed import validation (19 libraries, no unresolved native function pointers). `git diff --check` passed.

Deployment: PRX, data image and string table copied to PSP and copied back; all SHA-256 values matched. Reset was acknowledged, but subsequent ldstart and modlist received no device response (echo was handled locally). No b285 boot is confirmed and no performance/visual result is claimed. Hardware continuation requires restoring PSPLink responsiveness. The last b284 numbers were profiled and changing with scene activity; they are not a valid A/B baseline.

Next gate: boot b285, enable state check and exercise ordinary draws, saved passes and scene changes; require zero differences. Disable profiling/check mode for off/on/off/on device measurements. Keep or remove this optimization based on measured CPU savings before proceeding to persistent scene records. Whole-game 30 FPS remains unfinished.

Recovery attempt: restarted USB host and the shell bridge (old command queue archived locally). Device module-list response and b285 boot remain unconfirmed. Windows enumerates PSP Type B. No RAM pokes were sent to an unconfirmed build.


### b285 resumed: real PSP Heaven A/B

User restarted PSPLink, b285 boot confirmed at image 0x0900da00 (ELF base 0x08804000). CPU profiling reports no thread registers; clock setup is 333 MHz. User held Heaven, 10th Paradise stationary. One complete check-enabled report had 18,176 cache reuses / 6,656 rebuilds and **0 hash differences**. This validates state-key agreement in the observed scene, not whole-game visual equivalence.

Off/on/off/on, four 128-frame windows per arm, first window discarded:

| Arm | CPU ms | Presented frame ms |
|---|---:|---:|
| off 1 | 50.856 | 66.643 |
| on 1 | 51.099 | 66.815 |
| off 2 | 51.243 | 66.729 |
| on 2 | 51.085 | 66.729 |

No measurable gain: pooled off 51.049 ms, on 51.092 ms. Remains ~15 FPS. Cache and check disabled on device; source/probe defaults changed to off. The diagnostic build b285 still contains its original on default, so resetting that binary requires explicitly disabling it again. No claim of speedup or 30 FPS.

Artifacts: `build/psplink_b285/experiments/state285_{off1,on1,off2,on2}.{json,log}` and `state285_validation.log`. Screenshot: `${P4_HOST}/b285/start.png.bmp`.

Stronger next target: steady reports contain 5,376 per-mesh fallbacks / 256 frames (=21/frame), 168 size-category refusals / 256 frames (=21 retried every 32 frames), zero evictions and zero guard-category misses. The size category combines command count, absent mesh-cache slots and excess slots; it does not prove oversized lists. Split these reasons and inspect offending geometry before changing list limits or clipping. The previous b277 cache-capacity change did not eliminate this steady fallback population.


### b286: actual recording refusal identified on PSP

Added disjoint counters for empty commands, >4096 words, zero referenced mesh-cache slots, and >16 slots; first 32 refusals include geometry, vertex/index/mesh counts and command size. Build and both host tests passed. Copied all three runtime files to/from PSP and verified SHA-256; reset and boot b286 succeeded. User loaded Heaven.

Steady reports: **168 no_slots refusals / 256 frames**, zero empty/oversized/too-many-slot refusals. **5,376 per-mesh fallbacks / 256 =21 per frame**. Recorded command lengths in the initial geometry sample are only 41-107 words. Increasing list length or slot limits is not the fix.

In the same steady windows guard results report **5,120 hits and 256 misses /256 frames**; ordinary guard classification reports 256 whole meshes, 512 inside chunks, no partial or culled chunks. Thus the expensive fallback population largely reuses camera-dependent guard output already, rather than repeatedly performing all triangle clipping. This aggregate correspondence is not a per-geometry identity proof.

Source audit: `sl_note()` is reached only through `draw_mesh_hit()`. `draw_guarded_compact()` replays original mesh vertices plus its own retained-index/cut-vertex buffer directly through `draw_mesh_vertices()`, without registering these allocations with `sl_keep()`. The latter only copies/relocates referenced MeshCache blocks. A guard result is keyed by camera/model, geometry and guard options and cannot be made a permanent camera-independent static list by simply registering a pointer.

Prior live experiment on b285: `p4_guard_small=2` admitted five records initially but retained the 21/frame fallback population; CPU 52.678 ms, no gain versus the prior ~51 ms baseline. It was restored to 0. This experiment does not justify enabling the switch.

Artifacts: `build/psplink_b286/experiments/{record_refusals,heaven_refusals,heaven_steady}.log`; b285 `guardwhole285.{json,log}`. b286 remains running in Heaven, state hash caching off. Stable 30 FPS is still unachieved.

Implementation constraint for the next stage: choose camera-independent prepared spatial geometry for the permanent short path. A transitional guarded-list cache would need owned copies of both source vertices and retained/cut output, address relocations, an exact view/model/options dependency key, safe in-flight lifetime, and correct revalidation on camera changes. Merely enlarging the list or calling sl_note on the source vertex slot is incorrect because retained indices and cut vertices are separate buffers. Measure both stationary and moving camera; a frozen-view cache is not completion of the native-style static path.

### b288-b289: guard-cut static parts as prepared lists (p4_static_guard_lists, default 1)

Correction to the plan: at 30 FPS every tick is drawn (frameskip only fires when behind), so skipped-tick work does
not bound 30 FPS; the drawn tick must fit 33 ms. b284 Heaven (profiled): ~1.65 M cycles/frame of skipped tick at
20 FPS, drawn tick ~44 ms. Largest drawn-tick item: `asl` 3.76 M incl. ~2 M of `mesh` for the ~21 parts the list
recorder refused (no_slots, b286).

b288: the recorder also copies guard results (kept indices + cut vertices) used through `draw_guarded_compact`'s
hit path, plus the mesh cache slot; the list stores the result's view key (model rows, camera key, guard options,
`SL_VIEW` words) and is drawn only while the view is identical, otherwise dropped (retry 2 frames). Any guarded draw
that is not a cached result marks the recording unusable (`sl_guard_unowned`). Instances never share such a list.
b288 Heaven, profiled, camera mostly still: `mesh` calls 50 -> 12-15, guard lists ~13 hits/frame, but views changed
2-5 times a frame in some windows (re-record churn) and the profiled A/B showed no gain (off 57.1/57.4, on
58.1/58.5 ms). b289: a part whose list is dropped twice within 32 frames goes the long way for 300 frames; the
report splits view changes into model / camera / options. Unprofiled A/B pending (needs Heaven loaded again).

b290-b291: the trace showed the still camera's position alternating by 2 ULP each frame (pos.x 0x44145a79 /
0x44145a7b); the guard results kept both, the exact list key dropped every list. b291 compares the view with a
tolerance on rotation (1e-6) and position (min(1e-4 x (|p|+1), 1e-3 x near)); model, camera pointer, near, far and
options stay exact. The cut edges lie on the guard planes far off screen, so on-screen pixels do not change.

b291 real PSP, Heaven, profiler off, user standing still, 4 windows each (`build/psplink_b291/experiments/gl291_*`):

| arm | cpu ms | frame ms |
|---|---|---|
| off | 53.81 53.65 53.93 53.19 | 66.4-67.1 |
| on | 50.66 50.91 50.32 50.92 | 65.9-66.9 |
| off | 53.39 53.34 53.65 53.40 | 66.7 |
| on | 50.09 50.48 49.65 49.52 | 63.1-65.7 |

**-3.2 ms CPU** (both pairs). Steady counters: guard lists ~18 hits/frame, static mesh-list fallbacks 0 (were 21).
Screenshots off vs on: the scenery identical, differences only on the idle-animating party (357 px). One screenshot
taken right after the switch showed a half-drawn frame (HUD and foreground missing); the next one was complete:
likely the capture of a frame in progress, to be watched on the screen. Not yet checked: a walking camera.

### b297: ceiling of our own checks (experiment `p4_sl_trust`, default 0)

Real PSP, Heaven, profiler off, standing (3 kept windows each, `build/psplink_b297/experiments/tr_*`):

| arm | cpu ms | frame ms |
|---|---|---|
| base | 49.7 49.7 50.1 | 66.2-66.5 |
| trust 1 (lists drawn without key checks) | 48.9 48.7 48.7 | 66.2-66.5 |
| trust 3 (also without guard-band test) | 45.8 45.0 45.5 | 60.1-60.9 |
| guard_clip 0 (no CPU clipping) | 44.7 45.0 44.7 | 58.4-58.6 |
| trust 3 + guard_clip 0 | 43.7 43.8 44.3 | 58.4-59.0 |
| base again | 50.4 50.2 50.6 | 66.2-66.5 |

Our list checks cost ~1.2 ms; CPU guard clipping (and the paths it forces) ~5 ms; everything of ours off: -6 ms,
CPU still ~44 ms. Screenshots: differences only on the animating party at this view (no holes visible here; holes
are expected near the camera when walking). The remaining ~44 ms is the game's own per-frame work and the per-atomic
path outside the list (OT walk and callbacks, model/animation update, the atomic callback before sl_try, 2D,
particles): removing our checks cannot reach 33 ms.
