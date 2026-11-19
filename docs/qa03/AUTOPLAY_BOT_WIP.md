# Independent PPSSPP autoplay / regression-bot concept (WIP)

**Status 2026-11-19:** design proposal only. No tested bot, replay driver, emulator input API, or autonomous repair process is being claimed. This runner is deliberately **separate** from Codex/Work token consumption.

## Purpose
A locally running, user-controlled test harness plays **Persona 4 PS2-US port builds inside PPSSPP** and detects regressions while the owner is away or asleep. It preserves crash breadcrumbs and lets Codex/Work engage only when a failure cannot be triaged automatically. No guarantee of a complete intro-to-credits clear on the first iteration.

## Three test modes
1. **Deterministic smoke suite (build gate, highest ROI):** launch clean emulator profile and known checkpoint saves; fixed navigation script for XMB boot/title, menu/save-load, school→town map→Junes West→shops, Velvet Room fusion/Compendium, Dungeon/party, battle Rush/weak/All-Out/Shuffle, HOME/return. Prefer **checkpoints** over replaying the entire story for every patch.
2. **Long exploratory playthrough (overnight):** state-machine navigation / controller with periodic structured checks, route checkpoints and bounded recovery attempts, with explicit human-gated decisions for unusual story events.
3. **Targeted failure replay:** replay exact checkpoint + last `N` input events/seed/emulator settings/build hash to isolate crash, hang, freeze, render corruption or FPS threshold. A/B/A on perf06 or last passing build.

## Components and contracts
- **Controller adapter:** PPSSPP-version-pinned input injection and screenshot/debug-state API. Verify real accessible interfaces before coding; don't invent a PPSSPP CLI `--play` feature. May use external controller automation, an emulator debugging socket or GUI adapter if stable. Log all applied button down/up events with monotonic timestamps and frame indices where available.
- **Scenario engine:** YAML/JSON scripted checkpoints, seed/state assertions, route state machine (field, menus, battle, dialog). No unchecked infinite retries.
- **Observation module:** FPS/frame-times, screenshots, active scene IDs when exposed, audio/video state, mapped module logs, savestate provenance. Prefer direct framebuffer/API and structured telemetry; do not depend on OCR when unnecessary.
- **Watchdog:** distinguish an actual crash (process exit), prolonged stall (no frame/heartbeat), legitimate loading or paused dialogue, repeated futile navigation, visual regression (scene-specific reference), resource exhaustion (memory), and slow scenes. Never infer hang from low FPS alone.
- **Artifact bundle per failure:** `run_id, git_commit, EBOOT/p4data/p4str/p4strips hashes, PPSSPP version/backend/settings, save checksum, control-input recording, preceding 30–120 seconds of log, timestamps, last screenshot, 5–15 seconds around trigger if possible, reproduction attempts, performance metrics`. No original game ISO or copyrighted data in public uploads.
- **Triage:** auto-classify / deduplicate, mark `reproducible`, `intermittent`, `save-specific`, `unconfirmed`; Codex gets one concise failure package with probable source modules, never a full unfiltered raw log each time.
- **Stability/security:** run sandboxed with a copy of a supplied legitimate ISO and **copies** of saves, no network uploads by default, no destructive overwrites, no autopush of source fixes. An OP Level-99 save is a stress tool, **not** a legitimate-gameplay baseline and cannot cover all storyline flags. Include original unmodified and minimal persona-mod saves as controls.

## Implementation staircase (independent of Saturday rendering tasks)
- BOT-0: verify emulator version and actual automatable input/debug interface; one stable splash→title→load routine, clean shutdown. **Exit gate: exact replay works 10/10**.
- BOT-1: 5–10 checkpoint scripts (20–60 minutes total), screenshot hashes/timing reports, failure package; replay the same build consistently. **Exit gate: artificial crash/hang injected and correctly classified**.
- BOT-2: add combat/menus, save-dependent story/scene assertions, baseline A/B image comparisons. Keep PSP Go hardware as source of truth for FPS.
- BOT-3: long-running story state-machine and crash quarantine; when stable, integrate with build artifact verification and a small, bounded Codex investigation job triggered by high-confidence failures.
- BOT-4: optional permanent local QA daemon, user controls start/stop, no presumption this chat can execute when closed.

## Tests / hazards
- PPSSPP and PSP Go differ in timing, rendering, GPU emulation and memory; a PPSSPP-green run is **not** a PSP-Go performance pass.
- UI/menus are time-sensitive. Input events need screen-state checks and bounded waits, not only blind millisecond delays.
- A new build must **not** auto-start from zero for every cosmetic UI fix; smoke subset first, deeper sweep on integration milestones.
- Profiler builds can alter timing. Keep normal and instrumented builds distinct; log hardware plugin state.
- Claim no full-game coverage until the bot has reached credits on the **exact build** with verified route checkpoints.


