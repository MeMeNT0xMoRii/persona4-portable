# v0.27-dev · Phase 1

Stand 17. November 2026. `dev/v0.27` geht direkt von `9e1da616bc773c7fd5dc7246d58daf19c00f5fa0` (b249-xmb-home26) aus. Home25 (`413e06a327351f5eb32c63ea1433e30b645c2041`) ist die A/B-Referenz. [Handoff](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/538946094b63345d0fac75c44845f3aa3cb35004/docs/handoff/home26-2026-11-17)

## Kandidatenmatrix

Geprüft wurden die tatsächlichen Diffs vom gemeinsamen b249-Vorfahren `b23d7db` über [b339](https://github.com/dreikk91/persona4-psp/commit/d55464820ed19030b7973a9398e3b211403f8475), [b340](https://github.com/dreikk91/persona4-psp/commit/32e2aebc442a148051fad29a7062d1e7c5cc8d2f) und den gemeinsamen [b341–b344-Commit](https://github.com/dreikk91/persona4-psp/commit/99ae0c6229195cfc312d22595b9d50563c8f2c54).

| Kandidat | Einordnung | Entscheidung |
| --- | --- | --- |
| b339 · Fork-Pfade, FPS, Nebel, TV-Ringe, TMX/Town Map, Shuffle Time, Fusion und Social Stats | Bereits vorhanden | Fork-Pfade in Renderer, Census und Plattform beibehalten. Home26-Movie-Handoff, Fusionstrace und Layer-Trace bleiben die Referenz. |
| b339 · GE-Frame-Dump | Kompatibel nach Anpassung | In einem eigenen Diagnosemodus integriert. Der Fork startet dafür seriell. Erfasst wird die nächste gezeichnete Liste vor Wiederverwendung von Liste und Pools. Der Aufnahmestatus wird beim Medienwechsel verworfen. |
| b339 · UI-Glyphen/Sprites | Anpassung erforderlich | `p4_ui_quad_draw` plus Änderungen im generierten Spielcode brauchen Vergleiche zu den bestehenden Widescreen-Koordinaten, Clipgrenzen, Farb-/Alpha- und Texture-States. Als einzelner Performancekandidat nachziehen. |
| b339 · GE-CALL, statische Listen, Strip-Pack, LOD, undrawn ticks | Teilweise vorhanden, Rest riskant oder anpassungsbedürftig | Home26 nutzt bereits `p4_mesh_call`, `p4_skin_call`, direkte Bone-Zeilen und Guard-/Near-Plane-Pfade. Upstreams neue Listen speichern Zustände und Geometrie über Frames, Strip-Pack ergänzt ein Laufzeitartefakt. Vor einer Übernahme GE-Lebensdauer, Cache-Invalidierung, Fallbacks, Near-Plane und Draw-Reihenfolge gegen den Fork prüfen. LOD kann sichtbare Geometrie ändern. |
| b339 · Scene Launcher | Für die Phase-1-Diagnose zurückgestellt | `p4_scene_frame` sitzt im Pad-Pfad, zeichnet die Menüoberfläche per CPU in den angezeigten Framebuffer und manipuliert Szenen-/Storyzustand. Braucht getrennte Tests zu Medien, Saves und reproduzierbaren Szenensprüngen. |
| b340 · Windows-Extraktion | Kompatibel und nützlich | `docs/probe_archive` im gepinnten Decomp-Archiv wird bei der Extraktion übersprungen. Pfadvalidierung und kanonischer Archivhash erfassen weiterhin den ganzen Eingabesatz. Extraktionstest ergänzt. |
| b340 · Cache-Freigabe, GE-Idle und Feldpaket-Teile | Anpassung erforderlich | Die b339-Allocator-Retry-Kette und der Field-Pack-Flush fehlen im Fork. b340 ruft den Video-Handoff zur Freigabe auf. Home26s `p4_video_renderer_end` wählt dabei die Framebuffer-Bank neu und setzt Frame-Pacing und Feedback zurück. Falls der Speicherfehler hier reproduzierbar wird, Freigabe auf dem Draw-Thread separat vom Video-Handoff entwickeln und erst nach nachgewiesenem GE-Idle ausführen. Upstreams `F023_003.P01` erklärt den gemeldeten Schulweg-Absturz nicht. |
| b341–b343 · 2D-Depth, Masken unter Last, Alpha-Quantisierung | Anpassung erforderlich, grober Alpha-Modus riskant | Home26 hat den exakten 8-Bit-Effektmaskenpfad für `TEST=0x31001`, separate Z-Write-Partikelfälle und eine andere Filterung für Zielalpha/Framebuffer-Feedback. Upstreams erweiterte Z-Test-Zulassung und Crowd-Schwelle lassen sich erst nach einem konkreten Writer/Reader-Replay bewerten. Die grobe Rekonstruktion ab Bit 32 kann weiche Kanten sichtbar ändern. |
| b344 · Cloud-Shortcut | Relevanz für die offenen Fehler unbewiesen | Der neue Census-Hook trifft nur `DistortParticleDrawSky` Modus 0 mit passendem Masken-/Noise-Paar und Blend `0x54`. Bis zu rund 2,15 MB Cache und geänderte Kanten. Zio, Apsaras und Nebel sind diesem Pfad bisher nicht zugeordnet. |

## Änderungen und Build

- [`b6cf965`](https://github.com/MeMeNT0xMoRii/persona4-portable/commit/b6cf965a1ffd304c923608a2249231e1155c944e) · Opt-in-GE-Dump aus b339. `L+R+Square` speichert einmal pro Tastendruck `ge_NNNN.ppdmp` neben dem EBOOT. Ziel ist PPSSPP-Dumpformat 6. Die Routine begrenzt die Größe und prüft Dateivergabe, Schreibfehler und Listengrenzen.
- [`ad408e3`](https://github.com/MeMeNT0xMoRii/persona4-portable/commit/ad408e3aeadc64cf7274e5b09ac25afe5b9a6247) · Windows-Pfadlängen-Fix aus b340 mit Host-Regressionstest.

Gezielter Diagnose-Relink mit `python tools/psp_hw_debug.py --release --ge-dump --out build/v027-ge`. Der Build benutzt XMB-Runtime-Dateinamen, kennzeichnet sich im Manifest mit `ge_dump: true` und `release: false` und meldet den seriellen Modus im Startlog. Der Standardbuild behält den gepipelineten Renderer. Zum Start alle drei Runtime-Dateien aus demselben Satz in einer vorbereiteten US-Installation verwenden. Fertige Spielressourcen werden weiterhin separat aus der eigenen US-Disc benötigt. Dumpdateien können Spieltexturen oder Geometrie enthalten und bleiben privat.

Der Build verwendete den vorhandenen, SHA-1-geprüften US-ELF, den gepinnten Decomp-Stand `363eab8a966d60a9773b45f330f2344329ec4f0a` und die vorhandenen Census-, Rebase- und Link-Eingaben. Alle 821 Spielobjekte stimmen per SHA-256 mit dem archivierten home26-Manifest überein. Linux PSPDEV [v20261101](https://github.com/pspdev/pspdev/releases/tag/v20261101) mit GCC 15.2.0, Archiv-SHA-256 `5dc9d785a5fd490ee9d5204b90f715efb6c46940ba3944036bbef9c993c17713`, mit nativen `.exe`-Symlinks. Beide Relinks erzeugten bytegleiche Runtime-Dateien. Die Identität zur historischen Linux-SDK-Installation und ein bytegleicher kompletter home26-Neubau sind offen.

| Runtime-Datei | SHA-256 |
| --- | --- |
| `EBOOT.PBP` | `b1c5ef66a7e2ba3b473b81ef331ab80b3acc9ad7736e92a86ac451789733c775` |
| `p4data.bin` | `f9b77acaee7a650837877c0cd7ee586fa2b1593833765d6ee048f41852273289` |
| `p4str.bin` | `4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4` |

## Prüfungen und Grenzen

- Host · 80 bestanden, zwei übersprungen, drei Fehler. Home26 unter gleichen Voraussetzungen: 79 bestanden, zwei übersprungen, dieselben drei Fehler (VAG-Encoder unter Linux und zwei ältere Renderer-Fixtures). Der neue Extraktionstest bestand. Windows selbst wurde nicht ausgeführt.
- PSP-Build · 19 Importbibliotheken validiert, feste Datenadresse bestätigt, keine ungelösten nativen Funktionszeiger. Kein vollständiger Neukompilierungslauf der Spielobjekte.
- PPSSPP 1.20.4 · Software und OpenGL luden den Diagnosebuild mit 2.476 fehlerfrei geprüften String-Einträgen bis zur Anforderung von `font/size12/f11_001.tmx`. Separat erzeugte ein synthetischer GU-Probeaufbau mit dem neuen Dump-Writer auf beiden Backends identische Dumps. Geprüft wurden Tastenflanke, Version, zstd-Sektionen, Datengrenzen und der Replay-RGB-Wert eines grünen Sprites und seines schwarzen Hintergrunds im Software-Renderer.
- PSP Go · Keine neue Geräteabnahme. Vollständige US-Installation und passende Replays für Gameplay fehlen im Prüfsetup. XMB/HOME, Save/Load, Fusion, CRIWARE, Übergänge, Dungeon, Partikel und UI müssen mit den vorgesehenen Szenen erneut auf Hardware geprüft werden.
- Dump-Grenzen · Der Writer liest Daten nach dem GE-Abschluss. Zwischenzeitlich überschriebene Texturen, Bedingungssprünge und Feedback über mehrere Frames können die Wiedergabe verändern. Serieller Betrieb beeinflusst Timing. Bei Szenenbelegen erst prüfen, ob der Replay den beobachteten Frame tatsächlich abbildet.

Die home26-Baseline-Befunde gelten weiter. CRIWARE war bereits auf PSP Go korrekt zentriert. Titelblitze, Lichtkugeln, Apsaras-Flackern und Junes-Performance bleiben offen, ebenso die übrigen Punkte aus dem Handoff.

## Phase 2

Mit vorbereiteter US-Installation und passenden Saves den Diagnosebuild auf PSP Go starten. Zuerst einen kontrollierten Apsaras- oder Zio-Frame mit Dump und passendem home26-Log abgleichen. Danach den ersten abweichenden Draw und dessen Alpha-/Feedback-Verbraucher bestimmen. Für Intro/Titel und Junes West Frames im normalen Pipeline-Betrieb separat bei 333 MHz messen. Damit lassen sich Maskenänderungen beziehungsweise ein eng begrenzter Performancekandidat gezielt qualifizieren.
