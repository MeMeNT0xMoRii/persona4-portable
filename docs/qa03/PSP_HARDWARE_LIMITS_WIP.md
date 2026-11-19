# PSP Go and PSP-3000 — verified bounds and hardware budget (WIP)

**Purpose:** distinguish **unchangeable hardware ceilings**, **current port resource allocations**, and **measured dynamic bottlenecks**. Numbers are not a claim that perf06 currently uses all available resources or that every scene is GPU-bound.

## Verified device specifications

- PSP Go (**PSP-N1000**, generation **05g**) and PSP-3000 share the later-model **64 MB physical main RAM** class. PSP-1000 has 32 MB; it is not the accepted baseline.
- PSP CPU **Allegrex MIPS at up to 333 MHz**; a distinct Media Engine also runs at up to 333 MHz but does **not** become free Allegrex CPU cycles by merely enabling it. Program architecture, audio/video hardware APIs and synchronization determine its use.
- The PSP's Graphics Engine (GE) has **2 MiB dedicated eDRAM** and up to **166 MHz** graphics clock in common hardware specifications. The rendered display is **480×272**. Same pixel geometry on Go and 3000; their physical display sizes differ.
- PSP Go has **16 GB internal `ef0:/` flash** and optional `ms0:/` media; PSP-3000 typically uses `ms0:/`. Storage size does **not** increase CPU throughput, 64 MB physical RAM or dedicated GE eDRAM.
- **Firmware/CFW/OS reservations and loaded module buffers reduce usable application memory.** Do not interpret 64 MB as 64 MB freely usable by perf06. Check actual memory-block allocations and module modes. Port build requests `MEMSIZE=1` per `docs/building.md`.

**Sources:**
- Sony PSP Go product specifications: https://sonyinteractive.com/en/press-releases/2010/psp-playstationportable-go-available-at-an-attractive-new-price-of-16800-yen-starting-october-26-2010/
- Sony PSP-3000 specifications: https://sonyinteractive.com/en/press-releases/new-psp-playstationportable-comes-with-advanced-lcd-screen-and-built-in-microphone/
- PSP model specification comparison: https://www.psdevwiki.com/psp/SKU_Models
- GE specifications: https://www.psdevwiki.com/psp/index.php?section=1&title=Graphics_Engine
- PPSSPP technical GE overview: https://www.ppsspp.org/docs/psp-hardware/gpu/ge-overview/

## perf06 framebuffer VRAM reservation — **calculated from actual source**

`port/rwsky.c` (check pinned ref) `SCREEN_W=480`, `SCREEN_H=272`, `STRIDE=512`, `sceGuDrawBuffer`/display/doublebuffer and `sceGuDepthBuffer` ~L954–59.

| Allocation (stride-aware) | Calculation | Bytes |
|---|---:|---:|
| One 32-bit color buffer | `512 × 272 × 4` | **557,056** |
| Two color buffers | `2 × 557,056` | **1,114,112** |
| One 16-bit depth buffer | `512 × 272 × 2` | **278,528** |
| Nominal three-buffer total | sum | **1,392,640 (~1.328 MiB)** |
| Nominal GE eDRAM total | `2 × 1,048,576` | **2,097,152** |
| **Remaining before all other VRAM consumers** | difference | **704,512 (~0.672 MiB)** |

`port/rwsky.c` nearby `RTT_FULL_BASE` / `HALF_VRAM` calculations carve further offscreen texture/RTT space. Thus **0.672 MiB is not verified free VRAM** and not a limit on all textures: texture data can be kept in main RAM for the GE, at a different performance cost. Actual lifetime/overlap of RTT buffers, frame copies, texture cache and alignment must be traced.

## What code cannot transcend

- **Clock:** no normal optimization raises the physical maximum CPU/GPU frequency. Gains come from fewer instructions, fewer copies/state changes, reducing GE workload, better scheduling, caching, resolution/precision/feature tradeoffs.
- **Local VRAM:** cannot exceed physically present eDRAM. Possible architectural solutions: render-target reuse, 16-bit/depth format choices only if visually acceptable, CLUT textures, DXT where suitable, pre-swizzled textures, stream residency, tile passes; all require profiling/quality acceptance.
- **Main RAM:** can't materialize extra physical RAM via port C changes; use overlays, compression/streaming, smaller buffers, resource lifetime planning, less duplicated data, scratch pools. Compression can **increase** CPU usage and lower FPS, so measure.
- **Texture/fill/overdraw:** theoretical pixel fill rate is **not actual scene performance**. Transparent layers, particles, large alpha quads, feedback and render-target copies can dominate a 480×272 render. GPU bottleneck must be proved via GE/cycle/submit/wait instrumentation.
- **Compatibility:** PS2 GE/GS features are not one-to-one with PSP GE. Some visual effects require multipass/shader-like CPU staging or approximations; for each, compare rendering benefit, cost and authenticity.
- **Media Engine:** possible offloads exist but need firmware API compatibility and concurrency analysis. Treat as future architecture study, not free linear 2× CPU.
- **PSP Go and 3000:** differences in storage, screen and firmware/CFW do not by themselves imply large raw render throughput advantages; measured scene timing, clocks and plugin state are mandatory.

## Bottleneck diagnosis before declaring a hard limit

