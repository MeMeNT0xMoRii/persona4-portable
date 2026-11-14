# PSP performance: current status and plan (updated 2026-11-14, build 249)

This is the **current** plan for the PSP performance work. Older plans live in
[next_steps_b112.md](next_steps_b112.md) and [hw_test_plan_b97.md](hw_test_plan_b97.md). Use them as a record of
measured facts and earlier decisions, not as work still to do.

Goal from the user: a stable 30 FPS on a real PSP at **333 MHz**, with effects adapted rather than removed. Every
number below is from a real PSP at 333 MHz with the profiler on (`profmode t`) unless it says otherwise. The device
numbers from 2026-11-10 (b170-b176) were taken at 423 MHz by mistake. They are about 27% too optimistic: do not
compare them with 333 MHz numbers.

## b249 audio/video handoff investigation (2026-11-14)

b246 was still running with `NOSOUND`. The isolated audio/video control passed
660 seconds at 333 MHz, but the actual save-load -> gameplay -> title return hung
in b247 (acknowledged pause) and b248 (also cold MPEG sessions). b249 additionally
stops the audio workers and unloads owned SASCORE/AVCODEC before video, then
restarts audio from saved ADPCM block positions. Loaded banks/queued commands
stay in memory. Four short isolated movie transitions passed. The full-game
Heaven save -> 10+ minutes of sound -> title return **passed** in b249: all 3270
movie frames, result 0, audio resumed and continued mixing without new errors
in the measured post-movie window. User confirmed it works. b249 is running via
PSPLink with sound enabled; the XMB EBOOT was not replaced. Full results and reproduction:
[audio_video_hang.md](audio_video_hang.md).

## b245 in Heaven: far objects are the cost; persistent replay gains nothing (2026-11-14)

Heaven, 10th Paradise, standing at the entrance, 4 party members, b245, **profiler off** (PSPLink was restarted, no
`profmode t`: cpu numbers are ~10 ms lower than profiled ones and there are no hwprof rows), 4 windows an arm, first
dropped. Raw: `build/psplink_b245/experiments/h_*`, screenshots `${P4_HOST}/b245/h_*.png`.

| Arm | cpu ms | GE wait ms | frame ms | FPS |
|---|---|---|---|---|
| base | 54.23 / 54.46 / 54.66 | 12.4 / 12.2 / 12.1 | 66.7 | 15.0 |
| `p4_mesh_persist` 1 | 54.59 / 55.03 | 12.1 / 11.7 | 66.7 | 15.0 |
| `p4_skin_lod_depth` 3000 (2 of 7 skins simplified to 50%) | 54.06 | 12.6 | 66.7 | 15.0 |
| `p4_cull_far` 4000, min 500 vertices (16 atomics a frame dropped) | **43.57** | 6.4 | **50.0** | **20.0** |
| `p4_cull_far` 2000, min 100 vertices (35 atomics) | 40.42 | 6.9 | 47.4 | 21.1 |

- **Far objects:** dropping 16 far atomics is -10.7 ms CPU and one vblank step. Counters: skin draws 7 -> 6 a frame,
  skin batches 113 -> 79, bones sent 498 -> 346, mesh draws 270 -> 222. So one far skinned model with 34 batches and
  ~150 bones, plus ~13 static pieces. Picture at 4000: the three statues behind the gate (a few pixels tall) are
  gone, nothing else differs by eye. At 2000 / 100 the far stairs and garden are gone: a visible black gap, too much.
  Half of the gain is the frameskip step: at 50 ms a drawn frame carries fewer undrawn ticks.
- **`p4_mesh_persist`:** check mode 19,460 comparisons per 256 frames (76 calls a frame would replay), 0 differences;
  misses only "model" (~1.4 a frame). Timing: +0.4 ms CPU, no gain. The validation reads about as much memory as the
  walk it replaces (a 1.3 KB entry with two copies of the state shadow, 8 slots, 8 materials). Stays 0. To be worth
  it the check must be a few cache lines: one frame-state id instead of the copies, and the shadow compared by a
  version counter.
- **`p4_skin_lod_depth`:** simplifying far skins does not move CPU (the batches and bone uploads stay) and the GE is
  not the limit here. The far skinned model needs fewer batches, not fewer triangles.
- Heaven is not vertex-bound: 270 mesh draws a frame carry 8457 vertices in all.

**b246 (deployed, at the title, profiler off):** `p4_cull_cover` N: an atomic of at least `p4_cull_min_vertices` whose
bounding sphere covers less than N / 100000 of the view is not drawn (by size on the screen, so large far backdrops
stay, unlike `p4_cull_far`). Default 0. To do in Heaven: N 10, 30, 100 against `p4_cull_far` 4000; pick the N that
drops only what is a few pixels tall; then decide the default with the user and replace dropping by a cheap stand-in
(the far skinned model drawn as one rigid batch from its root bone, or a quad).

## b246 in Heaven: cull by size on the screen (2026-11-14)

Same place, profiler off, 3 windows an arm, first dropped; `build/psplink_b246/experiments/c_*`, screenshots
`${P4_HOST}/b246/c_*.png`.

| `p4_cull_cover` | atomics dropped a frame | cpu ms | frame ms |
|---|---|---|---|
| 0 | 0 | 56.6 | 66.6 |
| 10, 30 | 0 | 56.5 | 66.7-66.9 |
| 100 | 0 in the report | 54.7 | 66.7 |
| 300 | 9 | 47.7 | **50.2** (19.9 FPS) |
| (`p4_cull_far` 4000) | 10 | 44.1 | 50.0 |

At 300 (0.3% of the view) the picture loses the three far statues behind the gate (about 14 pixels tall); the two
statues at the gate, the stairs and the garden stay. **On trial at 300 (source default from b247, poked on b246), by the user's decision after this note: Margaret
appearing from the middle of the bridge is fine, and the user is looking at other scenes. What follows was the
reason it was first withdrawn; it stays as the list of things to watch.** The far skinned model it dropped (34 batches, ~150 bones, depth ~7700, most of the gain) is Margaret, an NPC
the player walks to (user). Rules from this: no size or distance cull for skinned models, ever; for static scenery
only as a stand-in (one mesh, one material) rather than nothing, and only after other locations are looked at.
Each statue is 1180 vertices in 5 meshes (5 draws): its cost is the draws, so simplifying triangles alone gains
nothing; one draw per far statue would.

**Which changes are general and which are tuned to one place (asked by the user):** exact ones reproduce the same
output and carry a check mode (profiler fix, strip key fix, strips, guard results, `p4_mesh_persist`): their risk is
an untested path, so each needs its check mode run in more scenes (so far: Margaret dialogue, the boss battle,
Heaven entrance). Lossy ones depend on thresholds picked in one scene (`p4_cull_cover`, `p4_cull_far`,
`p4_skin_lod_depth`, `p4_anim_far`): default off until looked at in a set of scenes (dialogue, battle, Heaven, a
town street, a dungeon corridor, the classroom), and never applied to characters or things the player must see. Open: a cheap stand-in instead of nothing
(one rigid batch from the root bone, or a quad), and other locations (a large far object with few pixels is not
expected, but only Heaven was looked at).

## b246 in the Margaret dialogue: far skins at half the triangles, 78 -> 67 ms (2026-11-14)

Held dialogue, profiler off, guard results and strips on, 4 windows an arm, first dropped;
`build/psplink_b246/experiments/d_*`, screenshots `${P4_HOST}/b246/d_*.png`.

| Arm | skins simplified / full a frame | cpu ms | GE wait ms | frame ms | FPS |
|---|---|---|---|---|---|
| `p4_cull_cover` 300 (nothing dropped here) | 0 / 10 | 55.73 / 55.48 | 22.2 / 21.1 | 78.0 / 76.7 | 12.8-13.0 |
| `p4_cull_cover` 0 | 0 / 10 | 55.41 | 22.2 | 77.7 | 12.9 |
| `p4_skin_lod_depth` 900, keep 50 | 6 / 4 | 51.36 | 15.8 | **67.25** | 14.9 |
| `p4_skin_lod_depth` 600, keep 35 | 8 / 2 | 51.17 | 15.6 | 66.8 | 15.0 |

Here the GE is the limit, so fewer triangles count (in Heaven they did not): -6.4 ms of GE wait, -4.3 ms CPU, one
vblank step. Picture: at 900 / 50% the six far party members look the same as full by eye at native resolution; at
600 / 35% the hero's and Kanji's hair is visibly damaged. `p4_cull_cover` 300 drops nothing in this scene and costs
nothing. The PSP was left with `p4_skin_lod_depth` 900 poked on for the user to judge; source default still 0.
Second frame-rate step from here needs cpu + GE wait under 50 ms: about 17 ms more.

## Where to continue (handoff, 2026-11-14, b244)

**b246 is on the PSP (see the section above); start PSPLink with `profmode t` and `reset` when hwprof rows are needed.**

**State of the PSP (older note):** b244 (profiling PRX) was deployed with matching readback hashes and booted; it sits at the title
screen. The user loads the Margaret dialogue by hand and says so; no button injection, no routes.

**Defaults in b244:** `p4_guard_result` 1, `p4_skin_strip` 1, `p4_pipeline` 1; everything else new is off
(`p4_mesh_persist`, `p4_mesh_persist_check`, `p4_interp_fast`, `p4_skin_dummy`), `p4_hanim_vfpu` 1,
`p4_skin_free_wait` 1, `p4_hanim_time` 1. psp_probe_ab.py restores these values after each arm.

**The finding that orders the work:** in the dialogue 61% of the CPU's cycles are memory stall, ~44k instruction and
~47k data misses a frame, ~155 cycles each. Changes that keep the memory traffic measure nothing (C and VFPU
arithmetic, `-Os`); changes that remove a walk through code and data win (guard results -4 ms, strips -3.4 ms).
Judge a candidate by the code and data lines it stops touching.

**Next steps, in order:**

1. **`p4_mesh_persist` (b243/b244, unmeasured).** A whole draw_meshes call whose meshes all replayed recorded CALLs is
   kept between frames (inputs compared exactly) and replayed as mesh_state + one comparison + a copy of its
   BASE/CALL words. b243 in check mode: 0 replays counted, ~17 records a frame (entries overwrote each other: bad
   table index) and the report line was cut; misses were only "model" (~0.8 a frame). b244 fixes the index (mixed
   hash, 2 entries a set) and splits the report. To do in the held dialogue:
   a. `psp_probe_ab.py p_check --base 0x08804000 --samples 5 --set p4_mesh_persist=1 --set p4_mesh_persist_check=1`;
      read `mesh persist ... checks N diffs D` and the `mesh persist misses` line. Wanted: checks about 100-150 a
      frame (x256), diffs 0, records near 0 after the first report.
   b. If diffs is not 0: the `TRACE mesh persist differs` lines give header and word counts; look for state the
      comparison misses (candidates: `draw_fixed_alpha`/`draw_skin_alpha` callers, fog, the ring record being
      refreshed between record and check).
   c. A/B/A/B `p4_mesh_persist` 0 / 1 (check 0), 4 windows each; sections `mesh`, `mhit`, `msetup`, `mpers` (new) and
      the frame's imiss/dmiss. Expected a few ms of CPU; not measured.
   d. Then a moving scene (the boss battle) with check 1 before any default change. Watch for a picture fault when a
      texture animates or a material fades: those inputs are in the comparison, the check proves it.
2. **Bone RwFrame writes.** `hanim time`: the two 64-byte writes into every bone's RwFrame are roughly half of kmat
   (~5 data misses a node); apose pays the same. Write only frames something reads: frames that hold objects, their
   ancestors, and nodes read by index (mdlComputeIndexedAttachMatrix, mdlCopyIndexedMatrix, attach lookups by frame
   id). `p4_bone_census` already counts 28% dead nodes. Needs a "mark live on read" hook and a check mode that
   compares the skipped frames with what the full path would have written at the moment of a read.
3. **Longer strips.** Now 2.33 indices a triangle; the game's own strips 2.15-2.18, welded ~2.0. First measure on
   the PC what a stripifier with swaps gives on captured meshes (`p4_mesh_capture` 1, tools/meshkit_offline.c);
   continue only below ~2.1. Then offline strips through capture/override (also removes the ~0.24 s build when a
   party appears) and batches cut along strips. Strips gave -3.4 ms CPU for -22% indices; the rest is extrapolation.
4. **The 2D path:** 5-7k instruction misses for 157 calls a frame (`2d` section, 1.1-1.3 Mcycles).
5. **Strips and the battle:** one clean pass on b242. Repeat the boss battle on b244 (strips are the default now);
   `p4_skin_dummy` 1 (the 0xFF000000 word) only if it hangs again.
6. **Guard results in motion:** run the battle once with `p4_guard_result_check` 1; it is the default without that.

