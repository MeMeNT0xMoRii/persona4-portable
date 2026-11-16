# PSP performance: current status and plan (2026-11-15, build 267)

The current state of the PSP performance work. The full history of every build (b97-b267: what was tried, the
measurements, why things are on or off) is in [archive/perf_history.md](archive/perf_history.md); search it by
switch name or build number before retrying an idea.

Goal from the user: a stable 30 FPS on a real PSP at **333 MHz**, with effects adapted rather than removed.
Distance-based simplification (LOD, stand-ins) is fine where the PSP screen cannot show the detail. Numbers are from
a real PSP at 333 MHz. Never compare profiled (`profmode t`, ~10 ms CPU a frame) with unprofiled numbers. The
b170-b176 numbers (2026-11-10) were taken at 423 MHz by mistake.

## Where the frame is

Frameskip draws one tick and skips the next; `cpu_us` is wall time from present to sync and holds the skipped tick
too. 20 FPS (a 50 ms frame) needs `cpu_us` under ~48 ms, 30 FPS under ~33 ms.

| Scene | Frame | CPU (`cpu_us`) | Notes |
|---|---|---|---|
| Heaven, 10th Paradise | 66.7 ms (15 FPS) | ~55.6 ms (b263) | static lists -4.3 ms, stand-ins ~-2 ms; GE wait ~10 ms |
| Margaret dialogue | 67 ms | ~52 ms (b263) | GE ~31 ms a frame: atomics 22.9, 2D 8.4 |
| Battle, held menu | 50 ms (20 FPS) | 41-46 ms (b264-b265) | arena per-mesh lists -0.5..-1.4 ms |
| Shuffle Time help overlay | ~188 ms (5 FPS) | ~188 ms (b265) | `btlShuffleEffStep` ~89%; see [shuffle_time_perf.md](shuffle_time_perf.md) |

Dialogue, b263 profiled, cycles a frame (inclusive, do not add up): frame 18.1 M; `qmodel` 4.4 M, `alayer` 3.2 M,
`kadd` 1.5 M, `kmat` 1.2 M, `actrl` 0.85 M, `kinterp` 0.72 M, `apose` 0.6 M; `geskin` 1.5 M, `mesh` 1.6 M, 2D 1.0 M;
skipped tick `SKIP mainDraw` 1.4 M, `SKIP linkOt` 0.8 M. Animation is the largest reserve:
[animation_psp.md](animation_psp.md).

## What is on

The render paths that carry the numbers above (all with a switch for A/B; most have a `_check` mode that compares
against the old path):

- **Static scenery as prepared lists** (`p4_static_list`, b258, [static_list.h](../port/static_list.h)): one recorded
  CALL per static atomic; arena atomics that cross the guard band split per mesh (`p4_static_mesh_lists`, b264,
  [arena_b264.md](arena_b264.md)).
- **Skinned characters**: GE skinning with strips packed on the PC (`p4_ge_skin`, `p4_skin_strip`, `p4_strip_pack`),
  8-bone palettes, one recorded list per character (`p4_skin_list`, b260), bone LOD by distance (`p4_skin_lod_depth`
  900, far level `p4_skin_far_depth` 3000 with 8 bones, [bone_lod.h](../port/bone_lod.h)).
- **Far objects** ([far_lod.h](../port/far_lod.h)): instance batching of small repeated statics (`p4_inst_batch`),
  untextured baked stand-ins beyond `p4_far_proxy` 300 (b257/b262). Far culling is off (`p4_cull_cover` 0,
  `p4_cull_far` 0): far models stay drawn.
- **Per-mesh and per-batch CALL replays** (`p4_mesh_call`, `p4_skin_call`, b189-b190), the mesh and topology caches,
  cached guard-band results (`p4_guard_result`, b236).
- **Animation**: C/VFPU hierarchy update (`p4_hanim_c`, `p4_hanim_vfpu`), lazy and far-step animation
  (`p4_anim_lazy`, `p4_anim_far`), native key decode (`p4_anim_native`, `p4_anim_keymap`).
