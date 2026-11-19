# Persona4-PORTABLE — LIVE HANDOFF / CONTINUITY (WIP)

**Created:** 2026-11-19, Europe/Vienna. **Living index:** [QA03 README](README.md). This file is intended to be the **single entrypoint for a replacement Project chat** and a stable handoff target for hourly scheduled research. The user must manually create a new chat; an automation cannot create one or move this conversation. A scheduled research run may not have all prior conversational detail—prioritize this file and linked documentation, verify GitHub.

## Canonical state

- Public repo: `MeMeNT0xMoRii/persona4-portable`.
- **Stable runtime baseline:** `dev/v0.27-perf06`, [`docs/v027_perf06.md`](../v027_perf06.md), game runtime commit `29b0fcfdec70222a4052099b67fb4a11e8d8aff4` (original measured package). Later branch HEADs contain **documentation-only** commits. Confirm runtime diff before claiming a new game build.
- Upstream test: `dreikk91/persona4-psp` b388 source `59b79763b2b175cb5e1ab6c92dda053f97e25c9f`; isolated `experiment/b388-renderer-test01` (upstream renderer with only PSP Go path/memcard and Rush anchor compatibility). **NO unreviewed automatic merge**.
- **ONLY UI image references:** user-private `P4P-Golden-UI-Comparison-QA03-2026-11-19.zip`: 120 Golden JPG (Vita, 960×544) and 120 P4P perf06 PNG (PSP Go, 480×272), exactly ID-matched `G_1…G_120`/`P_1…P_120`. Same *scenario*, but not guaranteed same day/character/Persona, etc. [120-pair audit](120_pairs_widescreen_WIP.md). **Obsolete 71-Golden image collection and all its IDs must NOT be used**. Archived audit [here](../v027_golden_ui_layout_qa03.md) is a historical artifact only.
- Golden provides **16:9 layout/proportions/anchors, roundness, safe area, portrait crop**; original PS2-US provides Vanilla style/content/gameplay. 16:9 All-Out cut-in crop is critical and requires its own plan for 2/3/4 participants. Golden-specific animation/art is not imported.
- **Permission:** user authorized **ongoing public WIP documentation, research, change history**. No automatic approval of runtime edits, merges, releases. Do not publish copyrighted screenshots, ISO, saves, game data or user-identifying paths. Notify on meaningful commits.
- User plans Saturday 2026-11-21 approval of a final efficient Codex/Work masterprompt, after additional PSP Go tests.

## Immediate task order / status
1. [README](README.md), [QA matrix](QA_MATRIX_WIP.md), [Code map](CODE_MAP_WIP.md), [120-pair audit](120_pairs_widescreen_WIP.md), [masterprompt WIP](MASTERPROMPT_WIP.md), [bot concept](AUTOPLAY_BOT_WIP.md), [change ledger](CHANGELOG.md).
2. [Hardware perf06 ↔ b388](../v027_qa03_b388_vs_perf06_hardware_2026-11-19.md), [perf04](../v027_perf04.md), [perf06](../v027_perf06.md), [replay saves](../replay_saves.md), [older QA03](../v027_qa03_handoff_2026-11-19.md) *except obsolete 71 IDs*.
3. Investigate **pin-verified upstream game decomp** `Raikaru/Persona4-Decompilation` commit `363eab8a966d60a9773b45f330f2344329ec4f0a` (see [building](../building.md) and [decomp code atlas](DECOMP_SOURCE_MAP_WIP.md)). Existing C source **confirmed accessible through connected GitHub API on 2026-11-19**, including exact `btlPanelDrawFrame` PS2 marker `FUN_0020B1A0` at source L3744–45, `msgCalendarDraw` marker `FUN_00283490` at L2395–96, and `mdlUpdateHierarchyPose` marker `FUN_00471370` at L844–45, for *static analysis*, but not a compiled PSP image or verified visual fix. Game source is fetched separately, never committed as retail data.
4. Follow the new confirmed [PSP Go/3000 hardware budget](PSP_HARDWARE_LIMITS_WIP.md) and [PPSSPP debugger automation interfaces](PPSSPP_BOT_INTERFACE_WIP.md). Root-cause claims require A/B and exact build/screenshot mapping; no invented FPS.
5. Update and review masterprompt incrementally from **new evidence only**, not blind edits. Each independent milestone: exact code SHA, issue, scene IDs, confidence T0/T1/T2/T3, tested/not tested, next action. Keep design freezes and separate branches.

