# Eye/skill cut-ins and upper-right field HUD — code trace (WIP)

**2026-11-19, independent interactive code investigation.** Inputs: pinned PS2-US decompilation `Raikaru/Persona4-Decompilation@363eab8a966d60a9773b45f330f2344329ec4f0a` plus P4P perf06. **Code evidence T1 only**; no PPSSPP render A/B (T2), no PSP Go confirmation (T3), no gameplay code edits. The only valid UI images are the private **120 Golden + 120 P4P screenshot pairs**, specifically eye cut-in **#116** and field date/weather **#93–105**. No source screenshot content was published.

## A. Three *different* cut-in routes in the original battle system

| Event/context | Original source + marker | What is verified | Remaining unknown |
|---|---|---|---|
| **All-Out / roundup** | [`btlActionRoundupMesUpdate` FUN_001AE800](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001ade90.c#L452-L484) → [`btlCutinLoad` FUN_001F9CF0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L54-L100) | Roundup creates a `BtlCutinRequest` **kind=0**, containing participant `ids` and count; loader selects member resource variants using `count+1`. | Exact 2/3/4 resource paths, individual sprite/GE crop and layer transforms are unresolved; see [All-Out trace](ALL_OUT_CUTIN_TRACE_WIP.md) |
| **Follow-up / pursuit** | [`btlActionPursuitMesUpdate` FUN_001A3F90](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001a1450.c#L1655-L1674) | Creates `cutin.mode=2` with acting unit ID and enqueues `btlCutinCreatePacket`. [Loader `case 2`](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L86-L90) has a separate resource template `D_00625100`. | Its geometry relative to eye cut-in #116 is not established |
| **Normal skill-action character cut-in** | [`btlActionSkillUpdate` FUN_001A7720](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001a1450.c#L3366-L3373), [decision conditions](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001a1450.c#L3950-L4003), [packet construction](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001a1450.c#L4030-L4054) | When `hasCutin` is set, this path creates **kind=4** with acting character `charId`, enqueues a start packet depending on the opening animation and a cut-in end packet. [Loader `case 4`](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L91-L96) chooses `D_00625130` if ID is less than 11 or `D_00625160` otherwise. | Whether screenshot **#116** is this `kind=4` path is **T0**. It is not established as a dedicated weakness/crit-only path. |
| **Other name/effect types** | [`btlCutinSetupName` FUN_001FA490](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L254-L290) | Distinct kind **1,3,5** name/effect setup; kind 5 creates `cutinDrawTaskCreate`. [Task `cutinDrawTaskUpdate` FUN_00120BC0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/cutinDraw.c#L59-L121) loads an effect and draws it with a separate position/flush path. | No proof kind 5 is the same critical/weak eye-strip geometry or that the generic task fixes kind 4 |

### Action-specific nuance worth preserving

The [skill-action code block](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlAction_inferred_001a1450.c#L3950-L4004) sets `hasCutin` for some actor/skill/target conditions (including flags and a random condition), and may explicitly clear it later. Therefore **do not label the kind 4 action cut-in “the critical-hit/weakness overlay” yet**. The screenshot can be correlated only by a reproducible hit and logging the request `kind`, `charId` and resolved resource path.

### Geometry tracing path

For a reproduced #116 frame:
1. Trace `btlCutinCreatePacket` for request `kind` and ID; log *only that packet* and its resolved `H_Cdvd_AcquireDirectory` path, expected `case 4` if skill route.
2. Follow [`btlCutinLoadStatus` FUN_001F9FA0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L103-L139) → [`btlCutinUpdate` FUN_001FA190](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlCutin.c#L153-L196) → [model-effect per-kind dispatch FUN_00485870](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Graphics/Model/mdlEffect_inferred_00484bb0.c#L490-L605).
3. Record **primitive kind**, original quad geometry, UVs and bounds, source/post-PSP viewport and scissor for a single reproducible frame. [`mdlEffectAdvanceTrackQuad` FUN_004824A0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Graphics/Model/mdlEffect_inferred_00481f30.c#L268-L355) or [`EffSpriteUpdate` FUN_004A5FC0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Graphics/Model/mdlEffect_inferred_004a5fc0.c#L91-L306) are **candidates**, not verified owners of the exact eye layer.
4. If #116 is found to use a different `kind` or script task, preserve that branch separately. Character identity/pose differs between Golden and perf06, so compare visibility, framing, shape and aspect, **not exact pixels**.

**Implementation rule:** All-Out `kind=0`, pursuit `kind=2` and actor `kind=4` are **not** safe to alter as one monolithic cut-in class. Do not disable animation or substitute Golden assets to hide crop.

## B. Persistent upper-right field date/weather (#93–105) — owner remains unresolved

Confirmed **nearby code that should NOT be automatically labeled the field HUD**:

| Source | Verified responsibility | Why not enough for field HUD |
|---|---|---|
| [`cldTimeChangeDraw` FUN_00266CC0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Scene/mt_scene_inferred_00266a40.c#L241-L321) and [`cldTimeChangeUpdate` FUN_00266BA0](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Scene/mt_scene_inferred_00266a40.c#L200-L240) | Animates **time-of-day change** using loaded sprite pack, finite elapsed frames. | Transient transition **not proven** to own persistent overlay top right |
| [`itfMsgProcedure_Window.c` calendar/Weather TV block](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/itfMsgProcedure_Window.c#L2535-L2600) | Six-day **TV forecast** cards with `calDrawWeather`; P4P rewrites only these window callers to `p4_forecast_weather`. | Weather News **#106**, not necessarily a field HUD script |
| [`calendarDraw` in `src/calendar.c`](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/calendar.c#L188-L235) | Standalone calendar view task/grid and date-index handling. | Neither a persistent scene HUD nor evidence of top-right sprite placement |
| [`mtSceneIconTaskUpdate` FUN_0026D890](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/mt_sceneIcon.c#L58-L107) | Loads scene-icon resource handles by index and spawns handle updates. | Task loads resources; **no verified date/weather sprite coordinates** |

The original field UI may be a resource-driven effect or task with a non-obvious name. Absence of a convenient `fclHud` or `drawWeather` symbol **does not prove its absence**.

### Efficient path to a verified field-HUD owner (Saturday execution)

1. From perf06 capture a **single fixed field frame** (same day/weather/area and no transition). In the existing [`port/rwsky.c` `p4_probe_2d` census](../../port/rwsky.c), filter `Vertex2D` bounding boxes to **top-right 480×272 safe area**, while recording original texture/raster identity and callsite/return address if reliable. The census already handles 2D primitive records; prefer extending it by a bounded, temporary diagnostic flag rather than new heavyweight instrumentation.
2. Search generated `build/census/src` and pinned decomp for the **specific draw call + resource**, not only strings `weather`/`date`. Map task owner → function / retail address → original coordinates → post-PSP coords.
3. Compare field **date text**, **weekday/time label**, **weather glyph**, **decorative frame** independently. Record bbox and relative aspect, then design local fixes if a distortion is actually confirmed; no global `target_sx/target_sy` replacement.
4. Do **not** rename `cldTimeChangeDraw` or `p4_forecast_weather` as field overlay owner before proving it with a real draw; this would waste Codex credits.

## Source ownership/status and next branch

- **T1:** exact original function markers and request-type distinction; confirmed transient/TV/calendar helper responsibilities.
- **T0:** exact `#116` kind, exact geometry/crop cause, persistent field HUD draw owner, whether the same renderer pass produces particular visual distortions.
- **T2/T3 not run.** No new files/assets/screenshots/game code were created beyond this **public Markdown research document**.
- **Recommended next code focus:** resolve the **kind 4** effect resource path and per-primitive draw handler, or link the persistent field HUD to a draw via bounded `p4_probe_2d`/generated-census trace. In parallel still investigate particle/alpha GE and bones when a precise reproducible scene exists.
