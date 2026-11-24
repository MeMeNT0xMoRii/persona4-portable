# VELOCITY01 UI ALIGNMENT 03 · Korrekturmatrix und QA

Basis UI02 Runtime 043724e7725fdf023e1dac67010669fceadcc69b. Die 15 PSP-Go-Bilder U001–U015 und die kuratierte Matrix sind die Bezugsbasis. G bezeichnet die normalisierte Golden-Referenz, A eine ältere UI01-Aufnahme und P den älteren perf06-Vergleich. Pixelziele beziehen sich auf 480×272. Die beiden gelieferten Matrixkopien sind bytegleich. Das technische Ergebnis unten enthält keine privaten Referenzbilder oder Rohlogs.

## Projektion und Rückwege

Die gewöhnliche Im2D-Projektion bildet 640×448 mit .75 und .607143 auf den PSP-Screen ab. Unkompensierte Quellgeometrie erhält so relativ zur Y-Achse 21/17 X-Breite. Das erklärt einen Teil ovaler Kreise, flacher Titel und breit wirkender Sprites. Andere Pfade besitzen eigene Korrekturen. Font-Position und Rastergröße entstehen getrennt aus Glyphenmetriken, Camp-Skill/-Item haben bereits einen Y-Fit, Radialfans eigene X/Y-Faktoren, und City-/Dungeon-Primitiven teilweise eigene Queue-Daten. Die globale Projektion bleibt. UI03 verwendet gemeinsame Gruppen-Transforms nach den bereits vorhandenen UI01-/UI02-Transforms, mit Zustandswechseln nur an den bestätigten Ownern. Beim Result-Portrait betrifft 17/21 X ausschließlich das unkompensierte Sprite am rechten Anker. UV, Canvas und Masken bleiben im bisherigen Pfad.

UI02 reproduziert die UI02-Zeichenregeln. UI01 schaltet UI02 und UI03 ab, UI_LEGACY schaltet alle drei Layout-Batches ab. Alle Marker werden beim Kaltstart neben der EBOOT gelesen. Die Build-ID bleibt auch mit älteren Layouts UI03. UI_NAV_TRACE bleibt als begrenzte Diagnose verfügbar. Static Flat ist Standard, alle anderen VELOCITY01-Performanceexperimente bleiben optional.

## Kampf

| IDs · Referenzen | Änderung · Prüfstand |
| --- | --- |
| B01 · U001/002, G077/110 | Der Roster-Callback wird hinter dem Frame und vor dem Target-Callback an dieselbe Ordering-Table angehängt. Die Roster-Presets und der Portrait-/COMMAND-Zeichenpfad bleiben lokal erhalten. Ziel sind Vordergrundringe und COMMAND in allen Roster-Zuständen. |
| B02 · U001, G077 | Gemeinsame Bottom-Bar-Hilfssprites und die aktive Befehlsbeschreibung rücken +5,5 PSP-Pixel. Die rote Platte bleibt auf UI02. Der Win-Textpfad und alle übrigen Hint-Einträge bleiben separat. |
| B03/B07 · U001/002, G081/082/110 | Die vier Rush-Hint-Sprites verwenden Haupt-/Untermenü-Anker im selben Layout. Reale Skill/Item/Titel/Swap-Callbacks angepasst. Show Info und Status bleiben getrennte Hints, ihre exakten Golden-Anker sind offen. |
| B04 · U002, G078/080–082/110 | Gemeinsames Label-Overlay der sechs radialen Kommandos erhält −6 Y und den lokalen X-Fit 0,93. Der Tactics-Anker wandert rechnerisch von ca. X60/Y183 auf X58/Y177. Ausgewählte Einträge des separat aufgerufenen Untermenüpfads verwenden denselben Label-Fit. |
| B05 · U001, G077 | Die Punkte stammen aus dem ausgewählten ATTACK-Glyphenrecord. UI03 zeichnet den regulären ATTACK-Record mit dem aktiven Tint am gemeinsamen Zeilenanker. Targetmarker verwenden andere Rendererpfade. Letzte Glyphenänderung visuell auf Go offen. |
| B06/B10 · U001/002, G077/078/080–082 | Die sichtbare Strip-Quad darf über das Rad hinausragen. Horizontale Länge aus 2 × 83 Quellpixeln, rund 124,5 PSP-Pixel vor Rotation. Vor der geerbten diagonalen Maskengeometrie wird der Tiefentest wiederhergestellt. Kreis und 12-Pixel-Raster behalten ihre UI02-Regeln. |
| B08 · U003, G083 | Rush Stop und animierte Pfeile sind offen. Der aktive Rush-Overlay-Pfad ist separat vom identifizierten Start-Hint, dessen Anker wurde nicht auf den Stop-Zustand übertragen. |
| B09 · A065, G084 | Eigenes X-Aspect-Fit auf das Result-Portrait-Sprite um X472. Bildinhalt, Hintergrund und UV unverändert. Unterschiedliche Referenzausschnitte und der aussagekräftige Übergangsframe bleiben für Hardware-QA. |
| B11 · U003, G083/113 | UI02-Navigator und Gegneranalyse sind beibehalten. Für den Vergleich fehlt die gleiche Portraitphase samt Text. Der sporadische Dungeon-Fehler bleibt offen. |

