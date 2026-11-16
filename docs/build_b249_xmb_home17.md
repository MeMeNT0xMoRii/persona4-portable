# b249-xmb-home17 hardware diagnostic

Home17 starts from [home16](build_b249_xmb_home16.md) at
`31073291a333f35f387dc3c0163dcf7fe09bfcf8`. It addresses P4-052 and the
intermittently hollow date numerals in P4-004. The PSP Go crash in P4-057
still blocks playthrough acceptance. Shuffle Time still needs a hardware
replay with the restored card artwork from home16.

## P4-057 · Persona fusion

The last session in the supplied `P4.LOG` starts as home16. It loads the
TV-world Entrance Hall and enters the Velvet Room. The fusion message,
sprite pack and textures report successful reads. The final Cross event
is `PAD 60.10 0040`. Two controller samples follow, through 60.20 s.
The report supplies the point in gameplay. The log has no exception
address or failing instruction, and its regular writer commits in batches.

Home17 writes the following entries directly to the log during a fusion
attempt so they survive the loss of the normal queued log tail.

| Entry | Observation |
| --- | --- |
| `confirm-rows` | Cross reaches the dispatcher in Normal Spread, Triangle Spread or Search. `mode` and `state` identify the menu phase. `first` and `second` are row indices. |
| `ingredients` | Normal Spread accepted the selected pair. The two fields are Persona IDs. |
| `preview-begin` | The result record is available before the game calculates the Social Link bonus and configures the preview. |
| `preview-queued` | The menu has applied the result preview parameters and moved to its waiting state. |

The two preview entries include the result Persona ID. Home15 completed
Izanagi + Ukobach → Apsaras from slot 10 and Rakshasa + Jack Frost → Power
from slot 15 in PPSSPP. Home16 reloaded the slot-10 save after the fusion
and displayed Apsaras. On home17, the slot-10 replay in PPSSPP 1.20.4
logged Normal Spread step `2a`, ingredients `1` and `26`, and both
preview entries for Apsaras (`14`). This replay used the strict invalid
memory access setting and proceeded through the animation, result and
acquisition sequence. A new home17 save/reload remains to be tested.
The PSP Go exception remains unidentified.

## P4-052 · Social Stats

Open Status, choose the protagonist and press Square from a copy of slot 9
on 04/18. On the original US PS2 disc (`SLUS_217.82`), the five orange
circles are aligned around the black radar and joined by an orange
pentagon. Home16 PPSSPP reproduces the long lines and moves the circles
towards the top of the screen. The PSP census shim narrowed packed X/Y
pairs to 32 bits in `radarDrawSegment`, `datGridEffectDrawStrip` and
`radarDrawNumber`. Those calls now carry both coordinates. The fixed
PPSSPP capture aligns with the US PS2 capture using the same save and
menu state. The package includes the three chart crops.

## P4-004 · Calendar glyphs

Load Dojima Residence in slot 8, examine the stairs and choose Sleep.
In the US PS2 04/17 → 04/18 transition, the upcoming digit is grey in
the outer strip and becomes dark as it moves into the yellow highlight.
On home16 PPSSPP, `18` before the scroll and `19` after the scroll lose
most of their grey fill in consecutive frames.

At 480 pixels wide, the highlight glyph pass should be confined to
columns 66–132. Home16 sent the right edge as the width. `sceGuScissor`
extended it through column 198, allowing the dark pass to cover the grey
glyphs. Home17 uses the difference of the edges for both nonzero-origin
scissors. The regression test now treats the third GU argument as width.
It fails for the home16 code and passes for home17.
[PSPSDK scissor definition](https://github.com/pspdev/pspsdk/blob/master/src/gu/pspgu.h)

The home17 PPSSPP replay restored filled grey numerals before, during
and after the 04/17 → 04/18 scroll and continued to the next morning.
The package compares these phases across the original PS2, home16 and
home17. In the matching home16 and home17 frames the glyphs inside the
yellow highlight were pixel-identical.

## Build checks

The PSP build compiled all 1,416 units. The link checked 19 PSP import
libraries. The address audit recorded zero proof errors across 5,857
rebased entries and 168 switch entries. Full host discovery ran 78 tests.
Home16 and home17 each recorded 73 passed, two skipped and three harness
errors. These include two existing C harness compile errors and the VAG
test's missing Windows C# compiler on this host.

The home17 Town Map replay from slot 9 travelled to Shopping District South
and reopened the map. The territory atlas and markers were visible when the
destination changed to Samegawa Flood Plain. The package records the local
source commit and includes the patch from the GitHub home16 baseline.

## PSP hardware replay

1. Back up the prepared P4 folder and its `saves/`. Replace `EBOOT.PBP`,
   `p4data.bin` and `p4str.bin` together. Start from the XMB at 333 MHz
   with sound. Confirm `b249-xmb-home17` in `p4.log`.
2. From a slot-10 copy, enter the Velvet Room and confirm the second Persona.
   If it crashes again, copy `p4.log` before the next boot. The `FUSION`
   entries identify the last recorded stage. If it completes, accept the
   result and check the animation, acquisition, Compendium and save/reload.
3. Load slot 9 and check the Social Stats chart and the Town Map. Use slot 8
   and watch the next number before and during the scroll. Replay Shuffle
   Time from slot 10 through card reveal and reward.
4. For rollback, back up new saves and copy all three home16 files together.

PSP Go and PSP-3000 acceptance for home17 are pending.
