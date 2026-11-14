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

## Development reference

Use the [playthrough/b176-xmb-home branch](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/playthrough/b176-xmb-home)
for the current fork. The tested release is **b249-xmb-home8**, built from
`e0b662e0bd969e95fd7fdf01b782388a1aaea748`. The later documentation commit
describes that binary. The first parent of the merge
`90efcf0688e837f58924957edab41e7333fe9097` is the preserved standalone
b176-xmb-home8 commit `54807166cd9d3607a8b1f31be1ce7687aadc4f99`.
Its second parent is the upstream b249 commit listed above.

The matching [build source on GitHub](https://github.com/MeMeNT0xMoRii/persona4-portable/commit/92543aa7b8d1b00d43f8aa7cc01c73e2d56742da)
is `92543aa7b8d1b00d43f8aa7cc01c73e2d56742da` with the same source tree
`8c6919b4708e70cb391db0e5e8ed9700a5a7187f`. The local bundle and GitHub use
different commit IDs because GitHub recorded new commit metadata. The
[development branch](https://github.com/MeMeNT0xMoRii/persona4-portable/commits/playthrough/b176-xmb-home)
lists the current documentation revision.

## Merge decisions and regression risks

- Renderer conflicts joined the fork's stencil and depth passes with b249's
  destination-alpha and non-finite-vertex handling. The fork's bounds checks
  also protect geometry that b249 captures for cache reuse. Retest moving
  cameras, the TV return, boss effects and repeated combat starts.
- Matrix and interpolation conflicts retain the fork's alias-safe fallback
  and equal-time key handling alongside b249's VFPU and cached paths. Watch
  head tracking during Shadow Chie and compare reported scripted motions.
- The video conflict retained launch-relative paths through b249's audio
  suspend and restart. The packager retains XMB artwork and records b249's
  object hashes. The upstream Windows utility and PSPLink firmware tests
  describe the upstream build.
- The fork leaves screen-size culling, mesh simplification and far-skin
  reduction off. Upstream's trial culling could hide visible models. The
  standalone mesh simplifier test reproduced an inverted sphere triangle
  with the experimental online path enabled. Bone updates still run on
  ticks where drawing is skipped.

## Test matrix

| Environment | Status for b249-xmb-home8 |
| --- | --- |
| Host | Clean PSP GCC build, link and address audit. Of 70 Python tests, 65 passed, two were skipped and three inherited errors remain. Standalone experimental mesh simplifier test has the failure above. |
| PPSSPP 1.20.4 | Software renderer smoke test and save/scene replays passed as detailed below. The synthetic movie reached MPEG creation and audio restart, with a decode error in PPSSPP. |
| PSP-3000 | The combined release awaits hardware testing. Earlier b176-xmb-home1 hardware reports cover XMB branding, save/load and HOME. |
| PSP Go (PSP-N1000) | The combined release awaits hardware testing. Earlier b176-xmb-home4 reports cover the castle through the 04/17 evening save. The home6 HOME hang was reported separately. |

## Next checks

1. On both PSP models at 333 MHz, test CAT_Ports launch, slot 8 and a fresh
   save/load. Replay HOME confirmation during Shadow Chie and retain the log
   if it hangs.
2. Repeat the head, battle entry/defeat, All-Out impact and scripted motion
   reports. Include the TV return and the other reported rainy scenes.
3. Test a real movie after a long sound-enabled session. Test hardware
   frame times at matched saves and scenes using the same clock and settings.

## Replay on PPSSPP 1.20.4

- Booted from `PSP/GAME/CAT_Ports/P4`. Slots 4, 7 and 8 loaded. Slot 8
  reached Dojima Residence on 04/17 evening. The game wrote an empty
  slot 9 and loaded it again after returning to the title. All 48 files
  belonging to the original eight slots were byte-identical afterward. The standalone b176-xmb-home8 release also
  loaded the new slot 9 and reached Dojima Residence.
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
