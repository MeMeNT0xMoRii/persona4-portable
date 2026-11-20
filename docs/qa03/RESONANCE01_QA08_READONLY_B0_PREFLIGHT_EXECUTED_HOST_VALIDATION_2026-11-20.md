# QA08 — B0 read-only Windows worktree readiness checker and actually executed host tests (20 Nov 2026)

**Status T1+H0:** source inspections at exact frozen runtime 29b0fcfdec70222a4052099b67fb4a11e8d8aff4 of [docs/building.md](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/docs/building.md), [tools/psp_build.py](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_build.py), [tools/psp_prepare.py](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_prepare.py), [tools/psp_census.py](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_census.py), [tools/psp_link.py](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py) and [tools/psp_hw_debug.py](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_hw_debug.py). **Actually authored and executed** [Python stdlib read-only B0 guard](host_tests/resonance01_b0_preflight_qa08.py). Local Python 3.13.5 py_compile PASS, **10/10 synthetic tmp mock cases PASS**, deliberately incomplete Linux /mnt/data experiment **9 BLOCKED / exit 2 as expected**, and remote Git blob 007313161ff4dce03517738624be7aae9a95d6c1 **exact local source match** (SHA256 d9429f5e314b348aa39f626c48b0b340d85ad32d92ac415bebb93f417bafd667; Git create commit bf655e411bcce0afa4a1d26814ff766dfe19104c; file 11,166 bytes). This is H0 **host tool validation only**, not Windows integration, native PSP build, PPSSPP T2 or Go T3.

## 1. What the frozen source actually provides — and why another read-only gate matters

