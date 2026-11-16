# P3P as a reference for PSP rendering (2026-11-15)

Persona 3 Portable (USA) is Atlus's own PSP port of the same engine family. Its EBOOT is a reference for how they
solved effects, lighting, animation and culling on the PSP. It is not code to swap into P4.

## Files

`ext/P3P/` (gitignored):
- `EBOOT.BIN` (encrypted), `EBOOT.dec.elf` (decrypted, tag D91613F0), `PARAM.SFO`.
- `ghidra/`: the Ghidra 12.2 project (`P3P.gpr`, `P3P.rep`; needs the ghidra-allegrex 12.2 extension, loader
  "PSP Executable (ELF)"), `out/decomp.c` (10142 functions), `out/functions.txt`, `out/src_index.txt`
  (source-path string -> referencing functions), `ExportDecomp.java` (headless export script).

Copy `ghidra/` to a local disk before opening it; over the share Ghidra is slow. Re-export:
`analyzeHeadless.bat <dir> P3P -process P3P_EBOOT.elf -noanalysis -scriptPath <dir> -postScript ExportDecomp.java <out>`.

No symbols, but assert calls name the source file, so `src_index.txt` maps modules to functions. The engine is
Atlus's own SDK (`src/awsdk/Aw*.c`), not RenderWare; models are GMO.

| Topic | Module | Functions |
|---|---|---|
| Shuffle Time effects | `battle/result/btlShuffleEff.c` | 0015d200, 0015d388 (task create), 0015d510 (per-frame placement), 0015da70 |
| Battle skill effects | `battle/btlEPL.c` | 00212ec8 .. 00215bfc |
| Particles | `graphics/effect/effParticle.c` | 002c7564 .. 002ca528; update 002c89a8 |
| Particle geometry streams | `graphics/effect/effGeomEngine.c` | 002d6bc4 .. 002d8190; stream getter 002d6e58, billboard build 002d6ff4 |
| Lighting | `awsdk/AwLight.c` | 00318e3c .. 003191b0 |
| Frame hierarchy | `awsdk/AwFrame.c` | 0031770c .. |
| Models | `graphics/model/mdlManager.c` | 002b5128 .. 002ba5e4 |
| Ordering table | `graphics/sdkOt.c` | 002ad338 .. |
| Shadows | `scene/k_shadow.c` | 000d092c |
| Battle camera | `battle/btlCamera.c`, `btlCameraCalc.c` | 001dad20, 001ebe38 |

## Shuffle Time: what P3P does differently

Our b265 profile: `btlShuffleEffStep` ~89% of the frame, mostly the per-particle matrix transform in
`ParticleProjectVertices` (effParticle.c:1072/1084), and a large share in skipped ticks
([shuffle_time_perf.md](shuffle_time_perf.md)).

P3P, read from the decompilation:

1. **The Shuffle Time task is thin.** `btlShuffleEff` only places up to 8 card effects plus 2 global ones each frame
   (set position, rotation, scale on effect handles). Effects are created once and restarted, not re-spawned.
2. **Particle update does no matrix work.** The update (002c89a8) copies each live particle's position, colour,
   size and angle into structure-of-arrays streams (effGeomEngine attributes 1/2/4/0x20, each with its own
   stride), wraps the angle, scales size by the frame step. One pass, no transform.
3. **Billboards are built only at draw time, in world space, and the GE projects them.** The draw (002d6ff4) takes
   vertex memory from the display list (6 floats a vertex: uv, colour, xyz; 4 vertices a particle), computes two
   corner offsets from size and sin/cos of the angle, rotates them by the camera 3x3, and writes center +/- offset.
   No per-particle 4x4 projection on the CPU.
4. **One effect step per frame with a time step**, not one call per tick.

## Compared with P4's code (checked the same day)

P4's `ParticleProjectVertices` (FUN_00487FB0, effParticle.c) is **the same function** as P3P's 002c89a8: same kinds
(1/4, 2, 5, 6, 7), same PTank streams, same angle wrap. The emitter-local branch (`flags & 1`) does a 4x4 transform a
particle in both games; P3P does it with scalar floats, P4 through `p4_particle_matrix` (VFPU since b266). So point 2
and 3 above are not a difference: P4 also fills streams and leaves billboard building to the PTank draw.

What remains different:
- **Calls per presented frame.** P3P steps effects once a frame with a time step. P4 runs `btlShuffleEffStep` once a
  tick, and frameskip gives it three ticks a presented frame in Shuffle Time (b265: 32.5 M cycles in two skipped
  ticks, 16.1 M in the drawn one). The b287 work (`p4_fx_skip_draw`, per-callback timing in the effect tree) is on
  exactly this.
- The b265 profile predates `p4_particle_vfpu` (b266); the share of the 4x4 transform needs a new measurement.
- Still open: whether P4 respawns card effects where P3P restarts them (`btlShuffleEffUpdate`,
  `btlShuffleUpdateRewardEffects`).
