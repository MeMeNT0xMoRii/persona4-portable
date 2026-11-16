# b337: skipped-tick font and calendar preparation

Two independent candidates on main after b336. Defaults remain `p4_font_skip=0` and `p4_calendar_skip=0`.
Both are supported by `psp_probe_ab.py`. The existing PSP session is untouched; no new FPS gain is claimed.

## Font

This brings the earlier font-skip experiment onto current main. In an actually skipped tick it omits glyph
camera/UV/corner/vertex preparation and Im2D/native submission. Palette changes, cache references, free-list
updates, used bits and nontransparent glyph texture state remain. Text-list reveal, frame counters, commands,
sound waits, completion and window updates still execute. Native font can be on or off.

The previous hardware font-skip series had a drifting baseline and was inconclusive. Im2D already returns
before vertex conversion in skipped ticks, so the saving is preparation before that boundary, not the entire
glyph submission time. Re-measure it with both native UI flags held at 1.

## Calendar

`calendarDrawBackdrop` mixes state advancement and drawing. The candidate retains the visibility/open-close
counter, flag changes and all five `radarAnimateValue` updates every tick. It omits sprite layout, temporary
allocations, rotation/scale preparation and submission only in skipped ticks.

Before returning it determines the last sprite the original callback would draw, including holiday/period
remapping, field/dungeon/event visibility and the tick clearing visibility flags. It replays that sprite's
device/sky state and restores fog. The state-only helper is generated from the existing `sdkSpriteDraw` state
blocks, uses a stack sample and allocates/submits nothing. Later callbacks retain the inherited render state.

## Validation

- `test_ui_font.py`: 64 cases and 80 production text ticks compare old/skip/native cache, palettes, visible
  vertices, reveal, commands and completion using 32-bit layouts.
- `test_calendar_skip.py`: 9,600 old/new tick pairs use the production calendar and animator. All visibility
  combinations, opening/closing/reopening, periods, field/dungeon/event modes and a zero-duration animator are
  covered. Calendar memory, final device/sky state, fog and shown sprite payloads match each pair. Candidate
  skipped ticks allocate no temporary sprites.
- `test_ui_sprite.py`: 6,144 compact/legacy sprite cases; 3,072 also compare the state-only helper against the
  full renderer's final state, including borders and flags. Transform/state fixtures are host substitutes.
- `test_ui_draw.py`: 8,192 generic/native renderer comparisons, including list capacity and shadow fades.

Builds: `build/psplink_b337/` and `build/psplink_b337_release/`. Glyph, sprite and calendar objects are regenerated
before fresh linking. Use matching executable/data/string files and ELF symbols; fresh-boot from a native save,
not an older executable's savestate.

## Hardware A/B

Keep `p4_ui_font=1`, `p4_ui_sprite=1`, profilers/checks off, ordinary frameskip enabled and the scene unchanged.
Compare `(p4_font_skip,p4_calendar_skip)` independently:

1. `(0,0)/(1,0)/(0,0)/(1,0)/(0,0)`.
2. `(0,0)/(0,1)/(0,0)/(0,1)/(0,0)`.
3. If both help, `(0,0)/(1,1)/(0,0)`.

Record CPU, GE wait and full present intervals with flags/screenshots. Drop the switch-spanning window and
compare neighbouring off arms. Inclusive profiler costs are not predicted savings. At roughly 60 Hz, three
vblanks are about 50 ms for the whole present interval; CPU time alone does not establish 20 FPS.

The existing 256-present report includes `UI skip font ... glyphs ... calendar ... ticks ...`. Counts prove
routing, not saved time; calendar counts include hidden invocations. Restore both skip controls to 0 afterward;
the A/B tool requests this automatically for controls changed by that arm.

Validate reveal, choices, palettes, window transitions, calendar entrance/exit, pause/resume, date/period changes
and field/dungeon transitions, including the first shown frame after skipped ticks. With frameskip disabled,
neither candidate should skip work. Hardware/runtime acceptance and speed remain pending before default-on.

PPSSPP smoke check: fresh b337 EBOOT/native Heaven save, all four UI controls enabled; save-list rendering,
field/calendar HUD, menu opening and returning to the field were inspected. The emulator naturally skipped
very few ticks, so four temporary 66,666-us debt injections exercised the existing bounded frameskip scheduler:
168 font glyphs and 8 calendar ticks took the new paths. The debt was restored; this was a coverage check,
not a performance measurement. `build/emulator_b337/` contains screenshots, log and `forced_skip_coverage.json`.
No new memory-fault/exception lines appeared; existing startup font/JIT-zero and MPEG diagnostics remain.
This does not validate PSP timing, message reveal or all calendar transitions. Controls were restored afterward.