**Distance-based detail (direction set by the user, 2026-11-14).** The user: Heaven with 4 characters is slow too;
limit the draw distance of locations, use LOD and simplified models for what is far (the angels, flowers as
sprites): the PSP screen does not show that detail. Detail that cannot be seen may go; what is seen up close stays.
What exists and what to do, in order:

a. **Measure first, in Heaven and in the dialogue:** `p4_cover_stats` 1 ("cover top" rows with the per-atomic work
   line: screen cover, vertices, indices, GE time with `p4_exp_ge_time` 1 and `p4_pipeline` 0) and the
   `anim depth of drawn skins` bins. Wanted: a table of atomics by screen cover against vertices and GE time, to pick
   thresholds from numbers.
b. **Far skinned characters, b245 (built, not deployed, not measured):** `p4_skin_lod_depth` D (clip w of the
   character's root in the last frame, 0 off) and `p4_skin_lod_keep` K (percent of triangles, default 50). Beyond D
   the character is drawn from a second skin cache entry simplified to K% (meshkit online, built once: 0.2-0.5 s
   for a party); back to full below 0.9 D. Report line `skin lod depth ... draws simplified N full M`. In the
   dialogue the party stands at 650-1300, so D 900 puts about half on the LOD. A/B: GE wait, frame, and the picture
   at native resolution (b211: 60% is visibly damaged up close; the question is whether it shows at that distance).
   If the picture holds: offline simplification through capture/override instead of the online build.
c. **Far static scenery (the 13 statues of Heaven, 1180 vertices each):** `p4_cull_far` Z with
   `p4_cull_min_vertices` already drops whole atomics beyond a view depth: use it for the upper bound of the gain,
   then replace the drop with a simplified copy in the mesh cache (meshkit on the compact mesh at build, keyed by a
   LOD level chosen from projected size) so nothing disappears.
d. **Flowers (84 a frame, 32 vertices, 2 draws each, ~2.5 ms of GE):** the cost is per draw, not per vertex. Far
   ones as one camera-facing quad each, or merged per cluster into one draw; near ones unchanged.
e. **Animation of far characters:** `p4_anim_far` exists (update every 2nd tick beyond depth 500) but almost never
   holds at 12 FPS because bones only update on drawn ticks; revisit when the frame rate is higher.

**Do not redo (measured, no gain):** `p4_interp_fast` 2 (VFPU interpolation, exact, 0 gain), `p4_hanim_vfpu` 2
(-0.14 ms in kmat, nothing overall), port `-Os` (+5-6 ms), `p4_interp_batch`, `p4_interp_pairs`, `p4_pose_runs`,
`p4_guard_split`.

**Reading old logs:** up to b238 the hwprof column "imiss" is data misses, "dmiss" is write-backs and "mem_k" is
copz; correct from b239.

## b242: the boss battle passes with strips; strips -3.4 ms CPU in the dialogue (2026-11-14)

The user played the boss battle that hung in b227-b229, on b242 with `p4_skin_strip` 1 (poked at the title),
`p4_skin_dummy` 0: it started and ran to the end. Log of the run: 0 exceptions, 0 pool-full traces, `skin free waits`
0 in all 16 reports (that guard never fired), strips built for up to 149 batches (50,250 -> 38,966 indices, 235 ms),
0 failed checks. So the fix that was exercised is the strip address and count in the recorded CALL key (b241). One
pass, not a repeated test; the 0xFF000000 word was not needed.

Guard result cache in the same run (camera moving): 1261 hits, 204 misses, 94 builds, 50 evictions in the battle
report; 6912 hits and 0 misses per 256 frames back in the dialogue. Run without the check switch.

Held Margaret dialogue after the battle, guard result 1, 4 windows an arm, first dropped
(`build/psplink_b242/experiments/s_strip*`):

| Arm | cpu ms | GE wait ms | frame ms |
|---|---|---|---|
| strip 0 | 66.59 | 16.68 | 83.42 |
| strip 1 | 63.21 | 14.70 | 78.07 |
| strip 0 again | 66.51 | 16.72 | 83.34 |
| strip 1 again | 63.17 | 14.70 | 77.97 |

**Strips: -3.4 ms CPU, -2.0 ms GE wait, and the frame leaves the 5-vblank step for the first time (78.0 ms, 12.8 FPS).**
Against b230's base in this scene (cpu ~70-72): guard results + strips = about -8 ms CPU.
`p4_skin_strip` defaults to 1 again in the source (next build, b243); the PSP was left with it poked to 1.

## b241 measured, b242 deployed (2026-11-14)

b241 (port built `-Os`) in the held Margaret dialogue, 4 windows an arm, first dropped; raw
`build/psplink_b241/experiments/n_*`:

| Arm | cpu ms | frame kcycles | imiss | mesh kcycles | kmat kcycles |
|---|---|---|---|---|---|
| b240 `-O2`, guard result 0 (earlier, other reset) | 70.5-72.2 | 22863 | 44065 | 5720 | 1067 |
| b241 `-Os`, guard result 0 | 77.36 | 25038 | 48244 | 6100 | 1311 |
| b241, guard result 1 | 73.27 / 73.24 | 23738 | 45278 | 4666 | 1332 / 1323 |
| b241, guard result 1, `p4_hanim_vfpu` 2 | 73.24 / 73.25 | 23698 | 45309 | 4691 | 1277 / 1280 |

- **`-Os` for the port is rejected:** ~5-6 ms more CPU and more instruction misses (48.2k against 44.1k), although the
  text is 46 KB smaller. (Across a reset, so a different moment of the dialogue; the difference is far outside the
  ~1 ms drift.) `P4_PORT_OPT` stays in psp_hw_debug.py, default `-O2`.
- **Guard result cache again -4.1 ms CPU** (mesh 6100 -> 4666 kcycles). **Default 1 since b242** at the user's request;
  moving scenes still to be run with `p4_guard_result_check` 1.
- **`p4_hanim_vfpu` 2** (now fusing the real callback): kmat -45 kcycles (~0.14 ms), whole CPU unchanged. Stays 1.

b242: `-O2`, `p4_guard_result` 1 by default, prof_store_scratch 4096 -> 1024 words (the image was 344 bytes over).
Deployed, readback hashes match, at the title with `p4_skin_strip` poked to 1 for the battle test below.

## b240 in the held dialogue: the CPU is memory-bound; b241 (2026-11-14)

First profile with the corrected columns (b240, Margaret dialogue, 333 MHz, per drawn frame, raw
`build/psplink_b240/experiments/m_*`): **frame 22.86 Mcycles, of them 14.05 M memory stall (61%)**; 44,065 instruction
misses and 46,577 data misses a frame, so ~155 cycles a miss on average (the boot walk with the GE idle: ~55).

| Section | cpu_k | mem_k | imiss | dmiss |
|---|---|---|---|---|
| frame | 22863 | 14053 | 44065 | 46577 |
| atomic | 9473 | 5524 | 20133 | 18882 |
| mesh (181 calls) | 5720 | 3515 | 14841 | 9686 |
| geskin | 2378 | 1133 | 1664 | 7027 |
| 2d (157 calls) | 1269 | 847 | 7212 | 1220 |
| qmodel | 4413 | 2533 | 4227 | 8600 |
| alayer | 3138 | 1656 | 2137 | 6358 |
| kmat | 1067 | 629 | 512 | 2724 |
| apose (2 calls) | 822 | 507 | 708 | 1975 |
| kadd | 1374 | 565 | 1065 | 1742 |

- `hanim time` (per 256 frames, 1989 updates, 138,400 nodes, every node through a callback): matrices 76.2 Mcycles
  (550 a node), NaN scan 20.9 M (151 a node), prologue 4.9 M, waiting frames 4.5 M; the rest of kmat, roughly 150 M
  (~1100 a node, the line was cut in the log), is the two 64-byte writes into each bone's RwFrame: ~5 data misses a
  node. The frame writes, not the matrices, are the largest part of the hierarchy update.
- `p4_interp_fast` 2 (VFPU node): 219,839 checks against retail, 0 differences, max difference 0. A/B/A/B: cpu
  72.16 / 72.05 / 72.03 / 72.00 ms, kinterp 765 / 762 / 768 / 760 kcycles. **No gain**: stays 0. In the scene a node
  costs ~1000 cycles against 423-578 warm at boot: the difference is memory stall, not arithmetic.
- `p4_hanim_vfpu` 2 fused 0 nodes in b240: the callback the characters carry is neither RpHAnim's `func_003954b0` nor
  the scale hook but the compressed-key scheme's `p4r_00415ad0` (read from a live interpolator: +0x3c = 0x0889513c),
  the same conversion. b241 accepts it. Not measured yet.
- `p4_hanim_check` with these hierarchies reports differences in the base build too (max 5.9e-3 with vfpu 1): it
  runs retail and then the C path on the same frames. Do not read it as a fault of the new path without a base run.

**Conclusion for animations and meshes.** They are slow because of cache misses (instruction and data about equally),
not because of arithmetic: the rewrites that kept the same memory traffic (b234-b238, b239 VFPU) measured nothing, and
the one that removed traffic (b236 guard results) won. Next, by expected size: (1) instruction misses: 44k a frame;
the renderer's hot paths carry their diagnostics inline (draw_skinned_ge 8.9 KB, skin_geometry 13.7 KB,
hanim_update_now 5 KB against a 16 KB instruction cache): b241 is the first probe, the port compiled `-Os`
(`P4_PORT_OPT=-Os`, text -46 KB); then cold code out of line and hot functions placed together; (2) bone RwFrame
writes (~0.6 Mcycles in kmat, more in apose): write only frames something reads (frames with objects, their
ancestors, indexed attach reads), behind a switch with a check; (3) the 2D path's 7.2k instruction misses for 157
calls.

**b241 (deployed, at the title; not measured in a scene):** port built `-Os`; `p4_hanim_vfpu` 2 accepts
`p4r_00415ad0`; strip leads for the battle hang, see below.

**Strips and the battle hang, two faults found by reading and one lead (none confirmed on the device yet):**
- the recorded skin CALL key did not hold the strip's address and count: a geometry rebuilt at the same batch,
  vertex and list addresses replayed a record pointing at a freed strip. b241 adds both to the key.
- a skin cache entry used in the previous frame could be freed while the pipelined GE was still drawing that frame:
  b241 waits for the GE first (`p4_skin_free_wait` 1, counter `skin free waits`).
- PS2RC's skinned sub-lists end with the word 0xFF000000 before RET ("the dummy word the GE's skinning needs");
  PSPSDK's sceGuDrawArray has none. `p4_skin_dummy` 1 emits it after each GE-skinned PRIM (recorded lists included);
  default 0. Test: battle start with `p4_skin_strip` 1, first with dummy 0 (does the key fix cure it), then 1.

## b239/b240: what the animation arithmetic costs by itself; profiler columns corrected (2026-11-14)

**The cache-miss columns of every profile up to b238 were mislabelled.** The table read `prof_regs[1 + index]` with
indexes 4, 13, 14, which are `copz`, `d_miss` and `d_writeback` of `PspDebugProfilerRegs`, and printed them as
"mem_k", "imiss" and "dmiss". So in older logs "imiss" is data misses, "dmiss" is dirty lines written back, and
instruction misses were never read. b239 reads `memory`, `i_miss`, `d_miss` (`p4_prof_index`, pokeable). Proof, boot
benchmark `hwbench` on the PSP (b239 and b240, same numbers): reading 4096 never-touched lines moves word 14 by
4101-4122 and word 13 by 10-40; 4096 reads of 64 warm lines move word 14 by 64.

**Measured at boot (333 MHz, warm unless said, cycles):**

| What | Cycles |
|---|---|
| a data-cache read miss (walk of 4096 cold lines: 60-65 a line against 10 warm) | ~55 |
| compressed-key interpolate, retail `p4r_00415bc0`, a node | 578 (two cold key lines: 720) |
| the same on the VFPU (`p4_interp_fast` 2, new) | 423 (cold 593); results equal to 1e-6 |
| one bone: FPU quaternion to matrix + `multiply_vfpu` | 186 |
| one bone fused on the VFPU (`p4_hanim_vfpu` 2, new) | 134; results equal to 1e-6 |

**What it means.** In the dialogue profile a node costs ~990 cycles in `kinterp` and ~1630 in `kmat`. The arithmetic
is 578-720 of the first and only 186 of the second: about 1450 cycles a bone of the hierarchy update are spent
around the matrix, not in it (frame writes, the scale-hook callback chain, the NaN scan, the parent LTM, waiting
frames), and that is why the C, cache and loop rewrites of b234-b238 measured nothing. Data misses are ~55 cycles
each: at ~49k a frame that is ~2.7 Mcycles of 22.9, real but not the main cost.

