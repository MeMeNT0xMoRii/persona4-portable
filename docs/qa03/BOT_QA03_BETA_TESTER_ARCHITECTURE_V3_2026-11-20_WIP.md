# QA06 BOT-V3 — PPSSPP automation and repeatable Persona4-PORTABLE beta testing (20 Nov 2026)

**Status: DESIGN ONLY. Not implemented, running or scheduled.** Only after RESONANCE01 becomes stable and a user-approved local environment has the private owned source assets/saves. A hands-off full intro-to-credits clear is a **long-term development goal**, not an immediate guarantee; begin with deterministic smoke tests. User controls private saves, disc data and screenshot/GE dump sharing. Earlier research: [bot proposal](AUTOPLAY_BOT_WIP.md), [PPSSPP API](PPSSPP_BOT_INTERFACE_WIP.md), [V2](PLAYTHROUGH_BOT_REAL_INTERFACE_V2_WIP.md), [route timebase](PPSSPP_ROUTE_TIMEBASE_DETERMINISM_BOT_WIP.md). This V3 consolidates missing hands-on executable acceptance criteria, not a new independent controller injection architecture.

## External interface, verified only in PPSSPP upstream development documentation

First party [PPSSPP WebSocket Debugger](https://github.com/hrydgard/ppsspp/blob/master/docs/WebSocketDebugger.md) and [CLI docs](https://www.ppsspp.org/docs/reference/command-line/) as read 20 Nov 2026:

- PPSSPP 1.21 and current development docs enumerate flags: **--debugger=PORT**, **--memstick=DIR**, **--state=FILE**, **--graphics=software**, and **--cpu-mhz=N**. Check exact installed binary with its own --help/version; no automatic assumption older release has these. The independent memstick directory MUST hold only disposable owned asset data and save clones; no overwriting user's normal PSP Go saves.
- Connect **ws://127.0.0.1:PORT/debugger** with mandatory WebSocket subprotocol **debugger.ppsspp.org**. The version event returns PPSSPP build/version, PID and loaded game path: verify all three, not simply successful port connection. Restrict to local firewall/host where possible; app builds may listen on every interface, unlike default headless localhost. Never expose the debugger's memory-write capability publicly.
- The protocol uses **ticket** correlation. Deferred actions (including cpu.resume) may have no immediate reply; choose client.config.set with acknowledgeDeferred=true and parse a deferred event separately from completion. Input.buttons.press acknowledges **after** its specified number of frames. Wrong expectations can make the test bot report a false hang. Input.buttons.send and input.analog.send must release all held controls on error/restart.
- Upstream documents **game.state.load/save/verify**, including load completion with wait=true. Some older third-party bridge docs wrongly claim the events do not exist; only the actual installed version handshake plus one harmless real probe can settle its availability. Check PPSSPP savestate compatibility per revision and user source/asset hashes.
- Current catalog: **cpu.status** (emulated microsecond us, ticks, clockHz), **cpu.runUntilTime** with relativeUs, **gpu.stats.get**, **gpu.buffer.screenshot/renderColor/renderDepth/renderStencil**, **gpu.displaylist.disasm**, **gpu.record.dump**, **hle.thread.list**, **hle.semaphore.list**, **hle.backtrace**, **memory.read** and breakpoint hit events. Default bot accepts read-only memory API and input/savestate operations; never write PSP RAM or change retail game logic.
- Headless with debugger starts CPU paused and headless default fast-forwards; BOT-0 must resume explicitly. For screenshot/performance comparisons, disable unlimited fast-forward and separately identify software vs Vulkan/D3D native rendering. **PPSSPP rendered FPS is not physical PSP Go FPS.**

## Architecture and state machine

Host-side one-process **supervisor** launches the isolated PPSSPP binary with version-pinned config and per-run memstick. A **WebSocket adapter** handles version/PID/path handshake, ticket/deferred, input down/up and status sampling. A **route engine** drives a finite state machine and stores checkpoints. Multiple low-cost **observer plugins** report actual input acknowledgements, game tick, GE submit/complete/display-accepted (if PSP Diag-Lite exists), CPU/emulator time, screenshots and resource metadata. A **fault classifier** validates independent signals. A **failure spool** retains a small, private last-256-events ring. **A/B/A rerunner** retries with one immutable last-passing build and exact same cloned save. **Only after reproducible evidence** does a small issue packet go to Codex for a targeted code change and test.

| Stage | Smallest reproducible work | Acceptance evidence |
| --- | --- | --- |
| **BOT-0 adapter** | Launch one exact PPSSPP executable; version/PID/game path; load disposable title checkpoint; send/release **5** short button events; run three times, verify response tickets and input release. | 3/3 same expected title/menu transition, no stuck button, full path and PPSSPP hash; no need for PSP game source edits. |
| **BOT-1 smoke** | Route manual test card G00/G01/G02/G03/G06/G10/G14/G16 from isolated, cloned checkpoint saves. | Scene state, expected menu state, screenshot availability, game tick/accepted displays, bounded transition time; compare school F006_002 to Junes F009_004. |
| **BOT-2 targeted failures** | Private cloned Compendium Register G14 and reinforcement G07, with known last passing perf06 A, RESONANCE01 B, perf06 A; 1–3 runs each. | First divergent phase, record whether CPU task, IO, GE, display, logger or observer stalled, classify 3/3 vs 1/3 flaky. |
| **BOT-3 chapter navigation** | Defined story checkpoints with human-reviewed branching policy, dialogue recognition, safe combat macros, resource and quest assertions. | Per-chapter completed routes, bounded retries, safe stop on unknown gate; not expected on first run. |
| **BOT-4 opt-in recurring runner** | Only if the user later explicitly installs/schedules it: build manifest gate -> BOT-0/1 smoke -> BOT-2 targeted replay -> private summary. | CI-like clean results without overwriting user saves or automatically merging patches. |

## Deterministic clock contract — avoid old source bug

Frozen native replay [route timebase proof](PPSSPP_ROUTE_TIMEBASE_DETERMINISM_BOT_WIP.md): game pad.c compares recorded timestamps directly against low 32-bit sceKernelGetSystemTimeLow()/1000, and route converter may not normalize first-event boot time. The time wraps after **4294.967296 seconds ~71.58 minutes**, much less than a full Persona 4 run. **BOT-0 avoids the in-game route.txt path:** drive input through PPSSPP WebSocket using host-side checkpoint-relative emulated time and acknowledged button edges. Re-anchor emulated us after each savestate; store OS monotonic wall time separately for watchdog. If native route.txt ever becomes a supported path, fix with explicit route activation epoch **and** 64-bit/wrap-extended time and test >72 minutes. A plain switch to wide time without subtracting the first event origin does not fix all mistakes.

## Failure classification: screen stability alone is not a hang

| Evidence | Classification | Required negative control |
| --- | --- | --- |
| OS process exits or nonzero status | HOST_PROCESS_CRASH | Check exit signal/error and last PPSSPP event. |
| Process still alive, WebSocket no response | EMULATOR_UNRESPONSIVE | Monitor host CPU and whether PPSSPP debugger itself stopped. |
| Emulated CPU us advances but game tick and accepted displays stop | GAME_THREAD_OR_GE_STALL_SUSPECTED | Check HLE threads, GE queued/finished and deliberate pause/loading. |
| GE finished but display accepted not advancing | PRESENT_SUBMISSION_FAULT | Frozen shown_seq can acknowledge unsuccessful sceDisplaySetFrameBuf; need Diag-Lite return status. |
| Image unchanged while dialogue/menus await player action | INTENTIONAL_WAIT | Route state, input release, audio/stage transitions, no false watchdog trigger. |
| Image differs but game advances | VISUAL_REGRESSION | Compare same P4P original and new build at same scene/save/animation, not blindly Golden art; extract 480×272 masks. |
| Event capture only in volatile RAM / HOME exits | NO_INCIDENT_AVAILABLE / DIAG_UNPERSISTED | Separate worker liveness, actual final-file readback CRC and committed state. |
| Route event missing after resume | ROUTE_TIMEBASE_DRIFT | Compare host checkpoint-relative vs PPSSPP emulated microseconds and event ack. |
| Failure only 1 of 3 runs | FLAKY | Include frequency; no automatic permanent game patch on single occurrence. |

## Small privacy-preserving failure packet

A local/private runs/UTC/route/build directory holds: manifest (source commit, exact EBOOT/P4DATA/P4STR/p4files hashes, profile, PPSSPP binary/version/backend, scene expected, **save clone SHA** and route version); last 256 button edges/scene events with **both host and emulated clocks**; triage.json with first failing step, CPU/GE/present stage, screen/last task and T1/H0/T2/T3 tier; up to **8 private screenshots** and **one bounded GPU record** if user enabled capture; optional PSP D1 incident file only after actual device verifies it; one-screen summary for Codex without full giant transcript. No automatic GitHub publication of private snapshots, original saves, memory dump, ISO or EBOOT. Avoid passwords, OS home paths, personal data and unfiltered game image bytes in public output.

Proposed test resource quotas (unmeasured, configurable): one PPSSPP instance, max 3 identical retries, finite per-route wall timeout, max 8 screenshots/one GE dump per failed route, stop after 3 identical unresolved failures, checkpoint/savestate only at known safe state; no auto self-patch, save mutation, public beta upload or release promotion. Running the test faster can increase path nondeterminism; for performance qualification run **regular real-time settings on physical PSP Go**, not headless fast-forward.

**First feasible prototype:** start PPSSPP with temporary memstick and exact binary, connect and version-handshake, drive five button events to the title, capture screenshot and game status, verify it 3×. Next select one battle wheel skill and one school scene control, then a **harmless homebrew-simulated stall** with a failure bundle. Only afterward add Compendium or reinforcement private test saves; full story later. These are **planned**, no bot executable or PPSSPP connection created this cycle.
