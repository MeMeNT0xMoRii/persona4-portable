# Persona4 PORTABLE

This playtest branch builds on [dreikk91/persona4-psp](https://github.com/dreikk91/persona4-psp).
The b249 integration builds on [b176-xmb-home8](docs/build_b176_xmb_home8.md).
The fork includes save recovery, the [custom XMB presentation](docs/xmb.md),
and a PSP HOME exit callback.
The tester has confirmed **HOME, save/load, the XMB artwork and title** on
PSP-3000 with **b176-xmb-home1**. Moving the installation into `CAT_Ports`
and launching from XMB failed when loading data from fixed paths.
**b176-xmb-home2** resolves game files and saves from the launch directory.
The tester confirmed launch from the normal XMB in `CAT_Ports` on PSP-3000.
See the [build record](docs/build_b176_xmb_home2.md).
[b176-xmb-home3](docs/build_b176_xmb_home3.md) improved the reported views
near Yukiko's Castle entrance and the door before the Chie fight.
[b176-xmb-home4](docs/build_b176_xmb_home4.md) tests the HOME exit and rain.
[b176-xmb-home5](docs/build_b176_xmb_home5.md) fixes the startup texture,
dialogue spacing and depth masks in the battle UI.
[b176-xmb-home6](docs/build_b176_xmb_home6.md) restores the numbered dials
in the save and load screens and the transform writes for model effects.
Hardware playtesting has reached Chie's Persona awakening and the next save.

Our previous hardware reference is **b165 with save recovery**. The
[`playthrough/b165-tested`](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/playthrough/b165-tested)
branch preserves it. The [hardware test backlog](docs/playtest.md) retains
observations until each is retested. Sound is enabled by default.

An experimental native PSP port of **Shin Megami Tensei: Persona 4** (PlayStation 2, USA, `SLUS_217.82`). The port combines recovered C, locally translated retail routines and PSP implementations of graphics, input, files, saves, audio and movie playback.

You must supply your own legally obtained USA disc image. Game executables, archives, music, voices, movies and generated game data are not included in the source repository.

## Windows preparation tool

Use the **source-build** preparation package: extract the entire archive, run `Prepare P4.exe`, select your original USA ISO and an output folder, then copy the resulting `P4` folder to `PSP/GAME/` on your memory card. The utility downloads the PSP compiler on the first build; no separate Python or PSPDEV setup is needed. See [the preparation guide](docs/prepare_game.md) for building the package and using it.

Download **P4-PSP-b249-windows-x64.zip** from the [b249 pre-release](https://github.com/dreikk91/persona4-psp/releases/tag/b249). The utility compiles EBOOT and matching data locally from your ISO and pinned upstream sources. It needs Internet access for the first upstream download, or a verified offline source ZIP. The older prebuilt-EBOOT b164 download was withdrawn.

## Build and install

- **Compile EBOOT.PBP from source:** follow [Building from source](docs/building.md), including the pinned decompilation revision and Windows PSP toolchain requirements.
- **Prepare resources and install:** the same guide covers extraction, audio/movie conversion and copying the complete installation to `PSP/GAME/P4`.
- **Windows preparation utility:** [Build and prepare](docs/prepare_game.md) describes the source-build GUI/CLI. Frozen source packages download the compiler and build EBOOT locally.

The b249 source package includes the audio/video handoff fix and rendering work through b249. The diagnostic b249 build passed the reported long-session title-return scenario on a PSP at 333 MHz; the generated XMB EBOOT needs separate hardware validation. See [the handoff results](docs/audio_video_hang.md) and [performance measurements](docs/perf_status.md).

See [b249 publication checks](docs/build_validation_b249.md) for current validation and known test limitations. The [earlier source-build utility validation](docs/source_build_validation_b164.md) covers build caching and hidden compiler processes.

This fork builds **b249-xmb-home8**. The Windows preparation utility linked
above builds upstream b249. Build from this checkout to include the fork's
changes. The PSP installation requires 64 MB RAM and firmware that runs homebrew.

## Status

This is a work in progress, with hardware testing of selected scenes, saves, battles and movie transitions. It has not been validated through a complete playthrough.

- Graphics run through PSP GU/GE, with clipping, mesh caches, hardware skinning and widescreen adaptations.
- Some scenes, including Dojima's room, have reached about 30 FPS on hardware at 333 MHz. Heavy scenes remain slower. Frameskip aims to keep the game clock near real time; this does not mean 30 displayed frames per second everywhere.
- Audio uses PSP SAS and streamed resources converted from the disc. Some sequenced sound behavior still needs fidelity checks.
- All 23 movies can be converted for hardware AVC playback, with separate PCM audio and movie skipping.
- b249 fixes the reported long-session title-return hang by stopping audio workers and recycling owned audio/video firmware modules before movie playback. One full-game 10+ minute sound session, all 3270 movie frames and resumed audio passed on PSP. Broader cutscene coverage remains open. Remove an old `NOSOUND` file to re-enable sound when testing this fix.
- Rendering work through b246 includes cheaper animation/skinning, corrected triangle-strip cache keys and cached guard results. The fork defaults `p4_cull_cover` to zero so distant actors remain visible. Upstream's Heaven numbers with screen-size culling describe that specific configuration. Far-skin triangle reduction and persistent mesh replay default to off.

The current performance status and plan are in [perf status](docs/perf_status.md). Earlier hardware measurements and issues are recorded in [development notes](docs/next_steps_b112.md), [audio notes](docs/audio_resources.md) and [video notes](docs/video_resources.md). Historical build numbers in these notes describe particular experiments.

The [earlier development history](docs/development_history.md) preserves the milestones and subjects of 68 local commits preceding this public source snapshot.

## Credits and upstream

This port depends on **[Raikaru/Persona4-Decompilation](https://github.com/Raikaru/Persona4-Decompilation)** and its contributors' matching decompilation, recovered names, types and address maps. Their work provides the foundation for compiling the game code for PSP. This repository is a separate port; the upstream project targets the original PS2 executable.

The reference checkout is pinned to [`363eab8a966d60a9773b45f330f2344329ec4f0a`](https://github.com/Raikaru/Persona4-Decompilation/tree/363eab8a966d60a9773b45f330f2344329ec4f0a). Fetch it separately into `ext/Persona4-Decompilation`; its files are not vendored here.

Additional dependencies include [PSPDEV](https://github.com/pspdev), [PSPSDK](https://github.com/pspdev/pspsdk), [FFmpeg](https://ffmpeg.org/), [PyAV](https://github.com/PyAV-Org/PyAV), [imageio-ffmpeg](https://github.com/imageio/imageio-ffmpeg) and [vgmstream](https://github.com/vgmstream/vgmstream). Widescreen adaptations in the census script also credit the PCSX2 SLUS-21782 patch by pavachan / nemesis2000.

## License and distribution

Original contributions owned by this project's contributors are available under the [MIT License](LICENSE), subject to the [license scope and third-party notices](THIRD_PARTY.md). This license does not grant rights to Persona 4, upstream decompiled code, retail assembly, SDKs, middleware or game assets.

The pinned Persona 4 decompilation checkout has no top-level license. See the third-party notices for the scope of the port's MIT license and the separate decompilation dependency.

Publish source tooling separately from locally generated game artifacts. **Generated EBOOT.PBP contains game-derived code**, including translated retail routines. The source-build utility generates these locally from the user's disc instead of bundling the game executable. The project license covers only our original contributions. See [publication notes](docs/publishing.md) for the packaging boundaries.

Persona 4 and associated names and assets belong to their respective rights holders. This is an unofficial project with no affiliation with or endorsement by Atlus, Sega or Sony.
