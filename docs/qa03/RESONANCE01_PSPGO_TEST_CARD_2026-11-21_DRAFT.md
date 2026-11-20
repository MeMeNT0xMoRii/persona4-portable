# Persona4-PORTABLE v0.27 — RESONANCE01 PSP Go test card
**Draft for Saturday, 21 Nov 2026.** This is a planned manual T3 test checklist; no new EBOOT exists yet. PPSSPP T2 test results cannot replace PSP Go observations. Only test builds accompanied by an exact manifest and confirmed matched EBOOT/p4data/p4str. The user should preserve original saves, user-supplied disc installation and immutable perf06 control.

## Before copying to PSP Go

- PC: record source commit, compiler, build/profile, EBOOT.PBP, p4data.bin, p4str.bin SHA256 and p4files.idx/assets version. No mixture of normal and diag EBOOT/data. Verify actual EBOOT startup ID/log profile are **RESONANCE01**, not frozen perf06 `BUILD=249` label. Confirm Codex had access to the real Windows PSPDEV build environment, rather than accepting an unbuilt source draft. Keep a separate local backup of known-good perf06 including its data files and user saves.
- Device: normal homebrew capable PSP Go (64MB PSP memory configuration). Test with same firmware/settings/CPU clock (expected 333MHz where supported), audio volume, display brightness and identical save/scene/camera. Store RESONANCE01 in a dedicated installation/copy procedure vetted against current loader/save paths; do not overwrite live saves without a backup. Full install from user's own existing game archive index, SOUND and VIDEO if required; no PS2 ISO is to be distributed.
- **Save/asset isolation note:** frozen `port/paths.c` roots `p4_save_dir` inside each actual EBOOT folder; moving regular and diag-lite to different install directories changes the saves/card0 and card1 paths. Prepare identical SHA-verified **private save clones** for each profile before A/B, plus matching p4files.idx and game containers/SOUND/VIDEO. If storage cannot hold multiple full installations, swap verified matched EBOOT/data/string profile triplets inside one distinct isolated P4TEST install **only while game is closed**, restore the same save clone before each leg and keep original perf06 installation and saves offline untouched. No new runtime path rewrite is assumed.
- Play regular RESONANCE01 first, then matching diag-lite and, if supplied/validated separately, fusion-exp. Do not use a serial GE dump or P4_FRAME_TIMING EBOOT as a normal FPS comparison.
- Collect 128 **successfully accepted display submissions**, 20s idle and 10s moving for measured field scenes; log p50/p95 frame time, game ticks/drawn/skipped, CPU and GE queue where available; log filename and timestamp. User-visible FPS snapshots may help identify scenes but are not sufficient paired performance evidence.

## Numbered hardware test route (mark PASS / FAIL / NOT RUN for each)

| ID | Scenario | Core expected behavior / diagnostic question |
| --- | --- | --- |
| G00 | Cold boot from XMB / title | Boots repeatedly without black screen, audio starts, controller works; note first load seconds and exact build/profile; no missing required file dialog |
| G01 | Intro movie then skip/start gameplay | Movie plays, subtitle/audio if present, skip via Start/Circle, no stale double buffer, hang or broken subsequent rendering |
| G02 | Load a backup normal save; school hallway F006_002 | Negative performance control: room/geometry/text/characters/footsteps, 20s idle+10s walking; record p50/p95 and no new stutter |
| G03 | Junes West F009_004 | Major GPU/RTT/camera/parallel/shadow test: same camera and time of day, 20s idle+10s walking; watch shadows/depth, false lines, accepted-present p50/p95, RTT actual slot/full/half + feedback mode |
| G04 | Shopping North F008_001 / South | Wall signs/facade, posters, foliage, lighting, transparency, camera; no CLUT/mip shimmer regression, paired CPU vs GE bottleneck |
| G05 | Field HUD calendar date/dayphase/weather | Top-right group anchors stable during time-of-day/weather changes, don't reinterpret Golden's differing date as an engine bug; no clipping or double xscale |
| G06 | Standard battle, 1–4 party members | Right team portraits/HP/SP and combat menu intact; wheel target yellow ~111×111 around (64,203) if UI-A patch enabled, all three rings, 8 command rows, cursor 0/3/7 |
| G07 | Enemy reinforcement wave from private standard and problematic save clones | Freeze vs recover, cold child vs existing child, 52-allocation first-unit branch, formation packet completion vs GE queued/finished; no raw save upload |
| G08 | Attack damage / Rush / target change / skill and item menus | Damage numbers track enemy position, wheel glyph/selection strip remain aligned, no missing outline/black panel or unexpected command |
| G09 | All-Out / eye cut-in / pursuit effects | Original art and color/alpha/UV, 2-/3-/4-party permutations where reachable; no black TV frame geometry stripes; extra GE-list headroom |
| G10 | Velvet Room entry, Igor/Margaret chair animation, normal two-parent fusion | 2-parent input select, result previews stable with NO manual popup while merely browsing **or on level-too-high/owned rejection or confirmation-dialog NO**; original 0x2D request is not YES, no wrong Persona, visual flicker or audio loss |
| G11 | Manual skill selection two-parent, if feature ON | List only legal available inheritances **after true YES and validated non-accident outcome**; exact slot count, Scroll/Select/Undo/Cancel; result record and 8-bit mask correct, no double-confirm reroll; accident result respects Vanilla policy; cancel leaves original saved/preview state unchanged |
| G12 | Triangle and 4–6 parent fusion, if feature ON | Correct mode-specific entry and exit, all inherited skills preserved, legal lists; if EXPERIMENTAL flag then mark separate from regular and don't silently pass untested mode |
| G13 | Apsaras and Zio/fusion glowing effects | Compare glow/alpha and fused Persona animation to same scene; no flicker/missing blue effect, no transition crash |
| G14 | Compendium Register/Withdraw, normal then minimally/heavily modified *private save clones* | No invalid Persona ID metadata read, NULL cache copy, wrong sort slot/record alias, unbounded loops or save corruption; backup untouched source |
| G15 | Save → close game → cold relaunch → load → new fusion | Inherited skills survive save/reload, no unselected sentinel or overwritten source Persona, book data consistent; preserve clean original save backup |
| G16 | HOME suspend/resume, change area, movie→title, exit | No stuck GPU bank/unfinished GE list, sound hang, lost log tail or corrupted data; incident file sealed/verified on failure |
| G17 | Fog-heavy dungeon/riverbed/TV world | Depth/fog/alpha/CLUT and CPU/GPU queue, native alpha masks and 2D NaN drops safe, especially GPU list highwater |
| G18 | Longer varied play session | Scene transitions, dialogue, voice, music, inventory, quests and repeated fights; note reproducible first failure and last 128 diagnostic events, not just a single FPS number |
| G19 | Compare regular RESONANCE01 vs diag-lite ON | Same exact scene/saves and regular objects, separate manifests, check profiler overhead and report whether p95 changes; compare against same perf06 regular control |

