# Visual differences from the PS2: open and fixed

Differences between the PSP port and the PS2 original that the user has seen on a real PSP. Performance work lives in
[perf_status.md](perf_status.md). Each entry gives where it shows, what differs, what is known, and what to try first.

## Open

### Junes floor reflection missing, and its render pass paid for nothing (found 2026-11-16, b326-b327)

- **Where:** Junes, West Entrance (field 9 room 4; scenes.txt preset "Junes West Entrance"). The user saw low FPS
  here on the PSP. PS2 reference: PCSX2 GS dump
  `PCSX2/snaps/Shin Megami Tensei - Persona 4_SLUS-21782_20261116171340.gs.zst` (04/23 after school, rain, hero alone).
- **PS2 (first frame of that dump, `tools/gs_frame.py`):** before the main frame the game renders into a 128x128
  target (FBP 0x1f8, TBP 0x3f00): a sprite clear, then **the hero's model with its textures** (groups 1-6, triangle
  strips, ~2700 vertices, ALPHA 0x44, TEST 0x517fb, Z write on), then two fans with a fade (rows 0-48 and 48-128,
  TEST 0x317fb / 0x3c803, Z masked). In the 640x448 frame, group 92 reads TBP 0x3f00 as a quad over the floor under
  the hero (screen 231..516 x 337..443). So it is a **floor reflection**; the game's task is named
  "shadow map camera" ("renderTex for shadow"), which is misleading.
- **Port (PPSSPP, b326/b327):** the pass runs (`rtt passes` 1-2 a frame, `rtt atomics` 2-12, `rtt 2d` 3-6; log
  `rtt raster ... 128x128x32 -> 128x128 at vram 1de000` and `1ee000`) but both camera textures read back as all
  zero, colour and alpha, on every read (VRAM read through the PPSSPP debugger, see below). No reflection is drawn,
  and the pass still costs ~10% of the frame here: with `p4_probe_flags` bit 16 (b327 diagnostic: no 3D into camera
  textures) "shadow map camera" went 643 -> 382 k cycles and 0x109 atomics 272 -> 124 k (clock profiler), with no
  visible change. Early in that boot two other 128x128 camera rasters got "no VRAM, drawn on the screen".
- **Likely cause, not yet confirmed:** the model drawn into the camera texture lands outside it or fails a test:
  viewport/offset or scissor of the 128x128 target, the depth test against a buffer the pass does not clear on the
  GE (the PS2 clears with a sprite; the port counts `rtt clears 0`), or the skinned path ignoring `rtt_target`.
- **Try first:**
  1. In PPSSPP's GE debugger, step the frame to the draws while the camera texture is bound (FRAME at VRAM
     0x1de000/0x1ee000) and look at viewport, offset, scissor, depth state and where the triangles land.
  2. Or log the GE viewport/offset/scissor and depth func once per camera-texture pass (`bind_target`/`set_camera`
     in rwsky.c), next to the 2D clear's counters.
  3. Then compare the fixed pass against the PS2 frame, and measure it on the PSP: if the reflection is too
     expensive there, a switch (half the vertices through the far skin level, or off) is the decision of the user.
