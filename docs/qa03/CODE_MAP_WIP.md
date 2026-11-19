# Code ownership map and executable investigation strategy (WIP)

> **New pinned upstream game-source atlas:** [exact function/line/PS2 markers](DECOMP_SOURCE_MAP_WIP.md). Hardware resource budgets [here](PSP_HARDWARE_LIMITS_WIP.md), PPSSPP debugger control [here](PPSSPP_BOT_INTERFACE_WIP.md). These documents complement the port map, not replace A/B validation.

**Verified on** `MeMeNT0xMoRii/persona4-portable` branch `dev/v0.27-perf06`, 2026-11-19. These are **entry-point line ranges**, not verified pixel-level root causes. Link to source and use exact commit SHA on implementation day; line numbers can shift. No runtime changes were made.

## Architecture layers

| Layer | Owner(s) in P4P repo | Evidence / how to investigate |
|---|---|---|
| Start, game image and logging | [`port/main.c`](../../port/main.c) top, `P4_BUILD_ID` ~21, boot + logging | EBOOT and `p4data.bin` identity, loader maps retail ELF data at fixed PSP image address, startup logs |
| PSP Go devices, saves | [`port/paths.c`](../../port/paths.c) `game_device` ~9 and `p4_paths_init` ~14; [`port/memcard.c`](../../port/memcard.c) `sync_card` ~32, `sceMc2*` ~59–216 | `ef0:/` vs `ms0:/` startup, save/restore, sync and HOME lifecycle |
| Renderer / RenderWare emulation | [`port/rwsky.c`](../../port/rwsky.c) ~11,397 lines | GE state, VBO/mesh/texture/material, clipping, framebuffer alpha, UI 2D conversion, skin, particle, camera |
| 2D coordinates and sprite projection | `port/rwsky.c` `target_sx=480/640`, `target_sy=272/448` ~1214–15 and target switching ~1282–85; Im2D vertex `out->x/y` ~2893–94; sprite `tint_sprite` ~3588–91 | Differential X/Y scaling **may** cause proportional distortion; to confirm isolate individual call sites, source region, UVs, PS2 pixel aspect and actual output box. **Never globally replace factors** |
| Battle UI/Rush/TV contour | `tools/psp_census.py` `FIXES` for `src/Battle/btlEffect_inferred_00202c60.c` ~437–46 (panel frame NaN and scale), `src/Scene/mt_scene_inferred_00266a40.c` ~85–96 (TV ring hook); `port/rwsky.c` 2D primitives | Trace script caller and draw boundaries; issue #77–83, #110/#113, keep Rush anchor `tools/psp_link.py` retail `0x7615a0` |
| Fonts and 4:3→16:9 game-level patches | `tools/psp_census.py` ~57–73: `src/Event/Fcl/fclBankManager_inferred_00103510.c`, `src/Scene/mt_sceneFunc.c`, `src/frFontRaster.c`, `src/nmCmdList_inferred_00353b50.c` | Widescreen transformation may happen in **both generated game source patches and renderer**; inspect combined effect; ensure glyph aspect/spacing remain correct |
| Social Links, portraits, dialogs | `port/rwsky.c` Im2D/sprite path + game-source UI callers located through generated `build/census/src` text; `tools/psp_census.py` rewrite entry points | IDs #43–45 (list vs detail), #50–72 dialogs/shops, #86–90 navigator, #107–109. Source specific UI controller function in decomp first; do not apply global portrait scale |
| Weather, date, calendar | `tools/psp_census.py` `shim` ~692–96: TV `calDrawWeather` replaced by `p4_forecast_weather` for `src/itfMsgProcedure_Window*.c`; calendar fixes ~188–206 and `port/rwsky.c` `draw_calendar_date` ~3374–86 | TV weather masks != field HUD; separate month/day typography, weather cards, depth/alpha and source call sites; Golden ID #106 TV Weather News, #93–105 field HUD |
| Fusion, shuffle, Margaret | `tools/psp_census.py` `src/Event/y_fclCmbBall.c` ~109–16; `src/Battle/btlShuffleCard.c` ~176–81 and `btlShuffleDraw.c` ~183–86; [`docs/v027_perf06.md`](../v027_perf06.md) fusion state 9/7 | Keep fixed Margaret seat and golden fusion HUD; b386 card-face cull isolates Im3D; do not change game fusion rules |
| Battle card rendering | `port/rwsky.c` `im3d_draw` ~11192–11237, current `cull_cards = (shuffle_card || shuffle_nonindexed) && render_state[RS_CULLMODE] == 2` ~11228 | Upstream b386 `p4_im3d_cull` extends to all Im3D that request backface culling; **strong static correspondence**, hardware A/B still required |
| Velvet Room carpet/Igor table | `port/rwsky.c` `bind_depth` ~2169; no equivalent scoped mesh culling; upstream b388 `port/rwsky.c` `p4_cull_back=3` and `bind_depth` ~2012–28 | b383 cull only lit untextured mesh without prelight, game cull mode 2. Broad cull has upstream-confirmed curtain/wall holes. Source-evidence strong; effects untested in fork |
| Animation, skeleton and bones | `tools/psp_census.py` `src/Graphics/Model/mdlManager.c` hooks ~312–56, [`port/rwsky.c`](../../port/rwsky.c) skinning/profiler sections; [`port/clip.h`](../../port/clip.h) near-guard | Separate skinned mesh, pose update, attachment, CPU guard and GE guard. b385 `p4_skin_guard_box` A/B; b388 `p4_skin_guard=0` has potential close-camera holes |
| Geometry/frustum and feedback | [`port/clip.h`](../../port/clip.h), `port/rwsky.c` camera RTT / DATE / alpha / blending / guard | Junes West floor/shadow, Castle floor lines, other distance effects; do not confuse Junes West Entrance with Department Store |
| Hardware perf / logs | [`port/frame_timing.h`](../../port/frame_timing.h), `tools/psp_hw_debug.py`, `tools/psp_debug.py`, `tools/ppsspp_lab.ps1` | Confirm actual 333 MHz PSP Go p50/p95, GPU wait, frame-to-frame regressions; PSP screenshots and log timings are different evidence |
| Audio/video + movie transition | `port/audio.c`, `port/audio_game.c`, `port/video_game.c`, `port/video.c`, `port/lifecycle.c`, `tools/psp_audio_video_probe.py` | Intro/title slowdown, movie handoff, return-to-title flash, keep sources and generated assets consistent |
| Build/translation/runtime identities | `tools/psp_census.py` ~692+ creates `build/census/src`, `tools/psp_link.py` `PORT` ~25 + relocation; `tools/psp_make_data.py` source markers / native functions; `tools/psp_data_census.py`; `tools/psp_vu0.py`; `docs/building.md` | `src/` game-source is fetched from pinned **external decompilation**; not shipped in this repo. 821 compiled game objects documented in perf06. Record SHA and toolchain, **do not upload original ISO or derived game assets** |

