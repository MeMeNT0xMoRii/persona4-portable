# v0.27 · Fehler und Fortschritt

## Stand nach dem perf04-Gerätetest und perf05

Das Fusionsmodell blieb laut mehreren perf04-Läufen auf der Go stabil. Die zuvor beobachteten Fusions-Lichtkugeln traten dabei nicht wieder auf. Zio mit Izanagi und Apsaras bleibt ein bestätigter eigener Fall. Für Margaret nach der Fusion und die weiße Fernwand im Castle fehlt der Nachweis einer perf04-Regression. Die dünnen Bodenlinien werden separat geführt.

perf05 korrigiert die Tiefenmasken der TV-Wettersymbole und respektiert das Mipmap-Formatbit der Quelltexturen. Wetterkarten sind im isoliert aufgerufenen Spielrenderer visuell überprüft. Im Klassenzimmer wurden 411.264 Byte zusätzliche Mip-Daten eingespart. Rush/Stop sitzt nach Korrektur einer falsch gebundenen US-Datenadresse im PPSSPP-Kampf sichtbar unten links. Die weiße Fernwand ließ sich aus Slot 10 in Yukikos Castle reproduzieren. Der Wandausschnitt blieb beim diagnostischen Abschalten des GE-Nebels pixelgleich, während sich der benachbarte Gangabschnitt sichtbar änderte. Die Geräteabnahme, Shopping-Flackern, Teddie, Return to Title und HOME bleiben offen. Messbedingungen, Szenen und Paketprüfung stehen im [perf05-Bericht](v027_perf05.md).

