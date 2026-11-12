# b176-xmb-home3

This test build is based on b176-xmb-home2. In the home2 playtest log,
`TRACE guard pool full` appeared 18,218 times. That branch dropped a whole
mesh if the maximum possible clipping output would exceed the per-frame
buffers, even when its actual output would fit.

The renderer now checks actual clipped vertices and kept indices when space
is tight. A mesh is submitted only after both outputs fit. On real exhaustion
the next mesh can reuse the unused ranges. The existing 256-frame log
summary now includes `guard pool recovered ... exhausted ...`.

The host regression test uses the production clipping code and guarded draw
function. It checks a mixed mesh that fits after clipping, the 28-byte lit
vertex layout, and exhaustion of either buffer with untouched sentinels.
25 host tests passed with one Windows-specific skip. Both standalone clipping
checks passed. The PSP release link validated 19 import libraries and zero
function pointers without native targets. The XMB package was checked for
title, artwork, MEMSIZE and matching executable/data files.

Hardware retest at 333 MHz from `CAT_Ports`. Load the same save, approach
a Shadow and the 1F chest from the same direction, and check the party
models in battle. Compare the home3 log counters with what appears on screen.
After a completed save, test HOME. The intermittent exit freeze and the
All-Out Attack and Shuffle Time reports are still under investigation.

Use all three files from the update together: `EBOOT.PBP`, `p4data.bin`
and `p4str.bin`. The existing installation supplies the archives and saves.

## Repositories to compare

- The [direct upstream](https://github.com/dreikk91/persona4-psp) publishes
  b176 at `0d89414`. The public forks list currently shows our fork only.
  Compare new commits against the exact base before selecting a change.
- [P4toPSP](https://github.com/tojikoumori/P4toPSP) describes an independent
  PS2-to-PSP recompiler. At `65e8c63` the public tree has only its README,
  which describes an unresolved transfer during startup.
- [Persona 4 Decompilation](https://github.com/Raikaru/Persona4-Decompilation)
  and [Persona 3 FES Decompilation](https://github.com/Raikaru/Persona3-FES-Decompilation)
  help compare shared engine behavior. The PSP build still uses the pinned
  Persona 4 snapshot because its function addresses are part of the build.
- [PSPSDK's file I/O sample](https://github.com/pspdev/pspsdk/blob/master/src/samples/kernel/fileio/main.c)
  provides a reference for the PSP exit callback.

Use focused diffs with applicable license notices and rerun the affected
hardware checks before adopting an external patch.
