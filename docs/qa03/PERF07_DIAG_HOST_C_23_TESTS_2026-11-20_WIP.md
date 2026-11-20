# QA05 — perf07 Flight Recorder: compiled C11 host ABI, sealed incident streaming, 23 tests (2026-11-20)

**Status:** Actual **host-compiled C** executable tests **23/23 PASS** on GCC and Clang/ASan+UBSan. This is **not** a PSP/EBOOT integration, gameplay runtime fix, performance profile or physical PSP Go test. The work stays in the **QA docs tree only**. Original perf06 game runtime remains immutable `MeMeNT0xMoRii/persona4-portable@29b0fcfdec70222a4052099b67fb4a11e8d8aff4`. Latest source documents: [Flight Recorder specification](PERF07_DIAGNOSTIC_FLIGHT_RECORDER_SPEC_2026-11-20_WIP.md) and [15 earlier synthetic JS checks](PERF07_DIAG_CAPTURE_PROTOCOL_HOST_TESTS_2026-11-20_WIP.md); this C experiment advances them, not replaces T2/T3.

## Exact host source, code and reproducible run

- **[Runnable standalone C11 test, `docs/qa03/host_tests/p4diag_host_v1.c`](host_tests/p4diag_host_v1.c)** — GitHub commit `c1543fa56e3a1691c29096c5ba390b3a382d9015`, Git blob SHA1 `be49fdf125e99a31681174a6fab2a749c54de485`, bytewise identical to **locally compiled** 12,834-byte file. Local SHA256 `e37a620510923ce35cdc4cffc7899488f30ebe56cf76166a3fb653247dbee827`; separately fetched GitHub copy reports exactly matching length and Git blob to local `git hash-object`. Pure synthetic/no copyrighted or private game data. It is **in docs/qa03/host_tests**, not linked by the PSP builder and not a game-runtime source modification.
- Host environment: Linux kernel 6.18.57, GCC Debian 14.2.0, Clang 17.0.0. Commands actually executed:
```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic docs/qa03/host_tests/p4diag_host_v1.c -o p4diag_host
./p4diag_host
clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -pedantic docs/qa03/host_tests/p4diag_host_v1.c -o p4diag_host_san
./p4diag_host_san
```
- Both return 0 with exactly **23 PASS** lines and `ALL 23 host-only C protocol tests passed`. Clang AddressSanitizer/UndefinedBehaviorSanitizer reported **no issue** in these executed paths. Host-only logs of both builds byte-identical SHA256 `51354b9f15112f6041ce1dcbaf214b92c12e029dc440164ee2ff3bc9662f0e31`. Compile flags and source bytes matter; **this does not validate Allegrex alignment, threading or filesystem**.

## What the actual C code implements and tests

- A native C `P4DRecord` **exactly 64 bytes**, source `_Static_assert(sizeof(P4DRecord)==64)`, fixed offsets `seq=8`, `list_words=52`, `detail=60`; **explicit little-endian 16/32-byte serialization** rather than copying the native struct bytes.
- **512 ×64B (32KiB)** fixed event ring, logical `ROLLING→CAPTURING→SEALED→EXPORTING→ACK`, last 128 records + next 128 records (**not frames**), early startup fewer pre-records, coalesced reasons and bounded drop/missed counters. Sequence modulo `u32` wrap and physical ring index wrap verified, strict sequence matching on retrieval.
- **File schema v1**: 32-byte little-endian envelope (`P4DR`, version, 64-byte stride, record count, CRC32 of payload, first/last seq, reason bits, flags); each record has `P4D1` magic/version/typed fields. One maximal snapshot **32 +256×64=16,416 bytes**; no additional big output staging buffer in the exporter (only one 64-byte current segment, plus existing ring). CRC32 known test vector `123456789→0xCBF43926` passed. CRC32 is accidental-corruption detection **not authentication**, and currently covers **payload, not entire header**: finalized PSP schema should protect/validate header fields too.
- Bounded export API with a **budget per worker tick** (4 KiB suggested, **not real Go measured**), synthetic varying positive short writes (4096/17/500/155/1), length/checksum/version checking and zero/negative write failure. Distinct `export_complete` field enables ACK only if complete; caller must also explicitly set `persisted_and_verified` after its actual storage verification. **Critical test-driven correction:** first C model accepted an ACK merely when in EXPORTING state; it was changed to require **both** full stream completion and caller confirmation of persistence; **new tests 20 and 21** reject early ACK and unverified ACK. A caller falsely claiming persistence is still outside this model; real PSP writer must check all IO/rename results.
- Source-level display semantic control retained from previous [T1 finding](PERF07_DIAG_CAPTURE_PROTOCOL_HOST_TESTS_2026-11-20_WIP.md): a failed `sceDisplaySetFrameBuf` can still advance perf06 `shown_seq`; host check distinguishes **display accepted** from gate-ack and does not claim to have observed a real failed flip. Proposed future collector logs GE completed, display submitted success/failure, and game tick/drawn/skipped separately.

