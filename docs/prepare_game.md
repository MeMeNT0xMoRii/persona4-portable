# Prepare Persona 4 for PSP

## Windows quick start

1. Extract the entire release archive to a folder on your PC. Keep `Prepare P4.exe`, `_internal`, `helpers`, `prepare.json` and `P4` together.
2. Start **Prepare P4.exe**.
3. Select your original **Persona 4 USA (SLUS-21782) ISO** and an output folder on your PC.
4. Click **Prepare game** and wait for **Ready**. Audio conversion includes music, voices and effects; all 23 movies are prepared too.
5. Copy the resulting **P4** folder from the output folder to **PSP/GAME/** on your PSP memory card. Start Persona 4 from the PSP menu.

Python, FFmpeg installation, PSPSDK and PSPLink are not required by the packaged Windows utility. Conversion runs offline. The original ISO is read only. Other regions, modified executables and compressed disc images are not supported yet.

Use a PC disk for preparation, then copy the result through USB disk mode. Reserve at least the ISO size plus 4 GiB of free PC disk space. Preparation can take a long time; it decodes over 17,000 streams and encodes all movies. The ISO is checked in full before conversion.

If preparation stops, select the **same ISO and output folder** again. Completed archives and conversions are verified and reused. Details are written to `prepare.log`. A different ISO or EBOOT build needs a new output folder. Two utility instances cannot prepare the same folder concurrently.

The output contains:

```
P4/
  EBOOT.PBP
  p4data.bin
  p4str.bin
  p4files.idx
  DATA.0, DATA.1, ...
  BTL.0, BGM.0, ENV.0, ZZZZZ.0
  SOUND/
  VIDEO/
  installation.json
```

Copy only `P4` to the PSP. Keep `.p4-work` on the PC if you want to resume or repeat preparation; it can be deleted once you have checked the game. `installation.json` is written only after every required output file passes SHA256 verification. The PSP port remains a work in progress; successful file preparation does not mean every part of the game has been tested.

## Developer packaging

For source compilation, use [Building from source](building.md). The steps below package an existing build. The package contains an EBOOT with game-derived code; read [publication notes](publishing.md) and [license scope](../THIRD_PARTY.md) before distributing it.

The public archive includes a build-specific EBOOT, numerical data transformation recipe and conversion tools. Original archives, converted media, retail executable and `p4data.bin` are generated from the user's ISO and excluded from this archive. `p4str.bin` is reconstructed from pointer-offset metadata. No source compilation is performed on the user's PC.

```
python -m pip install -r tools/requirements-prepare.txt
python tools/psp_prepare_release.py --release build/release_b164 --vgmstream C:/tools/vgmstream --freeze --zip --out C:/temp/P4-PSP-b164
```

Supply the complete Windows vgmstream CLI folder, including its DLLs and notices. The generator checks release artifact hashes, derives operations from the matching retail ELF, and proves that applying the recipe reproduces the release data exactly. It builds `Prepare P4.exe` using PyInstaller and bundles Python, Tk, PyAV, FFmpeg and the VAG encoder. `package.json` records distributed file hashes. The project's `LICENSE` and `PROJECT_NOTICES.md` accompany the package. Review third-party licenses and provide any required corresponding source before publishing; the generated `THIRD_PARTY.md` records this release checklist. The generator does not publish anything to GitHub.

For automation from source:

```
python tools/psp_prepare.py --cli --bundle C:/temp/P4-PSP-b164 --iso "D:/Persona 4.iso" --output D:/PreparedGame
```

The source CLI requires the Python dependencies; the packaged GUI does not. A public build should use `--freeze`.
