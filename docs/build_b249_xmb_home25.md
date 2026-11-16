# b249-xmb-home25 · PSP-Go-Hardware-Follow-up

Basis ist der veröffentlichte home24-Commit `3183081bec9683fd403575a3768eaa3653a95afe`, Baum `0cf70109efb376e1e88e3db8df261cf72eb57a55`. Die drei archivierten Runtime-Dateien entsprechen dessen Manifest. Dieser Build aktualisiert eine vorbereitete US-Installation (`SLUS_217.82`). Home25 wurde hier noch nicht auf PSP-Hardware ausgeführt.

## Korrekturen

- **Partikeltiefe.** Der US-Originalpfad `GeometryBlockSetPartState(0)` setzt `TEST_1=0x717FB` und `ALPHA_1=0x44`. Unterhalb der Alpha-Schwelle gilt `FB_ONLY`. Home24 ließ diese Fragmente dennoch Tiefe schreiben. Home25 behandelt die beiden Fragmentgruppen in der Reihenfolge der Partikel. Unterhalb der Schwelle bleibt die Farbwirkung erhalten, darüber wird weiterhin Tiefe geschrieben. Gleichartige Quads werden zusammen eingereicht. Die bestehenden Color-only-Partikel und die 8-Bit-Effektmaske wurden mit Kontrollfällen verglichen.
- **Fusionsliste.** Die Statuskreise nutzten eine rohe 16×16-Quad-Geometrie. Ihre Breite übernimmt jetzt die 4:3-Proportion bei gleichbleibenden Spaltenmitten. Die Anzeige links und die Auswahlregeln der Fusion verwenden weiterhin ihren jeweiligen Originalpfad.
- **CRIWARE.** Der Startup-Aufruf für `title/cri_logo01.tmx` zeichnet den ursprünglich sichtbaren 640×448-Ausschnitt der 1024×512-Textur. Auf der PSP wird das Bild proportional und mittig auf etwa 363×272 Pixel eingepasst. Der Host-Test prüft UV-Ausschnitt, Ausrichtung und das Seitenverhältnis. Der frühere TMX-Upload für große Texturen bleibt Bestandteil des Builds.