**Test names/results, all 23 PASS:**

| # | Actual compiled host C test |
| --- | --- |
| 01 | Native 64-byte struct + fixed field ABI |
| 02 | CRC32 standard `123456789` vector |
| 03 | Explicit LE16/LE32 encoding |
| 04 | Startup 1 pre + 128 post |
| 05 | 128 pre + 128 post |
| 06 | Physical 512-slot wrap |
| 07 | `u32` event-sequence wrap |
| 08 | Coalesced triggers keep first reason |
| 09 | SEALED overflow/drop counting |
| 10 | `u32` timer wrap (440us) |
| 11 | Negative display API result ≠ accepted present |
| 12 | 3-of-5 over 50ms and single over 100ms trigger |
| 13 | Complete C-encoded file roundtrip/parsing |
| 14 | Variable short-write reconstruction |
| 15 | Truncated export rejection |
| 16 | Payload CRC failure after bit flip |
| 17 | Unknown schema version rejection |
| 18 | Negative IO cannot ACK or overwrite |
| 19 | Zero-write cannot loop forever |
| 20 | Incomplete export cannot ACK (new) |
| 21 | Completed-but-unverified export cannot ACK (new) |
| 22 | Successful verified ACK re-arms ring |
| 23 | Zero worker budget performs no IO |

## Important distinction: model vs game implementation

This C code **does not call PSP syscalls, parse actual p4.log, know a P4 scene ID, read any retail data, perform GUI profiling, run a real disk writer, receive interrupts or implement cross-thread mailbox memory fences**. In the model the synthetic `tick` field is a placeholder populated from bounded record count, **not** a valid real-game monotone tick when count saturates at 512; real implementation must pass actual tick/timestamp/scene identity instead of copying this test-helper behavior. The model's `persisted_and_verified` is **an external boolean supplied by the caller**, not a durable-commit proof; PSP staging, filename uniqueness, write success, close/rename and incomplete-temp recovery need real checks. Host sanitizer success is not proof of no races in PSP callbacks/flip thread. If the logger/export remains SEALED for long, newer incidents are **counted but not captured**; this consciously prioritizes nonblocking gameplay and preserving first evidence. Distinguish `shown_seq` gate from successful software display submission and physical screen scanout.

### Next authorized coding gates, after SEC-01 P0 safety

1. Keep this exact C file under docs for Codex to compile independently and as a schema reference. On isolated **approved perf07 code branch**, factor reusable ring/schema from test main into a PSP-compatible runtime module; add actual per-frame game tick, stage timestamps and minimal mailbox per callback. Preserve original perf06 EBOOT and do not accidentally include host `main()` in PSP link.
2. Test PSP Allegrex ABI and `sceKernelGetSystemTimeLow/Wide` clock origin, mailbox atomicity, GE callback nonblocking behavior, display accepted-vs-shown stats, real file writer `sceIoWrite` negative/short writes and missing `ef0:/`/ `ms0:/`, temp rename failure and interrupted HOME/movie/suspend; do not assume host CRC/ASan covered it.
3. Only after **SEC-01 prewrite list safety** and bank ownership guard can a detailed GE event recorder be enabled on potentially unsafe fog/reinforcement scenes. Target **no synchronous per-primitive IO**; reuse background logger THREAD but not its 64KiB ASCII ring. Explicit ring/counters RAM budget ≤48KiB proposed; real PSP static memory collision/heap to be measured.
4. Profile regular perf06 vs regular perf07 vs diag-lite OFF/ON using exact separate artifact SHA256, PPSSPP T2 then **actual PSP Go T3** Junes West F009_004, school F006_002, battle/reinforcement, private modified Compendium save clones and video/HOME. Previous ≤1% CPU/≤2% presented-frame p95 measurement targets are proposals, **not measured**.

**No real perf07 diagnostic binary generated or gameplay code changed in this session.**
