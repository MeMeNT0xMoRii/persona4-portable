# Build and prepare Persona 4 for PSP

## Source-build Windows utility

The current utility builds **EBOOT.PBP on your PC**, then prepares resources from your original Persona 4 USA (`SLUS-21782`) ISO. New preparation packages contain our port sources and conversion tools. They contain **no prebuilt game EBOOT, compiled game objects, retail executable, rebased game data or build-specific numerical data recipe**. The pinned upstream decompilation is fetched separately.

The older prebuilt-EBOOT b164 download has been withdrawn. These instructions describe its source-build replacement; creating a package locally does not publish it to GitHub.

1. Extract the complete new utility archive to a PC folder. Keep `Prepare P4.exe`, `_internal`, `helpers`, `source`, `prepare.json` and `toolchain` together.
2. Run **Prepare P4.exe** and select your original USA ISO and an output folder outside the utility folder.
3. Click **Build and prepare**. The utility downloads the PSP compiler on the first build (about 100 MB, verified by SHA256, kept in the build cache), verifies the disc, obtains the pinned upstream sources, compiles the game and port, checks the linked image, packages EBOOT and prepares all audio/movies.
4. Wait for **Ready**, then copy the resulting **P4** folder to **PSP/GAME/** on your memory card. Start the game from XMB.

No separate Python, Git, FFmpeg, PSPLink or PSPDEV installation is required by the frozen source-build Windows utility. The compiler and PSPSDK come from the [toolchain release](https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2): a Windows x64 build of the official pspdev GCC 15.2.0 / binutils 2.44 branches, published with its corresponding source. A package that has `toolchain/pspdev` uses that instead; see [the toolchain notes](building.md). Developer packages without the frozen EXE still need a compatible PSPDEV installation.

The first source download requires Internet access. Later builds can reuse the SHA256-verified archive. The revision is pinned to `363eab8a966d60a9773b45f330f2344329ec4f0a`. An independently obtained ZIP with the expected SHA256 can be supplied through `--upstream-archive` for offline use (this also works when opening the GUI from a command line). Read the upstream license/provenance notices before redistributing material.

Use a local PC disk for preparation and build caching. Reserve at least the ISO size plus 4 GiB for resources, plus space for source/compiler workspaces. Preparation decodes over 17,000 streams and converts all 23 movies. The original ISO is read only. Other regions, modified executables and compressed images are not supported. A homebrew-capable PSP with 64 MB RAM is required; PSP-1000 support is not established.

## Build EBOOT only

Enable **Build EBOOT only** to skip archive extraction, audio and movie conversion. This creates:

```text
P4/
  EBOOT.PBP
  p4data.bin
  p4str.bin
  build_manifest.json
```

These are matching launch files, **not a complete installation**. Full preparation also creates the disc container parts, `p4files.idx`, `SOUND`, `VIDEO` and `installation.json`. You can rerun the same ISO/output folder with Build EBOOT only disabled to finish resource preparation.

## Cache, resuming and errors

The default compiler cache is `%LOCALAPPDATA%/Persona4PSP`. Its key includes the source package identity, original ELF and actual compiler/library file hashes. A complete build is reused only after all artifact hashes and the EBOOT's embedded PRX are verified. A changed/corrupt result is rebuilt from fresh generated inputs. There is no fallback to a bundled EBOOT after a compilation error.

Each build keeps `build.log` and manifests in its cache directory. General progress/errors go to `prepare.log` in the output folder. Audio/movie conversions retain their existing resumable checks. Use the same ISO/output folder to resume; another ISO, source package or generated EBOOT needs a new output folder. Concurrent instances cannot use the same output or compiler cache job.

Compiler and converter consoles are hidden. Their output appears in the program's log instead of opening separate windows. This also applies to the build worker used by the source CLI.

Keep `.p4-work` on the PC to resume preparation. Copy only `P4` to the PSP. `installation.json` is written only after every required installation output passes verification. Successful preparation does not mean that the unfinished port has been tested through the entire game. A fresh source build can differ from the older hardware-tested b164 binary and needs its own gameplay checks.

## CLI

```powershell
python tools/psp_prepare.py --cli --bundle C:/P4-Source-Utility --iso 'D:/Persona 4.iso' --output D:/PreparedGame --pspdev C:/pspdev

# Launch files only, with an explicit local cache and offline upstream archive:
python tools/psp_prepare.py --cli --build-only --bundle C:/P4-Source-Utility --iso 'D:/Persona 4.iso' --output D:/PreparedGame --pspdev C:/pspdev --build-cache C:/P4Cache --upstream-archive C:/downloads/persona4-upstream.zip
```

Use `Prepare P4.exe` in place of `python tools/psp_prepare.py` when running the frozen package. `--check` performs disc/dependency/source-package preflight; `--probe` additionally converts sample ADX/AHX audio and a short movie. These checks alone do not compile or test gameplay.

## Developer packaging

```powershell
python -m pip install -r tools/requirements-prepare.txt
python tools/psp_prepare_release.py --vgmstream C:/tools/vgmstream --freeze --zip --pspdev C:/pspdev --out C:/temp/P4-PSP-source
```

Supply the complete Windows vgmstream CLI folder, including DLLs and notices. The generator freezes Python/Tk/PyAV, bundles conversion helpers, copies the port/build sources and writes their SHA256 manifest. `package.json` inventories the distributed files. Packages do not include PSPDEV by default: the utility downloads the archive named in `tools/build_reference.json` (`recommended_toolchain`) and checks its SHA256. `--bundle-toolchain` with `--pspdev` includes a compiler instead; a bundled compiler takes priority over a user's existing `PSPDEV`, and an explicit `--pspdev` overrides both. The copied `BUILDINFO.txt` replaces that compiler's local installation paths with `${PSPDEV}` while retaining its version/source information; the original file is unchanged. Review the exact third-party binaries' corresponding-source requirements before redistribution.

Static `.a` / `.la` files in the compiler's `lib/bfd-plugins` directory are omitted: Windows BFD tools try to load them as DLLs and can stop with a Bad Image dialog. Target libraries elsewhere and actual DLL plugins are preserved.

The legacy packager remains available explicitly through `--legacy-prebuilt --release build/release_b164`. It includes an EBOOT and the older numerical reconstruction recipe. It is not the default source-build mode.

Packages are created locally. This command does not upload or replace GitHub releases. Local compilation changes what the package distributes; it does not establish legal clearance for the upstream sources, port code or tools. See the repository's license scope and publication notes.