**New in b239/b240 (all default off or diagnostic):**
- `p4_interp_fast` 2: the lerp node on the VFPU; slerp nodes and NaN take retail. Check: `p4_interp_check` 1.
- `p4_hanim_vfpu` 2: fused bone (also inside `p4_pose_run`), and the game's `mdlHierarchyScaleHook` recognised: for
  every node but its two special keys it is the saved stock conversion, so those nodes skip the two indirect calls.
  b238 found every pose node going through that hook ("custom callbacks"). Check: `p4_hanim_check` 1 (note: with the
  hook, check mode runs the hook twice on the scaled node; pre-existing).
- `p4_hanim_time` 1 (profiling build): `hanim time` line, kcycles per report: waiting frames interpolated, NaN scan,
  prologue, matrices, frame writes, and the ordinary pose runs.

**Not measured yet (needs the held dialogue):** the `hanim time` split, instruction misses per section, and the A/B of
the two switches. The boot numbers bound their gain at ~0.3 ms (interpolation) and ~0.1 ms plus the hook calls
(bones); the larger target is whatever the split shows around the matrices.

## b237/b238: compact ordinary runs inside the look-at pose (2026-11-14, no measured win)

`p4_pose_runs=0` (experimental, off at startup) consumes contiguous non-head nodes in a small native loop based
on `p4_hanim_update`. The original function still handles node 0x1389 and the directional-blend tail once per call.
The helper shares the original parent-stack pointers and stops before the head, so descendants inherit the head's
actual output. It preserves modelling/LTM writes, frame private flags and PUSH/POP/both-bit node modes.
b237 restricted eligibility to the stock key-apply callback. The device check in the user-held scene recorded
640 + 642 pose calls but **zero eligible runs/nodes/checks**: no correctness or speed conclusion is possible.
Raw: `build/psplink_b237/experiments/dialog_pose_check.*`. No non-finite head rotations were reported.

b238 forwards the captured current apply callback into the compact loop. Custom callbacks execute exactly once
per node, in the same order; stock conversion remains inline. This preserves scale-hook mutations instead of
bypassing them. A custom-node counter distinguishes this path in the device log.

`p4_pose_runs_check=1`, together with runs enabled, checks ordinary local and hierarchy matrices against the stock
key callback plus scalar `RwMatrixMultiply`, and checks transformed frame LTMs. For a custom callback, its
single original output supplies the reference local matrix; only the subsequent multiply is independently checked.
Check mode never invokes a custom callback twice. It records exact differing floats,
maximum absolute error, flag differences and matrices exceeding `1e-5 * (1 + max(abs(a), abs(b)))`; on a failure it
uses the reference matrix. Pads are excluded. This is a per-node arithmetic check, **not a whole-function state
replay or an independent check of run boundaries/parent-stack traversal**. The helper never receives the controller;
the original head code updates it once. The full controller/whole-hierarchy comparison proposed earlier remains
unimplemented; do not describe the local check as that stronger validation. Diagnostics are separate noinline
functions to keep their stack/register use out of the hot loop.

b237 and b238 profiling/release builds compile/link, pass all 19 import-library checks and resolve all function
pointers. b238 census compilation also passes every regime. Source-manifest hashes and all three SHA256 device
readbacks match. No button injection.

**b238 device checks, held Margaret dialogue, 333 MHz:** 99645 ordinary hierarchy matrices checked, all using
custom callbacks, 0 matrices outside tolerance, 0 flag differences. 358449 exactly differing components, maximum
absolute error ~0.000488 from VFPU vs scalar multiplication. No bad look rotation before/after, no corrective
redos in these reports. Counter windows straddle the script's restore, so the final 35581 checks are in the extra
`dialog_pose_check_counters.log`; the probe log itself contains 64064. This scene reports hierarchy flags 0x3000,
so the 0x4000 transformed-LTM branch is not covered. These are local arithmetic checks with the limits above.
Spot-checked check/A/B/A2 screenshots retain the dialogue and party; this is not full pixel/state equivalence.

**b238 A/B/A:** four 128-frame windows per arm, first discarded, three retained. Both check controls and guard
result cache stay 0 throughout timing; batch/pair interpolation and subdivision experiments are also off.

| Arm | CPU ms | GE wait ms | Frame ms | Pose section ms |
|---|---|---|---|---|
| A: runs 0 | 69.761 | 13.580 | 83.453 | 2.609 |
| B: runs 1 | 70.359 | 12.960 | 83.427 | 2.663 |
| A again: runs 0 | 70.231 | 13.012 | 83.380 | 2.626 |

Pose costs are the mean of the three retained `apose cpu_k` values / 333 (333 MHz). B costs ~0.04..0.05 ms more
in that section; whole CPU costs ~0.13..0.60 ms more than the two original arms, with ~0.47 ms baseline drift.
**No measured speedup. Keep `p4_pose_runs=0`, `p4_pose_runs_check=0`**, including startup defaults. The compact loop
experiment is retained behind its switch, not enabled as an optimization. The unchanged head block and callback
cost need separate attribution before another pose rewrite; do not assume that ordinary-loop overhead explains
the whole ~2.6 ms section. The stronger whole-function/controller comparison remains necessary for any later
change to head/controller behavior. Broader moving-camera/battle validation of guard-result caching is still open.

Raw JSON/logs and screenshots: `build/psplink_b238/experiments/dialog_pose_{check,A,B,A2}.*`, plus the extra
counter log; device screenshots in `${P4_HOST}/b238/`.
PSP left on b238 in the held dialogue. Live readbacks confirm pose runs/check, guard result, interpolation batch
and interpolation pairs all 0. Guard-result startup default remains 0 as before; the b236 live override did not
survive the later resets. No automatic commit.

## b236: cached guard results, CPU win in the held dialogue (2026-11-13)

Implemented `p4_guard_result` and `p4_guard_result_check`. A persistent result contains the kept u16 index list
and cut vertices of a guarded compact MeshCache entry. Replays skip box/vertex classification, clipping, index
construction, frame-pool copies and their writebacks. Material, texture, lights and alpha modulation remain live;
raw alpha bounds of each immutable cached output are learned on its first replay. Empty results are cacheable.

The key compares the source slot/key/build stamp, source buffers, counts, primitive/vertex layout, all 12 affine
model components, the full existing 18-word camera key, near and eight classification/clipping settings exactly.
No camera-position tolerance. Admission requires the same key in two different frames; moving-camera misses
only update pending metadata, without allocating/copying result buffers. 128 entries in 4-way sets, 256 KiB payload
budget plus about 30 KiB of metadata. Entries read by a GE list cannot be evicted until `ge_finished` reaches their
last-use sequence (`ge_queued + 1` when emitted). Movie transition clears the cache after the existing GE sync.

**Correctness on the PSP:** check mode reruns the original path and compares kept counts/indices and cut counts/
vertex bytes; only the original result is drawn. Held dialogue: 6912 exact comparisons, 0 differences. Changing
`p4_guard_chunk` 8 -> 16 rebuilt 27 records, evicted 8 old records and passed 5097 more comparisons, 0 differences,
0 allocation refusals and no pool-full/exception reports. The script restored chunk 8. Spot-checked screenshots
of the cached path retain the scene. These runs do not measure a moving battle's speed.

**Device A/B/A/B, same held Margaret dialogue, 333 MHz, profiler on:** four 128-frame windows per timing arm,
first discarded, three retained. Both failed animation experiments and subdivision remain off.

| Arm | CPU ms | GE wait ms | Frame ms |
|---|---|---|---|
| A: guard result 0 | 70.078 | 13.292 | 83.531 |
| B: guard result 1 | 66.652 | 16.769 | 83.529 |
| A again: guard result 0 | 68.861 | 14.469 | 83.492 |
| B again: guard result 1 | 66.831 | 16.452 | 83.385 |

The original arm drifted by ~1.2 ms, so report **about 2.0..3.4 ms CPU saved**, not one precise whole-frame gain.
The guarded-mesh section itself falls from ~1731k cycles / 5.20 ms to ~303k / 0.91 ms for the same 27 calls.
Steady cache: 6912 hits per 256 frames (27/frame), no misses/builds; initial chunk-8 payload only 11008 bytes
(~10.8 KiB). Other work changes too, so the ~4.3 ms section saving must not be equated with the total CPU saving.
Frame rate is still ~12 FPS: saved CPU time becomes more GE/presentation wait in this profiled, pipelined scene.

Both profiling and release builds compile/link and pass import checks. PRX/data/strings deployed with SHA256
readbacks. Raw: `build/psplink_b236/experiments/dialog_guard_{check,A,B,A2,keycheck,B2}.*`; screenshots in
`${P4_HOST}/b236/`. No route or button injection. PSP left in the held dialogue with guard result **1**,
check **0**, chunk **8**. The source/PRX startup default remains **0** for this experimental switch; a reset needs
an explicit switch-on again. Broader moving-camera/battle measurements remain to be done before changing the
startup default. Geometry-result cache checking is separate from pixel comparison of live material/state changes.

Next CPU target: simplify the special hierarchy-pose loop, then assess guard-result hit rate/cost in moving scenes.

## b235: decoded key-pair cache, experimental and OFF (2026-11-13)

Implemented `p4_interp_pairs` independently of `p4_interp_batch`. Known compressed-key callbacks can reuse the
decoded quaternion/translation pair and its quaternion sign/linear-branch decision. Every use validates both key
addresses and the entire 18-byte value payload of each key (time, quaternion, translation). Current time and custom
translation scale/offset remain live. Division/order of interpolation operations are retained. Slerp and aliasing
fall back to the original callback; mutable uncompressed keys keep their original path.

The cache uses dense per-interpolator node arrays, up to 64 interpolators and 256 KiB of node payload plus 1280
bytes of set metadata. LRU eviction bounds payload use; Destroy releases an interpolator's array. Allocation/count
failure retains the original path when the independent batch experiment is off. All records are CPU-only.

**Correctness:** `p4_interp_pairs_check=1` copies the input keys before each cached operation, runs the original
callback on the copies and compares the output (b234 checker, tolerance 1e-5). Startup/transitions: 16555 checks;
held Margaret dialogue: 419876 checks. No differences; reported maximum difference 0. In-place mutation counter
was 0 in these runs: invalidation for changed values is implemented but that particular condition was not observed
in the hardware samples. Startup/transition timings are not a benchmark, and check-mode timings are excluded.

**Controlled device A/B/A, same held dialogue, 333 MHz, profiler on, batch experiment off:** four 128-frame
windows per arm, first discarded, three retained. Per-node profiler sections retained in both paths.

| Arm | CPU ms | GE wait ms | Frame ms |
|---|---|---|---|
| A: pair cache 0 | 72.536 | 10.904 | 83.542 |
| B: pair cache 1, check 0 | 73.249 | 10.166 | 83.534 |
| A again: pair cache 0 | 72.383 | 10.847 | 83.333 |

**Rejected as a default optimization:** about 0.79 ms slower than the mean of the two original arms, ~12 FPS
either way. The steady experimental report has 206659 hits / 12031 misses (~94.5% hits), 85260 payload bytes
(~83.3 KiB), 0 evictions and 0 allocation refusals. Hit rate is not enough to justify it. Comparable 746-call
`kinterp` windows: original 739k cycles / 935 instruction misses / 583 data misses; cached 995k / 2330 / 1294.
The extra ~256k cycles (~0.77 ms at 333 MHz) in interpolation accounts for the frame CPU regression. The larger
working set and validation/management cost outweigh the avoided decode work in this scene; this does not prove
that every possible key-cache design would regress.

Both profiling and release builds compile/link and pass import validation. PRX/data/strings deployed with matching
SHA256 readbacks. Raw results: `build/psplink_b235/experiments/{pairs_startup_check,dialog_pairs_check,
dialog_pairs_A,dialog_pairs_B,dialog_pairs_A2}.*`; screenshots `${P4_HOST}/b235/`. PSP left on b235 in
the user's held dialogue, pair cache/check and the b234 batch experiment all off. No routes/button injections.

The ready guarded-result cache proposed here was completed in b236 (above). Next animation target is the special
hierarchy-pose path. Repeatedly decoding keys has been tested as a cache target and did not yield a CPU win with
this implementation.

## b234: specialized interpolation batches, experimental and OFF (2026-11-13)

Implemented `p4_interp_batch` (0 original, 1 experimental) in `lazy_interpolate`. It selects the known callback
once per interpolator, hoists the time/stride/custom data and inlines copy or scalar lerp in separate loops for
compressed and uncompressed keys. Slerp, nonfinite dot products and unknown callbacks retain the original
functions. Known scalar interpolation semantics were checked against Izanagi commit
`bfb12b6896955b626cc45254c92e8b76789ae147`, `overrides/003955a0.c` and `overrides/00415bc0.c`.
The existing model-copy hook writes only six floats: it deliberately leaves translation.z untouched.
Uncompressed interpolation must preserve the original in-place sign change of key2.

