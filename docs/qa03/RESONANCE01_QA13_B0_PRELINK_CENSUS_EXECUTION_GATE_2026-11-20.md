# QA13 — RESONANCE01 B0 pre-link census gate (2026-11-20)

**Status:** source-pinned T1 plus **actually executed host-only H0**. Frozen game-source commit 29b0fcfdec70222a4052099b67fb4a11e8d8aff4 unchanged. No private game ISO, Windows PSPDEV, native RESONANCE01 ELF, PPSSPP or physical PSP Go was available during this test. Synthetic fixtures are not device proof.

## New direct pre-link build gate

Original [psp_census.py L760–766, L868–902](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_census.py#L760-L902) mirrors all original C sources then compiles game objects named by rel.replace("/", "__")[:-2] + ".o", **without cleaning preexisting build/obj**. Subsequent [psp_link.py L127–162](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L127-L162) includes build/obj/*.o and **may weaken/mutate selected .o symbols before linking**. Thus even a zero-failure compile does not itself establish a clean, current linker input object set.

New read-only [qa13_census_prelink_gate.py](host_tests/qa13_census_prelink_gate.py), Git blob **a14b00f053b776d5532dfa09b6ae1ae41b6fbff4**, SHA256 **dc67211a6d8302f8edf3119884f65587d1e56c5c0696e364e7b9a1c2c50fa72c**, and matching [test_qa13_census_prelink_gate.py](host_tests/test_qa13_census_prelink_gate.py), blob **539a18ca2546150d4b1a20dc0590c4fc42ad20b0**, SHA256 **057818217542a02f7e8f1c6e09882a1493c1fb4881490bc6721819bd80a9b16d**.

- Reconstruct exact expected object names from mirrored build/census/src/**/*.c. Require same set as build/obj/*.o; refuse missing/extra, symlinks and collisions.
- Require five success rows in the compile report and an empty failed_compile.txt, with total reported source count equal to actual mirrored sources.
- Reject incomplete or non-MIPS32-little-endian relocatable ELF objects (magic, class, endianness, e_type=ET_REL, e_machine=EM_MIPS).
- Refuse object older than its mirrored source (fast stale heuristic; NOT source-byte attestation). Check before the linker mutates game .o files, not after.

**Actual local Python 3.13.5:** 16/16 positive and negative fixtures **PASS** normal, 16/16 **PASS with python -O**, py_compile PASS. The locally executed files were compared by git hash-object and match the exact published GitHub blobs. Negative cases include empty/failing compile reports, wrong source counts, zero/missing/stale/x86/wrong-type objects, source-newer-than-object, source and object symlinks. On the current incomplete /mnt/data research folder an actual CLI call exited **2 NO-GO** because build/census/src and build/obj are absent. No PSPDEV executable, local project checkout or original game ISO is present; no fake build was attempted.

**Limit:** Valid MIPS ELF32 headers and timestamps cannot prove ABI, source mapping, matching instruction body, actual PSPDEV compiler identity or correct PSP ELF symbol owners. The real Windows census and full source object inventory were **NOT RUN**. QA08 read-only preflight, QA10 final ELF map/owner/disasm and QA07 postbuild PBP artifact guard remain mandatory.

## Exact isolated Windows B0 order for Saturday Codex (not launched by this report)

1. Separate disposable RESONANCE01 worktree with its own full build/ and build/psp_dist; immutable perf06 EBOOT/p4data/p4str and private save backup must be OUTSIDE it. Verify exact pinned PS2 source, Windows PSPDEV tools+SDK and BUILDINFO, privately owned Persona 4 USA ISO, extracted retail executable full SHA1 **4eeec0360cf2715535d9f7e52eb69d786fb0158c**, clean worktree and unchanged control hashes. Run [QA08 B0 preflight](host_tests/resonance01_b0_preflight_qa08.py) with explicit real arguments. Any BLOCKED is NO-GO.
2. Run compile and **new pre-link gate before psp_link** in PowerShell, aborting on nonzero return:

       python tools/psp_census.py --compile
       if ($LASTEXITCODE -ne 0) { throw "B0 census compile failed" }
       python docs/qa03/host_tests/qa13_census_prelink_gate.py --workspace .
       if ($LASTEXITCODE -ne 0) { throw "B0 unsafe MIPS object inventory" }
       python tools/psp_ptr_proof.py
       if ($LASTEXITCODE -ne 0) { throw "B0 pointer proof failed" }
       python tools/psp_link.py
       if ($LASTEXITCODE -ne 0) { throw "B0 native link failed" }

3. Only after this, link separate actual RELEASE/DIAG profiles with truly different embedded P4_BUILD_ID and unique artifact manifests. Export matching per-profile GNU ld -Map and psp-nm -A original/port objects, final nm and psp-objdump -dr to QA10; confirm complete ABI/instruction body and full source/toolchain hashes. Frozen builder does not automatically emit all these proofs and '--out' does not isolate global build/psp_dist by itself.
4. QA07 strict source+object/PBP-PRX artifact guard, QA12 strict package closure with externally SHA-pinned build manifest plus original-ISO-derived 23 movie stems (catalogue itself also independently SHA-pinned). Do not generate or upload copyrighted assets to GitHub. Real PPSSPP T2 smoke then physical PSP Go T3/p50-p95 observations only after safety and device proof.
5. If real toolchain/private inputs absent, return **B0 NO-GO** and proceed only with isolated host/source research; do not manufacture EBOOT or claim RESONANCE01 success. SEC-01 GE capacity, bank retirement and display-accepted ACK remain P0; bounded D1 Flight Recorder next, then selectively measured UI/renderer/performance fixes.

**Build status:** No RESONANCE01 build created here. Docs/QA additions only. The 16/16 H0 fixtures establish a tested guard, not a repaired game runtime.
