# RESONANCE01 G2 next hardware smoke gate

**Hardware candidate BLOCKED. G2 P0_UNVERIFIED.** The isolated stability branch starts at b259 `ee514a5b53f49c47cb93b3ce8c865cf8942e1651`. The UI commits are separate.

| Gate | Current evidence | Smallest exit check for the intended route |
| --- | --- | --- |
| Generic texel/CLUT ownership | Existing extracted-source fixture reproduced premature destroy and mutation and again returned 8/8 expected cases. `RwRasterDestroy` reaches `func_003fb830` through the driver table. The Junes OpenGL free-site trace captured zero qualifying breakpoints, so it did not establish an active GE reference at release. | Start with a single bound allocation and the field→camp/scene teardown. Record pointer, actual GE address, open/sealed generation and matched DONE. Only then patch that allocation's retirement or refusal contract. Stop for an unowned reference, a wait on the open list or unbounded retained memory. |
| TMX destination | The raw copy fixture is a positive control for mutation with an open list. Real destination overlap remains open. | Log the actual copied range at `tmxDmaCopy` for the same transition. Compare a deliberately overlapped scratch range and a fresh-range control. Gate actual aliases. |
| Replay/RTT | Replay can retain addresses and RTT slot retirement is local to its owner. No observed cross-generation alias on the proposed route. | Trace one recorded hit and one RTT draw/sample/recycle across two lists, or establish that the optional path is unreachable for that configuration. |
| Commands and display | Existing isolated guards, FINISH/END, sealed generation and display ACK receipts stand. Remaining composites and final serial GU wait require route evidence. | Boundary test the relevant composite with a two-word tail. Check completed generation and ordered ACK across the transition. Establish path exclusion or bound the final wait. |
| Save and exit | b259 normal save, title return and cold reload passed with a clone. Host shutdown fault cases and standalone PSP Go v3 evidence retain their original scope. | On a later authorized device smoke, first prove idle HOME and completed-save HOME/cold reload on a dedicated clone. Do no device fault injection. |

Prepare separate perf06 and future stability installations with matched settings and clone inputs. Use school corridor `F006_002`, Junes `F009_004`, the fixed Izanami approach from slot 16, Camp and a short field transition. Keep camera, menu phase and repeated sampling windows matched. Final acceptance needs accepted-display timestamps, not render-loop FPS. Retain p50/p95, visible pacing and bank highwater only when available from bounded counters. If a run shows a GE stop, watchdog, freeze, visual corruption or save issue, end the trial, retain logs and switch back to the archived control. No hardware timing or game test was performed in this phase.

## Independent rendering queue

1. **Junes West floor reflection/shadow RTT.** The existing slot-9 route reaches `F009_004`. Capture the same floor and camera, then identify the specific RTT producer and the sampled pass. The documented perspective/orthographic difference is a candidate explanation and needs exact draw evidence. Start with one target or projection, with neighboring indoor transitions as controls.
2. **Fusion card face.** Start from the Entrance Hall Velvet route and use the already recorded Apsaras recipe with a cloned save. Capture the same reveal frame and inspect the front material terms before any draw change.

Keep Margaret tint and Igor/Velvet presentation separate from fused-card material. Dungeon lines/fog/depth need a matched camera and floor. Bright square light orbs and Zio/Apsaras VFX need exact animation-frame referents and a producer/reader alpha match. These remain open independently of the two proposed next tests.
