# QA12 — Adversarial host gates, ELF owner collision and GE fault injection (2026-11-20)

**Scope:** QA-only Python source and documents on dev/v0.27-perf06. **Frozen runtime/game commit 29b0fcfdec70222a4052099b67fb4a11e8d8aff4 remains unchanged.** No private ISO, 120-pair ZIP, saves, source game binaries, native ELF/linker map, PPSSPP T2 or physical PSP Go T3 available. H0 means actually executed **host/synthetic** negative cases; T1 means inspected static source; no new hardware safety, FPS or fix is claimed.

## 1. QA11 audit: seven false-positive PASS conditions exposed (actual H0 on exact QA11)

- Exact GitHub [QA11 guard](host_tests/qa11_release_update_guard.py) Git blob 11680dc59d602b429d1a510690a35cd0a59174c9, SHA256 8cd25e4482c01f495cf039606bf80c82fcf0ba4db62154c524ceb108fcbad300: **local byte-identical copy** validated with git hash-object. Existing QA11 5/5 self-test normal and -O PASS, but self-test coverage was incomplete.
- New [adversarial reproduction](host_tests/qa12_adversarial_release_probe.py), SHA256 e4b9fc001aa74e92a9261dd8cc76586f090ae6f93c75e2171fcbba7b8e97928b, Git blob 8c048fb9246392ebf7cb3d854a239831ef342273: executed under **Python 3.13.5 normal and -O**, **7/9 deliberately malformed fixtures incorrectly accepted** by QA11. False-PASS cases: omitted mandatory EBOOT and p4str *also omitted from the self-described manifest*, forged build number, forged total bytes, zero movies, absent SOUND/audio manifest, and altered EBOOT **plus matching forged release.json SHA**. Two controls **correctly REJECTED**: unlisted old VIDEO payload and EBOOT tamper without editing manifest.
- Interpretation: a closed-file-set test over *only the self-authored release.json* cannot certify that the correct files or known-good contents exist. This is a **QA gate weakness**, not proof a genuine release is corrupt or a malicious attacker exists.

## 2. Strict independent-identity release guard: 22/22 genuine H0 pass

New read-only [QA12 strict guard](host_tests/qa12_strict_release_guard.py), local SHA256 f9fc499cd5152dedf2f64c71c947f6682a04a1e369a8fd9e69c3bbcfa4011959, Git blob ca188e3bfa37f43baca2138bd86c7a4cc4fc9c29; matching [22-test suite](host_tests/test_qa12_strict_release_guard.py), SHA256 2efe82c1d034f5ee32e7d24801905be87ab443e2a3ebfaf57c306709250bc217, Git blob fec84bba218d0d083d84448dfef87f4e22a8044e.

**Actually executed:** 22/22 unittest normal PASS; 22/22 unittest Python -O PASS; py_compile PASS. Cases include a real CLI mock success/failure and independently pinned movie-catalogue tamper and self-reference rejection, plus clean valid synthetic input, missing EBOOT/p4str/audio manifest, wrong independent build number, bytes mismatch, wrong or missing movie identity, same-time altered EBOOT+release hash, stale file, changed trusted manifest digest, duplicate JSON key, symlinked output root, traversal, and missing external trust reference.

**Actual isolated candidate CLI** (adapt build number, manifest hash and source-owned movie catalogue; no fictional values):

    python docs/qa03/host_tests/qa12_strict_release_guard.py --release-folder <isolated_release_folder> --trusted-manifest <independent_verified_build_manifest.json> --trusted-manifest-sha256 <previously_frozen_SHA256> --expected-build <expected_new_build> --original-movie-stems <original_ISO_derived_23_uppercase_stems.json> --original-movie-stems-sha256 <previously_frozen_catalogue_SHA256>

- External trusted build manifest must be **outside** candidate release folder; its SHA-256 must have been frozen independently **before** examining the release, not calculated post-tamper. It must describe build/release and SHA256 for EBOOT.PBP, p4data.bin, p4str.bin. A copy held outside but silently updated with the release is **not independent**.
- Movie list must be 23 **verified original ISO-derived distinct uppercase stems**, externally SHA-pinned **before** release verification, not invented by the released movie JSON names; no original game assets are written to the repo. The guard checks exact 23 .p4v stems, complete file-set closure, bytes, mandatory triplet, SOUND/audio_manifest.json, SHA, duplicate JSON keys, link-path escapes and manifest cross-consistency.
- **Limitations:** A passing gate proves consistency only with the supplied trusted references; cannot independently prove their origin, correctness of PSP MIPS symbol ownership, runtime boots, 23 movie playback quality, original audio completeness, media durability on ef0:/ms0:/ or absence of every subtle Windows junction/path hazard. Actual customer package NOT RUN. Retain old QA11 for historical coverage; QA12 strict supersedes it for final packaging signoff.

## 3. QA10 ELF owner same-basename adversarial collision