## Camp, Dungeon und Dialoge

| IDs · Referenzen | Änderung · Prüfstand |
| --- | --- |
| C01 · U004/005, G024 | Gemeinsamer Camp-Menü-Zeichenpfad mit nominell 20,25 px Pitch. Separate Behandlung von ausgewählten Glyphen, Zeilenoffset und Titeltransition. Vanilla-Einträge und Menülogik bleiben. |
| C02/C09 · U006/007, G025/028 | Rechte Skill-/Item-Listenraster bleiben UI02. Ein separater Text-/Icon-Fit braucht verifizierte Glyphenmessung. Das Persona-Menü bleibt UI02. |
| C03 · U008, G031 | Drei unselektierte Equip-Kategorien verwenden die untere Gruppe (Y116–192 → Y99–191, X223 → X210). Symbole, Labels und Items verwenden dieselbe Abbildung. Die ausgewählte Kategoriephase bleibt UI02. |
| C04/C05 · U009/010, G038/041 | Status-Charakterwahl X288–459 → 278–447, Top Y15 → 17, mit leichter vertikaler Verdichtung. Detail-Infoblock Y14–98 → 23–116 und Detailtitel −22 X/−20 Y. Portrait-Crop und Golden-exklusive Infogruppen bleiben offen. |
| C06 · U011, G044 | Kartenabschnitt inkl. gebundener Rank-Elemente X13–95/Y12–105 → X14–96/Y5–130. Der separate Porträt- und Titelpfad sowie die untere Nameplate behalten UI02. |
| C07 · U012, G046 | System-Panel, Labels und Auswahlgruppe verwenden die gemeinsame native Abbildung X110–480/Y77–189 → X102–480/Y68–225. Der Fullscreen-Cursor-Maskenpfad bleibt auf seinem alten Scope. Config-Einzelzeilen bleiben offen. |
| C08 · U006/007/008–012 | Gemeinsamer Camp-Footer um den rechten Anker X470 korrigiert, 7 px tiefer am Ausgangsanker. Tasten, Winkelanimationen und Varianten sind die vorhandenen. |
| D01 · U004/005/012, G074 | cmpPartyPanelDraw, unabhängig vom Battle-Roster. Porträt und HP/SP-Körper aus X428–468/Y93–246 nach X440–478/Y69–243. Ränder bei mehreren Partygrößen prüfen. |
| D02 · G074/075/117 | UI02-Minimap, Marker, Floor-Label und Alpha-Clipping bleiben. Neue isolierte Geräteaufnahme offen. |
| F01 · U013, G097 | Stadt-Labelplatten und Ortssprites gemeinsam etwa X113 → X95, −10 Y und X-Fit .68. 5 Orte und Navigationslogik bleiben. Kreisclip und absolute Kartengeometrie bleiben offen. |
| S01/S02 · U014, G068/069/071/072 | Daidara und Shiroku behalten ihre bestehenden Portrait-/Shopressourcen. Keine sichere gemeinsame Zuordnung für einen Portrait-Fit oder Shop-Preislisten. Diese Familien brauchen eine eigene Reproduktion. |
| F02 · U015, G101 | Standard-Choice-Familie mit separater dunkler Panelgeometrie und Y-Layout für Text und Auswahl. Weißer Balken behält seine Breite. Andere Choice-Primitiven bleiben separat. |
| F03/F04 · U014/015, G068/097/101 | Orange Rückplatte separat mit späterem Startanker, ca. +10 Y im Quellpfad vor Drehung, und Namensanker mitgezogen. Die graue UI02-Vorderbox bleibt. Golden-exklusive gelbe Rückplatte floss nicht in Zielwerte ein. Datum bleibt UI02. |

Velvet-/Fusion-Detailpanels, Persona-Details, Shop-Preislisten, Config und separate Level-up-Layer bleiben bei der Basis. Für diese Pfade sind zeitgleich vergleichbare eigene Layoutzustände und sichere Owner erforderlich. Die bestehenden Fusion-Card-/Skill-Inheritance- und Save/HOME-Pfade sind geerbt.

## A01 · BGM

Die originale UI02-Datei enthält 35.245 Textzeilen. Audio-Status in Zeile 454 bei 705 Blöcken 0 Fehler/0 Underruns, in 2316 bei 6521 ebenfalls 0/0, in 21560 bei 161696 kumuliert 8/1. Danach folgt der regulär protokollierte Video-Suspend und Worker-Neustart. Die Werte ordnen das verstummte BGM keinem Cue zu. Es liegt kein belegter zeitlicher Zusammenhang zum Video vor.

