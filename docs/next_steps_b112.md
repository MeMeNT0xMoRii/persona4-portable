# Next steps after build 112 (written 2026-11-06)

> **Historical record.** This file is no longer the current plan. Its "Plan" and "Next" items were done,
> superseded or carried over. The current status and plan are in [perf_status.md](perf_status.md). Keep this file
> for its measured facts and the reasons behind earlier decisions. Device numbers dated 2026-11-10 were taken at
> 423 MHz, not 333.

For the agent that continues the PSP performance work. Goal from the user: a stable 30 FPS on the real PSP,
with effects adapted, not removed. Branch: `perf/pipeline-guard-b110`, not merged into `main`.

## Where we are

Commits on the branch, oldest first:

| Commit | What |
|---|---|
| `ff32c3c` | `p4_pipeline=1` by default; cheap guard band (compact arrays, cached view x projection, `p4_guard_gnear`, off-screen triangles dropped, chunk boxes `p4_guard_boxes`); profiler sections inside the guard; PSPLink tools |
| `7721d16` | `hash_words` read floats through `unsigned *`. Under strict aliasing the mesh-cache key took stale stack words, so ~100 entries a frame were rebuilt at random, depending on how the build was laid out. Fixed with `may_alias`. |
| `b24cac3` | `tools/psp_reasm.py`: a `lui` in a branch delay slot, used at the branch target, was never rebased. This crashed `sinf` (`func_00449120`) when Zio was cast. 8 sites fixed; Zio verified on b111. |
| `a6e1a87` | The log is written to the memory stick before PSPLink stdout, so a stuck USB cable does not stop the file log. This is build 112. b112 is built (`build/psplink_b112`) but **not yet deployed or measured**. |

Frame times on the PSP. All are with pipeline on; "profiler off" means `profmode` Off.

| Scene | b97 this morning | Now | Notes |
|---|---|---|---|
| Dojima's room (save 1, the spot after loading) | 74 ms | **44.5 ms** (b110) | GE is the wall: without the guard 50 ms. Chunk boxes keep off-screen chunks away from the GE. |
| Station (dialogue, bottom-left floor triangle) | — | 50.3 ms (b103) | GE is the wall. The guard costs ~0.2 ms of frame and ~6 ms of CPU. |
| Heaven, 10th Paradise (save 3, entrance stairs) | 117 ms | 88.9 ms (b103, profiler on) | CPU is the wall. Not yet measured after the hash fix. |

Results tables and raw logs:
- `build/psplink_b97/experiments/RESULTS.md` (morning A/B: pipeline, vsync, lighting, guard, mipmaps);
- `build/psplink_bNN/experiments/*.json|.log` per arm.

## How to work (this machine)

See also the memory note `p4-psplink-workflow`. Short form:

- **Toolchain.** Set `PSPDEV` to your Windows PSPDEV root (see `docs/building.md`).
- **Build.**
  - Bump `#define BUILD` in `port/main.c`.
  - Run `PSPDEV=... python tools/psp_hw_debug.py --out build/psplink_bNN` (~35 s).
  - If `tools/psp_reasm.py` changed, first run `PSPDEV=... python tools/psp_link.py` (~8 min) to regenerate `build/psp/reasm.o`.
- **Host programs.** Set `P4_HOST` to a local host folder and `P4_SESSION` to a separate local session folder.
  - `usbhostfs_pc.exe` and `pspsh.exe` come with PSPLink (`tools/psplink/` of a PSPLink host build).
  - Start `usbhostfs_pc` with no arguments, cwd `${P4_HOST}` (= `host0:/`). Never use `-vv` with its log on the share: a 2 MB copy then took 20 min instead of 4 s.
  - Then start `python tools/psp_shell.py --pspsh <pspsh.exe>`.
  - Processes started with PowerShell `Start-Process` did not survive here at the end. Use a Bash background task, with `tail -f /dev/null |` feeding stdin to `usbhostfs_pc`.
- **Deploy.**
  - Copy `p4.prx` as `${P4_HOST}/bNN/p4_bNN.prx`, plus `p4data.hwdbg.bin`.
  - In the PSP shell: `cp host0:/bNN/... ms0:/PSP/GAME/P4/`, copy both back, compare SHA256 with the manifest.
  - Then `reset`, then `ldstart ms0:/PSP/GAME/P4/p4_bNN.prx`, and wait for `hwdbg bNN PRX image=`.
  - The data file changes with almost every build: always copy it.
- **Profiler.**
  - `profmode t` before `reset` gives the `hwprof` sections. It survives `reset` but not a PSPLink restart.
  - It adds ~5 ms of CPU, so compare arms only within one mode.
- **One A/B arm.** `PSPDEV=... python tools/psp_probe_ab.py LABEL --base 0x08804000 [--set name=value] [--shot] [--preview]`.
  - The script restores the defaults in its `DEFAULTS` table: keep that table equal to the C defaults.
  - `--old-build` measures an older build with its own ELF.
- **Reaching a scene yourself.** `tools/psp_press.py --base 0x08804000 start:2 cross:2 ...` presses buttons through `p4_pad_press`.
  - From the title: `start:2 cross:2` opens "Load which file?". Then `cross:1.5 cross:20 cross:20` loads save 1 (the room). For Heaven, press `down` twice before loading (save 3 is the third entry).
  - Take a screenshot after every step (`psp_probe_ab.py navN --shot --preview`) and look at it. The replayed route once lost its `down` presses.
