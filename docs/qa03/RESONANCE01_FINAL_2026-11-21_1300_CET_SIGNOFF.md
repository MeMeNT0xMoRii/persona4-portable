# RESONANCE01 — final 21 Nov 2026 ~13:00 CET release preflight (bounded audit)

**Decision: CONDITIONAL GO for Codex G0 environment inventory ONLY; NO-GO for claims of release, GE safety, PSP Go approval, or verified FPS.** This is a documentation-only preflight, not a build, native test, or comprehensive certification. Frozen runtime source: `29b0fcfdec70222a4052099b67fb4a11e8d8aff4`; original PS2 decomp: `363eab8a966d60a9773b45f330f2344329ec4f0a`. Current branch `dev/v0.27-perf06` contains later docs and host QA; these do not establish a new EBOOT.

## Exact scope / coverage inventory

| Area | Actual verification this pass | Remaining |
|---|---|---|
| LIVE handoff | Read full GitHub blob `af9d23129fe6223198ccb84c63c696f2ab32d84d`, 313334 chars; newest QA18 top entry | Older long history NOT line-by-line audited |
| Priority code owners | Read GitHub blob `c0d2b6ddda91bb041207fa5c58bc4e07fe4f6a13`, 118347 chars | All 17 owners NOT independently re-proven |
| QA matrix | Read blob `bce0c382f603595b58e43a5d67166ef243f91b58`, 115623 chars | All 38 negative cases NOT executed or row-by-row independently reviewed |
| QA03 README | Read blob `50c323c114b3081c41ce1cffbce79b850ee3b592` | Every historical claim NOT revalidated |
| perf06 forensics | Read blob `cb6c24773c22d70504c9bdaf8b6fb46a455caca3` | Private regular perf06 EBOOT/p4data/p4str bytes NOT available |
| Launch Card / Starter / Master | Read full blobs `01d69abca1b45cf6bd14c37987c07d39464e616f`, `cd16484ab29add56975c2b61b3520c025209ed8b`, `7511ee28c8b40ba81235679b4228a76dcc4ecefd` | No Codex execution or external independent master audit rerun |
| QA09 + 120-pair table | Read QA09 blob `333798590f8e7487ab69b629d35acc5933b01f5e` and 120-pair table blob `e159b31aac457a02e4872411435b3041591fa221` | Private ZIP NOT available to this pass; C0 120/120 **historically reported**, C1 PARTIAL, C2 NOT RUN |
| QA11–QA15 | Read GitHub QA11 `95b6fc257a630ea5289b9eac3975722513874aed`, QA12 `757f1311f4d55dcb3b2e93e2089bd29e3719d000`, QA13 `29308097327c88af6f1e777bb87758d5b81cf0e4`, QA14 `c57471e16a78e42b291ca485c68ba7a6d59b385f`, QA15 `524cacb102454e10efd188a3d104e569b2f68e5d` | Historical host outcomes not rerun in this pass |
| QA16/QA18 | Read current Launch Card and QA18 report blob `862439d7babe5d34f84cc0b2495c6512981e6c5b`; guessed filename `RESONANCE01_QA16_LAUNCH_CARD_2026-11-21.md` returns 404; actual launch card exists | Other QA16 documents NOT exhaustively inventoried |
| QA10 host code | Read exact GitHub scripts `qa10_ge_schedule_model.py` blob `55f957735b5bdf8f7c67d1db20a2ef819b31d336`, `qa10_elf_symbol_owner_gate.py` blob `66ebe028755112dea662bc74c884ba98fd75ccd2`; QA13 gate blob `a14b00f053b776d5532dfa09b6ae1ae41b6fbff4` | QA10 14/14 GE and 14/14 ELF + six CLI cases are historical H0, NOT re-executed or PSP evidence |
| Frozen perf06 source | Read exact `port/rwsky.c` blob `9d6c30d74a7cfd336bc871f8a5258481cdffb7ee`, and `tools/psp_hw_debug.py` blob `38fbedc0ec5f5dc3c69c5d22155de1130977747b` | Full original/decomp, all native source, SDK inlined GE writers, actual ELF/map/disassembly NOT reviewed |
| Changelog | Read `docs/qa03/CHANGELOG.md` blob `f4055a9e7c89f8b3aea7f731ccec65ad7a0accaf` | Complete `docs/` tree inventory and exhaustive Markdown link checker NOT completed: connector has no recursive tree/list endpoint in this pass |
| Other QA03 markdown | **NOT REVIEWED exhaustively**: save/HOME, D1, Compendium, reinforcement, RTT, fusion, all individual UI notes, G00–G19, and 38-case detailed matrix | Consult issue-specific source notes on demand, do not assert comprehensive coverage |

