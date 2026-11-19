> **ARCHIVED / SUPERSEDED 2026-11-19 — DO NOT USE FOR ACTIVE UI WORK.** This document refers to an obsolete 71-image Golden-only collection. Current authoritative input is **120 matched Golden + 120 P4P-perf06** pairs, IDs 1–120, at [QA03 active hub](qa03/README.md) and [120-pair review](qa03/120_pairs_widescreen_WIP.md). All old bounding boxes, ID references, priority recommendations and missing-media claims are historical ONLY. Kept for traceability, not implementation. No code change.

# Persona4-PORTABLE v0.27 — Golden-UI-Geometrie-Audit (QA03, 19.11.2026)

**Geltung:** `MeMeNT0xMoRii/persona4-portable`, Dokumentationsbranch `dev/v0.27-perf06` (Runtime-Baseline `29b0fcf`, veröffentlichter Dokumentations-HEAD vor diesem Audit `ec9e8dd`). **Keine Runtimeänderung.**

**Entscheidung des Nutzers:** Persona 4 Golden auf Vita ist **Positions-/Proportionsreferenz** für Vanilla Persona 4 PS2 US auf PSP Go **480×272, 16:9**. Keine Golden-Assets, -Titelgrafiken, -Animationen, -Story-/Fusionsfeatures übernehmen. PS2-US bleibt Originaltreue-Referenz; ein tatsächlicher PSP-Go-Frame ist für die Abnahme nötig.

## A. Datenbasis, Sicherheit, Auswertungsgrenzen

- Privates Nutzerpaket: `P4P-Lab-Golden-UI-References-2026-11-18-v2`, **71 verifizierte 960×544-JPEGs**. Nummern `01..71` beziehen sich auf **private** `Screenshot_Index.csv` und `ByCategory/...`; Bilder, Vita-/PS2-/PSP-Fotos und Spielassets **nicht öffentlich auf GitHub hochladen**. Die fünf Contact Sheets wurden visuell geprüft.
- Fotos zeigen mehrere unterschiedliche Spieldaten, Felder, Personen und Menüs. Bildvergleich ist kein Beleg dafür, dass derselbe Vanilla-Darstellungsweg existiert.
- Screenshot-Koordinaten im vorliegenden separaten privaten Analyseblatt sind **manuell geschätzte Bounding Boxes**, nicht die tatsächlichen Im2D-Vertizes. Angegebene Genauigkeit etwa ±5–12 Golden-Pixel = ±3–6 PSP-Pixel. Keine automatische Pixelgleichheit behaupten.
- `x_target ≈ x_golden/2`, `y_target ≈ y_golden/2` nur zum **Ankervergleich**, denn 960×544 ist exakt 2×(480×272). Das ist kein globales Skalierungsgesetz für 4:3-PS2-Geometrie.
- Elementweiser Plan: `right = 480-(x+w)`, `bottom = 272-(y+h)` als Randabstände; Porträts und Embleme **uniform**, Textgröße und Panels unabhängig, Scissor-/UV-/Texturskalierung gesondert. **Keine** globalen Änderungen im gemeinsamen RenderWare-`Im2D`-Pfad.

### Einstufung

- **A:** Geeignet als **16:9-Layoutanker** (Position, Zone, Randabstand) für grundsätzlich in Vanilla vorhandene Elemente. **Auch A ist nicht hardwarebestätigt.** Inhalt, Farben und Assets aus PS2 behalten.
- **B:** Bedingt geeignet, ähnliche Funktion, aber andere Inszenierung, Skin, Listenstruktur oder Storylage. Nur nach phasengleichem PS2-/P4P-Frame und Renderer-Trace übernehmen.
- **C:** **Nicht als direkter Golden-Übernahmekandidat.** Andere Cut-ins, eigenes Golden-Design oder ungleiches System; gegen Vanilla-PS2 prüfen.

## B. Explizite P0-Zielzonen (PSP Go 480×272; vorläufige visuelle Ablesung)