- **Shadows**: capsule shadows and the look-at shadow pose (`p4_shadow_capsule`, `p4_shadow_lookat`).
- **Particles**: VFPU path for the particle matrix block (`p4_particle_vfpu`, b266, device check 0 diffs); the
  particle vertex pool reserves clipped quads instead of refusing whole tanks.

## Measured and left off

Each was measured on the PSP and gave no gain (details under its build in the history):
`p4_mesh_persist` (b245), `p4_interp_fast`/`p4_interp_batch`/`p4_interp_pairs` (b234-b235), `p4_pose_runs`
(b237-b238), `p4_guard_split` (b232-b233), `p4_2d_batch` (b261), `p4_skin_rigid` and palette sizes other than 8
(b207, b262), `p4_rwmul_vfpu` (b230), `p4_ge_static_clip` (b267), `p4_skin_simplify` (b211, tears hair online).
Not measured, still a candidate: `p4_anim_cull` (no animation for models not drawn, b173). Also off: `p4_anim_tracks` (b272, exact, no gain). Lossy culling (`p4_cull_far`, `p4_cull_cover`) is off by the user's choice.

## Plan, in order

1. **Code cleanup (2026-11-15, in progress).** Done in b268 (not yet run on the PSP): the experiments above that
   were measured dead removed with their switches. Next: split `port/rwsky.c` (~9.8k lines) into modules without
   changing behaviour, checking on the PSP after
   each step. Reasons: `.bss` keeps running into the fixed `.p4image` section (five times in b256-b263, fixed by
   moving arrays to the heap), and the hot paths miss the instruction cache (~146 misses a `mesh` call in b257).
2. **Lighter animation** ([animation_psp.md](animation_psp.md)): per-node key tracks (`p4_anim_tracks`, b269-b272)
   are exact on the PSP but gain nothing in the dialogue (cpu 56.6 / 56.6 / 56.4 / 56.3 ms off/on/off/on): off.
   Dialogue animation by size: `kmat` 0.96 M, `kadd` 0.94 M, `actrl` 0.76 M, `apose` 0.56 M, `kset` 0.32 M.
   Candidates: animation update rate for background characters (a quality trade: user's call), `actrl` breakdown,
   a one-pass skeleton update.
3. **Skipped tick**: `SKIP mainDraw` and `SKIP linkOt` (0.8-2.5 M cycles) run drawing work in a tick that draws
   nothing; b198 kept the OT callbacks for their side effects. Find which callbacks really need it.
4. **Shuffle Time**: keep effect clocks/spawn/lifetime in skipped ticks, move pure particle projection to drawn
   ticks ([shuffle_time_perf.md](shuffle_time_perf.md)).
5. Optional: split skin batches by weight count (GE), CLUT8 for 32-bit backgrounds, a breakdown of the 2D GE time.

Open items from older plans that were not re-checked (rendering gaps from 2026-11-09, movie or SAS hang after
~10 min of sound): [archive/next_steps_b112.md](archive/next_steps_b112.md), [audio_video_hang.md](audio_video_hang.md).

## How to work

Set `PSPDEV` to your compiler root, `P4_HOST` to a local PSPLink host folder and `P4_SESSION` to a separate local
session folder. These are machine-specific environment variables; paths below are relative to those folders.

- Edit sources on the share. Build with `python tools/local_build.py` with `PSPDEV` set, and add `--census` after
  any change to the FIXES in `tools/psp_census.py`. Game objects come from `build/obj`, not from `psp_hw_debug.py`.
- Bump `#define BUILD` in `port/main.c` for every build that goes to the device.
- Deploy through the PSPLink queue (`${P4_SESSION}/commands.txt`): copy the PRX and data, copy them back and compare
  sha256, `reset`, then `ldstart` in a separate step. `profmode t` survives `reset`.
- A/B: poke switches at `0x08804000 + ELF offset` (`psp-nm build/psplink_bNNN/p4.elf`), `dcache w`, read the value
  back, then take 3 or more `hwperf` windows per arm (`tools/psp_probe_ab.py`; keep its DEFAULTS in step with the
  switch defaults).
- `.bss` must end below `.p4image` (0x809a00): put new large arrays on the heap.
- Grep the decompilation in the local copy (`ext/Persona4-Decompilation`, also `ext/Izanagi` for the PC port's
  overrides by address); over the share it times out.
