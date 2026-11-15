# Replay saves for PSP hardware tests

Use the original US PS2 release (SLUS_217.82). Dates below are in-game dates
in month/day format. Slots 1–8 are available as private replay saves from
the home4 playthrough. The private home9 replay archive adds slots 9 and 10.

Keep the regular playthrough in its own slot. Preserve the replay saves in
separate slots, or copy the complete P4 `saves` directory to a dated private
backup when slots are needed. Keep the previous playthrough backed up
before starting a new game. Record the slot alongside any backed-up copy.

| Slot | In-game date | Save point and timing | Short replay |
| --- | --- | --- | --- |
| 1 | 04/11 evening | Dojima Residence, living-room calendar | First dream and transition into the first school day. Check the CRI logo on a cold boot before opening the load menu (P4-001). |
| 2 | 04/12 evening | Dojima Residence | 04/13 morning stat message, after-school classroom and Dojima sitting down in the evening (P4-006, P4-013, P4-034, P4-036). |
| 3 | 04/13 evening | Dojima Residence | Continue to 04/14 for the first TV-world visit, transitions, room obstruction and lighting (P4-005, P4-007, P4-037). |
| 4 | 04/14 evening | Dojima Residence, before sleeping | Take the stairs and select Sleep. After the calendar transition, check the rainy school route, assembly, tutorial battle and Shadow Yosuke (P4-004, P4-006–P4-014, P4-018, P4-038, P4-039). |
| 5 | 04/15 evening | Dojima Residence, after returning from Shadow Yosuke | Replay the Midnight Channel sequence, 04/15 → 04/16 calendar, Magician link and evening rain (P4-004, P4-008, P4-018, P4-040–P4-042). |
| 6 | 04/16 evening | Dojima Residence | 04/16 → 04/17 transition, early-morning Nanako scene and police station (P4-004, P4-006, P4-020). |
| 7 | 04/17 daytime | Central Shopping District, before Junes | Open the area menu with Square, select Leave the shopping district and confirm Junes. Continue through the food court, TV plaza and castle gate to 1F/2F, Shadow Chie, the Fool link and the return (P4-006, P4-007, P4-021–P4-029). |
| 8 | 04/17 evening | Dojima Residence after Chie's Persona and dinner | Resume the regular playthrough. The tester saved here and returned to XMB. |
| 9 | 04/18 after school | Yasogami High School | Open the Town Map and compare destinations. Check Equip categories, item help and Social Stats (P4-050–P4-052). |
| 10 | 04/18 after school | Entrance Hall, TV-world hub | Enter Yukiko's Castle. Replay field Shadows, regular fights, weakening a Shadow with Zio, All-Out, Shuffle Time and HOME (P4-015, P4-026, P4-031–P4-033, P4-044, P4-053). |
| Planned | Actual dungeon date | Yukiko's Castle 8F, save point before the boss door | Replay the boss and compare dungeon completion and return. Keep the hub save for ordinary fights. |

The load menu numbers slots from one. Its underlying directories start at
zero. Slot 9 is `BASLUS-21782Save8` (date index 17, phase 4,
area/room 6/6), and slot 10 is `BASLUS-21782Save9` (date index 17,
phase 4, area/room 20/1). The supplied files passed the retail summary
checksum check. The archive also contains internal slots 14 and 15.
Leave every supplied directory and backup intact.

Later dungeons can use the same three positions: hub before entry, last-floor
save before the boss where available, and first save after returning if there is
a transition worth testing. Add a separate save before any reproducible
story scene with a regression. When no save point is available in the scene,
keep the closest earlier one and note the route from it.

For a report, record build, PSP model, configured clock, save slot, in-game
date/time, location and exact trigger. Use the same camera movement or enemy
action when comparing builds. Use 333 MHz with sound on for the hardware comparison and launch from
`CAT_Ports` if that is the installed location. For P4-033, use slot 7 to
repeat HOME → Yes during Shadow Chie. Use slot 8 to check exit after a
completed save. If it hangs, record the screen and copy `p4.log` before
another launch when possible.
For the b176 movie issue, note elapsed time since boot and the next
prerendered movie that hangs, including whether picture, audio or input
continues. Keep save files, logs and captures in the private test records.

Calendar and dungeon references for the PS2 release:
[Zoel's walkthrough](https://gamefaqs.gamespot.com/ps2/945498-shin-megami-tensei-persona-4/faqs/53550),
[KADFC's walkthrough](https://gamefaqs.gamespot.com/ps2/945498-shin-megami-tensei-persona-4/faqs/65252)
and the [April walkthrough](https://persona4.wikidot.com/april).