| Referenz | Funktion | Vorläufige PSP-Zielbox | Regel | Vorbehalt |
|---|---|---|---|---|
| 45/46/47, 43/49/64/71, 57 | Datum/Tagesphase | x≈353–453, y≈9–35 | rechts oben, X/Y eigener Container | Label-Länge/Storyzustand abweichend |
| 46/57 | Wetterkreis/Statussymbol | x≈442–474, y≈37–66 | rechts oben getrennt von Datum | Golden-Wetterskin nicht importieren |
| 45/46/47/57 | Ortslabel | x≈9–190, y≈238–270 | links unten, Text nicht clippen | lange Ortsnamen dynamisch behandeln |
| 57 | Dungeon-Partyhud | x≈439–480, y≈70–243 | rechts am Rand, separate Charaktereinträge | Golden-Party-Skin nicht übernehmen |
| 57 | Dungeon-Minimap | x≈12–102, y≈156–246 | links unten, Seitenverhältnis erhalten | Karteninhalt/Skalierungsmodus getrennt |
| 25 | Rush/Stop | x≈23–128, y≈220–254 | links unten, **bereits perf06-Fix erhalten** | Vanilla-Grafik, Golden nur Ankerkontrolle |
| 43/48/64/66/70 | Dialogbox | x≈10–478, y≈201–270 | 16:9-Bodenbreite; Text-Innenabstände | Tutorial-/Kurzdialog kann abweichen |
| 43/48/64 | Dialogporträt | x≈304–440, y≈44–204 | rechts, **uniforme** Portraitgröße | Bild 43 Kashiwagi; genaue Crop pro Figur |
| 17 | Yosuke-Social-Link-Porträt | x≈298–433, y≈60–196 | rechts, keine Gesichtsdehnung | eigenes Social-Link-UI, nicht normaler Dialog |
| 49 | Choice-Menü | x≈14–379, y≈98–190 | links/mittig, von Portrait getrennt | Länge/Optionenanzahl berücksichtigen |
| 50 | Daidara Geldpanel | x≈339–480, y≈13–32 | oben rechts | Shop-spezifisch, Golden-Skin nicht kopieren |

**Verbindliche technische Korrektur zu früheren Analysen:** Niemals die obigen Werte als fertig kalibrierte PSP-Koordinaten oder als pixelgenaue Abnahme präsentieren. Codex muss pro Objekt die originale Geometrie (Vertices, Scissor, Sprite-Anker), deren Transformation und die geänderte Geometrie auf **gleichen Szenen** nachweisen.


### Vollständiger vorläufiger Satz aller 18 bisher abgelesenen Rechtecke

| Golden-ID | Element | Golden-Box (x1,y1→x2,y2) | PSP-Go-Box (x1,y1→x2,y2) | Positionstyp |
|---|---|---|---|---|
| 46 | Datum + Tagesphase (Exploration) | 705,18→906,70 | 352.5,9.0→453.0,35.0 | right_top |
| 46 | Wetter-/Statuskreis rechts oben | 883,74→948,131 | 441.5,37.0→474.0,65.5 | right_top |
| 46 | Ortsname unten links | 17,476→380,540 | 8.5,238.0→190.0,270.0 | left_bottom |
| 57 | Dungeon-HUD Datum/Tagesphase | 705,17→906,68 | 352.5,8.5→453.0,34.0 | right_top |
| 57 | Dungeon-Wetterkreis | 882,73→945,129 | 441.0,36.5→472.5,64.5 | right_top |
| 57 | Party-Porträts und Statusbalken | 878,140→959,486 | 439.0,70.0→479.5,243.0 | right_edge |
| 57 | Dungeon-Minimap | 23,312→204,491 | 11.5,156.0→102.0,245.5 | left_bottom |
| 57 | Dungeon-Ortsname unten links | 17,478→378,540 | 8.5,239.0→189.0,270.0 | left_bottom |
| 25 | RUSH/STOP unten links | 46,440→255,508 | 23.0,220.0→127.5,254.0 | left_bottom |
| 25 | Kampf-Party-Status rechts | 810,0→960,384 | 405.0,0.0→480.0,192.0 | right_edge |
| 43 | Dialogbox unten | 20,401→955,540 | 10.0,200.5→477.5,270.0 | bottom_full |
| 43 | Dialogporträt Kashiwagi | 608,87→880,408 | 304.0,43.5→440.0,204.0 | right_bottom |
| 43 | Dialogauswahl links | 27,171→708,344 | 13.5,85.5→354.0,172.0 | left_center |
| 49 | Dialogauswahl Yosuke | 27,196→758,379 | 13.5,98.0→379.0,189.5 | left_center |
| 17 | Social-Link-Porträt Yosuke | 595,120→865,391 | 297.5,60.0→432.5,195.5 | right_center |
| 17 | Social-Link-Infokarte unten rechts | 397,389→960,496 | 198.5,194.5→480.0,248.0 | right_bottom |
| 50 | Daidara Metalworks Hauptmenü / Talk sichtbar | 110,44→389,380 | 55.0,22.0→194.5,190.0 | left_center |
| 50 | Daidara Geldanzeige oben rechts | 678,26→960,64 | 339.0,13.0→480.0,32.0 | right_top |

