# Crisis Core: live frame comparison, 2026-11-14

Captured from the running PPSSPP v1.20.4, game ULES01044 (1.00), in the opening battle with the movement tutorial over the scene. This is one scene, with a tutorial overlay, not a whole-game performance claim. The game was left running; temporary CPU breakpoints were removed. No port runtime code was changed.

## Evidence

Local artifacts (game-specific, keep in ignored build/):

- `build/crisis_core_frame.ppdmp`: full GE frame recording (719,076 bytes), PPSSPPGE format v6.
- `build/crisis_core_stats.json`: emulator counters; `build/crisis_core_analysis.json` and `build/crisis_core_draws.json`: decoded commands and per-draw state.
- `build/crisis_core_window.png`: desktop screenshot identifying the battle and tutorial.
- `build/cc_displaylist.json`: separate CPU-side command-list snapshot at sceGeDrawSync, starting from a list address observed at sceGeListEnQueue. Its walker follows CALL/RET and JUMP but does not evaluate BJUMP: its command counts are not executed frame totals.
- `build/analyze_cc.py`: analysis of the decompressed capture; `build/cc_list.py`: temporary CPU-side list inspection script.

The emulator debugger listens on port **56779**, not the 12345 default in tools/psp_debug.py. Initial screenshot-buffer requests failed with "Could not download output". CPU reads of VRAM showed an old Square Enix splash: they are stale under this Vulkan backend and must not be used as the current frame. The desktop screenshot is the visual evidence.

## Full recorded frame

1,015 PRIM commands, 75,407 submitted vertices. PPSSPP also reports 231 flushes: a host backend flush is not a PSP draw call. Target rate was 30 FPS. Desktop timings, emulator GPU cycle estimates, and tutorial-scene cadence do not establish real PSP CPU/GE timings.

| Order (zero-based PRIM) | Observed operation | Draws | Submitted vertices |
|---|---|---:|---:|
| 0 | Clear via 2D sprites | 1 | 32 |
| 1-418 | Weighted geometry, lighting off, depth test/write on | 418 | 10,161 |
| 419 | Unweighted geometry, depth write off | 1 | 216 |
| 420-561 | Unweighted geometry, blending off, depth test/write on | 142 | 52,581 |
| 562-1005 | Weighted geometry again (418 draws, lighting on), plus 26 unweighted draws | 444 | 11,829 |
| 1006-1009 | Four triangle strips, depth write off | 4 | 16 |
| 1010-1014 | 2D overlay, blending on, depth test off | 5 | 572 |

All PRIMs target one RGBA8888 framebuffer in this capture (VRAM offset 0, stride 512). No stencil test is enabled at any recorded draw. Texture formats at draws are CLUT4/CLUT8; dimensions are 32-256 texels. These are active texture states, not counts of unique textures.

The first 418 weighted draws and the later 418 weighted draws reference exactly the same captured vertex payloads in the same order. Thus the repeated geometry is proven. Its visual purpose (shadow, depth/occlusion preparation, or another effect) is **not** proven by this inspection; do not label it a shadow pass solely from lighting being off.

Native formats:

- 1,009 triangle-list draws (74,859 vertices), four triangle strips (16 vertices), two sprite draws (532 vertices). Triangle strips are not the explanation for this frame's throughput.
- Weighted geometry: 836 draws, 20,322 submissions across both passes. Positions s16, normals s8, UV float32, weights float32, no vertex colour or index array. Separate formats with 1/2/3/4 weights use 10,296 / 6,738 / 2,880 / 408 submissions: mean **1.675 weight slots per submitted weighted vertex**. This counts declared slots, not inspected nonzero influences or unique skeleton bones.
- Main unweighted format: s16 position, float32 UV, RGBA8888 colour, no normals/index array (169 draws, 54,465 submissions).
- The CPU-side list snapshot contains CALLs to external command blocks and RETs, including repeated targets, plus BOUNDINGBOX/BJUMP pairs. This proves list reuse within that snapshot and GPU bounding-box commands. It does not prove when lists were built or that their bytes persist unchanged across frames. The GPU recording flattens control flow, so it cannot independently establish CALL counts.

## What transfers to this port

The direction is compatible, and partly implemented already:

- `port/static_list.h` and the skin-list path in `port/rwsky.c` already replay prepared commands. The outstanding work is coverage: near-camera floor/stairs and battle arena geometry still enter expensive general mesh processing. Prepare spatially smaller pieces and retain correct near-plane/guard-band handling; a GE bounding-box rejection alone does not solve crossing triangles.
- Packed skin positions, normals and UVs already exist (`p4_skin_pack=1`); our weight data is u8. Copying Crisis Core's float UVs/weights or nonindexed triangle lists would not automatically improve bandwidth. Static Vertex3D/Vertex3DN still use float positions/UVs and are a concrete candidate for one-time quantization, with the decode transform and clipping verified.
- Fewer declared skin weights are worth considering when GE limited. The port's real-PSP b262 palette-4 test saved about 1.9 ms of GE but added about 1.6 ms of CPU and did not improve frame time. Avoid paying a general batching/validation walk every frame to get this saving.
- Small native 2D/state paths are relevant, but the source game needs GS alpha/DATE reconstruction and feedback semantics. This frame has no stencil test and cannot justify deleting or bypassing those port effects.
- Current P4 bottlenecks remain CPU work documented in `perf_status.md` and `animation_psp.md`: model/animation update, draw-order walking, work on skipped ticks, and meshes missing prepared lists. The GE capture does **not** expose Crisis Core's CPU animation implementation. It supplies no evidence for a particular animation sampling/blending design.

