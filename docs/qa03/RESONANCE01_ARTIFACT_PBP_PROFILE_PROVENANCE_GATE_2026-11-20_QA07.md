# QA07 B0/B2 — read-only PBP + object/source/profile manifest guard, real synthetic H0 tests (20 Nov 2026)

**Status:** Independently read frozen **T1** [`tools/psp_hw_debug.py` L44–79, L90–164](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py#L44-L79), [`tools/psp_link.py` L128–145/L203–228](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L124-L145) and [`tools/psp_xmb.py` L9–18](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_xmb.py#L9-L18). Added **actual executed host-only** test [`host_tests/resonance01_artifact_guard_qa07.py`](host_tests/resonance01_artifact_guard_qa07.py): **8/8 Python synthetic self-tests PASS** with temp/mock files in the isolated research container. Source size **10,824 bytes**, 228 lines, local SHA256 **`4ea00da63ccbf3eb9834fbb3bbe3c9c017725a0b36cba2c63a7089b8f61d99c5`**, Git blob **`f7d886d6989c20c89641bb8c3cd6cc54ae5a4c62`** exactly matched to the uploaded source, commit `19f3058`. **NOT a PSP build, source port patch, actual EBOOT verification, PPSSPP T2 or PSP Go T3.** No game assets uploaded.

## New source-level manifest gap: hashes are useful but not complete source provenance

- The frozen `psp_hw_debug.py` computes `manifest.json` hashes of: port `.c/.S/.h`, XMB artwork and `tools/psp_xmb.py`; the selected final original **game** `.o` files under `object_sha256`; and five artifacts `p4.elf`, `p4.prx`, `EBOOT.PBP`, `p4data.bin`, `p4str.bin`. It records `build`, `build_id`, `release`, `frame_timing`, `ge_dump`.
- It does **not** record in these fields the **actual** pinned original decomp Git commit, toolchain binary hash/PSPSDK state, initial `psp_census.py` FIXES/flags/changes, `psp_link.py` source hash and linker flags, full Git dirty state, or a verified selected Reg-vs-Diag-Lite `profile`. A manifest can therefore have all *present* file hashes correct but not fully specify **how to regenerate those objects**. `--release` does not imply Diag-Lite: frozen parser has no such switch.
- Exact [`tools/psp_xmb.py`](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_xmb.py#L9-L18) passes native `p4.prx` as **DATA.PSP** argument to `pack-pbp.exe`. The proposed read-only guard parses the 8 PBP section offsets and compares the actual embedded DATA.PSP **byte-for-byte** to the separate output `p4.prx`. This is an additional relation that simple file-by-file SHA checks do **not** prove.
- Neither SHA256 on artifacts nor PRX embedding verifies **what source function owns an ELF symbol, the native PSP linker ABI or the runtime `P4_BUILD_ID`**. `port/main.c` uses its embedded label; the build script reads first literal `#define P4_BUILD_ID` via regex. For a new `#if P4_DIAG_LITE` conditional, these values could disagree; Codex must confirm actual PSP output and runtime log.
- The in-game `p4_runtime_digests` are 2×SHA1 of `p4data.bin` and string offsets per [`psp_hw_debug.py` L123–143](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py#L123-L143); this helps catch bad asset triplets at boot, **not** source provenance or a preboot GE/Flight Recorder test. The guard cannot independently locate/read the compiled digest from a stripped PSP ELF and does not claim to.

## Script: portable, read-only and deliberately fails incomplete future profiles

Run on a **separate experimental project workspace**, after the full builder generated its manifest and verified original object list:

```powershell
# 1. FIRST: host-only synthetic regression, no ISO/PSPDEV needed
python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py --self-test

# 2. Historical frozen b249/perf06 bundle compatibility inspection (read-only).
# Reports WARN that full decomp/compiler provenance and profile identity are absent.
python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py --root . --manifest build/release_b249/manifest.json --expected-profile legacy

# 3. NEW RESONANCE01 strict sign-off AFTER Codex adds actual manifest.profile
# and manifest.provenance fields. Existing frozen builder will FAIL intentionally.
python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py --root . --manifest build/RESONANCE01-regular/manifest.json --expected-profile regular --strict-provenance
python docs/qa03/host_tests/resonance01_artifact_guard_qa07.py --root . --manifest build/RESONANCE01-diag-lite/manifest.json --expected-profile diag-lite --strict-provenance
```

The script reads/re-hashes artifacts from **manifest's directory**, source paths relative to project root, `build/obj` original objects, and `build/psp/objects.rsp`. It refuses missing/mismatched artifacts, PBP/PRX payload, changed source bytes, object-list vs manifest mismatch, unexpected active `.o` which the frozen `psp_link.py` would sweep into its link, false `profile`, `frame_timing`/`ge_dump` incorrectly masquerading as Regular/Diag-Lite, and (when strict) absent named provenance fields. Excludes the frozen link's known `src__cri__`, `src__rw__`, `src__renderware__` object prefixes for its active-list comparison. Paths with `..` or escape are rejected. **No writes in check mode**; `--self-test` creates/discards a separate system-temp mock project. Python **3.10+** for `Path.is_relative_to` (matches docs/building.md).

A sensible proposed `manifest.provenance` extension for Saturday records **nonempty** keys `port_commit`, `decomp_commit`, `pspsdk_commit`, `toolchain_id`, `compile_flags`, `link_flags`, `builder_hashes`. **The guard only checks that the keys are present/nonempty; it does NOT authenticate that those reported commit/toolchain/flags are true.** Stronger SHA/version/dirty-tree checking must be performed separately by Codex and shown in final audit. Existing manifest artifact hashes are still calculated and checked directly. `--expected-profile legacy` explicitly cannot attest correct Regular/Diag-Lite compilation.

**Warning:** A user-rehashed incorrect game data bundle can still satisfy an attacker-edited manifest; the game runtime's embedded SHA1 may then reject at startup. There is no offline proof of actual PSP loader/runtime identity; require PPSSPP cold boot and real Go T3 afterward. Never accept the script's PASS as SEC-01 GE memory-safe, functional fusion, a good FPS figure or a valid public release.

## Actual H0 synthetic test transcript (Python stdlib, no actual executable)

```text
PASS 01 matching mock artifacts and object provenance
PASS 02 mismatched game data triplet rejected
PASS 03 embedded PRX mismatch detected
PASS 04 stale extra compiled object detected
PASS 05 missing true Diag-Lite identity rejected
PASS 06 serial GE dump incorrectly called Diag-Lite rejected
PASS 07 missing strict provenance rejected
PASS 08 post-build source edit rejected
ALL 8 QA07 synthetic manifest guard checks PASS. NO PSP code tested.
```

All eight cases used **synthetic 40-byte-header PBP and mock PRX/ELF/game data** and a temp project. The negative cases *deliberately* produce the expected guard failure, not a real-world crash. This is genuine **executed H0** for the Python file code; no claim an actual release path existed in this runtime.

## Why Saturday should use it

- Gate B0 baseline provenance **before** any “Regular vs Diag-Lite” performance comparison. After successful compile the first task becomes **run this checker against actual outputs**; if the builder cannot satisfy strict profile/provenance, either extend its manifest and re-run on its own experimental worktree, or mark BLOCKED/UNVERIFIED. Never edit frozen perf06 to make the baseline manifest modern.
- This acts as a quick cheap **host read-only gate** after major build steps, not a reason to rerun all game decomp objects; the manifest contains actual object SHA. Only rebuild changed dependencies under the existing strict owner/ABI gate. If the checker rejects an object inventory, regenerate from the **isolated** experimental `build/obj` after identifying the stale source; don't weaken all symbols a second time blindly.
- For a new D1 worker and two profiles, require compiler profile hash and actual loaded EBOOT `P4_BUILD_ID` separately; the guard flags misdeclared profile but cannot disassemble PSP MIPS symbol owner. Verify `p4.elf` with PSP `psp-nm/objdump` and actual PPSSPP log.
- No hardware-only race, GE bank lifetime or silent cache fault is exposed by mock PBP files. **SEC-01 full GE writer and queue fence remains P0** ahead of any graphical pass and physical user testing.

Related B0 [source-path isolation audit](RESONANCE01_END_TO_END_MASTERPROMPT_PREFLIGHT_2026-11-20_QA07.md), [Codex starter](CODEX_START_HERE_RESONANCE01_2026-11-21.md), [master](RESONANCE01_CODEX_MASTERPROMPT_2026-11-21_READY_DRAFT.md), [20-case physical Go card](RESONANCE01_PSPGO_TEST_CARD_2026-11-21_DRAFT.md).
