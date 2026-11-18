# Changelog

Builds b354-b388 (branch `perf/offline-mesh`, merged into `main` on 2026-11-18).
Each entry lists what was tried and what it gave. "PSP" means a real PSP at 333 MHz
with the profiler off. "PPSSPP" figures are emulated cycles: they show where the work is,
not how long it takes on a device. The test scene is the Margaret dialogue
(b360-b366 PSP: CPU about 48 ms, about 67 ms a frame, 14.8 FPS) unless another scene is named.

## Kept on

| Build | What was tried | Result |
|---|---|---|
| b365-b366 | GE words (state, texture binds, matrices, light, blend, stencil, fog, draws) written directly instead of through libpspgu (`p4_ge_raw`) | PSP: off costs about 4 ms CPU and 75.7-77.7 ms a frame. **On.** |
| b383 | RenderWare cull mode for lit untextured meshes without prelight (`p4_cull_back 3`) | The limousine's blue floor patch is gone on the PSP. Culling every 3D mesh (1/2) cut holes in curtains and walls. **3 on.** |
| b385 | Skin guard: a bone whose sphere leaves the guard band is tested again with the box around its vertices (`p4_skin_guard_box`) | Velvet Room, Satan after a fusion, PSP: 41.6 ms CPU / 24.0 FPS -> 30.8 ms / 29.7 FPS, 0 fallbacks. **On.** |
| b386 | Back-face culling for every Im3D draw with cull mode 2 (`p4_im3d_cull`) | Fusion cards show their faces on the PSP. **On.** |
| b377, b388 | CLUT8 copies of 32-bit textures (`p4_clut32`) | Cloud plane as CLUT8: GE after the CPU 26.1 -> 21.1 ms, same picture (b371). b388 extends it to every 32-bit texture (min size 0) with a hashed slot table (256 slots, 8 probes). The copy costs about 0.34 s per 256x256 at first bind: to move offline. **On.** |
| b388 | Guard cut skipped for geometry whose triangles are short relative to their depth (`p4_guard_big 1.0`) | PSP b366: 256 uncut, 0 cut, no frame change in the dialogue. Default on since b388; check near the camera in other scenes. |
| b388 | Characters skip the bone-sphere guard test (`p4_skin_guard 0`) | PPSSPP: sguard 241k -> 185k cycles a frame. Default 0 since b388; may leave small holes when the camera is inside a character. |
| b366 audit | Triangle strips for skin batches and strips packed on the PC (`p4_skin_strip`, `p4_strip_pack`) | PSP: strips off 83.4 ms a frame (12.0 FPS), pack off 78.4 ms (12.75 FPS), against about 67.6 ms. **Both stay on.** |
| b366 audit | Model preparation skipped in skipped ticks (`p4_skip_model_lite`), bone skipping in skipped ticks (`p4_frameskip_bones`) | Off costs about 3.7 ms and about 4 ms CPU. All animation shortcuts off together: 61.6 ms CPU / 78.1 ms a frame. **On.** |

## Measured and left off

| Build | What was tried | Result |
|---|---|---|
| b364 | Double-buffered command lists: the GE runs list N whole while the CPU writes N+1 (`p4_list_double`) | PSP: 47.9 ms CPU / 67.3 ms a frame, inside the baseline range. **Off.** |
| b358 | RenderWare frame sync keeps the bone LTMs the pose just wrote (`p4_sync_skip_fresh`) | PPSSPP check: 0 differences, about 430-670 multiplies a frame saved. PSP: -0.6 ms CPU, no frame gain. **Off.** |
| b360-b361 | Bone frames written only when read (`p4_frame_lazy`) | PPSSPP: kmat 217k -> 148k cycles, same picture. PSP: -1.5 to -1.7 ms CPU, no frame gain. **Off.** |
| b359 | MatFX env pass on the GE for GE-skinned characters (`p4_matfx_env_ge`) | Not exercised: the tested scenes have no skinned env-mapped atomics. **Off.** |
| b377-b378 | The port's caches in their own 12 MB arena (`p4_arena_kb`) | PSP, field 20: the arena filled up (largest free block 4 KB, about 7400 refused allocations a window), static lists re-recorded every frame, 106 ms a frame. **Off** until every cache has a budget that fits. |
| b379-b384 | A frame the game does not clear starts from the frame shown before it (`p4_keep_previous`) | Fixed the fusion-glow flicker on the PSP, but the GE cost in Heaven and field 20 (90-109 of 128 frames) was not measured. **Off again** (b384); the flicker is open. |
| b366 audit | Lazy interpolation, far animation, held AddAnimTime, native AddAnimTime alone | About 1 ms CPU or less each, no clear frame gain. Lazy and far stay on as before. |

## Diagnostics added

| Build | Tool | What it showed |
|---|---|---|
| b354-b355 | Timing of static-list recording; draws without a list counted by cause; histogram of guard-cut triangles | The ceiling of an offline (PC-built) static pack, and why lists are refused. |
| b356 | Profiler sections on the system clock without the thread profiler (`p4_prof_clock`) | Where the work is in PPSSPP. |
| b358 | Bone frame census (`p4_frame_census`) | About 640 bone frames written a tick, about 35 read. |
| b362 | Probe bits 128/256: hide only characters or only scenery | PSP GE alone about 26 ms: 8 characters 10.6 ms, 2D 8.4 ms, translucent cloud and floor planes and 84 flowers make most of the rest. |
| b363 | Per-frame CPU/GE timeline (`p4_timeline`) | CPU 47.8 ms and GE alone 26 ms, yet 67 ms a frame: they do not overlap. See [docs/ge_frame.md](docs/ge_frame.md). |
| b377 | Heap map (`p4_heapmap`), 2D texture census | The bus error after battles: a 2 MB request with 18 MB free in pieces, largest 1.4 MB. |
| b387 | Guard cut census per geometry (`p4_guard_census`), camera near/far in the log | Battle 512 cuts 1400-7300 triangles a window from two geometries (long edges about 6700 and 1100); Junes 0. Input for offline tessellation. |
| b388 | `p4_guard_tiny`: skip the cut for tiny geometry at any depth | Off; pick the length from the b387 histogram. |

## Preparation utility

- Build-cache paths stay under 250 characters: the job folder is named by 16 hex characters instead of 64,
  and the build stops early with a clear message when the longest workspace path plus 24 characters for
  compiler include joins would pass 250 (a deep custom cache folder). The freeze step reports its longest
  temporary path through the same check.
- `pack-pbp` takes ICON0/PIC1 from a local, git-ignored `build/icons/` when present. No artwork is
  shipped; packaged builds without icons stay byte-identical to the reference.

## Documentation

- [docs/hardware_b366.md](docs/hardware_b366.md): the b366 PSP audit (34 arms, 13,056 frames).
- [docs/cpu_live_b366.md](docs/cpu_live_b366.md): live CPU capture.
- [docs/crisis_core_animation_cost.md](docs/crisis_core_animation_cost.md): a lighter pose path modelled on Crisis Core.
- [docs/runtime_controls_b366.md](docs/runtime_controls_b366.md): all 230 runtime switches (translated to English).
- [docs/ge_frame.md](docs/ge_frame.md), [docs/visual_issues.md](docs/visual_issues.md): GE frame and fixed visual issues.
