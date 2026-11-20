# QA06 — IO-01: exact frozen PSP read function compiled on host, 11/11 checks + new logical file-size boundary gap (WIP)

**2026-11-20 ~14:35 CET.** This round followed user's open-ended `mach weiter`. Checked the latest [live handoff](HANDOFF_LIVE.md), the older [IO-01 package cursor/range-open report](IO01_PAK_CURSOR_RANGEOPEN_REASM_GATE_2026-11-20_WIP.md), and pinned original and port read/linker sources. **New actual runnable host-C evidence** strengthens the earlier JavaScript control-flow simulation, and distinguishes a new bounds-control issue. **No game source, executable or real retail data modified/read.**

- Frozen P4P perf06: `MeMeNT0xMoRii/persona4-portable@29b0fcfdec70222a4052099b67fb4a11e8d8aff4`.
- Original PS2 decomp: `Raikaru/Persona4-Decompilation@363eab8a966d60a9773b45f330f2344329ec4f0a`.
- **Exact committed, test-executed host source:** [`docs/qa03/host_tests/io01_actual_read_host.c`](host_tests/io01_actual_read_host.c), commit **`94b40d652e0632991d9bec3f63754b833ed3f0fc`**, blob `1f82e3db7c6ae1b326fa0e26e4d485a87e09cb51`. **7,771 bytes, SHA256 `aaecde00acf8e5c9cc6b6dbaf7dc3e9af2f984108ff193edc6182a64e1d4eb7d`**. Fresh GitHub fetch returned the **same blob SHA and same 7,771 characters** as local `git hash-object`; this is the exact source compiled and tested. This only transcribes the **actual `func_004c9010` body verbatim** from frozen [`port/files.c` L148–202](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/files.c#L148-L202) and surrounds it with in-memory PSP API mocks / 11 assertions. The host mocks and tests are new, **not** an actual PSP I/O subsystem or original full port/files.c compilation.

## C host run — actual compiler execution, not only math

```sh
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
  docs/qa03/host_tests/io01_actual_read_host.c -o io01_host_gcc
./io01_host_gcc
clang -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -pedantic \
  docs/qa03/host_tests/io01_actual_read_host.c -o io01_host_san
./io01_host_san
```

**GCC and Clang/ASan/UBSan each returned `ALL 11 exact-body/mocked-IO C host tests passed`**, with no reported sanitizer issue along those executed paths. The initial local test macro with braces was rejected by both C preprocessors; changed to a variadic `TEST(name,...)`, then **both compilers passed**. The committed file is this corrected final version. Test cases:

| # | Actual host test | Observed outcome |
| --- | --- | --- |
| 1 | One `read(handle,2)` | **H,D** in sectors 0/1, returns 2 sectors, state3, handle offset unchanged |
| 2 | Two sequential `read(handle,1)` on **same** handle | **H,H** instead of H,D; absolute cursor offset remains 0 after both |
| 3 | Separate handles / interleaved reads | Explicit seeks select independent starting sectors correctly |
| 4 | One call crossing the 1 GiB `PART` boundary | Correctly reads from mock part0 then part1 within **one call**, but no handle cursor commit |
| 5 | Handle `size=2048`, request `sectors=2` | Returns **2, stat3** and reads from mock adjacent sector anyway: **no logical file-size enforcement** in `func_004c9010` |
| 6 | `sceIoOpen` fails | `stat4`, `return0`, initial offset unchanged |
| 7 | First partial `sceIoRead` succeeds, next fails | `stat4` and return0, buffer partially modified, offset still unchanged |
| 8 | Zero sectors | `stat4` and no read |
| 9 | Negative sectors (`-1`) | `stat4` and no read in this safe range (no general integer-overflow audit implied) |
| 10 | Semaphore wait fails | `stat4` and no read |
| 11 | Offset beyond four 1GiB parts | `stat4` and no read |

**Safety/limits:** The harness deliberately does **NOT** call `func_004c9010(NULL,...)` because frozen body dereferences `h->offset` before any check; the absence of actual range opener could feed a null in the game *if* that unverified route activates. Mock `sceIoRead` returns synthetic 2048-byte H/D/E sectors, never retail assets; no 1 GiB heap allocation, fake fd part offsets, no actual semaphores, cache behavior or threads. Sanitizer success is not PSP Allegrex/concurrency validation. The test does not prove actual shipped EBOOT symbol lookup, game package requests, Junes FPS loss or hardware corruption.

## New direct source link: logical size vs container absolute offset

Frozen [`port/files.c` L104–137](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/files.c#L104-L137) creates `Handle` with `h->offset = (row->where >> 3) * 0x800` **absolute start in selected container**, `h->size=row->size` **logical size**. The build tool [`tools/psp_disc.py` L83–99](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_disc.py#L83-L99) writes `(path_hash, where, size)`, with `where = container_number + (LBA + CVM_BASE/SECTOR)*8`, not sequential size positions. **`func_004c9010` never consults `h->size`**, so a caller can read past the logical indexed file into another part of the container, even if all underlying file I/O succeeds. The host test #5 directly demonstrates this behavior **only when a caller requests too many sectors**; actual game calls may always stay within bounds. This is an **independent missing enforcement** from `h->offset` not advancing.

**Patch-design gotcha:** `h->offset` is an absolute container byte address, but `h->size` is a **relative file length**. Naively testing `h->offset + requestedBytes <= h->size` is WRONG and would reject normal files at nonzero container offsets. A safe future design needs **file base + current sector cursor + logical available sector count / range window**, including `ceil(size/2048)` final padded sector semantics; no arbitrary read into adjacent files, no blind clamp of buffer size, check multiplication overflow and error/partial-read policy. This is a **design requirement**, not an approved fix. No modification to the port until a real linked/active package path is proven.

## Original PS2 behavior and remaining unsolved activation

- Original [`sdkPakUpdateDirectoryLoads FUN_00468A50` `src/sdkPak2.c` L51–99](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/sdkPak2.c#L51-L99) calls `func_004c85a0(path,0)`, then `func_004c9010(handle,1,header)`, then `func_004c9010(handle,sectorCount,directory)` on the same handle; the PSP synchronous stat3 means these may complete in **one update call**. Original [`adx_fs.c::adxf_ExecOne` L1170–1188](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/cri/libadxe/adx_fs.c#L1170-L1188) commits read sectors to `skpos` on READEND or ERROR. Thus **if actual package-directory request is active**, the frozen current PSP cursor cannot supply the correct second sector, whereas ordinary one-shot reads remain correct. A null `handle` from first open is also not checked before PSP read in the original package state; do not claim this actually happened.
- Entry range-open `func_004c8708` remains **not implemented by frozen `port/files.c`**, its address `0x004c8708` is **beyond frozen `tools/psp_reasm.py` CRI cutoff `0x004c0000`**, and the linker may generate missing stub unless another actual symbol definition resolves it. [`tools/psp_link.py` L155–217](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/tools/psp_link.py#L155-L217) writes `build/psp/missing.txt`, `reasm_refused.txt`, and stubs. **No actual generated files/private ELF/p4.log available now**; must inspect first before inventing range-open semantics.
- No evidence that the package request list is nonempty in current Junes or freeze scene; original [`H_Cdvd_UpdateRequests`](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Kernel/sdkCdvd.c#L105-L139) calls package updater even with empty list, so a call stack alone is not activation proof.

## Saturday RESONANCE01 bounded evidence gate (after SEC-01)

First, **offline**: preserve exact frozen perf06 EBOOT/data/strings SHA256; if user-provided private build artifacts exist, inspect `missing.txt, reasm_refused.txt, stubs.c, psp-nm p4.elf` for `func_004c8708` and exact definition. Then **no-asset log events only**: `archive_request_state` and active node counts; `file_handle_valid`, file hash, `logical_file_size`, `base_absolute_sector`, `cursor_relative_sector`, `requested_sectors`, `container_part_boundary`, `io_sema_wait_us`, `io_read_us`, `complete_or_partial`, `stat`. Do NOT dump commercial paths, contents, save strings or sector bytes. Distinguish three physical problems: (A) synchronous I/O stall during scene load, (B) repeated wrong header due cursor, (C) NULL/stub range-open and logical-bounds fail. Validate in PPSSPP T2 first and PSP Go T3 only on safe known scenes, with separate normal perf06 vs RESONANCE01 hashes. SEC-01 game GE safety still P0; STAB-01 edited save and STAB-02 battle child separate P0/P1.

**Result strength:** a real compiled host reproduction of **two specific source behaviors** (nonadvancing same-handle cursor and missing logical-size enforcement), not a confirmed game freeze or FPS gain. No game code/EBOOT, external ISOs, modified saves, Golden assets, PSP tests or release created in this turn.