Alle Werte sind **manuelle Bildablesungen** und vor dem Commit eines Renderer-Fixes mit Vertex-/Frame-Traces zu verifizieren.

## C. Vollständige Prüfung sämtlicher 71 Golden-Referenzen

| ID | Szene aus `Screenshot_Index.csv` | Eignung | Was wirklich nutzbar ist bzw. nicht kopiert wird |
|---|---|---|---|
| 01 | `Party_Menu_Skill_Selected` | **B** | Linkes Hauptmenü/Charakter-Status, Inhalt und Abstände PS2 prüfen |
| 02 | `Persona_Roster_Izanagi` | **B** | Persona-Liste links, 3D-Modell rechts; nicht die Golden-Menüskin |
| 03 | `Persona_Detail_Izanagi` | **B** | Persona-Detail: Stats/Skills links, proportionaler Persona-Render rechts |
| 04 | `Persona_Equipped_Notification` | **B** | Einblendung/Popup nur als Safe-Area-Vorlage |
| 05 | `Status_Protagonist` | **B** | Protagonisten-Status: Trennung Textwerte/Charakterbild |
| 06 | `Status_Yosuke` | **B** | Teammitglied-Status: gleicher Paneltyp, anderer Charakter |
| 07 | `Equipment_Slots` | **B** | Ausrüstungs-Slots: linken Listen- und rechten Ausrüstungskontext prüfen |
| 08 | `Equipment_Weapons_List` | **B** | Waffenliste: Spalten, lange Texte und Scrollbereich |
| 09 | `Equipment_Weapon_Details` | **B** | Detail-Tooltip: begrenzen, nicht skalieren |
| 10 | `Item_List` | **B** | Inventarliste: Name/Anzahl/Party-HUD, Textabstände |
| 11 | `Item_Description` | **B** | Beschreibungskasten über Itemliste, keine Textverzerrung |
| 12 | `Skill_List` | **B** | Skillliste/Party-HUD und Rechterand |
| 13 | `Skill_Selected` | **B** | Skill-Auswahl: Fokusmarker, kein Beschneiden |
| 14 | `Skill_Description` | **B** | Skill-Beschreibung: Tooltip positionieren |
| 15 | `Social_Link_List` | **B** | Social-Link-Liste: linke Liste/rechter Porträtbereich |
| 16 | `Social_Link_Investigation_Team` | **A** | Social-Link-Detail: Porträt rechts, Info links/unten, PS2-Design behalten |
| 17 | `Social_Link_Yosuke` | **A** | Yosuke-Social-Link: Porträt nicht horizontal strecken, rechter Anker |
| 18 | `System_Menu` | **B** | System-Menü: Panel- und Randzonen, keine Golden-Texte übernehmen |
| 19 | `Save_Slot_Selection` | **B** | Save-Slots: Liste/Datum/Information, PS2-Slotformat behalten |
| 20 | `Battle_Main_Commands` | **B** | Kampf: rechte Team-Leiste nur Randausrichtung, Vanilla-Battle-HUD bleibt |
| 21 | `Battle_Target_Selection` | **B** | Targeting-HUD: Randabstände; PS2-Zielmarkierung unverändert |
| 22 | `Battle_Skill_List` | **B** | Skillliste im Kampf: Position/Safe-Area, Vanilla-Design |
| 23 | `Battle_Persona_Switch` | **B** | Persona-Umschaltung im Kampf: Layoutklasse nur bedingt vergleichbar |
| 24 | `Battle_Enemy_Analysis` | **B** | Enemy-Analyse: Panelflächen und 16:9-Ausnutzung, Vanilla-Inhalt |
| 25 | `Battle_Rush_Indicator` | **A** | Rush/Stop unten links: nur Kontrollanker, bereits gefixte perf05/perf06-Verankerung erhalten |
| 26 | `Battle_Follow_Up_Prompt` | **B** | Follow-up-Ja/Nein: Dialogposition und Lesbarkeit, PS2-Inszenierung |
| 27 | `Battle_All_Out_Attack` | **C** | All-Out-Cut-in ist Golden-spezifisch; PS2-US bleibt einzige exakte visuelle Referenz |
| 28 | `Battle_Shuffle_Time` | **C** | Shuffle-Time-Inszenierung abweichend: nicht Golden-Layouts übernehmen |
| 29 | `Battle_Results` | **B** | Kampfergebnis: Content-Panels als Randzonenprüfung |
| 30 | `Battle_Weak_Dizzy` | **C** | Weak/Dizzy-Effekt: andere Inszenierung, keine Positionsübernahme |
| 31 | `Battle_One_More_Overlay` | **C** | One-More-Overlay: andere Animation, PS2 als Referenz |
| 32 | `Velvet_Room_Main_Menu` | **B** | Velvet-Menü: Panel vs. 3D, kein Blau-/Goldenskin übernehmen |
| 33 | `Fusion_Type_Selection` | **B** | Fusionsarten: Golden enthält zusätzliche Optionen, Vanilla-Optionen unverändert |
| 34 | `Fusion_Ingredient_List` | **B** | Fusionszutaten-Liste/Scroll/Komponente, PS2-Modus prüfen |
| 35 | `Fusion_Ingredient_Details` | **B** | Zutaten-Details, Wertebalken und Optionen |
| 36 | `Fusion_Result_Preview` | **B** | Fusionsergebnis-Vorschau: Golden-Skill-Selektion keine Vanilla-Funktion |
| 37 | `Fusion_New_Persona_Dialogue` | **B** | Fusionsergebnis-Dialogtext unten, Persona-Modell separat |
| 38 | `Fusion_Result_Status` | **B** | Fusionergebnis-Status: Panel, Modell, Skillblock, kein Golden-Featureimport |
| 39 | `Fusion_Forecast` | **B** | Fusion-Forecast: Dialogbildschirm/Panel, bei Vanilla vorhandenen Daten bleiben |
| 40 | `Persona_Compendium_List` | **B** | Compendium-Liste: Liste/Geld/Status als Layout, Crash separat behandeln |
| 41 | `Weather_News_Week_Forecast` | **B** | TV-Wetterbericht: Kachelgrenzen, Clip und Reader; nicht 1:1 wie Feld-Wetter |
| 42 | `Weather_Calendar` | **B** | Kalenderwetter: separater Screen und UI-Modus |
| 43 | `Dialogue_Ms_Kashiwagi_Portrait` | **A** | Textbox unten fast bildschirmbreit + Porträt rechts; gemessene Zielboxen |
| 44 | `Town_Map_Location_Selection` | **B** | Stadtplan: Anker/Listen/Links-rechts-Aufteilung, Karteninhalte nicht Golden übernehmen |
| 45 | `Shopping_District_South` | **A** | Shopping District South: Feld-HUD Datum/Wetter rechts oben, Ortsname links unten |
| 46 | `Junes_West_Entrance` | **A** | Junes West Entrance (Lift/Einkaufswagen): P0-Feld-HUD/Ortslabel, Performance-Referenz |
| 47 | `School_Classroom_Building_2F` | **A** | Schulkorridor: gleicher Feld-HUD-Typ bei anderem Feld |
| 48 | `Dialogue_Daisuke_Portrait` | **A** | Daisuke-Normaldialog: Porträt rechts und untere Textbox |
| 49 | `Dialogue_Yosuke_Choice` | **A** | Yosuke-Choice: links Auswahl, unten Text, rechts Feld-Datum |
| 50 | `Metalworks_Main_Menu` | **B** | Daidara/Metalworks Hauptmenü: Talk sichtbar, aber Talk geöffnet NICHT vorhanden |
| 51 | `Metalworks_Character_Selection` | **B** | Metalworks Charakterwahl: Auswahl-/Ausrüstungszonen |
| 52 | `Metalworks_Buy_Weapons` | **B** | Metalworks Shop-Liste: Preis-/Geldanzeige und Scrolling |
| 53 | `Metalworks_Weapon_Details` | **B** | Metalworks Waffen-Detail: Tooltip, lange Texte, Statwerte |
| 54 | `Metalworks_Sell_Materials` | **B** | Metalworks Materialsale: Tabellen-/Zeilenbreite, nicht UI-Skin |
| 55 | `Metalworks_Sell_Material_Details` | **B** | Metalworks Materialsale-Detail: Tooltip/Anzahl |
| 56 | `Quest_List` | **B** | Questliste: Raster und Textposition; Golden-spezifische Quests nicht kopieren |
| 57 | `Yukiko_Castle_Exploration_HUD` | **A** | Castle 1F: Datum/Wetter rechts oben, Ortsname links unten, Minimap und Party rechts |
| 58 | `Dungeon_Map_Overlay` | **B** | Dungeon-Map: Vergrößerung/Maskierung der Karte, andere Funktion als Exploration-HUD |
| 59 | `Dungeon_Obtained_Item_Message` | **B** | Item erhalten: Textmeldung unten; nicht zusammen mit Map-/Navigator verändern |
| 60 | `Dungeon_Tactics_And_Rise_Portrait` | **C** | Golden-Rise-Navigator ist keine direkte Teddie-Vorlage; Teddie-Bug PS2-only |
| 61 | `Config_Options` | **B** | Config: Spalten/Zeilen/Safe-Area, PS2-Menü und Settings bewahren |
| 62 | `Title_Main_Menu` | **C** | Golden-Titelmenü unterscheidet sich: keine Skin/Layout-Übernahme ohne PS2-Beleg |
| 63 | `Title_Idle_Prompt` | **C** | Golden-Idle-Titelgrafik nicht für Vanilla-Logo verwenden |
| 64 | `Yukiko_Group_Dialogue_Choice` | **A** | Yukiko-Gruppendialog: Porträt rechts, Auswahl links, Text unten |
| 65 | `Bookstore_Books_List` | **B** | Buchhandlung: Shopliste/Geld/Info und Tooltips, PS2-Abgleich nötig |
| 66 | `Shiroku_Store_Owner_Greeting` | **A** | Shiroku-Begrüßung: generische Dialogbox unten + Portraitbereich |
| 67 | `Shiroku_Store_Main_Menu` | **B** | Shiroku-Menü: vertikale Optionen/Geld/Safe-Area, Ladenoptik nicht übernehmen |
| 68 | `Shiroku_Store_Buy_Medicine_List` | **B** | Shiroku-Kaufmenü: Listen-/Preis-/Geld-Panel, textuelle Lesbarkeit |
| 69 | `Shiroku_Store_Medicine_Tooltip` | **B** | Shiroku-Tooltip: Info-Panel ohne Clip am Bildschirmrand |
| 70 | `Shiroku_Store_Owner_Dialogue` | **A** | Shiroku-Gesprächsbox unten: Vergleich für allgemeine Shop-Talk-Geometrie |
| 71 | `Street_Food_Stall_Selection` | **A** | Straßenimbiss: Auswahl oben/links und Text unten, rechtoberes Feld-HUD |

