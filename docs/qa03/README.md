# Persona4-PORTABLE QA03 — active planning hub (WIP)

**Updated:** 2026-11-19. **Status:** User-approved publication of **provisional research and documentation only**. This is **not** approval to implement, merge, release, or declare a bug fixed. Runtime baseline stays **perf06** on `dev/v0.27-perf06`; the reference build's source code has not been changed by this documentation work.

- **perf06 is now a mandatory cross-rotation CONTROL:** [**Exact perf06 runtime/source/artifact/field-test atlas**](PERF06_RUNTIME_BASELINE_FORENSICS_WIP.md) — frozen 29b0fcfd source and documented EBOOT+data SHA256, 820/821 retained game objects, built-in GE/skin/mips/frameskip defaults, original 1× PPSSPP 32-frame windows and actual Go side-by-side observations. The actual private PBP bytes were not available for direct analysis. Before researching/modifying any issue, take **the precise perf06 code path + already tested identical scene** as a control, never assume current docs branch is a new EBOOT.

## Canonical inputs and scope
- **ONLY** the 2026-11-19 set of **120 Golden screenshots + 120 perf06 screenshots** is valid for Golden UI review: private `P4P-Golden-UI-Comparison-QA03-2026-11-19.zip`, numbered `G_1_…G_120_` and `P_1_…P_120_`, 120/120 exact pair IDs. The older Nov 18 **71-picture** Golden set and its ID mapping are **obsolete**, must never be mixed with the 120-pair index.
- User confirms that every pair represents the **same in-game scenario or UI action**, but not necessarily the same calendar day, weather, character, rank, persona or animation frame. Compare **geometry and composition**, not the differing content.
- Keep screenshot originals and game assets PRIVATE; publish scene IDs, measurements, source traces, analysis, validation outcomes only. The images are Atlus/Sega game content and the project is public.
- Golden is an intentional **widescreen geometric reference**: position, proportion, roundness, anchor, spacing, safe-area, portrait crop. Original Persona 4 PS2 US remains the **gameplay/art/animation reference**. No import of Golden-exclusive assets, combat systems, Shuffle Time, navigator mechanics, or cut-in artwork.
- For an explicitly non-vanilla aesthetic change (e.g. Castle distance white→black), obtain user approval. The alleged white PS2-original distant view is NOT automatically a renderer bug.
- Real **PSP Go** is the acceptance hardware; PPSSPP is diagnostics/replay, not a definitive hardware FPS proof.


### New high-priority first reads (2026-11-19)

1. [**Rolling priority queue and exact original→PSP source-line owner registry for Codex**](PRIORITY_QUEUE_CODE_OWNERS_FOR_CODEX_WIP.md) — P0 GE-list/memory/gameplay safety → 20/30 FPS → scene-based visual/UI work → all engine subsystems, QA bot, builder, optional mods; exact files/functions/line anchors, 15 priority IDs and A/B gates. Maintain this across each `mach weiter` and hourly run.
2. [**PS2 visual fidelity with 20 FPS floor and 30 FPS objective**](PS2_VISUAL_FIDELITY_20FPS_30FPS_PLAN_WIP.md) — Junes West, school corridor, Shopping District North/South, Riverbed, dungeons, battle/Velvet/title; CPU/GE/present/VRAM pipeline, targeted conservatively faithful optimizations and cited OpenUG2-PSP/SHAR/PPSSPP GE techniques. No new P4P hardware FPS.
3. [**GameBanana PS2 mod compatibility + native Manual Fusion Skill Inheritance mapping**](MODS_GAMEBANANA_COMPATIBILITY_WIP.md) — verified public P4 PS2 CLPS2C source with pinned original fclFuseRollPersona/fclCombineMenuFusion/shdPersonaDrawPanels owners, license risk and optional implementation/testing gates; other mods ranked by format, fidelity and memory.
4. [**PPSSPP playthrough bot V2: official WebSocket API and failure evidence**](PLAYTHROUGH_BOT_REAL_INTERFACE_V2_WIP.md) — verified current upstream game.state.save/load/verify, input injection, GPU/GE inspection, bounded logging and deterministic checkpoints; no running bot or hardware result.

