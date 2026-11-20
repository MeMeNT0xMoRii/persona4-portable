# QA08 — D1 incident triage, safe decision tree, eventual bot packet (20 Nov 2026)

**Status:** T1 verified frozen b249 source at 29b0fcfdec70222a4052099b67fb4a11e8d8aff4, host-only H0 Python synthetic 18/18 actually executed with Python 3.13.5 and Python -O. The new [offline candidate classifier](host_tests/resonance01_triage_qa08.py), 5,604 bytes, SHA256 aa51117effb6047ee255e14b28ec66736e7f24e4f2f883e94d23518a721ca71e, remote Git blob 53604f699f90e6d13fea5962b4d6b2c4386ec66b **exactly equals the executed local source** (commit 410b9cf). No runtime game code, real D1 binary file, native PSP EBOOT, PPSSPP T2, or Go T3 was used. The classifier returns **a hypothesis, never a proven root cause.**

## Why the progress signals must be independent

[Original rwsky.c L650–665](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L650-L665) reads sequence then buffer, and advances shown_seq even after sceDisplaySetFrameBuf fails. The source proves the possible invalid ACK path, not an observed PSP display error. [Original pipeline watchdog L1498–1525](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L1498-L1525) force-releases stall at 250 ms and gives up after 1 second without necessarily proving earlier GE work retired. [Original main.c L414–503](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L414-L503) can fail boot-data validation before the background log writer starts. HOME, movie loads, deliberate frameskip and a genuine lock can all look like a frozen screen to a human tester.

Required future independent counters: game task tick, drawing completed, GE list submit with **bank ID**, GE callback-finished with **bank ID**, actual **accepted** display-present seq/bank and return code, attempted display seq, watchdog state, current scene/load/movie/HOME mode, active I/O request and source callsite, D1 worker heartbeat and durable-file ACK. Do not derive FPS from game ticks or GPU busy time from queue elapsed alone.

## Bounded decision tree (candidate / falsifier)

| Priority | Strongest authentic observation | Output hypothesis | Mandatory follow-up |
|---|---|---|---|
| 0 | HOME dialog, standby, manual pause active | INTENTIONAL_PAUSE_OR_HOME | Resume/cancel, never report spontaneous crash |
| 1 | Asset path, P4 triplet or image digest failed before D1 | PRELOG_BOOT_OR_ASSET_FAILURE | Fix matching local install, not an imagined lost D1 file |
| P0 | Earlier GE list memory bank reused before real finish | P0_GE_BANK_LIFETIME_INVARIANT | Block hardware promotion and verify bank retire fence |
| P0 | Display-set function returned negative, shown_seq advanced anyway | P0_FALSE_DISPLAY_ACK_INVARIANT | Require observed API result; correct source+host sequence protocol |
| P0 | 1s watchdog gave up and GE work still pending | P0_GE_WATCHDOG_PENDING | GE queue / callback / bank ownership, not automatic GPU blame |
| P1 | Game tick advances while GE queue backlog persists | GE_QUEUE_BACKLOG_CANDIDATE | Separate scheduler, queue, GPU and corrupt list |
| P1 | GE callback completes but accepted display does not | DISPLAY_FLIP_OR_VBLANK_CANDIDATE | Flip event, wait, API return, actual display |
| P1 | I/O/semaphore wait overlaps a slow interval | IO_WAIT_CORRELATION_CANDIDATE | Prove actual blocked task/caller, no false attribution |
| P1 | Game ticks run, frameskip active, no draw | EXPECTED_OR_EXCESSIVE_FRAMESKIP | Intentional skips vs actual present rate |
| P1 | CPU frame slow without GE backlog | CPU_TASK_OR_RENDER_BUILD_CANDIDATE | One bounded owner timer and scene control |
| P1 | Worker alive, task tick and GE finish stop | GAME_TASK_WAIT_OR_DEADLOCK_CANDIDATE | Pause/load/IO/deadlock and task state distinction |
| UNKNOWN | Missing heartbeat, malformed/missing packet, total kernel/power halt | UNKNOWN_NEEDS_MORE_CONTEXT | Do not invent a cause or claim D1 persistence |