- **The USB cable loses contact.** The host log showed ~3000 reconnects in one evening. When replies stop:
  1. Count `waiting for device` in the host log before blaming the build.
  2. Ask the user whether the game still runs.
  3. Ask them to restart PSPLink.
- **Escaping.** Python heredocs inside Bash turn `"\\n"` into a real newline in C strings. Write patch scripts to the scratchpad with the Write tool instead. `port/*.c` are LF; keep them LF.

## Plan, in order (historical, 2026-11-06; superseded)

Each step: one change, one build, measured on the device with baseline and baseline_again, picture checked.

1. **Deploy b112 and remeasure.**
   - Heaven (profiler off and on): baseline, `p4_guard_clip=0`, `p4_guard_boxes=0`.
   - Station: the same arms, plus a screenshot of the bottom-left floor corner. b101 showed a hole there without the guard; it must not come back with boxes.
   - These are the first numbers after the hash fix outside the room.
2. **Guard to ≤1 ms of CPU** (the user's target). Current costs per frame:

   | Part | Cycles | Notes |
   |---|---|---|
   | array classify | ~0.66M | Heaven, b103 |
   | sending the pieces to the GE | ~0.49M | |
   | vertex classify | ~0.33M | |
   | triangle loop | ~0.26M | |

   - With boxes the array classify is a sphere test only.
   - Boxes v2:
     - loop chunk by chunk, not per triangle (no integer divide per triangle);
     - send runs of inside chunks as index ranges of the cached entry, without copying;
     - classify partial chunks' vertices with the VFPU (`guard_vfpu` with a gathered list) instead of `guard_code_lazy` on the FPU.
   - Measure `mgdraw`/`gloop`/`gsub` in Heaven.
3. **The GE is now the wall in the room and at the station** (CPU ~43-46 ms against ~45-50 ms of GE). Candidates:
   - full-screen 2D passes (`p4_probe_flags=2` showed 2D costs ~2.6 ms in the room; on the title screen it is mostly 2D);
   - the second TEST_1 passes (`gs test` line: ~187 a frame in Heaven).
   - Use `p4_exp_ge_time` (serial GE timing) and a GE snapshot (`build/psplink_session/analyze_ge.py`) to rank them.
4. **Cost of one draw.** `draw_mesh_vertices` costs ~11k cycles, ~1.5M a frame in Heaven (138 draws). Look at `bind_ge_light`, `draw_tested` and the state calls per draw.
5. **memit on the VFPU.** Writing vertices for the GE: ~4.6M cycles in Heaven before the hash fix, probably much less now. Remeasure first.
6. **Unattributed CPU.** ~13 ms in `mesh` and ~15 ms in `atomic` with no sub-section in Heaven, plus ~29 ms outside the render frame (game logic). Add profiler sections before optimising.
7. **Title screen (scene C of `docs/hw_test_plan_b97.md`).** Never measured.

## Facts measured this session (do not re-learn them)

- **The GE does not save triangles that cross the near plane if a corner is behind the eye.** b101 `p4_guard_near_ge` left the station hole. With `gnear`, the CPU cuts only at the four band planes, which pass through the eye, and the GE clips the near plane. The picture is the same.
- **`p4_vsync_late` does nothing in pipeline mode.**
- **Pipeline:** no flicker in Heaven or the room; buffer peaks below half; `texture waits` 0.
- **Cache audit (`p4_cache_audit=1`, every 8th hit rehashed in full):** 0 stale hits. The sampled key is safe. The audit itself costs ~7 ms: keep it off.
- **`clip_triangle_guard_fast` matches `clip_triangle_guard` byte for byte:** 45k triangles checked, 0 differences. It is not faster: the per-triangle cost is outside the clip arithmetic.
- **Lighting is already on the GE:** ~4500 vertices a frame in the room; the CPU lights ~140. A VFPU port of the lighting would not help.

## Update, late 2026-11-06 (builds 113-118, not committed yet)

Fixes in the working tree, each traced from a crash on the PSP:
- `tools/psp_census.py`: the `Get...Vec` quad accessors (`mdlEffectGetPositionVec/OrientationVec`) become void like
  the setters. Callers declare them void, and the hidden struct return shifted `output` to `a2`, so the PSP stored to
  0x6B in an event effect. `mdlEffectLoadQuadA/B` declared void in effDistortParticle.c and effPolygonTrack.c now get
  their real return type. Only the affected units were recompiled; a full `psp_census.py --compile` was not run.
- `tools/psp_census.py` FIXES: in `FldFrame_RaycastFieldWorlds`, a NULL field resource takes the camera's cell. The
  PS2 read RAM at 0x228; the PSP faulted while walking a dungeon after a door.
- `port/rwsky.c`:
  - `p4_cache_budget` follows the heap between 4 and 6 MB (classroom: CPU 91.8 -> 71.3 ms with 6 MB).
  - `p4_exp_ge_time` refuses to run with the pipeline. In pipeline mode it hung a battle load.
  - The pipeline watchdog in present logs the GE state and registers.

Open problems:
1. **Battle start hangs the GE (b117).** The first battle frame's list stays in state 2 (drawing) forever. Moving its
   stall address to the list end does not help, so the GE is stuck on a command inside the list. b118 logs the last
   base/vaddr/iaddr/prim/vtype/jump/call/signal/texture words (`TRACE watchdog GE`) but has not caught it yet.
   Next: reproduce once with b118 for the register dump, then the same battle with `p4_pipeline=0` set before it starts.
2. **user_main killed while walking a dungeon corridor (b118).** `exprint`: Breakpoint in sceThreadManager, t8
   0x800201A2. The thread's own stack is 0x0B9C0000..0x0BAC0000, but the game runs on sp 0x0BEBxxxx. This looks like
   the kernel's stack check after something wrote over the thread's stack bounds. Bisect: run b112 (before the cache
   budget and census changes) in the same corridor. Also check what lies next to 0x0B9C0000 (heap end? pools?).
3. **profmode t + reset can hang the boot.** It happened once before `GU up`. Without the profiler, boots were fine.

### Corridor freeze, bisected (late 2026-11-06)

- b112 crashes at the same corridor spot: `FldFrame_RaycastFieldWorlds`, NULL from `MT_Scene_GetRes(fieldId)`, read at
  +0x228. So it is not caused by builds 113-119.
- Neither workaround helps: taking the camera's cell (b117/b118: thread killed) or returning 0 (b119: main thread
  spins, no exception). Both are reverted; the census FIXES table has a note.
