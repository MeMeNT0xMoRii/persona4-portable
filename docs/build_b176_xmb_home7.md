# b176-xmb-home7

PSP test update for the existing US installation, built on 2026-11-14.
The [replay saves](replay_saves.md) cover the calendar, the first castle
visit and the return to Dojima Residence.

| Report | Change | Test result |
| --- | --- | --- |
| P4-033, freeze after HOME → Yes during Shadow Chie | The callback closes the card gate and waits for any active card operation. It polls the graphics engine and resets its queues if a list is still active before asking the firmware to exit. The existing log thread can report the last exit phase it observed. | The host harness exercises idle exit, a stalled graphics list, and a stalled list during a save replacement. A PPSSPP probe notified the registered callback with a stalled list, saw a successful queue reset and exited. The PSP firmware dialog and the reported boss-fight freeze still need a hardware replay. |
| P4-004, overlapping calendar graphics | The day strip uses its depth masks for unselected cells. The selected weekday and day number draw inside the yellow strip with scissor clipping. | Slot 4 in PPSSPP 1.20.4 software rendering showed separated forecast graphics and readable dates 14 and 15 on the yellow strip. The numbered dials and load confirmation still displayed in the same run. |
| P4-044, missing field sound | The SAS player reads the last sample-set index from offset 20 of the `Setb` chunk. The old check read a different header word, which rejected both notes of the field Shadow notice cue. | An isolated PPSSPP test using the original US BSE bank played cue group 3, cue 5 on bank 1, stream 11. The old code rejected two notes and produced silence. The corrected code started both notes, produced 96 nonzero PCM blocks over the probe window and reported zero audio errors. |

The report for home6 confirms the save-slot digits, question marks, the
two field Shadow models and the yellow command portrait and lettering
during a tested skill. It also records the renewed HOME freeze. The log
for that hardware run is still missing.

The home6 emulator replay from slot 7 passed Junes, the TV plaza and the
gate, then reproduced the red rectangular noise during a regular 1F
enemy's defeat. Its ordinary results screen showed the protagonist portrait.
The specific level-up screen, All-Out Attack, intermittent missing heads,
the TV transitions and umbrellas remain in the
[playtest backlog](playtest.md).

## Build and tests

- Linux PSP GCC 15.2.0 compiled 1,416 translation units from the pinned
  source and linked 825 staged inputs. The release has 19 validated PSP
  import libraries and zero unresolved native function pointers.
- The released data image matched 5,857 rebased entries and 168 switch
  targets. Two host clipping checks passed.
- The host suite had 59 passes, two platform skips and three errors. The
  same errors occur on the preceding source commit: the Windows C# VAG
  encoder is unavailable and two legacy renderer mocks fail to compile.
- The PPSSPP setup used the original COMSE and BSE banks and a minimal
  stream index. Music, voice, video and timing on PSP need the existing
  full installation.

## PSP replay

Back up the existing P4 folder with its saves. Copy the three files from
the update's `PSP/GAME/P4` directory into the existing P4 folder. The
startup log identifies `b176-xmb-home7`.

1. Use slot 7 at 333 MHz with sound enabled. Let a ground or flying Shadow
   detect the party on 1F and listen for the notice cue. Continue to
   Shadow Chie and confirm HOME → Yes during the fight.
2. If the game hangs, copy `p4.log` before the next launch. Record the
   action in progress when HOME was pressed and what the screen showed
   after confirmation.
3. Use slot 4 at Dojima Residence and choose Rest to check the 04/14 →
   04/15 transition. Use slot 8 to continue after Chie's Persona.
