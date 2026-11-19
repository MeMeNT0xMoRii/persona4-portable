# b388 renderer test 01 — source-only experiment (2026-11-19)

**Purpose:** Benchmark upstream dreikk91's b388 renderer on PSP Go without replacing b388's rendering modules. This branch is based on upstream b388 `59b79763b2b175cb5e1ab6c92dda053f97e25c9f`, NOT a full perf06 merge.

## Minimal P4P compatibility changes
1. `port/paths.c`: allow PSP Go internal storage `ef0:/` alongside `ms0:/`.
2. `port/memcard.c`: synchronize the correct currently selected device instead of hardcoded `ms0:`.
3. `tools/psp_link.py`: restore perf06's verified US Rush/Stop anchor `fGpffff84a0 = 0x7615a0` before GP-derived lookups.

All b388 GE/renderer, skin, mesh, animation, texture, CLUT, particle, fog, and strip-pack code remains exactly upstream.

## Deliberately postponed
perf06's weather-mask code, Margaret fusion-return correction, perf04 mipmap settings, more P4P rendering fixes, Golden HUD layout and other full-branch changes. Don't claim those work. The issue and Golden UI plans remain on `dev/v0.27-perf06`:
- https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/docs/v027_qa03_handoff_2026-11-19.md
- https://github.com/MeMeNT0xMoRii/persona4-portable/blob/dev/v0.27-perf06/docs/v027_golden_ui_layout_qa03.md

## How to verify
- Requires the owner's own SLUS-21782 ISO, pinned decompilation, PSP toolchain, and clean local source build; **no EBOOT is shipped here**. The official b388 Prepare utility may pin pristine b388 rather than this experimental checkout: don't assume it builds this branch.
- Install all matching b388-derived files together, including `p4strips.bin` (offline strip pack); `p4str.bin` is a different string-data file. Do not mix binaries from perf06.
- Use a separate game folder and save backup. Cold-start test the chosen `ef0:/` or `ms0:/` device, loading and saving, HOME, audio and movies.
- A/B same-save same-camera runs at Junes West Entrance F009_004, Samegawa Riverbed F010_002, school corridor and Margaret post-fusion. Record frame times/FPS and visual regressions, particularly particles, Rush/Stop, calendar weather, dungeon fog and Reinforcement transitions.
- Use **unmodified original save** control for Compendium Register and enemy reinforcement freeze. Keep b388 rendering fast paths ON; additional fixes only as separate commits and tests.

## Verification level
GitHub source changes only. No PSP toolchain compile, PPSSPP runtime or PSP Go hardware test has been performed for this branch. Performance claims must be established by the new hardware test, not borrowed from b366's audit.
