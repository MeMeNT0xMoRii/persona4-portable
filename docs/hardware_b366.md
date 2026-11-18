# b366 PSP performance audit (2026-11-17)

Completed hardware measurements for commit `472a53ad4e55dd053a4f4bfd5ca5822b913ed3df`.
The held Margaret dialogue has no measured FPS benefit from double lists or
the new lazy bone-frame experiments. Raw GE commands and triangle strips have
clear benefits. Temporal animation shortcuts have a smaller CPU benefit;
turning off all animation/model shortcuts together costs substantial time.

## Provenance and method

The diagnostic renderer and entry code were rebuilt into
`build/psplink_b366_hw_472a53a`. Its PRX, EBOOT, data image, strings and strip
pack have the same SHA-256 as `build/psplink_b366`. The 821 inherited retail/C
objects match the original b366 manifest. Port sources match the commit after
normalizing checkout LF/CRLF. Debug ELF bytes differ; probes use the fresh ELF.
PRX, data and strings copied to the PSP were read back and their hashes checked.
Details: `build/psplink_b366_hw_472a53a/hardware_provenance.json`.

PSPLink boot confirmed b366. The build requests 333/333/166 MHz; frequency was
not independently calibrated. PSPLink `profmode o`, pipeline and frameskip on.
Margaret's post-battle dialogue was held on the same text, without user input.
Each arm explicitly sets its controls and takes four 128-frame windows;
the first is discarded. Tables average the three retained windows.
Eleven repeated baseline arms bracket the groups. No emulator numbers are used.

The initial `hw_heaven_raw0` crossed from Heaven into battle while the user
played and is excluded. Two timing arms had screenshots before the user asked
to stop screenshots; the active next arm was stopped and restarted without
one. All subsequent timing arms are logs only. No visual equivalence or
transition-correctness claim follows from these measurements.

## Results

34 arms, 11 repeated baselines, 13056 retained displayed frames.

Baseline CPU: 48.10–48.43 ms; frame: 67.12–67.99 ms (about 14.8 FPS).

| Arm | CPU ms | Frame ms | GE wait ms |
|---|---:|---:|---:|
| base_a | 48.102 | 67.641 | 19.485 |
| raw0 | 52.115 | 75.721 | 23.554 |
| double1_raw1 | 47.909 | 67.336 | 19.374 |
| double1_raw0 | 52.454 | 77.721 | 25.212 |
| base_b | 48.193 | 67.901 | 19.655 |
| lazy0 | 49.155 | 68.078 | 18.867 |
| far0 | 48.554 | 67.164 | 18.557 |
| skipkadd0 | 48.486 | 67.120 | 18.580 |
| base_c | 48.302 | 67.208 | 18.852 |
| bones0 | 52.474 | 68.250 | 15.722 |
| model_lite0 | 51.965 | 70.779 | 18.758 |
| native0 | 48.225 | 67.164 | 18.884 |
| base_d | 48.413 | 67.120 | 18.653 |
| anim_conservative | 61.600 | 78.125 | 16.468 |
| frame_lazy1 | 46.558 | 67.034 | 20.421 |
| sync_fresh1 | 47.713 | 67.466 | 19.697 |
| base_e | 48.435 | 67.500 | 19.012 |
| base_f | 48.191 | 67.992 | 19.744 |
| temporal_off | 51.941 | 68.951 | 16.957 |
| temporal_off_native1 | 53.284 | 72.898 | 19.559 |
| temporal_bones_off | 59.185 | 77.938 | 18.699 |
| temporal_off_repeat | 51.802 | 68.656 | 16.798 |
| base_g | 48.280 | 67.643 | 19.308 |
| base_h | 48.187 | 67.646 | 19.401 |
| skin_strip0 | 52.824 | 83.393 | 30.515 |
| strip_pack0 | 51.331 | 78.424 | 27.037 |
| rtt0 | 48.213 | 67.164 | 18.898 |
| matfx0 | 47.887 | 67.310 | 19.368 |
| rtt_matfx0 | 47.906 | 67.120 | 19.160 |
| base_i | 48.297 | 67.294 | 18.942 |
| base_j | 48.427 | 67.341 | 18.856 |
| guard_big1 | 48.486 | 67.688 | 19.144 |
| base_k | 48.212 | 67.599 | 19.333 |
| final_trial | 50.974 | 67.547 | 16.518 |

Exact controls and all samples: `build/psplink_b366_hw_472a53a/experiments/all_timing_arms.json`.
Individual JSON/log pairs remain next to it. `base_*` uses the source defaults
for the tested controls. The suite scripts in `build/b366_hw_*.py` record order.

## Interpretation

- **`ge_raw` stays on.** Off costs about 4 ms CPU and pushes the dialogue to
  75.7–77.7 ms/frame, depending on the list mode. This test covers b365 and b366
  together; it does not isolate each individual raw-command replacement.
- **`list_double` stays off.** At raw=1, 47.91 ms CPU / 67.34 ms frame is within
  the repeated baseline range. At raw=0 it also produces no improvement.
- **Strips and strip pack stay on.** `skin_strip=0`: 83.39 ms/frame, 12.0 FPS.
  `strip_pack=0` (runtime-built strips still enabled): 78.42 ms, 12.75 FPS.
  Both are slower than the ~67.6 ms / 14.8 FPS baseline.