## Source-verified 2026-11-19 practical tester harness and triage schema

**Existing executable wrapper, not a bot:** [tools/ppsspp_lab.ps1 L1–132](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/tools/ppsspp_lab.ps1) currently supports isolated PPSSPP OpenGL/Software backend selection, per-build folder variants including P4-perf06, route file with PADS/STICK records, up to 1024 route entries/16383 characters, scene log marker screenshots, ordered ExtraScreenshotMarkers, and GPU dump/archive. It hashes EBOOT.PBP + p4data.bin + p4str.bin in runtime-sha256.csv. **Its 500ms log polling and 277ms multi-capture interval are NOT frame-accurate or deterministic action timestamps.** Do not assume it can automatically beat dungeons or respond to decisions. The Windows helper requires an existing local PPSSPP installation and legal privately supplied game data; none was launched in this research.

**Video-game port best practice** from [OpenUG2-PSP 0.4.0 tester guide](https://github.com/jussimir-labs/OpenUG2-PSP/blob/main/docs/TESTING.md): each report includes device/firmware, source, game mode/location, action, expected/actual, debug log and build ID; an in-game hotkey can write a local technical bug file. For P4P, plan an **opt-in L+R+SELECT log capture** only after checking input conflict and save/HOME state; collect sanitized logs + IDs, no ISO, copyrighted screenshots publicly or automatic user data uploads. This is a design candidate, not code.

**BOT failure packet v0 (planned, all file paths local/private):**
- manifest: run_id, UTC time, baseline SHA/branch, EBOOT/p4data/p4str SHA256, compiler/toolchain flags, PPSSPP exact version/backend, deterministic route revision, user data source edition;
- state: room/map scene ID, save/savestate checksum and profile, frame index where debugger supports it, last game flags/transition, preceding bounded input events (button down/up and control-frame time);
- watchdog: distinguish process exit/GE stall/task hang vs intentional dialogue wait, low FPS vs no frame advancement, screen blank vs script loading, with distinct confidence and timestamps;
- evidence: last 30–120s bounded logs, 2–3 pre/post failure framebuffer screenshots **kept private**, memory usage, GE list peak + RTT count + alpha writer/reader tags if instrumented;
- replay: minimal checkpoint, expected next state, three reruns on original untouched save and any separately edited save, A/B/A across last passing build, one-sentence Codex bug hypothesis + source-owner row ID;
- privacy: user explicitly chooses whether to submit a sanitized report; no automatic cloud uploads/third-party game asset publication, no overwriting saves or replacing a passing build.

**Execution staircase:** after source mapping, BOT-0 debug socket version handshake + title/load/5-button replay; BOT-1 five short UI/scene transitions and controlled synthetic timeout; BOT-2 combat/Velvet/save checkpoint, one hotkey report; BOT-3 extended story quest routing. Record separate artifacts per failed route and stop after bounded retrials. An unattended complete title-to-credits run remains *not built or tested*. Ensure PSP Go T3 for all frame-time goals (PPSSPP is not a physical FPS oracle).

## BOT-02: dev-only remote rescue without runtime card polling (2026-11-19)

New **external source example**, NOT a P4P implementation: OpenUG2 PSP [`psp-viewer/probe.c` L210–259](https://github.com/jussimir-labs/OpenUG2-PSP/blob/main/psp-viewer/probe.c#L210-L259) has its own lower-priority `HostCmd` 4KiB worker thread checking PSPLink `host0:/hostcmd.txt` (rather than scanning the Memory Stick in game-frame code) about once each second. It supports dev-only suspend, graceful exit followed by five-second bounded emergency exit, shut down before normal exit and is disabled for release. Its comments document earlier frame delays from on-card directory lookup. **Host0:/ requires PSPLink**; ordinary retail PSP Go users and standalone PPSSPP should not be expected to have it.

**For P4P bot**: use PPSSPP WebSocket/external host process when PPSSPP is running, and optional PSPLink worker only on development PSP Go, not an `ms0:/` or `ef0:/` polling loop inside the render/update/audio path. For normal users provide a manually requested, sanitized local technical report once, optional export only with consent. A watchdog **request** for exit must not be confused with a successful clean shutdown, especially during `memcard.c` save sync. Include exact BASE-01 perf06 EBOOT/data SHA/checkpoint, route clock v2 (wide+relative), host command source, active save operation, GE list bank and bounds. Test BOT-0 three short runs with simulated frozen scene vs intentional dialogue wait before extending. Refer to [P4P crossport perf06 report](PERF06_RTT_DEPTH_GE_PROFILER_SHOP_UI_CROSSPORT_WIP.md). **No bot/host rescue was implemented or run.**
