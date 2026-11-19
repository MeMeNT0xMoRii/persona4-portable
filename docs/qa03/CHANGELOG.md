# QA03 planning log and decision ledger

**Purpose:** Publicly track *planning/documentation* changes for the 19–21 November 2026 period. Do not imply a shipped runtime or new hardware acceptance. Appended change records and exact Git commits provide provenance.

| Date | Change | Basis / status |
|---|---|---|
| 2026-11-18 | Earlier 71 Golden images/audit captured | **SUPERSEDED** 2026-11-19. Do not use older Golden IDs as current comparison. |
| 2026-11-19 | Hardware comparison perf06 vs b388: b383 Igor-floor CULL, b386 Fusion-card faces; performance gains measured in upstream and observed by user in selected scenes | Candidate cherry-picks. No perf06 code change. |
| 2026-11-19 | Town Map correction: perf06 circular arc **exists**; blue locator matches Golden | **Closed, no UI fix.** b388 smoother FPS may still be investigated. |
| 2026-11-19 | New authoritative screenshot inventory: 120 Golden JPG + 120 perf06 PNG, 1:1 matched IDs, different in-game dates/characters allowed | User source; reference pairing accepted. Raw images **not** posted publicly. |
| 2026-11-19 | New criterion: Golden as **16:9 geometry** reference, including round combat action dial, black TV border, right-side party icons, Social Link faces, date/weather HUD, crit eye cuts and All-Out no-crop | WIP re-audit text, root-cause hypotheses only. |
| 2026-11-19 | #74 distant Castle white field: user says it likely matches PS2 Vanilla; Golden black is a redesign | **Not auto-fix.** Optional aesthetic deviation only with user permission. |
| 2026-11-19 | Planning docs publishing permission granted. Build execution, merging, release **not** independently approved by publication. | Baseline `dev/v0.27-perf06`; branch retains same runtime sources. |
| 2026-11-19 | Add QA03 hub, 120-pair review, source map, bug matrix, bot design and Saturday masterprompt DRAFT | WIP initial version. More user tests may change priorities and instructions. |

| 2026-11-19 18:24 CET | Existing Samstagsvorbereitung automation changed to **hourly** through Sat 21 Nov 23:00 CET, bounded runs, WIP docs only; no autonomous new chat possible | Scheduled task confirmed enabled; `HANDOFF_LIVE.md` for manual chat transfer. |
| 2026-11-19 18:30 CET | Read exact pinned `Raikaru/Persona4-Decompilation@363eab8a...` and linked Battle/Weather/Bones FUN address entries | `DECOMP_SOURCE_MAP_WIP.md` T1 static; no compilation or runtime proof. |
| 2026-11-19 18:30 CET | Compared PSP Go/3000 CPU/RAM/GE specs to perf06 framebuffer layout | `PSP_HARDWARE_LIMITS_WIP.md` hardware/VRAM formula; no performance diagnosis yet. |
| 2026-11-19 18:30 CET | Confirmed PPSSPP's public WebSocket debugger input, savestate, GPU capture, replay control catalog | `PPSSPP_BOT_INTERFACE_WIP.md` T1 API evidence; not yet connected or implemented. |
| 2026-11-19 18:30 CET | Created public `HANDOFF_LIVE.md` and linked it from draft masterprompt | Documentation-only continuity; new Project chat must be opened manually by user. |

| 2026-11-19 18:37 CET | Scheduled research adjusted to **hourly until Sat 2026-11-21 13:30 CET** (last regular slot 13:00). Handoff update policy strengthened: every new useful finding reflected in `docs/qa03/HANDOFF_LIVE.md`; no empty commits, successor chat loads live GitHub file. | Automation updated; docs-only, runtime untouched. |

| 2026-11-19 (interactive \`mach weiter\`) | **New T1 All-Out cut-in resource/effect trace** pinned to game-source SHA: roundup request FUN_001AE800 → cutin resource loader FUN_001F9CF0 (count+1) → model effect draw FUN_001FA190 → child primitive dispatch FUN_00485870. Separate candidate UV/geometry routines FUN_004824A0 and FUN_004A5FC0; KOMA2/3/4 directory strings exist but resolved runtime paths are not proven. | [ALL_OUT_CUTIN_TRACE_WIP.md](ALL_OUT_CUTIN_TRACE_WIP.md); updated source map, code map, QA matrix and Saturday masterprompt. T1 static, **no build or screenshot upload**. |

| 2026-11-19 · code-focused interactive | **New T1 graphics code atlas**: battle dial contour FUN_00203930/frame FUN_0020B1A0/backdrop FUN_00204A30; battle UI registry FUN_00200FD0; party portrait roster FUN_002167F0 and portrait FUN_002142B0; Social Link UI FUN_00356A10 → portrait quad FUN_0035AFF0 with start values FUN_0035BC10; weather/GS alpha mask ownership. Golden UI problems still lack exact T2/T3 root-cause tests. | [GRAPHICS_CODE_OWNERS_WIP.md](GRAPHICS_CODE_OWNERS_WIP.md) + source atlas/code map/QA matrix/masterprompt updated. No game code, builds, hardware or copyrighted assets changed. |

| 2026-11-19 · scheduled research policy | User clarified that source-code investigation must remain central even when the hourly topic is hardware, bot or performance; automation updated with a **CODE-FIRST** requirement: open exact pinned PS2/P4P source, document verifiable code ownership or explain why no new mapping is possible, avoid redundant searches. Existing Saturday 13:30 CET cutoff unchanged. | GitHub evidence/Docs only; no new runtime permission. |

## How new evidence must be merged
1. Record: date/time, reporter, affected build commit, save provenance, PSP vs PPSSPP, scene + pair ID, observed and *expected* behavior, screenshot/log (private URI, not public image), confidence.
2. Update `QA_MATRIX_WIP.md` and `120_pairs_widescreen_WIP.md` only if the evidence materially changes a claim; update `CODE_MAP_WIP.md` if a code owner becomes proven. Add a dated row here.
3. Update `MASTERPROMPT_WIP.md` **only after** reconciling current docs and permissions. Do not reproduce old, corrected claims. Each substantive change should carry a Git commit and a short user-facing note.
4. Promote an issue to `H`/hardware-validated only after the PSP Go has actually shown the fixed behavior under a controlled A/B; a PPSSPP proof remains `E`.
5. The human gives a final explicit go/no-go to the proposed Saturday implementation prompt. WIP publication does not mean an unreviewed merge to `main` or `dev/v0.27`.

## Source of truth decision
`docs/qa03/README.md` is the current index. The older `docs/v027_golden_ui_layout_qa03.md` and its 71-based coordinates are archived as historical, not active. The older hardware comparison may mention earlier Golden IDs only as dated history. The new 120 list has no implied 71-ID correspondence.

| 2026-11-19 | Harden historical documentation: replaced stale 71-based handoff UI section and labeled old b388 report Golden IDs historical | Prevent future Codex mixing different screenshot indexes; no runtime modifications. |

| 2026-11-19 19:00 CET | All-Out effect-tree draw gate pinned: frame/startFrame and child disabled flag filter visible primitives; callbacks conditional; dispatch handler header incomplete. Avoid logging all allocated children as visible. | T1 original source only; [trace](ALL_OUT_CUTIN_TRACE_WIP.md), commit `92f72dace5c1fd69a75e053402d39e9ac0c09189`; no kind→draw implementation or T2/T3. |