### Zuordnung der 71 Screenshots nach Eignung

**A: 14**, **B: 50**, **C: 7**. Dies ist eine dokumentierte **Quellen-Eignungseinschätzung**, keine Anzahl nachgewiesener Fixes.

## D. Verbindliche Umsetzungs- und Schutzregeln für Codex

1. **perf06-Hardware-Abnahme vor Merge**: FPS in Junes West Entrance (`F009_004`), Riverbed (`F010_002`), Schule, Shopping District, Castle; Referenz perf04. perf05-Regression nicht reproduzieren. Kein großer UI-Merge in die perf06-Referenz.
2. Für UI separaten experimentellen Arbeitsbranch nach Sicherung der Runtime-Baseline. Pro Elementfamilie **Feature-Flag + A/B**, keine globale 16:9-Streckung. Vorher/vorher-nachher-Bilder in PPSSPP/1× gleicher Szene und danach echte PSP Go.
3. **P0 Feld-HUD**: Datum/Tagesphase und Wetterkugel rechts oben getrennt; Ortslabel links unten. Erst Vergleich mit Original-PS2/HUD und P4P, dann minimaler Fix. Prüfen in Junes West Entrance, School Corridor, Shopping South, Castle. Der bereits reparierte TV-Forecast ist nicht dasselbe UI-Objekt wie das Feldwetter.
4. **P0 Portraits/Dialoge**: Social-Link, normale Dialoge und Shop-Dialoge unterscheiden. X/Y, Scale, Crop, Textbox, Choice, Tooltip separat. Gesicht auf Originalproportion zurück, 16:9-Randnutzung.
5. **P1 Dungeon**: Party-HUD, Minimap, Ortslabel und Statusindikator getrennt. Golden Rise 60 **nicht** als Vorlage für originalen Teddie anwenden; grüner Teddie-Block ist separater Texture-/CLUT-/Raster-Bug.
6. **P1 Kampf**: Rush/Stop ist in perf05/hypothetisch perf06 behoben, nicht überschreiben; Golden-Battle-Cut-ins/All-Out nicht importieren. All-Out 2/4 Person PS2-US als Soll, 3 Personen Kontrollfall; Draw-Order, UV/Scissor und `btlCutin` untersuchen.
7. **P1 Menüs**: Persona/Status/Equip/Skill/Item/Compendium nur Layoutbereiche, kein Design- oder Golden-Gameplay-Import. Compendium-Register-Crash ist **separat** und darf nicht mit Golden-Compendium verwechselt werden.
8. **P2 Shops**: Metalworks 50–55, Shiroku 66–70, Bookstore 65, Street Food 71. Geldtopright, Listen/Tooltip und Textbox-Ränder; **fehlende** Golden-Referenz ist ausdrücklich **Daidara Talk nach Auswahl** (Bild 50 zeigt nur Option).
9. **Keine Performance-Regression**: gleichbleibende PSP-Go-Route/Kamera, CPU ms, GE wait, Frame time, FPS. Keine erzwungene Texturfilter-/Mipmap-Rücknahme, die perf05-Regressionsursache wieder erzeugt. Kein Weglassen von Originaleffekten als Layoutoptimierung.
10. Quellen/Schutz: keine urheberrechtlich geschützten Bilddaten, privat archivierte Medien, Saves, ISO, EBOOT oder Game-BIN auf GitHub. Alle Screenshots lokal anhand privater `Screenshot_Index.csv` adressieren.

