# Building Persona 4 for PSP

These instructions target the current **b164** working-tree sources. Run commands from the root of this port repository in **PowerShell**. Build on a local disk, such as `C:/src/p4-psp`. Linker response files now quote object paths. Network shares are much slower and have additional path/ownership complications. The [source-build preparation utility](prepare_game.md) automates this pipeline with an isolated local cache.

The aim is to rebuild the port and recreate the installation from your own disc. Bit-for-bit equality with the maintainer's previous b164 binary is not yet guaranteed: compiler, libraries, upstream revision and compiled inputs affect the output. Record the versions and hashes from your own build.

The [2026-11-08 clean-build record](build_validation_b164.md) confirms that the code pipeline produced an EBOOT from fresh generated inputs. That build differs from the older local b164 binary and still needs hardware validation.

## Requirements

- Windows x64, Git and Python 3.10 or newer. The maintainer's current environment uses Python 3.14.
- A **Windows PSPDEV toolchain** with `bin/psp-gcc.exe`, GNU binutils, PSPSDK headers/libraries and `psp-fixup-imports.exe`, `psp-prxgen.exe`, `mksfoex.exe`, `pack-pbp.exe`. The scripts currently invoke `.exe` tools directly; Linux/macOS are not supported by these instructions.
- A clean checkout of the pinned [Persona 4 decompilation](https://github.com/Raikaru/Persona4-Decompilation), as described below. The PS2 matching compilers, MWCC and the upstream PS2 build are not needed for this PSP build.
- Your own unmodified **Persona 4 USA** PS2 ISO, containing `SLUS_217.82` with SHA-1 **`4eeec0360cf2715535d9f7e52eb69d786fb0158c`**.
- For audio/video preparation: the Python packages below, a [vgmstream CLI](https://github.com/vgmstream/vgmstream/releases/tag/r2168) Windows x64 installation with its DLLs, and the Windows .NET Framework C# compiler (`Framework64/v4.0.30319/csc.exe`) used to build the included VAG encoder. FFmpeg can come from PATH or imageio-ffmpeg; it needs the encoders used by the converters, including `libx264`.
- Sufficient PC storage for the ISO, extracted containers, conversion intermediates and final installation. Audio conversion processes over 17,000 streams, so reserve substantially more space than just the EBOOT size.
- For hardware playback: a homebrew-capable PSP with 64 MB RAM and enough memory-card space for the complete installation. The release sets `MEMSIZE=1`; PSP-1000 support has not been established.

Obtain PSP tools from [PSPDEV](https://github.com/pspdev) / [PSPSDK](https://github.com/pspdev/pspsdk). The tested Windows toolchain is published as the [toolchain release](https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2) (`pspdev-windows-x64-gcc15.2.zip`, with corresponding source). Its local `BUILDINFO.txt` records:

| Component | Reference |
| --- | --- |
| PSP GCC | 15.2.0; source snapshot `1a3399792491` |
| Binutils | source snapshot `a8b53fe2b582` |
| PSPSDK tools | source snapshot `09f02b88b9f3` |
| Local toolchain build date | 2026-10-17 |

Matching the compiler version alone does not pin every target library. Keep the toolchain's build information and licenses with your local reproduction record. Newer packages may require fixes and separate validation.

## 1. Fetch the exact decompilation input

After cloning this port repository and entering its root:

```powershell
git clone https://github.com/Raikaru/Persona4-Decompilation.git ext/Persona4-Decompilation
git -C ext/Persona4-Decompilation checkout --detach 363eab8a966d60a9773b45f330f2344329ec4f0a
git -C ext/Persona4-Decompilation rev-parse HEAD
git -C ext/Persona4-Decompilation status --short
```

The last command should print nothing. Do not substitute the latest upstream revision: source names, layouts and build adaptations may have changed. `ext/` stays ignored and is fetched independently. Read [the upstream license notice](../THIRD_PARTY.md) before redistributing anything derived from it.

## 2. Configure the tools and extract the retail executable

Set `PSPDEV` explicitly; historical fallback paths in individual scripts belong to other machines.

```powershell
$env:PSPDEV = 'C:/pspdev'
$env:PATH = "$env:PSPDEV/bin;$env:PATH"
& "$env:PSPDEV/bin/psp-gcc.exe" --version
& "$env:PSPDEV/bin/psp-ld.exe" --version

python -m venv .venv
& ./.venv/Scripts/Activate.ps1
python -m pip install -r tools/requirements-video.txt

New-Item -ItemType Directory -Force build | Out-Null
python tools/extract_elf.py 'D:/Games/Persona 4 (USA).iso'
Get-FileHash build/SLUS_217.82 -Algorithm SHA1
```

If local PowerShell policy prevents activation, use `.venv/Scripts/python.exe` in place of `python`. The extraction script currently checks only a hash prefix; compare the **full** SHA-1 printed above with the required value before proceeding. Extraction reads the ISO without modifying it.

## 3. Compile the game and build EBOOT.PBP

Use an empty `build/` directory for a clean reproduction (apart from the ELF extracted in step 2). If you already have build output, use a separate checkout to preserve it. The compiler mirror is regenerated, but older objects can otherwise remain in `build/obj`.

Run each command only after the previous command succeeds:

```powershell
python tools/psp_census.py --compile
Get-Content build/census/report_compile.txt
Get-Content build/census/failed_compile.txt

python tools/psp_ptr_proof.py
python tools/psp_link.py
python tools/psp_hw_debug.py --release --out build/release_b164
```

`failed_compile.txt` must contain no failed filenames before linking. The census script now returns a failing exit code when compilation fails; it also keeps the detailed reports. Do not continue with missing or stale objects.

What these commands do:

1. Mirror upstream C/headers into `build/census`, apply PSP compatibility fixes and compile objects into `build/obj`. The external checkout stays unchanged.
2. Derive `build/rebase_table.tsv` from the retail ELF and upstream maps.
3. Link native objects and translate retained retail routines locally; build the initial data image and run the address audit. This stage also creates inputs needed by the release linker.
4. Build the XMB release PRX, embed it in `EBOOT.PBP`, and generate its matching data and hash manifest without the hardware profiler.

The final code output is:

```text
build/release_b164/
  EBOOT.PBP
  p4.prx
  p4.elf
  p4data.bin
  p4str.bin
  manifest.json
```

Always use `EBOOT.PBP`, `p4data.bin` and `p4str.bin` from the same build. The initial EBOOT in `build/psp_dist` is replaced with the final release by the packaging step below. `p4.prx` and `p4.elf` are debugging artifacts and are not needed for XMB installation.

## 4. Recreate game resources from the disc

The container extraction script currently expects the ISO at this exact path in the repository root:

```text
Shin Megami Tensei - Persona 4 (USA).iso
```

Place a copy there (it is ignored by Git), or use a local filesystem link if appropriate. Keep the original disc image unchanged. Then run:

```powershell
python tools/psp_disc.py DATA BTL ENV BGM ZZZZZ
python tools/psp_video.py --all
python tools/psp_audio.py --all --vgmstream 'C:/tools/vgmstream/vgmstream-cli.exe' --pack build/audio_pack/SOUND
python tools/psp_release.py --no-build
```

The release packager uses `BUILD` from `port/main.c` (currently 164). It checks source/artifact hashes, all 23 complete movie conversions and the complete audio catalogue. If you change a source after step 3, rebuild before packaging. Partial `--seconds` movie probes do not qualify for release packaging.

The complete installation is in **`build/psp_dist/PSP/GAME/P4`**. `build/release_b164/UPDATE/PSP/GAME/P4` contains only an update and requires the archives/index from an existing installation. For a first install, copy the complete installation folder.

## 5. Install on PSP

1. Stop the game and connect the memory card through USB storage mode.
2. Copy the complete `P4` folder to **`PSP/GAME/P4`** on the memory card. Preserve/back up existing saves before replacing an installation.
3. Confirm that the folder contains the matching EBOOT/data files, `p4files.idx`, all extracted container parts, `SOUND` and `VIDEO`. Copy the entire packed `SOUND` directory and all `.p4v` / `.pcm` movies. PC-only `.mp4` previews and conversion `.json` files are not needed by the PSP.
4. Start Persona 4 from XMB. Start or Circle skips a movie. Logs are written under the game directory.

No PSPSDK, Python or conversion tools are needed on the PSP itself.

## Verification and reporting

Host regression checks for the preparation/conversion tools:

```powershell
python -m unittest discover -s tools -p test_build.py
python -m unittest discover -s tools -p test_prepare.py
python -m unittest discover -s tools -p test_audio.py
python -m unittest discover -s tools -p test_video.py
```

Keep `manifest.json`, `release.json`, the compile report, `build/address_audit/report.md`, the full retail ELF SHA-1, your port commit/working-tree changes, the upstream commit, compiler build information and `python -m pip freeze`. Manifest hashes identify a local build; they do not by themselves prove that it matches an older release.

On hardware, check boot, consecutive opening movies, skip/return to gameplay, loading/saving, a voiced dialogue, music, a battle and a dungeon transition. Measure performance in the same scene with the same profiler settings. See [hardware test notes](hw_test_plan_b97.md) and [development measurements](next_steps_b112.md); the scenes and saves in historical experiments are not bundled with the source.

Do not publish the generated installation or EBOOT as part of a source snapshot. See [publication notes](publishing.md) for the distinction between source preparation and binary distribution.
