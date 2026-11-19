# Persistent field date / time-of-day / weather HUD — original code owner FOUND (WIP)

**Investigation:** 2026-11-19 (interactive `mach weiter`). **Evidence: T1 source-level proven path**, original game `Raikaru/Persona4-Decompilation@363eab8a966d60a9773b45f330f2344329ec4f0a` and P4P `dev/v0.27-perf06`; **T2 PPSSPP A/B and T3 real PSP Go A/B NOT performed**. Canonical screenshot comparisons ONLY the user's private 120 Golden vs 120 P4P pairs from 19 November, primarily **#93–105**. Historical 71-image IDs are obsolete. No copyright game media uploaded.

## RESULT: persistent upper-right HUD owner identified

The earlier hypothesis that the steady field HUD could not be mapped from static source is **superseded**. It is **not** the six-day TV Weather News display, not the one-shot time-change transition, and not the standalone calendar grid.

**Full verified call chain (original game, pinned revision):**

| Step | Original function / address | Precise source | What code does |
|---|---|---|---|
| Boot flow | `initBootChainStep` **FUN_00111990** | [`src/Main/init.c` L132–156](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Main/init.c#L132-L156) | Once setup callbacks complete, directly calls `calendarStartTasks(0)`. |
| Create persistent controller | `calendarStartTasks` **FUN_00123730** | [`src/datCalendar_inferred_001228c0.c` L419–433](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L419-L433) | Creates global work and a main `calendarStateTask` plus **child `calendarQueueResult` task**, whose update repeatedly enqueues a draw. |
| Initialize texture & state | `calendarStateTask` **FUN_001228C0** | [L124–168](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L124-L168) | Loads UI sprite pack from `D_005E5870`, initializes **five separate animation records**, reads `datGetDateIndex`, `datGetTimeSlot`, `calGetDayTableValue`, calls `calendarSetMode(0)`. |
| Enqueue draw | `calendarQueueResult` **FUN_001236E0** | [L405–416](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L405-L416) | Allocates a `PrimQueueNode`, sets callback to **`calendarDrawBackdrop`**, work pointer `iGpffffb1dc`, appends into `D_00795F50`. |
| Draw **steady date/time/weather display** | `calendarDrawBackdrop` **FUN_00122A40** | [L184–402](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L184-L402) | Explicitly draws background, month/day digits, weekday, day phase and weather sprites at the **upper-right** PS2-screen coordinates. **This is the verified original UI owner.** |
| Reflect game calendar changes | `calendarSyncFromDate` **FUN_00123850** | [L468–486](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L468-L486) | Updates cached date/day phase/weather table state for ongoing HUD. |
| Visibility/transitions | `calendarSetMode` **FUN_001238C0** | [L492–556](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L492-L556) | Reinitializes per-group animation fields / visibility changes; source `field_0C` bitmask selects drawn groups. |

## Independent geometry blocks: source-level x/y anchors, **not final PSP pixel bboxes**

Source: [`calendarDrawBackdrop` L238–402](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L238-L402). These coordinates include **additional per-record offsets** (`calendar->records[i].field_10` / `field_14`), pivot, origin, texture dimensions, and animation. Numbers below are approximate base coordinates, not screen-space measurements from screenshot or PSP Go.

| Draw group / condition | Original drawing behavior | Example PS2 base geometry | 16:9 change strategy |
|---|---|---|---|
| Rotating/yellow corner background, bit **1** of `field_0C` | Sprite record `0x25` and selected `0x1D/0x1E`, rotated using `calendar->field_10` and current scene/event flags | first group X **574** (`0x23E`), larger sprite X **547** (`0x223`), Y approx **−33** for rotating card | Treat background as own flexible class; preserve transition rotations and avoid clipping |
| Calendar date and weekday, bit **2** | Two decorative records (`7`, `8`), month+day individual digit sprites, slash `0x13`, day-of-week indexed with `calGetWeekdayOfDay` | date digits broadly X **450–537**, source Y **17** (plus group offsets); weekday X **537** | Measure actual glyph/sprite size and independent margin; move group right/left without globally changing font geometry |
| Day phase / afternoon/evening banner, bit **2** | Sprite record normally `period + 0x14`, special replacements in field/event states; sets per-instance `signedScaleX`, `signedScaleY`; uses pivot and sprite height | X **552**, Y **31**, record 3 offsets; drawn via `sdkSpriteDraw` | Isolate **this exact sprite** and measure physical width, preserve phase-open animation |
| Weather glyph, bit **4** | Two records: **`0x1F`** background and actual **`calendar->field_08 + 0x20`** icon, conditional on `FldStepIndex` and event tasks | both X **583** (`0x247`), Y **51** plus record 4 offsets | Anchor icon/background together, preserve status/phase and separate weather from TV `calDrawWeather` |

The `field_08` weather/state index is initialized via `calGetDayTableValue(date, period)`; screenshot variant must use stable date/weather/phase. The weekday may use different colors depending on `calHasDayEntry`.

## SURPRISE: existing P4P-wide workaround already changes **only the day-phase sprite's X scale**

This is **new, verified T1 in the actual P4P source**, and must not be lost or blindly duplicated:

- Original PS2 [`calendarDrawBackdrop` L366–379](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/datCalendar_inferred_001228c0.c#L366-L379): after creating the day-phase sprite, `signedScaleX = 0xFF6`, `signedScaleY = (s16)progress`.
- Current P4P [`tools/psp_census.py` L188–190](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/tools/psp_census.py#L188-L190) has a **source substitution** for **`src/datCalendar_inferred_001228c0.c`**: `"signedScaleX = 0xFF6" → "signedScaleX = 0xBF6"`.
- The script applies `FIXES` during generated-source preprocessing ([`shim` L692–714](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/tools/psp_census.py#L692-L714)); if invoked for this source, the code path uses **0xBF6 = 3062** rather than **0xFF6 = 4086**. That is approximately **0.7494×** the original horizontal sprite scale.
- Original SDK [`sdkSpriteRight` / `sdkSpriteBottom` L193–249](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Kernel/sdkSpr.c#L193-L249) uses Q12 `scaleX / scaleY` independently to generate the quad extents; [`sdkSpriteDraw` L600–833](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Kernel/sdkSpr.c#L600-L833) computes the sprite's four textured vertex positions and dispatches via RenderWare 2D.
- PSP port [`put_vertex` L2888–2895](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L2888-L2895) converts X and Y to target pixels with distinct factors. At the native 480×272 view `target_sx=480/640=0.75`, `target_sy=272/448≈0.607143`; a non-precompensated equal source rectangle would look **~1.235× wider relative to height** on the PSP display. **However, this one day-phase sprite already has extra X shrink**, and texture original pixel aspect/other source transformations remain to check.

**Interpretation:** a partial special-case fix exists **for the day-phase sprite**, whereas date digit sprites and the weather icon are drawn through different instances and do **not** receive that one substitution. This offers a **concrete explanation worth testing** for **inconsistent proportions within the same top-right HUD**. It does **not** prove that all those elements are visually distorted or that the workaround is presently correct. Avoid blindly changing `0xBF6` or multiplying the whole group by a single factor.

## Follow sprite geometry to the PSP GE (verified static entrypoints)

`calendarDrawBackdrop` → `sdkSpriteDrawOnceAux` **FUN_0046D3B0** / `sdkSpriteDrawOnceTinted` **FUN_0046D4C0** for one-shot elements ([`sdkSpr.c` L1023–1066](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Kernel/sdkSpr.c#L1023-L1066)); or `sdkSpriteNewNode` → `sdkSpriteDraw` **FUN_0046B380** for animated day-phase ([L600–833](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Kernel/sdkSpr.c#L600-L833)); 2D vertex conversion → PSP `port/rwsky.c` `put_vertex`.

**Important:** the `sdkSprite*` functions are used by many other menus; they are **not suitable for broad permanent alterations**. Any experiment should affect this calendar controller's specific sprite instance or source-level group, using one isolated P4P generated-source patch and a bounded logging flag.

## Diagnostics must be better than existing broad 2D census

Existing [`port/rwsky.c` `census2d_note` L2918–3011](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L2918-L3011) is useful, but:
- `p4_probe_2d==2/3` logs only the **first 64** matching 2D draws in a census window, without original game callsite; `==4` only the first **48** textured draws and their vertices. The counters are reset in `census2d_report` after a **256-frame** report window in the current draw cycle ([`rwsky.c` L1758–1765](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/port/rwsky.c#L1758-L1765)).
- Therefore **absence of a top-right HUD draw in a raw full-frame census log is NOT evidence it is not rendered**. It may arrive after the cap or lack the chosen mode's size/texture filter. Census logs also use **PS2 source coordinates**, not post-`put_vertex` PSP-space values.
- Recommended first experiment when code edit permission exists: **one bounded trace at `calendarDrawBackdrop` / its `sdkSpriteDraw*` calls**, log **group, original sprite index, pre-projection vertex bbox, post-projection bbox, UV, raster ID, effective signedScaleX/Y, record offsets and active flags**, with hard **16–32 samples max**; disable by default. Avoid per-frame runaway logging on real 333 MHz PSP Go.

## Codex / Work efficient reproduction and acceptance

1. Start from exact perf06 gameplay baseline; confirm the existing `0xBF6` generated-source rewrite and its build manifest, as docs-only HEAD differs from game binary identity.
2. Use **#93–105** paired scenes at **steady HUD**, not opening/closing animation. Screen coordinates in Golden 960×544 normalize by **2** to PSP 480×272 for anchor comparison. Golden content/date/weather may differ; compare bounding boxes, glyph roundness, image aspect and safe margins, not pixels.
3. Record **four independently addressable groups**: ① background ② date+weekday ③ day-phase ④ weather icon+backdrop. Source `CalendarRecord` interpolation influences x/y, so capture steady values and the first/last animation phase for regression.
4. Evaluate whether the existing X-scale change makes day phase too narrow, correct, or still wide; **do not make a conclusion from constants alone**. Consider one local anchor adjustment per group, and per-sprite X/Y compensation where measurement warrants. Keep full-screen 16:9 unaffected.
5. Validate across **school, town, Junes West, home, dungeon**, multiple dates/periods, with an event overlay both present and absent. A/B PPSSPP and then actual PSP Go. Check no distortions in TV Weather News #106 or calendar/titles/cut-ins.
6. If screenshots show acceptable date/time/weather already, leave it unchanged. FPS gains/penalties require actual measurements; adjusting a few sprites is not a demonstrated speed fix.

**Bottom line:** this converts the persistent top-right HUD from **owner unknown (T0)** to **fully verified code owner (T1)** and reveals a preexisting specific 25% X-scale modification in the PSP source translation. Actual desired geometry correction remains **T0 until measured; T2/T3 not executed.**