## Verified source-backed hazards and falsifiers

- **SEC-01 P0:** frozen `port/rwsky.c` lines 650–663: `flip_main` updates `shown_seq` even when `sceDisplaySetFrameBuf` returns negative; lines 1504–1533: watchdog permits progress after a stalled GE/display gate; lines 1594–1614 publish/present framebuffer state. This is T1 control-flow evidence, not a reproduced PSP Go corruption. The QA10 abstract model historically covered 188 depth-bounded states, 14/14 H0 tests; it cannot prove actual PSP GE bank retirement. Require coherent seq+fb+bank+generation, accepted display result, GE retirement, bounded liveness, SDK/inlined writer audit and negative controls. If any remains unknown: `P0_UNVERIFIED`, NO hardware promotion. Roll back to untouched perf06.
- **B0 final-link:** frozen `tools/psp_hw_debug.py` contains `assert`-based image/offset checks at lines 83, 135, 143; Python `-O` bypasses these. Final symbol owner/map must refer to the **second digest relink**, not preliminary `psp_link` output. QA13 exact census gate runs after successful census and before linker mutation, invoked by ABSOLUTE `DOCS_ROOT/docs/qa03/host_tests/qa13_census_prelink_gate.py --workspace SOURCE_ROOT`. Never assume QA scripts exist in frozen source root. Require independent source/asset SHA and clean profile output.
- **QA11/12:** historical adversarial QA12 demonstrated QA11 false-accepted 7/9 malformed fixtures; QA11 cannot be sole release gate. QA12 strict complete UPDATE requires independent frozen source manifest and 23-movie catalogue identity; code-only first build should preserve and separately verify existing private perf06 resources, not reconvert 23 movies/17k audio by default.
- **UI:** QA09's 120/120 C0 inventory and 15 HIGH / 38 geometry / 52 comparable / 15 limited are historical documented assessments; C1 PARTIAL, C2 NOT RUN. First local wheel candidate approximately 111x111 at (64,203), with text/stripe/negative controls. Golden-only gameplay/assets excluded.
- **Performance:** p50/p95/p99 must measure **accepted presented** frames on real PSP Go (T3), with Junes F009_004 vs school F006_002, fog/battle/RTT controls, GPU and memory headroom. No fresh T2/T3 data.
- **D1:** first-run ring 512x64 = 32768 bytes, counters <=4096, snapshot OFF, <=49152 bytes **total additional RAM target** including worker stack and overhead. The earlier 64KiB design envelope is not a pass. Failure: `C_NOT_BUILT`, preserve B partial and log missing proof.

## Link and consistency checks

Direct GitHub fetch confirmed the listed paths and blobs; the guessed QA16 filename returned 404 and is NOT an authoritative link. An attempted broad relative-link checker did not complete due connector tool-call limits; **no claim of zero broken links**. Historical README and starter contain large stale context by design; current QA18 Launch Card takes precedence. No contradictory current G0/G1/G2/G3/G5 routing identified in inspected heads; entire long Markdown corpus NOT checked. Do not claim complete documentation certification.

## Compact Codex launch / decision

1. Read **only** `docs/qa03/RESONANCE01_CODEX_LAUNCH_CARD_2026-11-21.md` initially. Pin one immutable `DOCS_SHA`, separate read-only `DOCS_ROOT` and writable isolated frozen `SOURCE_ROOT`; confirm private Windows PSPDEV/ISO and matching artifact provenance. Missing prerequisites: `B0_NO_GO`.
2. G0→G1 clean build and prelink QA13 → G2 bounded SEC-01 safety → G3 Diag-Lite feasibility → G5 report. G4 graphics/UI/performance optional and normally deferred. No first-run Fusion EXP, Compendium speculation or broad cosmetic edits. Never represent host H0 as PSP execution.
3. First code-only matching EBOOT/data/strings over separately checked complete private perf06 resources. Final ELF owner and digest-relink map; independent manifest/catalogue only for complete UPDATE. No stale output reuse.
4. Mark `BUILD_FAILED`, `BUILT_UNTESTED`, `P0_UNVERIFIED`, `PARTIAL_DELIVERY/C_NOT_BUILT`, `T2_VALIDATED`, `T3_VERIFIED`, `PROVENANCE_VERIFIED` and `REPRODUCIBLE_VERIFIED` precisely. Record unrun negative controls and preserve rollback. No user PSP Go promotion until actual safe gate and user T3.

**Final signoff:** G0 planning **CONDITIONAL GO**; implementation safety/release **NO-GO until gates pass**. No private files, runtime code, build, merge or release changed during this audit.
