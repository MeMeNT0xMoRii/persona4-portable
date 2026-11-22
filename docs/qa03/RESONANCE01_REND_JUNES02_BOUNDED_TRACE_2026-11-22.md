# RESONANCE01 – REND-JUNES-02 bounded trace and Igor fallback

PPSSPP 1.20.4, OpenGL at 1×. The existing b259 package from source commit `ee514a5b53f49c47cb93b3ce8c865cf8942e1651` was replayed in disposable clones. No game code or package changed.

## Junes West F009_004 · BLOCKED

- Eight consecutive completed generations (1777–1784) used alternating GE banks. Each root list held the 128×128 render target at VRAM 0x1de000, had no texture-base reference to its extent and ended with FINISH/END. Separately captured execution dumps for three frames in the same stationary scene each contained 27 draws into the RTT and no draw sampling it as texture 0.
- Four CameraClear calls (generations 2594–2597) identified the parallel camera and its raster with mask 3 while the active RTT pointer was null. Decoder validation used the actual function signature: camera in a0, color in a1 and mask in a2. This corrects the previous negative filter. The executed streams clear the displayed target before the RTT bind, then emit two screen clears after restoring the screen target.
- The producer binds the intended RTT. The actual projection commands use perspective W for the parallel camera. The matching b259 floor consumer was not located within the bounded trace. Vanilla's non-indexed floor path and the distinct b388 projection, clear and alpha fixes do not establish that the b259 consumer executed.
- Host validation of the capture invariants passed. Native link and QA07 were not rerun. No Junes code patch or candidate. [Upstream Junes investigation](https://github.com/dreikk91/persona4-psp/blob/b388/docs/visual_issues.md#junes-floor-reflection-missing-and-its-render-pass-paid-for-nothing-found-2026-11-16-b326-b327)

## Igor fallback · STARTED

The existing Velvet Room route reached F008_003. Actual owner is the native Atomic renderer (`func_003be9f0` → `atomic_render_body`). The traced quad has four vertices, two strip triangles, flags 0x73, no texture or prelight, downward normals and RW Cull mode 2. The built-in scoped geometry probe skipped 13 of 13 matching draws and was reset. The unobstructed floor region was pixel-identical in the A/B/A of the tested menu pose. The reported blue patch needs a visible reproduction before advancing the culling candidate.

## Scope and next step

Capture a Velvet pose where the blue floor patch is visible and repeat the same one-quad control. Accepted-present p50/p95 are **NOT_MEASURED** for School and Junes. The existing perf06 timing artifact is instrumented and regular b259 is not. Debugger capture timings are not comparable. b259, UI-WHEEL-02 and REND-CARD-01 are unchanged. **G2 P0_UNVERIFIED.** No PSP Go promotion.
