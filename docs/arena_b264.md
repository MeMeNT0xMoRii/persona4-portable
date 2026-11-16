# Battle arena: per-mesh prepared lists (b264-b265)

Candidate started 2026-11-15. The whole-atomic prepared list remains the first choice. When that list is refused during recording (including clipped draws without complete cache slots), or its sphere crosses the guard band, `p4_static_mesh_lists=1` prepares a separate geometry/header and tight sphere for each source mesh. Meshes are submitted in their original order, with their original index arrays and primitive type. A safe mesh records/replays the existing self-contained static list. Crossing meshes keep the normal guarded draw path.

Source changes, locks, header serial/mesh/index/material changes, and late translucency invalidate the partition. The wrappers and lists share the static-list memory budget; copied GE data is retired through the existing delayed-free path. Partitioning is bounded to 128 meshes. No nested partitioning is performed.

The switch defaults to 1 in final b265 after real PSP A/B. b265 also marks newly allocated geometry wrappers locked for their first body draw, so a recycled wrapper address cannot inherit an old LockMemo UV shift/hash. The existing `p4_static_list_check` remains available to compare recorded command words. `static mesh lists` reports hits, fallback/record draws, and builds per 256 frames.

Validation so far:

- Host checks execute the renderer's actual partitioning, validation, ordered fallback and recursive release functions. They cover conservative bounds, original strip topology/index order, geometry lock/serial/material invalidation, malformed indices, nonfinite positions, memory budget, mixed hit/fallback order and byte accounting.
- Diagnostic and release builds compile/link; 19 PSP import libraries validated, no function pointers without native targets.
- Diagnostic PRX/data/string files deployed to real PSP with matching SHA-256 readbacks. b264 boots.
- Before reset, b263 in the user's stationary battle: frame 57.54 ms, CPU 57.38 ms, GE wait 0.016 ms, with profiler on. Mesh: 44 calls, about 3.03 M cycles. This is context only; compare off/on in b264 in the same battle for the actual gain.

Raw measurements live under `build/psplink_b263/experiments/arena_b263*` and `build/psplink_b264/experiments/arena264_*`. The b264 battle has a large fallen enemy and the skill menu. It is not identical to the pre-reset b263 scene; compare only the b264 off/on arms.

## Real PSP A/B, b264, 333 MHz

Three 128-frame windows per arm, first discarded. Same stationary battle menu. Each probe restores its controls; repeated off/on transitions include cache warmup in the discarded window.

| Mode | off 1 | on 1 | off 2 | on 2 |
|---|---:|---:|---:|---:|
| Section timers on: frame ms | 48.91 | 47.19 | 48.57 | 47.11 |
| Section timers on: CPU ms | 48.75 | 47.03 | 48.41 | 46.95 |
| Section timers disabled: frame ms | 42.23 | 41.00 | 41.95 | 40.42 |
| Section timers disabled: CPU ms | 42.11 | 40.88 | 41.84 | 40.30 |

Mean frame saving with section timers disabled: **1.38 ms (3.3%)**, about 23.76 -> 24.56 FPS. GE wait stays ~0.008-0.009 ms. This is a CPU saving, not a 30 FPS result. Kernel thread profiling remains enabled: `prof_state` was set to -1 through the matching b264 ELF to disable section instrumentation. These are not clean release/unprofiled kernel measurements.

Profiler-on mesh time falls from about 2.4 M to 2.0 M cycles, but splitting changes which work falls within `mesh`: the full frame A/B above is the useful performance evidence. Warm counters reached 512 per-mesh hits per 256 frames; much geometry still takes the guarded fallback. Remaining large crossing meshes are the next target, rather than assuming the arena is entirely on prepared lists.

Recorded-list checks: at least 109 comparisons in the saved check arm, plus a later completed report with 179 comparisons, **0 differences**. On/off complete screenshots show the same arena and HUD with idle animation differences. `arena264_on1.png` caught an incomplete intermediate frame (background only) and is excluded from visual comparison; `arena264_on2.png` and `arena264_toff_on2.png` show complete frames.

## Final b265

The render path matches the measured b264 candidate, with first-use LockMemo invalidation and the switch on by default. Host tests include the wrapper invalidation rule. Both builds link and validate all 19 import libraries; diagnostic PRX/data/strings match card readbacks. Final device smoke/check results are recorded below after booting this exact build.

Final b265 in a different enemy pose (standing instead of fallen), section timers disabled:

| Arm | frame ms | CPU ms | GE wait ms |
|---|---:|---:|---:|
| Per-mesh lists off | 50.078 | 45.976 | 4.044 |
| Per-mesh lists on | 50.047 | 45.468 | 4.516 |

Here the CPU saving is **0.51 ms**, while the frame remains at the 50 ms cadence (~20 FPS); the saved time moves to GE/vblank wait. Do not present the b264 3.3% result as an across-scene FPS gain. Warm final-build counters: 1,024 mesh-list hits per 256 frames (~4 per frame), 8,960 fallback/record draws, no new partition builds in that report. Off/on full screenshots show the same arena, characters and HUD. Raw: `build/psplink_b265/experiments/arena265_*`.

The device is left on b265 in the user's battle, `p4_static_mesh_lists=1`, `p4_static_list_check=0`, section timers disabled (kernel profiling still active). No release package was installed over the user's normal EBOOT; diagnostic PRX/data/strings were updated and verified.

Exact-build final check arm (`arena265_check`): 100 recorded-list comparisons in the saved report, **0 differences**. Controls were restored to mesh lists on / command check off.

## Vsync question

Vblank synchronization is not required for renderer correctness; completing GE work before presenting/reusing its buffer is required. Current pipeline presentation always waits for vblank in `flip_main`; `p4_vsync_late=0` only affects the nonpipeline branch in `func_003f79b0`, so it does not disable synchronization with the default pipeline on. A future no-vsync experiment should retain the pipeline/gate and the independent 30 Hz game-speed limit, and change presentation timing behind a switch. It may trade tearing for less wait, but cannot turn ~45 ms of CPU work into the 33.3 ms budget for 30 FPS. No vsync behavior was changed in this arena patch.
