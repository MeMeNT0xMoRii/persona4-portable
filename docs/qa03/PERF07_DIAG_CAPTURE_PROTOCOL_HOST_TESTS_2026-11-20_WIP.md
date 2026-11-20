# QA05 — perf07 diagnostic recorder: display semantics, bounded ring export, 15 synthetic host checks (WIP)

**2026-11-20 ~07:04 Europe/Vienna.** Continuation of user-requested enhanced perf07 diagnostics. Exact game source frozen at `MeMeNT0xMoRii/persona4-portable@29b0fcfdec70222a4052099b67fb4a11e8d8aff4`. Extends [flight recorder specification](PERF07_DIAGNOSTIC_FLIGHT_RECORDER_SPEC_2026-11-20_WIP.md). **Evidence:** T1 direct source, H0 nine in-memory ring/trigger tests + six in-memory export/schema tests **all passed**; NO PSP source edits, no native compiler/binary parser, PPSSPP T2, PSP Go T3, real performance gain, or confirmed display failure.

## Newly verified source fact: displayed/queued/shown are different state variables

- [`rwsky.c::flip_main` L647–668](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L647-L668) waits for `ge_finished>=flip_seq`, waits Vblank, calls `sceDisplaySetFrameBuf`, and calls `fps_presented()` only **on successful API return**. However **`shown_seq=seq` and `FLIP_SHOWN` signal are issued even when `sceDisplaySetFrameBuf` fails**. Thus `shown_seq` is a CPU/flip-thread **gate acknowledgment**, NOT proof a display submission was accepted. No perf06 real hardware failure evidenced.
- [`rwsky.c::fps_presented` L581–607](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L581-L607) returns immediately when `p4_fps_overlay==0`; default passive telemetry cannot simply reuse this counter. [serial path L1608–1614](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L1608-L1614) likewise unconditionally sets `flip_seq=shown_seq=ge_queued` after conditionally submitting a frame.
- **Telemetry gate:** collect `flip_attempt`, `flip_success`, `flip_error`, `display_return` and an accepted-submission timestamp separately from `shown_seq`, `ge_finished`, drawn/skipped counts. A successful display call is a **software-confirmed display-buffer submission after Vblank**, not proof of physical panel scanout; label FPS appropriately. Do NOT change gate error-recovery behavior in the same telemetry patch; doing so could create deadlocks. GE callback [L554–566](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L554-L566) increments `ge_finished` and signals the flip event flag; no malloc, disk IO, heavy lock or `p4_log` should be inserted there.

## Newly verified source fact: reuse worker THREAD, not existing ASCII RING for binary snapshots

