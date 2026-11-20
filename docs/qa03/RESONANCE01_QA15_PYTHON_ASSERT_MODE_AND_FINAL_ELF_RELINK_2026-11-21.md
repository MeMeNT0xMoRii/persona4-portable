# QA15 — Build-script Python optimization trap + final ELF second-link provenance (2026-11-21)

**Scope/evidence:** T1 source-verified immutable runtime/build scripts at commit `29b0fcfdec70222a4052099b67fb4a11e8d8aff4`; H0 actual local isolated Python behavior and 7/7 host tests under normal and `-O`. No executable PSP compilation, original user ISO, Windows toolchain, PPSSPP or physical PSP Go run. Do not claim the current build was ever executed with optimizations enabled or has been corrupted.

## QA15-01: Python `-O` can silently bypass critical native build guards (T1 + H0)

Direct frozen-source line owners:

- [`tools/psp_census.py` L692–712](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_census.py#L692-L712) contains two `assert old in text` preconditions before applying exact source/ASM shim substitutions. Under Python `-O`, a modified upstream input could silently keep the wrong source pattern instead of stopping.
- [`tools/psp_census.py` L891–902](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_census.py#L891-L902) asserts group source count matches total, separately from explicit compilation return code. This specific check also disappears under `-O`.
- [`tools/psp_link.py` L211–215](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L211-L215) asserts `p4_image == PSP_IMAGE`, a safety check for fixed retail image layout; **`-O` disables it**.
- [`tools/psp_hw_debug.py` L82–85, L132–143](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py#L82-L143) asserts unique linker script insertion anchor, fixed retail image at expected load offset, and matching binary string offset table. These checks vanish under `-O`.
- [`tools/psp_make_data.py` L100–104](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_make_data.py#L100-L104) asserts source retail pointer word matches rebase-table expected word before patching the data image. This is a high-value fail-fast boundary and vanishes under `-O`.

**H0 actual demonstration:** Executing `assert False, 'MUST_STOP'; print('DANGEROUSLY_CONTINUED')` in a normal interpreter returned exit **1** and AssertionError; executing identical command with Python `-O` returned exit **0** and printed `DANGEROUSLY_CONTINUED`. This proves Python optimization semantics, **not** that a real PSP build already failed.

New tiny [QA15 read-only builder mode gate](host_tests/qa15_python_build_mode_gate.py) Git blob `9e52200e9cad2a1c2d64663ffc074f2927c07c50`, SHA256 `c08aabb4137ba4e93892fbce88de3c2ebdac3fb6b1bab27cd96953075ee40066`, plus [7 test fixtures](host_tests/test_qa15_python_build_mode_gate.py) blob `10e6e022b071c863976f25ef0d8ac937ce38a836`, SHA256 `6c68d55e06ca2e979b37aed419fa45b8fe46f78f6c007ae79d75447d56afa5e2`. **Actually executed Python normal 7/7 PASS, Python -O 7/7 PASS, py_compile PASS**, hashes locally compared to published GitHub blobs. Tests include normal, `-O`, `-OO`, `PYTHONOPTIMIZE`, and actual subprocess return-code checks. The guard **returns 2** if its interpreter is optimized or `PYTHONOPTIMIZE` requests optimization. Its PASS proves its **own** invocation only; Codex must run all original Python builder processes with normal interpreter flags and unchanged environment.

**Actual Windows PowerShell Stage0 contract:**

    $env:PYTHONOPTIMIZE = '0'
    python docs/qa03/host_tests/qa15_python_build_mode_gate.py
    if ($LASTEXITCODE -ne 0) { throw 'B0 python assertion guards disabled' }
    python tools/psp_census.py --compile
    if ($LASTEXITCODE -ne 0) { throw 'B0 failed compiler' }
    python docs/qa03/host_tests/qa13_census_prelink_gate.py --workspace .
    if ($LASTEXITCODE -ne 0) { throw 'B0 unsafe object inventory' }
    python tools/psp_ptr_proof.py
    if ($LASTEXITCODE -ne 0) { throw 'B0 pointer proof failed' }
    python tools/psp_link.py
    if ($LASTEXITCODE -ne 0) { throw 'B0 linker stage failed' }
    python tools/psp_hw_debug.py --release --out <NEW_ISOLATED_EMPTY_RELEASE_DIR>
    if ($LASTEXITCODE -ne 0) { throw 'B0 EBOOT release link failed' }

**Better actual code correction when authorized:** replace critical `assert` guard conditions in affected source-build scripts **inside an isolated RESONANCE01 branch** with explicit `if ...: raise ValueError/RuntimeError`, retest both normal and optimized modes and verify behavior remains unchanged on valid sources. Until then, never use `python -O`, `-OO` or nonzero `PYTHONOPTIMIZE` for the original builders. Do not modify frozen perf06 baseline or accept a release just because commands exited zero.

## QA15-02: final ELF owner must follow the second release link (T1)

Frozen [`tools/psp_hw_debug.py` L109–143](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py#L109-L143) assembles **40 placeholder digest bytes**, calls `psp-gcc` link first, generates digest from first linked data/string, overwrites the digest object with real SHA1 bytes, calls **`run(*link)` a SECOND time into the same ELF path**, and only afterward runs `psp-fixup-imports`, `psp-nm`, `psp-prxgen`, re-evaluates final data and constructs output `p4.elf`/`p4.prx`/EBOOT and manifest. An intermediate `psp_link.py` `build/psp/p4.elf` is a *different link artifact* as well.

**New practical owner acceptance:** add `-Wl,-Map,<unique_stage>/p4_final.map` to the actual final release link invocation(s), collect the map **after the second link completed**; after import fixup collect final release `p4.elf` SHA256, `psp-nm -an`, `psp-objdump -dr`, `psp-nm -A --defined-only` hashes of the **exact staged objects used in that second link**, and QA07 PBP/PRX/manifest hashes. Record build/profile/first-vs-final link stage filenames; do not silently use psp_link.py's preliminary ELF or an owner map captured before digest relink. The scratch workspace is created by tempfile.mkdtemp and is outside the permanent release folder by default; preserve only essential native symbol provenance **privately** (not retail ISO or user saves), with reproducible stage hashes. A genuine QA10 `INCONCLUSIVE` exit3 remains NO PROMOTION, not success.

**Related risk:** `psp_hw_debug.py` permits pre-existing `--out` folders, overwrites selected expected files late, and writes the manifest only after compilation; after an interrupted build an old candidate may still sit there. For deterministic releases require a fresh empty profile-specific release path or explicit verified unique run ID and never take stale artifacts from it after a nonzero builder exit. This is T1 control-flow risk, not a reproduced bad build.

## Net effect / priority

QA15 catches **two B0 false-confidence traps** without adding new gameplay features. It should be a short **mandatory prebuild check** alongside QA13, and a final-link owner note in the current master, **not a new broad rebuild program**. Preserve G0–G5 scope: one real stable code-only RESONANCE01 triplet, P0 GE safe implementation, bounded Diag-Lite if proven, at most one graphics/perf candidate with T2 and later user T3. Frozen perf06 remains unchanged.