- Lead: the heap had only **80-100 KB free** in the dungeon (`mesh cache on ... heap_free`), so the field resource was
  probably never loaded. Big port allocations on the heap:
  - `guard_clip`: 512 KB (`MAX_GUARDED` 32K x 16 bytes);
  - skin cache: ~1.6 MB;
  - mesh cache: 1.8 MB here;
  - topology cache: 192 KB;
  - `mesh_lit`: 128 KB;
  - `churn_parts`: 96 KB;
  - `guard_stamp`: 64 KB.

  Check where the game's own resource allocations come from and whether one fails before this spot. Then shrink or cap
  the port's buffers when the heap is low.

## 2026-11-07: builds 120-127 (Heaven, entrance stairs, profiler on, pipeline on)

| Build | Change | CPU ms | Frame ms |
|---|---|---|---|
| b120 | profiler sections: per-task table (`hwtask`, port `kwlnTaskRunList`), mfrust/mslot/mcopy/altm/ablack/acover/geskin/sector | 87.6 | 87.7 |
| b121 | `p4flip` waits on an event flag instead of polling every 200 us (`p4_flip_event`) | 68.3 (off: 87.8) | 83.4 (off: 87.9) |
| b122 | cover ranking only with `p4_cover_stats`/`p4_exp_ge_time`/census | 65.3 | 83.4 |
| b123 | `vertex_alpha_min` only when the mesh bounds are unknown | 65.1 | 83.4 |
| b124 | bone matrices written directly, slot reuse (`p4_bone_direct`) | 63.4 (off: 64.4) | 83.4 |
| b127 | display list written through the cache, written back at each stall move (`p4_list_cached`, `--wrap=sceGeListUpdateStallAddr`) | 57.4 (off: 65.5) | 83.4 |

Facts measured:
- `thinfo` RunClocks over 21 s (b120): user_main 87%, p4flip 12% (the 200 us poll), ~3500 preemptions a second.
- libpspgu's `sceGuStart` writes the list through the uncached alias. In Heaven, under GE load, an uncached store costs ~124 cycles,
  a cached one ~18 (b125 `hwprof store`). This made every GE word expensive (`sceGuBoneMatrix` ~1600 cycles).
- b126 had 2D glitches in the save menu: the real `sceGuGetMemory` moves the stall before the caller fills the block, so with a
  cached list the vertices stayed in the cache. b127: with a cached list, `__wrap_sceGuGetMemory` never moves the stall.
- GE (serial `p4_exp_ge_time`, b120): atomics 53 ms, 2D 0.9 ms, ptank 0.08. The second TEST_1 passes cost ~16 ms of GE
  (`p4_gs_test=0`: 36.8 ms). 2D is not a GE cost in Heaven.
- `p4_exp_scissor=1` (no fill): frame 66.9 ms = 4 vblanks. Heaven is now held at 5 vblanks by GE fill: CPU 57 ms + GE tail.
- b127 per frame (18.6 Mcycles): mesh 7.8M (mhit 3.4, msetup 1.5 of which muvscan 0.7, mgdraw 1.8), geskin 2.4M
  (sbones 0.53, sguard 0.56, smat 0.45, 153 batches), game tasks outside the renderer: mainDraw self ~2.7M, `draw scene(linkOt)` ~2.9M.
- memit is 0 in Heaven: every mesh is a cache hit. A VFPU memit would not help here.

Next:
1. GE fill to reach 4 vblanks: the second TEST_1 passes (~16 ms GE). Candidates: skinned batches always do two passes
   (`draw_alpha_known = 0`); bound their alpha like meshes.
