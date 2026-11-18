# The GE side of the frame: command lists, raw words (b363-b366, 2026-11-17)

Branch `perf/offline-mesh`. Why the Margaret dialogue holds at 67 ms although neither the CPU nor the GE needs that
long, and what was built to find and fix it. Everything here is off or exact by default except `p4_ge_raw`, which
writes the same words libpspgu writes.

## What the PSP showed (b360, Margaret dialogue, 333 MHz, profiler off)

| Arm | CPU (`cpu_us`) | GE wait at the present | Frame |
|---|---|---|---|
| base | 47.8 ms | 19.1 ms | 67 ms (4 vblanks) |
| half of all objects hidden | 35.0 ms | 15.3 ms | 50 ms (3 vblanks) |
| pipeline off (CPU, then GE) | 46.8 ms | 26.0 ms | 78.6 ms |
| animation frozen (`p4_anim_freeze` 7) | ~40 ms | more | 67 ms |

The GE alone draws the frame in ~26 ms (pipeline off): characters 10.6 ms, 2D 8.4 ms, the rest scenery led by
full-screen translucent planes (clouds 2.7 + 2.4 ms, floor 2.0 ms) and 84 flowers (2.6 ms). With 47.8 ms of CPU and
26 ms of GE a frame overlapped as designed it would fit 3 vblanks (50 ms). It takes 4: CPU + the GE's tail. So the
step to 20 FPS here is how the CPU and the GE share the frame, not CPU work alone: CPU savings (frozen animation,
-8 ms) do not move the frame, while less GE work (half the objects) does. `texture waits` was 0: the CPU never waits
for the GE inside the frame; all of the wait is at the present.

How the pipeline works (b82, `rwsky.c`): two framebuffers; list N + 1 is written behind a gate (no stall-address
updates) until frame N is on screen, then the GE catches up on it while the CPU writes on; the flip thread shows a
frame at the first vblank after its list is done; the present of N + 1 waits until N is shown.

## Tools

- **`p4_timeline` N** (b363, poke): the next N presented frames log `TL frame k: first draw, present, resumed, gate,
  ge done, shown` in microseconds from the frame's start (the end of the previous present). Shows whether the GE
  idles (its work arrives late: the game draws at the end of the tick, 2D and the late translucent pass last) or
  runs slow next to the CPU (shared bus). Not yet taken on the PSP; PPSSPP's GE ends lists on vblanks, so its
  numbers say nothing.
- **`p4_probe_flags` 128 / 256** (b362): leave out the draw of skinned atomics (characters) / of the others
  (scenery); logic and animation go on. The GE share of each.

## Double-buffered command lists (`p4_list_double`, b364, default 0)

As native PSP games do: the gate opens only at the present, so the GE runs list N whole while the CPU writes list
N + 1 (one frame of latency on screen), instead of catching up on list N while it is written. The two lists and the
two halves of the vertex pools already alternated per frame, so the change is where the gate may open
(`gate_at_present`); the video hand-over (`p4_video_renderer_begin`) opens it itself before its sync. PPSSPP: the
Margaret route runs through with it on (scene changes, battle result), picture unchanged. Measure on the PSP with
`p4_timeline`: frame 67 -> ~50 ms is the hope if the gate scheme is the cause.

## GE words written directly (`p4_ge_raw`, b365-b366, default 1)

Native games bypass libpspgu and write the command words themselves. The command numbers of every replaced call were
read from libpspgu's own objects (`psp-objdump` of `libpspgu.a`), not from memory. `p4_ge_raw 0` is libpspgu again.