`p4_interp_batch_check=1` compares every specialized node with its original callback, using copies of the inputs
saved before the new path. Compares the output and, for uncompressed interpolation, key2 mutations, with relative
tolerance 1e-5 (identical bit patterns accepted directly). In startup/scene transitions: 140002 packed-node checks,
0 differences. In the held Margaret dialogue: 268106 more checks (copy and packed), 0 output differences, max
reported difference 0. No uncompressed callback nodes were observed; that branch is not hardware-validated yet.
Check-mode times are excluded from the A/B. Raw: `batch_check.*`, `dialog_batch_check.*`.

**Controlled device A/B/A, same held Margaret dialogue, 333 MHz, profiler on:** four 128-frame windows per arm,
first discarded, three retained. Per-node profiler begin/end calls remain in the new loop, so the comparison does
not remove hundreds of profiler sections only in the experimental arm.

| Arm | CPU ms | GE wait ms | Frame ms |
|---|---|---|---|
| A: batch 0 | 71.176 | 12.110 | 83.414 |
| B: batch 1, check 0 | 71.354 | 12.053 | 83.542 |
| A again: batch 0 | 71.167 | 12.178 | 83.500 |

No speedup: the specialized path is about **0.18 ms slower** in this scene. The original CPU windows are
71.122..71.208 ms; experimental 71.256..71.430 ms. Frame remains ~12 FPS. Do not enable it by default on these
results. This experiment does not establish the payoff of caching decoded key pairs or combining pose passes;
those changes were not implemented in b234.

Both profiling and release builds compile/link and pass import validation. Diagnostic b234 deployed with SHA256
readbacks of PRX/data/strings. PSP left in the user's dialogue with batch/check both 0; no routes or button
injections were used. Raw JSON/logs in `build/psplink_b234/experiments/dialog_batch_{A,B,A2}.*`; screenshots in
`${P4_HOST}/b234/`. The startup comparison includes transitions and is not a timing benchmark.

The decoded-pair experiment proposed here was completed in b235 (above) and also stays off. A C translation and
fewer indirect calls alone have been measured and did not help this scene.

## b232-b233: cached triangle subdivision, experimental and OFF (2026-11-13)

Completed the unfinished b232 prototype in `port/rwsky.c`. `p4_guard_split` is the maximum model-space edge
length (0 off); `p4_guard_split_grow` bounds triangle growth (default 8, clamped 2..64). Cache builds bisect the
longest oversized edge, share midpoints, interpolate attributes and unroll strips with their original winding.
The result is capped at 32 K vertices and 65535 indices per draw; allocation, growth or cache-budget failure
retains the original mesh. Both settings participate in the cache key. Cache hits use the split copy's primitive,
vertex/index counts and data. The inherited prototype did not publish cache metadata on successful subdivision;
this is fixed. A side bitset avoids expanding the already large MeshCache BSS array.

Subdivision never authorizes a draw by itself: `p4_guard_small=2` enables the existing per-draw geometric proof
for chunks/whole meshes. Unsafe chunks still use CPU clipping as the camera moves. Splitting runs at cache build
even if the first camera sees the mesh inside the band, so later movement can use it too.

**Device results, b232, stationary Margaret dialogue, 333 MHz, profiler on:** four 128-frame windows per arm,
first discarded, same held text box as the earlier measurements. Raw JSON/logs and screenshot paths:
`build/psplink_b232/experiments/dialog_{base,split2048}.*`.

| Mode | CPU ms | GE wait ms | Frame ms |
|---|---|---|---|
| subdivision off, small off | 71.793 | 11.540 | 83.484 |
| split 2048, growth 8, small 2 | 74.869 | 8.437 | 83.411 |

This subdivision setting is **about 3.1 ms slower on the CPU**. It built 20 subdivided copies (3663 -> 8726
triangles; 3 growth-limit refusals; about 23 ms total build work in the sampled report). No chunks or whole meshes
passed the small-triangle shortcut. Base guard work actually clips **2 triangles/frame** (`gclip` ~17-18k cycles,
~0.05 ms), while classifying and submitting **27 meshes/frame**. The old 5.3 ms was the entire guarded-mesh path,
not the time spent intersecting triangles with the guard planes. Uniform subdivision adds chunk checks without
removing those mesh submissions. The scene therefore does not support enabling this optimization by default.

A more aggressive b232 arm (512, growth 64) built 27 copies, 5728 -> 101382 triangles, and triggered
`TRACE guard pool full`. It was stopped by restoring the controls; its frame times and picture are **invalid as
a performance/correctness comparison** (`dialog_split512.*`). The initial moving battle arms (`split_base`,
`split128`) cross transitions and actions and are also not controlled timing comparisons.

**b233 fix:** guard pool preflight reserves indices for nonculled chunks and worst-case cut vertices for partial
chunks only. Previously it reserved worst-case cut vertices for every triangle, including all culled/inside chunks,
which falsely rejected large subdivided meshes. Keeps the original bounds checks and changes no clipping rule.
Both b232 and b233 compile/link and import checks pass. b233 is deployed with verified SHA256 readbacks.
The automatic route was stopped and removed at the user's request; do not inject buttons or replay routes
during further testing.

**b233 hardware follow-up:** `split512_poolcheck` (512, growth 64, small 2) spans the battle/menu-to-dialogue
transition and is a stress check, not a controlled timing comparison. No `TRACE guard pool full` or exceptions
in its log. The final dialogue screenshot retains the scene (spot check). It generated 20 copies / 94054 output
triangles in the first report and 7 / 7328 in the next; persistent cache 4.31 MB, free heap 14.67 MB. The final
dialogue windows cost 110.115, 108.889 and 108.589 ms CPU: finer subdivision is also far too expensive here.
After restoration to split 0 / growth 8 / small 0, `dialog_restored` measured 74.041 ms CPU, 9.393 ms GE wait,
83.542 ms frame (three kept windows). This is a different text/animation state from the b232 comparison above.
Raw logs/JSON: `build/psplink_b233/experiments/{split512_poolcheck,dialog_restored}.*`; screenshots alongside the
deployment in `${P4_HOST}/b233/`. PSP left on b233 in the user's dialogue with subdivision off.

Next for this scene: reuse guarded draw results while camera/model/geometry are unchanged, or reduce per-mesh
classification/submission overhead. Uniform subdivision is implemented for further experiments, not a proven win.

## b228-b230 on the PSP (2026-11-13, Margaret dialogue, profiled)

- **b228, animation lookups:** AddAnimTime found an interpolator's state by scanning 128 80-byte records, and the
  lazy slots by scanning 128 32-byte ones. Both now scan 512-byte pointer arrays. Against b212: kadd 6.5 -> 3.8-4.6
  ms, kset 2.7 -> 1.2, actrl 4.9 -> 2.9; frame profile 23.6 -> 22.4 Mcycles (cpu ~72.8 -> ~69 with strips off).
- **Strips off again (b230):** with `p4_skin_strip` 1, b228 crashed in free() while loading the boss battle and b229
  hung at the battle's start (flicker, black scene). With it off (poked at boot) the battle starts. b229 doubled the
  strip buffer to its true worst case (6n+6) and saw no overlong strip, so the cause is elsewhere. Open.
- **b230, `p4_rwmul_vfpu`:** RwMatrixMultiply on the VFPU. On/off/on/off in the dialogue gave no difference
  (apose 2.5 ms either way, cpu within the ~1 ms drift), so it defaults to 0.
- **Replay profile (b230):** an instance replay costs ~4k cycles (rplay 2.1 ms for 181 tries, lights 0.1, guard
  test 0.5). The flowers are not the mesh cost.
- **Where the drawn tick goes now (profiled, per frame):** atomic 28.3 ms (mesh 16.8: hits 7.9 of which guarded
  compact 5.3, setup 2.8, replays 2.1; GE skin 6.8; the rest ~4.7 per-atomic overhead for 191 atomics); qmodel 12.7
  (alayer 9.1, of which kadd 3.8; look-at pose apose 2.5 for 2 calls, ~2.4k cycles a node); kmat 3.0.

**Next, by size:** (1) the guarded compact meshes (5.3 ms, 27 a frame): keep the kept-index list and cut vertices
across frames while camera and model are unchanged (dialogue). Uniform cache-build subdivision was tried in
b232-b233 (above) and stays off; (2) the per-atomic overhead outside the meshes (~4.7 ms, 191 atomics: ~24k cycles
each); (3) the look-at pose: replace its per-node loop (not the multiply) with p4_hanim_update's form for the nodes
that are not the head; (4) find why strips break the battle start.

## b213/b225 on the PSP: fast skin strips, on by default since b227 (2026-11-13, Margaret dialogue)

b213 picks each strip's start from min-heaps of unused-neighbour counts instead of rescanning every triangle, and
runs b210's whole-mesh diagnosis only with `p4_strip_why`. b225, switch poked in the dialogue: 149 batches stripped
in **238 ms** for the whole party (b212: 6.7-7.9 s). `p4_strip_check` 1 compared 157 strips with b209's linear
search: 0 differ, 0 failed checks. Frame 83.4 either way; cpu 71.7 -> 70.1 (base again 71.6), ge_wait 11.8 -> 13.2.
b212's GE timer had strips at -3.7 ms of GE. `p4_skin_strip` defaults to 1 since b227. Cost: about +0.24 s once,
when a party's skin caches are built.

## Fixed on the way (visual, see visual_issues.md)

b219: the headless party members (undeclared `__sqrt` in `effMiscQuatAngle`). b225-b226: the stripes over the attack
target select (battle panel frame scale left unset by retail).

## Device session 2026-11-13 evening (b212, Margaret dialogue, real PSP, 333 MHz, `profmode t`)

Same dialogue as b201, text box held open; arms through `psp_probe_ab.py`, 3 kept 128-frame windows each (spread
within an arm <= 0.5 ms), base measured 4 times through the session (cpu 72.8-72.9 every time). Raw: build/psplink_b212/
experiments/{m_,g_}*.json/.log, screenshots in ${P4_HOST}/b212/.

Pipelined (normal mode). The frame is pinned at 83.4 ms (5 vblanks) in every arm; cpu + ge_wait = 83.3 always.

| Arm | cpu ms | ge_wait ms | vs base cpu |
|---|---|---|---|
| base (x4) | 72.8 | 10.5 | - |
| `p4_skin_rigid` 16 | 74.3 | 8.9 | +1.5 |
| `p4_skin_rigid` 64 | 73.2 | 10.1 | +0.4 |
| `p4_skin_palette` 4 | 75.2 | 8.1 | +2.4 |
| rigid 16 + palette 4 | 76.8 | 6.6 | +4.0 |
| `p4_skin_strip` 1 | 71.0 (repeat 69.7, windows 79.2/81.6 ms frame) | 12.3 | **-1.8** |
| palette 4 + strip | 73.4 | 9.8 | +0.6 |
| rigid 16 + palette 4 + strip | 75.1 | 8.3 | +2.3 |
| `p4_skin_pack` 0 | 73.2 | 10.2 | +0.4 (pack 1 saves 0.4) |
| `p4_anim_lazy` 0 / `p4_anim_native` 0 / `p4_anim_keymap` 0 / lazy 0 + native 0 | 73.1 / 72.8 / 72.9 / 73.0 | ~10.5 | <= +0.3 |

GE timer (`p4_pipeline` 0, `p4_exp_ge_time` 1; frame 117 ms serial). Atomics GE ms a frame: base 42.0 (again 42.0),
rigid 16 40.1, palette 4 **36.7**, strip **38.3**, pack 0 42.3, palette 4 + strip **34.5**, all three 34.8. 2D 8.2,
clear 0. The timer itself: 3 us an empty piece x 350 pieces = ~1.05 ms a frame. Flowers (a75a950, 84 a frame, 2 draws
each): 2.70 ms gross, ~2.45 ms net: real GE time, not the timer.

Skin cache builds on the device: normal 10 builds 0.27-0.33 s (longest 38-48 ms); **with strip 6.7-7.9 s for 10
builds, longest 1.1-1.5 s (2495 vertices)**: the greedy stripifier is ~25x the rest of the build on the PSP, a
multi-second freeze whenever a party appears. Strip cannot ship as an online build.

Pictures: all arms look the same as base by eye (diffs are idle animation/blinks); `p4_skin_pack` 1 lights the
characters like 0 (no blown-out white): the GE normalises the skinned normal on hardware too.

**What it means.** This scene is CPU-bound: the frame profile is 23.5 M cycles = 70.6 ms with the profiler (~62
without, b201), GE ~50 ms. GE savings do not show in frame time; switches that add batches (rigid, palette 4) cost
more CPU than nothing. Strip is the only skin switch that saves both (GE -3.7, CPU -1.8), but only if built offline
or with a much faster stripifier. The animation switches (b202-b204) save nothing measurable here. Profile entries
(inclusive, per frame, profiled): atomic 28 ms, qmodel 17, mesh 16.4 (181 calls), alayer 10.8, mhit 7.3, geskin 6.8,
kadd 6.5, actrl 4.9, mgdraw 4.7, kmat 3.7, sdraw 3.8.

