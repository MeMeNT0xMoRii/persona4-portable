# SAS audio feasibility probe

The audio budget is at most 1 ms of main-CPU work per 33.333 ms game tick,
including audio worker threads and system audio work. Time asleep waiting for
SAS or audio output is not CPU work. An isolated average below 1 ms is necessary
but does not establish a worst-case bound during gameplay.

## Build and run

```
python tools/psp_sas_probe.py
```

Set `PSPDEV` to your Windows PSPDEV root first. Output:
`build/sas_probe/{EBOOT.PBP,sas.prx,probe.elf,manifest.json}`. The build uses
`-Wall -Wextra -Werror` and checks PSP import ranges.

Copy EBOOT.PBP into `ms0:/PSP/GAME/P4SAS/` and launch from XMB. Alternatively,
create that directory and load `sas.prx` through PSPLink. The log is always
`ms0:/PSP/GAME/P4SAS/sas.log`; stdout also carries results. A run takes roughly
50 seconds. Quiet synthetic tones are intentional. No ISO resources are needed.

Use `ldstart ms0:/PSP/GAME/P4SAS/sas.prx output2` for the recommended single
stereo-output path. Omitting `output2` tests the traditional audio channel.
Reset PSPLink between runs; do not restart the PC USB server between runs.
After copying the PRX to the memory stick, copy it back and compare SHA256.

Summarize saved logs with:

```
python tools/psp_sas_report.py build/sas_probe/experiments/run4_output2.log
```

## Cases and interpretation

- CPU clock: 333 MHz; grain: 1024 stereo samples at 44100 Hz.
- Baselines: sleeping with no output, then paced silent audio output.
- PCM and synthetic looping VAG ADPCM, 0/8/16/32 voices, room reverb off/on.
- Repeat output-only and idle baselines after the test.
- No log writes or screen updates occur inside measured intervals.

Each RESULT contains raw wall time, audio thread runtime, system busy time
(`wall - idle runtime`) and their averages in microseconds per game tick.
System busy time includes other threads and background activity, so compare
against both idle baselines. The output-only baseline separates the audio
driver cost from the incremental SAS cost. Thread runtime is obtained from
ThreadMan and converted with `sceKernelSysClock2USec`.
Despite the SDK's argument names, that conversion returns seconds and remaining
microseconds, not the low/high halves of a 64-bit microsecond value.

The current probe also samples CPU idle time from a separate high-priority
thread approximately once per millisecond. `busy_us` includes that monitor.
Subtract `monitor_cpu_us` when calculating average audio cost; retain raw
figures for auditing. `peak_busy_bound_us` includes monitor cost, while
`peak_without_monitor_us` subtracts a lower bound on its measured thread runtime.
It does not subtract an estimated idle baseline.

For each adjacent pair of sample points, the peak calculation takes an
enclosing interval covering all 33.334 ms windows starting between them.
Wall timestamps bracket the idle-counter reads; monitor runtime snapshots
bracket those same reads in the opposite direction. This conservatively bounds
non-monitor CPU work over the enclosed windows, in the sampled steady-state
region. It is a bound, not an exact observed 33 ms peak. First/last edges,
configuration and resource loading are not covered. Monitoring can also affect
scheduling; this result still needs confirmation with the game running.

`max_sas_wall_us` includes waiting and is **not** a CPU-time maximum.
`end_flags` helps detect voices that unexpectedly ended; active test voices
should not end. The benchmark pauses old voices before configuring each case.
Synthetic VAG uses predictor 2; real samples, pitch variation, bank loads,
command bursts and streaming still require subsequent tests.

Before claiming the complete game's audio budget is met, integrate music,
speech and effects with real game data and measure command bursts and resource
loading as well. Check AVC playback and transitions.
ADX is not a direct SAS input: inspect the original banks and choose conversion
or a separate decoding path. This probe does not implement the game's CRI API.

## Status (2026-11-07)

The test now runs on the real PSP at 333 MHz. All active voices remained active;
all 20 cases completed. Logs and matching manifests are in
`build/sas_probe/experiments/`. The initial failed run had incorrect clock
conversion and attempted KeyOn on paused voices; its timings are invalid.

The ordinary audio channel (run1, without a sampling monitor) used
0.433-0.439 ms CPU per game tick for 8-32 voices. Audio output alone used
0.349 ms/tick. CPU cost barely changes with voice count, even though SAS wall
time rises to about 9.3 ms per 1024-sample block. CPU time and decode wait time
must remain separate when integrating this into the game.

With Output2 (run4), audio cost after subtracting measured monitor runtime was
about 0.11-0.12 ms/tick. The conservative non-monitor peak bound across SAS
cases was 0.873 ms. The identical binary repeated in run5 gave 0.11-0.12 ms/tick
and a maximum bound of 0.963 ms (16 VAG voices, reverb off); 32 VAG voices with
reverb gave 0.120 ms/tick and a bound of 0.840 ms. Both runs stayed below 1 ms
for the sampled steady-state windows. Ordinary channel peak bounds in run3 reached 1.301 ms, so
its averages alone were insufficient to establish the 1 ms requirement.

Use SAS plus Output2 as the first integration candidate. This establishes
feasibility for the tested steady-state synthetic workload, not a completed
sound implementation or a bound covering the game's bank loading and CRI API.