## Deep dive: All-Out and other battle cut-ins (2026-11-19)

The pinned PS2 game source has a verified **T1** routing chain for the All-Out/roundup effects: `btlActionRoundupMesUpdate` (FUN_001AE800) → `btlCutinLoad` (FUN_001F9CF0; group variant parameter count+1) → `btlCutinLoadStatus` (FUN_001F9FA0) → `btlCutinUpdate` (FUN_001FA190) → `mdlEffectFlushTreeFrame` (FUN_00485870; primitive kind dispatch). Different resource templates are used for other cut-in kinds. Investigate `mdlEffectAdvanceTrackQuad` UV/rect and `EffSpriteUpdate` geometry **only after identifying the exact All-Out primitive kind**. See [scoped evidence/addresses and procedure](ALL_OUT_CUTIN_TRACE_WIP.md). No confirmed crop fix or new runtime test.

## What the code observation proves—and does NOT

**Proved by static source read:** perf06 scales normal 2D vertex positions with different X/Y factors. It has special battle-panel/circular TV hooks, weather caller rewrites, fusion indicator width patches, and separate Shuffle-Time Im3D culling. Upstream b383/b386 contain the specific scoped culling variants. These are verified source facts.

**NOT yet proved:** which exact UI source call paints a particular #44 portrait, #77 combat circle, #116 eyes, #118 All-Out layer; whether a line adjustment yields a correct 16:9 rendered result; whether a fix maintains FPS on actual PSP Go. Static code cannot establish that; it requires draw/vertex capture and A/B with source + hardware artifacts.

## Expand this into a searchable whole-game index (incremental—not 100% in a weekend)

1. Preserve exact upstream game-source revision, toolchain identity and the prep `prepare.json` / `build_manifest.json`. Generate an inventory *locally* after `psp_census.py --compile` from the actual `build/census/src` tree. The generated game source is **not in public P4P repository**.
2. Export one row per function to `build/qa_index/functions.tsv`: `module | source_path | function_name | original_retail_address(FUN_*) | compiled_symbol | caller hints | ui_scene_ids | artifact_hash | confidence`. Unknown address remains `unknown`, not invented.
3. Export `build/qa_index/modules.tsv` and `calls.tsv` from a parser/compiler index (Clang AST or ctags for definitions; call-edge hints from static tooling), plus a scene trace table `scene_id -> UI controller -> Im2D/Im3D/GE draw -> texture/UV -> PSP screenshot`. Lexical grep alone is a **candidate** edge.
4. Use the linker/census tools to relate original `// FUN_*` addresses and native rebasing, verifying exact function symbol before claiming a mapping. Map imported assets by **metadata/hash**, never publish game art/audio/scripts.
5. On every new fix add function+line+address+scene+test+result to the index and this decision log. Full coverage is a long-term goal; prioritize currently touched systems before cataloging all unused functions.

## Minimum trace worksheet per issue

`Issue | Pair IDs | Gameplay scene/save | Source file + commit + line | Original addr/symbol (if known) | call chain | visible primitive/UV/scissor | baseline capture/hash | isolated change | PPSSPP A/B | PSP-Go A/B | rollback flag`.

Do not label source lines as “confirmed to produce desired visuals” before an observable A/B test; use confidence levels: **T0 candidate → T1 static mapped → T2 emulator output → T3 real PSP Go regression-approved**.
