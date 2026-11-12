# PSP-3000 playtest backlog

Updated 2026-11-12. These observations were reported by the hardware tester;
they are not independently reproduced fixes. Evidence currently consists of
two initial reports and 15 private photos, plus the b176 startup/HOME follow-up.
Personal photos, saves and disc data are
not included in this document.

## Tested build and progress

- Hardware: PSP-3000. Exact firmware and active plugins are not yet recorded.
- Edition: original US Persona 4, SLUS_217.82; Windows preparation on the tester's PC.
- Earlier gameplay reports: b165 plus save recovery; save/load explicitly
  reported working. Follow-up on b176-xmb-home1 confirms HOME, save/load,
  all supplied XMB artwork and the exact title on PSP-3000. The installed
  EBOOT has not been rehashed; build attribution follows the update exchange.
- Reached Yosuke's Persona awakening, returned home and saved. Reloading this
  particular later save has not been separately confirmed.
- No progress blocker reported up to that point. Slowdowns and stutters occur.
- The tester plans to continue through Chie's Persona awakening; this milestone
  and a full Yukiko's Castle run are not yet confirmed.
- The original delivered EBOOT SHA-256 is
  `3797d600c3dc29f62476d57f6b043c8bef23cc0119110ba11d204848e9af58cf`.
  The installed PSP file has not been rehashed.

## Open observations

| ID | Scene / trigger | Observation | Evidence / current status |
| --- | --- | --- | --- |
| P4-001 | Logo immediately after Atlus at startup | Large white rectangle instead of a recognizable logo | Repeated report; photo. Logo identity not yet established. |
| P4-002 | Igor's introduction and card animation | White screen flashes | Report; timing needs a replay or video. |
| P4-003 | HOME/PS button during gameplay | No visible response or normal exit in b165 | Resolved in the tester's b176-xmb-home1 follow-up: HOME now works. Separate cancel/confirm and save-in-flight scenarios were not individually described. |
| P4-004 | Calendar/day transition, including 04/15 to 04/16 | Background briefly disappears; large overlapping weather icons and changing background tiles | Repeated report and two photos; compare the full animation with the original. |
| P4-005 | First TV-world eerie-room visit, Yosuke's bathroom scene | Camera angle/foreground occlusion looks wrong | Photo with Chie's “Like I care…” dialogue; compare camera and occlusion separately. |
| P4-006 | Question-mark emotion on characters | Symbol appears at the feet rather than above the head | Tester reports every observed occurrence; photo at Teddie's feet on 04/15. Other emotions need comparison. |
| P4-007 | Leaving the TV world after the transition | Brief green screen | Two photos, including a green TV frame and a nearly full-screen green field. Duration unknown. |
| P4-008 | 04/15 early morning, girls gossiping on the way to school | Rain is not visible despite the rainy scene | Repeated report and scene photo; a still image cannot establish the entire weather animation. |
| P4-009 | 04/15 school assembly | Protagonist body appears corrupted | Seen in the first run, absent in the replay. Keep open as intermittent; photos show different dialogue moments. |
| P4-010 | Battle action selection | Green tint/overlay across battle scene | Photo. |
| P4-011 | Defeated enemy disappears | Dissolve animation looks too square | Report; animation comparison needed. |
| P4-012 | Level-up screen | Black/white bars cover the character image | Level-2 screen photo. |
| P4-013 | Text containing inserted player name or stat value | Excessive spacing after the name or in “knowledge … increased” | Name spacing visible in photo; level text also has a large gap before punctuation. Cause unknown. |
| P4-014 | TV-world liquor-store scene | Briefly wrong camera angle/foreground obstruction | Photo. Keep separate from P4-005 until compared. |
| P4-015 | Attack hits an enemy weakness | Eye/face cut-in appears in the lower half of the screen | Comparison question: the tester is unsure whether this differs from the original. Not a confirmed bug. |
| P4-016 | Triangle-button tutorial in Dojima's living room | Large gap before the triangle symbol | Photo; text layout issue, not a report of triangle input failing. |
| P4-017 | Various early-game scenes | Slowdowns and stutters | Report; no FPS/frame-time measurements or precise triggers yet. |
| P4-018 | 04/15 rainy school route | Missing/transparent parts of umbrella canopies | Photo; pale umbrellas show gaps. Investigate separately from missing rain. |
| P4-019 | Move installation into CAT_Ports and launch from normal XMB | Early exit loading data from fixed paths | Photo: HOME callback ready and boot-memory guard passed, then negative data/list results. b176-xmb-home2 replaces fixed runtime paths with the launch directory. Host checks pass; categorized XMB hardware retest pending. |

