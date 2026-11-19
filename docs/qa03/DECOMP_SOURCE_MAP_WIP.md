# Persona 4 PS2-US decompilation code atlas — first verified probes (WIP)

**Verified 2026-11-19:** Our fork pins public `Raikaru/Persona4-Decompilation` revision **`363eab8a966d60a9773b45f330f2344329ec4f0a`** in [`docs/building.md`](../building.md) lines 30–41. The GitHub public source at that **specific commit** was successfully read with the GitHub API; we do **not** need to ask Codex to rediscover the pinned repository. This is a working **source investigation map**, not whole-game catalog completeness.

## Source provenance and constraints

- Source: https://github.com/Raikaru/Persona4-Decompilation/tree/363eab8a966d60a9773b45f330f2344329ec4f0a
- Our fork reads/rewrites this separately fetched game C source under `ext/Persona4-Decompilation` and locally generated `build/census/src`; the P4P repo itself has `port/` runtime replacements and `tools/psp_census.py` compatibility rewrites.
- The decomp README explicitly says incomplete functions still contain retail assembly. **Readable C ≠ every game function recovered or semantically proven.** Source matches PS2 retail code; our PSP version uses mechanical shims and MIPS/PSP replacements.
- Do not republish original game files, binary excerpts, media, or complete third-party decomp dumps. Link directly to exact pinned public file/line. Check third-party licensing before downstream redistribution.

## NEW 2026-11-19 — All-Out resource / effect draw chain

- [**ALL_OUT_CUTIN_TRACE_WIP.md**](ALL_OUT_CUTIN_TRACE_WIP.md) maps the pinned original `btlActionRoundupMesUpdate` **FUN_001AE800** (kind=0 + party IDs + count) → `btlCutinLoad` **FUN_001F9CF0** (`count+1` resource templates, async paths) → `btlCutinLoadStatus` **FUN_001F9FA0** → `btlCutinUpdate` **FUN_001FA190** → `mdlEffectFlushTreeFrame` **FUN_00485870**, per-primitive dispatch table `D_00713480 + (kind << 6) + 0x0C`. Includes related track-frame bbox/UV and sprite-geometry candidates. The precise screen crop point is **not** proven. Reference Golden/P4P pairs **#112, #118, #119**, eyes **#116** separately.

## NEW 2026-11-19 — independently verified battle + Social Link graphics entrypoints

See [`GRAPHICS_CODE_OWNERS_WIP.md`](GRAPHICS_CODE_OWNERS_WIP.md) for exact original function lines and PSP code links: battle curved selection **FUN_00203930** + frame **FUN_0020B1A0**; independent backdrop **FUN_00204A30**, UI registry **FUN_00200FD0**; right portraits **FUN_002167F0** + **FUN_002142B0**; Social Link UI `datCommunityListDraw` **FUN_00356A10** → portrait `datCommunityPortraitDraw` **FUN_0035AFF0**, including their explicit coordinate/quad geometry; TV forecast weather caller and PSP alpha exception. **These are T1 owner mappings, not experimentally validated graphical fixes.**

## Confirmed source entrypoints (T1 static location only)

