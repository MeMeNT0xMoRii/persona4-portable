# QA03 — Manual Fusion Skill Selection and GameBanana compatibility (source audit)

**2026-11-19. Research only.** Runtime baseline commit 29b0fcfdec70222a4052099b67fb4a11e8d8aff4; later branch changes are docs and tooling, not a newly proven EBOOT. No gameplay-source edits, builds, PPSSPP/PSP Go testing or third-party game/mod asset uploads. Evidence T1 = verified source; T0 = idea/hypothesis. Golden assets/UI are NOT PS2 Vanilla compatibility proof.

## A. Verified PS2 manual skill inheritance source — high potential, separate optional module

- Actual P4 PS2 mod by teolicht505: https://github.com/teolicht505/-P4-Manual-Skill-Inheritance (README: CLPS2C compiler 1.0.5; main includes customGlobals.clps2c, inheritanceLogic.clps2c, drawManualInheritance.clps2c). Original PS2 assembly and code caves **will not run on PSP Allegrex**, and none of the PS2 absolute addresses is a native PSP address.
- Exact source SHA by file at research time: inheritanceLogic.clps2c ff42f7f9dd26c5e5c491f04ad2e9fa6c95e237cf; drawManualInheritance.clps2c 2e67ca01076e67d4bb7980f961a1f700a68a1399; customGlobals.clps2c 29358b20a337339489f8c744ea95ae13d11abf78.
- **License conflict:** the author's repository contains GPL-3.0 LICENSE; the P4P fork LICENSE is MIT. Avoid copying/adapting actual GPL source into an MIT-only released binary without a licensing decision. Safest: independent native implementation grounded in the original game's known behavior, or request alternate permission and review combined license requirements. Attribution still due; do not include third-party copyrighted assets.

| Original source / runtime mapping | Proof from pinned original Vanilla PS2 Decomp | How to use on native PSP (T0) |
| --- | --- | --- |
| Mod hook retail 0x00313034 (inherited skill add) | src/Event/Fcl/fclCombineMisc.c, FUN_00312C60 **fclFuseRollPersona** (~L483–570) creates unique skill pool, chooses count by tier, weights eligibility and adds skills; mod inheritanceLogic.clps2c L2,99 | Optional no-RNG selection path **before committing** result. Preserve defaults with feature off. |
| Inheritance whitelist/weights/count | fclCombineMisc.c **fclSkillPickCount, fclSkillWeight** (~L447–481); D_00641C20 tier, D_00641C40 inheritance class, D_00641E60 rate | Collect valid unique candidate skill IDs and exact Vanilla number of inherited slots. Do **not** allow illegal skills just because they appear in parents. |
| Normal/triangle/multi fusion state entry | Mod inheritanceLogic.clps2c L3–13 hooks 0x002EF978 / 0x002F55AC / 0x002F8E3C and 0x002ED474 / 0x002F0F4C / 0x002F6D34 | Different fusion modes need a shared but mode-aware selection state; never copy PS2 states 0x60,0x61 without checking collisions. |
| Mode result/preview | src/Event/Fcl/y_fclCombine.c **fclCombineMenuFusion** FUN_002ED430 (~L1477+), **fclCombineFuseSourceRecords** FUN_002F9C30 (~L4306–4340) | Copy source persona records within explicit 1..6 bounds and preserve 0x30-byte record format. Recalculate valid selection after user changes sources; avoid changing actual roster during preview. |
| Final result mask | FUN_002F9C30 stores the return byte from fclFuseRollPersona at **yListSlotWork(listSlot)+entryIndex+0x2E4** | Finalize exactly once and maintain inherited-skill bitmask, skill table and UI consistently; no sentinel 0xFFFF persisted to Persona/save data. |
| Display persona skill list | src/shdPersona.c **shdPersonaDrawPanels** FUN_00119E10 (~L1896–2192), calls ShdSkill_DrawListSlot for 8 slots (~L2167–2181) | Scoped native 480×272 overlay with selectable list and feedback, rather than wholesale font/texture/UI changes; do not inject mod's PS2 draw hook 0x0011AA6C. |
| PS2 hook scratch globals | customGlobals.clps2c uses 0x0094BEE0/0x0094BEE8/0x0094BEEA and 0x0094BF50+ code caves | A bounded PSP heap/POD object owned by fusion state, released on cancel/reload/HOME. Absolutely no writes to PS2 addresses. |

**Specific danger:** Current P4P has a separate **Compendium Register** invalid-cache-pointer hazard: original src/Yajima/y_list.c FUN_002E4090 copies 0x30 bytes from datPersonaCachedRecord (src/g_data/g_data.c FUN_0010FCB0) without checking possible NULL. The optional fusion module changes Persona result record lineage and therefore requires original-save, minimal-edited-save and complex-edited-save safety tests. Do not treat manual inheritance as a fix for the existing Register crash.

