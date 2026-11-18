# Vorbereitung der Quellcodeveröffentlichung
## Veröffentlichungsupdate – 18. November 2026

Der Entwicklungsbranch `dev/v0.27-perf06` wurde erfolgreich auf dem öffentlichen GitHub-Fork `MeMeNT0xMoRii/persona4-portable` veröffentlicht.

**Veröffentlichter Commit:** `f3c5d4e268c8c4378b915434bd2328a06aee02a2`

Die historischen Branches `dev/v0.27-perf03`, `dev/v0.27-perf04` und `dev/v0.27-perf05` sollen ebenfalls separat veröffentlicht werden, um die Entwicklungsschritte für andere Entwickler und Upstream nachvollziehbar zu machen.

Der reguläre perf06-Build befindet sich noch in der Hardwareabnahme auf echter PSP Go. Eine vollständige Wiederherstellung der Performance auf perf04-Niveau ist noch nicht bestätigt.

### Geplante Golden-inspirierte 16:9-Verbesserungen

Für zukünftige Entwicklungszyklen wird Persona 4 Golden (PS Vita) als ergänzende visuelle Referenz verwendet.

Eine private Sammlung von 71 Screenshots in nativer Auflösung (960 × 544) dokumentiert verschiedene Menüs, Charakterporträts, Social Links, Kampf-HUD, Fusion, Dialoge und weitere Benutzeroberflächen.

Ziele der geplanten Analyse:

- Horizontal gestreckte Charakter- und Social-Link-Porträts korrigieren.
- Fehlerhaft skalierte oder positionierte UI-Elemente identifizieren.
- Abgeschnittene All-Out-Attack-Porträts überprüfen.
- Einzelne Menü- und HUD-Layouts für die 16:9-Darstellung der PSP Go (480 × 272) verbessern.
- Die originale PS2-Gestaltung möglichst originalgetreu erhalten.

Persona 4 Golden dient dabei ausschließlich als zusätzliche Referenz für geeignete Widescreen-Anpassungen. Die PS2-US-Version bleibt unsere primäre Vorlage.

Die Golden-Screenshots sowie private PS2-/PSP-Go-Vergleichsaufnahmen werden nicht öffentlich bereitgestellt.

Diese UI-Verbesserungen sind derzeit geplant und **nicht Bestandteil des veröffentlichten perf06-Quellcodestands**. Eine spätere Implementierung soll separat entwickelt und auf Renderingfehler sowie Performance-Regressionen geprüft werden.
## Remote-Abgleich vom 18. November 2026

`git ls-remote --heads origin` ergab für `dev/v0.27` den Commit `ac513309495b7160e9f7eb704d4bb57253a82489` und für `main` den Commit `0d89414f8e2a7c67c2c3e9f7d7a567701616e0b9`. Der öffentliche Fork führte keinen der vier unten genannten Entwicklungsbranches. Der symbolische HEAD von `dreikk91/persona4-psp` zeigte auf `main` bei `59b79763b2b175cb5e1ab6c92dda053f97e25c9f`.

| Lokaler Stand | Öffentlicher Zielref nach gesonderter Freigabe | Inhalt |
| --- | --- | --- |
| `12cf596c9bb79fdd67a3dabe4119c327c863d97c` | `refs/heads/dev/v0.27-perf03` | Drei Commits ab dem öffentlichen Entwicklungsstand. Animation, Ressourcenlader, Labornachweise. |
| `1b4a369d0dd143a58d90caabc55c9eae523adbd3` | `refs/heads/dev/v0.27-perf04` | Fünf weitere Commits. VU- und Fusionskorrekturen sowie Hardware-Abnahmeprotokoll. |
| `45110f63ca103cfd929d54397cb310ad379bd789` | `refs/heads/dev/v0.27-perf05` | Fünf weitere Commits. Historischer Vergleichsstand mit auf Go bestätigter Feld-Regression. Rush und Wetter sind einzeln übernehmbar. |
| Lokaler Referenzstand des perf06-Berichts, Laufzeitquellen `29b0fcf` | `refs/heads/dev/v0.27-perf06` | Eigener Zweig ab perf04. Die übernommenen Einzelcommits verweisen im Committext auf ihre perf05-Herkunft. |

Die vorgesehenen Übertragungen sind genau die vier gleichnamigen Branches in den Fork. Für jede Freigabe den vollständigen lokalen SHA festhalten und genau diesen SHA auf den genannten Zielref übertragen. Vorher die Remote-Refs erneut abfragen und bei einer zwischenzeitlichen Veröffentlichung die Abstammung prüfen. Für einen UI-Bericht oder Versuch wäre eine eigene Entscheidung über einen zusätzlichen Ref erforderlich. Der Plan enthält keinen Push auf `main`, `dev/v0.27` oder Upstream und keine GitHub-Release-Anhänge.

## Umfang der Prüfung

Für den Bereich ab dem öffentlichen `dev/v0.27` die Commitfolge, alle hinzugefügten und wieder entfernten Pfade sowie die Textdiffs überprüfen. Im Vorabgleich der vier Entwicklungszweige fanden sich unter den geänderten Pfaden keine Spielarchive, EBOOTs, PRX-, ELF-, ISO-, CHD-, Save-, Log-, GE-Dump- oder Bild-/Videodateien. Der Abgleich der hinzugefügten Textzeilen auf persönliche Windows- und Home-Pfade war unauffällig. Die alleinige Prüfung des letzten Git-Baums wäre für einen Branch-Push unzureichend.

Die Projektlizenz und [THIRD_PARTY.md](../THIRD_PARTY.md) begrenzen die MIT-Freigabe auf eigene Beiträge. Insbesondere `tools/psp_census.py`, die VU-Übersetzung, gemischte Rendererdateien, Testfälle mit Spieladressen und ältere Widescreen-Anpassungen benötigen eine inhaltliche Herkunftsprüfung durch die Maintainer. Die fehlende pauschale Lizenz des Decompilation-Checkouts ist im Drittanbieterhinweis dokumentiert. Bei ungeklärter Herkunft die betroffenen Änderungen aus einem separat geprüften Veröffentlichungszweig auslassen. Originalcheckout und lokale Teststände bleiben erhalten.

Build-Ausgaben, relocatierte Daten, EBOOTs, die vollständigen Memory Sticks, Rohlogs, Saves, Bildschirmfotos, Bildserien und GE-Dumps bleiben privat. Für öffentliche Erläuterungen genügen Quellcode, ein anonymisierter Ablauf sowie zusammengefasste Messwerte mit Build und Testplattform. Für die historischen Branches sind die später bestätigten Gerätebefunde aus dem perf06-Bericht beim Veröffentlichen der Beschreibung zu ergänzen.
