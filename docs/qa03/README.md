# Persona4-PORTABLE QA03 — active planning hub (WIP)

**Updated:** 2026-11-19. **Status:** User-approved publication of **provisional research and documentation only**. This is **not** approval to implement, merge, release, or declare a bug fixed. Runtime baseline stays **perf06** on `dev/v0.27-perf06`; the reference build's source code has not been changed by this documentation work.

## Canonical inputs and scope
- **ONLY** the 2026-11-19 set of **120 Golden screenshots + 120 perf06 screenshots** is valid for Golden UI review: private `P4P-Golden-UI-Comparison-QA03-2026-11-19.zip`, numbered `G_1_…G_120_` and `P_1_…P_120_`, 120/120 exact pair IDs. The older Nov 18 **71-picture** Golden set and its ID mapping are **obsolete**, must never be mixed with the 120-pair index.
- User confirms that every pair represents the **same in-game scenario or UI action**, but not necessarily the same calendar day, weather, character, rank, persona or animation frame. Compare **geometry and composition**, not the differing content.
- Keep screenshot originals and game assets PRIVATE; publish scene IDs, measurements, source traces, analysis, validation outcomes only. The images are Atlus/Sega game content and the project is public.
- Golden is an intentional **widescreen geometric reference**: position, proportion, roundness, anchor, spacing, safe-area, portrait crop. Original Persona 4 PS2 US remains the **gameplay/art/animation reference**. No import of Golden-exclusive assets, combat systems, Shuffle Time, navigator mechanics, or cut-in artwork.
- For an explicitly non-vanilla aesthetic change (e.g. Castle distance white→black), obtain user approval. The alleged white PS2-original distant view is NOT automatically a renderer bug.
- Real **PSP Go** is the acceptance hardware; PPSSPP is diagnostics/replay, not a definitive hardware FPS proof.

## Documents (canonical now)
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

1g. [**Full renderer-artifact re-audit: Junes RTT, fog lines, fusion alpha, blue field edges**](RENDER_ARTIFACTS_RTT_FOG_TRIAGE_WIP.md) — independently rechecked pinned Vanilla source, perf06 and b388; confirmed **missing parallel-camera projection** in perf06, full original FieldNoise tiled/banded feedback composition, dedicated alpha reader/writer trace, Dojima #105 blue corner regions, and guarded upstream fidelity candidates. **No run/fix.**

1g. [**P0 GE display-list overflow + RTT camera clear/projection audit**](GE_LIST_OVERFLOW_AND_RTT_CLEAR_TRIAGE_WIP.md) — perf06 64Kiword ×2 unbounded GPU command lists vs b388 128Kiword guarded; wrong-target pre-update camera clear; Junes parallel camera + field-shadow Im3D chain, exact source evidence. **No crash reproduction or patch.**

1h. [**Corrected FieldNoise attribution and 2D alpha-mask capability**](FIELD_NOISE_ALPHA_MASK_FIDELITY_WIP.md) — original TV-world noise/vignette is **explicitly stubbed out in perf06**, so its PS2 tile seams cannot directly explain current black lines; perf06 narrow `p4_effect_mask` vs b388 generic `p4_2d_alpha_masks`; extra GE-list/fill cost. **No fix or device test**.

## Gates, in order
- **G0 Evidence:** classify every claim as USER/HARDWARE, VISUAL, STATIC-SOURCE, EMULATOR-A/B or PSP-GO-A/B. A static line match is **not** proof of visible change. Record date, exact commit, scene ID, build, save provenance and artifacts.
- **G1 Baseline freeze:** clean branch from perf06 runtime; screenshot manifest and checksums. Never overwrite known-working perf06 or user saves. No blind `main`/b388 merge.
- **G2 Isolated upstream visual fixes:** b386 Im3D card-back culling and b383 material-scoped Igor floor culling separately; tests for Shuffle, curtains, walls, Junes, Margaret, light effects.
- **G3 UI classes:** prove coordinate path and fix circle / portrait / HUD independently; compare Golden target positions in **480×272**, and preserve Vanilla story/art. Separate All-Out layer composition (2/3/4 participants). No global `target_sx = target_sy` hack.
- **G4 Performance:** measure CPU/GE waits and p50/p95 under matched real PSP Go scenarios; test b365 raw GE, b366 offline strip packs, b385 skin guard box **one at a time** only if relevant. `p4strips.bin` is not `p4str.bin`. Avoid speculative broad merges.
- **G5 Stability:** reinforcement freeze with original-save control, Compendium Register with minimal and clean saves, return-to-title flashes, saves/HOME, Teddie HUD, lighting and distant geometry.
- **G6 Integration:** only combine separately proven fixes, full regression set, clean-room packaged build, explicit **user review / go-no-go**. Never publish disc-derived data or private screenshots.

## Efficient collaboration
The masterprompt should refer to **this single hub**, read linked docs once, build a short execution matrix, group tool calls and focused tests, and report at phase boundaries. If information arrives before Saturday, update evidence/docs first, then the masterprompt—**no repetitive full-repository rereads and no second baseline**. The human user runs hardware checks and supplies results. GitHub is the shared change-controlled knowledge base; this chat cannot execute work continuously while the user is absent.