2. Check b127 in the room and at the station (GE-bound before; the cached list also shortens their CPU).
3. `muvscan` (0.7M): keep the UV shift with the cache entry instead of scanning every vertex each frame.
4. `draw scene(linkOt)` and mainDraw self (~5.6M): game code; look at what they do per frame.
- b127 battle crash: not the cached list. The old battle probe in the 256-frame report (rwsky.c, "TRACE unit") read
  `unit + 0xec` through 0x099b0001 (passed the range test, odd address): Address load in user_main. b128 checks alignment
  and range of every pointer the probe follows.
- b129: GE-skinned batches carry their vertex alpha bounds (x material alpha), so draw_tested skips the second TEST_1 pass
  when every pixel passes (`p4_skin_alpha`). Heaven: frame 83.4 -> 66.7 ms (15 FPS, 4 vblanks), CPU 57.5 -> 56.4 ms,
  ge_wait 25.9 -> 10.3 ms; on/off arms repeated. Screenshot diff on vs off (1520 px) is the same size as on vs on again
  (1717 px, animation): no visible change.

### Corridor freeze near treasure chests (b130-b131, 2026-11-07)
- b130 logged the "missing" keys: fc20, f030, e440, d850, ... one 0xBF0 frame apart: not resource ids but the low half
  of stack addresses. The game then died with `thinfo` Status KILLED, StackFree 0 (Breakpoint in sceThreadManager,
  ExitStatus 0x800201A2), sp below the 1 MB stack at 0x0BDC0000: a stack overflow.
