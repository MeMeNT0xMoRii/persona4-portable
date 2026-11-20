# QA06 — private 120-pair UI ZIP independently revalidated for RESONANCE01 (2026-11-20)

**Private data stay private:** this public document contains **no** screenshot, montage, extracted game asset, save or actual ZIP bytes. Tiers: **I** = direct current-upload image inspection/measurement, **T1** = pinned source map quoted from linked UI documents, **H0** = Python image-processing arithmetic; **no** new PSP game executable, PPSSPP T2 or PSP Go T3.

## Archive integrity and canonical identity

- Independently opened actual newly uploaded `P4P-Golden-UI-Comparison-QA03-2026-11-19(3).zip` from the user's QA06 handoff, **35,313,720 bytes**.
- Actual SHA256 **`12e73b56f2d1582102cce18669fb87429edd1df7e5f5e417754820a7eeabd662`**, exactly identical to the existing Saturday RESONANCE01 masterprompt and [UI anchor transform matrix](UI_RESONANCE01_ANCHOR_TRANSFORM_MATRIX_2026-11-20_WIP.md) canonical screenshot archive. This is the same set of bytes, not merely similar captures.
- `zipfile.testzip() -> None` (no CRC mismatch). 242 entries include two folders, **120 Golden `.jpg` exactly 960×544** and **120 P4P-perf06 `.png` exactly 480×272**. Complete IDs `G_1_..G_120_` ↔ `P_1_..P_120_`, no missing ID. Golden resized to 480×272 using Pillow LANCZOS for measurement. The older 71-image index remains INVALID.
- Visually re-inspected 480×272 side-by-side states #77/#80/#110/#113. Golden and perf06 differ in scenario cosmetic content, 3D lighting, character/rank and UI text; image identity does not imply same exact animation/timing. Source remains Persona 4 US Vanilla, Golden geometry-only.

## Independent seven-state yellow battle-wheel mask remeasurement

Method: Normalize Golden to 480×272, RGB→OpenCV HSV, crop to left wheel ROI `x<130,y>=140`; select hue in [16,44] (OpenCV 0..179), saturation >=100, value>=110; connected 8-neighbour components with area>200 px, then bounding union. Sensitivity rerun: (H17..43, S>=130, V>=145) and (H16..44, S>=140, V>=170). Under all three masks the following coordinates hold except a 1px Golden bottom variation in #80 at stricter thresholds.

| Pair IDs | Golden yellow mask bbox inclusive | perf06 yellow mask bbox inclusive | Interpretation |
| --- | --- | --- | --- |
| #77, #78, #80, #81, #82, #110, #113 | `x=9..119`, `y=148..258` (111×111) | `x=6..122`, `y=163..256` (117×94) | P4P wider by 6px, lower top by 15px, mask midpoint lower ~6.5px |

These are *yellow pixel envelope* bounds including text/ornament within ROI, **not exact rendered GE fan vertex bounds**, and are not direct evidence of a working code patch. At strict thresholds #80 Golden's bottom can be y257 (height110). #79 has no stable yellow component under this mask; #83 is visually/transitionally different (edge/bottom fragments), so **exclude #79/#83 from numeric steady-wheel acceptance rather than force-fit a false circle**.

## Implementation consequence / scoped control

- Supports the previously source-backed *candidate* **UI-A** three-fan local shape correction, `btlSelectionDrawCommands FUN_00207B00 -> shdDrawRadialFan FUN_00365F00` (radii 81/78/79, xscale~0.948718, yscale~1.171946, candidate center native(64,203)); full source/cost in [UI-A fan callee/GE budget](UI_A_RESONANCE01_FAN_CALLEE_GU_BUDGET_2026-11-20_WIP.md). The measured mask shape alone cannot prove this exact factor is optimum or that fan/glyph/strip origins coincide.
- Must still pair 7 static screenshot checks with at least different command indices 0/3/7, growth phases, target mode, RUSH and normal 1–4 roster; `btlUiSetOrigin` integer truncation and `btlPanelDrawStrip` independent float coords prevent one-size-fits-all shifts.
- Image comparison cannot establish source call owners for shop rings, Weather News, eye-cutin, All-Out, Teddie or alpha glitches without same phase/actor/asset verification. No Golden assets import, no universal renderer X/Y transform.
- **GO/NO-GO:** unchanged ability to navigate all commands; no clipping below bottom edge, text/selected stripe aligned, unchanged 3×50 fan vertices and acceptable GE headroom after SEC-01 safety; PPSSPP T2 same-state screenshot then actual PSP Go T3. Preserve immutable perf06 regular as A comparison. No claimed FPS/graphics fix from these private masks.

Current frozen game source `29b0fcfdec70222a4052099b67fb4a11e8d8aff4`; QA branch documentation may move without any new compiled EBOOT. This report documents **newly repeated evidence from the user's actual current upload**, not a shipped RESONANCE01 UI implementation.