Die elf erreichbaren Battle-Cues aus Retail-Trackwahl und der separaten Aktion wurden im privaten Paket aufgelöst. Alle haben gültige Stereo-P4A1-Header, 24 kHz, konsistente Blockzahlen und gültige Looppunkte. Die ersten physischen Streamenden liegen je nach Cue bei ungefähr 22 bis 468 Sekunden. Der Loader setzt am physischen Ende per Seek auf den Loopblock zurück. Der Mixer kann beim bestätigten Underrun unabhängig von den SFX-Voices nur den BGM-Stream schließen. Ein beabsichtigtes Stop-Kommando, Lade-/Seek-Fehler, Ringfüllstand oder SAS-Voice-Ende bleiben anhand des UI02-Logs nicht unterscheidbar.

BGM_TRACE ist separat und standardmäßig aus. Kanal 0 protokolliert Cue-/Archiv-ID, Load, Header, Start, Loop, EOF, Stop/Fade und Fehler. Die nativen I/O- und SAS-Fehlercodes werden am Aufruf erhalten. Etwa alle 12 Sekunden kommt bei laufender Musik ein Sample mit Quellblock, produzierten und konsumierten Blöcken, Queue-Stand und SAS-End-/Pauseflags hinzu. Ereignisse, Cue-Anfragen und Samples haben getrennte Budgets. Es gibt keinen Eingriff in Trackwahl, Wiederanlauf, Mischung oder Stop-Semantik. Für die Hardware-Reproduktion im gleichen Kampf das Menü ohne Eingabe über die Ausfallzeit geöffnet lassen, beim Ausfall einen SFX auslösen und die verstrichene Zeit sowie das Log sichern.

## Build und Prüfstand

Runtime ce3663f76b65c0c6b0d3e52cf7e07dfc0309931b. EBOOT SHA-256 d4a474a566666121ba1a0845a256ec9f4fa5eab068d429673521dfaa163dddcc. Das private Paket enthält 80 vollständig nachgehashte Dateien und unveränderte Teststand-Klone 9, 10 und 16. Native PSP-GCC-Kompilation und Link bestanden. 22 von 821 Spielobjekten neu gebaut, 19 Importbibliotheken gültig, keine unaufgelösten Funktionszeiger. Striktes QA07, Geometrie-/UI02-Fallback-Hosttest und der Battle-Fan-Hosttest bestanden. Die drei Audio-Hosttests für Paketformat und VAG liefen mit dem vorhandenen lokalen FFmpeg.

PPSSPP 1.20.4 am festen Pfad, Software 1×. In der ersten UI03-Fassung mit den finalen Layerregeln erreichte Slot 16 das steuerbare Izanami-Rad und die Skill-Liste. Beide aktiven Draw-Owner wurden im laufenden PRX bestätigt. Die Aufnahmen zeigen die Roster-Ringe und COMMAND über der Kontur, den auskragenden Strip und die offene Skill-Liste. Der Rush-Anker der Skill-Aufnahme lag gegenüber dem Rad rund sieben Pixel rechts. Daraufhin wurden Rush-X und die ATTACK-Glyphenwahl korrigiert sowie der tatsächliche Befehlsbeschreibungspfad tiefer gesetzt. Diese letzten Battle-Details bleiben visuell auf Go zu prüfen.

Die Camp-Aufnahmen zeigen den breiteren Zeilenabstand sowie die Skill- und Equip-Komposition. Die finale EBOOT bestätigte Schule, FPS-Anzeige, Camp-Raster und die wieder sichtbare Auswahlbeschreibung am unteren Anker. Der UI02-Marker reproduzierte im separaten Emulatorvergleich den UI02-Camp-Ausschnitt pixelgleich. Im Battle-Smoke protokollierte BGM_TRACE mehrere erfolgreiche Loop-Seeks für Cue 0x29, danach die reguläre Track-Umschaltung und laufenden Battle-Cue ohne Underrun im beobachteten Abschnitt. Im Emulator wurde der gemeldete Ausfall im kurzen Lauf nicht reproduziert.

## Hardware-QA

Slot 9 für Camp, Equip und UI02-Rückweg. Slot 10 für Dungeon-Party, beide Minimap-Modi, Viereck-Navigator und die geerbten Fusion-Fixes. Slot 16 führt zum späten Izanami-Kampf mit Spoilern. Rad/COMMAND, Skill, Tactics und Rush sowie die Übergangsframes erfassen. Für die normale Layout-QA nur FPS. Bei Layoutregression dieselbe Szene nach Kaltstart mit zusätzlichem UI02 prüfen. Für Musikstillstand BGM_TRACE separat setzen, auf demselben Kommando im ursprünglichen Kampf warten und Zeit sowie Fehl- und Folgeaufruf sichern. Die PSP-Go-Abnahme des UI03-Builds steht aus.