| System & pair IDs | Verified original source (exact pinned commit) | Function / line / retail marker | P4P adaptation | Current claim / action |
|---|---|---|---|---|
| Combat selection panel (#77–83, #110/113) | [btlEffect](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlEffect_inferred_00202c60.c#L3743-L3803) | `btlPanelDrawFrame` **L3744–45** marker `FUN_0020B1A0`; `btlPanelDrawBackdropGrid` **L3800–03** `FUN_0020B3A0`. `BtlPanelWork.scaleFrame` at L256–59; transition changes L3683–91. | `tools/psp_census.py` `FIXES["src/Battle/btlEffect_inferred_00202c60.c"]` ~L437–46 initializes otherwise uninitialized `scale` via `p4_panel_scale` | **Strong T1 entrypoint**, but unknown whether UI wheel oval comes from circle draw, aspect transform or backdrop mesh. Trace `btlPanelDrawOutlineFill` and caller → PSP Im2D vertices per target-selection state. |
| General UI TV ring/circle (battle or transition) | [mt_scene](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Scene/mt_scene_inferred_00266a40.c#L508-L610) | `PrimDrawContour` **L513–15** marker `FUN_00267B20`, checks `shape == 9` ~L585 | `tools/psp_census.py` ~L85–96 inserts `p4_draw_tv_ring` for shapes below 9 | **Potentially separate from combat action wheel.** Confirm actual call chain before changing anything. |
| TV weather cards / UI #106 | [itfMsgProcedure_Window](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/itfMsgProcedure_Window.c#L2393-L2595) | `msgCalendarDraw` **L2395–96**, `FUN_00283490`; `calDrawWeather` declared L2426 and called L2590 for animated calendar cells | `tools/psp_census.py` `shim` ~L692–96 replaces `calDrawWeather` with `p4_forecast_weather` in `src/itfMsgProcedure_Window*.c` | **Verified calendar/weather path**, but title `Weather News` overlap may be from a separate text layer. Trace labels / draw ordering; don't assume `calDrawWeather` causes title. |
| Widescreen field bank scale | [fclBankManager](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Event/Fcl/fclBankManager_inferred_00103510.c#L760-L775) | `D_00796728` bank scale set at **L768** to float-bit `0x3f800000` (1.0) in pinned PS2 source | `tools/psp_census.py` ~L57–60 rewrites to `0x3f400000` (0.75) in PSP source, plus scene FOV `/1.2578125f` and font fixes ~L62–73 | Potential **double interaction** with `target_sx=0.75`, `target_sy≈0.6071`. Do not undo individual fixes without a measured scene. |
| Bones / pose / model | [mdlManager](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Graphics/Model/mdlManager.c#L840-L852) | `mdlUpdateHierarchyPose` **L844–45**, marker `FUN_00471370`; `mdlRebaseLayerPose` **L2038–41**, marker `FUN_00473870` | `tools/psp_census.py` `mdlManager` FIXES ~L312–56; `port/rwsky.c` skeleton render, skin and per-section profiling | T1 candidates for head/limb transforms and skin performance. Do not infer a visual regression without test. |
| Shuffle Time cards | [btlShuffleCard](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlShuffleCard.c), [btlShuffleDraw](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlShuffleDraw.c) | `pscModelSetPersonaCard`, `pscModelDraw`, `btlShuffleDrawCards` | `tools/psp_census.py` `src/Battle/btlShuffleCard.c` + `btlShuffleDraw.c` ~L176–87 wrap card drawing | P4P already has shuffle-specific culling; upstream **b386** Fusion-card fix expands to other Im3D cull-mode 2 paths. Validate both to avoid regressions. |

## Verified current P4P PSP renderer 2D conversion

- `port/rwsky.c` ~L1214–15: normal display `target_sx = 480.0/640.0`, `target_sy = 272.0/448.0`. Ratio `target_sx/target_sy≈1.2353`.
- ~L1282–85: render-target transitions replace the factors with raster-specific dimensions.
- ~L2893–94: 2D input vertices separately multiply `x` and `y` by those factors.
- ~L3588–91: tinted sprites also scale width and height separately.
- Important: **PS2 source display pixel aspect + pre-existing widescreen transformations** mean the ratio alone **does not prove any particular face/circle deformation**; measure before and after the exact call.

## How to build the long-term whole-game atlas

For each generated symbol, produce a local CSV/SQLite index `module, original FUN address, source file, source line, generated-census file, PSP symbol/link address, caller(s), callees, resource refs, scene IDs, tested build, confidence, regression ID`. Source parser/compile reports plus `tools/psp_make_data.py` markers / `tools/psp_link.py` relocation are appropriate sources. Do **not** claim 100% coverage from text grep. The user is interested in all systems (field, battle, dialogue, UI, bones, texture pipeline, renderer, collision, saves, I/O, audio/video) over time; **prioritize what blocks active builds now**, then widen systematically.

### Proposed next probes (not yet inspected)
- Find game-level source owners of Social Link list/detail portrait controllers (#43–45) and eye cut-ins (#116) by looking at draw calls, resource IDs and task names in pinned decomp. Record candidates as T0 until caller links are verified.
- Map All-Out composition source and any crop/scissor/UV. Keep 2/3/4-character cases distinct.
- Build module manifest with file counts and original addresses for *pinned* SHA, not mutable `main`; compare upstream newer revisions only as separate discoveries.
- Design a source-index generator with a dry-run and exact SHA checks. Do **not** add runtime instrumentation until user approves.

**Status:** static original-game source **successfully accessible**; no compilation or emulator/hardware test performed in this investigation.