| Symptom | Likely candidate / experiment | Potential response |
|---|---|---|
| CPU > frame budget, GE mostly idle | animation pose updates, bones, triangle translation, draw-call setup, decomp compatibility bridges | precompute or cache bounded safe transforms, batch work, VFPU/VU replacement, raw GE commands |
| CPU often waits for GE | pixel fill / alpha overdraw / texture traffic / offscreen feedback | measure display-list and render-target passes; cull invisible meshes safely, reduce redundant passes |
| VRAM allocation exceeds eDRAM | RTT + double-color + depth + texture staging overlap | resource lifetime graph, arena layout, 16-bit options A/B, use RAM/streaming for textures |
| CPU/GE neither saturated but scene stutters | storage stalls, page/cache, audio/video transition, sync, game loading | I/O tracing and conditional prefetch, async file handling where game expects safe synchronization |
| Good PPSSPP FPS but poor real PSP Go | emulator host CPU/GPU differ, rendering backend and cache behavior | ALWAYS real-hardware perf counters and A/B at identical 333 MHz test settings |

### A practical hard-limit declaration must include
Same scene/save/camera on Go and 3000, clocks, profiler/overlay/plugin states, frame-ms p50/p95, CPU time, GE waiting, main RAM free/high water, VRAM reservations, number of draw primitives, texture formats/passes, build hash. Only after removing known accidental stalls and proving a persistent component saturation should the project choose a quality/FPS compromise.

**No limit test or perf benchmark was run as part of this documentation.**

## 2026-11-19 19:57 CET — perf06 profiler counter provenance (T1, no measurement)

Directly verified [port/rwsky.c lines 34–110](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L34-L110) on branch dev/v0.27-perf06, blob `9d6c30d74a7cfd336bc871f8a5258481cdffb7ee`: conditional `P4_HW_PROFILE` section enum includes `PROF_FRAME`, `PROF_2D`, `PROF_SYNC`, `PROF_SKIN`, `PROF_QEFFECT`, `PROF_QPRIM` and dozens of finer sections. `p4_prof_index[5]={1,2,3,12,13}` maps `cpuck, stall, memory, imiss, dmiss` via `prof_regs[1+index]`; `prof_read` reads these per section. The in-source b239 comment explicitly warns that pre-b239 `{1,2,4,13,14}` mislabeled coprocessor/cache counters. `prof_live` obtains `sceKernelReferThreadProfiler()`, requires PSPLINK `profmode t` and `reset`, and logs when counters unavailable. This is a **verified port instrumentation code path**, not verified counter accuracy or evidence of a bottleneck. Profile setup has overhead (comment notes historical ~800 section edges/frame); compare instrumented vs uninstrumented build and validate counter availability before using absolute numbers. Cost/benefit: high-value low-code audit before expensive GE optimization; no change authorized. Open: confirm exact current build flag and actual Go counter output, GE wait correlation and p50/p95. Original PS2 counterpart independently inspected: [mdlEffectUpdateScreenSprite FUN_004B1AD0 lines 287–335](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Graphics/Model/mdlEffect_inferred_004b1ad0.c#L287-L335) projects with 640/448 and calls `PrimAppendNode`; **not proof** this path dominates perf06 or All-Out.

## 2026-11-19 20:55 CET — profiling window and task attribution (T1 source, no measurements)

New exact perf06 read: [`port/rwsky.c#L112-L172`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L112-L172) `prof_begin`/`prof_end` record counter deltas only on the outermost invocation **of the same section** (`prof_depth[s]`), then `prof_report(frames)` prints per-frame averages and resets sums. These section totals are **inclusive across distinct nested sections**: adding `atomic + mesh + skin + 2d` double-counts time. [`#L178-L239`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L178-L239) `run_task` wraps `kwlnTaskRunUpdate`, tracks callback address and task name; `render_mono()` subtracts a subset (`ATOMIC, SECTOR, 2D, SYNC, VBLANK`) for reported `self_k`, **not a universal exclusive CPU-time measure**. Frameskip adds a distinct `SKIP_KEY` to task identities, so a task's cost may appear in two rows. [`#L1623-L1660`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L1623-L1660) reports `prof_report(128)` at a 128-sample boundary, not per individual presented frame. Verify exact counter/present sample alignment before labeling this p50/p95; averages alone cannot yield tail latency. `prof_live` [#L93-L115](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L93-L115) also runs a one-million-iteration spin and two microbenchmarks on first successful profiler connection; exclude warm-up from scene timing. **ROI:** interpret existing logs correctly before changing code; then obtain non-profiled stock frame-time distribution and separately profiled subsystem/task attribution. **Unverified:** active build defines, connected PSPLINK counters, actual p50/p95, Go measurements, GE saturation, whether any specific scene is bound by these sections. No source edit, runtime test or asset.

## Newly verified 2026-11-19 — main-RAM command-buffer bound (T1)

Unlike GE **framebuffers**, the **two 64Kiword GPU display-list buffers** in perf06 [`port/rwsky.c#L462`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L462) are **main RAM** `.bss` allocations: 2 × (65,536 words × 4 bytes) = **524,288 bytes**. The helper [`__wrap_sceGuGetMemory`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L1904-L1918) does not bound the pointer. b388 has **2 × 131,072 words = 1,048,576 bytes** allocated on the heap, plus a 4,096-word per-list reserve for selected draws. The additional **512KiB main RAM** would not consume GE eDRAM, but could pressure the PSP Go heap or overlap the fixed retail game image if naively added to static `.bss`. This is a **P0 crash-safety investigation**, not a measured perf gain. Read [full GE-list and RTT camera audit](GE_LIST_OVERFLOW_AND_RTT_CLEAR_TRIAGE_WIP.md); verify memory headroom and high-water before any patch.