Ausgangspunkt ist perf01 nach dem [PSP-Go-Test](v027_perf01_hardware.md). Für jeden historischen Eintrag bleiben Originalbeobachtung, Szene, Medienverweise und damaliger Build im [Playtest-Katalog](playtest.md) bzw. im [home26-Handoff](https://github.com/MeMeNT0xMoRii/persona4-portable/blob/538946094b63345d0fac75c44845f3aa3cb35004/docs/handoff/home26-2026-11-17/KNOWN_ISSUES.md) nachlesbar. Gleiche Nummern mit verschiedenen Präfixen bezeichnen verschiedene Einträge. Die Statusangaben gelten für den jeweils genannten Teilfall.

## Entwicklung seit dem perf01-Gerätetest

Der [perf02-Bericht](v027_perf02.md) führt Pixelvergleiche, Messbedingungen, Artefakthashes und die Abnahmeroute. Quellstand des Testbuilds ist `410fb09`. Der spätere perf02-Gerätelauf ergab in Junes rund 83,4 ms typische Frametime. Build, Emulatorvergleiche und Geräteabnahme für den Nachfolger stehen im [perf03-Bericht](v027_perf03.md).

| Bereich | Befund, Status und nächster Nachweis |
| --- | --- |
| P4-017 und Ladezeiten | **Technisch verbessert.** Die optionale Skin-Batch-Schätzung blockiert standardmäßig keinen Erstaufbau mehr (`609830d`). Native Texturblöcke werden zeilenweise kopiert (`5e1c3b9`), Mip-Deskriptoren gesammelt gebunden (`08eeac5`). CPU- und Befehlsersparnis sind synthetisch gemessen. Der Engpass in Junes, Titelframerate und Kampfspitzen sind auf Hardware offen. |
| P4P-001, P4P-003, P4-053, P4-058 | **Zwei isolierte Maskenfälle technisch korrigiert.** Der spätere überlappende Writer besitzt sein Pixel (`dfe256d`). Gleichmäßiges Alpha auf opaker nativer Textur wird in einem Durchgang geschrieben (`b3b83a8`). Pixelvergleiche in beiden Emulator-Backends bestanden. Lichtkugeln, Apsaras-Flackern, Igor-Schatten und Margarets Farbe bleiben als eigenständige sichtbare Probleme offen. |
| Teddies Porträt, P4P-015/016 und P4-029/033/055 | **Allokationsfehler technisch behoben, Szenenursache offen.** Fehler beim Erstellen indizierter Texturen und beim CPU-Lock halten das Raster konsistent (`8612fa1`), geprüft durch erzwungenen Speichermangel. Zwei warme Eintritte gegen Kaltstart stellen. Sporadische Abstürze und HOME-Hänger behalten ihren bisherigen Status. |
| P4-026, P4P-006/007/009/010, Schulkorridor | **Offen.** PS2-Patch und vorhandene Standbilder wurden geprüft. Eine phasengleiche Referenz für die beschnittenen Porträts und das Bodenmaterial fehlt. Der optionale Im2D-Trace (`0f09375`, Test `7717589`) erfasst die ersten 64 Eingaben je angefordertem Frame. Geometrie, Filter und UV-Korrekturen erst nach Zuordnung des Draws bewerten. |
| P4P-011/012, P4P-002/004 | **Offen.** `3027f1b` macht Filmgrenze, Pacing und die langsamsten Intervalle im separaten Pipeline-Timingbuild sichtbar. Titelblitz, Vordergrund/Wagen sowie die beiden Linienbefunde bleiben für den Vergleich an derselben Kamera und Filmphase vorgemerkt. |
| b339–b344 | **Teilweise integriert.** Statische Langzeitlisten, Offline-Skin-Packs, globale Cache-Freigabe, gröbere Alpha-Stufen, Cloud-Cache und weitere Qualitätsschalter sind mit Speicherbedarf und Messvoraussetzungen im [Kandidatenvergleich](v027_perf02.md#upstream-und-auswahl) zurückgestellt. |

## Aktuelle Gerätebefunde und Untersuchungen

| ID oder Beobachtung | Stand und nächster Vergleich |
| --- | --- |
| P4P-001 / P4-053 · Lichtkugeln | Auf Go weiter bei Zio und Fusion, bei weiteren Effekten historisch dokumentiert. Lichtkugeln bei All-Out sind unbestätigt. PTank-Z-Write und exakte Quad-Masken sind technisch geprüft. Für jeden Effekt den ersten auffälligen Writer und den späteren Blend-/Feedback-Leser bestimmen. |
| P4P-002 / P4-005 / P4-014 · Vordergrund | Schlafzimmer und Konishi-Laden mit fester Kamera. Junes-Einkaufswagen bleiben auf perf01 fehlerhaft. Je Objekt Mesh, Tiefenwerte, Clip und Reihenfolge bestimmen. |
| P4P-003 / P4-058 · Fusionsergebnis | Neu entstandene Persona auf Go alternierend hell/dunkel. Apsaras ist das reproduzierbare Beispiel. home25-Log zeigt wechselndes Feedback/Additiv bei gleicher Meshzahl. Bankrichtung wechselt je Lauf. Slot 10, benachbarte abgeschlossene Frames vergleichen. Igor-/Tischschatten separat. |
| P4P-004 / P4-022 · Nebel | Schwarze Streifen beim frei steuerbaren Erkunden des frühen Nebelwegs vor dem Kampf und weiße Bodenlinien in Yukikos Castle sind getrennte Go-Befunde. Distanznebel ist seit home19 technisch wiederhergestellt. Feste Kamera, Clip-, Tiefen- und Screen-Copy-Grenzen prüfen. |
| P4P-005 / P4-001 · CRIWARE | Darstellung historisch wiederhergestellt, Zentrierung auf home26 und perf01 auf Go bestätigt. Kaltstart als Kontrolle. |
| P4P-006 / P4P-007 · Wetter / Rush | Icons ragen aus den TV-Kacheln, die Rush-/Stop-Anzeige sitzt zu hoch. Die PS2-Referenz zeigt sie unten links. Beide auf Go offen. Je Sprite Anker, Scissor und Animationsphase erfassen. |
| P4P-008 / P4P-009 / P4P-010 · Tageswechsel, Fusion, Social Link | Tageswechselfarben weiter zum Original vergleichen. Dunkle Blöcke um beabsichtigte Fusionslisten-Striche offen. Social-Link-Porträts und Velvet-Room-UI eventuell zu breit. Vergleich je Element gegen natives PS2-4:3 und separat den US-16:9-Patch. |
| P4P-011 / P4P-012 · Intro/Titel | Langsamer Übergang und einfarbiger Return-to-Title-Blitz auf Go. Skip, kompletter Film, TV-Eintritt und Schwierigkeitswahl separat. Der home26-Bankwechsel ist im isolierten Test korrekt. |
| P4P-013 / P4P-014 · Teddie-Audio / Margaret | Stimmen-/Animationszuordnung benötigt zeitgleichen PS2-Vergleich. Margaret erscheint auf perf01 weiterhin matt-bläulich (auch P4-041). |
| P4P-015 / P4P-016 · Absturz / HOME | Schulweg nach Yosukes Mülleimerszene und sporadisches Hängen beim Exit. Perf01 bestand den getesteten Exit. Beim Auftreten Log vor Neustart sichern. |
| Teddies Dungeon-Porträt | Zweimal im selben Lauf fehlend, nach komplettem Neustart sichtbar. Slot 10, Eintritt/Wiedereintritt mit kaltem Start vergleichen. UI-Draw, Ressource, Raster und CLUT unterscheiden. |
| P4-026 · All-Out-Porträts | Auf perf01 bei zwei und vier Figuren beschnitten, drei Figuren sind Kontrollfall. Rushing-Modelle und Schlusslicht separat prüfen. |
| Schulkorridor · Ferntextur | Go zeigt unscharfen Boden, der beim Annähern schärfer wird. Erst Material, UV und Mip-Stufe in gleicher Kamera identifizieren. P4-054 betrifft frühere schwarze Polygone. |
| P4-017 · Laufzeit | Junes West, Shopping North/South, TV-Hub, Schulversammlung, Korridor, Titel, Kampfeinstieg und All-Out getrennt messen. Overlay-Beobachtungen und ältere Gerätewerte stehen in [perf01](v027_perf01_hardware.md) und [Performancehistorie](perf_status.md). |

## Stand nach perf04

- **P4P-003 / P4-058.** Der Wechsel zwischen hellem und dunklem Fusionsmodell ist für Apsaras im gehaltenen Erwerbsdialog unter PPSSPP korrigiert. Der allgemeine Projektionspfad verwendet nun zusammenhängende Vektorkomponenten. Weitere Ergebnis-Personas, die Helligkeit während der Animation und die PSP-Go-Abnahme sind offen.
- **P4P-001 / P4-053.** Vor und nach der Änderung wurden Bildserien und GE-Dumps während der Fusionsanimation am Effektmarker `18c9` erfasst. Die Tests erlauben keine Zuordnung der gemeldeten Lichtkugeln zu einem einzelnen Draw. Die blaue Zio-Entladung und Fusionspartikel auf der Go bleiben eigene Prüfpunkte.
- **P4-017.** perf03 erreichte auf der Go in Junes 12,4–13 FPS im Stand und 11,4–13 FPS in Bewegung. Der direkte Vergleich von Mesh-Replay-Eingaben in perf04 spart im kontrollierten PPSSPP-Lauf Gast-CPU-Zeit. Die Hardwaremessung steht aus.
- **P4-022, P4-043, P4-054.** Die sporadische weiße Fernwand in Yukikos Castle, dünne weiße Bodenlinien und schwarze Streifen auf dem frei steuerbaren Traumweg brauchen getrennte Kamera- und Bewegungsaufnahmen. Für das Shopping-Flackern fehlen ein reproduzierbarer Blickwinkel, der betroffene Gegenstand und die Bewegungsrichtung. Die Korridorprobe aus Slot 9 erfasst die verwendeten Filterzustände, die konkrete Mip-Stufe des Bodens in der Distanz ist offen.
- **P4P-006 / P4P-007.** Der TV-Kalender zeichnet Kartenrahmen, Datum und Wetter getrennt. Für Animation und Ruhezustand liegen Aufrufe von `calDrawWeather` vor. Der auslösende Sprite und der korrekte Bildausschnitt müssen in einer geöffneten Vorhersage bestimmt werden. Für Rush liegt die Sollposition links unten auf der PS2 vor. Beide Layoutfehler bleiben offen.
- **P4P-014.** Margarets stehende Pose nach einer Fusion und ihre sitzende Pose beim Wiedereintritt sind ein Gerätebefund des Benutzers. Ob die PS2 denselben Zustandswechsel zulässt, ist offen. Die Pose und ihre Farbdarstellung getrennt prüfen.

Messmethode, reproduzierte Szene und Hardwareabnahme stehen im [perf04-Bericht](v027_perf04.md).

## Vollständiger historischer Index P4-001 bis P4-058

„Gerät“ bezeichnet die damalige Bestätigung im angegebenen Testumfang. „Technik“ steht für implementierte, per Code, Host oder Emulator geprüfte Teilkorrekturen. Alle offenen Teilfälle behalten ihre [Spielstände und Routen](replay_saves.md).

| IDs | Gegenstand und Evidenz |
| --- | --- |
| P4-001, P4-003, P4-019, P4-049 | Gerät: Logo, normaler HOME-Exit, XMB-Pfad und Ziffern der Save-Dials. Perf01 bestätigt auch Speichern/Laden. Kritische Exit-Situationen siehe P4-033. |
| P4-002, P4-005, P4-007, P4-014 | Offen: Igor-Kartenblitze, Wandverdeckung, verschachtelte TV-Übergänge, Konishi-Kameracut. TV-Eintritt teilweise in PPSSPP geprüft. |
| P4-004, P4-052 | Gerät home17: Füllung des Tagesdatums und Social-Stats-Pentagon. Farbbeobachtung P4P-008 bleibt eigenständig. |
| P4-006, P4-008, P4-009, P4-018 | Technik: Effektpositionsschreiber. Gerätehinweise zu Regen, offene Szenen für Fragezeichen/Schirme. Schulversammlung mit defektem Protagonisten sporadisch. |
| P4-010, P4-013, P4-016 | Gerät home5 für Menü-Grünstich und getestete Textabstände. Zusätzliche Textfälle und Abstand vor Dreieck-Tutorial prüfen. |
| P4-011, P4-012, P4-015 | Gegner-Erscheinen/Auflösen und Level-up-Balken mit PPSSPP-Teilbelegen, Boss-Replay offen. Garu-Cut-in zu tief laut Go-Foto, Sollphase offen. |
| P4-017, P4-020, P4-021, P4-025 | Frametime-Einbrüche, Dreieck bei Nanako. Torwand und helle Seitenflächen am Castle auf älteren Geräten verbessert, Kamerareplays behalten. |
| P4-022, P4-023, P4-024, P4-030 | Technik für Distanznebel. Gerät home6 für fliegenden/stehenden Shadow-Körper bzw. Maske. Fragmentierte Truhe weiter offen, Belohnung funktionierte im Emulator. |
| P4-026, P4-027, P4-028 | All-Out nach Teilnehmerzahl und Phase, Chies Erwachenslicht, Fool-Karte über Teddie. Effekte und Layout offen. |
| P4-029, P4-033, P4-055 | Sporadisch: Daytime-Rückkehr nach Chie, HOME bei Shadow Chie, Castle-Eintritt. Erfolgreiche Wiederholungen und Loader-/Callback-Teiltests vorhanden. |
| P4-031, P4-032 | Köpfe im Bosskampf zeitweise fehlend. Shuffle-Time-Flächen/Slime in home16 PPSSPP korrekt, Geräteprüfung des Reveals offen. |
| P4-034, P4-035, P4-036, P4-037 | Offen: geskriptete Posen/Bewegungen, Dojima nach Abfahrt, Yukikos Kopf, harte Lichtkante im Posterzimmer. |
| P4-038, P4-039, P4-040, P4-041, P4-042 | Offen: flache Tutorial-Shadows, Shadow-Yosuke-Modell, Schlafzimmervorhang, Margarets Farbe, Magician-Aufhellung. |
| P4-043, P4-044, P4-045, P4-046 | South-Streifen nach home13 auf Go verschwunden, North-Geometrie offen. Dungeon-Shadow-Sound in home9 gehört, beide Cues separat. Shuffle-Kartenhöhe und roter Teppich offen. |
| P4-047, P4-048, P4-050 | Battle-Kommando-Outline im getesteten Skill bestätigt, weitere Labelphasen prüfen. Fool-Aufhellung offen. Equip-Koordinaten technisch korrigiert, Scrollen und PS2-Framing offen. |
| P4-051, P4-053, P4-054 | Town-Map-Atlas auf home15 Go bestätigt. Skill-Bursts offen. Schwarze Korridorkeile auf home13 Go nicht mehr beobachtet. |
| P4-056, P4-057, P4-058 | Systemmenü-Farbband auch auf PS2 vorhanden, Ausprägung offen. Apsaras-Erwerb auf home18 Go nach korrigierten unaligned Loads gelungen. Späte Fusion/NG+ und visuelles Flackern separat. |

Weitere Referenzen: Statische TVs in Margarets Bossarena, ältere Kopf- und Cursor-Fixes in den [visuellen Befunden](visual_issues.md). Langzeithänger bei SAS/AVC mit Ton und Firmware-Exit sind getrennte Fälle, siehe [Medien-Handoff](audio_video_hang.md). Die [Phase-1-Matrix](v027_phase1.md) und der [perf01-Vergleich](v027_perf01.md) führen die bisherigen Integrationen, synthetischen Messergebnisse und zurückgestellten Upstream-Funktionen.
