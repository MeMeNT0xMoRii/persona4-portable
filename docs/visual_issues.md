# Visual differences from the PS2: open and fixed

Differences between the PSP port and the PS2 original that the user has seen on a real PSP. Performance work lives in
[perf_status.md](perf_status.md). Each entry gives where it shows, what differs, what is known, and what to try first.

## Open

### Battle arena TV screens do not animate (reported 2026-11-13, b225; low priority)

- **Where:** Margaret's boss arena (BTL_BOSS_20D), on the TV screens that hang over the floor.
- **What differs:** on the PS2 the pictures keep changing, like "no signal" static. On the PSP they show one still
  noise picture.
- **Known:** nothing has been measured yet. On the PSP the screens show a coloured noise texture; its name is not
  known yet.
- **Try first:**
  1. Run `p4_probe_census 1` in the arena to find the screens' geometry and texture name.
  2. Find how the PS2 animates them: material UV animation, a texture or CLUT swap, or a raster written each frame.
  3. If it is UV animation, A/B `p4_anim_lazy` 0 / 1. b203 skips the material UV apply in undrawn ticks, so it is the
     first suspect.
  4. Check whether PPSSPP animates them. If it does not, the cause is in the port, not in the device.

## Fixed

| Build | What | Cause | Fix |
|---|---|---|---|
| b219 | Hero's and Naoto's heads missing in battle (b155 onwards) | `effMiscQuatAngle` called `__sqrt(x, 0.0f)` with no declaration. gnu89 made it `int __sqrt(double, double)`, so the result came back from the wrong register. A 0 there made the head look-at NaN, and the look state keeps NaN for good. | census FIXES give it the real prototype. `p4_look_fix` (on) stays as a safety net. |
| b225-b226 | Attack target select: long stripes over the screen (not in PPSSPP) | `btlPanelDrawFrame` / `btlPanelDrawBackdropGrid` leave `scale` unset once the panel has grown; retail passes whatever `$f20` held. On the PSP it was NaN, so the panel frame went to the GE with NaN corners. | census FIXES start `scale` from `p4_panel_scale` (0: no frame, as in the PCSX2 capture). `p4_2d_nan_drop` (on) leaves out 2D draws with NaN corners. |
