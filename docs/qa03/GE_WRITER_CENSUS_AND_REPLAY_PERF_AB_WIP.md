# QA03 — Direct GE-list writer census and already-existing replay caches

**2026-11-19. Source-only audit.** P4P game runtime baseline 29b0fcfdec70222a4052099b67fb4a11e8d8aff4; source inspected on dev/v0.27-perf06. T1 source evidence is not proof of a real perf06 overflow; no emulator, hardware, build or FPS result. Companion audit: [GE_LIST_OVERFLOW_AND_RTT_CLEAR_TRIAGE_WIP.md](GE_LIST_OVERFLOW_AND_RTT_CLEAR_TRIAGE_WIP.md).

## SEC-01: wrapper-only overflow guard would be incomplete

Source: [port/rwsky.c](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c), 11,397 lines. Static full-file text census found **37 lines mentioning gu_list->current** and **16 lines mentioning sceGuGetMemory** (including wrapper declaration). These are textual occurrence counts, not command totals and NOT a full GU/SDK/module-wide audit.

| Source / exact range | Writes to current GE command list | Default-active vs optional |
|---|---|---|
| L1903–1918, __wrap_sceGuGetMemory | Grows list by aligned size plus 2-word BASE/JUMP header, no end check, returns pointer to caller | Active when list cached (p4_list_cached=1), or gate closed; callers assume valid result |
| L2090–2106, custom mesh command | VTYPE, address setup and PRIM via raw pointer arithmetic | Active when mesh mode, p4_draw_batch>1 (default 8) |
| L2121–2127, ge_run_dry | Appends 0xF9 marker | Only p4_exp_ge_time diagnostic (default 0), serialises GPU |
| L2620–2637, bind_native_mips | 3 raw words per mip level, plus SDK sceGuTexFlush | Potential stock operation because p4_mipmaps=1; depends on texture eligibility |
| L5750–5770, call replay | 2-word BASE+CALL commands into separate GE ring | p4_skin_call=1 and p4_mesh_call=1 default, guarded by cache hits |
| L5986–5993, instance replay | Variable count of copied 32-bit GE words by memcpy, then advances pointer without remaining-list test | p4_instance_replay=1 default; source replay state must be valid |
| L6133–6140, persisted mesh replay | Copies e->words 32-bit commands and advances pointer | p4_mesh_persist=0 default: **not stock-active**; relevant if perf optimization enabled |
| L10341–10354, submit_skin_bone | 13 raw words per bone matrix | p4_bone_direct=1 default; GE-skinned batch only |
| L10518–10521, skin dummy | One raw 0xff command | p4_skin_dummy=0 default, inactive in normal mode |
| SDK sceGuDrawArray/sceGuClear/sceGuFinish and other port modules | Some writes occur through libpspgu without explicit gu_list->current in this file | **Not fully inventoried**; must trace wrappers/linked objects before claiming comprehensive protection |

Precise source anchors use [rwsky.c#L1903-L1918](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L1903-L1918), [#L2090-L2106](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L2090-L2106), [#L2620-L2637](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L2620-L2637), [#L5750-L5770](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L5750-L5770), [#L5986-L5993](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L5986-L5993), [#L6133-L6140](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L6133-L6140) and [#L10341-L10354](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L10341-L10354).

Current main list is 2 × 65,536 words = 512 KiB main RAM, NOT GE eDRAM. b388's 2 × 131,072 list candidate needs 512 KiB EXTRA main RAM. Separately, current CALL_RING_WORDS=256*1024 is an on-demand, heap-allocated **1 MiB GE sublist ring** with its own per-frame and record caps; guard main-list bounds AND GE ring lifetime. Mesh cache default is about 6 MiB. PSP_HEAP_THRESHOLD_SIZE_KB(4096) reserves stack headroom; 64MiB PSP Go RAM is not all free. Never blindly extend BSS across the fixed game image.

### Proposed first approved safety test
1. Freeze last known-good perf06 and calculate BSS/heap/PRX/fixed .p4image headroom. Count all emitting callsites including SDK, wrapper, raw-pointer writes, variable memcpy and pipelined banks. A wrapper-only guard is NOT sufficient.
2. Add side-effect-limited command-list high-water + preflight for requested number of words; reserve sufficient FINISH/END/tail words, report bank/frame/draw class/base/current/end/overflow. Current hwpeak listB is the **slowest wall-time frame**, not global max list occupancy.
3. Not all callers support NULL. Refusing a vertex allocation or a lone state command without aborting/repairing the frame can itself crash or corrupt GE state. Use a proven frame fail-safe rather than silent skip. Perform synthetic stress first; do not intentionally run corrupting overflow on physical PSP Go.
4. Only after memory safety, reversible A/B 64Kiword protected vs 128Kiword heap-backed double lists. Validate both cached/uncached, pipeline vs serial, GE stall/cache flush and fog-heavy battles. Report actual high-water, CPU/GE p95, memory and visually intact scene, not averages.
5. Keep alpha restoration, RTT shadow and noncritical new draw passes disabled until this safety gate passes.

## PERF-02: an existing but default-OFF second-level mesh cache may save CPU work

Current [rwsky.c L5652–5664](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L5652-L5664) already enables p4_skin_call=1 and p4_mesh_call=1. [L5875–5894](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L5875-L5894) enables p4_instance_replay=1. Thus "add GE call cache" is duplicative. But [L5824–5834](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L5824-L5834) has **p4_mesh_persist=0** and **p4_mesh_persist_check=0** defaults, so retaining a whole draw_meshes call across frames is still disabled. Historical b240 code comment notes 61% CPU cycles in memory stalls **in a past tested configuration**, NOT an actual School/Shopping/Junes measurement now.

In [persist_try L6088–6138](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L6088-L6138), validity checks include geometry lock, source array identities, camera and model transforms, RTT, pixel/render/sky state, texture_generation, light, material, mesh cache key, current GE call ring age. [persist_record L6150–6205](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L6150-L6205) only captures when the mesh body emitted BASE/CALL pairs, with PERSIST_WORDS=48 and PERSIST_MESHES=8. The optional p4_mesh_persist_check compares the recorded command words and post-geometry state while **running the actual body**, so correctness can be tested before replacing it with a replay. Note the conditional bulk memcpy overflow risk above becomes active if this knob is enabled.

**A/B ladder AFTER SEC-01 bounds:** baseline persist=0 → diagnostic persist=1/check=1 (not comparable FPS) → candidate persist=1/check=0; leave default instance replay/call cache unchanged. Capture checks/diffs/misses, work saved, GE list words, correct shadow/culling/materials, animation, source-accurate visuals, and physical Go frame times across school corridor, Shopping central/north (actual ID confirm), Junes, battles and Velvet. Avoid broad enablement if cache checking overhead/misses exceed CPU saved; do not assume setting the flag to 1 produces gain.

## No-regression conditions
- Do not hide an overflow by culling visible geometry or turning off Apsaras/glow/FieldNoise. Do not globally change alpha/stencil settings.
- Main command list and ring-buffer GPU lifetime are two independent safety dimensions; pipeline stall/address and cache-flush semantics must be preserved.
- Stock pipeline mode p4_pipeline=1 and frameskip require **actual presented-frame timing**; serial GE timer is only diagnostic.
- Scope audited: port/rwsky.c direct writers. Additional GU SDK/other-module sources are **still unknown** and MUST be mapped before any "fully fixed" claim.

**Status: T1 verified emitter inventory + default/cache state, T0 crash and FPS attribution, no runtime fix/build/PPSSPP/Go tests.**
