# Replay saves for PSP hardware tests

Use the original US PS2 release (SLUS_217.82). Dates below are in-game dates
in month/day format. This is a collection plan for the new playthrough.
Record the date and slot displayed by the game once each save exists.

Keep the regular playthrough in its own slot. Preserve the replay saves in
separate slots, or copy the complete P4 `saves` directory to a dated private
backup when slots are needed. Keep the previous playthrough backed up
before starting a new game. Record the slot alongside any backed-up copy.

| Priority | In-game date | Save point and timing | Short replay |
| --- | --- | --- | --- |
| Optional | 04/11 evening | Dojima Residence, living-room calendar, before sleeping | Opening dream and the transition into the first school day. To test the CRI logo, start the app from XMB before loading any slot (P4-001). |
| Useful if available | 04/12 evening | Dojima Residence calendar before sleeping, or the closest earlier save | Replay the 04/13 morning stat message, after-school classroom and Dojima's evening sitting motion (P4-006, P4-013, P4-034, P4-036). |
| Optional | 04/13 evening | Dojima Residence calendar, before watching the Midnight Channel if the calendar is accessible | First Midnight Channel if pending, then the 04/14 first TV-world visit, green transitions, room obstruction and lighting (P4-005, P4-007, P4-037). |
| Keep | 04/14 evening | Dojima Residence calendar, before sleeping | 04/15 rainy school route and umbrella canopies, assembly, then the TV transitions, tutorial battle, Shadow Yosuke and results screens (P4-006–P4-014, P4-018, P4-038, P4-039). |
| Keep | 04/15 evening | Dojima Residence calendar after returning from Shadow Yosuke, before watching TV or going to bed | 04/15 → 04/16 calendar animation, then the Magician link and the evening rain/umbrella scene (P4-004, P4-008, P4-018). |
| Saved | 04/16 evening | Evening save reported. Exact point relative to watching TV is open. | Direct route to the 04/17 morning Nanako scene (P4-020). |
| Keep | 04/17 daytime | Confirmed Shopping District save before Junes and the TV-world entry | Approach the castle gate, then castle 1F/2F, Shadow Chie and her awakening, Fool link, and return to the real world (P4-021–P4-029). |
| Useful if available | 04/17 daytime | First accessible save after Chie's fight. Use the TV-world hub butterfly before leaving if accessible, then keep the first save after returning as a separate slot if needed. | A hub save can shorten the P4-029 return test. A save made after the return is useful for HOME and as a recovery point. Confirm whether the butterfly is available in this scripted visit. |
| Keep | 04/18 or first free dungeon visit | TV-world hub butterfly before entering Yukiko's Castle | Repeatable 1F/2F field and combat tests: hallway, chest, flying and ground Shadows, battle entry and defeat, missing heads, All-Out portraits and Shuffle Time (P4-011, P4-022–P4-026, P4-030–P4-032). Knock down all enemies to trigger All-Out. Record the floor and enemy name for layout or encounter differences. |
| Keep on arrival | Actual dungeon date | Yukiko's Castle 8F, save point before the boss door | Short boss/effects replay and comparison of dungeon completion and return. Keep the hub save for ordinary fights. |

The tester reported saves on 04/13–04/15 and a successful exit after each.
Keep 04/14 for the 04/15 replay, and 04/15 for the next day transition.
Slot numbers and exact save-menu details are still to be recorded.

Later dungeons can use the same three positions: hub before entry, last-floor
save before the boss where available, and first save after returning if there is
a transition worth testing. Add a separate save before any reproducible
story scene with a regression. When no save point is available in the scene,
keep the closest earlier one and note the route from it.

For a report, record build, PSP model, configured clock, save slot, in-game
date/time, location and exact trigger. Use the same camera movement or enemy
action when comparing builds. On home4, a useful baseline is 333 MHz with
sound on, launched from `CAT_Ports`. For P4-033, test HOME → Yes once
after loading and once after a completed save after playing. If it hangs,
record the screen and copy `p4.log` before another launch when possible.
For the b176 movie issue, note elapsed time since boot and the next
prerendered movie that hangs, including whether picture, audio or input
continues. Keep save files, logs and captures in the private test records.

Calendar and dungeon references for the PS2 release:
[Zoel's walkthrough](https://gamefaqs.gamespot.com/ps2/945498-shin-megami-tensei-persona-4/faqs/53550),
[KADFC's walkthrough](https://gamefaqs.gamespot.com/ps2/945498-shin-megami-tensei-persona-4/faqs/65252)
and the [April walkthrough](https://persona4.wikidot.com/april).