**Suggested phased implementation:** research/UI sketch now, after stable perf build add feature branch with flag off by default; reproduce normal two-persona fusion, then triangle and special 4–6 parents; compute candidate list and legal slot count; select via Cross/undo via Circle/back, 5 visible rows and scroll; commit atomically only when complete; compare to unmodified Vanilla RNG with flag disabled. Stress 0/1/8 free slots, no legal candidates, duplicates, incompatible inherited skills, ability to cancel/change parents, Social Link extra skill, fusion accident, save/reload/Register/re-fusion, repeated transitions and effects. Measure GE display-list words because P4P has P0 65,536-word list capacity risk. No user save format changes in first version.

## B. GameBanana P4 PS2 shortlist with compatibility classes

Source hub: https://gamebanana.com/mods/games/8761 (verified 2026-11-19); GameBanana P4 PS2 installs normally use NTSC-U ISO + Mod Compendium, which is NOT the P4P PSP asset/update path: https://gamebanana.com/tuts/15216. Every mod package needs an exact content and permission audit before conversion. The P4G PC 32/64-bit and P4G Vita hubs must NOT be treated as direct compatibility.

| Mod | Direct P4P feasibility (research only) | Priority and hazards |
| --- | --- | --- |
| **Manual Skill Inheritance** | **HIGH potential but C/PSP logic reimplementation required**; verified public source mapping above | High optional post-stability; GPL-3.0 licensing and save/fusion state machine risks |
| **Widescreen Font Fix** https://gamebanana.com/mods/412801 | Medium: original PS2 font/layout adjustment/cheat; needs retail patch extraction, locate source glyph/spacing owner in frFontRaster/Im2D first | High investigative value for 16:9 typography; mod's 1.1 fixes spaced-out letters; original patch conflicts with old widescreen font cheat |
| **Always Cutin + Follow-Up** | T0 content type until package confirmed; likely battle script/code path, not simple texture | Medium later; increases VFX/cutin load, investigate once eye/All-Out performance and battle flow stable |
| **Run Faster** | T0 until package inspected; speed/movement field-timing changes could be native code hooks | Low for next build; risk NPC triggers/event scripting and FPS demand |
| **Classroom Cheat Sheet** https://gamebanana.com/mods/502475 | Likely event/message/asset data: mod adds star to correct answers during tests | Low-medium optional after event assets parser; gameplay change by design, preserve Vanilla localized text |
| **Saturated Portraits** https://gamebanana.com/mods/424557 | Texture replacement via PS2 assets; mod says currently mainly Social Link and Status portraits, not entire game | Medium only as local visual option; replacements use Golden graphics and rights/format need checking. Larger apparent image is NOT always higher fidelity or legal for distribution |
| **P4AU Battle Portraits** https://gamebanana.com/mods/395890 | PS2-native portrait asset replacements, format conversions conceivable offline after exact sprite resource mapping | Low-medium elective; third-party Atlus/Arc System Works art rights, not part of Vanilla-fidelity primary build |
| **texture HD ia** (hub listing) | Unknown file type and completeness until package read; do NOT claim ready | Research only; HD images may consume PSP bandwidth/RAM and cannot be assumed faster |
| **Persona 4 Spolszczona** (hub listing) | Translation/text assets may change fonts/string encoding and event scripts; no package examined | Low unless localization requested; may collide with current US font/display fixes |
| **Dummy ZZZZZ File** (hub listing) | Unknown utility/test component | Not a gameplay feature candidate |

**Do not** blindly use Persona 4 Golden Community Enhancement Pack mods; its versions and loader architecture differ, and Golden-specific patches/texture names do not establish compatibility with the PS2 US engine.

## C. Mod integration architecture proposal

- Builder option registry with ID, provenance, source version/license, base-game region and source file hash, conflict family, targets (code/asset/data) and default OFF. Add conversion stages instead of copying patched PS2 executable. Immutable original assets from user's own disc; no game data in public repository.
- Code mods: module-level C implementation or byte-level patch with *retail address → decomp function → PSP runtime callsite* verified; release only if licensing settled, build provenance recorded and direct game tests passed.
- Asset mods: detect container format, original asset path/record IDs, palette/alpha/texture dimensions, max GE-compatible sizes; generate PSP swizzled/CLUT data offline with CRC and fallback. Preserve texture transparency/UVs. Grade the screenshot on physical Go, not only desktop PPSSPP.
- Collision detection: no two mods may rewrite same sprite record, text bank or script byte range without explicit ordered merge policy. Keep *Vanilla PS2 fidelity* as primary default; optional visual changes should not hide renderer problems.
- QA automation/bot: for each enabled feature, add boot, scene replay, one modified content fixture, log tags, screenshots, crash signature, save round-trip and game completion gate. Tag tested scope T2/T3 and preserve user reports.

**Decision:** no mod port is implemented or PSP-tested by this audit. High-confidence next choices are manual fusion as an independent optional module and the widescreen font patch for source mapping; cosmetic mods remain lower priority until stability, 16:9 correctness and frame-time safety are established.
