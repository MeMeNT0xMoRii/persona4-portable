# VELOCITY01 UI ALIGNMENT 02 · Korrekturmatrix und QA

Runtime-Basis ist UI ALIGNMENT 01, 52c95f5b6d6cd5cd367909fdb70ac00de6045a1d. A bezeichnet die PSP-Go-Referenzen aus dem privaten UI02-Handoff, G die Golden- und P die perf06-Referenzen. Zusätzlich wurden die gemeinsamen Teile von P/G035, 045, 079 und 084 lokal geprüft. Ziele in nativen PSP-Pixeln. Animierte Sprite- und Maskenkanten sind Zielwerte aus dem Quellpfad, bis eine Aufnahme den betreffenden Zustand bestätigt.

## Bestätigte Korrekturen

| IDs und Referenzen | Umsetzung und Ziel | Stand |
| --- | --- | --- |
| B01 · A044/045, G077/113 | Gelbes Rad, drei Fans und Pivot aus UI01 erhalten. | Geerbter Hardware-PASS |
| B02 · A045, G077/113 | Acht Texte von rund 9,1 auf 12 px Pitch, Mittelpunkte 160 bis 244. Unselektierte und selektierte Glyphen mit X-Fit 0,94. Der schwarze Streifen nutzt denselben Y-Fit einschließlich Drehphase. | Implementiert und mathematisch geprüft |
| B03–B06 · A046–049, G081/082/110 | Listenraum X71–401 → 105–399. Skill/Persona Y80,5–169 → 81,5–177,5, Tactics separat → 89,5–169,5. Clip-Primitive, Cursor, Scrollbar und Inhalte folgen der Liste. Listentitel mit hellem Tint, eigenem X-Ausgleich und bestehender Alpha-Kurve. Item nutzt den gemeinsamen Listenraum. | Implementiert und mathematisch geprüft. Kontrast und Phasen visuell offen |
| B07/B08 · A044–049, G077/081/082/110/113 | TV-Kontur X27–407 → 52–404, Y14–243 → 11–247. Backdrop separat. Roster inkl. Porträts, HP/SP, Flash und Pulse X402–465 → 404–474, Y24–229 → 5–253. | Implementiert und mathematisch geprüft. 1–4 Slots und COMMAND-Überlagerung prüfen |
| B09 · A044–049, G077/110/113 | Rote Bar bei vollem Öffnen ungefähr Y254–272. Rush +38 X und −10 Y im Haupt-/Zielwahlrad. Gemeinsame 0x32/0x33-Hints entlang der Bar +8 Y. | Teilflächen umgesetzt. Abweichende Status-/Analyse-Hints offen |
| B10 · A044, G113 | Kompakter Navigator mit kleinerem Ring und Portraitfit, Textanfang bei ca. X85 statt 106 und schwarzer Box bis etwa X320. Ein- und Ausblendung am selben Pfad. | Implementiert. Lange Zeilen und seltenere Portraits visuell offen |
| D01/D05 · A037–043, G073–075/117 | Dungeon-Kreis und Party-Gruppe bleiben getrennt vom kompakten Battle-Navigator. | Offen. Vergleichbarer Dungeon-Auslöser und eindeutiger Party-Draw-Owner fehlen |
| D02/D03 · A038–041, G074/075 | Kleine Karte X15–112/Y162–244 → X13–102/Y157–246, große Karte X136–336 → X127–323. Eine Abbildung je Modus für Ecksprites/Checker, Kacheln und Symbolpositionen. Äußere Maskenkanten bleiben bildschirmdeckend. Tiefe, UV, Queue-Reihenfolge und Markerdrehung folgen der Basis. | Implementiert und Projektionen mathematisch geprüft. Clip und Übergang auf Go prüfen |
| D04 · A039/041, G075 | UI01 verschob den Quellanker Y406 auf 439, auf dem PSP also von etwa Y247 auf Y267. An der Unterkante kann in Reveal-Frames nur ein Teil des Labels sichtbar sein. UI02 setzt Y406 für beide Labels und Animationsrichtungen zurück. | Implementierte Rückkorrektur. Andere Ursachen für das fehlende Label bleiben offen |
| H01/H02 · A054/057/059–064, G098/100/102–105 | FldPanel2dUpdate verschiebt schwarze und animierte gelbe Platte sowie innere Maskenkanten. Ziel dunkle Kanten X9–188/Y246–263. Beide Text-Slots verwenden ihre Glyphenbreiten und -Offsets beim Zentrieren. Glyphengröße und Slide bleiben. | Implementiert. Klassenraum als repräsentativer Smoke |
| H03 · A001/052–064, G097–105 | Datum/Balken geerbt. Wetter-Kreis und -Symbol am UI01-Mittelpunkt (458,52) von ungefähr 22 auf 27 px. | Implementiert. Tageszeit/Wochentag behalten den UI01-Pfad |
| H04 · A053, G097 | Stadtkarte hat eine abweichende Ortsliste und Markierung. | Kreis, Clip und Liste bleiben für einen getrennten Vergleich offen |
| H05 · A055/056, G053/072 | Standarddialog mit dunkler Fläche X12–473/Y203–268, angepasstem Namen/Text und Fortschrittssymbol. Wortumbrüche, Ressourcen, Eingabe und Queue folgen der Basis. | Implementiert. Shop-Portraits und mehrzeilige Endzustände offen |
| C01–C04 · A010–013, G031–034 | Charakterauswahl X27–183 → 48–166, Y13 → 17. Ungeselektierter Kategorie-Preview +32 X nur in der Charakterphase. C03 behält den UI01-Fit. Ausrüstungslisten, Scrollbar und Help-Karte +20 Y, Statistikheader separat. | Implementiert. Charakterauswahl im Smoke, Kategorie und Help offen |
| C05/C06 · A003–009, G025–030 | Rechter Skill-/Item-Listenanker bleibt. Linke Skill- und Item-Party-Zeilen mit HP/SP, Zahlen und Namen von ca. 45,7 auf 48 px Pitch. | Implementiert. Skill mit einer Figur im Smoke |
| C07–C09/T01–T04 · A002/004/007/017/019, G024/026/028/038/040 | Titel-Anker Skill (111,222), Item (80,223), Status links (8,8) und rechts (326,7). UI01-Radar bleibt. | Titel umgesetzt. Camp-Hauptmenü, Radartext, Portrait und übrige Hints offen |
| S01–S04 · A026–028, G006/007 | Bestätigte Rahmen und Auswahlzustände bleiben. Memory-Card-Status 0xF (Prüfen/Lesen) verwendet die geometrische Mitte (320,224) und Blockmaße aus den Glyphen. | Implementiert. Kurzlebiger Status braucht Geräte-QA. Result-Icon bleibt UI01 |

