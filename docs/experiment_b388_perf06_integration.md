# Experiment: b388 + P4P perf06 (19 November 2026)

**Status: SOURCE BASE CREATED — NOT MERGED, NOT BUILT, NOT HARDWARE-TESTED.**
This branch `experiment/b388-perf06` was initialized from **upstream dreikk91/persona4-psp b388** at `59b79763b2b175cb5e1ab6c92dda053f97e25c9f`. Do not imply this branch contains perf06 code yet.
- P4P reference: `dev/v0.27-perf06` at `3a29e19fec27b094ebdc7d123c294881125bc0a3`.
- Common ancestor: b249 `b23d7dbb67ee49a355af533b84f4caaa138efa29`.
- Integration attempt: **draft PR #1** (P4P perf06 into b388 branch); GitHub reports `mergeable_state=dirty`. It is deliberately unmerged.
- Branches `main`, `dev/v0.27` and `dev/v0.27-perf06` must remain unchanged. Never force-push.

## Real technical overlap
GitHub comparison since b249 finds **122 upstream changed paths**, **108 P4P changed paths**, with **27 modified on both sides**:
`README.md`, `docs/perf_status.md`, `docs/visual_issues.md`,
`port/audio.c`, `port/audio_game.c`, `port/clip.h`, `port/files.c`, `port/ge_dump.h`,
`port/main.c`, `port/memcard.c`, `port/paths.c`, `port/paths.h`, `port/rwmath.c`,
`port/rwsky.c`, `port/swizzle.h`, `port/video_game.c`, `port/vu0.h`,
`tools/local_build.py`, `tools/psp_build.py`, `tools/psp_census.py`,
`tools/psp_debug.py`, `tools/psp_hw_debug.py`, `tools/psp_link.py`,
`tools/psp_prepare_release.py`, `tools/psp_vu0.py`, `tools/test_build.py`, `tools/test_census.py`.

Important: upstream b388's `port/rwsky.c` is substantially reorganized (about 163 KB versus perf06 about 548 KB); replacing it wholesale with the perf06 version would discard b388 performance work and is FORBIDDEN.

## Must preserve / validate when carrying perf06 behavior forward
1. **PSP Go internal storage `ef0:/` and save-device handling.** Current upstream b388 `port/paths.c` accepts `ms0:/` but has no `ef0:/` recognition; perf06 does. Verify `port/memcard.c`, `port/paths.c`, `port/paths.h` together.
2. **Rush/Stop HUD US anchor.** perf06 `tools/psp_link.py`: `VERIFIED_GP_ADDRESSES = {"fGpffff84a0": 0x7615a0}`. Upstream b388 lacks this alias. Migrate carefully, verify target relocation and original PS2 positioning.
3. **TV weather icon masking.** perf06 has a scoped `p4_forecast_weather` wrapper in `port/rwsky.c` and patched callers in `tools/psp_census.py`; no such wrapper is visible in b388 `port/rwsky.c`. Re-evaluate in b388's modular 2D backend and use captured before/after pixel comparison.
4. **Mipmap policy.** perf05's generalized format-bit change caused severe PSP Go regressions; perf06 restored perf04 policy. Compare upstream b388 mip sampling/texture creation to perf04/perf06 **without** blanket texture mip removal.
5. **Margaret seated after fusion.** perf06 patch comes from recovered US game-object translation/census route, not simply `rwsky.c`. Reapply via the matching source/compile path if needed, preserving upstream behavior.
6. Preserve existing supported fixes (CRI splash, fusion/alpha, masks, faces, UI, textures, early dungeon effects, game/sound/home behavior). Check against real hardware, not only PPSSPP.
7. In perf06 project documentation: `docs/v027_perf06.md`, `docs/v027_qa03_handoff_2026-11-19.md`, `docs/v027_golden_ui_layout_qa03.md`. This experiment must NOT mix Golden-UI restructuring into the performance attribution build.
8. New QA: Compendium Register crash after save editing; enemy reinforcement transition freezes at static scene but HOME exits. Use an original, unmodified save as control before attributing to port.

## Upstream features worth testing in isolation
- `p4strips.bin` offline-created strip pack **is separate from `p4str.bin`**; b366 real-PSP audit reports approximately 67.6 ms with pack versus 78.4 ms with runtime-generated strips in held Margaret dialogue.
- Raw GE commands / cache, CLUT8 and large-bone-guard paths require their own A/B tests and image-equivalence checks. Do not disable them merely to make a merge easy.
- Upstream b388 XMB output **has not been hardware-validated** in its release notes; its predecessor b366 diagnostic profile is not equivalent to a finished playthrough.

## Integration procedure for Codex / Work
1. Materialize BOTH repos with full Git history; keep three worktrees: original perf06, pristine b388, isolated integration branch. Find merge base at b249.
2. Capture diff and conflicts (real `git merge` in disposable worktree); resolve semantically, **not** by choosing 'ours' or 'theirs' globally.
3. Consider first a **b388-only baseline build**, then separate opt-in changes (Go storage; Rush; weather; Margaret), each compiled and independently traced on PSP Go. Record sha/manifests for each.
4. Confirm PSP Go 333 MHz, same Save, same route: Junes West Entrance F009_004, Samegawa Riverbed F010_002, school corridor, Velvet Room post-fusion, battle reinforcements and Compendium registration; keep FPS/frametime and visual screenshots.
5. PSP package requires original US SLUS-21782 disc/source, toolchain, matching EBOOT.PBP + p4data.bin + p4str.bin + (where required) p4strips.bin. **No games/retail data, images, saves, build binaries in GitHub.** Do not mix files from different builds.
6. Report regressions and follow clear pass/fail criteria before promoting changes to any maintained branch. Keep performance work and UI refactor in separate experimental branches.

The present branch is an intentionally clean b388 source snapshot plus documentation. A working merged EBOOT.PBP is **not** available yet.