| Path | Words | Note |
|---|---|---|
| State cache (`st_enable` for one-word states, alpha func, depth func, depth mask, pixel mask) | ATE/ZTE/STE/ABE/BCE/DTE/FGE/CLE/TME/LTE/LTE0-3/AAE/PCE/CTE/LOE, ATST 0xDB, ZTST 0xDE, ZMSK 0xE7, PMSKC/PMSKA 0xE8/0xE9 | libpspgu's `gu_states` is kept, `sceGuGetStatus` (im2d.h, shadow_capsules.h) reads it |
| Texture bind (`bind_raster_body`, normal rasters) | CMODE, CBP, CBW, CLOAD, TMODE, TPSM, TBPn/TBWn/TSIZEn per mip level, TFUNC, TFILTER, TLEVEL, TWRAP, one TFLUSH | libpspgu flushed after TexMode and after every TexImage; fragment 2x is never used, so TFUNC carries no 2x bit |
| Model and view matrices (`set_matrix`) | 0x3A/0x3C, then 12 x 0x3B/0x3D (float >> 8) | |
| Every `sceGuDrawArray` (--wrap in both links) | VTYPE, BASE + IADDR, BASE + VADDR, PRIM | the stall address moves every `p4_draw_batch` draws through `stall_flush` and the gate, not a syscall per draw (2D, particles, clouds, shadows, libpspgu's own `sceGuClear`) |
| GE light (`bind_ge_light_body`) | CMAT 0x53, AC/AA 0x5C/0x5D, EMC 0x54, DMC 0x56, AMC 0x55, SMC 0x57, light 0 position 0x63-0x65, type 0x5F, diffuse 0x90, ambient 0x8F | b366 |
| Blend (`set_blend`, `bind_blend`) | ALPHA 0xDF, SFIX 0xE0, DFIX 0xE1 | b366 |
| Stencil (`bind_mask_mode`) | STST 0xDC, SOP 0xDD | b366 |
| Fog (`bind_fog`) | FOG1, FOG2, FOG colour | b366, was sceGuSendCommandf/i |

Left on libpspgu on purpose (rare, or tied to its context): the camera projection (sent only when the camera
changes), depth offset/range (only when the decal bias changes), scissor (the `p4_exp_scissor` experiment), the
screen-copy and render-to-texture binds, `sceGuStart`/`Finish`/`Sync`, init.

PPSSPP, Margaret, b365: pictures equal to `p4_ge_raw 0`; `tbind` 79k -> 66k and 2D 361k -> 346k emulated cycles a
frame. The point on the PSP is the per-draw syscalls of the 2D draws and the call overhead in the instruction cache,
which PPSSPP does not show: A/B `p4_ge_raw` 0/1 there.

## Native-game practice, where the port stands

| Practice | Port |
|---|---|
| Raw command words instead of sceGu | hot paths yes (above); static/skin lists, bones, BASE/CALL were raw before |
| Double-buffered command lists | `p4_list_double` (b364), off until measured |
| Striped clears | yes, `p4_clear_stripes` |
| No indexed triangles | no: every draw is indexed; the GE has no post-transform cache, so indices cost reads and memory only |
| PRIM advances the vertex pointer | partly: `p4_strip_prims` (off) draws several PRIMs on one index list |
| Small vertex formats | statics s16 positions + u16 UVs (b345-b351), skins s16/u16/s8 normals; dynamic paths still float |
| Precomputed lighting, 1-2 lights | yes: mostly prelit, one GE light |
| State chunks as CALL | yes: static lists, skin lists, mesh replays |
| Few active bones per vertex | no, see below |
| 16-bit framebuffer | no: 8888, its alpha byte is our stencil (PS2 masks, DATE) |
| CLUT4/CLUT8 textures | mostly (PS2 originals); a few 32-bit backgrounds (clouds 256x256) |
| Swizzled textures | yes (`port/swizzle.h`) |

**Why 8 weights a vertex.** The GE has no bone index in a vertex: weight k multiplies bone matrix k. A batch's
palette holds up to 8 bones (`p4_skin_palette`), so every vertex carries 8 weights (zeros where unused) and the GE
does 8 matrix multiplies for it, although RenderWare gives a vertex at most 4 and usually 1-2. The native way: order
the palette so busy bones sit in the low slots, group a batch's triangles by the slots they really use, and draw each
group with its own PRIM and VTYPE weight count (vertex arrays per weight count, built once). b262 tried palette 4
(GE -1.9 ms, CPU +1.6 ms from twice the batches) and one-bone batches (no gain) when the dialogue was judged
CPU-bound; per-PRIM weight counts inside a batch were not tried and cost no CPU at run time.

## Next

1. PSP: `p4_timeline 24` in the dialogue; A/B `p4_list_double`, `p4_ge_raw`; `p4_probe_flags` 128 and 256.
2. By the timeline: if the GE idles, the double lists should carry the frame to 50 ms; if it runs slow beside the
   CPU, less GE work: per-PRIM weight counts with non-indexed strips (one re-layout of the skin batches), the clouds
   in CLUT8, a breakdown of the 8.4 ms of 2D.
