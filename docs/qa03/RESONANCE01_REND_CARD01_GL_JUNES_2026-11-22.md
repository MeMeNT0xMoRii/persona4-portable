# RESONANCE01 – REND-CARD-01 OpenGL validation and Junes RTT

## REND-CARD-01

**PASS / KEEP** for the Apsaras fusion in PPSSPP 1.20.4, OpenGL at 1×. Fresh processes replayed the same cloned slot and compared b259 to the existing experimental candidate. The preserved native-link, strict QA07, host and Software 1× results precede this OpenGL validation.

| Run | Source commit | EBOOT SHA-256 |
|---|---|---|
| b259 | `ee514a5b53f49c47cb93b3ce8c865cf8942e1651` | `08e33de5bc531729ea1f87a9ee7203095acdebc06ea7652c4d0a92e0ebf58232` |
| REND-CARD-01 | `8734c91b992a753f317aeec68856ff51511a8e23` | `e2b00c4597a51040c73887a724e5830ded2810fd6af9204a02342b9bd73b3d23` |

The internal screenshot series shows both illustrations in the front-facing phase and both backs in the rear-facing phase. The OpenGL frames were matched visually by phase. The preceding software test provides the exact-frame comparison. Igor, the table and the neighboring effects showed no new visible fault. Both runs produced all eight matching acquisition log samples after normalizing the display-bank field. Cull restoration is covered by the existing production-function host test and adjacent drawing was observed visually. A register-level OpenGL trace of restoration was outside this run.

- **Shuffle Time NOT_TESTED.** The available build-specific battle checkpoints lead to the scripted Izanami encounter. A compatible Shuffle Time checkpoint was unavailable.
- **Second fusion NOT_TESTED.** After acquisition, the reopened Normal Spread roster contained only Apsaras.
- No renderer stop, GE capacity error or pipeline watchdog appeared in the two tested fusion runs.

## Junes West F009_004

**BLOCKED** for a visible shadow fix. The existing replay reached the same entrance on b259. Six sampled binds used camera projection type 2 and the same 128×128×32 RTT target. An archived b259 OpenGL GE frame from this checkpoint contains 27 draws to the target, 24 with depth test and writes enabled, plus three with both disabled. The projection uses perspective W. The three observed clear draws target the screen. No draw in that frame samples this RTT as texture 0. The bounded live trace recorded neither a matching camera-clear call nor a matching floor-consumer call.

Vanilla constructs a parallel camera and has explicit clear and non-indexed floor-projection paths. b388 documents distinct changes for parallel projection, clear ordering, sampling and alpha. The next bounded test is to associate the exact Vanilla clear and floor consumer with the same b259 frame before wiring an RTT sampler. A projection-only change would leave the observed frame without a floor sample. No new Junes patch or package was produced. [b388 investigation](https://github.com/dreikk91/persona4-psp/blob/b388/docs/visual_issues.md#junes-floor-reflection-missing-and-its-render-pass-paid-for-nothing-found-2026-11-16-b326-b327)

## Performance and scope

Perf06, b259 and the experiment are **NOT_MEASURED** for accepted-present p50/p95 in school, Junes West and battle. The existing visual captures and render counters do not supply a matched timing series.

UI-WHEEL-02 stays at its documented validation scope. No new UI patch. Igor's table/floor is the next independently documented graphics candidate after Junes. G2 stays **P0_UNVERIFIED**, with raster/CLUT lifetime, TMX, replay/RTT and integrated synchronization still open. This is PPSSPP evidence. There is no PSP Go game-test promotion.