**Next (CPU first):** 1) where the 16 ms of `mesh` (181 calls) and `mhit` go; 2) `alayer`/`kadd` (17 ms together)
still large although lazy/native are on: check what they do in the drawn ticks; 3) strip through b211's offline
capture/override (no load cost), then on by default; 4) keep rigid/palette at 0/8 until CPU is under the GE.
Battle (Margaret boss on AI) swings 34-59 ms between windows: needs the command menu held open to measure.

## Plan after 2026-11-13 (agreed with the user): tried, to measure, to try next

**Where we stand (b201, Margaret dialogue, real PSP):** 83.4 ms a frame (5 vblanks, 12 FPS). CPU ~62 ms without
the profiler, GE ~50 ms (atomics 42.4 + 2D 8.1). 30 FPS needs both below 33 ms. On the GE, ~10 skinned characters
are ~20 ms and vertex-bound (~1 us a vertex, fill small); two full-screen background planes ~2.5 ms each are
fill-bound; 84 flowers a frame show ~2.5 ms but that includes the per-piece timer. On the CPU, animation was ~20 ms
in the profile (inflated by per-node profiling).

**Tried 2026-11-13, PPSSPP only (no device numbers yet; every switch is in psp_probe_ab.py):**

| Build | Switch (default) | What it does | PPSSPP result |
|---|---|---|---|
| b202 | `p4_anim_lazy` (1) | undrawn ticks move only the animation clock; walk and interpolation at the first reader | 0 diffs vs retail in ~25k checks |
| b203 | (with lazy) | material UV animation deferred, its apply skipped in undrawn ticks | 0 diffs |
| b204 | `p4_anim_native` (1), `p4_anim_keymap` (1) | AddAnimTime in C, key -> node map per animation | ~95% of steps native, 0 diffs, 0 map misses |
| b205 | `p4_skin_pack` (1) | skin vertices s16 positions, u16 UVs (36 -> 28 bytes) | picture same; lighting relies on the GE normalising normals |
| b206 | `p4_cover_stats` | per-atomic GE work counters; GE timer calibration | skins: indices 2.9x vertices, 6.2 weights blended for 1.1 used |
| b207 | `p4_skin_rigid` (0) | one-bone batches for rigid triangles | weight work -40..-45%, batches ~1.9x |
| b208 | `p4_skin_palette` (8), `p4_bone_census` | palette cap; dead-bone count | palette 4: weights -38..-52%, batches 2.3x; 28% of nodes dead |
| b209/b210 | `p4_skin_strip` (0) | skin batches as strips | indices -22% (2.33 a triangle); game's own strips 2.16 |
| b211 | `p4_skin_simplify`, `p4_mesh_capture`, `p4_mesh_override` (0) | meshkit: online vs offline simplification | 60%: indices -40%; online 0.2-0.5 s at load and tears hair; offline cleaner but faces break |
| b212 | (report) | skin cache build time | 15-30 ms a geometry, ~180 ms when a party appears |

**Device session (Margaret route; then battle):**

1. `p4_exp_ge_time` 1 with the pipeline off, `p4_cover_stats` 1: GE time per character next to its work line, and
   the timer's own cost ("ge time empty piece"). Decides what the GE pays for: indices, vertices or weights.
2. A/B `p4_skin_rigid` 0 / 16 / 64 and `p4_skin_palette` 8 / 4 (and both): GE wait and frame time.
   Expected: if weights matter, -3..-6 ms of GE on characters; the doubled batches cost CPU and GE per draw.
3. A/B `p4_skin_strip` 0 / 1. Expected: if the GE pays per index, about -20% of character GE time; if not, ~0.
4. A/B `p4_skin_pack` 1 / 0, and look at the characters' lighting (blown-out white = the GE does not normalise).
   Expected: a few ms of GE if vertex fetch matters.
5. A/B `p4_anim_lazy` 1 / 0 and `p4_anim_native` 1 / 0 (CPU; `alayer`, `kadd`, `kinterp`). Expected: several ms
   of CPU; the target is animation at most ~3 ms.
6. Read "skin cache builds" (hitch when characters appear) and the flowers' net GE time.

**Next, depending on the device answers:**

- GE pays per index (step 3 helps): long strips. A swap-capable stripifier (meshoptimizer, MIT) offline through the
  b211 capture/override path, strip lists instead of triangle lists; batches cut along strips. Expected ~1.6-1.8
  indices a triangle instead of 2.33 (another -25..-30% of indices); without swaps the measured ceiling was ~2.0.
  Cheaper first step: meshes that fit 8 bones drawn with the game's own strips (2.16, perfect order).
- Weights matter (step 2 helps): keep the better of rigid / palette 4 on by default, maybe both.
- Characters still over budget: offline simplification of far characters only, at 80-90%, faces and hair locked
  (by material or texture name) or UV distortion in the cost; judged by eye at native resolution.
- Animation still large on the CPU: skip dead nodes (28%) where the game never reads them by index; slerp on the
  VFPU (`p4_interp_fast`); a fused per-node pass.
- Visible hitch when characters appear: a prebuilt PSP skin format through capture/override (whole batches).
- Level geometry (Heaven needs a new route, the old one is broken): split only band-crossing big triangles at cache
  build (lets `p4_guard_small` work), merge small static instances, CLUT8 for 32-bit backgrounds.

**Set aside, with reasons:** rewriting the animation system like P3P (no evidence P3P's is lighter; same engine
family); dropping hierarchy nodes from the data (animations address nodes by index); merging the 84 flowers (draw
order among blended things; wait for the calibrated timer); online simplification (load-time cost, worse picture).

## b205: P3P-shaped skin vertices (2026-11-13, PPSSPP only)

P3P's captured battle frame (build/p3p_render_research/render_findings.md) sends s16 positions and u16 UVs on all
3D draws; our GE skin batches sent float positions and UVs. `p4_skin_pack` (default 1, part of the skin cache key):
s16 bind positions over the geometry's box, the box folded into each bone matrix sent (rotation x s, translation +
rotation . centre); u16 UVs over the batch's range through TEXSCALE/TEXOFFSET (whole part of the offset dropped,
skins wrap). The skinned normal grows by s; the GE divides lighting dot products by the normal's length (PPSSPP's
probe-based software GE), so light is unchanged. 8 bones lit: 36 -> 28 bytes a vertex, unlit 32 -> 24.
PPSSPP battle route: `p4_skin_diff` 0 differing of ~150k checks (s16 within tolerance); screenshots packed and
float look the same; the 132 s poke to 0 rebuilt 7 geometries. **Device A/B needed** (dialogue: GE ~42 ms atomics,
characters ~20 ms of it): GE wait and frame time with `p4_skin_pack` 1 / 0. Watch on the device: lighting on
characters (the normal-length claim is PPSSPP's, not measured here) and texture swim on large UV ranges.

## b212: skin cache build time (2026-11-13, PPSSPP clock, Margaret route)

"skin cache builds N took T us, longest M us": 15-30 ms a geometry (longest 30 ms, 3098 vertices), up to ~180 ms
when a party appears (10 builds). A one-off hitch at a scene change, not frame time. A prebuilt PSP format (b211's
capture/override carrying whole batches) would remove it; worth it only if the device shows a visible hitch.

## b211: meshkit, online vs offline simplification of skinned meshes (2026-11-13, PPSSPP, Margaret route)

`port/meshkit.h`: one half-edge-collapse simplifier (quadrics; seam, open-edge and cross-bone collapses refused; no
vertex made or moved, so UVs, normals and weights stay valid) for both ways. Host check `tools/test_meshkit.c`
(grid with seam and border, sphere; red control): PASS. Online: `p4_skin_simplify` N% at skin cache build (many
collapses a pass). Offline: `p4_mesh_capture` 1 writes capture/<key>.bin next to the EBOOT;
`tools/meshkit_offline.c` (one collapse a pass) writes override/<key>.bin (index lists only, 239 KB for 33
geometries); `p4_mesh_override` 1 takes them. All default 0; capture/override files are game data and stay local.

Margaret, 60% kept (55,063 -> 32,947 triangles):
- per character: idx -40%, uniq vertices -32%, wslots -30% (same for both ways).
- online cost at load: 0.2-0.5 s of PSP clock per batch of characters (PPSSPP), e.g. 17,075 triangles in 511 ms;
  offline: 2.4 s on the PC for all 33, nothing at load.
- picture: both visibly damaged at 60%. Online tears hair and heads (Marie's hair, holes); offline is cleaner but
  faces still break (UV-mapped features on few triangles). The quadric knows positions only.
Next if this route is wanted: offline only; keep 80-90%; protect faces/hair (lock by material or texture name) or
add UV distortion to the cost; judge by eye at native resolution.

## b209: skin batches as triangle strips (2026-11-13, PPSSPP, Margaret route)

`p4_skin_strip` 1 (default 0; in the cache key): each GE skin batch also as one strip (greedy, starting at the
triangle with the fewest unused neighbours; strips joined by zero-area triangles with the parity kept), unrolled and
compared with the list at build (0 failed checks in every run), the list kept for the CPU paths. Result: indices
-22% (e.g. 50,250 -> 38,966 a build window; ~2.33 a triangle instead of 3), picture the same as off (1474 px vs
1474 px of idle-animation outline against the reference). Worth it only if the device shows the GE paying per
index (then -22% of that); the weight work (b207/b208, -40..-50%) is the bigger lever either way.
b210 asked why the strips are short (indices a triangle, per mesh before batching, Margaret characters): the game's
own PS2 strips 2.15-2.18, one greedy strip over the whole mesh ~2.39, the same with same-position vertices welded
~2.0 (UV seams cost ~8%), batch strips ~2.33. So P4's character meshes are short strips by nature (hair, fingers,
cloth pieces); a perfect stripifier would reach ~2.0-2.16, only 7-10% below now. Starting from the game's own strip
order (tried as mode 2) did worse than the greedy start and was removed.

## b208: dead bones counted; palette cap (2026-11-13, PPSSPP, Margaret route)

`p4_bone_census` 1: per drawn skinned hierarchy, nodes with skin weights, nodes whose RwFrame holds objects, live
(those plus ancestors) and dead. Margaret: 9 hierarchies, 731 nodes, 446 weighted, 34 attached only, 529 live,
**202 dead (28%)** (per hierarchy 16-28 of 66-91). Dead nodes still get interpolated and multiplied every drawn tick.
Skipping them would save ~28% of the per-node animation work (rough: ~1000 cycles a node for the FPU interpolation,
b172), but the game may read a dead node's matrix by index (mdlComputeIndexedAttachMatrix effects,
mdlCopyIndexedMatrix): such reads must mark the node live first. Not done; worth it only if the device shows the
per-node animation still large after b202-b204.

`p4_skin_palette` N (default 8; in the cache key): at most N bones a batch. N 4: wslots -38..-52% a character,
batches 2.3x (357 vs 157 a frame), bones sent +27%; picture fine. Same trade as `p4_skin_rigid` (b207: -40..-45%,
batches ~1.9x); the device decides which (or both).

## b207: rigid skin batches; what instance replays send (2026-11-13, PPSSPP)

Route `tools/routes/margaret.txt` reaches the b197/b201 Margaret dialogue hands-off in ~212 s (PPSSPP).
`p4_skin_rigid` N (default 0; in the skin cache key): a mesh's triangles whose corners all hang from one bone alone go
to a one-bone batch (GU_WEIGHTS(1)) when that bone has at least N of them; opaque meshes only (regroup). Margaret, N 16,
per character: wslots -40..-45% (e.g. 39,543 -> 22,824), batches ~16 -> ~30 a character, bones sent +16%, uniq +7%
(vertices shared with other batches are duplicated). Screenshot at 222 s vs off: only 1-px outlines of the far
characters differ (idle animation phase), no artefacts. Device A/B decides: GE (weights) against CPU and GE per-draw
cost of twice the batches. Try N 16 and 64.