### Code-Einstiegspunkte (durch Repoquellen belegt, nicht als fertiger Fix)

- Unsere Quelle `port/rwsky.c`: `SCREEN_W=480`/`SCREEN_H=272`, zentraler `SkyIm2DRenderPrimitive/IndexedPrimitive`; daher globale 2D-Skalierung **gefährlich**.
- `port/rwsky.c`: `p4_forecast_weather` markiert `calDrawWeather` ausschließlich im speziellen Wetterkarten-Durchlauf (nicht dasselbe wie die Feld-HUD-Wetteranzeige).
- `tools/psp_link.py`: verifizierter Alias `fGpffff84a0` für Rush/Stop auf US-Adresse `0x7615a0`; **nicht überschreiben**.
- Die separat heruntergeladene, gepinnte PS2-Decompilation hat einschlägige Erzeugerpfade etwa `src/calendar.c`, `src/datCalendar.c`, `src/Camp/cmpPersona.c`, `src/Camp/cmpMain.c`, `src/Event/Fcl/y_fclTalk.c`, `src/Event/Fcl/y_fclShopDraw.c`, `src/Battle/btlPanel.c`, `src/Battle/btlCutin.c`. **Konkrete Besitzerfunktion und exakte Vertex-Erzeugung sind pro UI-Klasse noch zu beweisen; Dateinamen sind lediglich Suchstart.**
- Die dokumentierte perf06-Recovery behält pref04-Mipmap-Verhalten. Die globale Mipmap-Regel nicht für UI-Schärfe verändern.

