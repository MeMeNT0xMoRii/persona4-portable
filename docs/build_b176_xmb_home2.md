# b176-xmb-home2: movable installation

Based on b176-xmb-home1, retaining upstream b176, save recovery, HOME callback,
title Persona4 PORTABLE and supplied XMB artwork.

Runtime paths now derive from the executable launch directory. This covers
boot data/string tables, archive index/parts, SOUND, VIDEO, NOSOUND, logs and
the saves directory. Normal installation and CAT_Ports use the same code.
Absolute ms0: executable paths take precedence; relative executable names use
the working directory. Invalid/overlong paths stop with a visible diagnostic.
Maximum installation directory length is 159 bytes including its trailing slash.

Validation: 25 host tests, 24 passed and one Windows-only skip; resolver cases
include original folder, CAT_Ports, renamed folder with spaces, cwd fallback,
and invalid/overlong inputs. Release linker validates 19 PSP import libraries
and reports zero function pointers without native targets. PBP artwork and
program sections are compared byte-for-byte, SFO title and MEMSIZE checked.
The release is not yet tested on PSP hardware.

Install the three matched update files EBOOT.PBP, p4data.bin and p4str.bin.
Move the whole existing P4 directory, including saves and converted resources,
to e.g. ms0:/PSP/GAME/CAT_Ports/P4/. Keep a backup first.
The update contains no replacement saves or full game resource archives.

Known b176 movie hang after extended sound playback and visual issues remain
open. No performance or rendering fixes are claimed by this update.
