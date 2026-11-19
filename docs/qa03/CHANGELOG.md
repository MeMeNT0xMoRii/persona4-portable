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

## How new evidence must be merged
1. Record: date/time, reporter, affected build commit, save provenance, PSP vs PPSSPP, scene + pair ID, observed and *expected* behavior, screenshot/log (private URI, not public image), confidence.
2. Update `QA_MATRIX_WIP.md` and `120_pairs_widescreen_WIP.md` only if the evidence materially changes a claim; update `CODE_MAP_WIP.md` if a code owner becomes proven. Add a dated row here.
3. Update `MASTERPROMPT_WIP.md` **only after** reconciling current docs and permissions. Do not reproduce old, corrected claims. Each substantive change should carry a Git commit and a short user-facing note.
4. Promote an issue to `H`/hardware-validated only after the PSP Go has actually shown the fixed behavior under a controlled A/B; a PPSSPP proof remains `E`.
5. The human gives a final explicit go/no-go to the proposed Saturday implementation prompt. WIP publication does not mean an unreviewed merge to `main` or `dev/v0.27`.

## Source of truth decision
`docs/qa03/README.md` is the current index. The older `docs/v027_golden_ui_layout_qa03.md` and its 71-based coordinates are archived as historical, not active. The older hardware comparison may mention earlier Golden IDs only as dated history. The new 120 list has no implied 71-ID correspondence.

| 2026-11-19 | Harden historical documentation: replaced stale 71-based handoff UI section and labeled old b388 report Golden IDs historical | Prevent future Codex mixing different screenshot indexes; no runtime modifications. |
