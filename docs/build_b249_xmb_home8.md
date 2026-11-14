# b249-xmb-home8

This release combines the fork's b176-xmb-home8 work with upstream b249
(`b23d7dbb67ee49a355af533b84f4caaa138efa29`). Upstream published the work since
b176 as one commit. The merge retains both histories and reconciles the
overlapping renderer, pose, video and packaging changes. The private replay
saves are outside Git.

| Area | Integration |
| --- | --- |
| Saves, launch and exit | The memory-card code, paths, HOME callback and XMB PNGs match home8. The b249 movie handoff uses the same launch-directory resolver as the rest of the game. |
| Character heads and effects | Includes b249's corrected `__sqrt` call in `effMiscQuatAngle`, target-panel fixes and finite bone handling. Retains the fork's interpolation guards, alias-safe matrix multiply, Level Up tint, TV rings, umbrella materials and battle stencil coverage. |
| Rendering cost | Includes caching of clipped geometry, skin strips and geometry command reuse. Screen-size culling defaults to zero. Event animation clocks and bone updates use the fork's home8 settings. VU0 measurement calls are disabled in the release. |
| Movies and sound | Stops the audio workers and releases owned SAS/AVC modules around movie playback. Retains the home7 Shadow notice sound correction. Upstream's physical PSP result is recorded in [the handoff notes](audio_video_hang.md). |
| Build | The temporary boot string offsets live on the main stack. This frees 32 KiB of static storage so the combined renderer fits below the data image at `0x0900da00`. The release manifest records the linked object hashes and XMB artwork hashes. |

## Replay on PPSSPP 1.20.4

- Booted from `PSP/GAME/CAT_Ports/P4`. Slots 4, 7 and 8 loaded. Slot 8
  reached Dojima Residence on 04/17 evening. The game wrote an empty
  slot 9 and loaded it again after returning to the title. All 48 files
  belonging to the original eight slots were byte-identical afterward.
- All 58 samples of the 04/17 TV entry matched b176-xmb-home8 pixel for pixel
  in the sampled framebuffer. The replay reached the TV plaza, castle gate
  and 1F. A same-scene toggle reproduced the white umbrella holes on 04/15
  and restored the translucent panels after re-enabling the correction.
- On 1F, Yosuke's All-Out prompt and both attack portraits appeared with
  clipping and box culling enabled. The attack completed and play continued
  through Shuffle Time and back to the field. The impact cloud is still
  bright and obscures some motion. A replay earned 60 EXP and a natural hero
  level-up to 5 with the full portrait, level and dialogue visible. Heads
  were visible in the tested ordinary encounter. The boss failure and
  scripted character movements need their own replays.
- The geometry-cache comparison on 1F matched 2,607 recomputed outputs.
  One recorded group of 46 skin strips used 11,242 indices for geometry
  that used 14,256 triangle-list indices. These are emulator work counts.
- The startup handoff with a synthetic movie exercised audio pause, module
  unload, codec creation and audio restart. PPSSPP rejected the test clip
  on its first decode and the game continued to the title. This test leaves
  playback and the long-session fix on PSP open.

## Build validation

- 1,416 translation units, 825 staged inputs, 19 validated PSP import libraries,
  no unresolved native function pointers, and 5,857 rebased entries plus 168
  switch targets checked with no proof errors.
- The PPSSPP depth-mask probe passed. The host suite recorded 65 passes, two
  platform skips and three inherited errors in the Windows VAG encoder and
  older render-state mocks. Save recovery, HOME callback, guarded pool and
  simulated audio-handoff tests passed.
- Release PBP SHA-256
  `d0707cacfe20ed2ff59becb6b06becc48349fd0fd0625ae084a4e8ee6f758c80`.
  XMB title, artwork and MEMSIZE were verified from the packed file. Physical
  PSP timings at 333 MHz and the HOME confirmation during Shadow Chie remain
  open for this release.

## Installation

Use an existing prepared US installation and back up its P4 directory with
`saves`. Copy `EBOOT.PBP`, `p4data.bin` and `p4str.bin` from the same update into
that directory. The startup log identifies `b249-xmb-home8`. Slot 8 resumes
the playthrough. For a rollback, use the matched files from
[b176-xmb-home8](build_b176_xmb_home8.md).
