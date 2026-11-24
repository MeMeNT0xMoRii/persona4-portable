# VELOCITY01 · Performance-Transplant

## Ausgangsbasis

`dev/VELOCITY01-performance` liegt in `C:/P4Velocity01/source`. Die Basis ist RESONANCE01 b262 aus dem PERF-HW-01-Profil, Commit `f14c2be290cbb8cd853da46642fd98041e2d3fe2`. Der b262-Paketbeleg und das native Manifest stimmen mit 36 archivierten Quellen, 821 Spielobjekten und den nativen Laufzeitartefakten überein. Die vier CRLF-Dateien im Archiv stimmen nach Zeilenenden-Normalisierung mit dem Git-Checkout überein. Der integrale b262-Recovery-Commit `526078a` ist Vorfahr. Zwischen b259 und b262 sind das Kampfrad UI-WHEEL-01/02, die Fusion-Kartenkorrektur, die Social-Link-/Kalenderkorrekturen, die Raster-Retirement-Änderung und die Minimap-Tiefenmaske in der Historie enthalten. Die Herkunft zeigt die Integration im Quellstand. Der Prüfstatus der Einzelkorrekturen richtet sich nach den unten aufgeführten Läufen.

Die b260-Runtime aus HARDWARE-PREVIEW-01 (`ffdeb774`) und das b261-Paket G2-LIFETIME-01 (`ed04ec8`) haben mit ihren lokalen Manifesten übereinstimmende EBOOT-Prüfsummen. Beide Commitstände sind Vorfahren von b262. Das separate UI-WHEEL-02-Artefakt hat intern weiter die Kennung 259.