## Zweiter Batch

| IDs und Referenzen | Umsetzung oder offener Punkt |
| --- | --- |
| N01/N02 · A014–016/021, G035–037/042 | Persona-Assign-Titel mit eigenem Fit aus P/G035. Stat-Balken, Affinity, Skillgrid, Help, Next-LV und Modelle offen. |
| N03 · A017–020, G038–041 | Status-Titel unter T03/T04 umgesetzt. Namen, Blend-Silhouette, Portrait und HP/SP im Status bleiben offen. |
| N04/T05 · A022–024, G043–045 | G045 bestätigt die untere Geometrie im vergleichbaren Detailzustand. Detail-Nameplate und zugeordneter Text X177–459 → 204–434. Geerbter Portrait-FIT und Liste separat. |
| N05 · A025, G003–005 | Save-Slot-Liste offen wegen abweichender Komposition und Slotformat. |
| N06–N08 · A030–036, G009/011–016 | Velvet- und Fusion-Panels offen. Eintragsstruktur und Maskenketten brauchen eigene Zuordnung. Fusion-Card und Skill-Inheritance behalten ihre Pfade. |
| N09 · A050, P/G079 | Grüner Kopfblock ab Y64 → 43 mit Name/HP/SP. Affinity-Leiste und Symbole −8 Y. Modell, Diamant, Skills und unbekannte Werte folgen der Vanilla-Komposition. |
| N10 · A055/056, G053/068/072 | Gemeinsame Dialoggeometrie unter H05 umgesetzt. Shop-Kauf-/Verkaufslisten und Masken offen. |
| N11 · A065, P/G084 | EXP/MONEY/ITEM-Gruppe mit Icons, Werten und Itemzeilen −4 X. Bild- und Titeldramaturgie bleiben Vanilla. |
| N12–N14 · A029/058/063/064, G008/046–049/098–105 | CHECK folgt weiter der Weltposition. Config und Spezialeffekte haben abweichende Kompositionen. Entrance Hall bleibt Render-QA. |
| N15 · A042/043 | Navigator-Portrait war im UI01-Hardwarelauf vorhanden. UI_NAV_TRACE für den Fehl- und Folgeaufruf verfügbar. Ursache offen. |

