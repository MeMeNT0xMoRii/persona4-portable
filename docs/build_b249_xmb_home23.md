# b249-xmb-home23

Home23 starts from home22 at `5e7a4c63a014791041459dae12fce7e252ca409d` on
`playthrough/b249-home22`. Three separate commits cover screen-copy filtering,
raster read locks and GE bone matrix submission. A fourth identifies and documents
the test build. The package targets the prepared US installation (`SLUS_217.82`).

## Rendering corrections

- **Screen-copy filters.** The US PS2 executable specifies the filter in
  `mdlEffectDrawScreenTexture` (`0x001FB480`). The transition routine at `0x001FBB50`
  uses filter 1 for two unscaled passes and filter 2 for the zoomed layers.
  Home22 always selected linear sampling. Home23 respects explicit nearest
  sampling for full-size copies in VRAM and RAM. The scaled-down capture mode
  keeps linear upsampling. Effect coverage, blend state, copy geometry and
  command count are the same for these paths.
- **Read-only texture access.** The PS2 RenderWare wrappers at `0x003EC6A0`
  and `0x003EC3D0` pass the lock mode to the raster driver. Home22 performed
  the upload conversions on every unlock. An RGBA read could double an already
  converted alpha, and reading an 8-bit palette could swap its middle index
  groups again. Home23 protects the alpha and palette bytes after a read-only
  lock. Explicit writes and native stream uploads still convert the PS2 data
  once. Pixel writeback still covers a buffer that was linearized for reading.

The screen-copy change was checked against the retail render-state sequence
and synthetic pixel captures. An identical PS2/PSP gameplay frame for that
transition was unavailable.

## Skinning

Each bone upload writes the GE's four rows directly from the current bone
and packing transform. This eliminates the temporary 4×4 matrix and its
row/column traversal in direct mode. The same bone indices, slot checks,
pose inputs and fallback arguments are used. There is no memo across uploads,
actors or poses.

The A/B fixture extracts both implementations, alternates their timing order
and cycles four poses. It covers four synthetic reuse levels and a workload
sized from an earlier TV entrance trace (5 skin draws, 111 batches, 498 bone
uploads). Additional comparisons skip some batches and exercise both packed
and unpacked inputs plus the SDK callback arguments. The fixture compares
574,992 words across its parity cases, including the SDK argument captures.
Every comparison matched. A pose in the trace-sized case emits 6,474 GE
command words in both versions.

| CPU work in the fixture, median µs per workload | Home22 | Home23 |
|---|---:|---:|
| PPSSPP at guest 333 MHz, packed, distinct bones | 409.06 | 241.78 |
| PPSSPP at guest 333 MHz, packed, high reuse | 408.20 | 243.01 |
| PPSSPP at guest 333 MHz, packed, trace-sized | 415.16 | 249.15 |
| PPSSPP at guest 333 MHz, unpacked, trace-sized | 227.36 | 131.78 |
| Host GCC, packed, trace-sized | 12.44 | 8.61 |

Two fresh PPSSPP runs reproduced the same guest-clock medians. The host
numbers have additional variance. Timing covers command preparation only.
The comparison did not measure game FPS, physical GE time or device cache
behavior. Home22's memo experiment remains separate. Its cost with misses
was higher than the reference.

## Validation and open issues

- **PPSSPP 1.20.4, synthetic images.** Software and OpenGL yielded identical
  readbacks in the raster A/B. Full-size feedback matches the requested point
  or linear output at all 2,880 sampled pixels per case. Home22 differed at
  2,874 pixels when point filtering was requested. The tested reduced-copy
  strips match home22. In RGBA8888, T8 and T4, three read cycles preserve all
  tested texture bytes and all 256 rendered RGB pixels per format. Home22
  changed 253, 256 and 256 pixels respectively. Direct native upload and
  write-lock controls match. The probe uses linear test rasters.
- **PPSSPP, existing synthetic regressions.** Particle depth/blend samples,
  soft alpha mask output and the dungeon fog fixture match home22 byte for
  byte. The 2D depth-mask smoke test passes. The linked release starts,
  loads its data and reaches the first font request.
- **Packaged MIPS code, synthetic input.** Six Normal Spread fusion cases
  reach the Apsaras preview with the same traces and recorded tint arguments
  as home22. These run with stubbed platform/model calls on a CPU emulator.
- **Host checks.** 75 passed, two skipped, and three existing fixture errors.
  Town Map atlas/swizzle, calendar clipping, Shuffle Time culling and fog
  checks passed. The VAG round-trip needs the Windows compiler and two older
  renderer fixtures fail to compile.

The game archives required for full gameplay were unavailable. Matched
gameplay phases for Zio, All-Out Attacks and the Apsaras preview remain open.
In the wider renderer audit, the mixed particle alpha test at `0x717FB`,
overlapping soft masks, the reported arena TV animation and the shared depth
buffer of camera textures still need scene-specific traces. The confirmed
screen-copy and raster bugs are independent of those open diagnoses.
PSP Go and PSP-3000 play, FPS and audio are pending.

The test ZIP contains the short device checklist, checksums, A/B outputs and
source-extracting probes for the changed paths. Its manifest pins the GitHub
source and records the three runtime files' hashes.