## E. Noch fehlende, tatsächlich wertvolle Referenzaufnahmen

**Sofort wichtig (P0):**

- **Vanilla PS2-US und P4P perf06**: gleiche Szene und möglichst gleicher Kamera-/HUD-Zustand (Junes West Entrance mit Aufzügen/Körben; Schulkorridor; Shopping South; Castle 1F); mindestens einmal Datum/Tagesphase/Wetter und Ortslabel sichtbar. Golden 45–47/57 ist vorhanden.
- **PS2-US vs. P4P** normaler Dialog und Social-Link möglichst mit **demselben** Charakter (z. B. Yosuke), damit gemessener Gesichts-Stretch vom Stilunterschied getrennt wird. Golden 17 + 49 vorhanden, aber andere UI-Modi.
- **Daidara Metalworks → Talk nach Anklicken**: Golden-Screenshot fehlt (50 zeigt nur den Menüpunkt). Kein neues Shop-Menü nötig.
- **Vanilla-All-Out** 2 und 4 Figuren in derselben Animationsphase plus P4P; Golden 27 **nicht** als Positionssoll. Drei Figuren als Kontrolle.

**Bug-fokussiert (P1):**

- **PS2-US-Teddie** im frühen Dungeon und P4P Teddie bei Erfolg sowie ggf. grünem Rechteck; Golden 60 Rise **nicht als Ersatz**.
- **Reinforcement-Hänger**: Basis-Save gegen modifizierten Save in gleicher Kampf-/Wellenroute, Übergangsvideo, Spielort, Spieltag, Build und Logs. Ein Nutzerfoto vom eingefrorenen Teppich existiert privat; **nicht öffentlich hochladen**.
- **Shopping North** spezifischer Blickwinkel des Gebäudeflackerns; **Dojima außen** orange Straßen-/Randfläche; **Samegawa Riverbed** als Perf-Regression-Test. Golden-Material ist hierfür keine zwingende Sollreferenz.