## Open technical high priorities

- T1 (static candidates) b383 scoped Igor table/backface culling; b386 Fusion card front Im3D culling; independent candidate builds only if later approved.
- 16:9: battle action wheel and black TV contour (#77–83), party HUD/badges, Social Link portraits (#43–45), weak/crit eyes (#116), All-Out (#112/#118/#119), field HUD (#93–105), weather TV (#106), dialogs and shops.
- Perf: school/town map/shopping regions often better under b388; Junes West Entrance slow under both. CPU/GE profiling and isolated b365/b366/b385 candidates; preserve perf06 graphics and Margaret weather fixes.
- Save/gameplay: enemy reinforcement freeze and Compendium Register freeze observed with modified saves—original/minimal controls required. Intermittent Teddie green/missing portrait, title transition blips, fusion/Apsaras orbs, home exit.
- Castle #74 distant WHITE may be original PS2. Do not call it a bug; optional black Golden-inspired style only on user's explicit design approval. Town Map circle+blue marker in perf06 confirmed correct; **do not alter**.

## Constraints, risks, efficiency
- A **source line is not proof of a correct rendered outcome**. Status T0 hypothesis → T1 original+port static mapping → T2 PPSSPP deterministic A/B → T3 real PSP Go A/B.
- Exact decomp source available publicly and pinned. Original game image and converted resources absent from public repository. It is possible to index sources; cannot run full PSP builds or game without legally supplied data and toolchain.
- Portable device bounds: 333 MHz CPU, 64 MiB class main RAM on PSP Go/3000, 2 MiB GE eDRAM; profiling distinguishes CPU, geometry transform, fill, VRAM, textures, I/O, video, and effects. No unlimited RAM/VRAM through code alone.
- Bot design: PPSSPP WebSocket debugger can inject button events, save/load savestates, screenshot, GPU stats and logs. This is **not yet** a running autonomous playthrough bot and PPSSPP success doesn't prove real hardware throughput.
- Never force global `target_sx=target_sy` for 2D; aspect needs per-class handling.
- Track all WIP updates in [CHANGELOG.md](CHANGELOG.md), only meaningful commits; avoid unnecessary token-costly complete rereads.

## Hourly research protocol (19–21 Nov 2026)
- Saved automation **P4P Samstagsvorbereitung** scheduled **hourly from Nov 19 19:00 to Nov 21 23:00 Europe/Vienna**. Autonomous tasks are periodic, NOT continuous, and may depend on account/tool availability. Existing separate upstream watch may also be running.
- At start read this file plus CHANGELOG, then choose exactly one previously insufficiently examined subsystem. Research sources or code; document **verified evidence only**, record exact links/commit and unresolved blockers. If no new evidence, **no empty commit**; try another independent focus.
- At end append a compact dated section to this handoff: `slot | area | newly verified | changed docs / commit | blockers | next highest-ROI`. Avoid replacing original canonical goals.
- Explicitly tell user when a manual new Project chat would be beneficial; **cannot monitor idle chat length perfectly nor create a new conversation automatically**.

## Text to start a new Project chat

> Continue Persona4-PORTABLE QA03 without losing context. Read the latest `MeMeNT0xMoRii/persona4-portable` branch `dev/v0.27-perf06` file `docs/qa03/HANDOFF_LIVE.md`, then `docs/qa03/README.md`, `CHANGELOG.md` and linked WIP docs. The **120 matched Golden/perf06 UI pairs from 2026-11-19** are the only valid UI references; the 71-image collection is superseded. Continue research and GitHub WIP text documentation; do not modify game code, publish screenshots/assets, merge or release without new explicit permission. Respect the pinned decomp SHA and exact runtime baseline. Tell me what changed since the last handoff and the next high-value investigation.

## Rolling hourly entries
- **2026-11-19 pre-19:00 research:** decomp target SHA verified; exact original game FUN names/callers mapped in `DECOMP_SOURCE_MAP_WIP.md`; PSP double color+depth eDRAM budget documented in `PSP_HARDWARE_LIMITS_WIP.md`; PPSSPP official input/screenshot/savestate/replay protocol logged in `PPSSPP_BOT_INTERFACE_WIP.md`. No runtime edits or builds. Next: deeper pinned-source UI controls, full index module inventory, bot handshake version pin and source cache assumptions.
- **2026-11-19 initial:** handoff established; new source/hardware/debugger research files linked above. User request to increase research frequency to hourly; no automatic new chat transfer supported.