Die offizielle PCSX2-Datei `SLUS-21782_DEDC3B71.pnach` passt zum XOR-CRC `DEDC3B71` des vorhandenen US-ELFs. Ihr 16:9-Abschnitt enthält 55 Schreiboperationen für Kamera, FCL, Schrift, HUD und FMVs. Die beiden lokalen 2D-Anpassungen folgen den jeweiligen Draw-Koordinaten. Die 3D-Framing-Differenz zwischen PS2 4:3 und PSP 480×272 wurde bei der Einordnung berücksichtigt. Social-Link-Porträts und Garu bleiben Vergleiche pro Element und Animationsphase. [PCSX2-Patch](https://github.com/PCSX2/pcsx2_patches/blob/b919d7fb5849feeb30b2da07d02f6dc662f28ba5/patches/SLUS-21782_DEDC3B71.pnach)

## PSP-Go-Aufnahmen

| Aufnahme | Befund |
| --- | --- |
| `IMG_7278(1).mp4` | Bei etwa 0:24–0:27 sind Karten, Ring und heller Übergang zu sehen. Der Originalcode besitzt dafür additive Kartenpässe. Danach erscheint Apsaras mit wechselnder Helligkeit. Die Ursache dieses Flackerns und der von eigenen wie gegnerischen Skills gemeldeten Lichtkugeln ist offen. Die Partikel-Tiefenprobe isoliert eine Abweichung, ohne diese Szenen zuzuordnen. |
| `IMG_7277(1).jpeg` | Izanagi und Ukobach sind lesbar. Das PS2-Original zeichnet in unbesetzten Zeilen Strichplatzhalter und daneben einen Kreisraster. Die schwarzen Flächen um die Striche im PSP-Foto benötigen eine eigene Prüfung von Sprite, Palette und Alpha. Die Kreise haben jetzt eine lokal korrigierte Geometrie. [US-PS2-Listenreferenz bei etwa 6:07](https://archive.org/details/endurance-run-persona-4-part-14?start=367) |
| `IMG_7276(1).jpeg` | Junes West Entrance, 04/18, **11,8 FPS** in der Anzeige. Die auffälligen Drahtgestelle und Körbe befinden sich rechts neben dem Spieler. Die geprüften PS2-Sequenzen zeigen andere Bereiche des Junes-/TV-Zugangs. Für eine Aussage zu Mesh, Indizes, UV und Material fehlt die passende Kamera auf genau diese Objekte. |

Beim ersten HOME-Versuch blieb „Bitte warten…“ stehen, beim zweiten gelang der Exit. Der Pfad wartet im Callback auf einen laufenden Kartenbefehl, prüft die GE und fordert `ExitGame` an. Es gibt keinen Befund, der den Hänger einer dieser Phasen oder dem Audio zuordnet. Der Host-Harness prüft den Fall mit laufendem Save und hängender GE. Beim nächsten Auftreten hilft die letzte `HOME exit:`-Zeile in `p4.log`.

## A/B und Grenzen der Messung

PPSSPP 1.20.4, Software und OpenGL, liefert dieselben 32 RGB-Abtastungen für die Tiefenprobe. Unterhalb der Schwelle bleibt die spätere Schicht an sechs Stellen sichtbar. Bei Alpha 254 und 255 verdeckt sie der Vordergrund. Die Kontrollen für deaktiviertes Z-Schreiben, `0x71801` und sich überlappende Partikel stimmen mit dem erwarteten Ergebnis überein. Die bestehende 256-stufige Maskenrampe sowie die Fog- und Raster-Kontrollen wurden gegen home24 verglichen.

| Isolierte Displaylisten-Erstellung bei 333 MHz Gasttakt | Home24 | Home25 |
| --- | ---: | ---: |
| 115 Quads unter der Schwelle | 20 Bytes, ca. 0,65 µs | 36 Bytes, ca. 1,0 µs |
| 5 vollständig gemischte Quads | 20 Bytes, ca. 0,65 µs | 216 Bytes, ca. 3,1 µs |
| 115 vollständig gemischte Quads | 20 Bytes, ca. 0,65 µs | 4.616 Bytes, ca. 58 µs |

Die Zeiten erfassen die CPU-Submission bereits aufbereiteter Vertices. Die 115 vollständig gemischten Quads ergeben als obere Grenze 230 Draws. Unterhalb der Schwelle bleiben die Quads gebündelt. Die reale Zahl gemischter Quads und die GE-Zeit sind offen. Für Junes West Entrance, Shopping District South, Entrance Hall und Kampfbeginn fehlen vergleichbare home25-Messfenster. Das bestehende Hardwareprofil kann je Szene CPU-Zeit, GE-Wartezeit, Präsentation, Skin-Batches und Texture-Waits aufzeichnen, bei Kampfbeginn zusammen mit den Ladeereignissen. GE-Wartezeit enthält Synchronisationszeit.

## Test auf PSP Go

1. `EBOOT.PBP`, `p4data.bin` und `p4str.bin` gemeinsam ersetzen. Kaltstart bei 333 MHz, CRIWARE ansehen, Save laden und Ton prüfen.
2. Slot 10, Normal Spread mit Izanagi und Ukobach. Statuskreise und Platzhalter ansehen, Apsaras erwerben und den Dialog zwei Sekunden offen lassen. Bei Flackern `FUSION visual` aus `p4.log` sichern.
3. Zio, einen gegnerischen Skill und ein All-Out prüfen. Im Dungeon kurz Nebel, Fernsicht und Shuffle Time ansehen. Town Map, Kalender und Menüs stichprobenartig öffnen.
4. Mit derselben Kamera Junes West Entrance, Shopping District South und Entrance Hall prüfen. Kampfbeginn getrennt messen. Bei erneutem HOME-Hänger Szene und letzte `HOME exit:`-Zeile festhalten.

Der ausgelieferte EBOOT wurde in PPSSPP bis zum ersten Font-Asset gestartet. Sechs synthetische MIPS-Fälle mit dem gepackten EBOOT erreichen dieselbe Apsaras-Vorschau und dieselben Farb- und Menüwerte wie home24. In den Host-Tests gab es 77 bestandene, zwei übersprungene und drei bereits auf home24 bekannte Fehler. Die letzten betreffen den Windows-VAG-Compiler und zwei ältere Render-Mocks. Alle 16 PPSSPP-Nebelwerte entsprechen home24. Die Rasterkontrollen beim Framebuffer-Filter und bei Read-Locks stimmen mit der Referenz überein. Ein vollständiger Spielordner für Gameplay-Replays fehlt in der Prüfumgebung. Spielstart mit Assets, Save/Load, Sound, der komplette Fusionsablauf, Lichtkugeln, Hardware-FPS und der Firmware-Exit brauchen den PSP-Go-Test.