## Documents (canonical now)
0. [**Authoritative rolling priority queue and code owners for Codex**](PRIORITY_QUEUE_CODE_OWNERS_FOR_CODEX_WIP.md) — first-read ID queue; rotated research across all systems, P0 command-list safety. Avoid parallel competing source-of-truth queues.
1. [Complete 120-pair UI review](120_pairs_widescreen_WIP.md) — all 120 IDs, visual findings, confidence and required next checks.
1b. [Code owners for combat HUD, party portraits, Social Link portraits, TV weather and effect masks](GRAPHICS_CODE_OWNERS_WIP.md)
1c. [Skill/pursuit vs All-Out cut-in kinds; historical field-HUD candidate exclusions](EYE_CUTIN_FIELD_HUD_TRACE_WIP.md) — exact battle request kinds 0/2/4. The former 'field owner unknown' conclusion is now superseded.
1d. [**Persistent upper-right field HUD OWNER FOUND**](FIELD_HUD_CALENDAR_SOURCE_TRACE_WIP.md) — pinned `calendarDrawBackdrop` FUN_00122A40 with boot/task chain, date/day-phase/weather sprite groups, and existing `0xFF6→0xBF6` X-scale workaround. T1 source owner verified; actual on-device geometry and fix remain T0/T2/T3 pending.
1e. [**Direct 120-pair image cross-check, render paths and performance triage**](RENDER_PATH_PERF_AUDIT_WIP.md) — visual comparisons #98/#100/#105/#106/#112/#116/#118/#119, pinned screen-sprite / atomic / 2D GE routes; existing CPU/GE profiling and framebuffer-alpha tradeoffs; **no actual benchmark or fix**.
1a. [All-Out / cut-in pinned PS2 call chain](ALL_OUT_CUTIN_TRACE_WIP.md) — **new T1** participant-dependent resource requests, effect-tree flush and crop/UV investigation. Exact crop root cause still unknown.
2. [Issue + evidence matrix](QA_MATRIX_WIP.md) — prioritized bugs, hypotheses, proof tiers and regression cases.
3. [Source and dependency map](CODE_MAP_WIP.md) — verified file/line entry points and scalable decompilation inventory proposal.
4. [Pinned game decomp source atlas](DECOMP_SOURCE_MAP_WIP.md) — exact 2026-11-19 read-only original function names, US FUN addresses and port fix locations.
5. [PSP Go/3000 hardware limits and framebuffer budget](PSP_HARDWARE_LIMITS_WIP.md) — sourced 64 MB, 333 MHz, 2 MiB GE eDRAM, formulas and profiling gates.
6. [PPSSPP verified remote debugger / bot interface](PPSSPP_BOT_INTERFACE_WIP.md) — actual WebSocket protocol, savestate, input, screenshot, GPU stats, replay.
7. [Autonomous playthrough/bot design](AUTOPLAY_BOT_WIP.md) — PPSSPP smoke/regression runner, failure artifact contract.
8. [Codex/Work masterprompt — DRAFT](MASTERPROMPT_WIP.md) — standalone orchestrator prompt; user will approve/finalize by Saturday Nov 21.
9. [Change log / decision ledger](CHANGELOG.md) — append-only human-readable changes and explicit status.
10. [Hardware comparison perf06 ↔ b388](../v027_qa03_b388_vs_perf06_hardware_2026-11-19.md) — b383/b386 findings, isolated upstream candidates. Earlier Golden numeric references in this document are **historical only**, not valid canonical 120 IDs.
11. [perf06](../v027_perf06.md), [perf04](../v027_perf04.md), [issues](../v027_issues.md), [replay saves](../replay_saves.md), [building](../building.md).

1f. [**Broader QA code-first triage + next-build UI tranche**](STABILITY_UI_PERF_WIDE_TRIAGE_WIP.md) — verified Compendium Register active-but-uncached Persona NULL-dereference risk, reinforcement/GE discriminator, title-film framebuffer boundary, dungeon/VFX candidates, repeated battle-dial pixel-envelope difference, independent UI-A/B/C/D cohorts and Junes instrumentation. **No runtime fix or T2/T3.**

