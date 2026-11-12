# Persona4 PORTABLE

This playtest branch builds on [dreikk91/persona4-psp](https://github.com/dreikk91/persona4-psp).
It includes the local save-recovery fix and the [custom XMB presentation](docs/xmb.md)
prepared for the next PSP-3000 test build. Hardware playtesting has reached
Yosuke's Persona awakening; a complete playthrough has not been validated.

Our hardware baseline is **b165 with save recovery**. The
[`playthrough/b165-tested`](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/playthrough/b165-tested)
branch preserves it; [`playthrough/b165-xmb`](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/playthrough/b165-xmb)
contains the next XMB changes and the [18-item hardware test backlog](docs/playtest.md).
The fork's `main` branch currently contains upstream b176; it has not replaced
our tested baseline. No custom release or complete-playthrough claim is implied.

An experimental native PSP port of **Shin Megami Tensei: Persona 4** (PlayStation 2, USA, `SLUS_217.82`). The port combines recovered C, locally translated retail routines and PSP implementations of graphics, input, files, saves, audio and movie playback.

You must supply your own legally obtained USA disc image. Game executables, archives, music, voices, movies and generated game data are not included in the source repository.

## Windows preparation tool

Use the **source-build** preparation package: extract the entire archive, run `Prepare P4.exe`, select your original USA ISO and an output folder, then copy the resulting `P4` folder to `PSP/GAME/` on your memory card. The utility downloads the PSP compiler on the first build; no separate Python or PSPDEV setup is needed. See [the preparation guide](docs/prepare_game.md) for building the package and using it.

The utility compiles EBOOT and matching data locally from your ISO and pinned upstream sources. It needs Internet access for the first upstream download, or a verified offline source ZIP. The older **P4-PSP-b164-windows-x64.zip** download was withdrawn because it contained a prebuilt EBOOT. The source-build utility is implemented in `main`, but its replacement Windows download has not been published yet. Build a local package using the preparation guide below.

## Build and install

- **Compile EBOOT.PBP from source:** follow [Building from source](docs/building.md), including the pinned decompilation revision and Windows PSP toolchain requirements.
- **Prepare resources and install:** the same guide covers extraction, audio/movie conversion and copying the complete installation to `PSP/GAME/P4`.
- **Windows preparation utility:** [Build and prepare](docs/prepare_game.md) describes the source-build GUI/CLI. Frozen source packages include the compiler and build EBOOT locally.

A [clean source-build check](docs/build_validation_b164.md) successfully produced an EBOOT. Its output differs from the older local b164 binary; the fresh build has not yet been tested on PSP.

The [source-build utility validation](docs/source_build_validation_b164.md) also covers the frozen Windows runtime, build caching and hidden compiler processes.

This branch uses **b165 plus the save-recovery and XMB changes**. The source build currently targets Windows and a Windows PSPSDK toolchain. A PSP with 64 MB RAM and firmware capable of running homebrew is required; PSP-1000 support is not established.

## Status

This is a work in progress, with hardware testing of selected scenes, saves, battles and movie transitions. It has not been validated through a complete playthrough.

- Graphics run through PSP GU/GE, with clipping, mesh caches, hardware skinning and widescreen adaptations.
- Some scenes, including Dojima's room, have reached about 30 FPS on hardware at 333 MHz. Heavy scenes remain slower. Frameskip aims to keep the game clock near real time; this does not mean 30 displayed frames per second everywhere.
- Audio uses PSP SAS and streamed resources converted from the disc. Some sequenced sound behavior still needs fidelity checks.
- All 23 movies can be converted for hardware AVC playback, with separate PCM audio and movie skipping.

Hardware measurements and remaining issues are recorded in [development notes](docs/next_steps_b112.md), [audio notes](docs/audio_resources.md) and [video notes](docs/video_resources.md). Historical build numbers in these notes describe particular experiments.

The [earlier development history](docs/development_history.md) preserves the milestones and subjects of 68 local commits preceding this public source snapshot.

## Credits and upstream

This port depends on **[Raikaru/Persona4-Decompilation](https://github.com/Raikaru/Persona4-Decompilation)** and its contributors' matching decompilation, recovered names, types and address maps. Their work provides the foundation for compiling the game code for PSP. This repository is a separate port; the upstream project targets the original PS2 executable.

The reference checkout is pinned to [`363eab8a966d60a9773b45f330f2344329ec4f0a`](https://github.com/Raikaru/Persona4-Decompilation/tree/363eab8a966d60a9773b45f330f2344329ec4f0a). Fetch it separately into `ext/Persona4-Decompilation`; its files are not vendored here.

Additional dependencies include [PSPDEV](https://github.com/pspdev), [PSPSDK](https://github.com/pspdev/pspsdk), [FFmpeg](https://ffmpeg.org/), [PyAV](https://github.com/PyAV-Org/PyAV), [imageio-ffmpeg](https://github.com/imageio/imageio-ffmpeg) and [vgmstream](https://github.com/vgmstream/vgmstream). Widescreen adaptations in the census script also credit the PCSX2 SLUS-21782 patch by pavachan / nemesis2000.

## License and distribution

Original contributions owned by this project's contributors are available under the [MIT License](LICENSE), subject to the [license scope and third-party notices](THIRD_PARTY.md). This license does not grant rights to Persona 4, upstream decompiled code, retail assembly, SDKs, middleware or game assets.

The inspected upstream revision has no repository-wide license. A public repository and attribution alone do not grant permission to redistribute or relicense its contents. Keep the upstream checkout external and resolve applicable permissions before distributing derived material.

Publish source tooling separately from locally generated game artifacts. **Generated EBOOT.PBP contains game-derived code**, including translated retail routines. The source-build utility generates these locally from the user's disc instead of bundling the game executable. The project license covers only our original contributions. See [publication notes](docs/publishing.md) for the packaging boundaries.

Persona 4 and associated names and assets belong to their respective rights holders. This is an unofficial project with no affiliation with or endorsement by Atlus, Sega or Sony.
