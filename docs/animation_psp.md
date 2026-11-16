# Animation on the PSP: where the time goes, and a lighter system

Started 2026-11-14 (b263). The user's direction: make the animation system light and fitted to the PSP, as native
PSP games do (Crisis Core moves many enemies without slowing down), keeping what the game does with it.

## What runs now, per model and tick

`mdlQueueModelForDraw` (FUN_00479100) -> `mdlAdvanceModel` (FUN_00478A30) for every model with flag 0x1000:

1. The clump's root frame: `RwMatrixMultiply(identityMat, model)` then `RwFrameTransform` (func_003e9cb0), which
   puts the root on RenderWare's dirty list. The camera begin syncs every dirty root's whole frame tree.
2. `mdlAdvanceLayer` for each of the 2 layers (`alayer`): RtAnim on the layer's interpolators -
   `RtAnimInterpolatorAddAnimTime` (`kadd`: the key walk; inside it the per-node interpolate callback `kinterp`),
   `SetCurrentTime` (`kset`), `Blend` (`kblend`), `SetCurrentAnim` (`kanim`).
3. `mdlAdvanceAnimControl` (`actrl`), attachment and effect transforms, `mdlTickMotionState` (`amotion`),
   child models (weapons) through `mdlCopyIndexedMatrix` and a recursive `mdlAdvanceModel`.
4. Later, when a skin is drawn (`anim_drawn`, lazy since b202): `RpHAnimHierarchyUpdateMatrices` (func_00397c40,
   `kmat`, ported to C as `p4_hanim_update`): every node's local matrix from its key frame, the matrix stack, and
   (hierarchy flag 0x1000) the local matrix written into each bone's RwFrame.

Already done in the port (rwsky.c): lazy animation (undrawn ticks move only the clock; walk and interpolation
at the first reader), AddAnimTime's plain steps in C with a key -> node map (`p4_anim_native`, `p4_anim_keymap`),
`p4_hanim_c` (+ VFPU multiplies), no bone rebuild in skipped frames (`p4_frameskip_bones`), far characters every
2nd tick (`p4_anim_far`). Measured without gain (memory-bound): VFPU interpolation, batched and paired
interpolation (`p4_interp_*`), pose runs.

## Formats (Izanagi overrides, ext/Izanagi at bfb12b6)

- Interpolator (`overrides/003d5840.c`): words [0] animation, [1] time, [2] next key in the stream, callbacks,
  +0x24 interpolated-frame size, +0x28 key size, +0x2c node count, +0x44 interpolate callback, +0x4c the per-node
  interpolated frames (each starts with its previous and next key pointers).
- Animation: +4 key count, +0x10 key array, +0x14 custom data. P4's skeletal keys are RtAnim type 2, compressed
  (`overrides/00416bc0.c`, `00415bc0.c`): 24 bytes in memory - previous-key pointer, time (float), rotation
  quaternion as four 16-bit floats, translation as three 16-bit floats (offset/scale in the custom data). Keys
  of all nodes are interleaved by time in one stream; AddAnimTime walks it forward and hands each due key to its
  node (the map built in b204).
- Hierarchy (`overrides/00397480.c`, `00397c40.c`): +0 flags (0x1000: write local matrices into the frames),
  +4 node count, +8 matrix array, +0x10 16-byte node info (id, index, flags PUSH 2 / POP 1, frame).
- The game's layer blender uses keyframe subtract/combine (`003963c0`, `00396520`) and the interpolators
  `003955a0` / `00395bd0` (layer-specific key layouts).

## Measured cost (profiler on, cycles a frame, both ticks)

| | Heaven entrance, b258 (7 skins, ~9 hierarchies) | Battle, b263 (3 skins) |
|---|---|---|
| frame | 19.0 M | 16.2 M |
| `qmodel` (queue + advance, all models) | **6.3 M** | 1.1 M |
| of it `alayer` | 2.3 M (`kadd` 1.0 incl. `kinterp` 0.45 for ~460 nodes, `kset` 0.2) | 0.9 M |
| `actrl` / `apose` | 0.5 / 0.55 M | 0.27 / 0 |
| `qmodel` not under a section | **~2.5 M** | ~0.2 M |
| `kmat` (bone matrices, in the skin draw) | 0.77 M | 0.24 M |
| `ftrans` (RwFrameTransform) | - | 0.05 M |

Heaven: about 7 M of 19 M cycles (37%) are animation and model update; ~11 k cycles a node. ~60% of qmodel's
cycles are memory stall (9.6 k data + 8.9 k instruction misses a frame). The battle is lighter here: its CPU
goes to the arena meshes on the slow path (`mesh` 3.8 M for 39 calls) and `battle_draw_3d`.

Open: the ~2.5 M of qmodel outside every section (root transform, attachments, effects, child models,
the queue itself) needs census hooks inside mdlAdvanceModel. The `fsync` wrap of `p4r_003ed7e0`
(_rwFrameSyncDirty) counted 0 calls: its callers reach it by another name; find them before timing it.

