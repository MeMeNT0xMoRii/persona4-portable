# b249-xmb-home22 performance test build

Reference is [home21](build_b249_xmb_home21.md) at
`fb87d510860a351a9b7487b13808400af566a43f`. Home22 is an A/B update for the
prepared US `SLUS_217.82` installation. Its renderer change is confined to
the 8-bit screen effect-mask loop.

## Mask dispatch

The exact-coverage mask branch sets `GU_TFX_MODULATE` with texture alpha
through `bind_texture`, or uses the vertex color when texture sampling is
disabled. The GE computes `((vertex_alpha + 1) * texture_alpha) >> 8` with
texture alpha. A uniform vertex alpha bounds the result. Home22 skips the
bit passes above that bound. For vertex alpha of at least 128, or vertices
with varying alpha, the loop still submits all eight alpha bits. The clear
draw still clears every covered alpha even at zero opacity.

The clear draw selects the 2D vertex format. Later passes submit the same
vertex address and carry that format forward. The bundled PSPSDK's
`sceGuDrawArray` skips writing `VTYPE` when its format argument is zero.
The stencil mask, clip, depth and blend state follow the home21 path, and
the existing fallback covers unsupported quad layouts. Hardware reference
for alpha and coverage is the [GE raster pipeline](https://www.ppsspp.org/docs/psp-hardware/gpu/raster-pipeline/).

## Mask A/B measurements

One fixture ran all three renderer revisions in PPSSPP 1.20.4 software,
with the guest clock at 333 MHz. It recorded five samples per case and
rotated the arm order after a warm-up. Each sample built eight callable GE
lists with eight masks per list, then executed the last list eight times.
Each quad covered 240×144 pixels. The table gives medians from two separate
processes. The last two columns report Linux process CPU time within the
execution interval, with PPSSPP pinned to one host CPU, in milliseconds per
64 mask submissions. Guest CPU times are per 64 mask submissions in
microseconds. A sampled alpha pixel after each timed execution confirmed
that the recorded lists drew and that home21 and home22 agreed.

| Uniform vertex alpha | Draws per mask, 21 / 22 | Bytes per eight masks, 21 / 22 | Guest list build µs, 21 / 22 | Host CPU ms, run 1 | Host CPU ms, run 2 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 9 / 1 | 2,372 / 580 | 235 / 109 | 391.34 / 52.87 | 319.95 / 36.76 |
| 32 | 9 / 7 | 2,372 / 1,732 | 235 / 201 | 467.24 / 366.20 | 290.67 / 247.12 |
| 64 | 9 / 8 | 2,372 / 1,924 | 235 / 217 | 415.29 / 399.28 | 327.82 / 310.85 |
| 127 | 9 / 8 | 2,372 / 1,924 | 235 / 217 | 416.16 / 372.79 | 308.47 / 275.75 |
| 128 | 9 / 9 | 2,372 / 2,116 | 234 / 228 | 386.34 / 339.64 | 355.60 / 383.09 |
| 255 | 9 / 9 | 2,372 / 2,116 | 235 / 227 | 358.70 / 333.05 | 425.42 / 423.87 |

The home20 four-level mask uses four draws, 1,060 bytes per eight masks,
and 110–111 µs to build the same volume of lists. It quantizes sample
alpha. In the fixture the guest execution and sync time was 115–116 µs
across all arms. PPSSPP's guest report does not resolve the physical GE
cost. The process-CPU times measure the emulator's software rendering and
its submission overhead. The two runs support a gain at alpha 32, 64 and
127, with mixed execution times at 128. Alpha zero is a control, since the
retail distortion routine can return before it submits that quad. Overall
game frame time and effect frequency need measurement on the PSP.

To reproduce, set `PSPDEV` to the native toolchain and build the fixture
with source files from pinned home20 and home21 checkouts.

```sh
python tools/psp_mask_perf_probe.py build/mask-perf \
  --home20 path/to/home20/port/rwsky.c --home21 path/to/home21/port/rwsky.c
```

Run its PBP in PPSSPP software or on the device. It writes
`p4-mask-parity.tsv`, `p4-mask-timing.tsv`, `p4-mask-events.tsv` and
`p4-mask-done.txt` on the virtual or physical memory stick. The event
pairs mark the execution interval for measuring the emulator process CPU
clock. Run one instance on one core for the host comparison.

## Bone-Matrix reuse experiment

The home17 PSP Go log for the stable TV entrance has three matched
256-frame windows. Per frame it records five skin draws, 111 batches,
708 palette entries and 498 matrix uploads. The existing same-slot check
already saves 210 uploads per frame. These counters do not give the bone
IDs of the 498 uploads, so they do not establish the hit rate of a memo
after that check.

The new sweep runs the home21 packing math and tests a per-draw memo of
the 12 GE words. It varies bone reuse, changes the pose and geometry pack
across four phases, and skips selected batches to simulate a CPU clipping
fallback. A fresh 32-byte validity bitmap is used each time a skin draw
starts. The GE words match at all checked poses, including reordered slots.
The storage used by the experiment is 12,288 bytes of packed words plus
validity bits. Actual characters, attachments, Persona changes and scene
loads require a prepared gameplay replay.

```sh
python tools/bench_skin_pack_sweep.py port/rwsky.c build/bone-host
python tools/bench_skin_pack_sweep.py port/rwsky.c build/bone-psp --psp
```

The larger input matches the TV-entrance volume, including the existing
210 same-slot skips, with synthetic bone ordering. Two PPSSPP runs
reproduced the following guest-clock medians in microseconds per
five-draw workload. The fixture alternates seven samples per arm after a
warm-up, with 256 workloads per sample on the guest and 1,024 on the host.

| Synthetic palette case | Folds before / memo | Guest µs before / memo |
| --- | ---: | ---: |
| Unique | 499 / 499 | 461.5 / 548.3 |
| Quarter reused | 499 / 375 | 460.4 / 439.7 |
| Half reused | 499 / 245 | 460.4 / 325.9 |
| High reuse | 499 / 110 | 463.1 / 209.6 |
| TV-entrance volume | 498 / 196 | 468.4 / 292.4 |

Removing the per-draw validity reset in a test copy caused the GE-word
comparison to fail on another draw. The measured guest slowdown without
hits is about 19%. Home22 ships the mask change. The bone memo needs a
real per-skin distinct-upload count and a device `smat` time before it can
qualify. The ZIP includes both host CPU-clock and PPSSPP guest-clock rows.

The same TV-entrance trace reported 28,416 successful skin command-list
replays per 256 frames, plus 12 texture, 84 camera and 18 light state
retentions per frame. Those existing paths already cover repeat work in
that window. A material or mesh experiment needs a narrower repeated
state case and matching target, fog, texture generation and pose before
the command lists can be compared.

## Build and regression coverage

| Test | Result |
| --- | --- |
| PPSSPP software, exact mask parity | 65,536 combinations of vertex and texture alpha matched the formula in both arms. All 130,560 pixels after blending matched home21, including a tilted strip with varying alpha and linear filtering. |
| PPSSPP software, isolated regressions | Home21's particle depth and blend controls, precise alpha/ramp fixture including its indexed shape and depth/scissor controls, 2D depth masks, and 16 dungeon-fog samples passed. The pixel results match home21. |
| PPSSPP, packaged runtime | Startup loaded both data files and all 2,476 checked string entries. It reached the first font request. |
| Synthetic MIPS CPU, packaged PRX | All six Normal Spread cases reached the same Apsaras preview as home21. |
| Host test suite | 75 passed and two skipped. The same three known harness errors cover the Windows VAG compiler and two renderer fixture builds. The legacy Town Map atlas, calendar scissor, Shuffle Time and fog checks passed. |
| Release and address audit | Native PSPDEV GCC 15.2.0 linked the 821 verified home21 generated objects and checked 19 imports. All 5,857 rebased entries and 168 switch targets passed. |

The prepared game archives were unavailable. Real Zio, All-Out, Apsaras,
dungeon approaches and menu playthroughs on PSP Go and PSP-3000 are open.
Use the same save, camera and animation phase for home21 and home22. In
slot 10 compare a skill fade and normal particles, complete Apsaras
acquisition and keep `p4.log` if the model flickers. Check sustained mask
effects for frame cadence at 333 MHz. To install or roll back, exit to
the XMB and replace `EBOOT.PBP`, `p4data.bin` and `p4str.bin` as a set in
the existing P4 folder. The boot log identifies the selected build.
