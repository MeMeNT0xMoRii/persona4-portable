# Local EBOOT generation without compiling the recovered game

Status on 2026-11-08: **feasibility audit implemented; a working generator is not available**. The existing Windows preparer still supplies an EBOOT and does not generate its code from the ISO. Neither that package nor the public release was changed by this experiment.

Subsequent work added [source-build preparation](prepare_game.md), which compiles recovered C locally. That is a different approach from the translation-only generator assessed here. The older public prebuilt-EBOOT download has been withdrawn.

The proposed generator would take the retail ELF from the user's own disc, translate its R5900 code locally, connect the PSP runtime and package the resulting executable. Precompiled recovered game objects or a patch containing their replacement machine code would not meet the objective of avoiding distribution of that game-derived code.

## Measured limitations

`tools/psp_generator_audit.py` checks the full retail ELF SHA-1, reads upstream function names/boundaries, then applies the current translator's checks without compiling recovered C. Optional symbols from port-only objects identify candidate runtime replacements. No game assembly, game object or EBOOT is emitted.

With the pinned upstream checkout and exports from the clean b164 port objects, the scan found 13,105 known code starts:

| Classification | Count |
| --- | ---: |
| Accepted by current translation checks | 8,169 |
| Candidate port-runtime replacements | 176 |
| SDK boundaries | 447 |
| CRI boundaries | 3,741 |
| Other translation refusals | 572 |

The 572 refusals include MMI (121), fall-through handling (120), 64-bit shifts (116), VU0 (69), hardware registers (40), syscall/mfsa (31), 128-bit memory (30), 64-bit memory (25) and other constructs. Each row records only the first detected problem.

These counts are **not** a compatibility percentage or a count of required missing implementations. They include unreachable and replaceable routines. Accepted translation does not establish instruction semantics, ABI compatibility, complete call resolution or runtime correctness. SDK/CRI boundaries need explicit replacement/usage analysis rather than blanket translation.

There is already a direct startup blocker:

```text
kwlnBoot (001002c0)
  -> kwlnMainLoop (00101350)
     -> translator refuses a PS2 hardware-register access
```

The direct-call traversal stops at unsupported boundaries and runtime candidates. It cannot account for all indirect calls, callbacks or pointer tables. Consequently, passing that traversal would still not establish that a game works. The report explicitly keeps `eboot_generation_supported` false.

## Run the audit

The pinned upstream checkout and `build/SLUS_217.82` are required, as described in [the build guide](building.md). The audit itself needs no C compilation or PSP compiler:

```powershell
python tools/psp_generator_audit.py
```

For the runtime-aware scan, export text symbols from **only** our runtime objects. If those objects already exist, this step does not compile anything:

```powershell
$runtimeNames = 'main','platform','rwsky','rwdata','rwmath','files','ee','pad','memcard','video','video_game','audio','audio_game','video_nal'
$runtimeObjects = $runtimeNames | ForEach-Object { "build/psp/$_.o" }
& "$env:PSPDEV/bin/psp-nm.exe" --defined-only @runtimeObjects | Set-Content -Encoding ASCII build/port_symbols.txt
python tools/psp_generator_audit.py --runtime-symbols build/port_symbols.txt
```

Do not supply symbols from recovered game objects, `reasm.o`, `stubs.o` or the combined game ELF as if they were our runtime. An export is only a candidate: its correspondence, provenance and EE-to-PSP calling convention still need checking.

Reports are written to `build/generator_audit/audit.json` and `functions.tsv`. The recorded run also keeps its actual input exports in `runtime-symbols.txt`. Reports contain names/addresses and classifications, not game instruction payloads.

Regression checks:

```powershell
python -m unittest discover -s tools -p test_generator_audit.py
```

## Work required for a generator

1. Extract names, function boundaries and relocation metadata independently of any locally compiled recovered-game objects. Review the provenance of metadata to be distributed.
2. Replace required PS2 system interactions with explicit runtime bridges, starting with the main loop. Validate the ABI at every native/translated boundary.
3. Extend translation for needed VU0/MMI and real 64/128-bit operations, including branches, delay slots, register state and memory layout. The existing fallback translator is specialized for a smaller C-linked function set.
4. Preserve the port's game fixes and adaptations in the new translation path. Fixes in the census script currently act on C sources and do not automatically apply to retail instructions.
5. Build the data/function-pointer relocations against the new output, then assemble/link/package locally without any precompiled recovered-game inputs. Refuse unresolved required routines rather than silently emitting logging stubs.
6. Validate the resulting image in an emulator and on PSP, including rendering, animation, sound, movies, saving and gameplay transitions. A successful link alone is insufficient.

This is a substantial new execution path, not a packaging-only change. Keep the existing build usable while developing it. Local generation changes what is distributed; it does not itself establish legal clearance for a generator or its dependencies.