1f2. [**Historical renderer-artifact re-audit (FieldNoise source attribution superseded)**: Junes RTT, fog lines, fusion alpha, blue field edges**](RENDER_ARTIFACTS_RTT_FOG_TRIAGE_WIP.md) — independently rechecked pinned Vanilla source, perf06 and b388; confirmed **missing parallel-camera projection** in perf06, documented original PS2 FieldNoise (but actual perf06 FieldNoise is a no-op as corrected below), dedicated alpha reader/writer trace, Dojima #105 blue corner regions, and guarded upstream fidelity candidates. **No run/fix.**

1g. [**P0 GE display-list overflow + RTT camera clear/projection audit**](GE_LIST_OVERFLOW_AND_RTT_CLEAR_TRIAGE_WIP.md) — perf06 64Kiword ×2 unbounded GPU command lists vs b388 128Kiword guarded; wrong-target pre-update camera clear; Junes parallel camera + field-shadow Im3D chain, exact source evidence. **No crash reproduction or patch.**

1h. [**Corrected FieldNoise attribution and 2D alpha-mask capability**](FIELD_NOISE_ALPHA_MASK_FIDELITY_WIP.md) — original TV-world noise/vignette is **explicitly stubbed out in perf06**, so its PS2 tile seams cannot directly explain current black lines; perf06 narrow `p4_effect_mask` vs b388 generic `p4_2d_alpha_masks`; extra GE-list/fill cost. **No fix or device test**.

1i. [**Vanilla material-level 16:9 for All-Out and eye cut-ins**](VANILLA_CUTIN_MATERIAL_WIDESCREEN_REFERENCE_WIP.md) — independent pinned Izanagi PC renderer identifies `0x795050` battle screen-model queue, named `haikei_*` panels, `koma_{a..d}` polygon portraits, `megane_kiran_*` and `SYKN_*` background strips; only a scoped **candidate** for the PSP crop fix, no tested build.

1j. [**Multi-scene UI audit: TV Weather title, Shiroku/Metal Works, Social Links, config and negative controls**](UI_MULTI_SCENE_OWNER_AUDIT_WIP.md) — direct private 120-pair image reinspection and pinned code: black Weather News title candidate original (165,244)→PSP (123.75,148.14), shop `fclShopItemPanelUpdate` independent animated sprite/pivot/ring/logo owner, and config Golden/Vanilla option-count mismatch correction; no actual build/T2/T3.

1k. [**Camp menu source owners: Skill/Equip/Persona, rows/scroll/footer and controls**](CAMP_MENU_UI_OWNER_TRACE_WIP.md) — pinned original `cmpSkillMenuDraw` FUN_00138BF0, `cmpEquipMenuDraw` FUN_00131A00, `cmpPersonaMenuDraw` FUN_00135DC0 and `cmpMainTaskStep` router; menus have separate X/Y source anchors and true original content; avoid global scaling. No build/test.


1l. [**Native PSP optimization study: fidelity-first 20→30FPS**](PS2_FIDELITY_PSP_GO_PERFORMANCE_PLAN_WIP.md) — OpenUG2 PSP PVS/section culling and offline T8 mips, SHAR-PSP offline texture palette savings as external references; verified existing P4P swizzle/mip/mesh/guard/skin toggles; targeted Junes/School/Shopping plan. Not a performance benchmark.
1m. [**GameBanana manual fusion + other PS2 mods compatibility**](MODS_MANUAL_FUSION_PORT_FEASIBILITY_WIP.md) — public CLPS2C/PS2 retail hooks mapped to pinned native C fusion/Persona menu owners; GPL-3.0 licensing vs P4P MIT; flags-off native adaptation and save regression requirements; other mod classes and blockers.
1n. [**Expanded Codex code-trace supplement**](CODEX_CODE_TRACE_PRIORITY_LEDGER_WIP.md) — cross-system source/line and hypothesis test matrix, priority aging and fresh triage links; **authoritative top-level queue remains** [PRIORITY_QUEUE_CODE_OWNERS_FOR_CODEX_WIP.md](PRIORITY_QUEUE_CODE_OWNERS_FOR_CODEX_WIP.md).


1o. [**P0 exact GE direct-writer census + existing mesh-persist optimization trial**](GE_WRITER_CENSUS_AND_REPLAY_PERF_AB_WIP.md) — wrapper is not sole emitter, counts raw mip/GE-CALL/instance replay/bone writes, normal vs debug flag defaults, cached sublist/main-RAM lifetime and a check-before-speed A/B. Static T1 only.
1p. [**Non-renderer rotation: Compendium cache, Weather News animation, audio/video**](SAVE_WEATHER_AUDIO_VIDEO_CROSSCHECK_WIP.md) — additional unchecked original cache sort path, a resolver that seeds cache but returns NULL, weather title three draw phases, 320KiB audio rings and movie-display restore trace. No actual T2/T3.