- The stack was dumped with a tiny PRX (build/stackdump/dump.c, `ldstart host0:/dump/dump.prx` writes
  host0:/stack.bin; PSPLink's peek refuses addresses above 0x0A000000). The loop, one level per 0xBF0 bytes:
  RaycastFieldWorlds (camera fallback) -> RwFrameGetLTM(camera frame) -> frame sync (p4r_00394c70 -> p4r_00393900)
  -> ... -> RaycastFieldObjects -> RpClumpForAllAtomics -> ... -> RaycastFieldWorlds with a stack address as key.
- b131: the fallback reads the camera frame's cached LTM (RwFrame + 0x50) instead of RwFrameGetLTM. Not yet verified.
- **Root cause (b132):** b131 froze the same way without RwFrameGetLTM. The return address saved for
  `jal RpClumpForAllAtomics` was 0x245f14, the first instruction of RaycastFieldWorlds: RaycastFieldObjects ended in that
  call with no return. Its loop `for (k = 0; (entry = D_007E8020.objects[k]) != NULL; k++)` reads a table declared
  `u8 *objects[1]`; GCC took k > 0 as undefined, ran the body once and let the function fall through into
  RaycastFieldWorlds with stale registers (a stack address as the key) -> RaycastFieldObjects -> ... until the stack
  overflowed. Census FIXES: `objects[]` (flexible). The camera-cell fallback and its log stay; with real keys they
  should not fire.
- A scan of the b132 binary for functions that end in a call (fall through after it): only libc noreturn calls and one
  reasm label are left.

### Shuffle Time cards (b133-b136)
- Shuffle Time showed no cards: 0 atomics and ~36 RxPipelineExecute calls a frame on non-atomic objects, all skipped.
  They come from p4r_00410420 (RwIm3DTransform on the rwIm3DPool: numElements, elements, stride 36, then the stash) and
  p4r_00410520 (RwIm3DRender*: the stash, primType, indices, numIndices; numVerts 0 because the skipped transform
  pipeline would fill it). b136 keeps the pool's vertices at the transform call and draws them at the render call with the
  mesh state (camera, ltm, blend, TEST_1, current texture). `p4_im3d`. Six cards visible; Im3D now draws in every scene.

### Game draw code (b137, Heaven stairs, profiler on)
- `p4_uv_memo`: the whole-number UV shift kept per UV array, recomputed when 8 sampled coordinates change. muvscan
  753 -> 325 kcycles, CPU 58.6 -> 57.7 ms (A/B/A). Frame stays 66.7 ms (4 vblanks).
- sdkOtFlushBuckets hooks (census FIXES, `hwot` lines, self = without renderer sections): mainDraw's own ~3 Mcycles are
  spread thin: movie native passes (type 5) 0.36M, mdlApplyDrawState 0.30M, cmpPartyPanelDraw 0.30M, the clear
  pass (type 8) 0.28M, calendarDrawBackdrop 0.18M, ... no single big item.
- The next target is `draw scene(linkOt)` = resrcMngRenderTask: ~3.0 Mcycles (~9 ms) of its own, building the
  ordering table. Not broken down yet.
- b138/b140 (Heaven stairs): draw scene(linkOt) = resrcMngRenderTask 3.1 Mcycles, of which mdlQueueModelForDraw 3.0M
  (9 models). Inside mdlAdvanceModel (census FIXES hooks, p4_sec): animation layers (mdlAdvanceLayer) 1.48M for 11
  layers (~134k each), anim control 0.19M, frame/attach/motion/effects < 0.05M. ~1.4M of mdlQueueModelForDraw is
  still outside these hooks. qsort, attach matrices, queue nodes, effects: < 0.1M each.
- b141 (Heaven stairs), inside mdlAdvanceLayer (1.46M): RpHAnimHierarchyUpdateMatrices (func_00397c40) 0.64M for 9
  hierarchies (~70k each), RtAnimInterpolatorAddAnimTime (func_003d5bc0, keyframe interpolation) 0.52M for 29 calls,
  mdlUpdateHierarchyPose (look-at, 1 call) 0.25M, SetCurrentTime 0.06M; blend/SetCurrentAnim not called here.
  Candidates for the VFPU: the matrix update (quat -> matrix, parent multiply per bone) and the interpolation.
- b142: RpHAnimHierarchyUpdateMatrices in C (p4_hanim_update, `p4_hanim_c`, off by default). Check mode
  (`p4_hanim_check`, original then C on the same hierarchy, every 8th call): 288 checks, 0 differing floats. A/B: kmat
  628k vs 631k, no gain. 363 bones a frame, flags 0x3000 (modelling + LTM of every bone frame): ~1750 cycles a bone,
  ~3 data misses a bone writing the bone matrix and the frame's two matrices while the GE reads memory. Memory-bound:
  a VFPU version would not help. Not pursued.
- b143 guard loop by chunk (`p4_guard_loop2`, no change: Heaven's guarded meshes are strips) and chunk size as a switch
  (`p4_guard_chunk`): CPU 57.1 ms (32), 56.6 (16), 56.1 (8; gclass 0.41M, gloop 0.49M, gsub 0.43M). Default now 8.
  Next: box classification on the VFPU (it grows with smaller chunks).
- b144 chunk boxes on the VFPU (`p4_guard_box_vfpu`, check `p4_guard_box_check`): 149523 boxes compared with the FPU,
  0 different classes. gclass 0.48M -> 0.34M, CPU 56.5 -> 56.1 ms (A/B/A). ~760 boxes a frame, still ~445 cycles a
  box (store/load of the results, per-box asm entry): a whole-array asm loop writing the classes would halve it.

### Dojima's room reaches 30 FPS (b144-b145, 2026-11-07)
- b144, profiler on: frame 36.2 ms, CPU 36.0, no GE wait (b110 this morning: 44.5 ms).
- b145 (UV memo 1024 slots, 256 hit only 74 of 140 a frame here), **profiler off**: frame 33.43 ms = 29.9 FPS,
  CPU 31.9 ms, GE wait 1.5 ms; UV memo off: 33.46 ms, CPU 32.3. The margin to 2 vblanks is ~1.4 ms.
- Profile of the room (b144, profiler on, kcycles): mhit 2560 (155 draws), mainDraw self 1930, msetup 1230 (mkey 450,
  muvscan 460), mgdraw 1135 (gclass 486), geskin 1110 (sguard 321 for 3 characters), 2D 660.

### Title screen (b145-b146)
- The title cycles: normal phases 17.7 ms (CPU 3, GE wait 15); the heavy phase (logo with glows, silhouettes) 83-107 ms,
  CPU 27, GE wait 57-79: GE-bound. Serial GE (p4_exp_ge_time): atomics ~32 ms + 2D ~36 ms a frame.
- 2D census (`p4_probe_2d`, b146): ~18-20 screens of 2D fill a frame: untextured blend 58 (Cs*Ad + Cd) 3-4.6 screens,
  untextured alpha-only writes (mask 00ffffff) 3-6, untextured blend 44 ~2, 128x128 blend 58 1.3-2, 512x512 8-bit
  palettized textures (cheap format) 3-4 full screens.
- 3D: geometry a1a8910 (3 vertices, untextured, test 50003) ~7 ms of GE when drawn (43/256 frames, one phase); the
  silhouette characters (v1100-1400, 14-34 draws) 0.4-0.9 ms each averaged. Hiding a1a8910 in a phase without it
  changed nothing; not yet caught in its phase.
- Screenshots (psp_probe_ab --shot) sometimes catch a frame still being drawn (orange sky, no HUD, a light-blue band):
  not what the screen shows.

### Frameskip (b147)
- The game advances a fixed 1/30 s a frame (iGpffff8040 = 0x3D088889, never written): below 30 FPS everything slows.
- `p4_frameskip` (now on by default), `p4_frameskip_max` 2: when the game is more than half a tick behind real time, the
  next frame runs its logic and draws nothing (camera begin/clear, 2D, atomics, sectors, Im3D return at once; present
  does not wait or flip). Heaven: off 15 FPS shown, 50% speed; on (max 2) 11-14 FPS shown, ~98% speed (29.3 ticks/s);
  max 1: 12 FPS at 80%. A skipped frame still costs ~17-20 ms (logic and animation). Dojima's room (30 FPS) does not
  skip. Next: skip the bone-matrix work on skipped frames (~3-4 ms each) if nothing in the logic reads it.
- Sound will need full game speed; frameskip keeps the game clock near real time.

### Save screen (b149-b150)
- "Load which file?" ran ~7 FPS (146 ms a frame), GE idle: mcProc 17 Mcycles a frame. port/memcard.c made both save
  directories again (mkdir + stat on the memory stick) on every card call, and the game probes the card about once a
  frame (sceMc2GetInfoAsync 251 per 256 frames). b150 remembers the directories: mcProc 0.15-0.4 Mcycles, the screen
  is smooth (user). Card calls are counted in the log (`memcard per 256 frames`).
- b149: the frameskip target is always 2 vblanks (30 Hz); b148 took 1 vblank where the field noise pass was off and
  ran the game at ~37 frames a second.
- b148: on skipped frames the bone matrices are not rebuilt (func_00397c40 in the port, original as p4r_00397c40;
  `p4_frameskip_bones`). Not measured on its own yet.


### Movie integration and release b161 (2026-11-07)
- `port/video.c`: shared hardware AVC + PCM player; `video_game.c` replaces the movie scheduler task.
  Movies are converted from the player's ISO by `tools/psp_video.py`. Always copy the entire VIDEO directory.
- The renderer drains the GE and drops rebuildable mesh/skin caches before a full-screen movie; after it,
  it restores the framebuffer, dirties GE state and resets frameskip debt. Normal task destruction resumes the event.
- Crucial fixes: one decode call can return multiple pictures; always drain the decoder on skip as well as EOF.
  Keep MPEG firmware initialized across movies (repeated teardown/reinit could hang); delete per-movie contexts.
- Link MPEG first (the custom NAL import precedes its SDK stubs). Validate import ranges: overlapping library
  ranges corrupted sceGeEdramGetAddr before boot. The release keeps the bone NaN repair without profiling wrappers.
- Build/package: `python tools/psp_release.py` (PSPDEV as above). Output `build/release_b161/UPDATE/PSP/GAME/P4`:
  EBOOT.PBP, p4data.bin, p4str.bin, and all 45 .p4v/.pcm files (~408 MiB). The full psp_dist directory is also updated.
- Release b161 tested on PSP through the exact PRX embedded in the EBOOT: consecutive opening movies and
  P4CT001 (new-game TV advertisement), sound, skipping, and return to the event. Validation logs under
  build/release_b161/validation. More detail: docs/video_resources.md.
- Avoid PSPLink scrshot during AVC playback; it hung the decoder in earlier probes. Do not savemem VRAM
  directly to host0 either: usbhostfs cannot reliably write from VRAM. RAM ring-buffer snapshots are usable.

### SAS audio feasibility (2026-11-07)
- Standalone `port/sas_probe.c`, build with `python tools/psp_sas_probe.py`; details in `docs/sas_audio.md`.
- Real PSP, 333 MHz, 1024-sample blocks, PCM/VAG, 0/8/16/32 voices, room reverb off/on.
- Recommended candidate: SAS + `sceAudioOutput2`. Two complete runs: ~0.11-0.12 ms main-CPU work per
  33.333 ms game tick after subtracting the measured sampling thread. Conservative non-monitor CPU
  bounds over sampled 33.334 ms windows: max 0.873 ms then 0.963 ms. Includes output; no idle-baseline subtraction.
- Ordinary audio channel: ~0.44 ms/tick average, but conservative peak bounds reached 1.301 ms.
- Raw logs, manifests and JSON: `build/sas_probe/experiments/`. Active voices stayed active; all cases passed.
- Not integrated with game CRI calls, original banks, streaming or AVC transitions yet. The 1 ms result covers
  steady synthetic playback only. No custom ME kernel code was needed for this probe.
- Keep PC USB server alive between runs; use PSPLink `reset`. Commands to the queue use LF; initial USB
  handshake timeout -7 was resolved by restarting PSPLink on the console after the PC server was ready.

### Public preparation utility (2026-11-08)
- `tools/psp_prepare.py`: Windows GUI and CLI; select a retail USA ISO and output folder to prepare a full P4 installation.
- Extracts all five containers, builds the file index, prepares all audio and movies, reconstructs build-specific retail data and installs the matching EBOOT.
- Build-specific numerical recipe reproduces b164 p4data/p4str exactly without including their original contents in the public package.
- Whole-ISO identity, exclusive output lock, resumable archive/conversion checks and final SHA256 verification; writes installation.json only on success.
- `tools/psp_prepare_release.py --freeze` creates an offline Windows x64 package with Python/Tk, PyAV, FFmpeg, vgmstream and precompiled VAG encoder.
- User/developer instructions: `docs/prepare_game.md`. Package generation is local; no GitHub publication is performed.
- Host verification: 3 preparation tests, 3 audio tests and 5 movie tests passed. Frozen runtime was checked against the real ISO with Python removed from PATH.
- Full clean-install preparation and gameplay coverage remain separate from the small frozen converter smoke test. Review bundled third-party corresponding-source requirements before publication.

## 2026-11-09: open rendering gaps (found in PPSSPP, b165 work)

- **MatFX is not implemented.** `port/rwsky.c` has no RpMatFX support: environment maps and dual-texture materials draw
  their base colour only. Seen: Heaven's treasure chests are flat yellow blocks. Needs the MatFX plugin data per
  material (effect type, env-map texture, coefficient, frame) and a second GE pass or a sphere-map UV pass.
- **Heaven minimap is empty.** The dark frame at the bottom left draws; its content does not, and the player's green
  arrow is at the top left. `p4_probe_2d` = 2 logs single small 2D draws (box, raster, UV, alpha, z test).
- **Heaven's orange fog is not visible.** The game draws a full-screen grid of 80x64 additive untextured tiles
  (RGBA 172 49 0 122, blend 0x48, 54 draws a frame). The TV-world noise/vignette pass (`FldNoiseUpdate`) is still off.
- **Render to texture (camera type 5) is new in b165**: 128x128x32 in event f007_002, 512x256x16 (drawn at 256x128)
  in battle skills. Not yet checked by eye.
- **Rise's navigator portrait** is missing in battle; it does not use a camera texture (bust-up `b34` loads).
- **Destination-alpha effects are the largest visual gap.** On the PSP the frame buffer's alpha byte is the stencil:
  one value per draw, no interpolation. Seen against PCSX2 (2026-11-09): the battle-entry swirl draws its noise
  opaque instead of only where the mask is; the TV-world haze (`FldNoiseUpdate`, still off) is a centre-weighted
  ring mask written to destination alpha and read by blend 0x54. A shared approach: keep the mask geometry with its
  vertex alpha and redraw it with the pass that reads destination alpha.
- **Battle floor** shows bright rainbow stripes; PCSX2 shows a dark glossy floor with reflections (likely MatFX).
- **Megidolaon**: the falling object is still missing after the colour-pack fixes (b165).
- **Field Shadow** symbol: a red ring and small purple bits instead of the dark smoke blob.
- **Minimap corridors**: PCSX2 draws white explored corridors; the port draws only icons.
- **Destination alpha test (GS DATE) is ignored everywhere.** Field shadows (`FldShadow_DrawBlobPacket`,
  k_shadow.c:648) render the silhouette into a 128x128 camera texture, then gradient bands with ATEST_1 0x3C803
  (DATE on, DATM 1: only where the silhouette left alpha bit 7 set), and project the texture onto the floor. Without
  DATE the bands fill the whole square. The user, comparing with PCSX2: on the battle arena the rainbow should show
  only inside the characters' shadows; in the port it covers the floor. Characters draw with ATEST_1 0x7F06B (DATE on
  too). Plan: emulate alpha bit 7 with the stencil (it is the frame buffer's alpha byte on the PSP): draws without
  DATE write stencil 0xFF where they pass (REPLACE), DATE draws test it (EQUAL, ref DATM << 7, mask 0x80); first
  inside camera-texture passes only, since the screen's stencil already carries the pixel-mask emulation.
- Tools from b165: `p4_probe_3d` (census of 3D draws per material; read `census3d` from memory, the log drops
  lines), `p4_hide_material` (one material not drawn), `p4_probe_2d` 2/3 (single 2D draws, camera begins, clears).
  The rainbow floor did not disappear when any of 48 materials was hidden: it may come from a 49th entry or a path
  not yet counted.
- **A character's head is missing** in a battle view (b165 session); not yet looked at.

## Plan after 2026-11-09 (b165, PPSSPP session with the user comparing against PCSX2) (historical; open items not re-checked, see perf_status.md)

State at the end of the day. Fixed and seen working in PPSSPP: infinite recursion through the p4r_00397c40 alias (black
movie, p4_audio crash), 30 FPS cap everywhere (title ran at 60 game frames/s), movies visible (sceDmacMemcpy), minimap
(GS ATEST FB_ONLY in 2D; s64 vec2 parameters kept 64-bit for the player arrow; corridors now show). Built but not
confirmed by eye: render to texture, DATE inside camera textures, generic ALPHA_1 with FIX = 0, VU0 colour packs and
particle trails, pexew, the ring stack temporaries. Nothing from today is on a real PSP yet.
`build/psp_dist` holds a build with the 3D census widened to 160 entries; the one installed in PPSSPP has 48.

In order:
1. **Battle arena: rainbow only inside the characters' shadows.** In the original the floor is dark and the
   characters' shadows show the rainbow. Install the current build, enter a battle, run `python tools/psp_census3d.py`
   and `... hide DIR` to find the rainbow draw and the shadow draws (look for DATE, pixel mask and blends 0x52/54/58).
   Then design one model of the screen's alpha on the stencil that serves both the existing pixel-mask/destination-
   alpha blends (GE scale, 1.0 = 255) and GS DATE (bit 7 of the PS2 alpha, 0x80 = 1.0).
2. **Destination-alpha effects** with that model: battle-entry swirl (noise should show only through the mask), the
   TV-world haze (`FldNoiseUpdate`, off since b44; ring mask with interpolated alpha: redraw the ring geometry with the
   pass that reads Ad), the Shadow enemy's dark smoke.
3. **Megidolaon's falling object** still missing: list the effect types in skill 18A, check `P4_EE_ASM` traps that
   remain (20 in src) and effects drawn through paths without colour.
4. **MatFX** (environment maps, dual textures): chests (blue in PCSX2, flat yellow here), the battle floor's reflections.
   The census prints the words after each material; find the MatFX plugin offset first.
5. **A character's head missing** in a battle view; **Rise's navigator portrait** in battle (bust-up b34 loads).
6. **Cost on the PSP**: the FB_ONLY double pass runs ~74 times a frame (mostly HUD); skip it when the texture's alpha
   range is known to be all above or all below the reference (swizzle head alpha_min/max). Measure VU0-in-C work
   (`vu0 per frame`, peak ~9.5k instructions) and the P4_VU0_COUNT site lookup with the profiler on hardware.
7. Rebuild the auto package (`P4-PSP-auto-b164`): it still has the psp_link alias bug.

### Device session after b168 (2026-11-10)
- Builds: `build/psplink_b168` (profile), b168 release in `build/psp_dist`. Scenes: Heaven stairs, station, the street.
- Animation: `kinterp` (slerp/lerp callback: RpHAnim `p4r_00415bc0`, game `mdlKeyFrameInterpolateCopy`) against `kadd`
  (whole AddAnimTime). If kinterp is most of kadd and its memory stall is small, move the slerp to the VFPU with an FPU
  check; if the stall dominates (as kmat in b142), leave it.
- Mesh key: `mkey`/`muvscan` with `p4_lock_memo` 1 and 0 (b167); `cache audit` must stay at 0 stale.
- Cache budget: `mesh cache budget` lines should hold 6 MB now (b167 counts the untouched heap).
- Guard: in PPSSPP the street needs ~100 boxes and ~140 vertices a frame, 0 triangles cut; check gclass/gloop there.

### Device session 2026-11-10 (b168-b173)
- **The profile builds since b159 were slow because of the bone check**: `hanim_scan_after` (p4_bone_check) re-scanned
  every recent bone matrix after every task when the profiler is on. Dungeon corridor (b163) 108 -> 33 ms a frame,
  Heaven stairs 117 -> 50 ms. b169: `p4_bone_check` 0 by default. Release builds never ran the scan.
- Heaven stairs b169: 50 ms (3 vblanks), CPU ~49 ms. `kinterp` ~55% of `kadd`, stall ~10%. `p4_lock_memo` 1 vs 0:
  mkey 421k -> 144k, CPU 50.3 -> 49.0 ms. Cache budget holds 6 MB. `cache audit` checks only entries built while it is
  on (switch it on before entering a scene); not yet run on a fresh scene.
- b169 hung going back to the title (movie P4CTOP2): `sceMpegCreate` never returned (user_main waiting in sceMpeg), the
  MPEG work area was at 0x0a31b040 for the first time (heap after Heaven). b170: the player's buffers live in the 4 MB
  volatile block (0x08400000, sceKernelVolatileMemTryLock; heap fallback). Boot movie plays from it; **the return to
  the title after a long session is not yet tested**.
- Classroom (many students), b170-b172: 50 ms (3 vblanks), CPU ~44-49 ms. Per drawn tick: mesh ~5.3M cycles (mhit 3.1,
  mgdraw 2.0), geskin 3.7M, qmodel+alayer 6.2M (kadd ~1.1M, kmat ~1.5M), draw scene 3.8M.
- b171: skinned guard spheres against planes built once per skin (`p4_skin_guard_fast`, 116k checks 0 diffs): sguard
  936k -> 270k, CPU -1.6 ms. skin-to-bone x pose on the VFPU (`p4_skin_vfpu`, 440k checks 0 diffs): ~-0.3 ms, on by
  default from b172. Counters `anim ...` (updates, unseen, far_due, depth bins of drawn skins).
- b172: RpHAnim compressed-key interpolate (p4r_00415bc0) in C with the slerp on the VFPU (`p4_interp_fast`, off): max
  quaternion diff 2.4e-4, but kinterp only -6%: 98% of nodes take the lerp branch (dot >= 0.999); the cost is reading
  the keys (cache misses). Not worth it. AddAnimTime is now overridden in all builds (func_003d5bc0).
- b172-b173: far animation LOD (`p4_anim_far` 1, `p4_anim_far_depth` 500 clip w, step 2, `p4_anim_far_kadd` 1: the
  AddAnimTime step is kept and added on the next due tick): classroom qmodel 4.09M -> 3.10M, alayer 3.51M -> 2.47M,
  CPU -1 ms (rest became GE wait). User: students move fine. `p4_anim_cull` (not drawn for 2 ticks) stays off: nobody
  is off screen in the classroom; try it on the street.
- Next: classroom needs ~15 ms less CPU for 2 vblanks: break down `draw scene` (resrcMngRenderTask) and the mesh hit
  path (mhit ~20k cycles a mesh); street (`gclass`/`gloop`, p4_anim_cull); title return with the volatile video.

### Movie hang traced to the SAS player (b174-b176, 2026-11-10)
- The hang is not sceMpegCreate and not the buffer address: b174 reused one SceMpeg (`mpeg reused`, `init au` ok) and
  still hung on the first Media Engine call after 604 s of play (EPC 0x0B9C6F1C inside sceMpeg).
- b176 with `ms0:/PSP/GAME/P4/NOSOUND` (SAS never started): a movie after 2560 s (42 min) of play decoded fine. With
  sound, movies hung after ~10 min (b169, b172, b174); short gaps always worked, sound or not.
- Next (b177): a NOREVERB switch (`__sceSasRevType` off), SAS error codes in the log, NOSOUND removed; play 10+ min,
  then a movie. If it still hangs: key off and stop SAS completely before a movie.
- Far LOD: b175 holds only characters whose model has not moved for 8 ticks (`moving_kept` counter); the b174 Heaven
  doubling (body lagging its shadow) is gone.