- **`guard_big=1.0` stays off after the test.** Baselines before/after were
  48.43/48.21 ms CPU, 67.34/67.60 ms frame; enabled was 48.49 / 67.69 ms.
  Its report reached 256 uncut, 0 cut, longest edge about 9: there is little
  guard-cut work to save here. The lack of a win does not establish safety
  near a different camera or a large triangle.
- **Lazy interpolation alone saves about 1 ms CPU here**, with no convincing
  frame-time improvement. Far animation/held-kadd individual deltas are small.
  **Bone skipping is useful**, but setting `frameskip_bones=0` also prevents
  lazy/skip-kadd activation, so its ~4 ms delta is not pure matrix-build cost.
- **`skip_model_lite` is useful:** off costs about 3.7 ms CPU in its arm;
  the frame window is more variable, averaging 70.78 ms.
- **Native AddAnimTime/keymap are conditional.** Off under the ordinary lazy
  baseline is indistinguishable from baseline. With temporal shortcuts off,
  native=0 was faster than native=1 in this session. Do not extrapolate one
  baseline-only result to all combinations.
- **All conservative switches off together are expensive:** 61.60 ms CPU /
  78.13 ms frame. Keeping model preparation skips but also disabling bone
  skips gave 59.19 / 77.94 ms. They are not recommended as a blanket toggle.
- **`frame_lazy=1` saves about 1.7 ms CPU, `sync_skip_fresh=1` about 0.6 ms**,
  without a convincing displayed-frame gain. Both remain off for the final
  correctness trial. No visual test was requested for these timing arms.
- **RTT and MatFX are inactive in this dialogue.** Their counters emit no
  active-pass reports across these windows. Disabling either or both changes
  little; this is no evidence about cost in a scene that uses them.
  MatFX env-off is `p4_matfx_env=0`; `p4_matfx_env_ge` alone is not that switch.
  `p4_rtt=0` only prevents new slots: already-created slots still render.
- **16-bit vertex packing was retained throughout.** RAM readbacks confirmed
  `static_position_pack=1`, `static_uv_pack=1`, `skin_pack=1`; fallback float
  paths remain possible for ineligible geometry.

## CPU/GE timeline

Five separate 24-frame captures cover the four raw/double combinations plus
a repeated baseline. They are outside performance windows and timeline=0
is acknowledged during every timing arm. Logging changes execution cost:
these captures tend to release the gate around 83 ms instead of the ordinary
~67 ms frame interval. The gate stays near the present-resume point in both
list modes; the measured double-list mode does not reach the hoped-for 50 ms.

The first-draw field is a stall-flush marker and is absent (`-1`) in most rows.
It does not establish first geometry submission or GE idle time. Initial/stale
rows with implausible timestamps and a transient interrupted capture must not
be interpreted as steady GE durations. In particular, do not add the logged
start-to-show latency to `hwperf` CPU or treat it as serial GE time.
Raw `*_timeline.log` files and `dlg_timeline_summary.json` preserve the evidence.
The timeline is useful for the gate/present relationship, but these markers
are insufficient for a complete GE idle/bus-contention attribution.

## Final live correctness trial

After all comparisons, the following values were applied to the running b366
and every word was read back (`final_runtime_controls.json` and `.log`):

```text
p4_ge_raw=1                 p4_list_double=0
p4_skin_strip=1             p4_strip_pack=1
p4_frameskip_bones=1        p4_skip_model_lite=7
p4_anim_lazy=0              p4_anim_skip_kadd=0
p4_anim_far=0               p4_anim_far_kadd=0
p4_anim_native=0            p4_anim_keymap=0
p4_frame_lazy=0             p4_sync_skip_fresh=0
p4_rtt=0                    p4_matfx_env=0
p4_matfx_env_ge=0           p4_guard_big=0.0
p4_timeline=0               p4_probe_flags=0
```

This profile measured **50.97 ms CPU / 67.55 ms frame (~14.80 FPS)**.
It gives up about 2.7 ms CPU headroom versus baseline without losing the
dialogue's displayed-frame step. It is a live debugging profile, not a claim
that the reported animation glitches have been fixed. **Source defaults were
not changed; these RAM settings reset on restart.**

The user reports a defeated character remaining standing, then falling and
rising during revival, and Margaret briefly bending again after picking up
her book. Neither death/revival nor the book transition was replayed during
the held-dialogue measurements. Those two transitions remain the next
correctness checks with this profile, then individual toggles if needed.

## Scope

No new CLUT code was added, as requested. `p4_clut32` is not in the tested
b366 ELF; it exists only in another local worktree/build family. It cannot
be switched on in this session. No other local build was silently substituted.
The MatFX/RTT ablations need a separate scene with active passes before any
general performance conclusion; no such second-scene result is claimed here.

See [all 230 runtime controls](runtime_controls_b366.md) for defaults,
dependencies, modes, source locations and tool support.

## Subsequent live state: animation bug reproduction

At the user's request after the performance series, the original b366 animation
settings were re-enabled: `anim_lazy`, `anim_far`, `anim_far_kadd`,
`anim_skip_kadd`, `anim_native`, `anim_keymap` and `frameskip_bones` are 1;
`skip_model_lite` is 7. `frame_lazy` and `sync_skip_fresh` remain 0.
All ten words were confirmed by RAM readback. Other controls were untouched.
This supersedes the animation part of the live trial profile above; its timing
results remain historical measurements of that trial.
Evidence: `experiments/animation_enabled_runtime_controls.json` and `.log`
under the same diagnostic build directory.