1q. [**Pinned PSPSDK direct GU emitters, frame termination and perf07 candidate**](PSPSDK_PINNED_GE_EMITTERS_PERF07_GATE_WIP.md) — exact documented PSPDEV snapshot has inline command words, no capacity guard, clear NULL dereference and two final words; any safe list fix must address SDK as well as P4P. `perf07` is only proposed.
1r. [**PPSSPP bot route clock determinism**](PPSSPP_ROUTE_TIMEBASE_DETERMINISM_BOT_WIP.md) — absolute `sceKernelGetSystemTimeLow` vs original `p4.log` timestamps, no route-origin normalization, per-read vs per-frame injection; v2 checkpoint/route version proposal, untested.

1s. [**Cross-system perf07 rotation: PS2 package loader vs PSP read cursor, main-RAM linker checks, point MIPs, long-bot clock**](PERF07_ROTATION_PAK_IO_MIPS_LINKER_BOT_WIP.md) — original `sdkPak2` two-stage archive read is not equivalent to port `func_004c9010`'s persistent cursor, missing adapter range-open needs link verification; 64-bit PSP clock exists, and exact 32-bit wrap is 71.58 min. T1 source + synthetic arithmetic only, no runtime fix.


1t. [**Perf06 RTT shared depth, external GE per-pass profiler, cached-GE late-write audit, shop UI**](PERF06_RTT_DEPTH_GE_PROFILER_SHOP_UI_CROSSPORT_WIP.md) — T1 source-level top-left shared RTT/main depth hazard, existing P4P .ppdmp v6 and OpenUG2 GPU pass parsing/PSPLink host rescue methods; private #54/#70 shop geometry; no runtime correction or benchmark.
1u. [**Three-pair quantitative battle wheel ellipse and vertical offset**](UI_BATTLE_WHEEL_3PAIR_PIXEL_GEOMETRY_WIP.md) — direct private #77/#80/#110 Golden↔perf06 480×272 bright-yellow mask, threshold-robust: Golden ~111×111px circle vs perf06 117×94px ellipse, center 6–7px low. Distinguish fan circle, selected stripe and separate command row owners; T1+I evidence, T2/T3 open.

## Gates, in order
- **G-1 Mandatory safety first:** verify GE display-list capacity guards and log true high-water before adding any extra stencil/RTT/draw passes. Preserve the perf06 binary, home/save and original/minimal/complex save checks. Set scene-level native Go frame-time baseline before optimizing fidelity; do not infer 30FPS from other ports.
- **G0 Evidence:** classify every claim as USER/HARDWARE, VISUAL, STATIC-SOURCE, EMULATOR-A/B or PSP-GO-A/B. A static line match is **not** proof of visible change. Record date, exact commit, scene ID, build, save provenance and artifacts.
- **G1 Baseline freeze:** clean branch from perf06 runtime; screenshot manifest and checksums. Never overwrite known-working perf06 or user saves. No blind `main`/b388 merge.
- **G1b P0 render-list safety first:** verify command-list capacity/high-water, all GE writers, RAM headroom before adding visual draw passes or enabling more alpha work.
- **G2 Isolated upstream visual fixes:** b386 Im3D card-back culling and b383 material-scoped Igor floor culling separately; tests for Shuffle, curtains, walls, Junes, Margaret, light effects.
- **G3 UI classes:** prove coordinate path and fix circle / portrait / HUD independently; compare Golden target positions in **480×272**, and preserve Vanilla story/art. Separate All-Out layer composition (2/3/4 participants). No global `target_sx = target_sy` hack.
- **G4 Performance:** measure CPU/GE waits and p50/p95 under matched real PSP Go scenarios; test b365 raw GE, b366 offline strip packs, b385 skin guard box **one at a time** only if relevant. `p4strips.bin` is not `p4str.bin`. Avoid speculative broad merges.
- **G5 Stability:** reinforcement freeze with original-save control, Compendium Register with minimal and clean saves, return-to-title flashes, saves/HOME, Teddie HUD, lighting and distant geometry.
- **G6 Integration:** only combine separately proven fixes, full regression set, clean-room packaged build, explicit **user review / go-no-go**. Never publish disc-derived data or private screenshots.

