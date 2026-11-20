# QA07 — Fusion Skill Selection SESSION model: actual compiled C11 host tests, 18/18 PASS

**2026-11-20, tested in this interactive audit, not tomorrow's PSP Go.** Source [host_tests/fusion_session_contract_resonance01.c](host_tests/fusion_session_contract_resonance01.c), latest commit **7ba744b6f8fbfe53cddb8a59f422c69b5ecfe7a6**, exact Git blob **0f979c59ef89fa68f33c18b189741c78c8e88ab2**. Local 8,144-byte file SHA256 **9c92a0a03c7b021e840e27aa8d90c3ebb2ed8752270a1bab81d6f5c8b780a0a6**, independently verified locally with git hash-object and by freshly fetching GitHub SHA and 8,144-byte source. Source was independently written as a **test-only model**, not an adaptation/copied file from the GPL PS2 manual inheritance mod.

Actual environment: Linux host GCC Debian **14.2.0**, Clang **17.0.0**. Both compiled successfully with strict warnings and executed their full test main. Clang ran with **AddressSanitizer + UndefinedBehaviorSanitizer** with no reported issue on covered paths. Compiler instructions that were actually used:

    cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
      docs/qa03/host_tests/fusion_session_contract_resonance01.c -o fusion_host_gcc
    ./fusion_host_gcc
    clang -std=c11 -O1 -g -fsanitize=address,undefined \
      -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic \
      docs/qa03/host_tests/fusion_session_contract_resonance01.c -o fusion_host_san
    ./fusion_host_san

**GCC: 18/18 PASS; Clang/ASan+UBSan: 18/18 PASS.** Each output has 18 PASS lines + a final summary; logs were byte-identical SHA256 **5348f7ac7a22f6e54dfad498fb7433604d7d800f53b0b9a1b8b99865f5216f88**.

The initial source's TEST(name,code) macro rejected C initializer commas (compile failure in both compilers). Fixed to variadic TEST(name,...) so test blocks are accepted. The corrected, exactly tested file was committed after the failed compile (7ba744b6...). This matters: a source being on GitHub was **not** proof it compiled until this test correction.

| Test | Asserted invariant | Result |
| ---: | --- | --- |
| 1 | Dedup across two parents, existing result skill excluded | PASS |
| 2 | Synthetic zero-weight skills are not selectable | PASS |
| 3 | Repeated choice of a skill rejected | PASS |
| 4 | Premature commit cannot mutate result | PASS |
| 5 | Exactly three valid selections commit once with correct mask | PASS |
| 6 | Cancel during partial choice leaves original result byte-identical | PASS |
| 7 | Changing parent generation blocks stale session commit | PASS |
| 8 | Zero/unknown skill ID denied | PASS |
| 9 | Requested slots cap at free result slots | PASS |
| 10 | Full eight-skill result has no extra inherited slots | PASS |
| 11 | Zero eligible skills leaves result intact | PASS |
| 12 | Six 8-slot parents fit exactly 48 unique candidates | PASS |
| 13 | Seven parents rejected before any scan | PASS |
| 14 | One-parent input rejected by the GUI contract | PASS |
| 15 | Mode3 triangle and mode6 share safe bounded session structure | PASS |
| 16 | Eight inherited bits occupy exactly 0xFF mask / eight slots | PASS |
| 17 | Unexpected external result-record mutation blocks commit | PASS |
| 18 | Selection count cannot exceed original requested quota | PASS |

## Scope and what this deliberately does NOT validate

- Synthetic legality uses **id % 7 != 0**, only a mock predicate. Real P4 [fclSkillWeight and fclSkillPickCount](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Event/Fcl/fclCombineMisc.c#L447-L480) and their PS2-US data tables **are not loaded or tested**. The session receives an external original_picks number, not the real tier table.
- A test-only Skills object has only eight uint16_t skill IDs. It is **not the entire original 0x30-byte Persona record**, a real work slot, inherited mask at offset +0x2E4, save data, UI surface, clock, actual pad controller or GE list. No source scene, overlay and input switching or Igor/accident/triangle/multi flow has been integrated into EBOOT.
- The model has no actual frame/tick, task ID pointer lifetime, PSP Allegrex ABI, compiler/linker, GE/VRAM, callback mailbox, IO or concurrency. There is no in-game perf measurement, actual selection, save roundtrip, emulator or PSP Go proof.
- It **does not fix original Compendium NULL cache risk** or the source-level preview-trigger trap. A native implementation must attach to confirmed state transitions rather than hooking fclFuseRollPersona globally. See [full fusion source audit](FUSION_RESONANCE01_MANUAL_INHERITANCE_IMPLEMENTATION_REAUDIT_2026-11-20_WIP.md).

## Codex Saturday engineering gate

Before touching game source, rebuild and rerun this exact 18-case host test and 23-case existing Flight Recorder host test, then implement a **feature-flagged, clean-room, task-owned native session** with source snapshot, actual weight table, original pick quota and atomic result+mask commit. Validate on PPSSPP T2 for 2,3,4,5,6 input and full cancel/save lifecycle; only then enable inside regular RESONANCE01. If not ready, stable regular and independent diagnostic EBOOT remain usable and fusion-exp must be clearly labeled. Actual PSP Go T3 user tests are still needed.

**Results are H1 host compiled model, NOT a tested manual fusion mod in the game.**