- **b328-b329 (2026-11-16):** the b328 log (`rtt atomic ...`, the hero's origin in clip space) put the hero at
  clip (-0.998, 0.997), w 526: the "shadow map camera" is a **parallel** RenderWare camera (projectionType 2, camera
  +0x14) and `camera_projection` always divided by z, squeezing the hero into the target's corner. b329 builds an
  orthographic matrix for parallel cameras: the hero now lands at clip (0, -0.23), w 1. The VRAM read below still gives
  all zeros, also with `p4_probe_flags` 32 (b328: no depth test into camera textures), so that read is probably blind
  to PPSSPP's GPU-side rendering (try "Read framebuffers to memory (CPU)" or the GE debugger); the floor has to be
  compared on screen against the PS2.
- **Software renderer (same day):** with PPSSPP's `SoftwareRenderer = True` the VRAM read works. It is a **shadow,
  not a reflection**: the PS2 draws the hero with RGBAQ 0x80000000 (black, textures for the alpha; `gs_frame.py` now
  prints vertex colours), and b329's texture is RGB 0 with the hero's silhouette in alpha, as it should be. Left:
  (1) rows 0-47 have alpha 0xFF across the whole width in the port; on the PS2 fan idx 7 (vertex alpha 0, TEST 317fb
  FB_ONLY) writes alpha 0 there, so the stencil-as-alpha model of camera passes is wrong for that fan; (2) the port's
  silhouette is ~1.5x wider and lower (PS2 x 50-78, y 21-87); check the parallel camera's view window (x/y scale);
  (3) no shadow is visible under the hero on screen yet (group 92: the quad that samples TBP 0x3f00).
- **b330-b331 (fixed):** the floor quad is a non-indexed RwIm3D draw (`func_004106a0`), which the port never drew
  (b324 scoped that entry to Shuffle Time); b330 draws it when it is textured with a camera texture
  (`p4_field_shadow`). b331: draws without DATE in a camera texture write their own alpha into the stencil (was 0xFF:
  the alpha-0 fan made a dark square behind the hero), and the quad's camera texture is clamped as on the PS2
  (CLAMP 5; with GU_REPEAT the silhouette tiled over the room as black spots). Left: the DATE fan's alpha gradient
  (0 -> 0x80 over rows 48-128) is lost, so the shadow is solid black instead of fading.
- **b332:** the fade. In a camera texture an untextured DATE draw with RGB 0 (the gradient fan) writes its alpha into
  RGB, unblended, and marks the slot (`fade`); the shadow quad then blends the floor by `1 - RGB`
  (`p4_shadow_fade`, 0: the even black shadow of b331). Junes texture RGB: weak at row 52, full from ~76.
- **b333:** the game clears the shadow camera before its begin update, when the port still had the screen bound: the
  camera texture was never cleared (`rtt clears 0`) and the silhouettes of every pose piled up into a growing blob
  (Living Room, field 7 room 2). The camera clear now binds the camera's own texture. Confirmed by the user in PPSSPP.
  Diagnostic: `p4_probe_flags` 64 draws no unskinned atomics into camera textures.
- **How the textures were read:** with PPSSPP running the hwdbg build, `psp_debug.Debugger().ask('memory.read',
  address=0x04000000 + offset, size=128 * 128 * 4)` (offsets from the `rtt raster` log line), RGBA8888.
- **Getting a PS2 save there:** the port writes PS2-format saves (`saves/card0/BASLUS-21782SaveN`). A 03/20 save
  cannot reach April days (the day scripts see the late story flags and skip every slot); the user made an April save
  with the scene menu's day picker and copied it into a PCSX2 *folder* memory card (`memcards\mc\p4.ps2`, formatted
  by PCSX2 first, the save as a subfolder). A copy of the save is in `build/pcsx2_saves/`.

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

### Rain and the umbrella's sheen (b269-b270, device check pending)

- **Where:** rainy field days (04/12 school route, 04/15 park). PS2 reference: PCSX2 GS dump
  `...20261115023639_(2).gs.zst`, see [alpha_rendering.md](alpha_rendering.md).
- **What differed:** no rain at all. The rain is a PTank whose particles carry a matrix each (`FldRainUpdate`, data
  flags 0x2008000a, no position cluster), and `draw_ptank_body` returned early without a position cluster. On the PS2
  the rain shows as screen-parallel textured quads about 92x114 pixels at one depth (draws 101-103 of the dump: alpha
  0x3e-0x61, ALPHA 0x44, TEST 0x717fb, so they fail the alpha test and write no depth). The clear umbrella also had no
  MatFX env-map pass (b269 `p4_matfx_env`).
- **b270:** `p4_ptank_matrix` (on) places such particles by the matrix: row 3 is the centre, half of the right and up
  rows are the quad's half extents (as Izanagi's `p4gl_ptank.c`). The `ptank spill` log line counts matrix particles.
- **Check on the PSP:** rain present on 04/12 and 04/15, streaks the right way up, density and brightness against the
  PS2 screenshot; the umbrella canopy keeps the trees behind it; the cost of both in hwperf.