## Next development checks

1. Categorized-folder startup (P4-019), retaining HOME and save/load coverage.
2. Flashes, green transitions/battle overlay and level-up bars.
3. Emotion placement, camera occlusion and intermittent body corruption.
4. Calendar, rain/umbrellas, text spacing and remaining effects; compare the
   weakness cut-in before changing it.

Any newly discovered progress blocker or save failure takes priority.
Shared rendering-state or transparency causes remain hypotheses.

## Branches and prepared XMB change

- `playthrough/b165-tested`: source baseline matching the tested save-fix code.
- `playthrough/b165-xmb`: baseline plus host-test repair, supplied XMB artwork,
  exact release title **Persona4 PORTABLE**, and these notes.
- `main`: upstream b176, commit `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`
  at fork setup; retained unchanged.
- `playthrough/b176-xmb-home`: b176 plus save recovery, supplied artwork,
  **Persona4 PORTABLE** title and HOME exit support. Selected explicitly by
  the tester for the next build. Sound remains enabled. The above hardware
  rendering observations still belong to b165. The basic b176 functions
  confirmed above do not establish that those rendering issues are fixed.

XMB packaging was checked with actual PSPSDK packaging tools: title and
MEMSIZE verified, all three PNGs recovered byte-for-byte from the expected
PBP sections, synthetic executable payload unchanged. The 16 passing
build/preparation tests and one Windows-only skip are host checks. The XMB
change was initially a packaging-only probe, not a playable build.

On 2026-11-12 the actual **b176-xmb-home1** release EBOOT and matching data
were built. Title, images, executable section, imports and source hashes
were checked again against those real artifacts. The [build record](build_b176_xmb_home1.md)
records 23 passing host tests and one Windows-only skip. HOME exit ordering
was tested during a blocked save-file replacement; this does not exercise
the PSP firmware's HOME dialog. The later tester report confirms working
HOME (P4-003); the rendering reports remain open, with P4-019 added separately.

b176 adds a separate upstream risk: after around 10 minutes with sound, the
next movie may hang in `sceMpeg`. This is documented by the upstream author,
not newly reproduced by our tester. Retest movies after a longer session.

See [XMB presentation](xmb.md) for artwork dimensions and packaging details.

## Latest follow-up: categorized installation

The tester used the ARK5 custom launcher while P4 was under GAME because of
their Categories Lite arrangement. After moving it into CAT_Ports, it appears
in the normal XMB but exits by itself. The startup photo shows the HOME callback
and boot-memory check succeeded, followed by negative data/list load results.
The data error is 0x80010002. Source inspection confirms fixed old install paths.

b176-xmb-home2 resolves the installation directory once from argv[0], with a
working-directory fallback, before starting game threads. Boot data, archive
index/parts, SOUND, VIDEO, NOSOUND, logs and saves use that directory.
Move the entire installation, including saves. No fallback reads an old copy.
Memory-stick paths only (ms0:); unsupported or overlong paths fail explicitly.
Retest XMB startup, existing save load, fresh save/reload, sound, movies and HOME.
The known upstream long-session movie/audio issue is unchanged.