- [docs/building.md requirements](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/docs/building.md): manual developer build expects **Windows x64**, Python **3.10+**, original Persona4 USA ISO and original ELF full SHA-1 **4eeec0360cf2715535d9f7e52eb69d786fb0158c**, pinned original decomp Git checkout **363eab8a966d60a9773b45f330f2344329ec4f0a**, Windows **12 PSPDEV tools .exe**, SDK linker script, and a completely independent build tree. Tested reference PSP GCC 15.2.0 SDK snapshot 09f02b88b9f3, but executable presence ≠ verified exact toolchain.
- [psp_build.py L269–295](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_build.py#L269-L295) already has strong **toolchain_identity()** that hashes actual compiler+library files; **do not replace it with file-presence checks**. [psp_build.py L498–565](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_build.py#L498-L565) source-build GUI uses its own cache identity and deletes/rebuilds only its validated cache/work folder. Developer Codex checkout and source-build cache are different workflows. Do not assume both have same paths.
- The bundled [psp_prepare.py --check L488–554](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_prepare.py#L488-L554) **creates a report directory / writes a report**, though it does not compile or convert when --probe is omitted. Hence it is **not strictly read-only**. The new QA08 B0 script avoids file writes entirely in ordinary audit mode.
- Frozen manual [psp_census.py L868–877](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_census.py#L868-L877) does not clear old build/obj/*.o, [psp_link.py L128–145](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L128-L145) mutates selected compiled objects and hardcoded distro outputs, [psp_release.py L91–117](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_release.py#L91-L117) can overwrite build/psp_dist even with --no-build. New checker is **only an input gate**; preserve old perf06 control outside experimental worktree and never call those scripts until all B0 blockers are addressed.
- Frozen new [QA07 postbuild artifact verifier](host_tests/resonance01_artifact_guard_qa07.py) addresses a **different moment:** after a *real* build it checks EBOOT embedded PRX, object inventory and source/artifact hashes, with newly required actual manifest profile/provenance. Use QA08 B0 **BEFORE compiler**, QA07 real artifact guard **AFTER compiler**. They complement, not replace, the original source-build cache validator and actual ELF linked symbol analysis.

## 2. Read-only Windows execution, required sources, safety

Run the script **from the new disposable experiment checkout**. Example placeholders only, no private file content is committed:

    python docs/qa03/host_tests/resonance01_b0_preflight_qa08.py --self-test
    python docs/qa03/host_tests/resonance01_b0_preflight_qa08.py --workspace . --control-dir D:/P4P-frozen/perf06/PSP/GAME/P4 --control-hashes D:/Private/perf06_triplet_sha.json --pspdev D:/pspdev --decomp ext/Persona4-Decompilation --iso D:/Private/Persona4_USA.iso --retail-elf build/SLUS_217.82

**The checker does not create folders, edit paths, compile, update Git, download, extract or write a report file.** It rehashes the three control artifacts, optionally compares against a **previously and independently recorded private SHA256 JSON**, rejects a nested/same worktree; detects missing manual build scripts, missing Windows PSPDEV binaries and linkfile.prx; checks exact clean decomp Git HEAD, experimental checkout Git status and frozen-source diff; checks only ISO file presence (NOT ISO integrity) and full SHA1 of already extracted retail executable; warns about preexisting game .o and hardcoded build/psp_dist folder. It produces PASS/WARN/BLOCKED statuses **on stdout only**, exit **2 if any BLOCKED**. For a fresh environment with no private tools, STOP native build, continue independent source-only host/mock testing. Self-test uses a disposable Python temporary mock directory only.

**Security limitation:** Tools’ existence does **not** prove their bytes/trust or correct Windows arch. A previously pinned control SHA may come from the same compromised input if not created independently. A clean decomp Git HEAD does not prove absent ignored files, and an empty native diff against frozen source does not prove local untracked/new generated objects. The script does **not** execute psp-gcc --version (deliberately read-only), verify SDK binary hashes, inspect complete ISO contents, parse or disassemble actual PRX, prove installed 64MiB setting, or run game tests. Follow on with psp_build.toolchain_identity, full original ELF check, generated-object inventory, separate release manifest, psp-nm/objdump owner and actual runtime ID. The source-build ZIP may lack a Git metadata checkout: this checker targets the **manual developer worktree**, not the GUI utility’s ZIP archive mode; that flow already verifies pinned tree hash b1a5681e7b30280b558c2deb438b2815b414eee4e6a59da55e551778ead45bc0.

**Special caution:** This Python script prints the three private SHA256 values locally so the developer can pin them. **Do not upload its raw output logs, private filesystem paths, ISO/save files or golden screenshots to public GitHub.** Baseline hashes are local evidence; no actual baseline files were available in our research runtime.

## 3. H0 transcript and acceptance gates

    PASS 01 valid three-file mock control
    PASS 02 separate project trees
    PASS 03 same project rejected
    PASS 04 two different immutable pins
    PASS 05 Windows-specific compiler inventory
    PASS 06 destructive stages known
    PASS 07 tampered data SHA found
    PASS 08 missing data found
    PASS 09 empty data found
    PASS 10 mock is not genuine retail ELF
    ALL 10/10 QA08 synthetic B0 checks PASS. No PSPDEV/PSP build was run.

Separate negative integration **ACTUALLY RUN** on the local incomplete Linux research folder, with no private files:

    NO-GO: 9 B0 build blocker(s). Source-only investigation allowed.
    process exit code = 2

The mock self-tests exercise standalone pure path/control/hash checks; they **do not** simulate a full positive Windows PSPDEV+ISO+Git integration and do **not** prove the script has no unforeseen issue in a real Windows environment. Actual Windows full B0 PASS remains **NOT RUN**, requiring user/Codex access. Compiling a real EBOOT remains NOT RUN.

## 4. Saturday decision

1. Read short starter and master without rereading all WIP. Run B0 QA08 on new experimental checkout only and review every BLOCKED/WARN.
2. If no exact Windows toolchain, private retail input, pinned decomp or control: **NO native build**; continue isolated static/host tasks while reporting dependency. Do not fake a build.
3. If all inputs ready: document hashes/versions and no overlapping control/worktree; a **fresh full census** inside disposable experiment build/obj, then psp_ptr_proof, psp_link (mutates own objs), real native release builder, real ELF owner check, true profile/manifest. After compilation run [QA07 real artifact guard](host_tests/resonance01_artifact_guard_qa07.py), and only then T2 smoke / T3 user Go.
4. D1 static ring is **not** guaranteed to link with fixed retail image; see [QA08 RAM/VRAM budget](RESONANCE01_QA08_PSPGO_MEMORY_BUDGET_AND_DIAG_LITE_ALLOCATION_GATE_2026-11-20.md). Source safety (all GE emitters and bank retirement) remains a hard P0 gate. The classifier / beta bot is later and only classifies validated incidents.

**Rollback / privacy:** B0 script is read-only in audit mode; no game runtime changes. Preserve freeze control and all saves. Do not publish a self-contained update without user's complete matching private assets.

Linked [short starter](CODEX_START_HERE_RESONANCE01_2026-11-21.md), [master](RESONANCE01_CODEX_MASTERPROMPT_2026-11-21_READY_DRAFT.md), [QA08 D1 triage](RESONANCE01_QA08_D1_INCIDENT_TRIAGE_DECISION_TREE_AND_BOT_PACKET_2026-11-20.md).