### Velvet Room: a new persona's glow flickers after a fusion (found 2026-11-17, b366-b383)

- **Where:** Velvet Room, the new persona's introduction after a fusion (seen with Satan). The glow flickers; slower
  when the frame rate drops (15 FPS with the pipeline off), so it follows drawn frames, not game ticks.
- **Seen:** screenshots alternate between a darker persona and a brighter one with doubled blades (glow layers).
  Per frame: colour cleared (camera clear mask 3), later a second camera clears depth only (mask 2), 80 tiles with
  blend 0x42 (subtract) darken the bottom row, and the screen copy is drawn ~7 screens a frame with blend 0x58
  (Cs x destination alpha + Cd): the glow depends on the framebuffer's alpha.
- **Not the cause (measured on the PSP):** `p4_ge_raw`, `p4_skin_list`, `p4_frameskip`, `p4_skip_visual_bits`,
  `p4_skip_model_lite`; `p4_keep_previous` (b379, start a frame from the shown one) never runs here (colour cleared).
- **Try first:** the destination alpha the glow multiplies by: what writes the alpha byte in each of our two buffers
  (p4_dst_alpha_passes, alpha writes of the persona's translucent draws); a GE dump (L+R+SQUARE) of two consecutive
  frames, compared in PPSSPP.

## Fixed

### Velvet Room: fusion cards without faces (fixed b386, 2026-11-18)

- **Where:** Velvet Room, the cards of a fusion: frames drawn, faces missing.
- **Cause:** the cards are Im3D quads with RenderWare cull mode 2; back and face are coplanar, and without back-face
  culling the back covered the face (the Shuffle Time cards had the same fault and got culling only for themselves).
  A GE dump replayed in PPSSPP showed the same picture: the port's commands.
- **Fix:** `p4_im3d_cull` 1: CCW-front culling for every Im3D draw with cull mode 2.

### Limousine (Velvet Room): blue patch on the floor around the table (fixed b383, 2026-11-17)

- **Where:** Velvet Room, floor under Igor's table; it came and went with the camera angle.
- **Cause:** a 4-vertex lit, untextured quad without prelight (geometry flags 0x73, normals down) lies on the carpet
  and has RenderWare's cull mode 2 (cull back faces). From the camera it is a back face; the port never culled, so it
  was drawn over the carpet (z fighting decided where).
- **Fix:** `p4_cull_back` 3: back faces culled (CCW front) for lit, untextured meshes without prelight when the game
  asks for it. Culling every mesh (`p4_cull_back` 1/2) cut holes in curtains and walls: our strips do not keep the
  winding, so a general fix needs winding-safe strips first.

| Build | What | Cause | Fix |
|---|---|---|---|
| b219 | Hero's and Naoto's heads missing in battle (b155 onwards) | `effMiscQuatAngle` called `__sqrt(x, 0.0f)` with no declaration. gnu89 made it `int __sqrt(double, double)`, so the result came back from the wrong register. A 0 there made the head look-at NaN, and the look state keeps NaN for good. | census FIXES give it the real prototype. `p4_look_fix` (on) stays as a safety net. |
| b225-b226 | Attack target select: long stripes over the screen (not in PPSSPP) | `btlPanelDrawFrame` / `btlPanelDrawBackdropGrid` leave `scale` unset once the panel has grown; retail passes whatever `$f20` held. On the PSP it was NaN, so the panel frame went to the GE with NaN corners. | census FIXES start `scale` from `p4_panel_scale` (0: no frame, as in the PCSX2 capture). `p4_2d_nan_drop` (on) leaves out 2D draws with NaN corners. |
| b251 | April 15 park: the umbrella's white canopy (`kasa_toumei`) missing, trees painted over it | Its alpha can never pass the GS test (GEQUAL 0x5f, RGB_ONLY on failure), so the failed-alpha pass writes RGB without depth and trees drawn later cover it. | `p4_translucent_late` (on): such proven-failing RGB_ONLY draws are queued and drawn back to front after opaque 3D, alpha test, blend and depth test kept ([alpha_rendering.md](alpha_rendering.md)). Host tests pass; a look on the PSP at both sides of the umbrella is still to do. |