**Nicht notwendig:** ein weiterer Junes-„Department Store“-Shot (Nutzer meint West Entrance); Golden-All-Out 2/4 als Vanilla-Soll; Golden Rise als Teddie; weitere Golden-Shiroku-Grundmenüs.

## F. Freigabekriterien und offene Risiken

- Keine Funktion wird als **definitiv technisch passend** deklariert, bevor PS2-US-Vergleich und Code-Trace denselben UI-Objekttyp bestätigen. A heißt **geometrisch sehr brauchbare Golden-Vorlage**.
- Pro Item dokumentieren: Original-PS2-Bbox, Golden-Bbox, P4P-vorher-Bbox, neu errechnete P4P-Bbox, Clip-/UV-/Rasterzustände, Perfdelta, Screenshot und Gerätetest-Status.
- 16:9-Fill allein ist nicht das Ziel. Proportionsfehler beseitigen, PS2-Stil, Lesbarkeit, Original-Gameplay und stabile PSP-Go-FPS erhalten.
- Dieser Bericht **ändert keine Runtime-Dateien** und belegt **keinen** neuen funktionierenden UI-Fix.

**Für den Samstag-Start:** zuerst `docs/v027_qa03_handoff_2026-11-19.md` lesen, dann diesen Audit und `docs/v027_perf06.md`, `docs/v027_issues.md`. Die Golden-JPEGs sind lokal beim Nutzer, nicht im GitHub-Checkout.