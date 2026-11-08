# Hardware test plan, build 97 (written 2026-11-06)

For the agent that runs the next PSPLink session on the real PSP. Goal: numbers that decide the next
optimisation. Nothing in this plan changes code. Report every result with its build number.

## Questions this session answers

1. Heaven: what do b92-b97 changes save (instance replay, guard, mipmaps, per-mesh alpha test)?
2. Title screen and the room of the first save: why are they slow? How much do lighting, the vblank wait and 2D cost?
3. Is `p4_pipeline=1` usable now (b83 fixed its flicker in PPSSPP; never checked on hardware since)?

## Setup

- Build: `build/psplink_b97` (EBOOT/PRX, `p4data.hwdbg.bin`, `p4str.bin`, `manifest.json`, `p4.elf`).
  Its source manifest matches the tree at the time of writing. If any source changed since, rebuild with
  `PSPDEV=<pspdev> python tools/psp_hw_debug.py --out build/psplink_bNN`. Do not
  measure a build whose manifest does not match.
- Toolchain: set `PSPDEV` to your Windows PSPDEV root (see `docs/building.md`).
- Deploy and start as in `build/psplink_session/SESSION.md`:
  - copy the files;
  - check SHA256 on both sides;
  - run `profmode t`, then `reset`, then `ldstart`;
  - confirm the boot line `hwdbg b97 PRX image=` in `console.log`.

  The `hwprof` lines need `profmode t`.
- Module text base: query `modinfo`; it was `0x08804000` in every earlier session. Pass it as `--base`.

## How to run one arm

```
PSPDEV=<pspdev> python tools/psp_probe_ab.py <label> --base 0x08804000 [--set name=value ...] [--shot]
```

What the script does:
- writes the switches and reads them back;
- collects 3 `hwperf` windows of 128 frames and drops the first;
- restores every default in `DEFAULTS`.

Results go to `build/psplink_b97/experiments/<label>.json` and `.log`.

Rules (each was learned from a failed session):

- **One arm at a time.** Never run two arms at once.
- **Ask the user before each scene.** The user stands still in the scene and says when it is ready. Do not move
  the camera between arms.
- **Every scene gets `baseline` first and `baseline_again` last.** If they differ by more than ~3 ms, the arms in
  between are not comparable. Say so instead of ranking them.
- **Look at the picture when an arm changes it.** Run that arm with `--shot` too.
- **Never ask the user to unplug USB while the game runs.** In the 2026-11-05 session that froze the PSP.
- **A dead transport is not a game hang.** If the logs stop, ask the user whether the game still runs. Record that
  arm as incomplete.

## Scene A: Heaven, 10th Paradise (the third save)

Stand at the entrance stairs (the same spot as b85/b86).

| Label | Switches | Watch |
|---|---|---|
| `heaven_baseline` | none | frame ms; `hwprof` mhit, hempty, hbind, dlight, ddraw, msubmit; `instance replay` line |
| `heaven_no_replay` | `p4_instance_replay=0` | CPU and frame change (flowers: 84 copies a frame) |
| `heaven_old_guard` | `p4_guard_world=0 p4_guard_full_cache=0` | `guard` and `mgdraw` sections (b86: 2.1 M and 3.1 M cycles) |
| `heaven_no_mips` | `p4_mipmaps=0` | GE wait |
| `heaven_vsync_late0` | `p4_vsync_late=0` | `present_us` (b86: ~10.5 ms); with `--shot`: tearing? |
| `heaven_pipeline` | `p4_pipeline=1` | frame ms; ask the user: flicker? HUD ok? |
| `heaven_fog` | `p4_fog=1` | cost (expected ~0); with `--shot` |
| `heaven_mesh_alpha0` | `p4_gs_mesh_alpha=0` | GE wait; `gs test` line (PPSSPP: second passes 187 vs 210 a frame) |
| `heaven_baseline_again` | none | control |

## Scene B: the room of the first save (Dojima's house)

Load the first save. Stand still in the room.

| Label | Switches | Watch |
|---|---|---|
| `room_baseline` | none | frame ms, CPU, GE wait, present |
| `room_no_light` | `p4_light_prelit=0 p4_light_unprelit=0` | cost of lighting (the picture gets darker, as in b57) |
| `room_cpu_light` | `p4_ge_light=0` | lighting on the CPU instead of the GE |
| `room_no_guard` | `p4_guard_clip=0` | guard cost (holes in the floor are expected) |
| `room_vsync_late0` | `p4_vsync_late=0` | present time; tearing? |
| `room_pipeline` | `p4_pipeline=1` | frame ms; flicker? |
| `room_no_2d` | `p4_probe_flags=2` | cost of all 2D (HUD, filters) |
| `room_baseline_again` | none | control |

Background: b56 limousine CPU 33 ms. b58 (lighting + guard) roughly doubled it. b73 Dojima 67 ms
(CPU 49, GE 16). The user remembers the room as fast before lighting.

## Scene C: title screen

Wait until the title menu is up and idle.

| Label | Switches | Watch |
|---|---|---|
| `title_baseline` | none | frame ms, CPU vs GE wait |
| `title_no_2d` | `p4_probe_flags=2` | the title is mostly 2D: GE fill of full-screen passes |
| `title_vsync_late0` | `p4_vsync_late=0` | present time |
| `title_pipeline` | `p4_pipeline=1` | frame ms; flicker? |
| `title_baseline_again` | none | control |

If 2D dominates, the next step is a GE snapshot of one title frame. Use `analyze_ge.py` in `build/psplink_session`
to count the full-screen passes and their blend modes.

## What to bring back

1. A results table per scene. For each arm:
   - frame ms and fps;
   - CPU, GE wait and present in ms;
   - the main `hwprof` sections;
   - one line on what the picture showed.

   Save it to `build/psplink_b97/experiments/RESULTS.md`, written in UTF-8.
2. The raw `.json` and `.log` per arm. The script writes them.
3. A verdict per scene. Which of CPU, GE fill or vblank wait is the wall? Which arm moved the frame by more than
   the baseline/baseline_again spread?
4. Anything that broke: flicker, tearing, missing objects, freezes. Give the arm and the time.

## Known values (PPSSPP, b94-b97, Heaven)

These are for checking that the device build behaves the same, not timing figures:
- `instance replay 1 per frame: draws 109 records 23`, with no refusals;
- 17 guarded meshes a frame;
- `gs test ... mesh_alpha 1`: second passes 187 a frame (210 with `p4_gs_mesh_alpha=0`), allfail 22;
- `guard ... full_cache 1 hits 4096 builds 0` per 256 frames.
