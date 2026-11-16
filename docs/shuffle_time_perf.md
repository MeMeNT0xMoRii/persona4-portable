# Shuffle Time slowdown, b265, 2026-11-15

Quick live diagnosis requested while investigating Crisis Core. PSP was left in Shuffle Time's Help overlay after the battle. Screenshot: `${P4_HOST}/b265/slow_scene265.png`; preserved raw profile: `build/psplink_b265/experiments/slow_shuffle.log`.

Before enabling section timers: frame about 188.5 ms (~5.3 FPS), CPU about 188.3 ms, GE wait about 0.009 ms. The static arena path is inactive: zero ordinary atomics and zero static mesh-list activity in these windows. No arena optimization was toggled or disabled during this diagnosis.

A brief profile with cleared section nesting depths reported:

| Work, per presented frame | kcycles |
|---|---:|
| Full frame | 54,553 |
| `btlShuffleEffStep` in two skipped ticks | 32,499 |
| `btlShuffleEffStep` in the drawn tick | 16,132 |
| `mainDraw` drawn tick (inclusive) | 3,335 |
| `mainDraw` two skipped ticks (inclusive) | 1,445 |
| 2D rendering (inclusive) | 1,834 |
| Particle rendering (`ptank`, inclusive) | 214 |

The Shuffle Time effect task accounts for about **89% of profiled frame cycles**, roughly 146 ms of CPU cycles at 333 MHz. A large share runs in skipped ticks. These rows are inclusive and should not be summed indiscriminately.

VU0 census also reports about 30,589 interpreted blocks per frame at each of effParticle.c:1072 and :1084: the per-active-particle matrix transform and store in `ParticleProjectVertices`. This points to particle preparation within the effect as a target; it does not individually time those two blocks or prove they account for all of the task's cost.

Separately, frequent `TRACE vertex pool full` messages show particle geometry is refused. This can explain incomplete effects and needs its own capacity/preflight investigation; increasing the pool alone does not address the dominant effect-update CPU cost.

Next fix candidate: retain effect clocks/spawn/lifetime updates in skipped ticks, defer purely visual particle projection/PTank buffer preparation to drawn ticks, and replace the interpreted particle matrix operation with a native loop/VFPU path. Side effects must be audited before skipping the whole effect task. No such fix was made in this diagnostic pass.

Section timers were restored to disabled (`prof_state=-1`), matching the prior device state. Kernel profiling remains enabled. Temporary screenshot/profile probes did not change the game scene.

## Reaching Shuffle Time on demand (2026-11-15)

Shuffle Time after a battle is a roll in `btlEncountRollChance` (src/Battle/btlTarget_inferred_001eec60.c): byte table
`iGpffffa9b0` (EE 0x00763AA0), percent by protagonist turns (0, 1, 2, 3, 4+): retail 50 25 20 20 20. On the PSP it
is at 0x09263AA0 from b305 (EE + K 0x08B00000; `psp-nm` prints it as A 0x00a5faa0, i.e. + load base 0x08804000;
b304 and earlier, K 0x08AE0000: 0x09243AA0). Set all five to
100 (only reader of the table) - lasts until the next reset:

    pokew 0x09263AA0 0x64646464
    pokew 0x09263AA4 0x00000064
    dcache w

Still no Shuffle Time when the encounter has flag 2, or no enemies counted (unkC5A == 0), or btlShuffleSelectMember
finds no cards.

## b287: per-callback timing of the effect tree

Census FIXES in mdlEffectCommitTreeFrame / mdlEffectFlushTreeFrame book each child's kind callback into the hwot
table (needs `profmode t`): key = callback | 1 (advance, +0x08) or | 2 (draw, +0x0C), SKIP bit for undrawn ticks,
inclusive (nested effect trees count again). `p4_fx_skip_draw` (default 0) leaves the draw callback out in undrawn
ticks, for a live A/B with a screenshot.