Priority: extend prepared geometry/list coverage to the battle arena and near-camera static pieces, measure CPU savings on real PSP at 333 MHz, then address model update and skipped-tick traversal. The observed native frame does not support a promise that a matching draw sequence alone yields 30 FPS.

Format references used to decode the local capture: PPSSPP upstream `GPU/Debugger/RecordFormat.h`, `GPU/Debugger/Record.cpp`, `GPU/GPUState.cpp`, `GPU/ge_constants.h` (downloaded into ignored build/ for inspection).

## Follow-up: actual guard-band strategy in Crisis Core (2026-11-15)

The same emulator remained running in ULES01044's opening battle/tutorial. Two new complete frames and two CPU-side command-list snapshots were collected, followed by CPU memory-write watches with a positive control. Artifacts/scripts are under ignored `build/crisis_core_guard/`, `build/cc_geometry_probe.py`, `build/cc_lists_probe.py`, `build/cc_list_compare.py`, and `build/cc_write_watch.py`.

### The floor really is tessellated already

Decoded all 169 unweighted 3D triangle-list submissions (18,155 triangles). The six flat submissions at world Y=0 contain **345 triangles / 234 unique positions**. Typical floor edges are about **400 world units**; they form a clear grid rather than a few huge spanning triangles. `build/crisis_core_guard/floor_wireframe.png` is a top-down plot directly from those decoded vertices. Other platforms/overlays are excluded from that floor plot.

The camera in this capture has near distance about **150 world units** and far distance about 50,050, reconstructed from its GE projection matrix. Camera position is approximately (0, 537.5, -3996.9). The floor's world coordinates span roughly 4,000 by 12,000 units. The ratio to the near plane matters; do not copy an absolute 400-unit subdivision threshold to P4 without checking its camera/projection/scales.

All 169 static triangle-list submissions have GE clipping enabled. An offline geometric reconstruction found:

- 230 static triangles crossing the near plane, which are submitted to the GE as original triangles.
- 416 static triangles reaching outside the screen-coordinate guard window after near clipping.
- **0 of those guard-window outliers had an intersection with the visible frustum** under the captured matrices.

These counts describe the captured camera only. The reconstruction uses ordinary floating-point matrix/plane math, not bit-exact float24 GE arithmetic, and counts potential frustum intersections without depth occlusion. It establishes how this scene's geometry avoids visible side-guard losses; it is not a proof for every camera or every level.

### It uses a prepared floor list, with hardware bounding-box rejection

Located the exact live vertex arrays corresponding byte-for-byte to all six captured floor submissions. Their vertex addresses are stable across the two CPU snapshots, beginning at 0x0929864c. The floor command block begins at 0x09d919c0 and contains this actual sequence:

```
VTYPE position_s16
BASE / VADDR bounding_box_vertices
BOUNDINGBOX 8
BASE / BJUMP block_end
texture / CLUT / material state
VTYPE / VADDR / PRIM ...
RET
```

Thus the CPU can submit a matrix and CALL; the GE can reject the object using its eight bounding-box corners and skip its prepared draws. The command block does not run a CPU per-triangle guard classifier.

**Write-watch evidence:** watched the entire 34,360-byte range spanning the six floor vertex payloads, and the entire 4,960-byte floor command block, for 1.001 seconds while emulated CPU ticks advanced by about 333 million. Neither range was written. A separate write watch on the main command buffer immediately stopped at a CPU store, proving the write-breakpoint mechanism was active. All temporary CPU and memory breakpoints were removed, and the emulator was resumed. `write_watches.json` holds the results.

The earlier walker stopped prematurely at SIGNAL/END pairs. The new walker handles those continuation pairs and finds 1,022 candidate PRIMs versus 1,015 actually recorded. It still does not evaluate BJUMP, so its candidate CALL/PRIM counts are not execution totals. Of 115 common CALL targets, 94 compared command-block prefixes were unchanged; differences in the other compared blocks were only **BONE_MATRIX_DATA** and one **TEXOFFSETU** word. Some compared prefixes are capped at 512 words; this is not a whole-list immutability assertion. All 173 unweighted 3D vertex payloads (including four strip submissions) were byte-identical in the two GPU recordings.

### Consequence for P4

The proposed direction is concretely supported by this scene: already tessellated static geometry, stable GE-native vertex arrays and prepared lists, GPU bounding-box rejection, GE near-plane clipping, and dynamic bone/UV values changed as command words. For the watched floor there is no per-frame CPU rebuilding or CPU vertex clipping.

What is not established is whether the grid was authored by artists, generated by an offline converter, or built at load time. Nor does this prove that sending arbitrary tessellated P4 meshes without validation is safe. A P4 implementation should derive subdivision requirements from its camera range, prepare spatial groups/lists, and keep a reference/check path for camera cases that cannot be guaranteed safe. The previous P4 experiment that retained per-frame chunk classification did not test this complete approach.

The local disassembler endpoint `gpu.displaylist.disasm` is unsupported by this PPSSPP build; command names above were decoded from raw guest memory using upstream GE register definitions. The GPU capture remains the source for actual frame draw totals.
