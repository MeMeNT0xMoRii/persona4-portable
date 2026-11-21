# RESONANCE01 Vanilla UI correction matrix

The pair IDs and family assignments come from the existing [QA09 inventory](RESONANCE01_GOLDEN_120_PAIR_COVERAGE_VS_UI_FIX_READINESS_2026-11-20_QA09.md). The [anchor reference](UI_RESONANCE01_ANCHOR_TRANSFORM_MATRIX_2026-11-20_WIP.md) records the original 640×448 source geometry and the PSP projection. Numeric entries below distinguish a source anchor from a measured pixel target. READY applies to an isolated test, within its stated coverage.

| Pair IDs / family | Owner and evidence | Vanilla geometry, next bounded change and checkpoint | Status |
| --- | --- | --- | --- |
| 001–002 Title | Different assets | Preserve Vanilla title art. Only test native selection safe area if clipped. Title boot. | VANILLA_DIFFERENCE |
| 003–007 Save/Load | Owner and sprite region open | Match slot content and confirm phase before moving rows or scrollbar. Cold load of clone. | NEEDS_MEASUREMENT |
| 008 TV Entrance | Pair includes content differences | Compare common panel edge independently. Entrance Hall clone. | VANILLA_DIFFERENCE |
| 009–010 Velvet menu | Vanilla and Golden support different fusion menus | Keep Vanilla choices. Measure only corresponding choice panels. Velvet clone. | VANILLA_DIFFERENCE |
| 011–018 Fusion panels | Selection/result/detail paths differ | Check each panel and card plane in the same phase. Separate from inheritance and art. | NEEDS_MEASUREMENT |
| 019–022 Compendium | Compendium UI and register fault are distinct | Same datum and portrait before list width or positioning. Separate recovery test. | NEEDS_MEASUREMENT |
| 023 Margaret portrait | Dialogue portrait family | Identify texture, UV and tint separately from label. Match normal dialogue. | NEEDS_MEASUREMENT |
| 024–030 Camp Skill/Item | `cmpSkillMenuDraw` confirmed | Skill nominal X 193.5 icon/225 text, pitch 20.64 PSP px before animation. Measure icon vs text vs selected row and thumb separately. Camp → Skill. Risk: scroll and long text. | NEEDS_MEASUREMENT |
| 031–034 Equip | `cmpEquipMenuDraw` confirmed | Match party member, equipped item, prompt and stats before deciding row or tooltip pivots. Camp → Equip. | NEEDS_MEASUREMENT |
| 035–037 Camp Persona | `cmpPersonaMenuDraw` confirmed | Keep 3D pivot separate from list origin. Match same Persona and skill record. Camp → Persona. | NEEDS_MEASUREMENT |
| 038–042 Status/Social Stats | Radar trace and exact element targets incomplete | Verify radius of five circles and right margin. Match rank and animation. | NEEDS_MEASUREMENT |
| 043–045 Social Link | `datCommunityPortraitDraw` confirmed | Quad half extents 50×64 source units before scale; origin and motion affect final position. Test same asset and masked-grid state before local portrait FIT. | NEEDS_MEASUREMENT |
| 046–049 System/Config | `cmpConfigDraw` confirmed | Vanilla seven rows, projected first anchor (164.25,72.25), pitch 19.43 px before motion. Preserve options and bottom confirm. | VANILLA_DIFFERENCE |
| 050–052 Teddie dialog | Portrait/choice owner and pivot separate | Compare matched portrait/dialogue rectangles at Entrance Hall. | NEEDS_MEASUREMENT |
| 053–068 Daidara | Children of `fclShopDraw` | Same purchase state before ornament, text or panel adjustment. Check ring pivot, scene animation and clipping. | NEEDS_MEASUREMENT |
| 069–072 Shiroku | `fclShopItemPanelUpdate` and layout | Same menu and phase first; logo and chain ring need independent pivots. | NEEDS_MEASUREMENT |
| 073–076 Castle/map | Minimap owner unverified | Match floor and map bounds. Hold color correction on #074 pending Vanilla comparison. | NEEDS_MEASUREMENT |
| 077–083 Battle wheel | `btlSelectionDrawCommands`, three confirmed fans | Keep ring (64,203) nominal and 15-source-unit text pitch. UI-WHEEL-02 translates ordinary rows, selected glyph and settled strip by −13 source Y. Verify first-turn Izanami checklist. Independent target/Analysis paths excluded from offset. | READY |
| 084 Results | `btlResultSimpleDraw` candidate | Fix only after portrait/silhouette and reward-frame correspondence. | NEEDS_MEASUREMENT |
| 085 VFX | Frames are unmatched | Require exact spell and animation phase before diagnosing alpha/depth. | BLOCKED |
| 086–087 Rise Navigator | Portrait/choices owner incomplete | Match dialogue mask and prompt state. No reuse of roster pivot. | NEEDS_MEASUREMENT |
| 088–092 Yomotsu map/HUD | Camera and lighting differed | Match minimap area, choices and scene state separately. Slot 16. | NEEDS_MEASUREMENT |
| 093–096 Home/calendar | Date and weather owners established, transition unresolved | Match day before overlay or calendar adjustment. | NEEDS_MEASUREMENT |
| 097 Town map | Existing marker and circle confirmed | Preserve working map. Use as a comparison control. | VANILLA_DIFFERENCE |
| 098–105 Field date/weather | `calendarDrawBackdrop`, existing phase-label horizontal correction | Keep distinct right anchor, date, phase and weather. X origin 414 px before animated layout. Use School/Junes. Avoid applying the existing scale twice. | NEEDS_MEASUREMENT |
| 106 TV weather | `msgCalendarDraw` | Preserve six Vanilla forecast cards. Record1 title identity still open. | VANILLA_DIFFERENCE |
| 107–109 Rise dialogue | Different outfits/states | Match original asset and portrait/choice phase first. | VANILLA_DIFFERENCE |
| 110–113 Battle follow-up/Tactics | Independent `List` heading candidate, All-Out owner partial | Verify exact List sprite slot before moving a header. #113 wheel control. Target phase and 2/3/4 party variants separate. | NEEDS_MEASUREMENT |
| 114–115 Shuffle | `btlShuffleDrawTaskUpdate`, differing mechanics | Preserve original cards, phases and mechanics. Verify only original panel bounds. | VANILLA_DIFFERENCE |
| 116–117 Eye cut-in/Nav | `btlCutinLoad` family, exact matched asset open | FIT actor eyes and validate streak effects separately. | NEEDS_MEASUREMENT |
| 118–120 All-Out/Evening | All-Out family traced, Evening owner open | Match party size, faces and phase. Evening shape is a separate family. | NEEDS_MEASUREMENT |

First Camp batch shortlist: Skill baseline and scrollbar containment, Equip selected row and tooltip, Persona model-versus-panel spacing. Each is blocked on matched artwork/content and a stable ROI that establishes a visible Vanilla defect. Group only validated local edits. Keep Config option structure and six-card weather out of that batch.
