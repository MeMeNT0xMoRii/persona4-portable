# PSP switches: complete list for b366

Checked against the diagnostic b366 ELF for commit `472a53a`, 2026-11-17.
Below are **230** switches, numeric parameters and diagnostic commands.
Internal counters, pointers and request arrays are listed separately at the end.

"Initial" is the value in the sections of the checked ELF before startup code runs,
not a snapshot of live RAM. For example, FPS and quiet change at startup,
and one-shot diagnostic counters count down while running.
For plain flags 0 turns off and 1 turns on; modes, masks, budgets and floats
are described individually. "A/B" means the current `tools/psp_probe_ab.py`
supports it through `--set`; "RAM" means the symbol exists but needs a separate PSPLink poke.
Actual addresses are always computed from the matching ELF and the load base.

This is a list of what the code can do, not a recommendation to turn every optimisation on.
`_check` verifications and profiling add work themselves. A state of 0 with no active
passes in one scene proves nothing about the effect's cost in other scenes.

16-bit vertex packing for static geometry and characters is on in the initial b366.
Packing has eligibility conditions and a float fallback path.
Current hardware measurements are in [the b366 audit](hardware_b366.md).

## GE, command lists and frame presentation

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_ge_raw` | `1` | RAM | Writes hot GE commands directly instead of calling libpspgu; 0 returns to libpspgu. [rwsky.c:1716](../port/rwsky.c#L1716) |
| `p4_pipeline` | `1` | A/B | Overlaps CPU and GE work through the frame-presentation thread. [rwsky.c:80](../port/rwsky.c#L80) |
| `p4_list_double` | `0` | A/B | Opens the GE list only at present; double-buffering experiment. [rwsky.c:290](../port/rwsky.c#L290) |
| `p4_list_cached` | `1` | A/B | Command lists in cached memory with explicit cache writeback. [rwsky.c:333](../port/rwsky.c#L333) |
| `p4_draw_batch` | `8` | RAM | Draws between GE stall-address updates; 1 = every draw. [rwsky.c:1604](../port/rwsky.c#L1604) |
| `p4_flip_event` | `1` | A/B | Wakes the present thread with events instead of frequent polling. [rwsky.c:209](../port/rwsky.c#L209) |
| `p4_vsync_late` | `1` | A/B | Only without pipeline: 1 = a late frame waits for the next vblank; 0 = present at once, tearing possible. With pipeline=1 this flag does not control the flip thread's wait. [rwsky.c:1168](../port/rwsky.c#L1168) |
| `p4_frameskip` | `1` | A/B | Skips drawing frames to keep game logic at pace. [rwsky.c:95](../port/rwsky.c#L95) |
| `p4_frameskip_max` | `2` | A/B | Maximum consecutive skipped frames. [rwsky.c:95](../port/rwsky.c#L95) |
| `p4_state_cache` | `1` | RAM | Does not resend unchanged GE state. [rwsky.c:1603](../port/rwsky.c#L1603) |
| `p4_clear_stripes` | `1` | RAM | Clears the screen in narrow stripes instead of one rectangle. [rwsky.c:1086](../port/rwsky.c#L1086) |
| `p4_fps_overlay` | `0` | RAM | On-screen FPS counter; at startup also set by the FPS file. [rwsky.c:259](../port/rwsky.c#L259) |
| `p4_timeline` | `0` | A/B | Captures a timeline of the next N frames; 0 off. A counter, not a boolean. [rwsky.c:228](../port/rwsky.c#L228) |

## Static geometry, caches and compact vertices

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_static_list` | `1` | A/B | Prebuilt GE lists for static objects. [static_list.h:26](../port/static_list.h#L26) |
| `p4_static_mesh_lists` | `1` | A/B | Separate lists and bounds for each mesh of a static object. [static_list.h:27](../port/static_list.h#L27) |
| `p4_static_guard_lists` | `1` | A/B | Reuses prepared lists of parts the CPU cut for this camera. [static_list.h:106](../port/static_list.h#L106) |
| `p4_static_list_check` | `0` | A/B | Every 64th frame rewrites the list and compares command words. [static_list.h:26](../port/static_list.h#L26) |
| `p4_static_list_budget` | `4.1943e+06` | A/B | Memory budget of static lists, bytes. [static_list.h:26](../port/static_list.h#L26) |
| `p4_static_position_pack` | `1` | A/B | Packs eligible static positions as s16. [static_list.h:24](../port/static_list.h#L24) |
| `p4_static_uv_pack` | `1` | A/B | Packs eligible static UVs into 16 bits. [mesh.h:763](../port/mesh.h#L763) |
| `p4_static_uv_check` | `0` | A/B | Checks the UV packing result. [mesh.h:763](../port/mesh.h#L763) |
| `p4_static_state_cache` | `0` | A/B | Reuses the hash of unchanged state instead of recomputing it. [static_list.h:237](../port/static_list.h#L237) |
| `p4_static_state_check` | `0` | A/B | Recomputes the hash to check the state cache. [static_list.h:237](../port/static_list.h#L237) |
| `p4_sl_trust` | `0` | A/B | Experiment: shortened eligibility check of a prebuilt static list. [static_list.h:107](../port/static_list.h#L107) |
| `p4_ge_static_clip` | `0` | A/B | Experiment: prepares static geometry for clipping on the GE. [static_list.h:28](../port/static_list.h#L28) |
| `p4_ge_clip_growth` | `256` | A/B | Allowed growth of the prepared geometry in that experiment. [static_list.h:28](../port/static_list.h#L28) |
| `p4_mesh_cache` | `1` | RAM | Cache of prepared mesh arrays between frames. [rwsky.c:604](../port/rwsky.c#L604) |
| `p4_mesh_compact` | `1` | RAM | Compact arrays of only the vertices a mesh uses. [mesh.h:558](../port/mesh.h#L558) |
| `p4_mesh_call` | `1` | A/B | Reuses GE sub-lists for cached meshes. [mesh.h:606](../port/mesh.h#L606) |
| `p4_cache_budget` | `6.29146e+06` | A/B | Mesh cache budget, bytes. [rwsky.c:609](../port/rwsky.c#L609) |
| `p4_cache_reuse_last` | `1` | RAM | Reuses the last mesh entry on a miss when the pipeline allows. [mesh.h:277](../port/mesh.h#L277) |
| `p4_cache_flush_on_load` | `1` | RAM | Frees the cache when a field package starts loading. [mesh.h:273](../port/mesh.h#L273) |
| `p4_cache_audit` | `0` | A/B | Checks that cached mesh inputs are still current. [rwsky.c:607](../port/rwsky.c#L607) |
| `p4_lock_memo` | `1` | A/B | Reuses the key and UV shift for unlocked geometry. [rwsky.c:605](../port/rwsky.c#L605) |
| `p4_material_state` | `1` | RAM | 0 = material in the cache key; 1 = part of a GE-lit material passed as state; 2 = unlit compact too. [mesh.h:275](../port/mesh.h#L275) |
| `p4_topology_cache` | `1` | RAM | Cache of mesh topology and index preparation. [mesh.h:579](../port/mesh.h#L579) |
| `p4_topology_budget` | `196608` | RAM | Topology cache budget, bytes. [mesh.h:580](../port/mesh.h#L580) |
| `p4_instance_replay` | `1` | A/B | Replays commands for copies of one mesh. [mesh.h:848](../port/mesh.h#L848) |
| `p4_inst_call` | `1` | RAM | Replays a prebuilt static list for another compatible instance. [static_list.h:742](../port/static_list.h#L742) |
| `p4_inst_batch` | `1` | A/B | Merges small compatible instances into a batch. [far_lod.h:18](../port/far_lod.h#L18) |
| `p4_inst_batch_vertices` | `64` | A/B | Mesh size limit for such merging. [far_lod.h:18](../port/far_lod.h#L18) |
| `p4_inst_batch_min` | `4` | A/B | Minimum instances for a batch. [far_lod.h:18](../port/far_lod.h#L18) |
| `p4_far_proxy` | `300` | A/B | Area threshold for simplified untextured stand-ins of distant static geometry; 300 ≈ 0.3% of the screen, 0 off. [far_lod.h:207](../port/far_lod.h#L207) |
| `p4_far_proxy_min_vertices` | `100` | A/B | Minimum object vertices for a far proxy. [far_lod.h:207](../port/far_lod.h#L207) |

## Characters, skinning and triangle strips

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_ge_skin` | `1` | A/B | Character skinning on the GE; 0 moves it to the CPU. [skin.h:341](../port/skin.h#L341) |
| `p4_skin_pack` | `1` | A/B | s16 positions and u16 UVs in eligible GE batches; 0 = float. [skin.h:354](../port/skin.h#L354) |
| `p4_skin_palette` | `8` | A/B | Bone limit per batch, 1-8; a triangle with more bones needs an exception. [skin.h:357](../port/skin.h#L357) |
| `p4_skin_strip` | `1` | A/B | Draws GE batches as triangle strips; 0 = triangle lists. [skin.h:477](../port/skin.h#L477) |
| `p4_strip_pack` | `1` | A/B | Uses strips prepared on the PC from p4strips.bin instead of building them the usual way. [skin.h:486](../port/skin.h#L486) |
| `p4_strip_prims` | `0` | A/B | One PRIM per strip instead of joining with degenerate indices. [skin.h:486](../port/skin.h#L486) |
| `p4_skin_call` | `1` | A/B | Cached GE sub-lists of state and draws for character batches. [mesh.h:605](../port/mesh.h#L605) |
| `p4_skin_list` | `1` | A/B | Replays a prebuilt list of all eligible geometry of a character. [skin.h:1770](../port/skin.h#L1770) |
| `p4_skin_alpha` | `1` | A/B | Known alpha bounds of skinned batches to cut alpha passes. [rwsky.c:464](../port/rwsky.c#L464) |
| `p4_skin_sticky` | `1` | A/B | Reuses a compatible bone layout across batches. [skin.h:1016](../port/skin.h#L1016) |
| `p4_skin_regroup` | `1` | A/B | Regroups triangles for batches: 1 = eligible opaque meshes; 2 = all, diagnostic. [skin.h:1016](../port/skin.h#L1016) |
| `p4_bone_direct` | `1` | A/B | Sends bone matrices to the GE directly instead of sceGuBoneMatrix per slot. [rwsky.c:465](../port/rwsky.c#L465) |
| `p4_skin_guard` | `1` | A/B | Checks whether a character leaves the guard band; 0 leaves more work to the GE, holes possible near the camera. [skin.h:1896](../port/skin.h#L1896) |
| `p4_skin_guard_fast` | `1` | A/B | Prepared planes for the bone-sphere test. [rwsky.c:502](../port/rwsky.c#L502) |
| `p4_skin_guard_check` | `0` | A/B | Compares the fast sphere test with the old path. [rwsky.c:502](../port/rwsky.c#L502) |
| `p4_skin_vfpu` | `1` | A/B | VFPU for skinning matrix products. [rwsky.c:502](../port/rwsky.c#L502) |
| `p4_skin_vfpu_check` | `0` | A/B | Compares VFPU products with the FPU. [rwsky.c:502](../port/rwsky.c#L502) |
| `p4_skin_split` | `1` | RAM | 1 = only unsafe batches on the CPU; 0 = the whole character on the CPU on a problem; 2 = every batch on the CPU for checking. [rwsky.c:506](../port/rwsky.c#L506) |
| `p4_skin_finite` | `1` | RAM | Batches with NaN/unacceptable bone matrices go to the CPU. [skin.h:1711](../port/skin.h#L1711) |
| `p4_skin_free_wait` | `1` | A/B | Waits for the GE before freeing data it may read. [rwsky.c:484](../port/rwsky.c#L484) |
| `p4_skin_dummy` | `0` | A/B | Experiment: an extra 0xFF000000 word after a skinning PRIM. [rwsky.c:485](../port/rwsky.c#L485) |
| `p4_skin_simplify` | `0` | A/B | Percentage of triangles kept by online simplification; 0 off. [skin.h:562](../port/skin.h#L562) |
| `p4_skin_lod_depth` | `0` | A/B | Depth where skin simplification starts; 0 off. Known to leave holes in geometry. [rwsky.c:471](../port/rwsky.c#L471) |
| `p4_skin_lod_keep` | `50` | A/B | Percentage of triangles for the normal skin LOD. [rwsky.c:473](../port/rwsky.c#L473) |
| `p4_skin_lod_min` | `128` | RAM | Minimum mesh size for simplification, so small details are left alone. [rwsky.c:477](../port/rwsky.c#L477) |
| `p4_skin_far_depth` | `0` | A/B | Depth of the far level with fewer bones/triangles; 0 off. [rwsky.c:481](../port/rwsky.c#L481) |
| `p4_skin_far_keep` | `30` | A/B | Percentage of triangles at the far level. [rwsky.c:482](../port/rwsky.c#L482) |
| `p4_skin_far_bones` | `8` | A/B | Target bone limit at the far level. [rwsky.c:482](../port/rwsky.c#L482) |
| `p4_mesh_override` | `0` | A/B | Uses prepared indices from override/<key>.bin. [skin.h:562](../port/skin.h#L562) |
| `p4_mesh_capture` | `0` | A/B | Writes geometry to capture/<key>.bin for preparation on the PC; creates files on the card. [skin.h:562](../port/skin.h#L562) |
| `p4_strip_why` | `0` | A/B | Diagnoses why strips are short and how much repacking could gain. [skin.h:478](../port/skin.h#L478) |
| `p4_strip_check` | `0` | A/B | Checks strips against the original triangles and winding. [skin.h:478](../port/skin.h#L478) |
| `p4_skin_diff` | `0` | RAM | Diagnostic comparison of GE-batch vertices with CPU skinning. [skin.h:1659](../port/skin.h#L1659) |
| `p4_batch_stats` | `0` | A/B | Counts alternative batching options; may disable fast lists. [skin.h:1050](../port/skin.h#L1050) |

## Animation, bones and skipped ticks

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_hanim_c` | `1` | A/B | Builds skeleton matrices in port code; 0 = the original translated function. [hanim.h:9](../port/hanim.h#L9) |
| `p4_hanim_vfpu` | `1` | A/B | 0 = FPU, 1 = VFPU matrix products, 2 = experiment with a fused bone build. [hanim.h:14](../port/hanim.h#L14) |
| `p4_hanim_check` | `0` | A/B | Sampled comparison of skeleton matrices with the original path. [hanim.h:9](../port/hanim.h#L9) |
| `p4_hanim_nan_fix` | `1` | A/B | Replaces a non-finite interpolated pose with the key pose. [hanim.h:55](../port/hanim.h#L55) |
| `p4_hanim_key_span` | `1` | RAM | Handles keys with equal times without dividing by zero. [hanim.h:366](../port/hanim.h#L366) |
| `p4_look_fix` | `1` | A/B | Checks and repairs non-finite look-at head poses. [hanim.h:134](../port/hanim.h#L134) |
| `p4_frameskip_bones` | `1` | A/B | Does not rebuild bones in ticks without drawing; also affects the lazy/skip-kadd conditions. [hanim.h:20](../port/hanim.h#L20) |
| `p4_anim_cull` | `0` | A/B | Skips updates of invisible skeletons. [hanim.h:397](../port/hanim.h#L397) |
| `p4_anim_far` | `1` | A/B | Updates distant skeletons less often. [hanim.h:397](../port/hanim.h#L397) |
| `p4_anim_far_step` | `2` | A/B | Update interval of a distant skeleton, ticks. [hanim.h:397](../port/hanim.h#L397) |
| `p4_anim_far_depth` | `500` | A/B | Depth beyond which anim_far applies. [hanim.h:398](../port/hanim.h#L398) |
| `p4_anim_far_kadd` | `1` | A/B | Defers AddAnimTime for a skeleton held by far/cull. [hanim.h:574](../port/hanim.h#L574) |
| `p4_anim_skip_kadd` | `1` | A/B | Defers AddAnimTime in ticks without drawing when bones are skipped. [hanim.h:579](../port/hanim.h#L579) |
| `p4_anim_lazy` | `1` | A/B | 0 = at once; 1 = defer interpolation in skipped ticks; 2 = in every tick, experiment. [hanim.h:600](../port/hanim.h#L600) |
| `p4_anim_lazy_check` | `0` | A/B | Compares materialised interpolators with running the original calls on a copy. [hanim.h:600](../port/hanim.h#L600) |
| `p4_anim_native` | `1` | A/B | Plain AddAnimTime in port C code instead of the original path. [hanim.h:601](../port/hanim.h#L601) |
| `p4_anim_keymap` | `1` | A/B | Cache mapping animation keys to skeleton nodes. [hanim.h:636](../port/hanim.h#L636) |
| `p4_anim_tracks` | `0` | A/B | Cache of per-bone tracks instead of walking the shared key stream; experiment. [anim_tracks.h:30](../port/anim_tracks.h#L30) |
| `p4_anim_tracks_budget` | `1.04858e+06` | A/B | Track cache budget, bytes. [anim_tracks.h:31](../port/anim_tracks.h#L31) |
| `p4_anim_tracks_reserve` | `1.57286e+06` | A/B | Free-heap reserve below which tracks give memory back to the game. [anim_tracks.h:32](../port/anim_tracks.h#L32) |
| `p4_frame_lazy` | `0` | A/B | Defers writing bone RwFrames until read; not the same as anim_lazy. [hanim.h:1346](../port/hanim.h#L1346) |
| `p4_sync_skip_fresh` | `0` | A/B | Does not remultiply fresh bone LTMs during RenderWare sync. [hanim.h:1331](../port/hanim.h#L1331) |
| `p4_sync_skip_check` | `0` | A/B | Remultiplies to compare with the skipped result. [hanim.h:1331](../port/hanim.h#L1331) |
| `p4_skip_model_lite` | `7` | A/B | Mask of work in skipped ticks: 1 = root/attached matrices, 2 = effect matrices, 4 = indexed child matrices. [rwsky.c:119](../port/rwsky.c#L119) |
| `p4_skip_visual_bits` | `3` | A/B | Mask of draw preparation skipped in skipped ticks: 1 = material pass walks, 2 = material tint. [rwsky.c:123](../port/rwsky.c#L123) |
| `p4_pose_plain` | `0` | A/B | Diagnostic: replaces the look-at pose with a plain skeleton update. [hanim.h:21](../port/hanim.h#L21) |
| `p4_anim_freeze` | `0` | A/B | Freeze mask: 1 = advance layer, 2 = bone matrices, 4 = anim control. Can stall logic that waits for an animation. [hanim.h:1660](../port/hanim.h#L1660) |
| `p4_bone_check` | `0` | A/B | Diagnoses bad bone matrices after the build. [skin.h:215](../port/skin.h#L215) |
| `p4_bone_census` | `0` | A/B | Counts live/weighted/attached bones, drops nothing. [hanim.h:502](../port/hanim.h#L502) |
| `p4_frame_census` | `0` | A/B | Counts reads of bone RwFrames. [hanim.h:1220](../port/hanim.h#L1220) |
| `p4_hanim_time` | `1` | A/B | Detailed timing of skeleton-build parts in profiling mode. [hanim.h:1066](../port/hanim.h#L1066) |
| `p4_head_trace` | `0` | A/B | Diagnostic log of problem heads/bones. [hanim.h:74](../port/hanim.h#L74) |
| `p4_pose_trace` | `0` | RAM | Traces the pose for N frames to find backward jumps. [hanim.h:1664](../port/hanim.h#L1664) |
| `p4_pose_trace_from` | `0` | RAM | Game frame where the pose trace starts; 0 disables the scheduled start. [hanim.h:1666](../port/hanim.h#L1666) |
| `p4_pose_trace_len` | `3000` | RAM | Length of the scheduled pose trace, frames. [hanim.h:1666](../port/hanim.h#L1666) |

## Guard band, culling and visibility

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_guard_clip` | `1` | A/B | CPU clipping of unsafe triangles; 0 hands them to the GE, which may drop them. [rwsky.c:600](../port/rwsky.c#L600) |
| `p4_guard_gnear` | `1` | A/B | Lets the GE handle the near plane in the main guard-band test. [rwsky.c:540](../port/rwsky.c#L540) |
| `p4_guard_near_ge` | `0` | A/B | Separate experiment: sends triangles past the near plane without CPU clipping. [rwsky.c:555](../port/rwsky.c#L555) |
| `p4_guard_big` | `0` | A/B | Edge/depth threshold: triangles small relative to their depth go to the GE whole; float, 0 off. [mesh.h:906](../port/mesh.h#L906) |
| `p4_guard_fast` | `2` | RAM | 0 = old classify, 1 = fast FPU, 2 = VFPU. [rwsky.c:530](../port/rwsky.c#L530) |
| `p4_guard_memo` | `1` | RAM | Reuses the all-inside result for the same matrices and array. [rwsky.c:531](../port/rwsky.c#L531) |
| `p4_guard_check` | `0` | RAM | Sampled comparison of VFPU classify with the FPU. [rwsky.c:532](../port/rwsky.c#L532) |
| `p4_guard_clip_fast` | `1` | A/B | Fast CPU triangle-clipping algorithm. [rwsky.c:533](../port/rwsky.c#L533) |
| `p4_guard_clip_check` | `1` | RAM | Sampled comparison of fast and old clipping. [rwsky.c:534](../port/rwsky.c#L534) |
| `p4_guard_compact` | `1` | A/B | Clips only the compact vertex array of the current mesh. [rwsky.c:538](../port/rwsky.c#L538) |
| `p4_guard_boxes` | `1` | A/B | Tests bounds of triangle blocks before testing single vertices. [rwsky.c:545](../port/rwsky.c#L545) |
| `p4_guard_chunk` | `8` | A/B | Triangles per block; effectively limited to 8-256. [guard.h:774](../port/guard.h#L774) |
| `p4_guard_loop2` | `1` | A/B | Alternative loop for guard-clip blocks. [guard.h:774](../port/guard.h#L774) |
| `p4_guard_box_vfpu` | `1` | A/B | VFPU block-bounds test. [rwsky.c:553](../port/rwsky.c#L553) |
| `p4_guard_box_check` | `0` | A/B | Compares the VFPU bounds test with the FPU. [rwsky.c:553](../port/rwsky.c#L553) |
| `p4_guard_small` | `0` | A/B | Experiment bypassing CPU clip for small partly visible blocks: 1 = blocks; 2 = whole meshes too. [rwsky.c:550](../port/rwsky.c#L550) |
| `p4_guard_full_cache` | `1` | A/B | Caches GE-lit mesh arrays that pass the guard tests. [rwsky.c:558](../port/rwsky.c#L558) |
| `p4_guard_world` | `1` | A/B | Tests spheres against camera planes in world coordinates. [rwsky.c:563](../port/rwsky.c#L563) |
| `p4_guard_sphere` | `1` | RAM | Starts with the sphere test; 0 classifies vertices at once. [rwsky.c:564](../port/rwsky.c#L564) |
| `p4_guard_result` | `1` | A/B | Caches CPU clipping results for repeated inputs. [guard.h:1145](../port/guard.h#L1145) |
| `p4_guard_result_check` | `0` | A/B | Checks the cached clipping result. [guard.h:1145](../port/guard.h#L1145) |
| `p4_mesh_frustum` | `0` | A/B | 0 = off; 1 = count only; 2 = drop meshes fully outside the frustum. GE skin not included. [mesh.h:458](../port/mesh.h#L458) |
| `p4_cull_far` | `0` | A/B | Does not draw large enough meshes beyond the given depth; 0 off. [rwsky.c:2367](../port/rwsky.c#L2367) |
| `p4_cull_min_vertices` | `500` | A/B | Minimum vertices for cull_far. [rwsky.c:2367](../port/rwsky.c#L2367) |
| `p4_cull_cover` | `0` | A/B | Does not draw objects below a screen-area threshold; 0 off, value in 1/100000 units. [rwsky.c:2381](../port/rwsky.c#L2381) |
| `p4_probe_depth_mode` | `0` | A/B | 0 = off; 1 = count; 2 = hide those entirely farther; 3 = entirely nearer than depth_cut. [mesh.h:504](../port/mesh.h#L504) |
| `p4_probe_depth_cut` | `2000` | A/B | Depth threshold for probe_depth_mode, float. [mesh.h:505](../port/mesh.h#L505) |

## Lighting, materials, transparency and shadows

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_ge_light` | `1` | A/B | Lights eligible meshes on the GE instead of the CPU. [rwsky.c:509](../port/rwsky.c#L509) |
| `p4_light_prelit` | `1` | A/B | Adds lighting to prelit colours. [rwsky.c:565](../port/rwsky.c#L565) |
| `p4_light_unprelit` | `1` | A/B | Lights unprelit meshes instead of full material colour. [rwsky.c:528](../port/rwsky.c#L528) |
| `p4_shade_fast` | `1` | RAM | Fast path for vertex colour. [mesh.h:267](../port/mesh.h#L267) |
| `p4_fog` | `1` | A/B | RenderWare fog. [mesh.h:1129](../port/mesh.h#L1129) |
| `p4_cloud` | `1` | RAM | Special path for noise clouds with a soft mask in texture alpha instead of scratch framebuffer alpha. [cloud.h:3](../port/cloud.h#L3) |
| `p4_alpha_x2` | `0` | A/B | Doubles vertex alpha like PS2 RGB; 0 keeps alpha 0-255. [mesh.h:5](../port/mesh.h#L5) |
| `p4_gs_test` | `1` | A/B | Reproduces TEST_1 and alpha-fail for 3D instead of a fixed alpha test. [guard.h:427](../port/guard.h#L427) |
| `p4_gs_test_skip` | `1` | A/B | Skips a redundant second alpha pass when alpha bounds prove it unnecessary. [guard.h:434](../port/guard.h#L434) |
| `p4_gs_test_raw_prelit` | `0` | RAM | Experiment: raw alpha reference for prelit arrays. [guard.h:433](../port/guard.h#L433) |
| `p4_gs_mesh_alpha` | `1` | A/B | Per-mesh alpha bounds instead of only the geometry-wide bound. [guard.h:464](../port/guard.h#L464) |
| `p4_gs_alpha_test` | `1` | RAM | Reproduces the PS2 alpha test for 2D passes where it affects depth. [im2d.h:544](../port/im2d.h#L544) |
| `p4_alpha_write_test` | `1` | RAM | Applies the alpha test to alpha-only mask writes. [rwsky.c:572](../port/rwsky.c#L572) |
| `p4_alpha_pixel` | `0` | RAM | Experiment: more precise per-pixel alpha/stencil for 3D. [rwsky.c:589](../port/rwsky.c#L589) |
| `p4_alpha_mask_coarse` | `32` | RAM | Lowest bit the 2D alpha mask is built from; 32 = coarser/cheaper, 1 = all bits. [rwsky.c:581](../port/rwsky.c#L581) |
| `p4_2d_alpha_masks` | `1` | A/B | 2D masks by fragment alpha. [rwsky.c:591](../port/rwsky.c#L591) |
| `p4_dst_alpha_passes` | `1` | A/B | Draws passes that read destination alpha with the required masks. [rwsky.c:573](../port/rwsky.c#L573) |
| `p4_dst_alpha_many` | `8` | RAM | Threshold of many destination-alpha passes for the shortened path; 0 = all passes. [rwsky.c:578](../port/rwsky.c#L578) |
| `p4_translucent_late` | `1` | A/B | Deferred late pass for eligible translucent RGB_ONLY meshes. [rwsky.c:593](../port/rwsky.c#L593) |
| `p4_decal_bias` | `0` | RAM | Depth offset of overlay passes, in depth units. [rwsky.c:529](../port/rwsky.c#L529) |
| `p4_blend_fix` | `0` | RAM | Internal PS2 ALPHA FIX value; the game's render state may change it. [im2d.h:283](../port/im2d.h#L283) |
| `p4_matfx_env` | `1` | RAM | Extra MatFX env-map pass; 0 turns this effect off. [rwsky.c:2516](../port/rwsky.c#L2516) |
| `p4_matfx_env_skin` | `1` | RAM | CPU skinning of a character's env map when the env pass needs it. [rwsky.c:2517](../port/rwsky.c#L2517) |
| `p4_matfx_env_ge` | `0` | A/B | Experiment: MatFX env-map pass of GE-skinned characters on the GE. [skin.h:1916](../port/skin.h#L1916) |
| `p4_shadow_shear` | `1` | RAM | Builds the projected shadow from the character's pose by shear instead of a second full build. [skin.h:48](../port/skin.h#L48) |
| `p4_shadow_lookat` | `1` | A/B | Shortened shadow path for characters with a look-at pose. [skin.h:63](../port/skin.h#L63) |
| `p4_shadow_capsule` | `1` | A/B | 0 = the original model projection; 1 = projected capsule shadow mask. [skin.h:52](../port/skin.h#L52) |
| `p4_shadow_skip_on` | `1` | RAM | Does not prepare shadows in skipped ticks. [rwsky.c:156](../port/rwsky.c#L156) |
| `p4_shadow_fade` | `1` | RAM | Shadow gradient in the RGB camera texture instead of a uniform black shadow. [im2d.h:425](../port/im2d.h#L425) |
| `p4_field_shadow` | `1` | RAM | Draws the shadow field/quad through Im3D. [rwsky.c:2906](../port/rwsky.c#L2906) |
| `p4_shadow_probe` | `0` | RAM | Diagnostic shadow-pose comparisons; modes 1-3 described in skin.h. [skin.h:43](../port/skin.h#L43) |
| `p4_screen_date` | `1` | RAM | Emulates the destination-alpha test on the main framebuffer. [rwsky.c:1995](../port/rwsky.c#L1995) |
| `p4_rtt_date` | `1` | RAM | Emulates the destination-alpha test in camera textures. [rwsky.c:1990](../port/rwsky.c#L1990) |
| `p4_rtt_alpha` | `1` | RAM | Alpha value for the silhouette in RTT shadows. [rwsky.c:2000](../port/rwsky.c#L2000) |

## Textures, RTT, 2D and effects

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_swizzle_textures` | `1` | RAM | Converts textures to the GE block layout; applies while textures are prepared. [rwsky.c:728](../port/rwsky.c#L728) |
| `p4_mip_build` | `1` | RAM | Builds mip levels while textures are prepared; does not rebuild existing ones. [rwsky.c:798](../port/rwsky.c#L798) |
| `p4_mip_min` | `64` | RAM | Minimum texture area for building mip levels. [rwsky.c:798](../port/rwsky.c#L798) |
| `p4_mipmaps` | `1` | A/B | Uses already built mip levels at bind; fit for live A/B. [rwsky.c:798](../port/rwsky.c#L798) |
| `p4_uv_rebase` | `1` | A/B | Reduces large UVs by a whole number of tiles for GE precision. [rwsky.c:568](../port/rwsky.c#L568) |
| `p4_uv_shift_step` | `1` | A/B | Step of that shift, in tiles. [rwsky.c:566](../port/rwsky.c#L566) |
| `p4_uv_memo` | `1` | A/B | Caches the UV check result instead of rescanning the array. [rwsky.c:569](../port/rwsky.c#L569) |
| `p4_rtt` | `1` | A/B | Allows creating new VRAM camera-texture slots; slots already created stay at 0. Not a global live switch for all RTT passes. [rwsky.c:904](../port/rwsky.c#L904) |
| `p4_rtt_scale` | `0` | RAM | 0 = full size in the free VRAM tail, else half; 1 = full sharing the screen-copy area; 2 = half. For new slots. [rwsky.c:905](../port/rwsky.c#L905) |
| `p4_feedback_half` | `1` | RAM | Screen copy for feedback: 0 = full in RAM, 1 = full in VRAM, 2 = half in VRAM. [im2d.h:53](../port/im2d.h#L53) |
| `p4_feedback_sprites` | `1` | RAM | Screen-feedback path through sprites instead of the original passes. [im2d.h:10](../port/im2d.h#L10) |
| `p4_im3d` | `1` | A/B | Draws RenderWare Im3D. [rwsky.c:433](../port/rwsky.c#L433) |
| `p4_ui_font` | `1` | A/B | Native accelerated font path. [im2d.h:904](../port/im2d.h#L904) |
| `p4_ui_sprite` | `1` | A/B | Native accelerated UI sprite path. [im2d.h:904](../port/im2d.h#L904) |
| `p4_font_skip` | `0` | A/B | Skips glyph preparation in ticks without drawing. [rwsky.c:102](../port/rwsky.c#L102) |
| `p4_calendar_skip` | `0` | A/B | Skips calendar UI preparation in ticks without drawing. [rwsky.c:102](../port/rwsky.c#L102) |
| `p4_2d_nan_drop` | `1` | A/B | Drops 2D draws with NaN coordinates. [im2d.h:465](../port/im2d.h#L465) |
| `p4_panel_scale` | `0` | A/B | Fixes the battle panel frame scale; float, 0 keeps the original behaviour. [im2d.h:466](../port/im2d.h#L466) |
| `p4_tv_ring` | `1` | RAM | Special path for the TV-world entry rings. [im2d.h:557](../port/im2d.h#L557) |
| `p4_texture_tint` | `1` | RAM | Special path for tinted texture silhouettes. [im2d.h:558](../port/im2d.h#L558) |
| `p4_particle_vfpu` | `1` | A/B | VFPU for particle transforms; may fall back to 0 automatically when a check fails. [rwmath.c:154](../port/rwmath.c#L154) |
| `p4_particle_vfpu_check` | `0` | A/B | Sampled comparison of VFPU particles with the original path. [rwmath.c:154](../port/rwmath.c#L154) |
| `p4_ptank_matrix` | `1` | RAM | Particles with their own matrix, including rain. [ptank.h:139](../port/ptank.h#L139) |
| `p4_ptank_spill_budget` | `2.09715e+06` | A/B | Budget of extra memory for streamed particles, bytes. [ptank.h:64](../port/ptank.h#L64) |
| `p4_fx_direct` | `1` | RAM | Direct path for effect meshes instead of the general one. [fx_direct.h:18](../port/fx_direct.h#L18) |
| `p4_fx_opaque` | `1` | RAM | Opaque effects without redundant blend/alpha test. [fx_direct.h:21](../port/fx_direct.h#L21) |
| `p4_fx_regs` | `1` | RAM | The effect's own ALPHA/TEST registers instead of the scene state. [fx_direct.h:24](../port/fx_direct.h#L24) |
| `p4_fx_skip_draw` | `0` | A/B | Skips effect draw preparation in skipped ticks; experiment. [rwsky.c:135](../port/rwsky.c#L135) |
| `p4_fx_trace` | `48` | RAM | Logs the next N unseen effect geometries; the counter counts down. [fx_direct.h:105](../port/fx_direct.h#L105) |

## Diagnostics, logs and control

| Parameter | Initial | Access | What it does |
|---|---:|---|---|
| `p4_probe_flags` | `0` | A/B | Hide/diagnostic bit mask; fully decoded below. [rwsky.c:444](../port/rwsky.c#L444) |
| `p4_probe_geometry` | `0` | A/B | Address of the first selected geometry to hide/thin out. [rwsky.c:444](../port/rwsky.c#L444) |
| `p4_probe_geometry2` | `0` | A/B | Address of the second selected geometry. [rwsky.c:445](../port/rwsky.c#L445) |
| `p4_probe_geometry3` | `0` | A/B | Address of the third selected geometry. [rwsky.c:445](../port/rwsky.c#L445) |
| `p4_probe_geometry4` | `0` | A/B | Address of the fourth selected geometry. [rwsky.c:445](../port/rwsky.c#L445) |
| `p4_probe_keep_every` | `0` | A/B | 0 = hide the selection; N = keep every Nth instance. [rwsky.c:447](../port/rwsky.c#L447) |
| `p4_probe_census` | `0` | A/B | One-shot geometry and coverage report; resets itself to 0. [rwsky.c:446](../port/rwsky.c#L446) |
| `p4_probe_2d` | `0` | A/B | 2D diagnostics: 1 = census; 2-3 = detailed passes; 4 = textures; 5 = NaN/coordinates. [im2d.h:464](../port/im2d.h#L464) |
| `p4_probe_3d` | `0` | RAM | 3D material and texture diagnostics. [mesh.h:175](../port/mesh.h#L175) |
| `p4_hide_material` | `0` | RAM | Address of a material not to draw; 0 disables the filter. [mesh.h:176](../port/mesh.h#L176) |
| `p4_cover_stats` | `0` | A/B | Ranks objects by screen coverage. [rwsky.c:2362](../port/rwsky.c#L2362) |
| `p4_replay_census` | `0` | A/B | GE command counters in instance replays. [mesh.h:849](../port/mesh.h#L849) |
| `p4_churn_top` | `1` | RAM | Logs geometries that change/rebuild the cache. [mesh.h:296](../port/mesh.h#L296) |
| `p4_exp_scissor` | `0` | A/B | Limits 3D to a 16×16 corner to separate fill rate from vertex work; the picture is cut on purpose. [rwsky.c:1669](../port/rwsky.c#L1669) |
| `p4_exp_ge_time` | `0` | A/B | Synchronous timing of GE sections; changes overlap and slows the frame. [rwsky.c:1907](../port/rwsky.c#L1907) |
| `p4_prof_clock` | `0` | A/B | Profiling sections by system time without the thread profiler. [prof.h:40](../port/prof.h#L40) |
| `p4_clock_prof` | `0` | RAM | Separate timing of dispatched tasks; not the same as prof_clock. [prof.h:174](../port/prof.h#L174) |
| `p4_vu0_profile` | `1` | RAM | Accounts VU0 emulation work by call site; the initial value depends on the profiling build. [rwmath.c:221](../port/rwmath.c#L221) |
| `p4_fpu_chop` | `0` | A/B | Experiment: FPU round-toward-zero mode. [rwsky.c:168](../port/rwsky.c#L168) |
| `p4_memstat` | `0` | RAM | Accounts memory allocations; enable from startup, the table needs about 768 KB. [main.c:129](../port/main.c#L129) |
| `p4_log_sync` | `0` | RAM | Writes the log synchronously instead of the usual background path. [main.c:50](../port/main.c#L50) |
| `p4_quiet` | `0` | RAM | Internal console-output mute while the game owns the screen; changes during startup. [main.c:39](../port/main.c#L39) |
| `p4_pad_inject` | `0` | RAM | Button bit mask added to every controller read. [pad.c:27](../port/pad.c#L27) |
| `p4_pad_press` | `0` | RAM | One-shot press bit mask, used with pad_reads. [pad.c:29](../port/pad.c#L29) |
| `p4_pad_reads` | `0` | RAM | How many controller reads pad_press is held for. [pad.c:29](../port/pad.c#L29) |
| `p4_shuffle_force_bonus` | `0` | RAM | Debug: forces the Shuffle Time bonus. [rwsky.c:49](../port/rwsky.c#L49) |
| `p4_video_keep` | `0` | RAM | Old experiment keeping the MPEG session between videos; 0 = the session ends normally. [video.c:132](../port/video.c#L132) |

## The p4_probe_flags mask

Bits combine with bitwise OR. These are diagnostic interventions that change the picture on purpose.

| Bit | Action |
|---:|---|
| 1 | Hide plain atomics. |
| 2 | Hide 2D. |
| 4 | Hide particles. |
| 8 | Remove dynamic lighting. |
| 16 | Skip 3D inside RTT; also, with `probe_keep_every > 1`, thins out all atomics. The bit has two uses. |
| 32 | Disable the depth test inside RTT. |
| 64 | Skip non-skeletal atomics inside RTT. |
| 128 | Skip drawing skeletal atomics; animation and logic continue. |
| 256 | Skip drawing the remaining atomics; logic continues. |

## Dependencies for a correct A/B

- `frameskip_bones=0` also disables the deferral conditions of `anim_lazy=1` and `anim_skip_kadd`; it is not the isolated cost of the matrices alone.
- `anim_far_kadd` makes sense together with `anim_far` or `anim_cull`.
- `strip_pack` and `strip_prims` make sense when `skin_strip` is used.
- `anim_native` may cost differently with lazy on and off: test the combinations.
- `rtt=0` does not disable existing slots. Without further code changes a clean test needs the resources created anew or a separate diagnostic skip of RTT drawing.
- `mip_build`, `swizzle_textures`, the RTT creation parameters and some cache parameters apply when resources are prepared and do not automatically rebuild what already exists.
- `matfx_env_ge=0` does not disable MatFX: the main switch of the env pass is `matfx_env`.
- `frame_lazy` defers RwFrames; `anim_lazy` defers key interpolation. They are different mechanisms.

## Parameters of other local builds

`p4_clut32` is in the uncommitted table of the test script but **absent from the b366 ELF**.
Its CLUT code was found in a separate worktree and in local b370-b376.
It is not a switch of the running b366. No new CLUT code was added during
this testing. The old `tools/psp_clut.py` alone also does not prove
that the current runtime reads its package.

## Internal symbols that are not performance switches

- `p4_acos_clamped`, `p4_anim_tick`: acos repair counter and tick number.
- `p4_audio_*`: audio stream statistics/state; not quality or speed settings.
- `p4_mc_bytes`, `p4_mc_writing`: memory card operation state.
- `p4_cache_flush_pending`: internal cross-thread cache-free request.
- `p4_scene_result`: result of the last transition request; `p4_scene_req[5]`: its command and arguments. `tools/psp_scene.py` drives transitions.
- `p4_timer_control`: PS2 timer register compatibility, not an FPS setting.
- `p4_video_on_frame`, `p4_video_buttons`: callback function pointers, not boolean parameters.

`NOSOUND`, `FPS`, `route.txt`, scene presets and the tools' command-line parameters
are separate file/CLI means, not additional RAM switches from this list.
