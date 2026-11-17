# P3P shadow effects as a reference (2026-11-17)

P3P has related shadow effects, but they are not a byte-for-byte version of
P4's orange noise cloud. The useful difference is in the resources and particle
renderer: soft coverage is already in texture alpha, rather than reconstructed
in framebuffer stencil one bit at a time.

## Evidence from the original USA game

Input: local ULUS10512 ISO and its decrypted EBOOT. EBOOT SHA-256:
`be2abbd43a4ae7ce5aa2f1f146083ffe0d924fc2eb1874e6fcdc21cba40d49db`.

`data/battle/btleff.bin` was extracted from the ISO's CPK tables and CRILAYLA
decoded. The decoded archive is 416628 bytes with 68 named BED entries. Both
MEMST.CPK and umd0.cpk contain the same decoded archive. The BED reader at
`001ef794` resolves effect roots and their parts through `002c4ee4` and
`002c11e0`; the constructor table at `00430544` maps their kinds to modules.

| Resource | Size | Effect parts |
|---|---:|---|
| SHADOW_S.BED | 4192 bytes | 4 particle parts, 1 blur-filter part |
| SHADOW_D.BED | 6752 bytes | 7 particle parts, 2 polygon-track parts, 1 blur-filter part |

SHADOW_D is selected from the battle effect table at `DAT_0043cd08 + 0xdac`
by `001c10b4`. SHADOW_S is also loaded separately by `001f2b9c`. The names and
call sites establish shadow-related battle resources; this audit does not
identify every occasion on which they run.

## What makes the particles cheaper

Particle parts reference the global texture bank through resource kind 4:

- SHADOW_S uses texture IDs 6, 21, 8 and 9.
- SHADOW_D uses texture IDs 8, 0, 9 and 21, with ID 21 reused by several parts.
- IDs 6/8/9 are 32x32 textures: soft smoke, a cloud and spots.
- ID 0 is a 64x64 soft disc.
- ID 21 is a 64x64 noise/pattern texture with a soft circular alpha channel.

These textures were read from the running original P3P texture bank
(`00560494`, relative to module base). `002a498c` binds indexed rasters with
swizzling enabled; the inspection images undo the GE swizzle and decode the
RGBA8888 palette. Reading those bytes as linear pixels incorrectly produces
bands. The decoded RGB and alpha are shown separately in
`build/p3p_render_research/shadow_effects/shadow_textures.png`.

The particle billboard renderer `002d6ff4` builds four UV/colour/position
vertices per particle and submits them through `0035c064`. The texture supplies
the soft coverage in this particle path. It does not contain our stencil bit
reconstruction loop. The blur-filter part is separate: subtype 2 reaches
`002e2c3c -> 002e2be8 -> 002e16d0`, which constructs screen-sampling geometry.
This is not evidence that the entire shadow effect is a single draw.

## Consequence for P4

Use the approach, not the P3P assets: prepare P4 noise RGB with a soft alpha
channel, preserve particle tint/lifetime/UV animation, and draw those clouds
with source alpha. This can eliminate the cloud's separate alpha-mask passes.
Its blend result, filtered edges, depth behavior and any later consumers of
framebuffer alpha must be checked against the current path before enabling it.

The scoped P4 implementation and its separate checks are recorded in
[Single-pass enemy clouds (b344)](cloud_b344.md). Original P3P was
run and its loaded textures inspected, but a complete enemy appearance/death
sequence was not captured. The existing `spell1.ppdmp` is a skill frame and
cannot establish the full shadow effect's draw count or hardware performance.

Local extraction helper and binary reports are under
`build/p3p_render_research/`; extracted game assets remain ignored.
