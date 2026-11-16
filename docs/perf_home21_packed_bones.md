# Home21 packed bone matrix measurement

Reference build is `b249-xmb-home21` at `fb87d510860a351a9b7487b13808400af566a43f`.
The measurement reuses the packing arithmetic from `draw_skinned_ge` and
emits the same `BONE_MATRIX_NUMBER` and 12 `BONE_MATRIX_DATA` words per
upload. Home21 already skips an upload when the current GE slot holds the
same bone. A moved or evicted bone still needs an upload and another fold
of the geometry's packing transform. That transform is constant for a
single `draw_skinned_ge` invocation.

The candidate computes the 12 packed words on first use of each bone and
copies them on later uploads in the same invocation. Each simulated pose
starts with an empty memo. The experiment checks every emitted word across
two poses before collecting timings. The cache uses 12,544 bytes for 256
entries and flags in the fixture. This is a host measurement of repeated
preparation. The number of GE bone uploads is identical between arms.

## Reproduce

```sh
python tools/bench_skin_pack.py
```

Run on Linux with GCC. The fixture builds with `-O2 -ffp-contract=off`.
It uses two synthetic palettes, one with 113 batches of five bones cycling
through 48 bones, and one with 32 batches of eight distinct bones. Each
sample is 2,000 simulated draws, with nine samples in alternating order
and a discarded warm-up. Times below are medians in microseconds per draw
from two runs on an EPYC 7763 with GCC 14.2.

| Workload | Folds home21 / memo | Identical GE words | Run 1 µs home21 / memo | Run 2 µs home21 / memo |
| --- | ---: | ---: | ---: | ---: |
| Reused bones | 565 / 48 | 7,345 | 12.37 / 3.33 | 20.22 / 4.71 |
| Unique bones | 256 / 256 | 3,328 | 5.01 / 6.70 | 6.98 / 6.11 |

Repeated folds dropped in both runs. The unique-bone control gave mixed
timings. To qualify a device change, count candidate hits per geometry
first. For packed meshes with sustained reuse, measure `smat`, CPU frame
time and available stack/heap on PSP Go before and after. Use the same
camera, pose and clipping path and compare framebuffer output. Scratch
cache line traffic and low reuse can offset the saved arithmetic. A
persistent memo would need explicit invalidation for changed poses and
packing parameters.

## Next measurements on the home21 reference

| Area | Evidence | Small A/B to qualify |
| --- | --- | --- |
| Mesh and material work | The b245 persistent replay comparison was about 0.4 ms slower in Heaven. | Count per-mesh `call_*` hit/miss and material-bind commands before measuring a narrower repeated-bind case. Include raster generation, target, fog and alpha in the equality check. |
| Skin batches and transfers | The recorded b193 battle census estimates 152 to 99 body batches when regrouped within meshes. This is a count comparison. | Compare palette lists, duplicated vertex bytes and resulting GE commands on one opaque mesh. Then measure `geskin` and `smat` with matching output. Triangle order matters for transparency. |
| GE state changes | `st_*` already caches state while recording meshes. Raw calls and target changes invalidate that state. | Count writes that repeat the exact active register value at a safe boundary. Preserve draw order and treat replayed lists, texture flushes and target switches as state changes. |
| Animation and rendering | The b240 Margaret trace attributed substantial memory work to two 64-byte RwFrame writes per bone. | Count rereads of both matrices in each draw. Compare output matrices, attached effects and look-at across camera motion before reusing a result. |
| Memory and cache | The b240 trace recorded about 61% stall cycles. The b242 strip A/B reduced both CPU interval and GE wait in the same dialogue. | Record warm and cold `mhit`, GE wait, index/writeback bytes and D-cache misses independently. Match draw workload and poses in each arm. |

Use 333 MHz, the same slot and camera, repeated A/B/A runs and a saved
home21 reference. Existing device measurements and scene conditions are
in [perf_status.md](perf_status.md). Host timing gives no PSP frame-time
estimate.