## Efficient collaboration
The masterprompt should refer to **this single hub**, read linked docs once, build a short execution matrix, group tool calls and focused tests, and report at phase boundaries. If information arrives before Saturday, update evidence/docs first, then the masterprompt—**no repetitive full-repository rereads and no second baseline**. The human user runs hardware checks and supplies results. GitHub is the shared change-controlled knowledge base; this chat cannot execute work continuously while the user is absent.

### 2026-11-20 cross-port and handoff retention first-read
- [**QA05 cross-port technical comparison: OoT PSP, GTA III re3-PSP, GTA LCS/VCS PSP→PC recomp and New 3DS port**](CROSSPORT_OOT_GTA_PSP_GU_BOT_2026-11-20_WIP.md) — source-verified GE list rollover test (4096 **bytes** reserve in OoT vs b388 4096 **words**), independent shadow RTT/no-depth-write hypothesis for Junes, texture-slot/stride/cache validity, offline asset conversion, LCS unattended runner with cloned save hashes + watchdog; includes critical-topic handoff audit. No P4P implementation or T2/T3. Cross-port methods are **patterns, not validated P4P speedups**.
- [**2026-11-20 P0 GE writer census**](SEC01_GE_LIST_WRITER_CENSUS_2026-11-20_WIP.md) — pinned perf06 raw list writers and conditional cache-stall/visibility hazard, with safe instrumentation design.

### 2026-11-20 — actual 120-pair reupload and extended UI-A replication

- Private uploaded reference ZIP `P4P-Golden-UI-Comparison-QA03-2026-11-19(2).zip` validated CRC/PIL, **120/120** matched images, no duplicates; SHA-256 `12e73b56f2d1582102cce18669fb87429edd1df7e5f5e417754820a7eeabd662`. This is provenance only; **no images or ZIP uploaded to public GitHub**. In a new chat requiring pixel data, access the original attached file if mounted or request reattachment. It is distinct from the regular perf06 game artifacts.
- [**Seven-state battle-wheel replication: #77/#78/#80/#81/#82/#110/#113**](UI_BATTLE_WHEEL_3PAIR_PIXEL_GEOMETRY_WIP.md) — **all seven private pairs actually reprocessed**, Golden yellow box x9–119/y148–258 (one y257) vs perf06 x6–122/y163–256; original source 78-unit radius and current x=.75/y=.607142 2D scaling predict perf06 ellipse and anchor. I+T1 **not** T2/T3 and no fix. Separate command labels/strip, negative Rush #83 and no wheel #76; do not global-scale every UI asset.

### 2026-11-20 — re-read 48 core/indexed documents; source-first STAB-01 continuation

- [**STAB-01 deep source check: Register metadata-ID validation order, cache eligibility 0x8/0x20, three unsafe cache-copy paths**](STAB01_REGISTER_CACHE_ID_METADATA_GATES_2026-11-20_WIP.md) — pinned PS2 original `yListSlotRefresh FUN_002E4090` reads metadata with **16-bit ID before checking 0..255**; `datPersonaCachedRecord FUN_0010FCB0` can return NULL when cache bit0 inactive; `datPersonaInitAllRecords FUN_0010FFA0` seeds only ID 1..255 with metadata flags 0x28 clear; `yListSlotSortWeighted/ByKey` have independent unchecked `memcpy(0x30)`; `datPersonaResolveRecord` is **not** a safe universal fallback. **T1 hazard, real user's modified-save freeze still T0 cause.** No private save/PPSSPP/Go run or patch.
- Documentation cross-check: 48 central and README-/handoff-linked QA, perf history, hardware, code-owner, mod, bot, render and UI files reviewed on branch `dev/v0.27-perf06`; this checks **indexed findings, not every possible unindexed file or every historical chat sentence**. Old `docs/v027_golden_ui_layout_qa03.md` 71-picture work is superseded; current private 120-pair SHA remains authoritative. **SEC-01 memory/list safety must gate changes before any visual P0 wheel work**, even where `QA_MATRIX` labels both P0. No build authorization.
