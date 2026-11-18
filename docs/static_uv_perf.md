# Packed static UV candidate, b324 (2026-11-16)

Isolated `perf/shuffle-time` candidate, not deployed or merged into main. This is the first stage of static
vertex packing: positions stay float. P3P's captured 3D draws use short UVs; the character path already does so.

When a static list records a compatible mesh cache hit, `static_uv.h` packs two float UVs into two u16 values
with per-mesh GE texture scale/offset. Unlit vertices shrink from 24 to 20 bytes; lit vertices from 28 to 24.
Colours, normals and positions are copied byte for byte. UV spans over 8, nearly zero nonconstant spans,
nonfinite/extreme values, meshes under 16 vertices, MatFX and deferred transparent meshes keep their old path.

The list owns copies of packed vertices and indices, rather than copying the unused float vertex payload too.
The ordinary float mesh cache remains available to CPU clipping and other draws. Matrices, clipping paths,
triangle order and geometry bounds stay unchanged; CPU geometry readers retain float inputs. The alpha scanner supports
the new short-UV layout, including indexed draws and material alpha.

Temporary packed arrays reuse the existing four-frame buffer lifetime management with a separate 256 KiB
budget. They consume neither the frame vertex pool nor the particle spill budget. Allocation failure draws the
original mesh. The temporary overhead expires after four presented frames; retained static lists own their data.

Controls in `tools/psp_probe_ab.py`:

- `p4_static_uv_pack=1`: pack compatible meshes while recording lists; 0 records ordinary float lists.
- `p4_static_uv_check=1`: check reconstruction error and every unchanged vertex attribute. It forces list
  re-recording every 64th frame so the check also runs after enabling it on a cached scene. A difference disables
  packing. Turn this off for timing, and keep the experimental `p4_sl_trust` at 0.
- List keys and same-frame instance memoization observe the packing switch. Log `static UV pack` reports
  converted vertices, vertex payload bytes saved, temporary bytes, refusals and check differences. It reports
  packing work during recording, not the number of vertices replayed every frame.

UV quantization is approximately 1/16 texel at the largest supported span on a 512-texel texture, plus floating
state precision. This is not a claim of pixel-identical output. Extra scale/offset commands also cost GE work:
reduced vertex/list bytes do not establish an FPS improvement.

Host tests (`test_static_uv.py`) exercise 16,384 vertices across both layouts, constant/negative UVs, invalid
and tiny ranges, indexed alpha bounds, untouched attributes, copied-list ownership/address relocation after
the original memory is cleared, option/memo invalidation, independent buffer budgets and four-frame retirement.
The existing static mesh and state tests also pass. Hardware acceptance remains required.

PSP check: fresh boot, stationary and moving Heaven/battle views, enable checking until counters show zero
differences, then disable it and compare off/on/off at 333 MHz. Inspect texture seams, moving/tiling UVs,
transparent effects, camera-near geometry and scene transitions. Position compression is a later stage.