Margaret dialogue, b263 profiled (10 skins, held text box): frame 18.1 M cycles; `qmodel` **4.4 M** (24 calls), of it
`alayer` 3.2 M (`kadd` 1.5 M incl. `kinterp` 0.72 M for ~740 nodes, `kset` 0.38 M), `actrl` 0.85 M, `apose` 0.6 M
(2 calls); `kmat` 1.2 M (18 calls); `geskin` 1.5 M; `mesh` 1.6 M (22 calls); 2D 1.0 M. Tasks: drawn `mainDraw` 9.8 M
(self 2.5 M), `linkOt` 3.8 M; skipped `mainDraw` 1.4 M, `linkOt` 0.8 M. Unprofiled cpu ~51-53 ms (b262).

## The lighter system (plan)

Keep what the game asks for (which animations, layers, weights, times, attach points); replace how poses are made.

1. **Tracks per node.** When an animation is first used, convert its key stream into per-node arrays (time,
   quaternion, translation, packed), contiguous per animation. Each node keeps a cursor: no stream walk, no
   per-key pointer chasing. Kept in a cache keyed by the animation pointer; the RtAnim interpolator state the
   game reads (time, current anim) is still maintained.
2. **One pass for all layers.** For each node: sample the body layer, the face/look layers, blend, build the
   local matrix and multiply down the hierarchy, on the VFPU, written straight into the hierarchy matrices.
3. **Frames only where read.** Bone RwFrames written only for nodes something reads (attachments, indexed attach
   matrices, the shadow and look-at code): a "live" set per hierarchy, filled on first read; skinning reads the
   hierarchy matrices. The bone census found 28% of nodes dead; the others need not be written either unless read.
4. **LOD.** Far characters: update every 2nd-4th drawn tick, and only the far skin level's kept bones and their
   ancestors.
5. **Check mode and switch.** Every N frames the retail path runs too and the matrices are compared (tolerance
   for the 16-bit keys); a switch for A/B; the picture compared at native resolution.

Expected: Heaven's animation from ~7 M to ~2-3 M cycles, enough with the skipped tick's share for the 20 FPS step.
Order: (0) census hooks for the unaccounted 2.5 M, (1) tracks + one-pass body layer behind a switch with the check
mode, (2) other layers, (3) live frames, (4) LOD.

## b272 on the PSP: tracks are exact and do not pay off; off by default (2026-11-15)

- b271 crashed loading a dialogue (bus error in chkmemAllocate: the game's heap ran out with 1.7 MB of tracks).
  b272: budget 1 MB, 1.5 MB heap reserve, idle tracks freed after 3 s, a failed allocation frees all tracks and
  retries. b272 ran battle and dialogue without a crash.
- Exactness: battle 2869 checks, dialogue 8388 checks, 0 differences.
- **Margaret dialogue, unprofiled, `p4_anim_tracks` off / on / off / on: cpu 56.60 / 56.62 / 56.38 / 56.25 ms.** No
  gain beyond noise (profiled it was ~0.2 M cycles). The frame read 67.4 / 71.5 / 68.2 / 74.8 ms with GE wait rising
  in the "on" arms; not explained by tracks (CPU is the same), not chased.
- The SubAnimTime/restart path of b271 never ran: the dispatch layers' interpolators are not compressed-key ones
  (69 SubAnimTime calls a 256-frame window, all retail; no back-step refusals counted).
- Conclusion, as with b234-b239: per-node interpolation work is not where the animation's time goes. Default 0;
  code and host test kept for a later one-pass skeleton update, which would build on them. A smaller key (16 bytes)
  does not change that.

## b270-b271 on the PSP (2026-11-15, Margaret dialogue, profiled)

- **Exactness:** b270 with `p4_anim_lazy_check` 1 and tracks on: 4012 + 10231 checks, **0 differences** (every word of
  the interpolator and its frames against retail on a copy). 64 animations held, 1.4 MB, 0 refused, 0 fallbacks: the
  64-entry table was full, so b271 has 128.
- **Cost, b270, cycles a frame (inclusive, sections nest):** frame 18.4 M; `qmodel` 3.49 M; `alayer` 2.37 M, of it
  `kadd` 0.94 M, `apose` 0.56 M (2 look-at poses), `kset` 0.32 M, `kanim` 0.03 M, `arebase` 0.02 M and part of `kmat`
  (mdlAdvanceLayer calls the hierarchy update itself when it updates the pose); `actrl` 0.76 M; `kmat` 0.96 M in all
  (18 calls); `aframe` 0.10 M; `ascale` + `ahandle` + `achild` 0.04 M. qmodel minus its sections is ~0.17 M: in the
  dialogue nothing big is untimed any more. The other agent's reading of "~1.36 M uncovered in alayer" forgot that
  `apose` and part of `kmat` sit inside `alayer`.
- Tracks against b263: `kadd` 1.35 -> ~0.94-1.04 M; `kinterp` 0.65 M -> 0.03 M is mostly the profiler's per-node
  trampoline that no longer runs. Data misses in `qmodel` stay ~8-9 k a frame: the gain is small, as feared.
- **b271:** SubAnimTime from tracks (`anim_track_sample` back: pairs step back, next-key pointer 0) and the AddAnimTime
  that follows it (retail restarts and walks the whole stream from the first key; tracks end in the same pairs). That
  is mdlAdvanceLayer's dispatch layers: SetCurrentTime(t - 1) then SetCurrentTime(t) every tick, `kset` 0.32 M for 61
  calls. Host test covers both against retail's code. Built and deployed; PSPLink hung on `reset` (usbhost
  "extralen 65536, ret -7"): not measured yet.
