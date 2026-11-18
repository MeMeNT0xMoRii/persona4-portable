# Live CPU capture after guard/subdivision were disabled

PSP b366, 333 MHz requested, source ELF from 472a53a. No screenshots or reset.
The baseline is two fresh 128-frame windows with timing sections disabled.
Both p4_prof_clock and p4_clock_prof were then enabled for three windows;
the first profile window is discarded. The two controls were restored to 0,
and RAM readbacks confirm the restoration.

Clock profiling includes instrumentation overhead and counts nested sections.
It does not measure CPU busy percentage, memory stalls or cache misses.
Do not sum parent and child rows or rescale these timings into a purported
exact breakdown of the unprofiled CPU interval.

## Unprofiled frame

| Window | CPU ms | Frame ms | GE wait ms |
|---|---:|---:|---:|
| 1 | 58.902 | 67.141 | 8.139 |
| 2 | 58.295 | 66.695 | 8.320 |

## Clock-profiled sections (inclusive, nested)

| Section | ms/frame |
|---|---:|
| frame | 77.306 |
| tasks | 77.228 |
| atomic | 24.288 |
| qmodel | 19.641 |
| aadv | 18.608 |
| alayer | 13.550 |
| apose | 8.716 |
| asl | 8.033 |
| geskin | 6.668 |
| 2d | 5.486 |
| kadd | 4.368 |
| sdraw | 2.613 |
| sbones | 2.521 |
| kinterp | 2.239 |
| qeffect | 1.694 |
| actrl | 1.610 |
| afp | 1.276 |
| ptank | 1.246 |
| sguard | 1.176 |
| mesh | 1.140 |
| kset | 0.749 |
| qprim | 0.749 |
| altm | 0.667 |
| tbind | 0.661 |
| kmat | 0.441 |
| qfx | 0.387 |
| aenv | 0.381 |
| msetup | 0.363 |
| mhit | 0.354 |
| ftrans | 0.347 |
| ablack | 0.315 |
| aframe | 0.200 |
| aib | 0.155 |
| kanim | 0.089 |
| achild | 0.084 |
| amotion | 0.081 |
| ascale | 0.075 |
| ahandle | 0.072 |
| aattach | 0.071 |
| arebase | 0.056 |
| acover | 0.050 |
| mslot | 0.048 |
| qfld | 0.047 |
| hbind | 0.038 |
| mkey | 0.033 |
| mlights | 0.021 |
| rplay | 0.020 |
| mfrust | 0.018 |
| kblend | 0.015 |
| muvscan | 0.012 |
| gesync | 0.011 |
| hempty | 0.006 |
| lbind | 0.000 |
| memit | 0.000 |
| dstate | 0.000 |
| drestore | 0.000 |
| fsync | 0.000 |
| sector | 0.000 |
| dpose | 0.000 |
| gclass | 0.000 |
| skin | 0.000 |
| dcolor | 0.000 |
| guard | 0.000 |
| gclip | 0.000 |
| dlight | 0.000 |
| stest | 0.000 |
| sbind | 0.000 |
| dframe | 0.000 |
| rlight | 0.000 |
| qsort | 0.000 |
| gloop | 0.000 |
| kframe | 0.000 |
| msubmit | 0.000 |
| gsub | 0.000 |
| smat | 0.000 |
| mcopy | 0.000 |
| ddraw | 0.000 |
| dskip | 0.000 |
| dfx | 0.000 |
| afx | 0.000 |
| dvis | 0.000 |
| ablend | 0.000 |
| vblank | 0.000 |
| qattach | 0.000 |
| mgdraw | 0.000 |
| rguard | 0.000 |
| mshade | 0.000 |

## Tasks

Self excludes measured renderer sections; it still includes animation/model work.

| Task | Inclusive ms | Self ms |
|---|---:|---:|
| mainDraw(drawOt) | 41.471 | 12.904 |
| draw scene(linkOt) | 12.123 | 12.123 |
| draw scene(linkOt) [skipped tick] | 9.506 | 9.506 |
| mainDraw(drawOt) [skipped tick] | 5.959 | 5.959 |
| shadow map camera | 2.027 | 0.818 |
| shadow map camera [skipped tick] | 0.925 | 0.925 |
| --- showraster --- [skipped tick] | 0.554 | 0.542 |
| collision update [skipped tick] | 0.357 | 0.357 |
| npc control [skipped tick] | 0.341 | 0.341 |
| field filter [skipped tick] | 0.336 | 0.336 |
| collision update | 0.294 | 0.294 |
| field filter | 0.246 | 0.246 |
| npc control | 0.245 | 0.245 |
| clear ot [skipped tick] | 0.245 | 0.245 |
| field event [skipped tick] | 0.212 | 0.212 |
| clear ot | 0.204 | 0.204 |
| field 2d panel [skipped tick] | 0.194 | 0.194 |
| renderTex for shadow [skipped tick] | 0.173 | 0.173 |
| field event | 0.144 | 0.144 |
| field 2d panel | 0.135 | 0.135 |
| --- showraster --- | 0.132 | 0.132 |
| renderTex for shadow | 0.128 | 0.128 |
| SceneManager Task [skipped tick] | 0.068 | 0.068 |
| CalcMsgMng [skipped tick] | 0.054 | 0.054 |

Raw capture: `build/psplink_b366_hw_472a53a/experiments/cpu_live_20261117T204941Z`.