`p4_replay_census` 1: per instance replay, CALLs followed. Margaret: 21 words, 2 draws, CLUT loads 5%, texture
flushes 24% of replays. The 84 flowers a frame (b201's 9f01a50, f028_002_flower, 32 vertices) do not reload their
CLUT; their 2 draws are the game's TEST_1 pair (ATEST GEQUAL, FB_ONLY with depth writes). Merging them would change
the draw order among blended things; wait for b206's timer calibration on the PSP before touching them.

## b206: what the GE is asked to do, per atomic (2026-11-13, PPSSPP counts)

With the cover table on (`p4_cover_stats`, or `p4_exp_ge_time`), each "cover top" row gets a "work" line: idx (vertices
fetched = index count), uniq (distinct vertices; for static meshes an upper bound: max index + 1), wslots (weights x
idx), and for skins batches, nonzero weights vs weights blended, rigid triangles (all corners on one bone). CALL and
instance replays are parsed from their words (b201's rows said "vertices 0" for them). The cover table moved to the
heap (30 KB of .bss back). `p4_exp_ge_time` now times an empty piece once a frame and counts pieces: b201's 9f01a50
(32 vertices, ~80 pieces a frame, 2.5 ms) may be mostly the timer itself.

PPSSPP battle, skinned characters per frame: idx ~2.9x uniq (e.g. 8208 / 2873); nonzero weights ~1.1 a vertex but
~6.2 blended (3172 of 17,919: 82% of the blend work on zero weights); rigid triangles 40-62%. Device question for the
next `p4_exp_ge_time` run (dialogue, pipeline off): does a character's ge_us follow idx, uniq or wslots? idx -> the GE
re-transforms per index (strips or non-indexed runs help); wslots -> rigid triangles drawn as plain meshes with the bone
as world matrix, and batches sorted by weight count.

## b202: animation advanced in time only in undrawn ticks (2026-11-13, PPSSPP only)

Goal from the user: animation CPU from ~20 ms (b197 profile, inflated by the per-node profiler trampoline) to ~3 ms.
Reading of the retail code: SetCurrentTime is not a replay from 0 (it adds the difference or walks back), so the
waste is that every tick walks the key stream and interpolates every node, ~1.5 of ~2.5 ticks a drawn frame for
nothing. `p4_anim_lazy` (default 1): inside mdlAdvanceLayer of an undrawn tick, AddAnimTime of the layer's own
interpolators (hierarchy currentAnim, rebase input) moves only `currentTime` when retail would fire no callback and
not loop; walk and interpolation wait for the first reader (bone update, look-at, blend/rebase outside a deferral,
RtAnim SubAnimTime/SetCurrentAnim/Blend/Destroy). The layer blend and rebase of such a tick are recorded and replayed.
A retail AddAnimTime catches the walk up itself. `p4_anim_skip_kadd` no longer holds these interpolators.

PPSSPP battle route with `p4_anim_lazy` 2 (every tick) and `p4_anim_lazy_check` 1 (a copy runs the retail calls):
~2,470 deferrals / 256 frames, ~10 a frame (of ~17 AddAnimTime calls), ~125 refused (callback/loop),
every materialisation compared, **0 differences** in ~25k; the battle plays normally. Blends and rebases: 0 in this
route, so their record/replay is **not exercised yet**. Device: A/B of `p4_anim_lazy` 1/0 in battle and the dialogue
(`alayer`, `kadd`, `kinterp`). Static link was over the image by ~12 KB: vertex pool 127 K -> 126 K vertices.
b203: material UV animation (mdlAdvanceAnimControl) deferred the same way, its apply (func_003d7cf0) left out in undrawn
ticks; PPSSPP battle lazy 2 + check: ~4,100 deferrals / 256 frames (+~6.5 a frame), 0 differences. Look-at stays per tick
(its directional blend counts calls, so skipped calls would slow head turns).
b204: plain AddAnimTime steps (no callback, no loop) in C (`p4_anim_native`): clock, key walk, interpolation. Retail
found each passing key's node by scanning the interpolation frames; the key -> node map is now built once per
animation (`p4_anim_keymap`, 128 cached, every answer checked, retail search on a miss). PPSSPP battle, lazy 0 +
check: ~4,090 of ~4,300 steps / 256 frames native, all compared with retail copies, 0 differences; ~15.6k keys
walked, 0 map misses, 0 map builds in steady state.
Next for the 3 ms goal: the remaining per-tick cost (key walk of drawn ticks, material UV anim in `actrl`, look-at),
then a native per-node pass (walk + slerp + blend + matrix) on the VFPU.

## b201: the dialogue is now also GE-bound (2026-11-12)

b201 started without `profmode t` (PSPLink hung twice on `reset`), so no hwprof sections, and CPU numbers are
~10 ms lower than profiled ones: the thread profiler itself costs ~10 ms a frame. Same Margaret dialogue:

| Arm | CPU ms | GE wait ms | Frame ms |
|---|---:|---:|---:|
| `p4_anim_skip_kadd` 1 (A) | 61.91 / 61.91 | 21.5 | 83.42 |
| `p4_anim_skip_kadd` 0 (B) | 63.51 | 19.9 | 83.41 |
| pipeline off | 58.94 | 25.8 (+15.4 present) | 100.1 |

- `p4_anim_skip_kadd` (AddAnimTime of drawn skinned characters held in undrawn ticks, added on the drawn tick):
  -1.6 ms CPU. Only ~10 of ~100 AddAnimTime calls a frame are held: the rest are interpolators not mapped to a
  drawn skinned hierarchy (anim_by_interp). Kept on; movement looked normal in the screenshot.
- **CPU 62 ms < 66.7 ms, yet the frame stays at 5 vblanks: the GE is now a limit too.** `p4_exp_ge_time` (serial,
  pipeline off): GE per frame atomics 42.4 ms + 2D 8.1 ms = ~50 ms. With skin regroup/sticky off: 42.6 ms (the
  b199 batching does not change GE time). Top atomics by GE time: two full-screen background planes (100% cover,
  128x128x32 and 256x256x32 textures) ~2.5 ms each; each GE-skinned character ~1.9-2.5 ms (2-2.5k vertices,
  8-weight float batches, 15-23 draws, TEST_1 second pass `717fb`). ~10 characters -> ~20 ms of GE.
- For 30 FPS both must fit 33 ms: GE ~50 -> <33 and CPU (one drawn tick) ~48 -> <33.
Raw: `build/psplink_b201/experiments/dlg_b201_*`.

## b199-b200: PSP-shaped skin batches, guard box table (2026-11-12)

Same Margaret dialogue, real PSP, profmode t, 4 windows per arm (first discarded). Frame stays 83.4 ms = 5 vblanks
in every arm: a CPU saving shows as FPS only once it crosses a vblank step (66.7 ms at ~2 ticks/frame). For 30 FPS
one tick (logic + animation + render) must fit 33 ms; undrawn-tick work then disappears by itself.

| Arm | CPU ms | Frame ms |
|---|---:|---:|
| b199 all new switches on | 73.77 / 73.78 | 83.50 / 83.49 |
| b199 all off (as b198) | 74.92 | 83.59 |
| b199 skin on, p4_guard_small 0 | 73.44 | 83.42 |
| b200 (skin on, guard_small 0, 4-way guard boxes) | 72.97 | 83.43 |

- `p4_skin_sticky` + `p4_skin_regroup` (built once per geometry cache; the switches are part of the cache key, so
  a poke rebuilds): batches 228 -> 157, bone matrices sent 1405 -> 714 a frame; geskin 2.82 -> 2.22 Mcycles
  (sbind 887 -> 615k, smat 519 -> 280k). Screenshot looks right; clothing/hair overlap not separately reviewed.
- `p4_guard_small` (small band-crossing chunks drawn uncut): 0 chunks qualify in this scene, the band-crossing
  triangles are large. Costs ~0.3 ms; default 0. Useful only after splitting large static triangles in the cache.
- Guard boxes: the direct-mapped table rebuilt 2 entries a frame (known since b169). b200 4-way LRU sets: builds
  512 -> 0 per 256 frames, gclass 664 -> 346k cycles.
Raw: `build/psplink_b199/experiments/dlg_b199_*`, `build/psplink_b200/experiments/dlg_b200_A0.*`.
Next: animation (alayer ~3.95, kadd ~2.86 Mcycles a frame across ticks), then the unattributed ~1.2 Mcycles in
`atomic` and ~1.5 in `mesh`.

## b198 result: skipped rendering preparation (2026-11-12)

Same Margaret dialogue, real PSP, profmode t, A/B and separate bits, 4 windows (first discarded):

| Mode | Frame ms | CPU interval ms | sync interval ms |
|---|---:|---:|---:|
| Both skips, A | 83.48 | 74.09 | 9.29 |
| Original preparation | 83.54 | 76.07 | 7.36 |
| Material-group walks only | 83.51 | 75.10 | 8.31 |
| Material tint only | 83.54 | 75.06 | 8.33 |
| Both skips, A again | 83.42 | 74.04 | 9.27 |

CPU interval saves about 2.0 ms; SKIP mainDraw counters drop 9.64 -> 6.28-6.30 ms. **No demonstrated FPS gain**:
the complete scene remains about 12 FPS. bitmask 3 is left enabled, and all probe hide flags are restored to 0.
Temporary diagnostics: no 2D 67.12 ms / 14.90 FPS; no 3D atomics 33.35 ms / 29.98 FPS; full scene restored
83.61 ms / 11.96 FPS. These are not additive layer costs because frameskip/logic ticks change.

**Correction to earlier GE interpretation:** hwperf ge_wait includes waiting for shown_seq, whose flip thread
waits for GE completion and vblank. It is not a pure GPU-busy metric. Therefore the earlier 6.6 ms wait in this
dialogue must not all be attributed to GPU rendering, and the extra wait after CPU savings does not prove a
GPU bottleneck by itself. Separate finish/display timing is needed to establish that split.

Report/raw results: `build/psplink_b198/experiments/dialogue_b198_report.md`, per-arm JSON/log and summary.
Only render-only passes/tint work were omitted; animation, callbacks and effect updates still run. Final device
screenshot preserves models, portrait and dialogue; broad transition/animation validation remains separate.

## Current result: b197 capsule shadows (2026-11-12)

User reports that the geometry-volume proxy looks better. Controlled real-PSP A/B/A in the same stationary battle,
profmode t, requested 333 MHz, 4 windows per arm (first discarded):

| Arm | Frame ms | FPS | Shadow drawing dvis ms | Drawn shadow-pass dstate ms |
|---|---:|---:|---:|---:|
| Capsule A | 50.35 | 19.86 | 3.85 | 8.23 |
| Original B | 67.72 | 14.77 | 12.99 | 18.18 |
| Capsule A again | 50.40 | 19.84 | 3.83 | 8.08 |

Section times convert inclusive CPU counters at 333 MHz; overall frame times are measured wall time.
Steady counters: 5 shared masks/frame, 285 capsule segments/frame (including 24 rigid contributions), no skeletal
proxy fallback. The total frame saves about 17.35 ms; dvis itself saves about 9.15 ms, with additional savings
including fewer skipped logic ticks (~1.51 ticks/drawn frame versus ~2.03 in an original steady window).
The <=3 ms dvis target has not been reached. Full shadow cost remains larger than dvis due to pose/bookkeeping.
Do not add nested dstate, dvis and atomic totals. Capsule mode is left enabled.

Reports/raw windows: `build/psplink_b197/experiments/battle_b197_report.md` and matching JSON/log files.
Visual improvement is user-confirmed; Chie direction and broader animation/scene coverage are not separately
confirmed. No emulator tests. The older prototype failures and b193 baseline remain below for provenance.

## b197 second scene: Margaret dialogue with party (2026-11-12)

Real PSP, no switches changed, 5 windows (first discarded): **83.64 ms / 11.96 FPS**. CPU interval 76.88 ms,
GE wait 6.64 ms, present 0.12 ms. Character-shadow pass and capsule counters are zero in this scene.
Task-counter estimates at 333 MHz: drawn mainDraw 38.97 ms, SKIP mainDraw 9.53, drawn draw-scene 10.44,
SKIP draw-scene 7.93, other tasks 7.64. Nested model rendering: atomic 28.09 ms (mesh 15.46, GE skin 9.01);
qmodel 17.57 ms (animation layers/controllers); kmat 3.08 ms. These nested rows must not be added together.
About 2.50 ticks/drawn frame, 1612-1617 hierarchy nodes/frame, 10 GE skin draws / 228 batches / 1405 bone
uploads per frame. Mesh caches are warm (no builds/misses); first-fit per mesh counts 157 batches vs 228.
Prioritize investigating preserved-state no-draw paths in skipped ticks (17.45 ms combined mainDraw + scene),
animation/controller work and batch submission. This scene's GE wait is not yet isolated by pass.
Details: `build/psplink_b197/experiments/scene_margaret_dialogue_b197_report.md` plus JSON/raw log.

## b198: render-only preparation in undrawn ticks (2026-11-12)

First measured candidate for the dialogue's ~17.45 ms of SKIP mainDraw + draw-scene work. Three audited
movieNativeDrawPass1/2/3 material-group walks take their existing record==NULL no-draw path in skipped ticks.
Model draw-state callbacks still update blend tracks and run optional draw/reset callbacks; only their material
colour walk is omitted. Tint is reconstructed from per3modelMatColor on drawn ticks, not accumulated from
previous material colour. Animation clocks, mdlAdvanceModel, attached models, effects, queue construction and
OT start/end callbacks still run. No whole-task early return and no changes to animation/keyframe evaluation.

p4_skip_visual_bits: 0 original, 1 pass walks only, 2 tint only, 3 both (default). Counter row skip visual records
omitted pass/tint walks. Local full census and link/import checks pass; all four generated hooks verified.
New code/artifact hashes match the manifest. Device readback hashes match, idle reset/boot succeeded and thread
profiler is live. Performance/visual comparison pending return to the same
Margaret dialogue; user confirmed that scene can be reproduced. No emulator tests.

## Latest device session (2026-11-12, b193)

Both Heaven and battle were measured through PSPLink with `profmode t`, frameskip on and the build requesting
333/333/166 MHz. Clock was not independently calibrated in this session. The deployed PRX/data/string files were
copied back from the PSP and SHA-256 checked. A/B/A switches used the confirmed module text base `0x08804000`.
Heaven: 3 windows per arm, first discarded (2 retained). Battle: 4 windows, first discarded (3 retained).

| Individual switch | Heaven | Battle |
|---|---|---|
| All defaults on | 66.95-67.31 ms, about 14.9 FPS | 63.39-63.52 ms, about 15.8 FPS |
| `p4_hanim_vfpu` off | 67.34 ms; small effect within scene variability | 63.52 ms; no meaningful overall gain (~0.02 ms) |
| `p4_skin_call` off | 66.99 ms; no clear frame-time gain | 65.24 ms; CALL saves about 1.8 ms |
| `p4_mesh_call` off | 71.02 ms; CALL saves about 3.9 ms | 65.53 ms; CALL saves about 2.1 ms |
| `p4_shadow_lookat` off | no character shadows in this scene | 65.31 ms; on saves about 1.8 ms |

These are marginal effects measured one at a time, not an additive old-build comparison. All four switches were
restored to 1 and read back. User confirmed on 2026-11-12 that shadows look normal. A dedicated moving-head test was not separately reported. In battle,
`drestore` drops from about 0.83 Mcycles to 0; `dpose` grows from about 0.77 to 0.9 Mcycles, with net savings.
GE wait is about 0.01 ms in battle: the bottleneck remains CPU work. CALL replay has no steady-state misses:
153 skin + 100 mesh hits/frame in Heaven; 294 skin + 82 mesh hits/frame in battle. The latter has 302 batches
in total (152 body + 150 shadow); some use the CPU fallback and are not recorded.

The b191 SKIP rows now have device measurements. Per drawn frame, Heaven spends about 3.1-3.2 Mcycles in
SKIP `draw scene(linkOt)` plus 1.7 Mcycles in SKIP `mainDraw`; battle spends about 1.0 plus 1.6-1.7 Mcycles.
Battle has about 1.90 logic ticks per drawn frame (487/256). These costs are potential reserve, not all removable:
state updates and animation progression must remain intact.

Next work, based on these measurements:
1. Instrument draw-queue preparation in skipped ticks, especially Heaven's `mdlQueueModelForDraw` work, and use
   existing no-draw paths where possible while preserving state. Do not just return from the task.
2. Then regroup opaque skin batches within each material/mesh. Device counts confirm body 152 -> 99 batches and
   shadow 150 -> 97 for first-fit per mesh; retain triangle order for alpha. CALL still leaves real CPU work,
   but batch-count reduction alone does not establish its timing benefit.
3. Keep both CALL optimizations; further hierarchy VFPU work is low priority. User reports normal shadows; retain
   a moving-head check in broader visual regression coverage.

Raw logs, per-window JSON, and reports are in `build/psplink_b193/experiments/heaven_20261112_*` and
`battle_20261112_*`. The older "waiting for device A/B" notes below are superseded by this session.

## P3P reference: blob shadows (reviewed 2026-11-12)

P3P3DS backend code and three existing original-P3P GE captures were inspected. The captured shadow-like
black floor draws are soft textured planar quads (same 32x32 T8 alpha texture), not skinned character silhouettes:
2 draws / 8 vertices in battle, 5 / 20 in the spell frame, 4 / 16 in Tartarus. In the battle capture every
quad vertex lies at world y=0. This is a concrete alternative adaptation if blob shadows are acceptable;
it does not establish timings on a physical PSP or the technique for every P3P scene.
P4's ~10.6 ms dvis is CPU submission of animated-model shadows across the scene, not GE time.
A blob path could eliminate the second skinned draw; its CPU cost and state correctness need measurement.
Detailed evidence: `build/p3p_render_research/p3p_shadow_comparison.md`, JSON and decoded texture alongside it.

## b194 capsule-shadow prototype (2026-11-12)

User approved approximate articulated capsule shadows and requested testing only on a physical PSP.
`port/shadow_capsules.h` is included by rwsky.c: up to 12 long skeleton segments are projected with the existing
light shear, rasterized as an opaque union into a 128x128 VRAM mask (64x64 if the RTT allocator scales it), then
composited once as a depth-tested world-space plane. Scene depth is never cleared for the mask, and scene alpha
is masked during composite. No full vertex skinning on the successful proxy path. The current body pose and the
original shadow state/effect updates remain in use. The proxy is generic, not yet a hand-tuned rig per character;
head/weapon detail and projected bounds need visual evaluation.

`p4_shadow_capsule` defaults to 1; 0 selects the existing silhouette path. Unsupported hierarchies, unavailable
VRAM and unsafe projected quads fall back to original rendering; each hierarchy is attempted once per shadow
pass. `capsule shadow` reports count successful proxies, segments and fallbacks. A/B helper knows the switch.
The independent header retains the compiler's visibility of the renderer helpers; no broad renderer refactor.

Validation so far: local diagnostic compile/link and import/address checks pass; deployed PRX/data/strings copied
back from PSP with matching SHA-256. No emulator test, by user request. b194 boots on hardware with live thread
profiling. Battle visual/performance validation is pending; no sub-3-ms result is claimed.

## b194 device result: prototype not accepted (2026-11-12)

A/B/A in the same battle (4 windows, first discarded): capsule 62.383 ms / 16.03 FPS, original 67.753 ms /
14.76 FPS, capsule again 62.380 ms / 16.03 FPS. Inclusive dvis is about 10.05-10.13 ms with the prototype versus
11.99 ms original. Only two hierarchies/frame use the proxy (24 capsule segments), remaining shadows stay original.
The user saw character shadows disappear while weapons remained; uniform-mask correctness is not established.
User also clarified that old shadows have darker legs than clothing; overlap and material/texture alpha remain
candidate explanations, not an isolated diagnosis. No sub-3-ms result and no successful visual validation.

Build defect: local mdlManager.o predates the canonical shared object, and lacks the p4_shadow_lookat_on call.
This explains why b194 only shears two shadows despite the look-at switch reading 1. Do not compare directly
against b193; synchronize/regenerate game objects before the next build. Details and raw windows:
`build/psplink_b194/experiments/battle_b194_report.md`.

Original mode was restored and screenshots collected, then a later diagnostic VRAM savemem attempt stalled
PSPLink (zero-byte capsule_mask.bin, no subsequent shell acknowledgements). A final mode-0 command is queued,
but its execution cannot be confirmed; re-enter PSPLink before further hardware commands.

## b195: all character shadows through the proxy path (2026-11-12)

User clarified that b194 shadow disappearance started at battle entry, before the A/B switches. Code review found
that b194 rasterized its white mask with stencil disabled: GE framebuffer alpha is stencil, so colour coverage
alone does not establish opaque texture alpha. b195 explicitly replaces mask stencil with 255, then disables it
for composite. Shadow opacity now comes from the owner's shadow alpha rather than the first mesh material.

Capsule mode enables the body-pose shear for look-at characters too. Rigid attachments use cached bind-space
bounds to form a projected capsule rather than always retaining their original detailed shadow. The current
implementation still falls back to original rendering if VRAM, skeleton or guard-band checks reject a proxy;
coverage and visibility must be checked on hardware. No timing/visual claim for b195 yet.

A complete local census compile passed (FIRST/RW/CRI/SCE/MIDDLEWARE all zero failures). The rebuilt mdlManager.o
contains calls to p4_shadow_lookat_on and p4_shadow_shear_set_color. local_build.py now stamps census inputs
(psp_census.py + compat.h), regenerating objects when unstamped/changed; manifests include the hashes of the
objects actually linked. PSP/port tests remain hardware-only by user request.

b194 frame-time explanation, same A/B/A: original mainDraw 51.35 ms vs capsule 47.58 ms (~3.78 ms saved),
including dvis 11.99 -> 10.13 ms (~1.86 ms). SKIP mainDraw 4.86 -> 4.09 ms and SKIP draw-scene 3.02 -> 2.55 ms
(~1.23 ms combined); remaining ~0.36 ms elsewhere. Therefore the ~5.37 ms total marginal effect is not just
shadow submission or just preparation: render work, skipped-tick work and cache/execution behaviour change.
These are inclusive-cycle conversions at requested 333 MHz and must not be added across nested sections.

## b195 visual defects and b196 revision (2026-11-12)

User: Margaret's shadow breaks into moving/stretching parts; other characters can have a gap through the torso.
b195 hardware counters confirm all five shadow owners use shear, but 24 separate masks/frame (5 skeleton + 19
rigid attachments). A sampled dvis was about 1.50 Mcycles (~4.5 ms), frame about 53.5 ms; these are observations,
not a completed controlled A/B. The visual result is not accepted. Old mode was requested before rebuilding.

b196 changes: construct a stable rig from inverse-bind matrices instead of selecting 12 longest current-pose
segments every frame. Keep short spine links and a terminal-head sphere, fixed radii in bind units. Collect
skeleton and rigid proxies into one union per clump, compositing only at shadow-pass end; no independently
blended clothing/weapon masks within the same clump. Clip the textured receiver at near/guard planes instead of
rejecting the whole shadow when a corner leaves the valid range. Small receiver height bias avoids exact floor
coplanarity. Rig creation logs nodes/link counts for device inspection.

Local diagnostic compile/link/import checks pass and build manifest matches the sources; b196 uses the same
rebuilt mdlManager.o hash as b195. Device PRX/data/strings read back with matching hashes. Hardware visual and
performance validation of this revision still pending; all testing remains on a real PSP.

## b196 visual feedback and b197 geometry-volume proxies (2026-11-12)

User still found Margaret's silhouette strange and reported Chie's shadow pointing in an unexpected direction.
PSP screenshots were collected in Margaret's floating dialogue scene, for capsule and original modes; Chie was
not visible, so her direction is not resolved by these captures. Both modes use the same game light/shear matrix.
A statement that Chie's direction is fixed would be premature.

The joint-link proxy does not model the volume around the bones. It also used clump-frame scale for radii even
when the hierarchy matrices carry additional scale. b197 replaces joint links with cached per-bone capsules fit
to the actual influenced bind vertices in bone-local space. Dominant influences plus secondary weights >=0.25
contribute; overlapping volumes are combined in the clump mask. Each capsule is transformed by its actual pose
matrix (and model only for local-space hierarchies), with pose scale applied to its radius. Every geometry now
contributes, instead of deduplicating all atomics by hierarchy and letting the first one stand in for the body.
No full-model vertex skinning per frame. Auxiliary joints with no influenced geometry add no standalone segment.

Local compile/link/import checks pass; source/artifact hashes match. The rebuilt mdlManager object hash remains
identical to b195. Device PRX/data/strings verified by readback before restart. Visual quality and performance of
b197 still need the next real-PSP battle; no emulator test was run.
During reset of b196, before b197 boot, sceGE_Manager raised a bus error in SceKernelLoadExecThread
(EPC 0x8809C938, BadVAddr 0x3D026184). The b197 files are verified on the card but boot is not yet confirmed;
After the user physically restarted PSPLink, idle reset and b197 boot succeeded; thread profiler is live.
Battle visual/performance validation remains pending. Trace: build/psplink_b197/reset_failure_b196.log.

## Earlier baseline (b187)

Test scene: a battle (BSV105), party idle at the action menu. Frame time ~67 ms (about 15 FPS), all of it CPU.
`ge_wait` is ~0.01 ms, so the GE is idle and GE-side work (vertex formats, fill) does not move the frame.
Frameskip runs ~2.2 game ticks per drawn frame.

CPU per drawn frame: ~21.8 Mcycles in b187.

| Part | Mcycles | Notes |
|---|---|---|
| `mainDraw` (OT flush), of which: | ~19 | runs once per game tick, both ticks per drawn frame |
| - atomic rendering (`atomic`) | 10.5 | GE skin `geskin` 4.6 (302 batches, ~12k cycles each: `sbind` 1.0, `stest` 1.2, `smat` 0.8); meshes `mesh` 5.1 (`mhit` 2.8 for ~118 cache hits, ~24k each) |
| - shadow pass `mdlUpdateDrawState` (OT node 28) | ~6.6 | `dvis` (drawing the shadow) 3.3-3.6, `drestore` 0.8, `dpose` 0.7, `dcolor` 0.15, frameskip-skipped ticks `dskip` 0.4 |
| - 2D | ~1.5 | |
| `draw scene` (animation: `qmodel`, `alayer`, `kadd`, `kmat`) | ~2.1 | `kmat` (hierarchy update) 0.4-0.8 |

## Done 2026-11-11/12 (b178-b187)

| Build | Change | Result on the PSP |
|---|---|---|
| b178 | `p4_churn_top`: which geometries rebuild for their positions | In battle only small locked geometries churn (3-60 vertices a frame). The 29k vertices a frame seen in PPSSPP do not appear on hardware. |
| b179-b181 | GE skin: a large flat triangle from Chie's hand or head | Cause: the game parks a hidden part of the model (bone 38) at coordinates past 2^31. The CPU clipper dropped those triangles; the GE drew them. `p4_skin_finite` sends batches with such a bone to the CPU path, which leaves out their triangles. Cost <0.5 ms. Fixed (user confirmed). Diagnostic `p4_skin_diff` (off). Commit `97f748f`. |
| b176 vs b181 | A/B in the same battle | 93 ms vs 72 ms. b177's per-batch guard split keeps characters on the GE. |
| b182 | Profiler sections inside `mdlUpdateDrawState` (census FIXES) | This function is the character shadow pass. It flattens the clump frame along the light, poses the skeleton, draws every atomic again, then restores and poses again. 7.66 Mcycles a frame, 1.45 of them in skipped ticks. |
| b183 | `p4_shadow_skip_on`: in a tick frameskip leaves undrawn, take the game's own no-shadow path (alpha 0), keep the effect transforms and attachment delays | 74.6 -> 69.8 ms (A/B with a live poke). User: shadows look as before. Commit `eaa9fe1`. |
| b184-b185 | `p4_shadow_probe` (now off): is the shadow pose the body pose times the shear S? | Yes, for every plain pose (34/34 to 1e-3). The pose taken before the shadow pass is one animation tick old. Look-at poses (`flags140 & 0x4000`) differ in a few bones. |
| b186-b187 | `p4_shadow_shear`: no look-at, so pose once for the body and draw the shadow from it. Atomic models and world-space bones are multiplied by S (GE world matrix or the CPU paths). | Used for ~2.2 of ~5 shadows a frame (the rest are look-at). A/B: 67.1 vs 67.3 ms, about 0.2 ms. `drestore` -0.34 Mcycles, `dvis` +0.3. User: shadows look right. **Not committed yet** (working copy). |
| tool | `tools/local_build.py` | Builds in a local mirror (`P4_LOCAL`, default `p3f-src` under the user home directory) and copies `build/psplink_bNNN` back. `psp_census.py --compile` takes 1m50 s locally against 29 min on the share. Link ~20 s. Same bytes for b182. |

Rejected after measuring (do not redo):
- s16 vertices / a native PSP vertex format: they lower GE work and cache memory, but the GE is idle. The CPU would
  pay for the conversion.
- Material colour as GE state (`p4_material_state`): 0 rebuilds in Heaven, at most 4 meshes in battle.
- Lock-triggered skin cache rebuild (b180): the triangle was not a stale cache; it never fired.
- VFPU for the CPU fallback batches of the skin: only ~8 batches a frame, <0.5 ms in total.

## Plan, in order

1. **Hierarchy update on the VFPU.** Done in b188 (`p4_hanim_c` 1, `p4_hanim_vfpu` 1, `multiply_vfpu` per bone).
   The C path is now chosen inside `hanim_update_now`: before, `p4_hanim_c` 1 skipped the frameskip, far-LOD and NaN
   checks of `func_00397c40`. PPSSPP battle route with `p4_hanim_check` 1: ~367 checks per 256 frames, 0 unaligned,
   max diff 3-7e-4 (rounding), the battle looks right. **Waiting for a device A/B** (`kmat`): b142 found this update
   memory-bound (C vs retail 628k vs 631k), so the gain may be small. `p4_hanim_vfpu` 0 / `p4_hanim_c` 0 switch back.
2. **GE CALL sublists for skin batches** (`p4_skin_call`). Done in b189: each GE batch's `bind_mask`, `bind_raster`,
   `bind_ge_light` and `draw_tested` words are recorded once into a 1 MB ring, keyed by a hash of their inputs and of
   `gs` without its camera fields. Later frames emit BASE+CALL and restore `gs`. The bone matrices stay per frame.
   Shadow-pass batches get their own record (2 a batch). PPSSPP battle route: ~262 of ~266 batches a frame replay
   (67070 hits / 256 frames, 0 refreshes, 0 refused, ring never full), and the picture is the same with the switch
   poked off/on. **Waiting for a device A/B** of `sbind`+`stest` and `dvis`. Original notes: Record each batch's state and draw words once (texture and
   CLUT, mask, alpha test, material and GE-light colours, VTYPE/VADDR/IADDR/PRIM, the TEST_1 second pass, RET) in a
   persistent arena. Free entries only 2 frames later, because the GE may still read last frame's list. Set BASE
   explicitly. Clear `gs` when recording and adopt its end state after the CALL. Each frame only bone matrices, scene
   light and CALL go into the list. CPU batches are never recorded. Target: `sbind`+`stest`+`sdraw` (~12k cycles x
   302 batches) and `dvis` (the shadow draws the same batches). Rough estimate: -5 to -10 ms. Not proven yet.
3. **The same for mesh cache hits** (`mhit` ~24k cycles a hit). Done in b190 (`p4_mesh_call`): the recorder from b189
   is shared (`call_inputs`, `call_key`, `call_replay`, `call_record`), and `draw_mesh_hit` records a hit drawn whole.
   Hits cut by `draw_guarded_compact` stay on the CPU. Instance replay may now copy BASE+CALL words. PPSSPP battle:
   123 hits a frame, 82 replay as CALLs, ~41 are guard-cut. 0 misses, 0 refused. The picture is the same with the
   switch off/on. **Waiting for a device A/B** of `mhit`. Original notes: This extends instance replay from a memcpy inside one
   frame to persistent records across frames.
4. **Look-at shadow poses** (~2.8 of 5 shadows a frame). Measure `mdlUpdateHierarchyPose` alone. Check whether the
   look-at shadow pose equals the look-at body pose times S. In b184 they differed in a few bones, but there the body
   pose was compared one tick stale.
   **Read 2026-11-12, set aside:** the look-at pose has state. `mdlUpdateHierarchyPose` (mdlManager.c ~1300-1406)
   slerps the stored `MdlLookState.rotation` toward the target by `blend` and writes it back on every call. The game
   calls it twice a tick: the shadow pass (sheared parent, so a sheared target) and then the body. That is why the
   look-at shadow pose differs in a few bones. Drawing the shadow from one body pose would halve the head-turn speed.
   Keeping two calls to keep the speed leaves only the frame flatten/restore to save, less than b187's 0.2 ms.
   **b192 (`p4_shadow_lookat`, on):** one body call with blend' = 1 - (1 - blend)^2 (two steps, the same turn speed),
   and the shadow drawn from that pose times S. PPSSPP battle: 5 of 5 shadows a frame through the shear (2.6 with the
   switch off). The head no longer follows the shadow call's sheared target. Needs a look at a head turn, and a device
   A/B (`dpose`, `drestore`).
5. **Frameskip**: ~2.2 logic ticks per drawn frame. Find what else runs in undrawn ticks that nobody sees, as was
   done for the shadow pass. b191: `hwtask` and `hwot` rows of undrawn ticks are booked apart and marked `SKIP` (24
   rows each). The profiler needs the PSP (`profmode t`), so the next device session starts here: battle, frameskip
   on, then read the SKIP rows.

## Plan agreed 2026-11-12 (review of an agent's analysis, battle and Heaven)

Order: measure first, then one change where the measured reserve is largest.

1. **Device session, 333 MHz, battle and Heaven.** A/B/A for b188 (`kmat`), b189 (`sbind`+`stest`, `dvis`), b190
   (`mhit`; in Heaven b190 matters more: b169 `mhit` 2.84 vs `sbind`+`stest` 0.34 Mcycles), b192 (`dpose`,
   `drestore`, and a head turn by eye). Then the b191 `SKIP` rows: what of `mainDraw`/`qmodel` runs in undrawn ticks
   (~2.2 ticks a drawn frame in battle, ~1.5 in Heaven). Bone updates are already skipped there (`frameskip bones`);
   look for the rest. Never a plain `return`: b183's way (the game's own no-draw path) keeps state updates.