- Frozen manual/release link uses --allow-multiple-definition, stages duplicate object files and weakens native definitions. QA10 previous [map/nm checker](host_tests/qa10_elf_symbol_owner_gate.py) compared only Path(...).name when matching the actual link-map owner with inspected object source. A stale staged C:/build/stale/rwsky.o could pass its map-owner *candidate* condition when the inspected object was C:/build/candidate/rwsky.o. **QA10 still returned overall INCONCLUSIVE on unverified disassembly; this was a false-positive intermediate owner predicate, NOT a completed T2 proof.**
- [Source-extracted owner negative probe](host_tests/qa12_owner_path_probe.py) ran **3/3 normal and -O**: old basename test falsely classifies candidate PASS, corrected predicate explicitly INCONCLUSIVE on full-path mismatch, good identical full path remains a candidate PASS. This is a targeted extracted conditional trace, **not** an executed real PSP linker map.
- Updated [QA10 native owner guard](host_tests/qa10_elf_symbol_owner_gate.py) commit fb37954023477e8aaebab9835d720e068603fe26: full map/nm object paths must match for candidate range success; path collision returns **INCONCLUSIVE**, not invented success. QA10's internal 12-test original self-test used Python assert (disabled by -O); changed to explicit raising check, and added two path fixture checks. **Updated full QA10 self-test was NOT rerun on an exact local copy in this QA12 turn**; require that before Codex relies on revised test count.
- Codex must still generate actual per-profile GNU ld -Map, collect same-stage psp-nm -A, final psp-nm and MIPS psp-objdump -dr, inspect instruction delta/ABI, confirm correct PRX/EBOOT identity, not let --allow-multiple-definition or copied object aliases silently accept wrong owners.

## 4. GE model: 20/20 source-inspired fault-injection host assertions

[QA12 bounded model](host_tests/qa12_ge_fault_model.py), SHA256 f27669920788ece2eba728fe05e997678d2d60c6985043554fe7d4601a28814b, Git blob 54642c7c2626d04a38f361940289c870d283605f; [20-test suite](host_tests/test_qa12_ge_fault_model.py), SHA256 a63b72cd543a0808667deed4b933c8fd32a3d4b11101466605910198c6fe9f52, Git blob f6dc6bf245eaeacb5012cee6c4af69f2a56b501b. **Actual Python 3.13.5 normal 20/20 PASS, -O 20/20 PASS, py_compile PASS**. Includes both baseline negative witnesses and deliberately strict fail-closed alternative:
- Failed sceDisplaySetFrameBuf can advance baseline shown_seq without accepted_seq. Strict model refuses fake acceptance.
- Two watchdog continuations may select bank0 while first GE bank still live. Strict model halts new submissions pending a real retire fence; **no liveness/performance proof**.
- A failed GU start or a non-success synthetic serial-sync result does not permit strict candidate reuse. **Real PSP sceGuSync status mapping remains unverified**.
- Budget boundaries use 65,536-word list, 2 synthetic command words and 2 tail words, including exact boundary, negative, huge and zero-byte requests. This is **not** an exact libpspgu inline command footprint for real draw transactions. One initial off-by-one expected-value **failed** during test construction and was corrected **before** 20/20 re-run, not hidden or confused with runtime.
- GE-completed/display-not-yet-accepted state remains separately representable. Strict watchdog candidate is conservative and may stop a healthy display-late pipeline; **needs explicit recovery and bounded throughput plan before implementation**.

To rerun the **exact GitHub-published Python fixture sources**, using the active checkout:

    python -m unittest discover -s docs/qa03/host_tests -p 'test_qa12_*.py'
    python -O -m unittest discover -s docs/qa03/host_tests -p 'test_qa12_*.py'
    python docs/qa03/host_tests/qa12_adversarial_release_probe.py
    python docs/qa03/host_tests/qa12_owner_path_probe.py

Existing QA10 14/14 GE interleaving fixtures, QA07 16/16 C flip model, B0 QA08, SEC-01 writer census and D1 partial-write C suite remain separate; not replaced or rerun here.

## Saturday execution implications

1. **B0:** source worktree + private toolchain/ISO/assets/ELF provenance before runtime patches. Re-run updated QA10 full self-test normal/-O, then real final-link owner map/objdump per profile.
2. **B1:** GE P0 atomic full-list (including SDK inline writers) budget, FINISH/END tail, retired bank/vertex half fence, attempted-vs-accepted display ACK, serial no-reuse after sync error, and recovery behavior that does not create another deadlock. QA12 20 fixtures are **host design negatives only**, not validation of a native patch.
3. **B2:** D1 recorder separate bounded ring/worker, checked allocation, short/zero/full disk-write and 0..127 POST fail-close; recorded 'ACK' must mean CRC+reopen+device durability evidence, not old text-log head advance.
4. **Post-build:** strict QA12 release guard with independently pinned manifest and original ISO stem inventory; separate QA07 PBP/PRX guard and PPSSPP T2/real PSP Go T3 acceptance. No upload of game data or private images.

**NO-GO status today for any claim of stable RESONANCE01:** no modified executable, no toolchain-linked actual PSP binary, no original ISO manifest verified, no hardware tests. All findings are reproducible local host checks or static analysis. All runtime source remains frozen.