Referenzen sind perf06 `29b0fcfdec70222a4052099b67fb4a11e8d8aff4`, b259 `ee514a5b53f49c47cb93b3ce8c865cf8942e1651` und der Dokumentationsstand `01a4f05e76adaddad37e4b96876a91a63c04c87f`. Die drei Upstream-Tags wurden per Git aufgelöst und mit ihrer Historie verglichen. Die Commit-IDs sind b388 `59b79763b2b175cb5e1ab6c92dda053f97e25c9f`, b450 `3c93f927ce716f8aa3bee2be10ef1c182cd18c91` und b493 `a337bfb49e22f9b814db7de551657a8445b88e76`. Die von Upstream ausgewiesenen Zahlen stammen aus einzelnen PSP-Profiling-Läufen. [b493-Changelog](https://github.com/dreikk91/persona4-psp/blob/b493/CHANGELOG.md) · [b493-Validierung](https://github.com/dreikk91/persona4-psp/blob/b493/docs/build_validation_b493.md)

## Migrationsmatrix

| Optimierung | In b262 vorhanden? | Upstream-Codepfad | Abhängigkeiten | Erwarteter Nutzen | Risiko |
| --- | --- | --- | --- | --- | --- |
| Weight Groups, flache Skin-Streams und kompakte Vertices | Gepackte S16/U16-Vertices, U8-Gewichte und indexierte Strips vorhanden. Variable Gruppen und flache Streams fehlen. | `port/skin.h`, b390–b411 | GE-VTYPE, Bone-Slots, Strips und CPU-Fallback | Weniger Nullgewichte pro Vertex, relevant für Junes-NPCs | Reihenfolge, Rundung, Skin-Guard, GE-Wörter und Heap. Ein früheres lokales Farbfeld-Experiment hatte 18 abweichende Pixel in Junes unter Software-Rendering. |
| Persistent Static Lists, `p4_static_flat` | Lokaler Mesh-CALL-Ring und standardmäßig ausgeschaltetes `p4_mesh_persist` vorhanden. Upstream-StaticLists fehlen. | `port/static_list.h`, b397–b400 | Cache-Invalidierung, GE-Retirement, Alpha/Depth, Guard, RTT | Weniger wiederholte Feld-Draw- und Materialarbeit | Neue längerlebige GE-Referenzen, Speicherbudget, mögliche Geometrieverluste |
| Native GE-Listen und s8/s16 | Raw-GE-/CALL-Pfade und gepackte Meshdaten vorhanden. Upstream-Compiler für ganze Static Lists fehlt. | `port/static_list.h`, b436–b445 | Statische Listen, Positionsdekodierung, Winding und Bounds | Upstream Heaven 2,65 → 1,62 MB Listendaten bei gleicher CPU-Zeit | Präzision, Guard und Listenkapazität |
| `p4_world_direct` | Fehlt. Retail-Passordnung plus eigener Atomic-/Mesh-Pfad vorhanden. | `port/world_direct.h`, b453–b461 | Feld-Buckets, StaticList/Instanz-Memo, Matrix-, Kamera-, Material- und Rasterinvalidierung | Stärkste direkte Upstream-Junes-Evidenz. Dort 52,8 → 44,4 ms CPU im Profiling-Lauf. | Ausgelassene Draws, Lifetime, Renderzustand, Fade-Buckets. Upstream b488 korrigierte einen Report-Zugriff nach Geometriefreigabe. |
| `p4_col_cull` | Upstream-AABB/Grid fehlt. | `port/collision.h`, b464–b472 | Aktuelle Geometrie-/Matrixdaten, konservative Tests, geordnete Treffer | Potenzial beim Laufen. Upstream Heaven-Brücke 4,0 → 0,7 Mio. Kollisionszyklen pro Frame. | Fehlende Kollisionen bei falschen Bounds oder vorzeitigem Reuse. Grid verändert temporär den Triangle-View. |
| `p4_frame_lazy` | Eigenes verzögertes Animations-Sampling vorhanden. Bone-Frame-Schreibaufschub fehlt. | `port/hanim.h`, b360/b473, Census-Hooks | Reader-Hooks für LTMs und Modellmatrizen | Upstream Heaven im Stand ca. 0,6 ms CPU | Falsche Attachment-, Effekt- oder Animationsmatrizen |
| `p4_world_defer` | Fehlt. | `port/world_direct.h`, b481 | World Direct und explizite Flush-Grenzen vor regulären Draws | Upstream Heaven ca. 0,4 ms CPU | Draw-Reihenfolge und Blend/Depth |
| Rastergenerationen | Vorher globale Texturgeneration. Jetzt enger b439-Teilschritt für dedizierte Glyphen. | `port/rwsky.c`, `port/static_list.h`, b439 | Bind-/CALL-Schlüssel, eindeutige Rasterzuordnung | Weniger allgemeiner Rebind-/CALL-Churn bei Glyphen-Uploads | Kollisionen der Stamp-Tabelle können zusätzliche Rebinds auslösen. Aliase und generische Schreiber behalten den globalen Schlüssel. |
| GE-/2D-State-Cache und b493-2D-Texturen | Eigene 3D- und benachbarte Im2D-State-Caches vorhanden. Upstream-Texture-only-Pfad und 2D-Prescaling fehlen. | `port/im2d.h`, `port/clut32.h`, b448–b450, b482–b493 | GE-State-Grenzen, Textur-Locks, CLUT, RTT und Alpha | Upstream vor allem Titelbild. Lokaler additiver Cache-Versuch sparte im gemessenen Camp-Frame keine State-Writes. | Schärfe, HP-Gauges, Fusion und Feedback. `p4_ui_native` bleibt aus. |

Abgeschaltete Upstream-Experimente wie `p4_static_pack`, `p4_static_gate` und `p4_eff_rotate` bleiben außerhalb des Builds. `p4_frame_lazy` und `p4_world_defer` sind bei Upstream b493 aktiv und haben kleinere gemessene CPU-Effekte. [Upstream-Renderer](https://github.com/dreikk91/persona4-psp/tree/b493/port)

## Implementierung

Runtime-Commit `8a6956e6fec507219b16835552cd2c74cc0125bc`. `port/rwsky.c` führt eine 1-KiB-Tabelle für Glyphen-Stamps ein. Ein Upload in eine dedizierte Glyphen-Rasterallokation verwirft zugehörige Texturbindungen und CALL-Aufzeichnungen. Andere gewöhnliche 3D-Texturen können ihre Aufzeichnungen weiterverwenden. Generische Pixel-/Paletten-Locks, Freigaben, Movie-Rückkehr, Aliase/RTT sowie die Whole-Draw-Replay-Keys verwenden weiterhin globale Invalidierung oder den globalen Schlüssel. GE-Draw-Reihenfolge, Guard, Listenbudget und Retirement bleiben in den vorhandenen Pfaden.

`VELOCITY_LEGACY_TEX` als leere Datei neben `EBOOT.PBP` schaltet beim nächsten Kaltstart auf den bisherigen globalen Schlüssel. Die gleiche EBOOT bleibt damit für beide Modi nutzbar. Auch bei fehlendem Speicher bleibt der Legacy-Modus aktiv. Das Startlog meldet `scoped` oder `legacy`.

## Native Prüfung und Host-Tests

- PSP-GCC 15.2.0, PSPSDK `09f02b88b9f3`, Decompilation `363eab8a966d60a9773b45f330f2344329ec4f0a`. Der Build kompiliert den Port neu und übernimmt die 821 gegenüber b262 geprüften Spielobjekte. Zwei Links mit eingebetteten Laufzeit-Digests, 19 validierte PSP-Importbibliotheken und 0 Funktionszeiger ohne natives Ziel. QA07 mit strenger Provenienz bestätigt das Manifest, die Quellen, die Spielobjekte und das eingebettete PRX.
- Fünf gezielte Host-Tests mit dem vorhandenen Windows TinyCC bestanden. Sie prüfen Glyphen-Revisionsgrenzen, Raster-Allocation/-Lock, Mip-Eligibility und zwei bestehende Draw-Pfade. Sechs Fehler in weiteren extrahierten Render-/Display-Harnesses reproduzieren sich mit demselben Compiler auf der unveränderten b262-Basis. POSIX-Lifecycle-Test auf Windows übersprungen.

## PPSSPP-Smoke und Junes-A/B

PPSSPP 1.20.4, jeweils frische Instanz mit passenden Runtime-Dateien und geklonten Ingame-Saves. Titelbild im Software-Renderer und Klasse 2-2 (`F006_006`) in OpenGL 1× bildlich bestätigt. Junes West (`F009_004`) wurde in OpenGL 1× in beiden Modi und im Software-Renderer im Feldbild bestätigt. Die Software-A/B-Bilder zeigen dieselbe statische Geometrie. Je nach Bildbank unterscheiden sich 559 bis 588 von 130.560 RGB-Pixeln in den Figurensilhouetten und ihrer direkten Umgebung. Die Aufnahmen waren zeitlich nicht auf dieselbe Animationsphase synchronisiert. Ein Teil der OpenGL-VRAM-Rücklesungen zeigte den vorherigen Render-Target-Inhalt. Diese Aufnahmen wurden nicht zur Bildabnahme verwendet.

| Junes West | Legacy | Scoped |
| --- | ---: | ---: |
| Mesh-CALL-Aufzeichnungen im 256-Frame-Fenster mit Szenenübergang | 463 | 402 |
| Skin-CALL-Aufzeichnungen im selben Fenster | 413 | 305 |
| Mesh-CALL-Aufzeichnungen in den beiden anschließenden stabilen Fenstern | 7, 2 | 7, 2 |
| Skin-CALL-Aufzeichnungen in denselben stabilen Fenstern | 3, 1 | 3, 1 |
| Stationäres Messfenster mit 15 s, akzeptierte Presents | 449 | 449 |
| p50 / p95 in diesem Emulatorfenster | 33,367 / 33,367 ms | 33,367 / 33,367 ms |

Die CALL-Zählung ist ein Hinweis auf weniger Aufzeichnungsarbeit beim Übergang. Die Emulatorläufe messen keine PSP-Go-Zeiten. Für stationäres Junes zeigen die vorliegenden Messungen keinen Vorteil.

Slot 10 lud Entrance Hall, Velvet Room und die Apsaras-Vorschau. Das Log meldete wiederholt Karten- und Feedback-Aktivität. Castle 1F und Abschluss des Dungeon-Tutorials sind im Log belegt. Die Korridorroute bestätigte nur den Klassenzimmer-Start. Ein Kampf lief trotz kurzer Suche in der prozeduralen Etage nicht an. Battle-Depth, All-Out Attack, Castle-Außenmauer, Nebel, Minimap-Kante und visuelle Fusion-Details bleiben Geräte- oder weitere Emulatortests. Während der erreichten Szenen erschienen keine Renderer-Stops. Der geerbte generische G2-/GE-Lifetime-Status bleibt `P0_UNVERIFIED`.

## Lokales PSP-Go-Testpaket

Installationsordner `C:/P4Velocity01/output/P4P-VELOCITY01/PSP/GAME/P4P-VELOCITY01`. Paket mit 74 einzeln geprüften Dateien, ca. 5,75 GB, einschließlich Ressourcen des vorhandenen privaten b262-Pakets und der geklonten Menüslots 9 und 10. Die Spielressourcen stammen aus dem bestehenden Laborpaket. `INSTALL-PSP-GO.md`, `package-sha256.csv` und `build-manifest.json` liegen daneben auf Paketebene.

| Datei | SHA-256 |
| --- | --- |
| `EBOOT.PBP` | `5cb02226fbffcc41868c820c5e2981a2eaed2a1b8e445021acdf4ade779fa626` |
| `p4data.bin` | `240f05638d2e5095e3cc9628d1204a699750f502a4244946f5f16f9dae27ea60` |
| `p4str.bin` | `4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4` |

PSP Go zuerst mit Junes West und demselben Kameraausschnitt prüfen. Mit `VELOCITY_LEGACY_TEX` und Kaltstart in den Legacy-Modus wechseln. Je Modus über L+R+START ein 15-s-Present-Fenster nach 5 s Vorlauf erfassen. Danach Schule, Castle, Fusion und einen normalen Kampf mit anschließendem All-Out-Return vergleichen. Der Gerätetest steht aus.

## Nächste Pakete für Junes

1. **Static Lists und World Direct.** Zuerst messen, welche opaken Junes-Buckets tatsächlich wiederholt denselben CALL erzeugen. Einen begrenzten Satz über den bestehenden CALL-Ring mit unveränderter Passreihenfolge wiederverwenden. Erst nach bestätigten Hits, Guard-/Castle-Kontrollen und Budgetmessung auf persistente Listen und direktes Dispatching ausweiten.
2. **Skin Weight Groups.** Die quantisierten Gewichte im bestehenden Skin-Schema gruppieren. RGBA, Topologie und CPU-Fallback erhalten. Junes-Draw-Anzahl, Gewichts-Slots, Matrix-Uploads und Software-/OpenGL-Bild prüfen. Flache Streams und Farbfeldentfernung separat bewerten.
3. **Collision Broad Phase.** AABB-Ausschluss für passende Feldabfragen im Compare-Modus prüfen. Treffer und Reihenfolge mit dem Retail-Pfad vergleichen. Junes im Laufprofil und im Stand getrennt messen. Das Grid erst bei belegtem Restaufwand betrachten.

## PSP-Go-A/B · erster Raster-Cache-Fix

Das Geräte-Log bestätigt Junes West `F009_004`, `PERFHW1 status=OK` und je 189 akzeptierte Presents ohne Ablehnungen. Scoped kam auf p50 / p95 / max von **83,415 / 83,428 / 83,445 ms**, Legacy auf **83,415 / 83,435 / 100,100 ms**. Die medianbasierte Rate beträgt in beiden Modi ca. **11,99 FPS**. Die Unterschiede bei p95 und Maximum belegen in diesem Paar keinen relevanten FPS-Gewinn. Der Glyphen-Cache bleibt als Experiment verfügbar.

## Static Flat · nächster Hardwarekandidat

Upstreams b493 World Direct lernt Feld-Buckets aus persistenten, atomaren Listen oder Instanz-Memos und prüft pro Eintrag Kamera, Modellmatrix, Texturen, Material, Draw-State und Guard-Band. Unser b262-Pfad besitzt einen bestehenden GE-CALL-Ring pro Mesh und einen standardmäßig deaktivierten Whole-Draw-Replay-Prototyp. Er hat keine entsprechend validierte persistente Liste pro Atomic. Für dieses Paket wurde deshalb die aus b393 stammende Static-Flat-Transformation auf den bestehenden Mesh-Cache übertragen. [Upstream Static Lists](https://github.com/dreikk91/persona4-psp/blob/b493/port/static_list.h) · [World Direct](https://github.com/dreikk91/persona4-psp/blob/b493/port/world_direct.h)

Der Kandidat expandiert Cache-Daten für geeignete unverdeckte Dreieckslisten und Strips in Indexreihenfolge, einschließlich degenerierter Strip-Verbinder. Er bewahrt die Vertexbytes und PRIM-Reihenfolge. Der zusätzliche Stream gehört derselben Allocation wie der indizierte Stream. Er nutzt dieselben Cache-Budget-, Eviction- und Movie-Drain-Pfade. Der GE-CALL-Key enthält die tatsächlich benutzten Adressen und den VTYPE. Cache-Miss, Budgetüberschreitung, Guard-Clipping, RTT und Schatten-Shear behalten den indizierten Pfad. `VELOCITY_INDEXED_MESH` schaltet diese Variante beim Kaltstart ab.

Der Nutzen hängt von GE-Zugriffen auf nicht zusammenhängende Vertexdaten ab. Höherer Speicherbedarf und verlorene indexierte Vertex-Wiederverwendung können diesen Vorteil reduzieren. Der PPSSPP-Zähler `static flat per 256 frames` zeigt Ausführung und Kopierumfang für den Hardware-A/B.

Native PSP-Compilation und Link bestanden. 19 Importbibliotheken, 0 Funktionszeiger ohne natives Ziel. Strikte QA07 und gezielter Host-Test für beide Vertex-Strides, Indexreihenfolge, degenerierte Verbinder, Alignment und ungültige Indizes bestanden.

PPSSPP 1.20.4 bestätigte die Save-9-Route über Klasse 2-2 nach Junes West in beiden Modi. In den verglichenen Software-Frames in Junes ist die statische Geometrie visuell gleich. Die Differenzen lagen im Bereich der animierten Figuren. Das Junes-Berichtsfenster zeigte 27.721 Flat-Draws bei 29.189 Mesh-Cache-Treffern. Der gesamte Mesh-Cache belegte 1.579.302 Byte mit Flat und 1.028.798 Byte im indizierten Lauf. Das OpenGL-Replay mit Slot 10 erreichte die Apsaras-Vorschau und Castle 1F einschließlich Tutorialabschluss. Die geprüften Läufe meldeten keinen GE-Stop. Battle-Depth, UI-WHEEL, All-Out und visuelle Detailkontrollen für Castle, Nebel, Minimap und Fusion bleiben für die Geräteprüfung offen.

Runtime 0c6b31af8058759f7344f658e8e628a9237578df. Lokales Update-Trio, Manifest, Prüfsummen und Testkarte unter C:/P4Velocity01/output/P4P-VELOCITY01-STATIC-FLAT. EBOOT-SHA-256 44396b0fdda5d6894e88d5b947412f7fc8c91f36493ffe69da685e86f113d753. Die neue Änderung ist auf PSP Go noch ungemessen.


## PSP-Go-A/B · Static Flat bestätigt

Junes West `F009_004`, beide Läufe mit Glyphenmodus Scoped und 15 s Accepted-Present-Fenster. Die Build-ID und `static mesh` bestätigen die Zuordnung. `indexed.log` hat einen Static-Flat-Buildstart im Indexed-Modus. In `static-flat.log` ist nur der dritte Start für diese Auswertung relevant. Die ersten beiden Starts gehören zum früheren Glyphen-A/B.

| Modus | Akzeptiert / abgewiesen | p50 | p95 | Max | Medianbasierte FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Indexed | 190 / 0 | 83,414 ms | 83,429 ms | 83,440 ms | 11,99 |
| Static Flat | 225 / 0 | 66,733 ms | 66,744 ms | 66,753 ms | 14,99 |

Beide Zeitfenster endeten mit `PERFHW1 status=OK`. Die Medianrate steigt um rund 25 %, entsprechend etwa fünf auf vier VBlanks pro Frame. Die Zahl der akzeptierten Presents im festen Fenster steigt um 18,4 %. Die Messung isoliert CPU und GE nicht. Im stationären Protokoll lagen die 256-Frame-Fenster bei 31.526 bis 32.088 Flat-Draws. Mesh-Cache 1.582.026 statt 1.024.874 Byte, also +557.152 Byte beziehungsweise etwa 0,53 MiB. In den untersuchten Logs erschienen weder GE-Stop noch Crash-Meldung. Static Flat bleibt der bevorzugte experimentelle Modus mit Indexed-Fallback.

## Persistent Replay · begrenzter Vorläufer für World Direct

Der Kandidat nutzt die vorhandene persistente Whole-Draw-Replay-Struktur über dem GE-CALL-Ring. Er zeichnet nur vollständige Folgen belegter BASE/CALL-Paare für vorhandene Flat-Meshes auf. Die Retail-Callbacks, Reihenfolge und Guard-Fallbacks bleiben aktiv. Opt-in per `VELOCITY_PERSIST`, Vergleich per `VELOCITY_PERSIST_CHECK`. Der Vergleich führt den regulären Draw aus. Im Replay-Modus wird alle 64 Frames stichprobenweise verglichen. Eine Abweichung deaktiviert den Replay-Pfad für den laufenden Prozess.

Wiederverwendung verlangt frische Geometry-Lock-Revision, unveränderte Mesh-/Indexquellen, Matrix, Kamera, Materialien, Raster-/Texturgeneration, GE-State-Shadow, Live-Cache-Slots und Ring-Alter. Sie prüft den Guard-Band erneut, markiert die verwendeten Mesh-Cache-Slots als benutzt und nutzt die normale Listen-Reserve. Die Tabelle wird beim Movie-Drain invalidiert. RTT, Schatten-Shear, aktive Fusion-Capture und Diagnosepfade bleiben beim vorhandenen Draw. Der Kandidat legt keine zusätzlichen GPU-Payloads an. World-Direct-Buckets und das Umgehen der Retail-Callbacks wären ein eigener Schritt.


Ein früher, feldübergreifender Emulatorlauf traf außerhalb von Junes auf eine abweichende GE-Befehlsfolge. Der Vergleich schaltete den Replay-Pfad dabei wie vorgesehen ab. Der ausgelieferte Runtime-Stand begrenzt Replay auf Feld 9, Raum 4 mit vorhandenem Resource-Manager und ohne ausstehenden Raumwechsel. Der Feld-Guard bestand einen gezielten Host-Test.

Der native PSP-Link und die strikte QA07-Prüfung bestanden. PPSSPP 1.20.4 erreichte Schule und Junes in den Kontroll-, Replay- und Vergleichsläufen. Im ausgewiesenen Junes-Fenster meldete Replay **16.875 Wiederholungen**, 240 Stichproben und 0 Abweichungen. Der separate Vergleichslauf meldete bei regulärer Ausgabe **17.115 Vergleiche ohne Abweichung**. Die Junes-A/B-Aufnahmen zeigten keine erkennbare fehlende statische Geometrie. Ein OpenGL-Negativtest mit aktivem Marker erreichte die Apsaras-Vorschau mit 0 Replay-Aufzeichnungen und 0 Replays. In den finalen Smokes gab es keinen GE-Stop. Castle, Dungeon-Nebel/Minimap und Kampf wurden mit dem finalen Build nicht visuell abgenommen.

Runtime `e429c8dc4d101b5c9e6ceef00f10d03dd42720a9`. Lokaler Kandidat `C:/P4Velocity01/output/P4P-VELOCITY01-JUNES-REPLAY`. EBOOT-SHA-256 `51dada9fc642db7c83d414cff6fb64db9005f60ce8ded3437420c11299bab2b8`. Der Replay-Gewinn auf PSP Go steht aus. Für die erste Gerätemessung denselben Build mit und ohne `VELOCITY_PERSIST` vergleichen und die Replay-/Diff-Zähler zusammen mit `PERFHW1` sichern.


## PSP-Go-A/B · zusätzlicher Effekt von Persistent Replay

`flat-only.log` (2.696 Zeilen) und `flat-replay.log` (2.694 Zeilen) vollständig ausgewertet. Je ein Start derselben Build-ID `P4P-VELOCITY01-PERSIST` mit Scoped-Glyphen und Static Flat. Beide Logs bestätigen Junes West `F009_004` und ein Messfenster.

| Modus | Akzeptiert / abgewiesen | p50 | p95 | Max |
| --- | ---: | ---: | ---: | ---: |
| Static Flat | 225 / 0 | 66,733 ms | 66,751 ms | 66,770 ms |
| Static Flat + Replay | 225 / 0 | 66,733 ms | 66,753 ms | 83,417 ms |

Beide `PERFHW1 status=OK`, ca. 14,99 FPS. Replay erzielte keinen belegten zusätzlichen FPS-Gewinn. Das Maximum des Replay-Laufs liegt etwa einen VBlank höher. In vier stationären Berichtsfenstern 22.496–22.749 Replays, 6.123–6.201 Records, 48–62 Backoffs, 356–364 Prüfungen und 0 Differenzen je Fenster. Der Render-Workload ist nicht exakt gepaart. Stationär lagen die Flat-Zähler bei 31.490–32.058 gegen 38.562–39.098 Draws pro 256 Frames. Die Skin-Batches überlappen mit ungefähr 18.500–19.500. Die stationären Logs zeigen keine Cache-Misses, keine Texture-Waits und keine Flip-Fehler. Ein Texture-Wait liegt im Replay-Übergangsfenster. Keine GE-Stop-, Watchdog- oder Crash-Meldung. Static Flat bleibt bevorzugt, Indexed ist Rückweg, Replay bleibt optional auf Junes begrenzt.

## Junes-Profil · nächster Kandidat

Der Pipelineschritt wartet auf die GE-Fertigmeldung und danach auf VBlank, bevor der Flip veröffentlicht wird. Die 66,733 ms entsprechen vier 16,683-ms-Stufen. Drei VBlanks lägen bei etwa 50,05 ms. Eine kürzere CPU-Aufbau- oder GE-Restzeit kann innerhalb derselben Stufe ohne Änderung der akzeptierten Present-Intervalle bleiben. Die Hardwarelogs enthalten Arbeitszähler und keine getrennten CPU-/GE-Dauern. In diesen Fenstern wurden etwa 256 von ungefähr 512 Ticks ohne Draw ausgeführt. Statische Draws und Skin-Batches bleiben häufig, Cache-Eviction und Texture-Waits sind im stationären Ausschnitt keine belegten Hauptkosten. Die separaten Runtime-Tripletts für das vorhandene `P4_FRAME_TIMING` protokollieren `hwframe cpu`, `wait`, `ge_tail` und `post`. `cpu` enthält die Zeit seit dem vorherigen Renderpfadende, inklusive Spielarbeit. `ge_tail` ist die verbleibende GE-Zeit am Synchronisationspunkt. `wait` umfasst auch die Display-Bestätigung, daher ist `wait - ge_tail` kein isolierter VBlank-Zähler. Diese Diagnostik in beiden Modi mit denselben Assets, Kamera und Taktung vergleichen.

| Kandidat | Tatsächlich betroffener Pfad | Bewertung |
| --- | --- | --- |
| World Direct | Feld-Buckets und RenderWare-Atomic-Callbacks vor dem lokalen Mesh-CALL. Upstream meldet in Junes 52,8 → 44,4 ms CPU. | Gutes CPU-Potenzial. Die lokalen Replay-Aufnahmen bilden nur vorbereitete Mesh-CALLs ab. Ein kompletter Bypass benötigt Feld-Buckets, Validierung von Material, Kamera und Guard sowie eine eigene Lebensdauerstrategie. |
| Skin Weight Groups | Lokaler gepackter Skin-Stream übergibt pro Batch eine feste GE-Weight-Count. Nicht verwendete Slots belegen Gewichtslanes. | Gewählte Umsetzung mit enger Wirkung auf GE-Daten, wiederverwendeten CALLs und bestehenden Draw-Pässen. Die alten Junes-Dumps zeigten viele dünn belegte Gewichtslanes, aber eine reine Kürzung des Batch-Endes hätte dort nicht gegriffen. |
| Collision Culling | Feldstrahlen und Kugelsweeps beim Laufen. Upstream Heaven-Brücke 4,0 → 0,7 Mio. Kollisionszyklen. | Priorität für einen bestätigten bewegungsgebundenen CPU-Engpass. Vor jedem Ausschluss müssen Bounds und Retail-Trefferfolge im Compare-Modus geprüft werden. |

[Upstream-Profiling und Changelog](https://github.com/dreikk91/persona4-psp/blob/b493/CHANGELOG.md)

## Skin Weight Groups · Junes-Paket

`VELOCITY_SKIN_GROUPS` aktiviert den Versuch nur in Junes West. Ein gepackter Batch kann einen oder zwei indexierte Streams mit kleinerer Weight-Count nutzen. Reihenfolge der Nichtnullgewichte pro Vertex, Quantisierung, Vertexattribute, Dreieckswinding und Reihenfolge der Alpha-/Depth-Pässe bleiben erhalten. Der Originalbatch bleibt für CPU-/Guard-Fallback, andere Szenen, ungünstige Matrix-Uploads und Platzmangel erhalten. Die zusätzlichen Vertex-/Index-Payloads teilen den GE-Retirement-Pfad des Skin-Owners. Der Gruppenspeicher ist auf 512 KiB für diese Payloads begrenzt. Mesh- und Persist-Modus separat wählbar.


Finale Quelle `0c8ee3364f422752457ca88c7a4562b18c3920ed`. PSP-Compilation und nativer Link für reguläres und Timing-Trio bestanden. Strikte QA07 für die reguläre Variante bestanden. Timing-Artefakte bestanden die QA07-Kohärenzprüfung im Legacy-Modus. Gezielt hostgeprüft sind Split, Strip-Parität und degenerierte Indizes, unveränderte Gewichtsbytes und Attribute, Speicherbuchung und Matrix-Fallback.

PPSSPP 1.20.4 erreichte Junes mit beiden Markerzuständen. Im ausgewiesenen 256-Frame-Fenster nutzten **125 von 17.131 Skin-Batches** die Gruppen. Geschätzt 14.500 Gewichtsslots weniger, 4.874 zusätzliche Payload-Bytes, 440 Matrix-Fallbacks. Beide Modi meldeten in diesem Fenster 74.439 Bone-Uploads. Die konservative Auswahl erreicht nur einen kleinen Teil der beobachteten Batches. In den nicht phasensynchronen Junes-Aufnahmen war keine sichtbare fehlende statische Geometrie oder deformierte Figur erkennbar. Der OpenGL-Gegenlauf erreichte die Apsaras-Vorschau und meldete dort 0 gruppierte Batches. Die finale Timing-Variante schrieb `hwframe` im Emulator. Die Diagnose bestätigte emulierte 33,367-ms-Fenster bei variabler Aufbau-/Wartezeit. Diese Werte sind keine getrennten PSP-Go-Kosten. Castle, Nebel, Minimap und Battle bleiben gezielte Gerätekontrollen.

Reguläres Trio `C:/P4Velocity01/output/P4P-VELOCITY01-SKIN-GROUPS`. EBOOT SHA-256 `c2be259e99846873c58a34e61c33a88ecbeee86b6231b1d5111326bd33fd33d9`. Timing-Trio im Unterordner `Timing`, EBOOT SHA-256 `cdf123705304d678ea983ba3fc7dbe6842d6ca74cc01558e3ebc814e928faafe`. Hardwaremessung steht aus.


## 24.11.2026 · PSP-Go-A/B Skin Weight Groups

`skin-off.log` (2.494 Zeilen) und `skin-on.log` (3.148 Zeilen) bestätigen je einen Start von `P4P-VELOCITY01-SKIN-GROUPS`, Scoped-Glyphen, Static Flat und deaktiviertes Persistent Replay. Die Junes-West-Initialisierung und genau ein `PERFHW1 status=OK` sind pro Datei belegt.

| Modus | Akzeptiert / abgewiesen | p50 | p95 | Max |
| --- | ---: | ---: | ---: | ---: |
| Skin OFF | 222 / 0 | 66,734 ms | 66,751 ms | 100,123 ms |
| Skin ON | 225 / 0 | 66,733 ms | 66,751 ms | 66,779 ms |

Beide Mediane entsprechen rund 14,99 FPS. Skin ON ist aktiv und meldete im stationären Beispielfenster 76 von 18.410 gruppierte Skin-Batches, 8.816 geschätzt ersparte Gewichtsslots und 512 Matrix-Fallbacks bei 4.874 zusätzlichen Payload-Bytes. Das andere stationäre ON-Fenster meldete 119 Gruppen und 13.804 ersparte Slots. Vergleichbare Zähler belegen keine identische Arbeitslast. Im ausgewiesenen stationären Fenster waren es 18.818 Skin-Batches und 31.651 Flat-Draws bei OFF, gegenüber 18.410 Skin-Batches und 31.401 Flat-Draws bei ON. Stationär keine Mesh-Cache-Misses, Texture-Waits oder Flip-Fehler. Die höheren OFF-Maxima und unterschiedlichen Zähler erlauben keine verlässliche Aussage zu Frametime-Spitzen. Kein zusätzlicher FPS-Gewinn belegt. Static Flat bleibt der wichtigste belegte Gewinn. Skin Groups und Persistent Replay bleiben optional, Indexed bleibt der Rückweg.

## Timing und nächster größerer Pfad

Die Hardware-A/B-Logs enthalten Accepted-Present-Werte und Arbeitszähler, keine getrennten CPU-/GE-Zeiten. Junes blieb in beiden Armen bei rund vier VBlanks. Das bereits vorhandene Timing-Trio ist die nächste Messung, ohne neuen Renderer-Link. Im PPSSPP-Junes-Timing blieben mehrere Fenster bei 33,367 ms p50, während sich Aufbau und Wartezeit verschoben. Das zeigt die Quantisierung im Emulator und misst keine PSP-Go-Phasen. [Exakte PSP-Go-Timing-Anleitung](VELOCITY01_TIMING_PSP_GO.md)

World Direct bleibt der aussichtsreichere größere **CPU**-Kandidat, falls das Timing in Junes einen relevanten Aufbauanteil zeigt. Upstreams Junes-Diagnose meldet 52,8 → 44,4 ms CPU. Der lokale Persistent-Replay-Pfad hatte viele Hits, lässt die Retail-Callbacks aber bestehen. Sein unveränderter Present-Median entscheidet die Callback-Frage nicht. Bei dominanter GE-Restzeit hat eine gezielte GE-Draw-Aufteilung Vorrang. Collision Culling lohnt als Bewegungsprüfung, wenn die stationären und bewegten CPU-Werte auseinanderlaufen. Der aktuelle Skin-Groups-Anteil ist zu klein, um weitere Tests ohne konkreten Hinweis zu priorisieren. [Upstream b493 mit Messkontext](https://github.com/dreikk91/persona4-psp/blob/b493/CHANGELOG.md)

## Stabiler PPSSPP-Testpfad für VELOCITY01

`C:/P4Velocity01/emulator/PPSSPPWindows64.exe` ist der feste Testpfad. Die verwendete PPSSPP 1.20.4 stimmt per SHA-256 `27d3edbb06dc623dab60877d3e3a975aef4034c7d4fac464e92fd4998864d2ec` mit den vorhandenen b259- und RESONANCE01-Labor-Binaries überein. Das Hilfsskript `tools/ppsspp_launch.ps1` wählt den jeweiligen Test-Memstick über die PPSSPP-Windows-Datei `installed.txt`. Einer gemeinsamen EXE ist jeweils ein aktiver Test zugeordnet. Die elf lokalen `run-*.ps1` und `start-smoke.ps1` nutzen den gemeinsamen Startpfad. Vorhandene Installationen und Testverzeichnisse mit ihren EXE-Kopien, Saves, Konfigurationen und Aufnahmen wurden erhalten.

Zwei frische Timing-Smokes mit Ports 37931 und 37932 starteten dieselbe Binärdatei und erzeugten jeweils `p4-timing.log` mit `hwframe` im zugeordneten separaten Test-Memstick. Ein gleichzeitiger Start über den gemeinsamen Launcher wurde abgewiesen. Alle zwölf lokalen Launcher und das gemeinsame Skript wurden als PowerShell geparst. Die Firewall-Regeln wurden unverändert gelassen. Windows kann den stabilen Pfad beim ersten Netzwerkstart einmal abfragen. Neue Testverzeichnisse erzeugen keine neuen EXE-Pfade. [PPSSPP 1.20.4 für Windows](https://github.com/hrydgard/ppsspp/blob/v1.20.4/Windows/main.cpp)


## PSP-Go-Timing · Junes West F009_004

Die bereitgestellten Logs wurden vollständig ausgewertet. `timing-flat.log` enthält 2.820, `timing-replay.log` 2.737 Zeilen. Sie bestätigen dieselbe Timing-Build-ID, Static Flat, Skin Groups aus, je einen Junes-Start und das jeweilige Replay-Flag. Beide `PERFHW1`-Ergebnisse sind OK mit 225 akzeptierten und 0 abgewiesenen Presents.

| Modus | PERFHW1 p50 / p95 / Max | Aufbauzeit | Wait | GE-Rest | Post |
| --- | --- | ---: | ---: | ---: | ---: |
| Static Flat | 66,733 / 66,748 / 66,795 ms | 56,343 ms | 10,335 ms | 3,612 ms | 0,053 ms |
| Static Flat + Replay | 66,733 / 66,746 / 66,761 ms | 54,798 ms | 11,880 ms | 4,354 ms | 0,054 ms |

Aufbauzeit, Wait, GE-Rest und Post sind Mittelwerte der je neun `hwframe`-Blöcke mit je 32 Frames zwischen `armed` und `status=OK`. Sie enthalten Render-Frames aus dem Vorlauf und sind nicht das identische Sample der Accepted-Present-Intervalle. `cpu` ist verstrichene Aufbauzeit inklusive Spielarbeit. `ge_tail` steckt bereits in `wait`. Die Werte isolieren weder CPU-Kernzyklen noch die gesamte, teilweise überlappende GE-Arbeit.

Die rund 1,545 ms Differenz bei der Aufbauzeit liegt innerhalb derselben VBlank-Stufe und ist wegen der unterschiedlichen Arbeitslast kein isolierter Replay-Gewinn. Stationäre Flat-Zähler 31.587–31.689 gegen 36.018–36.588 pro 256 Frames. Das Replay-Beispielfenster enthält 23.327 Replays, 4.989 Records, 44 Backoffs und 367 Vergleiche ohne Differenz. Beide Modi blieben bei rund 14,99 FPS. Einige Render-Aufbauspitzen lagen über 70 ms. Beim Laden trat im Flat-Lauf ein deutlich größerer Aufbaupeak mit Raster-Writeback auf. Das ist vom stationären Feld zu trennen. Die stationären Cacheberichte enthalten keine Slot-Misses, Texture-Waits oder Flip-Fehler. Die Logs enthalten keine GE-Stop- oder Watchdog-Meldung.

## CPU-Transplant · GE-CALL-Zustandsdaten

Upstream b493 läuft für World Direct über Feld-Buckets und kann normale Atomic-Callbacks bei bekannten vorbereiteten Listen umgehen. Die Gültigkeit hängt unter anderem an Bucket-/Atomic-Lebensdauer, Matrix und Kamera, Materialänderungen, Rastergenerationen, Guard-Band und Draw-Reihenfolge. Unsere Static-Flat-Payloads gehören einzelnen Mesh-Cache-Slots. Die optionalen Whole-Draw-Replays sind an CALL-Ring und Mesh-Slots gebunden. Aus diesen allein lässt sich noch kein gültiger direkter Ersatz für einen Feld-Bucket ableiten. [World-Direct-Quellpfad](https://github.com/dreikk91/persona4-psp/blob/b493/port/world_direct.h)

Der praktische Teiltransplant übernimmt die Wiederverwendung geprüfter Zustandsdaten aus dem Umfeld von `wd_pass_keys`. Im lokalen stationären Flat-Lauf mit deaktiviertem Replay kamen rund 33.000 Mesh-CALL-Treffer pro 256 Frames zustande. Der bestehende Lookup hashte dafür bei jedem Aufruf die GE-State-Shadow-Wörter neben den Material-/Mesh-Eingaben erneut. Der Kandidat vergleicht die bisherigen GS-Bereiche bytegenau und weist identischen Zuständen eine CPU-seitige ID zu. Material-, Raster-, Generations- und Mesh-Eingaben werden weiterhin gehasht. Der Ring-Eintrag vergleicht die Zustands-ID separat. IDs bleiben stabil. Ist ein Bucket voll, greift für weitere Zustände der ursprüngliche Key. Gleiches gilt bei Allokationsfehler oder erschöpften IDs.

Aktivierung nur per `VELOCITY_CALL_STATE` in Junes West. RTT, Shadow-Shear, Fusion-Capture, Skin-CALLs und Nicht-Junes behalten den bisherigen Key. Der Pfad übernimmt keine neuen GPU-Puffer und verändert keine Atomic-Callbacks, Feld-Buckets, Guard- oder Draw-Reihenfolge. Static Flat bleibt Standard, Indexed, Replay und Skin Groups bleiben separat wählbar.


Der erste Emulatorversuch mit Verdrängung bestätigte weniger Hasharbeit, erzeugte aber im betrachteten Junes-Fenster 11.541 Mesh-CALL-Neuaufzeichnungen. Der finale Kandidat verwendet feste IDs ohne Verdrängung. Im Software-A/B am festen PPSSPP-1.20.4-Pfad erreichten beide Modi Schule und Junes. Das vergleichbare Junes-Fenster meldete in beiden Armen je 27.817 Flat-Draws, 28.878 Mesh-CALL-Treffer und 352 Mesh-CALL-Aufzeichnungen. Memo meldete 23.260 Zustands-Hits, 52 neue IDs, 5.210 Legacy-Fallbacks und 839.232 GS-Wörter, die nicht erneut gehasht wurden. Der Emulatorzähler misst vermiedene Hasharbeit. Der Nettoeffekt einschließlich der Bytevergleiche ist auf PSP Go ungemessen. In den nicht animsynchronen Junes-Bildern war keine fehlende statische Geometrie erkennbar. Die Läufe meldeten keinen Renderer-Stop.

Native PSP-Compilation und Link sowie strikte QA07 für das reguläre Trio bestanden. Gezielt hostgeprüft sind genaue Zustandsbytes, Kameraausschluss wie im bisherigen Key, volle Buckets, erschöpfte IDs und die Trennung der Legacy-/Memo-Keys. Die separate Timing-Diagnose passt mit `-Os` für den Port unter das feste Datenlayout. Sie bestand Native Link und QA07-Kohärenzprüfung im Legacy-Modus und schrieb beim Boot-Smoke `hwframe`. Reguläre `-O2`-FPS-A/B und `-Os`-Timing-A/B sind getrennte Vergleiche.

Runtime-Commit `e0efa3fc16d0e3fe0edd00674f4afb29c4684a23`. Vollständiges lokales Installationspaket mit 74 geprüften Dateien unter `C:/P4Velocity01/output/P4P-VELOCITY01-CALL-STATE`. EBOOT SHA-256 `144eca76d210b0660f2d5b0d768a1eebcbcb21f72ec29e7364217e1f91936ef3`. Separates Timing-Trio unter `Timing`, EBOOT SHA-256 `51883751d705887dfe0890cbe29b7a7e52ef25f5750b22faed3012ef71d5e785`. Beide Geräte-Arme zuerst stationär mit identischer Kamera, Flat aktiv und Replay sowie Skin Groups aus vergleichen. Noch kein FPS-Nachweis für Memo.

## CALL STATE · PSP Go, Junes West F009_004

Die beiden vollständigen Logs stammen von je einem Kaltstart des CALL-STATE-Builds. Beide melden Static Flat und Scoped-Glyphen. Persistent Replay und Skin Groups waren aus. Die 15-Sekunden-Messung nahm je 225 Presents an und verwarf keinen.

| Markermodus | Logzeilen | PERFHW1 | p50 | p95 | Maximum | FPS aus p50 |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| CALL STATE OFF | 2714 | OK, n=225, r=0 | 66,734 ms | 66,753 ms | 66,769 ms | 14,99 |
| CALL STATE ON | 2978 | OK, n=225, r=0 | 66,733 ms | 66,747 ms | 66,756 ms | 14,99 |

Im letzten 256-Frame-Fenster hatte CALL STATE ON 29.619 Treffer, 3.068 Fallbacks, null neue IDs und 1.066.284 übersprungene Wörter im GS-Hash. Die letzten OFF/ON-Fenster meldeten 32.058/31.509 Flat-Draws, 19.466/18.533 GE-Skin-Batches und 55.416/54.888 gerenderte Atomics. Beide meldeten 63.232 Frustumtests und rund 93 unmittelbar aufeinanderfolgende Instanz-Replays je Renderframe. Es gab in diesen Fenstern null Mesh-Slot-Misses, null Texture-Waits und keine Flip-Fehler. In den vollständigen Logs fand sich kein GE-Stop.

Beide Arme blieben bei vier VBlanks pro Present. Die vorhandenen Logs enthalten keine getrennte CPU- oder GE-Phasenzeit. Die leicht unterschiedliche Renderlast und die VBlank-Quantisierung begrenzen die Aussage der Mikrosekundenunterschiede im Present-Intervall. Die übersprungenen Wörter zählen Hash-Arbeit. Einen isolierten CPU-Zeitgewinn oder einen zusätzlichen FPS-Gewinn belegt das Paar nicht. Static Flat bleibt der einzige auf PSP Go gemessene FPS-Fortschritt und bleibt standardmäßig aktiv. CALL STATE, Persistent Replay, Skin Groups und der Indexed-Fallback bleiben per Marker erreichbar.

## World Direct · erster Schritt im b262-Feldrenderer

Die bisherigen PSP-Go-Timingfenster erfassten rund 55–56 ms verstrichene CPU-seitige Arbeit pro Frame einschließlich Spielarbeit. Sie weisen einzelnen Feldfunktionen keine Zeit zu. Die CALL-STATE-Fenster zeigen einen hohen Hot-Path-Anteil mit rund 215 gerenderten Atomics, 247 Frustumtests sowie rund 129–131 Mesh-CALL-Treffern pro Renderframe. Die 93 bestehenden Instanz-Replays pro Frame lokalisieren eine wiederholte Arbeit konkret. Der bestehende b262-Pfad prüft in diesem Fall trotzdem das Atomic, verarbeitet Beleuchtung und Skinning-Eignung und betritt die generische Mesh-Dispatch-Funktion, ehe er die fertigen GE-Befehle kopiert.

b493 ersetzt die drei Materialpässe durch eigene Bucket-Schleifen. Der direkte Pfad behält vorbereitete Befehle, Sichtbarkeitsdaten, Matrizen und Guard-Informationen für Bucket-Einträge über Frames hinweg. Seine Prüfung besichtigt aktuelle Nodes und Atomics, Zustand und Kamera, Material und Rasteränderungen sowie die Generation der vorbereiteten Listen. Fade- und Sonderpfade behalten die Referenz. b493 meldet upstream 52,8 auf 44,4 ms im profilierten Junes-Lauf. Unsere b262-Mesh-Slots besitzen dagegen die Static-Flat-Payload und der GE-CALL-Ring besitzt die pro Mesh aufgezeichneten Befehle. Ein über Frames gespeicherter Bucket-Eintrag hätte ohne zusätzliche Lebensdaueranbindung keine belastbare Garantie für Atomic, Raster, Listenreferenz oder Slot. [b493 World Direct](https://github.com/dreikk91/persona4-psp/blob/b493/port/world_direct.h)

Runtime `141543e96cd7faec75f79a46421baddd680c0a89` bindet den ersten Schritt an die Retail-Pässe 1 und 3 in geladenem Junes West. Der Retail-Code liest die jeweils gültigen Buckets, prüft Frustum und Fade-Sentinels und setzt Alpha, Blend und Depth an den bisherigen Stellen. Der normale Atomic-Callback kann die bereits vorbereitete, unmittelbar benachbarte Instanz im selben Frame direkt über die vorhandene Wiedergabefunktion ausgeben. Sie prüft weiterhin den GE-Listencursor, Geometrie-Eingaben, Material, Kamera, Licht, Rastergeneration und Guard. Die frühe Auswahl verlangt eine lebende ungesperrte statische Geometrie und fällt bei Skin, Partikeln, RTT, Schatten, Probes und ungewöhnlichen Beleuchtungskonfigurationen in den Standardpfad zurück. Der Pfad sammelt keine Bucket- oder Atomic-Referenzen über Frames, erstellt keine GE-Liste und übernimmt keine Ressourcen. Traversal, Weltkugel und Sichttest bleiben in dieser Stufe im Retail-Pass. Der eigentliche direkte Treffer überspringt den generischen Atomic- und Mesh-Aufbau.

Mit `VELOCITY_WORLD_DIRECT` läuft jeder 64. Frame stichprobenweise im Standardpfad. `VELOCITY_WORLD_CHECK` prüft jeden vorhergesagten Treffer über den Standardpfad. Die Prüfung verlangt, dass die Referenz dieselbe Wiedergabefunktion wählt. Bei Abweichung schaltet die Sitzung die Direktwiedergabe ab. `VELOCITY_WORLD_PROFILE` misst zusätzlich die verstrichene Zeit der Retail-Pässe 1 und 3 in beiden Armen, einschließlich etwaiger Warteanteile. Die Passzeit ist weder ein isolierter Hardware-CPU-Counter noch ein zusätzlicher Teil der `PERFHW1`-Framezeit. Die Host-Auswertung muss die 256-Frame-Fenster nach tatsächlichen Draw- und Pass-Zahlen paaren.

### Build und Verifikation

- PSP-GCC 15.2.0, regulärer Release mit `-O2`, 825 gestagte Link-Eingaben einschließlich der 821 Spielobjekte. Native Link-Prüfung meldete 19 gültige PSP-Importbibliotheken und null Funktionszeiger ohne natives Ziel. Disassembly von `sdkOtFlushBuckets` bestätigt die Weiterleitung von Pass 1 und 3 über die neuen Wrapper und den direkten Aufruf von Pass 2. Striktes QA07 bestätigte Manifest, Sources, Spielobjekte und PBP-Einbettung.
- Der gezielte Host-Test mit extrahiertem World-Direct-Callback prüfte Treffer, abgelaufenen Frame, geänderten Listencursor, Geometry-Lock, Skin, Partikel, RTT, Schatten, Fusion, ausgeschlossene Probes und die automatische Rückschaltung bei erzwungener Referenzabweichung. Neun PRX-/Pointer-Tests und acht gezielte C-Regressionstests zu Frame-Timing, Raster, GE-CALLs und Renderzustand bestanden. Zwei ältere GE-CALL-Fixtures benötigten für den Host-Lauf die Extraktion ihrer bereits vorhandenen Hilfsfunktionen. Diese Anpassung erfolgte nur im Test-Runner.
- PPSSPP 1.20.4 am festen Pfad `C:/P4Velocity01/emulator/PPSSPPWindows64.exe`. Im vergleichbaren 256-Frame-Fenster meldeten ON, OFF und CHECK jeweils 48.273 Atomics, 50.150 Sichttests, 27.913 Flat-Draws, 28.975 Mesh-CALL-Treffer und 350 Records. ON erreichte 19.995 direkte Wiedergaben und 279 Stichproben. CHECK bestätigte alle 20.274 Vorhersagen über den Referenzpfad. Die verglichenen Fenster meldeten null Abweichungen. Ein OpenGL-Neustart brach im Windows-NVIDIA-Treiber ab. Die anschließenden Software-Smokes liefen bis Junes und in die Schule. Die Emulatorlaufzeiten sind keine PSP-Go-Messdaten.
- Vollständiges A/B-Paket `C:/P4Velocity01/output/P4P-VELOCITY01-WORLD-DIRECT`. 74 Dateien mit 5.746.501.739 Bytes gegen die vorherigen Ressourcen-/Save-Hashes und das native Manifest geprüft. EBOOT-SHA-256 `7ff01ab3629e829347ba7815b4d1a4b4df5ef467ecd5310707da14f7bf61bea8`. Die Installation und das Hardware-Messpaar stehen in `INSTALL.md`.

## World Direct · Bucket-Timing auf echter PSP Go

Beide vollständigen Kaltstart-Logs aus Junes West F009_004 wurden ausgewertet (OFF 2.620 Zeilen, ON 2.618). Beide melden Scoped, Static Flat, Bucket-Timing aktiv und Persistent Replay, CALL STATE und Skin Groups aus. `PERFHW1` meldet jeweils OK, n=225 und r=0. OFF lieferte p50/p95/max 66,733/66,750/66,758 ms, ON 66,733/66,738/66,753 ms. Beide lagen bei rund 14,99 FPS beziehungsweise vier VBlanks. Die 256-Frame-Berichte umfassen eigene Renderfenster rund um die Present-Messung.

| Fenster bis Frame | Passaufrufe je Arm | OFF pass_us | ON pass_us | Differenz je Renderframe | Flat-Draws OFF / ON |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1024 | 2048 | 5.659.192 | 5.133.480 | −2,054 ms | 31.448 / 31.449 |
| 1280 | 2048 | 5.594.863 | 5.109.934 | −1,894 ms | 32.060 / 31.846 |
| 1536 | 2048 | 5.595.988 | 5.064.803 | −2,075 ms | 31.694 / 32.057 |
| Mittel | 2048 | 21,940 ms/Frame | 19,933 ms/Frame | −2,008 ms (−9,15 %) | |

ON meldet 23.436 direkte Treffer in jedem Fenster, bei 23.808/23.804/23.808 Versuchen und 372/368/372 Checks ohne Abweichung. Beide Läufe hatten je Fenster 63.232 Frustumtests. Die Zahl der gerenderten Atomics, Skin-Batches und verworfenen Frustumtests variierte leicht. Es gab stationär keine Mesh-Slot-Misses oder Flip-Fehler und im ersten Fenster 1/2 Texture-Waits OFF/ON. Die beiden vollständigen Logs zeigen keinen GE-Stop. Die gemessene Einsparung betrifft die verstrichene Zeit der Retail-Pässe 1 und 3 einschließlich möglicher Warteanteile. Das erste Fenster bietet die engste Übereinstimmung der Flat-Draw-Zahlen. Die Hardwaremessung isoliert weder die CPU-Zeit noch einen Gewinn bei der Present-Stufe.

## World Lists · zweiter World-Direct-Schritt

Der Kandidat erweitert die bestehenden Junes-Wrapper um die b493-Bucket-Dispatch-Idee. Pass 1 und 3 laufen in ihrer Retail-Reihenfolge über die **live** eingereihten Listen. Fade auf Bucket 5, Vertex-Alpha, Blend, Depth-Übergänge, Frustumtests und Sondercallbacks behalten ihre Bedingungen. Pass 2 bleibt Retail. Der Dispatch bevorzugt die bereits auf PSP Go gemessene frameinterne Nachbarwiedergabe; danach kommen frameübergreifende vorbereitete ganze Atomic-Draws für gewöhnliche Buckets infrage. Treffer ersetzen den Atomic-Callback und die erneute Aufbereitung der Meshes und Materialbefehle durch validierte BASE+CALL-Paare. Kamera-/Modellsetup und Guard verwenden den gemeinsamen Mesh-Pfad. Bei exakt vorhandenem GS-Setup, passender Depth-Bias-Einstellung und inaktiven Fog-/Scissor-Sonderzuständen genügt die Modellmatrix. Im Prüfmodus wird dafür die Länge des regulären Setup-Abschnitts kontrolliert. Bei Veränderungen läuft der normale Callback am gleichen Punkt der Reihenfolge.

Die Liste ist an Bucket-Eigentümer, Atomic und Epoche gebunden. Die Lebensdauerprüfung deckt Junes-Feldzustand, Resource-Manager, Battle-Task, ausgebliebene Bucket-Pässe und die Freigabe des Bucket-Satzes ab. Der Native-Hook invalidiert vor `hMaestroFreeListSet`. `FldResource_Destroy` gibt diese Sätze vor den zugehörigen Clumps frei. Ein Treffer vergleicht live Geometry/Lock-Memo-Revision, Mesh-Layout, Kamera, Modell, Material- und Rastergeneration, Guard, GE-State und Slot-Stempel. Der Renderer prüft das Alter des CALL-Rings, markiert alle referenzierten Mesh-Slots als benutzt und belässt die physische Freigabe in der vorhandenen GE-Retirement-Logik. Im Bericht landen ausschließlich Zähler. Wie im b488-Fix wird beim Reporting kein früher gespeichertes Geometrieobjekt dereferenziert. [b493 World Direct](https://github.com/dreikk91/persona4-psp/blob/b493/port/world_direct.h)

`VELOCITY_WORLD_LISTS` schaltet die Listen zusätzlich zur frameinternen World-Direct-Variante ein. `VELOCITY_WORLD_CHECK` vergleicht vorhergesagte Listen über den Referenzcallback und deaktiviert bei einer Abweichung die Listen. Der Betriebsmodus nimmt diese Stichprobe alle 64 Frames. Indexed bleibt als Rückschaltung erhalten. `VELOCITY_WORLD_STACK` aktiviert zusätzlich Persistent Replay, CALL STATE und Skin Groups. Neben dem bestehenden Instance-Replay haben die Listen Vorrang an berechtigten Feldpositionen; die normale Persistenz kann andere Pfade bedienen. Beide verwenden denselben begrenzten CPU-Tabellenspeicher mit getrennten Eigentümerschlüsseln. CALL STATE greift am Mesh-CALL-Aufbau und wird durch einen Listentreffer umgangen. Skin Groups greifen im separaten Skin-Pfad. Die Wirkung des Gesamtmodus muss separat gemessen werden.

### Finale Verifikation

- Regulärer `-O2`-Build mit PSP-GCC 15.2.0, 19 gültigen PSP-Importbibliotheken und null Funktionszeigern ohne natives Ziel. Striktes QA07 bestanden. Im finalen ELF rufen die Retail-Pässe 1 und 3 die Wrapper auf. Beide Field-Resource-Freigabepfade rufen die Epochengrenze vor der Clump-Freigabe auf. Eine optionale 3D-Diagnosetabelle wurde für das feste PSP-Image auf Lazy-Heap-Allokation umgestellt. Das Diagnosewerkzeug liest beide Layouts.
- Gezielte Host-Fixtures bestanden für Bucket-/Atomic-Eigentümer, Invalidierung vor Freigabe, Feld- und Resource-Manager-Wechsel, Battle-Eintritt und Rückkehr, Renderlücken, Epochenüberlauf sowie Depth-, Fog-, Kamera-, Zustands- und Scissor-Fallback. Zusätzlich bestanden 9 Pointer-/Linktests und 8 gezielte C-Regressionstests. Zwei ältere GE-CALL-Fixtures wurden wie beim Vorgänger im Test-Runner um ihre fehlenden Hilfsfunktionen ergänzt.
- PPSSPP 1.20.4 am festen Pfad `C:/P4Velocity01/emulator/PPSSPPWindows64.exe`. Der finale B-Lauf erreichte bei 2.048 Passaufrufen 19.144 persistente Listentreffer, 3.220 Records, 300 Stichproben und null Diffs. Zusätzlich blieben 23.436 frameinterne Direktwiedergaben aktiv. CHECK meldete im entsprechenden Fenster 21.760 geprüfte Listen ohne Diff. Der konservative reine Modell-Setup-Pfad hatte im Junes-Smoke null Treffer. Kamera-/Depth-/Fog-Prüfungen und vorhandene Bindings blieben daher aktiv. Der Smoke belegt die Listenwiedergabe. Die Emulatorzeiten weisen keinen zusätzlichen PSP-Go-Zeitgewinn nach.
- Die Kurzroute mit aktivem Marker bis zum Klassenzimmer 2-2 zeigte keine World-Lists-Draws außerhalb von Junes. Kampfrückkehr wurde im Host-Fixture abgedeckt. Ein vollständiger echter Kampfrückkehr-Lauf bleibt Teil der Hardware-Funktionsprüfung.

- STACK war in einem vollständigen Junes-Fenster aktiv. 18.718 World-Lists-Treffer, 505 reguläre Persistent-Replays, 8.705 CALL-STATE-Treffer und 120 gruppierte Skin-Batches. 291 World-Lists- und acht reguläre Replay-Checks jeweils ohne Diff. Das belegt die Koexistenz der Pfade in diesem Fenster. Die Trefferzahl der World Lists sinkt gegenüber B. Die CPU-/GE-Arbeit der Schalter überschneidet sich teilweise und die Hardwarewirkung des Gesamtmodus bleibt offen.
- Paket mit 74 hashgeprüften Dateien unter `C:/P4Velocity01/output/P4P-VELOCITY01-WORLD-LISTS`. Runtime `8a6c8e87aad5b13325a9a9c0a57cada2507cd119`. EBOOT-SHA-256 `02f6fd67d5f3a82a96e74b4b5d2ac4daafa4faf3b6193ed99cdad3da7f16796e`. `INSTALL.md` beschreibt zwei notwendige Hardwareläufe und einen optionalen Kombinationslauf.

## Abschluss vor UI Alignment · PSP Go, Junes West F009_004

Vollständige Logs aus dem privaten UI-Handoff, Runtime `8a6c8e8`. Ausgewertet sind die Fenster mit Endframes 1280, 1536, 1792 und 2048, jeweils 256 Renderframes und 2048 Passaufrufe.

| Log / effektiver Modus | Zeilen | PERFHW1 p50 / p95 / max (ms) | Pass-Summen in µs | Passzeit pro Frame | Static-Flat-Draws |
| --- | ---: | --- | --- | ---: | --- |
| world-direct / Direct | 2780 | 66,733 / 66,738 / 66,759 | 4.675.551 / 4.707.606 / 5.017.890 / 5.068.084 | 19,013 ms | 32.088 / 31.874 / 31.358 / 32.076 |
| world-lists / Lists | 3667 | 66,734 / 66,742 / 83,407 | 5.170.719 / 5.143.002 / 5.033.743 / 5.143.928 | 20,011 ms | 31.937 / 32.075 / 31.316 / 32.030 |
| world-stack / Static Flat | 2767 | 66,733 / 66,743 / 66,757 | 5.028.003 / 4.929.291 / 4.986.486 / 4.954.569 | 19,432 ms | 31.870 / 31.390 / 32.074 / 31.765 |
| world-stack-v2 / Stack aktiv | 2805 | 66,733 / 66,748 / 66,759 | 5.241.648 / 5.262.246 / 4.935.277 / 4.968.877 | 19,930 ms | 32.076 / 31.534 / 31.786 / 32.078 |

Je `status=OK`, 225 akzeptierte und null verworfene Presents, rund 14,99 FPS. Die Zeile mit den **effektiven** Schaltern identifiziert `world-stack.log` als Static Flat und nur `world-stack-v2.log` als Stack. In den vollständigen Logs fand sich kein GE-Stop, Flip-Fehler oder automatischer Rückfall. Der Lists-Lauf protokollierte nach der Feldunterbrechung erneut Epochengrenzen und Listentreffer. `model_only` blieb in den verglichenen Fenstern bei null. Passzeiten können Wartezeiten einschließen, die Draw-Last variiert und das einzelne Maximum von Lists erlaubt keine belastbare Aussage über Spitzen. Der frühere isolierte Direct-Vergleich bleibt ein Passzeitbefund von rund 2 ms. Lists und Stack liefern hier keinen ausreichenden zusätzlichen Vorteil. Static Flat bleibt die einzige auf PSP Go nachgewiesene FPS-Verbesserung der Phase.

Das UI-Paket verwendet Static Flat ohne weitere VELOCITY01-Experimente als Standard. Alle bisherigen Schalter und Pfade sind vorhanden. `VELOCITY_SCOPED_TEX` aktiviert den experimentellen Scoped-Glyphenpfad, `VELOCITY_LEGACY_TEX` behält Vorrang. Der normale visuelle Hardwarelauf verwendet ausschließlich `FPS`.
