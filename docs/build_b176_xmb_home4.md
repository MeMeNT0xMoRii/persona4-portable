# b176-xmb-home4

The PSP Go playtest at a 333 MHz setting froze after the tester confirmed
Yes in the HOME dialog. The home3 log reached frame 21,760. It has no
callback phase markers, so the stall location remains open.

The exit callback stops new card commands and waits for the current
operation to release the card lock before calling the firmware exit.
It no longer calls `sceIoSync` itself. The save operation still syncs
before and after installing the replacement file. The host regression
test covers idle exit, exit during a blocked replacement, preservation
of the old save and rejection of new writes once the exit starts.

The US game's field rain task passes per-particle matrices and colour
through its RenderWare PTank. The old renderer only accepted particles
with a position cluster. This build uses each rain matrix's position and
right and up vectors to draw the textured quad with the existing near
clipping, UV and colour handling. The first submitted matrix draw logs
`TRACE ptank matrix drawn` with its flags and output vertex count.

The PSP release link validated 19 import libraries and zero function
pointers without native targets. Check the HOME dialog and the look and
cost of rain on the PSP. For the rain, use the 04/15 school route or the
04/16 evening scene. The umbrella canopies have their own report (P4-018).

Install `EBOOT.PBP`, `p4data.bin` and `p4str.bin` from the same update in
the existing P4 directory, including a categorized `CAT_Ports/P4` install.
Load an existing save and test HOME after selecting Yes, then repeat after
playing and completing a save. See the [playtest backlog](playtest.md)
for the open graphics reports.

## First hardware report (2026-11-13)

The new-game run reached the transition after 04/12. Rain was visible on
04/12 (P4-008), and one HOME attempt returned to XMB (P4-033). Umbrella
canopies were still intermittently transparent (P4-018). The run also
reproduced the CRI logo, calendar, question-mark placement and battle
tint reports. The opening character motions and Dojima's lingering model
are recorded as P4-034 and P4-035. A repeat HOME test after longer play
and a completed save remains pending.
