# STAB-02e — AI-script child spawn is a distinct reinforcement candidate (2026-11-20)

**Priority:** STAB-02 P0. **Evidence:** T1 pinned source, T0 linkage to user-observed reinforcement freeze. No T2/T3 or binary trace. Original Raikaru/Persona4-Decompilation **363eab8a966d60a9773b45f330f2344329ec4f0a**, PSP perf06 frozen **29b0fcfdec70222a4052099b67fb4a11e8d8aff4**.

## New exact original code fact

Original [src/Battle/btlEffect.c L628–641](https://github.com/Raikaru/Persona4-Decompilation/blob/363eab8a966d60a9773b45f330f2344329ec4f0a/src/Battle/btlEffect.c#L628-L641), `btlAICommandScriptSpawnChild` / **FUN_001EA2D0**: reads script parameters 0 and 1, calls `btlUnitInitChild(((BtlAction*)action)->unit, childKey)` at **L638**, then `btlAICommandSetActionCode(action, action+0x38, actionCode & 0xffff)` at **L639**. This is an independently identifiable AI-command → child initialization path; it does **not** directly call `btlEffectCreateFormationPacket` in this function. The declaration of that packet function in `src/promoted/code1_001a.c` L174 is not a call-site and must not be counted as evidence.

This narrows the investigation: a reinforcement could originate from AI script child spawning and then fail in child creation, command scheduling, model load, or later rendering. It does **not** establish that the observed freeze uses this AI opcode, or that retail constructor **0x001AC008** is reached. Check script dispatch registration for FUN_001EA2D0, then inspect `btlUnitInitChild` FUN_0019EF30 in `src/Battle/btlUnit_inferred_0019e9f0.c` before pursuing the formation resource-count theory further. Do not equate an AI child with formation resource-copy or the GE watchdog.

## Frozen PSP control and falsifier

[port/rwsky.c L1499–1533](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/29b0fcfdec70222a4052099b67fb4a11e8d8aff4/port/rwsky.c#L1499-L1533) has independent 250ms GE stall reporting and 1s give-up. A freeze without GE queue backlog would weaken the GE-only hypothesis; a freeze before FUN_001EA2D0 dispatch would falsify this particular AI-script origin. No actual trace was captured.

**Minimal proposed test:** non-mutating trace at AI dispatch / FUN_001EA2D0 entry and exit, child init entry/exit, packet enqueue/dependency completion and GE queue/finish/show; compare normal battle, successful spawn, failing reinforcement with same private save and separate untouched original-save control. Use regular perf06 matched hashes as baseline; proposed instrumentation must be separately approved, bounded and nonblocking. **Cost/risk:** small source investigation; runtime trace medium effort, scheduling perturbation and I/O overhead; rollback immediately if negative-control battles, save/HOME, or GE presentation regress. No direct game-source edits or builds in this pass.

**Next queue:** STAB-02f AI opcode dispatch and child init chain; if no link, rotate to STAB-01 Compendium rather than inventing a formation dependency. Golden 120 private pairs untouched.
