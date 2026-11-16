# b336: native glyph and sprite preparation on main

This selectively adapts the b332 UI candidate (`perf/shuffle-time`, `4aee26d`) onto main after `840c210`.
It does not merge particle experiments, animation fusion, static vertex packing, font skipping or the message
profiler. Both `p4_ui_font` and `p4_ui_sprite` are supported by `psp_probe_ab.py` for A/B.

**b339:** `p4_ui_font=1`, `p4_ui_sprite=1` and `p4_fog=1` are now the defaults (user decision, 2026-11-17). A fresh
b339 boot on the PSP read all three as 1 and loaded a save. The wider UI checks listed below (reveal, choices,
portrait changes, menus, battle UI, Shuffle Time) are still open.

## What changed

The census generates compact variants of `frFontRasterDrawGlyph` and `sdkSpriteDraw`, keeping their original
bodies as switchable fallbacks. They prepare four 24-byte vertices with normalized float UV, packed RGBA8888
and float XYZ, instead of four 64-byte PS2 vertices. The existing renderer still emits its original 96-byte
float-UV GE payload. No UV quantization, atlas, whole-line cache or draw-call merging is introduced.

Glyph cache references, palette replacement, free-list/used bits, opacity, widescreen coordinates and text-list
reveal/commands/completion remain intact. Sprite calculations retain pivots, integer scaling, origins, UV flips,
rotation, colour expansion/tint/fade, borders, extensions and state setter order. Initially null sprite rasters
retain the original path, which can read unwritten UVs; this change does not invent coordinates for that case.

The implementation lives in `im2d.h` and shares state setup and emission with the current main Im2D path.
Main's list-capacity guard, shadow fade, alpha masks and alpha-test second-pass format reuse are preserved.
Framebuffer feedback, alpha-only pixel masks and active 2D diagnostics fall back through generic Im2D.
Frameskip, pipeline timing, finite-coordinate guards and current target/texture scaling remain in force.

## Evidence and acceptance

Real PSP b1332 (the earlier b332 candidate, build label changed) showed a gain in an unchanged ten-character
Margaret dialogue at 333 MHz, profilers off and font skipping off:

| Arm | Native font + sprite | FPS | CPU ms |
|---|---|---:|---:|
| off1 | 0 | 13.69 | 51.44 |
| on1 | 1 | 14.90 | 48.78 |
| off2 | 0 | 14.26 | 50.91 |
| on2 | 1 | 14.86 | 48.91 |
| off3 | 0 | 14.20 | 50.85 |

Both on arms beat all off arms, approximately +0.6..1.2 FPS and around 2 ms CPU saved against neighbouring
measurements. Static dialogue text/name/portrait pixels matched; only moving characters differed. This result
does not establish b336 performance after integration with newer main changes. Reduced GE synchronization wait
also does not prove reduced total GPU work; ordinary vertex payload and state commands remain equivalent.

Checks on main:

- `test_ui_font.py`: real 32-bit glyph/cache layouts and production functions, 32 glyph cases and 80 text ticks
  covering palettes, references, transparent glyphs, reveal, commands, sound waits and completion.
- `test_ui_sprite.py`: 6,144 comparisons of compact vs original vertices/state, including flips, borders, rotation,
  tint/fade, scale and extension strips. Unchanged transform providers use deterministic host fixtures.
- `test_ui_draw.py`: 8,192 generic/native command and GE vertex comparisons, including full lists and shadow fades.
  A one-time comparison against pre-integration main also passed all configurations.
- Target glyph/sprite C objects regenerated and compiled; fresh link inputs precede diagnostic/release builds.
  Focused feedback and widescreen checks pass. Older alpha-mask/translucent/render-state test fixtures have
  unrelated missing dependencies/prototype mismatches and remain failing; this change does not claim a green suite.

Builds: `build/psplink_b336/` and `build/psplink_b336_release/`. Install matching executable/data/string files
together and fresh-boot from a native save, without an older executable's savestate.

Before changing defaults, validate text reveal, choices, portrait changes/slides/fades, menus, battle UI and
Shuffle Time on PSP. Repeat same-scene old/new/old with `(p4_ui_font,p4_ui_sprite)=(0,0)/(1,1)/(0,0)` and
record exact b336 artifacts and controls. The hardware session is not modified by this integration.

PPSSPP b336 smoke check: a fresh EBOOT boot loaded the copied Heaven native save, with both UI controls
enabled. The save list, field HUD and Status screen with its large protagonist portrait were inspected.
Screenshots and the isolated process log are in `build/emulator_b336/`. No new memory-fault/exception lines
appeared; existing startup font/JIT-zero and MPEG diagnostics remain. This does not replace hardware testing
of b336 or validation of dialogue transitions. The two controls were restored to 0 after the smoke check.

b337 adds separate default-off [font/calendar skipped-tick candidates](ui_skip_perf.md) for the next A/B.
