# Persona4 PORTABLE · Projektübergabe home26

Stand 17. November 2026, Europe/Vienna. Startdokument für den nächsten Work-Chat.

| Bezug | Verifizierter Stand |
| --- | --- |
| Repository | [MeMeNT0xMoRii/persona4-portable](https://github.com/MeMeNT0xMoRii/persona4-portable) |
| Verbindliche Baseline | [b249-xmb-home26](https://github.com/MeMeNT0xMoRii/persona4-portable/tree/playthrough/b249-home26), `9e1da616bc773c7fd5dc7246d58daf19c00f5fa0` |
| Rückfall/A/B | `playthrough/b249-home25`, `413e06a327351f5eb32c63ea1433e30b645c2041` |
| Dokumentation | `handoff/home26-2026-11-17`, direkter Nachfolger der Baseline mit sieben Markdown-Dateien |
| Nächster Entwicklungsstand | `v0.27-dev`, vorgesehen `dev/v0.27`. Den Baseline-SHA beim späteren Abzweigen verwenden. |

## Git-Beziehungen und Zustand

`git ls-remote` bestätigte den home26-Ref. Lokal ist das Commit-Objekt vorhanden, sein Tree ist `bea8a4203c694f3551528eb73d64c04227b74e71`. Der Arbeitscheckout war bei der Inventur sauber und detached auf dem veröffentlichten SHA. Ein historischer **lokaler** Ref `playthrough/b249-home26` zeigt auf `f2191fa` mit identischem Tree; für Übergabe und Reproduktion gilt ausschließlich der veröffentlichte SHA. Ignorierte Buildartefakte sind separat inventarisiert. `dev/v0.27` war auf dem Remote zum Prüfzeitpunkt nicht vorhanden.

- Remote-`main` stand auf `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`. Das ist der b176-Stand; die aktuelle Fork-Entwicklung liegt auf den Playthrough-Refs. `da8e2ce` verbindet die damaligen Fork-Fixes mit Upstream b249 (`b23d7db`).
- Die verifizierten Remote-Tips home20 bis home26 bilden von `0dde6a9` über `fb87d51`, `5e7a4c6`, `93f1171`, `3183081` und `413e06a` eine Vorfahrkette bis `9e1da61`. Home25 ist über drei Implementierungscommits und einen Build-Identitätscommit mit home26 verbunden. Deren Status steht in [FIX_HISTORY.md](FIX_HISTORY.md).
- Der Handoff-Branch dient als lesbares Archiv. Für spätere Änderungen gilt der home26-Code-Commit als Startpunkt.

## Architektur

Persona 4 PS2 US (`SLUS_217.82`) läuft als nativer PSP-Port auf Geräten mit 64 MB RAM, hier auf PSP Go bei 480×272 und Zielbetrieb 333 MHz. Der Build verbindet dekompiliertes Spiel-C aus dem gepinnten externen Projekt, lokal übersetzte Routinen aus dem Original-ELF und PSP-spezifische Plattform- und RenderWare-Ersatzpfade. Die PSP GE verarbeitet Renderlisten, Skinning und Texturen. PS2-GS-Zustände für Blend, Alpha, Nebel, Tiefentest und Framebuffer-Feedback werden umgesetzt; die Unterschiede sind gerade für Effekte und UI relevant.

| Bereich | Code und Schnittstelle |
| --- | --- |
| Boot, Firmware und Plattform | `port/main.c`, `platform.c`, `lifecycle.c`, `paths.c`, `ee.c`. XMB startet den EBOOT-PRX. Die zugehörigen `p4data.bin` und `p4str.bin` liefern Spiel-Datenabbild und Strings. |
| Renderer | `port/rwsky.c` für RenderWare/GE und Present, `rwdata.c` und `rwmath.c`, ergänzende Header für Feedback, Clipping, Meshes und Texturswizzle. PS2-Funktionsadressen bleiben relevante Referenzpunkte. |
| Medien, Eingabe und Saves | `port/video*.c` für AVC/P4V und Filmübergänge, `audio*.c` für PSP/SAS, `files.c` für Archivindex und Dateizugriffe, `memcard.c` und `pad.c`. Proprietäre Daten stammen aus der eigenen US-Disc. |
| Source-Build und Diagnose | `tools/psp_census.py`, `psp_ptr_proof.py`, `psp_link.py`, `psp_hw_debug.py`. Disc-Aufbereitung separat. Vorbereitungspfade und Herkunft siehe [BUILD_REPRODUCTION.md](BUILD_REPRODUCTION.md). |

## Projektgeschichte und Entscheidungen

1. Frühe lokale Entwicklung bis b164: Code-/Datenübersetzung, Boot, RenderWare auf GE, Archive und Save-System, anschließend AVC und SAS. Die frühere lokale Historie ist nur ausschnittsweise dokumentiert. [Projektchronik](../../development_history.md)
2. b165/b176 und frühe Fork-Playtests: reproduzierbare Source-Utility, XMB, Save-Wiederherstellung, HOME, Archiv- und Rendererarbeit. b249 integrierte die spätere Upstream-Basis mit Fork-Fixes und weiteren PSP-Go-Replays. [b249-Validierung](../../build_validation_b249.md)
3. Home20–25 vertieften einzelne Partikel-, Alpha-, Feedback-, Textur-, Skin- und UI-Pfade. Home26 brachte einen Movie-Framebuffer-Handoff, CRIWARE-Zentrierung und einen begrenzten Animations-Trace. [Technischer home26-Buildbericht](../../build_b249_xmb_home26.md)
4. **PSP Go bestätigt** die CRIWARE-Zentrierung. Titelblitz, schwacher Introübergang, Effekte und weitere Hauptprobleme sind weiterhin beobachtet. Das Teddie-Porträt fehlte einmal und war beim Wiederholungstest wieder da. Die synthetischen Handoff- und Logging-Ergebnisse bleiben gültige Teilnachweise. Ihre Auswirkungen auf die beobachteten Spielszenen sind offen.
5. Home26 bleibt verbindlich. Handoff und Diagnose-Logging können später kontrolliert gegen home25 verglichen werden. Upstream b339–b344 ist Prüfmaterial für selektive Änderungen, weitere Schritte stehen in [V027_PREPARATION.md](V027_PREPARATION.md).

## Evidenz und Start im neuen Work-Chat

**Hardware bestätigt** bedeutet konkrete PSP-Go-Beobachtung aus dem Playtest. **Technisch gestützt** bedeutet Code-, Host- oder Emulator-Evidenz im angegebenen Umfang. **Ungetestet/Vermutung** bezeichnet offene Hypothesen. **Unbekannt/fehlend** bezeichnet nicht zugängliche Rohdateien oder unzureichend dokumentierte Details.

Übernimm zuerst Baseline-SHA, [offene Fehler](KNOWN_ISSUES.md), [Testbelege](TEST_EVIDENCE.md) und [Artefaktinventar](ARTIFACT_MANIFEST.md). Die sieben Dokumente können im neuen Chat als Einstieg referenziert werden. Für einen exakten Neubau fehlt in diesem Workspace die frühere native Linux-Toolchain; für Gameplay fehlen die vollständige vorbereitete Installation und einzeln passende Saves. Der vorhandene home26-Update-Satz ist geprüft. Frühere verschwundene Nachrichten und die home26-Hardware-Rohaufnahmen wurden nicht rekonstruiert. Der Chat kann mit Planung und Quellprüfung starten; für Hardware- oder Gameplay-Abnahmen müssen die fehlenden Eingaben separat bereitstehen.