- [`port/main.c` L48–134 and L289–311](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/main.c#L48-L134): `char p4_log_ring[65536]` accepts `snprintf`-formatted text lines from 160-byte `p4_log` buffer; existing `log_writer` wakes ~100ms and drains ASCII to p4.log. Source comments note ~17ms open latency on hardware. **Do not insert raw binary into text ring or log every 64-byte record via `p4_log`**; that would distort frames and could overflow the 64KiB ASCII ring.
- Use **same existing background writer thread**, but a **distinct bounded diag binary queue / file** with explicit validity checks. Initial proposal max 4KiB write per worker tick, measured/tuned on real Go. No sync file operations from render/GE callback/task, no 16KiB synchronous fallback if thread startup fails. Worker handles short writes/errors with bounded retries, only marks snapshot ACK after verified full write; temp file and unique session/build name must not overwrite saves. Avoid claiming file survives sudden power loss. If backpressure accumulates, **drop new trace events and count them rather than stall gameplay**.

## Tested single-producer ring protocol model, not implemented C yet

Proposed **512 × 64 byte=32 KiB ring + ≤16 KiB bounded metadata/counters**. One designated writer at safe game-thread phase; other threads/callbacks only contribute minimal mailbox timestamps/flags and never block. JS host model is **single-threaded only**: PSP callback/CPU memory ordering and atomicity remain **unverified**.

States: ROLLING → CAPTURE (first trigger freezes logical `start=max(0,seq-128)`, collects exactly next 128 **records**) → SEALED (no further overwrite, increment `dropped` and `incident_pending` counters) → EXPORTING (worker emits snapshot with bounded IO) → ACK (only after successfully persisted) → ROLLING. Coalesce repeated triggers during CAPTURE into bounded reason flags; do not restart first capture. A 512-record ring can protect 128 preceding and 128 subsequent records, even across ring slot wrap, if **SEALED means no ring writes until ACK**. It does NOT guarantee 128 frames: two events per frame give half as much time history. Budget normal sampling to roughly one base record per frame and keep exceptional event counters/mailbox separate. No postmortem promises if crash happens before writer flushes.

## Actual 15 synthetic checks, all PASS in this turn

**H0 in-memory JS ring/logic checks (9):**
1. 400 rolling records → trigger +128 after: 256 sequential records, seq **272..527**.
2. 1028 records → physical 512-slot wrap: seq **900..1155** intact.
3. Two triggers during one capture coalesced (slow+IO), first trigger preserved, 256 capture records.
4. SEALED drops next event and increments counter without overwriting captured history.
5. Early capture after 1 record gives **129 records**, post-ACK can start a new full 256-record capture.
6. 32-bit microsecond timer wrap `0xFFFFFF00 → 0x000000B8` gives unsigned elapsed **440µs**.
7. `64-byte` little-endian record header/field roundtrip: magic `0x31443450` (P4D1), version 1, ID 7, full uint32 frame seq and last-field sentinel.
8. Synthetic negative display result despite `shown_seq==requested_seq` is classified **NOT accepted present**.
9. Distinguishes initial proposed trigger **3 of last 5 intervals >50ms** vs nontrigger, and **one >100ms** severe trigger.

**H0 encoding/export model checks (6):**
10. Encoded **256×64 + 32-byte header =16,416-byte** synthetic capture with little-endian v1 fields and CRC32. Parsed sample seq near uint32 wrap and list words 50,000–50,255 unchanged.
11. Mock partial-writes sizes cycling **4096,17,500,155,1** reconstruct exact 256 records and checksum.
12. Truncated file (last 3 bytes missing) correctly rejected.
13. Single flipped payload byte correctly rejected by CRC32.
14. Changed version 1→2 correctly rejected.
15. Mock zero-progress write correctly rejected (no infinite retry).

**Limits:** these are 15 passing *synthetic source-logic/serialization* checks, not an executable PSP build, installed host C parser or on-device IO. CRC detects accidental corruption, not authentication. Follow-up missing cases: negative `sceIoWrite`, PSP callback interleaving/atomicity, writer-starvation and 4KiB budget stress, full disk, missing removable storage, file rename errors, incomplete temporary files, crash before export, cross-endian ABI/packing, producer drop counter wrap, 72-minute clock epoch and seq rollover, accepted display call without physical scan-out proof.

## Actions for next authorized perf07 implementation

1. **Do not touch immutable perf06:** create isolated approved source branch and freeze diagnostic schema/ABI. Add an actual host C/PSP-ABI static-size + event parser test and malformed/corrupt-file tests before compiling EBOOT.
2. Add no-overhead when disabled and low-cost LITE event taps: independent successful display submission timestamp, separate thread signal/GE callback finish, source-safe per-bank list hiwater and SDK return checks, game tick/drawn/skip. **Fix SEC-01 writing safety separately**; recorder is evidence, not GE pre-write protection.
3. Add bounded binary exporter using existing worker thread **but not ASCII log ring**. Fail-safe on full or broken storage, no rendering-thread fallback and no saves/log overwrite. Persist build EBOOT SHA256 and source/asset hashes via manifest.
4. Profile on PPSSPP (T2) first, then **physical PSP Go (T3)** with regular perf06, regular perf07 and diag-lite OFF/ON; Junes F009_004, School F006_002 negative control, combat/reinforcements, Compendium private save clones, movie/HOME, ef0: and ms0: storage. Record real presented-swap p50/p95 and capture overhead **measured**, not presumed (previous ≤1% CPU / ≤2% p95 targets not tested).
5. Deep PSPLink task profiler and serial GE dump remain separate **nonbenchmark** profiles. If any actual GE overflow/list-bank live reuse occurs, enforce independent safe-failure behavior before continued stress on Go.

**All findings remain docs/research only; Saturday perf07 not built, no original ISO/assets or 120-private Golden image ZIP uploaded.**
