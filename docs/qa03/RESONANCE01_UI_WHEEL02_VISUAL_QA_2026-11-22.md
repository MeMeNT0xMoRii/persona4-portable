# RESONANCE01 UI-WHEEL-02 visual QA

**IMPLEMENTED and TESTED within the PPSSPP visual scope.** Experimental source commit `74eddc6eb56fd6effc9427cb88e2812846f8164c` from UI-WHEEL-01 `f3a8e915900cf36de441aab52f8f97ae1391625a`. The candidate package variant is UI-WHEEL-02. Its inherited runtime title and build number still identify b259. The manifest and EBOOT SHA-256 `963c28f0edf1dec7a524431580ae25356f7db19d58d12c66c7420e587f9c28f8` identify the actual binary.

The `tools/psp_census.py` overlay shifts the ordinary command glyphs, selected glyph and settled strip of `btlSelectionDrawCommands` up by 13 source units. The active text and strip receive the same offset. The retained sprite origin continues to anchor the help and Rush sprites. The opening strip and the extended strip for other modes retain their original paths. All three radial fans, the 15-unit row pitch and the Vanilla entries stay intact. One linked game object changed out of 821. Native link with 19 import libraries and strict QA07 passed. The original host fixture and pinned donor check passed, as did both missing/ambiguous row-anchor tests.

In the fixed Izanami fight from a cloned save, PPSSPP 1.20.4 software mode at 1× captured both the UI-WHEEL-01 checkpoint and the UI-WHEEL-02 fresh battle route. The run checked the executable artifacts, live instruction fingerprints, actual function execution and all eight selected row indexes. The comparison uses the settled wheel, same Skill selection and same camera. Background idle animations varied.

| Measured in native PSP pixels | UI-WHEEL-01 | UI-WHEEL-02 |
| --- | ---: | ---: |
| Yellow mask | 111×111 | 111×111 |
| Yellow mask center | (64.5, 203.5) | (64.5, 203.5) |
| Text block Y span | 176–245 | 168–237 |
| Gap above / below text within yellow mask | 28 / 13 | 20 / 21 |
| Selected Skill strip Y span | 211–219 | 203–211 |

The eight-row capture shows each highlight aligned without label clipping. The lower rim remains adjacent to the existing Rush hint. The separate Tactics, Skill, Persona and Item panels opened, target selection and Analysis detail rendered, Guard advanced the turn, Rush started an attack, and Escape remained disabled for this encounter. Field Camp, Skill, Equip and Persona captures also reached distinct linked draw owners. A separate OpenGL 1× smoke reached school `F006_006` and Junes West `F009_004`. No GE stop or watchdog occurred in the observed battle, Camp or OpenGL smoke logs.

The automated comparison wrote PNGs, a marked difference and receipts locally. PPSSPP 1.20.4 returned unknown event for the WebSocket savestate save/load commands. The baseline used same-build startup auto-load and a successful repeat verified a distinctive opcode and the expected Skill row. The candidate replayed the card clone. OpenGL screenshot readback was unavailable via the debugger. Software screenshots contain actual game output and are independent of OpenGL fidelity and timing. Golden geometry is taken from the existing QA03 measurements, with no new direct Golden image match. Accepted-present p50/p95: **NOT_MEASURED**.

The [UI correction matrix](RESONANCE01_UI_CORRECTION_MATRIX_2026-11-22.md) carries the existing 31-family assessment forward. Camp targets still need matching content and pixel measurements, so the first larger UI batch is **BLOCKED**. The separate [G2 smoke gate](RESONANCE01_G2_NEXT_SMOKE_GATE_2026-11-22.md) lists unresolved prerequisites. Hardware candidate **BLOCKED**. G2 remains **P0_UNVERIFIED**.
