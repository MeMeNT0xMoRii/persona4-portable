# QA08 — Source-backed PSP Go memory, VRAM and diag-lite allocation gates (20 Nov 2026)

**Evidence:** T1 exact immutable perf06 `29b0fcfdec70222a4052099b67fb4a11e8d8aff4` inspected, H0 arithmetic checked independently with Python; **NO linked RESONANCE01 ELF**, heap snapshot, PPSSPP T2 or PSP Go T3, and no inferred FPS gain. Current docs branch is not a new game build. All sizes below are **binary KiB/MiB**. All prices and RAM headrooms remain empirical until full build/device metrics. This file is an *allocation map*, **not a claim all categories add simultaneously**.

## A. Confirmed RAM and VRAM ownership (do not confuse them)

| Owner (source exact lines) | Frozen expression | Bytes / KiB | Lifecycle / physical domain | Important limitation |
|---|---|---:|---|---|
| [`port/main.c` L20–41](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L20-L41) | `P4_IMAGE_SIZE=0x40B000` | 4,239,360 / **4,140 KiB** | Reserved rebased game retail data + BSS in loaded ELF fixed image | Do not add again as free heap; actual segment/residency must be confirmed from linked ELF |
| [`port/rwsky.c` L452–463](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L452-L463) | `list_store[2][64*1024] * 4` | 524,288 / **512 KiB** | Static RAM / CPU + GE display list; two pipeline banks | Bank is NOT free on 1 s watchdog until truly retired |
| [`rwsky.c` L783–786, L830–833](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L830-L833) | `Vertex3D` six 4B fields =24B; `POOL_VERTICES=125*1024` | 3,072,000 / **3,000 KiB** | Static RAM, two halves when pipeline=1 | GE-lit `Vertex3DN` is 28B, but backing storage is still fixed as `Vertex3D` units; no extra static array |
| [`port/main.c` L48–55](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L48-L55) | `LOG_RING=64*1024` | **64 KiB** | Static RAM, regular lossy text logger | Not a durable D1 black box; 100 ms async text flush |
| [`port/main.c` L143–161](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L143-L161) | `ms_site[1024],ms_copy[1024]` ×16B | **32 KiB** | Static RAM even when `p4_memstat=0` | Separately `ms_table` **768 KiB HEAP** only if deep census enabled; do not count it in regular |
| [`port/audio.c` L16–20, L29–55](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/audio.c#L16-L55) | 5 `Stream` × 2 × 2048 ×16B rings | **320 KiB** | Static RAM inside audio stream structures | Stream metadata, SAS, output/input, bank requests additionally exist |
| [`port/rwsky.c` L924–944](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L924-L944) | `feedback_pixels=512×512×4` | **1,024 KiB** | One-time heap allocation | Distinct from VRAM feedback copy; full heap availability affected |
| [`rwsky.c` L938–944](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L938-L944) | `POOL_INDICES×2`, `MAX_GUARDED×16`, `MAX_GUARDED×1` | **384+512+32=928 KiB** | Dynamic one-time index, guard_clip, guard_code buffers | Allocation failure partially disables guard clipping or forces pipeline=0 |
| [`rwsky.c` L5618–5628](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L5618-L5628) | `MAX_GUARDED×(4+4+2)` | **320 KiB** | Lazy heap geometry scratch | Only on first need; user-visible symptoms differ on failure |
| [`rwsky.c` L5661–5676](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L5661-L5676) | `CALL_RING_WORDS=256×1024 words×4` | **1,024 KiB** | Lazy heap GE CALL record ring | Do not discard/reallocate while GE still references saved commands |
| [`rwsky.c` L915–918, L1699–1711](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L915-L918) | `p4_cache_budget=6144×1024` (adaptive 4–6 MiB) | Up to **6,144 KiB** | CAPPED heap mesh cache, not static reservation | 1,024 KiB heap-free reserve is a *cache policy*, NOT certified system-wide safety margin |
| [`rwsky.c` L5636–5643](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L5636-L5643) | `p4_topology_budget=192×1024` | Up to **192 KiB** | Optional heap topology cache | Actual occupancy and page fragmentation require measurements |
| [`main.c` L14–18 and `port/ee.c` L4–34](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/ee.c#L4-L34) | main stack 1024 KiB; each *created* EE thread at least 64 KiB | **1,024 KiB + N×64KiB** | thread stacks/system allocation | MAX_THREADS=16 is a slot limit, **NOT** proof all 16 stacks are allocated; diag worker own stack unknown |
| [`port/video.c` L20–22, L120–156, L182–217](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/video.c#L120-L156) | 2MiB decoder DDR + 3×(512×272×4) frames + 256KiB packet + work/headers | **movie-only**: ~4.2 MiB+ provisional | Typically from separate 4-MiB volatile block; heap fallback if lock/work fails | Heap peak *can suddenly jump* when volatile allocation unavailable. Must measure same movie transitions before/after build |

**VRAM is separate from 64-MiB system RAM:** `rwsky.c` L22–24, L953–954, L1195–1207 reserves 0x200000 = **2,048 KiB** local eDRAM. Frozen `STRIDE=512`, `SCREEN_H=272`, GU_PSM_8888 color and 16-bit depth yield:

| VRAM component | Source formula | Bytes | KiB |
|---|---|---:|---:|
| Two 32-bit 512×272 framebuffers | `2*512*272*4` | 1,114,112 | **1,088** |
| Shared depth buffer | `512*272*2` | 278,528 | **272** |
| Screen feedback copy, incl +1 row | `512*(272+1)*4` | 559,104 | **546** |
| 8KiB-aligned RTT tail start | `align_up(1,392,640+559,104,8192)` | 1,957,888 | **1,912** |
| **Default tail physically remaining** | `2,097,152−1,957,888` | **139,264** | **136 KiB** |

The 136-KiB tail supports some small camera textures but **not a new 256×256×32bpp RTT (256 KiB)** alongside fixed buffers. `p4_rtt_scale=0` already performs full/half fallback. A full-size override may **repurpose the feedback-copy region**, so treating 136 KiB as a guaranteed always-free texture heap is incorrect. No UI effect should reserve extra VRAM before one-frame owner / mode diagnosis. CPU `feedback_pixels` 1 MiB is separate, **do not double count VRAM**.

## B. New dangerous integration trap: 32KiB D1 in static .bss can FAIL LINK even with ample runtime free memory

Frozen `port/rwsky.c` [L88–91](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L88-L91) comments it shrank a **profiling static scratch array from 4096 to 1024 words** because the mapped data image was over by **344 bytes**. `port/main.c` [L39–41](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L39-L41) puts the retail data region at fixed `P4_IMAGE`; `tools/psp_link.py` [L210–215](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L210-L215) asserts that `p4_image` starts exactly at `PSP_IMAGE`. The release linker repeats load base / image checks [`tools/psp_hw_debug.py` L80–89, L130–139](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py#L80-L89).

**Therefore:** Adding a global `static P4_DiagRecord recorder[512]` **may push native .bss into the immovable retail data image**. A normal Windows PC can have enough free RAM and **still** reject the linked ELF. This is source-backed *risk*, not a demonstrated QA08 linking failure. Make D1 a **single bounded heap allocation at controlled postboot initialization**, or explicitly prove sufficient ELF static region slack with map/sections and an unchanged `p4_image` address. Do not add global 32KiB silently. If heap allocation fails: **disable Diag-Lite and log once**, do not partially activate an invalid pointer, expand GE list, alter game saves or overrun. Postboot allocation means **no guarantee of incident capture during early boot** (known D1 limitation).

Proposed incremental D1 budget **must be declared and measured**, not confused with 32KiB total:

| Added owner, hypothetical initial cap | KiB | Evidence |
|---|---:|---|
| Fixed 512×64B event ring | **32** | D1 spec mathematical requirement, not compiled |
| Bounded event indices/counters/buckets | **up to 16** | Initial design budget only |
| Separate frozen PRE128+POST128 snapshot (if copy instead of pin) | **up to 16** | Design option; can instead use a safe pinned ring window and independent durable ack |
| Worker stack, kernel-thread state, readback buffer / memory fragmentation | **UNKNOWN** | Must source/build/device-measure; do not claim 64 KiB all-in |
| **Provisional known data allocation cap, excluding unknown overhead** | **64 KiB** | 32+16+16: includes optional copy, not double counted permanent |

`main.c` L486–503 already logs `sceKernelTotalFreeMemSize` and `sceKernelMaxFreeMemSize` at boot and after retail data load. Extend low-overhead A/B measurements **only after source authorization**, logging actual free/maxfree after renderer init, after CALL ring alloc, after topology/cache warmup, at F009_004 Junes and F006_002 school, in battle/Compendium, immediately **before** movie volatile lock, during fallback if visible, and **after** return from movie. Snapshot max contiguous allocatable block, not just total free; distinguish heap mallinfo plus p4_heap_unclaimed from PSP total free.

**Required signoff B2:** `p4.elf` true `.bss`/`p4_image` owners and addresses; no changed unrelated native image layout; D1 allocation fail-closed; worker bounded max stack + 0..127 partial POST incidents; no per-draw disk IO; no new VRAM; no unretired GE bank; actual Go long-session maxfree differential and regular-vs-lite 128 **accepted** display-present windows with FPS percentiles. Require measured lifetime peak and 1MiB mesh cache policy as independent rather than arbitrary absolute 64MiB free subtraction. On absent actual hardware, report **NOT VERIFIED** and do not promote an optimized/safe Go-ready claim.

## C. Exact check arithmetic (independently H0 calculated)

```python
assert 0x40B000 == 4_239_360
assert 2 * (64 * 1024) * 4 == 524_288
assert 125 * 1024 * 24 == 3_072_000
assert 5 * 2 * 2048 * 16 == 327_680
assert 256 * 1024 * 4 == 1_048_576
assert (192 * 1024 * 2) + (32 * 1024 * 16) + (32 * 1024) == 950_272
vram_total = 0x200000
vram_tail = (2*512*272*4 + 512*272*2 + 512*(272+1)*4 + 8191) & ~8191
assert vram_tail == 1_957_888
assert vram_total - vram_tail == 139_264
assert 512*64 + 16*1024 + 256*64 == 65_536
```

**Rollback:** If static layout/link, heap maxfree/alloc or VRAM footprint changes without source and Go verification, keep recorder experimental/off and ship neither unverified memory optimization nor a claim that the 64-MiB device can absorb it safely. Revert only targeted D1 allocation changes inside experimental branch, preserving perf06 and all private saves.

Related [B0 read-only Windows checker](host_tests/resonance01_b0_preflight_qa08.py), [SEC-01 GE invariants](SEC01_GE_LIST_WRITER_CENSUS_2026-11-20_WIP.md), [D1 partial hang durability](DIAG_D1_PARTIAL_STALL_HOME_CRASH_DURABILITY_2026-11-20_QA06_WIP.md), [RESONANCE master](RESONANCE01_CODEX_MASTERPROMPT_2026-11-21_READY_DRAFT.md).
