# Source-build preparer validation (2026-11-08)

The new source-build preparer was exercised separately from the existing b164 installation and public prebuilt-EBOOT release. No PSP installation or public release was replaced.

## Verified behavior

- The source CLI built all 1,416 recovered C files from fresh generated inputs, linked the port, passed the address audit and packaged EBOOT with matching data. The compiler cache path contained spaces.
- The frozen Windows executable completed the same full pipeline using its own Python runtime, with Python removed from PATH. The upstream ZIP was supplied offline and checked against its pinned SHA256. No precompiled game objects or EBOOT were supplied to the build.
- The final release validated 17 PSP import libraries and had zero function pointers without a native target. The address audit reported no proof mismatches; its existing limits and hardware-access candidates still apply.
- A repeat invocation verified and reused the completed build cache without recompiling.
- Frozen preflight loaded Tk and PyAV and checked the retail ISO, source payload and helpers without expecting a bundled EBOOT.
- Real compiler launches from both source and frozen workers were intercepted before creation to verify `CREATE_NO_WINDOW` (`0x08000000`). A test refuses to launch the compiler if that flag is missing.
- Frozen worker exceptions returned exit code 1 with captured tracebacks rather than opening an unhandled-error dialog. Data generation and the address audit now run inside the link worker, avoiding unreliable nested windowed-Python standard-handle inheritance.
- Regression tests cover cache reuse/corruption, failed builds, wrong input identity, source integrity, archive traversal, matching PBP/PRX contents, launch-only installation and hidden worker flags, alongside existing preparation/conversion tests.

The successful generated launch files match the previously recorded clean source-build hashes:

```text
EBOOT.PBP  0a7a830263e3f5b37b6b859af588a032ab5289fa0d40f0c5ab6ebbf06f5527e6
p4.prx     e3ec1515304cde6ef446757f917ae6f43cd3cf3245075bdbc7a5673c7a7abf9b
p4data.bin ef9fc8a9a9b4f3a1e85cb3fac929d654b1d4ef04f675f347afd2f2a4d1627337
p4str.bin  4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4
```

They differ from the older prepared b164 EBOOT distributed in the initial public release. The ELF debug hash can vary with workspace paths; its hash is retained in each local manifest.

## Scope and limitations

The tested environment used Windows x64, PSP GCC 15.2.0, GNU binutils 2.44 and the existing local PSPSDK installation. A compatible Windows PSPDEV directory must be selected/configured or optionally bundled. No verified automatic compiler download is provided.

The source package and game build are separate products: the package contains port/build sources and media helpers, while the executable and rebased data are generated in the local build cache. The package does not contain a prebuilt game EBOOT, retail ELF, game objects, converted game media or a numerical game-data reconstruction recipe. The upstream sources are fetched separately or supplied as a verified archive.

This run did not repeat the hours-long complete audio/movie conversion or a complete clean installation, and the generated EBOOT was not retested on PSP. Building successfully does not establish full-game compatibility or legal clearance.

Local logs are retained under `build/`: `source_build_quiet_verified.log`, `source_build_cache_reuse.log` and the freeze logs. The compiler cache retains the exact source-package/toolchain identity, compile report, address audit and artifact manifest. These generated/private files remain ignored by Git.

## Automatic Windows package follow-up (2026-11-09)

Frozen source packages now include PSPDEV by default. The desktop window asks for the ISO and output folder; compiler selection and offline-source overrides remain CLI options. The bundled compiler takes priority over an existing PSPDEV environment variable.

Two Windows packaging failures were reproduced and fixed:

- An unusable inherited console handle raised `OSError: [Errno 22] Invalid argument` while logging and again while printing its traceback. File and GUI logging now continue when console echo fails.
- The local SDK contained `lib/bfd-plugins/libdep.a`. BFD tried to load this static archive as a Windows DLL, opening a Bad Image dialog during `psp-nm`. Packaging omits `.a` / `.la` files from that plugin directory, preserving DLL plugins and target libraries elsewhere. The original SDK is unchanged.

The frozen utility built all 1,416 C files with zero failures, validated 17 PSP import libraries and produced the same EBOOT/data hashes listed above. Python and PSPDEV were removed from PATH and PSPDEV was unset; the package supplied its compiler, runtime and helpers. The first build downloaded and verified the pinned upstream archive. The desktop layout was also captured and inspected.

The package's frozen resource probe passed disc/runtime checks, converted original ADX and AHX samples and packed a one-second H.264 movie sample. Host checks passed: 12 build tests, 4 preparation tests, 3 audio tests and 5 video tests. These cover the new compiler-priority, static-plugin and failed-console cases. The ZIP inventory and CRC were checked; no game EBOOT, rebased game data or static BFD plugin is distributed. Compiler BUILDINFO keeps version/source information while replacing the maintainer's installation paths.

Logs, probe reports, package inventory and generated launch files are under `build/auto validation/`. No GitHub release or PSP installation was changed. The complete audio/movie conversion was not repeated, and the generated EBOOT still needs PSP gameplay validation.