2. **Batch counts** (b193 `p4_batch_stats`, PPSSPP is enough): per pass (body / shadow) batches, meshes, palette
   uploads, and what first-fit regrouping would give per mesh, per texture across meshes (the shadow: one colour)
   and all together. Decides between the candidates below.
   **Result, PPSSPP battle route (battle_b177, steady window, per frame):**

   | Pass | draws | batches now | meshes (floor) | first-fit per mesh | per texture | all meshes | bones sent now | palette, first-fit per mesh |
   |---|---|---|---|---|---|---|---|---|
   | body | 8 | 152 | 44 | 99 | 94 | 71 | 957 | 581 |
   | shadow | 6 | 150 | 42 | 97 | 92 | 69 | 951 | 575 |

   Heaven (heaven_save3, PPSSPP, per frame): body 7 draws, 153 batches, first-fit per mesh 99, all 72; bones sent
   971 -> 593; no shadow pass. Mesh cache 123 hits (100 as CALLs, 23 guard-cut), 0 builds, 0 emitted vertices, guard
   box builds 2 a frame. PPSSPP has no Allegrex counters (no `mhit`/`memit` cycles) and skips no ticks (0 of 256 vs
   130 of 386 on the PSP in b169), so counts carry over to the device, times and frameskip do not.
   152 + 150 = 302, the device profile's batch count. First-fit inside each mesh alone gives -35% batches and -40%
   bone matrix uploads (slot reuse saves only ~5% today). Merging the shadow across textures adds little (97 -> 92);
   across all meshes 69, but that ignores texture alpha. So the next change is regrouping per mesh for both passes
   (opaque meshes; alpha keeps triangle order), not a separate shadow batching. Still open: duplicated vertices of a
   regrouped batch (skin cache memory) and what b189 left of the per-batch cost on the device.