**Ordering matters.** Safety candidates supersede UI/performance. “I/O overlap” does not prove that I/O caused a stutter. PSP threads and GE jobs can overlap. The user's edited-save Compendium and enemy reinforcement remain separate original-code owners; the classifier only selects the next instrumented replay, **never modifies saves**.

## Actual host-only classifier and protocol

Run on any Python >=3.10 host (no PSPDEV, ISO, PPSSPP necessary):

    python docs/qa03/host_tests/resonance01_triage_qa08.py --self-test
    python docs/qa03/host_tests/resonance01_triage_qa08.py --input C:/Private/P4P/normalized_incident.json

**Important:** input is a future **normalized local flat JSON summary**, not the raw D1 binary. Example keys: ge_pending, watchdog_gave_up, bank_reused_before_ge_retired, display_result, shown_seq_advanced, game_tick_advanced, io_in_flight, io_wait_ms, frameskip, cpu_frame_ms, manual_pause, post_records and persist_ack. There is currently **no native D1 recorder implementation or binary decoder/normalizer**. Do not feed arbitrary corrupted D1 bytes to this toy. The classifier returns primary_candidate + a fixed UNCONFIRMED_HYPOTHESIS confidence + completeness/durability flags + verification next actions.

**18/18 H0 actual execution:** 12 candidate cases (home, boot, bank, display, watchdog, backlog, complete-not-present, I/O, frameskip, CPU, game wait, unknown), 4 partial/full/invalid post-count tests (0,127,128,129) and 2 ACK conditions (not acknowledged, purported verified). All passed locally with normal Python and Python -O; the uploaded Git blob matches local SHA1 Git hash. No physical I/O sync or device CRC was executed by this classifier. The reported ACK condition is **caller-supplied**, never independently validated. A future decoder must enforce D1 magic/schema/version, fixed length, CRC, sequence bounds, same-build source/profile ID and safe file path **before** creating this summary.

Partial capture: post_records=0..127 is potentially valid if the independent worker seals it; post_records=128 is full; 129 invalid. The old C reference model required all 128 POST and is **not** a real PSP daemon; see [QA06 D1 partial-stall analysis](DIAG_D1_PARTIAL_STALL_HOME_CRASH_DURABILITY_2026-11-20_QA06_WIP.md). Durable ack must be backed by actual temp file checked writes, device sync, rename, reopen/readback and CRC, and is only best effort across card loss or power cut. HOME immediate quit may leave no packet. Do not put D1 export in the game save semaphore or run synchronous per-draw disk I/O.

## Future beta tester / human tester normalized incident envelope (proposal)

- Local manifest/build ID/profile, source commit, scene key, opaque checkpoint ID (not raw save), selected test action list and controlled timebase.
- Private cloned-save checksum and button-response transcript; use an opaque public identifier instead of committing user saves/screenshots/ISO/Golden pairs.
- PRE128 + actual POST0..128 count, bounded source address tags, progress values from game/GE/display, bank retirement, decoder validation status, persistence ACK and first-choice *candidate* (not asserted cause).
- BOT-0 remains **future, not Saturday**: first validate five button actions with acknowledged PPSSPP frames, repeat one checkpoint three times, then report packet and source owner. Do not launch uncontrolled narrative loops or rely on automatic PSP Go control.
- Physical Go T3 user test follows secure source+host+PPSSPP gates; no “complete game through credits bot” currently exists.

**New negative tests to run later** (NOT RUN): malformed/truncated CRC, malicious JSON types, u32 seq wrap and reused GE list IDs, explicit movie transition, repeated IO-wait that is not causal, display failure with unmodified shown_seq, and concurrent STOP+HOME. These are not covered by the 18/18 simple host classifier.

**Rollback:** disable experimental classifier/recording if false attribution, blocked game load, dangerous list probe or IO contention occurs. Keep regular perf06 unchanged. Related [QA08 RAM/VRAM budget and D1 .bss hazard](RESONANCE01_QA08_PSPGO_MEMORY_BUDGET_AND_DIAG_LITE_ALLOCATION_GATE_2026-11-20.md), [read-only B0 input preflight](host_tests/resonance01_b0_preflight_qa08.py), [D1 full plan](PERF07_DIAGNOSTIC_FLIGHT_RECORDER_SPEC_2026-11-20_WIP.md), [Saturday starter](CODEX_START_HERE_RESONANCE01_2026-11-21.md).