## Rückwege und Reproduzierbarkeit

UI01 stellt die UI01-Zeichenregeln und Quellanker wieder her. Die neue Build-ID und ihre Diagnoseausgabe bleiben sichtbar. UI_LEGACY hat Vorrang und deaktiviert die Alignment-Regeln beider Batches. UI_NAV_TRACE schaltet nur das bestehende begrenzte Protokoll ein. FPS ist der reguläre Marker. Die Dateien ohne Endung neben die EBOOT legen und kalt aus dem XMB starten. Static Flat bleibt Standard.

Sofort gezeichnete Komponenten nutzen Familien-Scopes im Screen-Pfad. Deferred Map- und Dialogteile erhalten ihre Koordinaten beim Schreiben der bisherigen Queue-Daten. Die Renderstates und Tiefe bleiben Eigentum ihrer bisherigen Callbacks. Der optionale HAnim-Vergleich nimmt seine 16-KiB-Scratchfläche bei Bedarf aus dem Heap, damit das feste Retail-Datenimage beim Link Platz behält.

## Native Validierung und privates Paket

Runtime 043724e7725fdf023e1dac67010669fceadcc69b. EBOOT SHA-256 7eb19548a9001dd549ac64c66550e091e79f9a49ebb02c9ef3b923cfe9a68a77. Das native PSP-GCC-15.2.0-Image mit -O2 enthält 821 Spielobjekte und 17 aktualisierte Übersetzungseinheiten. Link und PRX-Embedding bestanden, 19 Importbibliotheken gültig und keine nicht aufgelösten Funktionszeiger. Striktes QA07 und acht synthetische Negativ-/Positivprüfungen des Guards bestanden. Der C-Hosttest mit aktiven Assertions prüft die acht Radzeilen, Skill/Tactics-Kanten, beide Minimap-Projektionen, Titel, Roster und den Rückweg auf UI01. Der Fan-Patch bestand seine vier Negativfälle und den Quellvergleich.

Der kopierbare private Ordner enthält 80 Dateien und rund 5,75 GB. Die geklonten Spielstände 9, 10 und 16 sind bytegleich zur UI01-Testbasis. Der Smoke bestätigte Laden von Slot 9. Laden von 10 und 16 mit der neuen EBOOT bleibt für die Geräteprüfung offen.

## Visueller Smoke

PPSSPP 1.20.4 am festen Pfad C:/P4Velocity01/emulator/PPSSPPWindows64.exe, Software-Rendering. Der erste UI02-Lauf erreichte Klasse 2-2, Camp, Skill-Charakterwahl, Skill-Liste und Equip-Charakterwahl. FPS-Anzeige war sichtbar. Nach einer im Smoke gefundenen Abweichung wurde ausschließlich der optische Anker für Ortsnamen korrigiert und die finale EBOOT im selben Klassenzimmer erneut geprüft.

An der Scanlinie Y253 lag die dunkle Platte bei X9–187 inklusive. UI01 lag bei X19–244. Die hellen Pixel von Class 2-2 lagen im finalen UI02-Bild bei X67–126 und Y251–259. Die horizontale Mitte liegt damit etwa 1,5 px links von der Balkenmitte. Das Startup-Log bestätigt Static Flat, UI02 und die deaktivierten übrigen Performanceexperimente. Ein Kaltstart derselben finalen EBOOT mit UI01 protokollierte alignment-01 und reproduzierte die UI01-Messung für Platte und Schrift (X19–244 sowie X92–151). Im erreichten Abschnitt fand sich kein GE-Stop oder Watchdog. Der Smoke deckt weder Kampflisten noch Minimap-Clip, S04, Fusion oder das sporadische Navigator-Porträt ab.

## Hardware-QA

Für den normalen Lauf genügt FPS. Slot 9 deckt Schule, Camp, Equip, die Menütitel und Save/Load ab, Slot 10 Castle, beide Map-Modi, Dungeon-Navigator und Velvet Room. Slot 16 führt zum späten Izanami-Kampf und enthält Spoiler. Dort Rad, Skill, Item, Persona, Tactics und deren Animationszustände prüfen. Bei Fehlern dieselbe Szene nach Kaltstart mit UI01 vergleichen. Bei fehlendem Navigator-Porträt UI_NAV_TRACE setzen und einen Fehl- und Folgeaufruf mit Log sichern. PSP-Go-Abnahme des neuen Builds steht aus.
