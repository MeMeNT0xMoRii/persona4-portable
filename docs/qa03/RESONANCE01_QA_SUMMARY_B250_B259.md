# RESONANCE01 QA summary, b250–b259

G0 and G1 passed. b250 reproduced the baseline artifact set byte for byte. Subsequent candidates passed scoped host regressions, native linking, strict QA07 and selected PPSSPP routes. **G2 remains P0_UNVERIFIED. G3 remains C_NOT_BUILT.**

| Build | Scoped result |
| --- | --- |
| b251–b253 | Added FINISH/END reservation, status-based bank retirement, separate display acknowledgement and checked writer/allocation guards. An isolated PPSSPP probe confirmed numeric list ID reuse. |
| b254–b257 | Fixed a reproducible direct draw overrun, added complete preflights to selected SDK, inline and replay operations, included the internal SDK Clear draw, corrected the cold-mesh command budget and stopped treating a failed batch stall update as successful. |
| b258 | Integrated a transactional glyph upload and cache, preserved valid glyphs on failed recolor and pinned glyph slots until matching GE retirement. Scoped text and scene regressions passed. BSS grew by 64 bytes. |
| b259 | Coordinated fatal renderer stop and HOME with the card transaction barrier. All 14 synthetic host Save-I/O cases passed. In PPSSPP, the game saved to a fresh slot in the Entrance Hall, returned to the title via the game menu and loaded the new save in a separate emulator process. |

The standalone GE probe v3 passed on a physical PSP Go. It exercised list ID reuse, completion without relying on callback delivery and loss of an old ID after a global wait. This is hardware evidence for the standalone probe. RESONANCE01 game code has no corresponding PSP Go playtest. PPSSPP did not exercise the post-save PSP HOME callback or trigger a renderer abort inside a live save transaction.

The shortest path to a controlled game smoke is to reproduce and address the generic raster lifetime conflict on the intended route and establish whether TMX destinations overlap live references, verify the reachable GE composites and bound the serial wait, then run a cloned-save rehearsal with explicit stop and exit observations. Dungeon combat checkpoints must depend on the current game state or a build-specific isolated replay state because floor layouts are procedural.