3. **Candidates, by measured reserve:**
   - Undrawn-tick work found in step 1.
   - Shadow pass with its own batches, merged across materials whose state is compatible (texture/alpha cut-outs,
     material alpha, blend/depth/mask, draw order). Geometry unchanged. Preferred over a simplified shadow mesh.
   - Regroup body batches within a material (first-fit by bone set). Opaque meshes only: alpha keeps triangle order.
     Worth it only if step 2 shows fewer batches and b189 left a real per-batch cost.
   - Reuse `bones[]` from `skin_bones` for the shadow (same `hierarchy->matrices` under the shear). Key: skin,
     hierarchy and a pose version; check every pose write first. At most ~0.25 Mcycles.
4. **Later or not at all:** more VFPU in the animation (`kmat` is 0.4-0.8 Mcycles and memory-bound; VFPU interp
   b172 gave -6%), a VFPU clipper (`gclip` 0.055 Mcycles in Heaven), a simplified shadow mesh.
   Heaven guard box rebuilds (b169: exactly 2 a frame, maybe a slot collision in `guard_slot_boxes`) cost ~0.01-0.03
   Mcycles: fix only in passing. Heaven numbers above are from b169 (clock not confirmed): re-profile on b192+.

Guard-cut mesh hits (b190: ~41 of 123 a frame in battle, `draw_guarded_compact`): the GE drops any triangle with a
corner outside its 4096-pixel window, so meshes that cross the guard band are cut on the CPU every frame. Their chunk
boxes already decide most of it (b190 battle per frame: ~1700 chunks culled whole, ~470 inside, ~128 partial; only
partial chunks are cut per vertex). Their cost on the PSP is not measured yet (`gdraw`, `gclass`, `gloop`, `gclip`,
`gsub`). Options, if it matters: record their state part (bind_mask/raster/light) as a CALL and emit only the draws;
draw inside chunks as index ranges of the cached array instead of copying them to the frame pool.

Open items from earlier plans that were not re-checked in this session (rendering gaps from 2026-11-09, movie or
SAS hang after ~10 min of sound): see the dated sections in [next_steps_b112.md](next_steps_b112.md).

## How to work

Set `PSPDEV` to your compiler root, `P4_HOST` to a local PSPLink host folder and `P4_SESSION` to a separate local session folder. These are machine-specific environment variables; paths below are relative to those folders.

- Edit sources on the share. Build with `python tools/local_build.py` with `PSPDEV` set,
  and add `--census` after any change to the FIXES in `tools/psp_census.py`. Game objects come from `build/obj`, not
  from `psp_hw_debug.py`.
- Deploy through the PSPLink queue (`${P4_SESSION}/commands.txt`): copy the PRX and data,
  copy them back and compare sha256, `reset`, then `ldstart` in a separate step. `profmode t` survives `reset`.
- A/B: poke switches at `0x08804000 + ELF offset` (`psp-nm build/psplink_bNNN/p4.elf`), `dcache w`, read the value
  back, then take 3 or more `hwperf` windows per arm.
- Grep the decompilation in the local copy (`ext/Persona4-Decompilation`); over the share it times out.