- Next, by measured size: `kmat` 0.96 M and `apose` 0.56 M (bone matrices: the one-pass body update), `actrl` 0.76 M
  (material UV animation; no breakdown yet), `kadd` 0.94 M.

## b269 (2026-11-15): per-node tracks and the missing census, built, not yet on the PSP

- **`p4_anim_tracks` 1** ([anim_tracks.h](../port/anim_tracks.h)): an animation with compressed keys (callback
  p4r_00415bc0, 24-byte keys) is converted on first use into per-node tracks, decoded, 32 bytes a key, so a node's two
  keys lie together. A plain step (the native AddAnimTime path and lazy materialisation) has no stream walk: each
  node moves along its own track from the key pointer its frame holds; the frame's key pointers, quaternion,
  translation and the interpolator's next-key pointer (+8) are written as retail leaves them. The linear case is
  retail's arithmetic on the decoded floats; slerp nodes call retail. Refused (retail path): a stream not ordered by
  previous-key time, odd layouts, frames holding foreign keys. LRU cache, `p4_anim_tracks_budget` 2 MB; the
  `anim tracks` log line counts steps, nodes, slerps, fallbacks, builds.
- Host test [tools/test_anim_tracks.c](../tools/test_anim_tracks.c) (32-bit MSVC): synthetic animations, tracks
  against retail's walk + interpolation, every word identical (linear and slerp nodes, negative dots, multi-key
  steps); a stream out of order is refused; rewritten keys are caught by the check-mode checksum.
- **Census** (psp_census.py FIXES, b269): `ascale`, `ahandle`, `achild` inside mdlAdvanceModel, for the ~2.5 M
  cycles of `qmodel` that no section covered in Heaven.
- On the PSP, in this order: `p4_anim_lazy_check` 1 with tracks on (expect `anim check` 0 diffs; a difference means
  the PSP's retail interpolate rounds differently: then compare with a tolerance), then tracks off/on/off/on in the
  dialogue and Heaven (`kadd`, `alayer`, cpu_us unprofiled), then read the three new sections.

## Handoff (2026-11-14, end of session)

Superseded in part: item 1 (arena) was done in b264-b265, items 2-3 started in b269 above.

State: branch `psp-perf-b258`, last build b263 (profiling build; PSP running it with `profmode t`). Commits on the
branch: e1f3021, 52407ad, 2262660, 416c4dd, dfdf913, 7395923 + this one. `port/audio.c` and `port/memcard.c` hold
another agent's uncommitted fixes (save loading not seen the first time, audio): not reviewed here, not committed.

Next, in order:
1. **Arena meshes on the fast path (battle).** b263 battle: `mesh` 3.8 M cycles for 39 calls on the old path,
   while static-list hits are cheap. Read `static list` misses in a battle log first: `guard` (near-camera, CPU
   clipping: the arena floor), `new`/`refused size` (recorded while clipped: no slot drawn whole), `changed`
   (UV-scrolled or locked arrays: `locked`/`material`). For guard-band misses: per-mesh lists (an atomic's meshes
   recorded separately; per frame each mesh inside the band is a CALL, only crossing meshes take the body), since
   b232 found only ~2 triangles a frame really need cutting. port/static_list.h `sl_try`/`sl_keep`; per-part
   spheres (`sl_parts`) were too coarse in Heaven.
2. Census hooks inside `mdlAdvanceModel` for Heaven's ~2.5 M cycles of `qmodel` outside every section
   (tools/psp_census.py FIXES, build with `local_build.py --census`).
3. The lighter animation above: per-node tracks + one-pass body layer behind a switch with a check mode.
4. Skipped tick: `SKIP mainDraw` 1.2-1.4 M and `SKIP linkOt` 0.8-2.5 M cycles; b198's audit kept OT callbacks
   running in skipped ticks for their side effects - find which callbacks really need it.

The 20 FPS step needs cpu_us (drawn + skipped tick, wall) under ~48 ms: Heaven 55.6, dialogue ~52, battle ~46-52.
