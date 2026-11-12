# Persona4 PORTABLE XMB presentation

Release builds use the exact XMB display title **Persona4 PORTABLE**.
Diagnostic builds append `HW Debug b<build>` so they remain distinguishable.
The installation folder and save paths remain under `PSP/GAME/P4`.

| PBP section | Source file | Dimensions |
| --- | --- | --- |
| ICON0 | `assets/xmb/ICON0.png` | 144 × 80 |
| PIC0 | `assets/xmb/PIC0.png` | 310 × 180 |
| PIC1 | `assets/xmb/PIC1.png` | 480 × 272 |

These are the three PNGs supplied for this project's XMB design on
2026-11-12. They are embedded without resizing, re-encoding or changing the
artwork. The design was supplied by the tester; depicted game imagery and
logos remain subject to their existing rights and are not relicensed under MIT.

Both `psp_link.py` and `psp_hw_debug.py --release` use `psp_xmb.py` to write
PARAM.SFO and pack EBOOT. ICON1, SND0 and DATA.PSAR remain empty. MEMSIZE stays 1.
The source preparation package includes all three PNGs and the shared helper
in its hashed inputs, so artwork changes invalidate its build cache too.

This change prepares the next build. It does not fix the in-game startup logo,
the HOME button or any other gameplay/rendering issue. XMB appearance still
needs a check on the PSP-3000 after the next build is installed.
