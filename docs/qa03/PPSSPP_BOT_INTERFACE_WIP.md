# PPSSPP automation — verified remote interface, not yet a built bot (WIP)

**2026-11-19 source check:** PPSSPP's official development documentation confirms both graphical and headless builds can expose a **WebSocket debugger**, supporting input injection, save states, screenshots, GE stats, memory and replay controls. This resolves an earlier architecture uncertainty. The user's bot goal—unattended smoke/playthrough, automatic incident reports, Codex only when needed—is **feasible to prototype**, not yet implemented, and no full-game completion is claimed.

## Authoritative references

- https://www.ppsspp.org/docs/development/getting-started/ (official remote debugger / headless build)
- https://github.com/hrydgard/ppsspp/blob/master/docs/WebSocketDebugger.md (command catalog; freeze actual PPSSPP release/commit before implementing)
- https://www.ppsspp.org/docs/development/ppsspp-internals/emu-api/ (emulator-side PSP test API, when useful)
- https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/InputSubscriber.cpp (exact button parameter schema, to be read at pinned PPSSPP version)
- https://github.com/hrydgard/ppsspp/blob/master/Core/Debugger/WebSocket/ReplaySubscriber.cpp (exact replay schema)

## Verified interface at current documented PPSSPP development head (verify in target build)

1. Launch PPSSPP app/headless with `--debugger=PORT`; WebSocket endpoint `ws://127.0.0.1:PORT/debugger`, **required subprotocol** `debugger.ppsspp.org`. Some headless builds stop CPU on start, resume explicitly. Some flags may vary by released binary; probe `version` first.
2. JSON requests shaped `{"event":"NAME","ticket":1,...}`, echo tickets in replies. Example read-only `{"event":"cpu.status","ticket":1}`. **Do not assume a host action succeeded until response/ack**.
3. Input: **`input.buttons.send`**, **`input.buttons.press`** and **`input.analog.send`**. Press may answer only after button was held for requested number of frames.
4. Game: `game.status`, `game.state.load/save/verify` (savestate path/slot), `game.speed.get/set`. PPSSPP savestates and native P4 save files are different objects, both should be versioned/hashed.
5. Diagnostics: `cpu.status` (emulated `us` and `clockHz`), `gpu.stats.get/feed`, `gpu.buffer.screenshot`, `gpu.buffer.renderColor/renderDepth/renderStencil`, `gpu.record.dump`, `memory.read`, `hle.thread.list`, `log.channel.set`, `cpu.breakpoint.*`. Log streams can drastically slow emulation: rate-limit.
6. The debugger also lists `replay.begin/abort/flush/execute/status` and `replay.time.get/set`. **Investigate semantics and determinism carefully** before claiming built-in record-and-playback works across all game state.
7. Broadcasts include `game.start`/`game.quit`, CPU stepping, `input.buttons`, logs, breakpoints; can drive a watchdog and event timeline.

## Minimal safe prototype, avoiding needless Codex credits

- Create a **local** small Python harness using a tested WebSocket client; don't commit credentials or private ISO/save paths.
- Probe protocol `version` and game path/PID to attach to the right instance. Freeze PPSSPP version, backend, screen resolution, device clock simulation, controller mapping and logging config.
- BOT-0: boot one legal local P4 build, issue 5 known inputs, collect one screenshot/CPU status, close gracefully. Repeat 10× and record failures.
- BOT-1: add individual scenario checkpoint `.ppst` and native save hash manifest; replay menu navigation; assert screenshot and expected state within timeouts. No machine-learning action needed at first.
- BOT-2: failure bundle with preceding inputs, status, GPU buffer screenshot, rolling log, build manifest, safe sanitized metadata, categorized hang vs load vs pause. Inject a controlled artificial stall for watchdog verification.
- BOT-3: sequential checkpoint regression. Only later try dynamic story routing to credits and conditional adaptation. User's max-level/OP save can speed testing but can alter battle and Compendium behavior, so always keep ordinary-save control.
- BOT-4: CI runner or scheduled local agent; on deterministic high-confidence defect, hand a **small** triage artifact to Codex/Work, not an entire playthrough transcript.

## Important footguns

- Interface support varies by exact PPSSPP build; docs reflect development head and may be ahead of installed release. Test handshake and event handlers with the chosen executable before build.
- A hardware hang does not guarantee a PPSSPP hang; the converse also holds. Official emulator is HLE, not cycle-perfect PSP Go.
- Enabling verbose logging may itself cause apparent performance regressions; cap log verbosity and keep tests deterministic.
- Direct use of localhost debugger is best; don't open a debugger with memory-write and input capabilities on untrusted LAN.
- Avoid unbounded retries or unchecked automatic save writes. Always operate on copies and stop after a bounded failure budget.

**Status:** Static protocol research T1, no bot implementation or actual connection test performed yet.
