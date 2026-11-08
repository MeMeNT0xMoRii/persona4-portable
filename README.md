# Persona 4 for PSP

An experimental native PSP port of **Shin Megami Tensei: Persona 4** (PlayStation 2, USA, `SLUS_217.82`). The port combines recovered C, locally translated retail routines and PSP implementations of graphics, input, files, saves, audio and movie playback.

You must supply your own legally obtained USA disc image. Game executables, archives, music, voices, movies and generated game data are not included in the source repository.

## Download the Windows preparation tool

The prebuilt Windows package for b164 has been withdrawn because it contained a game-derived EBOOT; a package that builds the EBOOT locally is in preparation. For now, build from source with [the build guide](docs/building.md), then prepare your own disc with [the preparation guide](docs/prepare_game.md).

This is an experimental prerelease. The utility bundles the previously prepared b164 EBOOT and reconstructs its matching resources from your ISO; the source repository contains no prebuilt EBOOT. The independently checked clean source build described below has different hashes.

## Build and install

- **Compile EBOOT.PBP from source:** follow [Building from source](docs/building.md), including the pinned decompilation revision and Windows PSP toolchain requirements.
- **Prepare resources and install:** the same guide covers extraction, audio/movie conversion and copying the complete installation to `PSP/GAME/P4`.
- **Windows preparation utility:** [Prepare game](docs/prepare_game.md) describes the optional build-specific GUI package. It prepares resources using an existing EBOOT; it does not compile source.

A [clean source-build check](docs/build_validation_b164.md) successfully produced an EBOOT. Its output differs from the older local b164 binary; the fresh build has not yet been tested on PSP.

The current working-tree build is **b164**. The source build currently targets Windows and a Windows PSPSDK toolchain. A PSP with 64 MB RAM and firmware capable of running homebrew is required; PSP-1000 support is not established.

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

Publish source tooling separately from locally generated game artifacts. **EBOOT.PBP and the preparation package contain game-derived code**, including translated retail routines; excluding ISO files and assets does not establish permission to redistribute those binaries. The project license covers only our original contributions. See [publication notes](docs/publishing.md) for the packaging boundaries.

Persona 4 and associated names and assets belong to their respective rights holders. This is an unofficial project with no affiliation with or endorsement by Atlus, Sega or Sony.