**Rare-fusion negative case for G10–G12 (only with safe, disposable test saves and a compiled fusion-exp path):** distinguish non-accident, near-level alternative `0x36/0x4F`, and alternate-by-level `0x37/0x50` falling back to `0x36/0x4F`; observe candidate count once per state transition, ensure no infinite search, zero-count modulo, duplicate RNG roll, wrong substitute, unwanted skill transfer or lost confirmation state. Pinned source may never allow <=5 eligible candidates in ordinary play; mark NOT RUN rather than fabricate this condition or damage original saves. [QA06 source audit](FUSION_ACCIDENT_NEARLEVEL_NONTERMINATION_2026-11-20_QA06_WIP.md).

**Diagnostic-storage negative case:** existing frozen `port/main.c::log_drain` advances its ring tail after a `void log_commit` even if file open/short write failed; existing `p4.log` is best-effort. A new Flight Recorder incident must stay SEALED until a full write/rename/sync/verify/ACK and report export failures. If the on-device log is absent, mark no evidence instead of treating silence as a clean scene. Test export/home/low-storage in separate safe cases without risking save files. See [QA06 source negative-contracts](BASE_LOG_SAVE_BUILD_NEGATIVE_CONTRACTS_2026-11-20_QA06_WIP.md).

## If a failure occurs

Record device/firmware/storage (ef0:/ or ms0:/), build name/profile/manifest SHA256, exact scene ID and save **hash only**, game date/time, menu action sequence, first visible symptom, repeatability (x of y), log timestamp, last accepted displayed frame, GE list bank highwater/watchdog reason, any RTT/IO/fusion event and p4.log + Flight Recorder binary incident if created. Copy logs privately only after exiting safely; no publicly shared save contents/ISO, and don't fabricate stack traces. On a severe hang or suspected save corruption, restore immutable perf06 and backup save, mark failed feature OFF and do not keep writing into the same save.

## Sign-off meaning

- HOST: native C/Node tests only, not game tested.
- T2: PPSSPP test with actual artifact, input/save/scene and log.
- T3: PSP Go physical observation. Needed before any claim that performance, stability, manual inheritance or visuals are **validated on PSP Go**.
- PASS and FAIL are per scene and per artifact SHA. **NOT RUN is not PASS**. A normal RESONANCE01 build with a disabled experimental fusion feature may still qualify as testable, but must never be advertised as having working selectable fusion inheritance.

Use [Saturday Codex masterprompt](RESONANCE01_CODEX_MASTERPROMPT_2026-11-21_READY_DRAFT.md) and [fusion audit](FUSION_RESONANCE01_MANUAL_INHERITANCE_IMPLEMENTATION_REAUDIT_2026-11-20_WIP.md) for engineering owners and test obligations.


## QA06 2026-11-20 added fusion callback controls (not yet executed)

Specific trace expectation from [pinned original fusion YES/rare audit](FUSION_FINAL_APPROVAL_GATE_2026-11-20_QA06_WIP.md): mode2 request 0x2D -> reject/eligibility0x2F -> YES/no at dialog0x30 -> rare0x34; mode3 0x46 ->0x48 ->0x49 ->0x4D; multi 0x57 ->0x59 ->0x5A ->0x5C. Mark each **YES, NO, rejected, accident/non-accident** subcase separately as PASS/FAIL/NOT RUN if feature enabled; verify no modal on NO or rejected fusion and that modal doesn't perturb rare decision. A visible manual selector before original affirmative is a failure even if its chosen skills seem valid. These subcases supplement G10–G12 and do not change 20-case count. Keep selector experimental until native acceptance.
