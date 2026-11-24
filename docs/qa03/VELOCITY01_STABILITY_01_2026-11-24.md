# VELOCITY01 STABILITY 01 Â· PSP-Go-Fehlermatrix

Privater Kandidat auf UI03-Runtime ce3663f76b65c0c6b0d3e52cf7e07dfc0309931b. Runtime 669a07eaa13cd5b26c5019454f36dfe091b0de60. EBOOT SHA-256 2f968a372e15ceebe153da464a0e170f85a57dabbb2540933d2fc954a61d24cb.

Static Flat bleibt Standard. Indexed bleibt rÃ¼ckschaltbar. Persistent Replay, CALL STATE, Skin Weight Groups, World Direct, World Lists und der Stack bleiben optional. Die 15-Sekunden-Logmessung und der FPS-Marker behalten den bisherigen AuslÃ¶sepfad.

## Hardwarebefunde aus UI03

Die QA-Matrix enthÃ¤lt 8 historisch behobene, 49 bestandene und 15 fehlgeschlagene Punkte sowie neue Beobachtungen. Der private Original-Log umfasst 68.344 Zeilen und sechs UI03-Starts. Neun bestÃ¤tigte Slot-Loads, keine explizite SchreibbestÃ¤tigung und kein Treffer fÃ¼r GE-Stopp oder Watchdog. Die frÃ¼here Hardwarebeobachtung zu Save/HOME ergÃ¤nzt das Log. Elf Audio-Statusmeldungen an VideoÃ¼bergÃ¤ngen zeigen null Underruns. Kumulierte FehlerstÃ¤nde 71, 37, 37 und 1 sind ohne Cuereferenz. Der Kandidat fÃ¼r den Schulweg ist f010_003 ab Zeile 25781 mit Event 460_001A_TEST in 26100â€“26172. Die Aufnahmen sind zeitlich nicht an den Log gebunden.

## P0 Â· G2 / Save / HOME

| Pfad | Codebefund und Status |
| --- | --- |
| Generische Raster und CLUT | Owning-RAM wird bereits an die offene oder versiegelte GE-Sequenz gebunden und erst bei DONE freigegeben. Eine RasterzerstÃ¶rung konnte die logische Im2D-Textur im State-Array belassen. Non-owning und RTT ohne eigene RAM-Allokation gingen zudem am Revisionswechsel vorbei. STABILITY rÃ¤umt den identischen State-Zeiger und invalidiert bei jeder RasterzerstÃ¶rung. Bereits eingereihte Befehle behalten ihre Allokation und arbeiten in Reihenfolge. Host-Regression geprÃ¼ft. |
| CPU-Mutation und Aliasse | Generische Pixel-/CLUT-Locks und TMX-DMA warten auf die versiegelte Liste. Gecachte Font-Slots verwenden Pins pro Bank. FÃ¼r eine mÃ¶gliche Ãœberlappung zwischen einem In-place-Write und einem frÃ¼heren Draw in **derselben offenen Liste** fehlt die echte Adress-/Sequenzprobe fÃ¼r generische Lock-/TMX-Pfade und geteilte Backings. P0 offen. |
| RTT und VRAM | Zielwechsel flushen den GE-Texturcache. Bei auÃŸerhalb einer offenen Liste gelÃ¶schtem Ziel lÃ¶schte die Wiederherstellung das Pending-Flag vor der PrÃ¼fung und verlor den Flush. Der nÃ¤chste Screen-Bind konsumiert das Flag jetzt selbst. Listenaufbau reserviert bereits Platz fÃ¼r den Flush. RAM-Invalidierung und VRAM-Command-Ordering sind getrennt. FIFO ordnet die bisherigen GE-Lese-/Schreibbefehle, und Slotvergabe erzeugt keine CPU-Schreiboperation. Draw/Sample/Recycle Ã¼ber die tatsÃ¤chlichen Field-/Battle-/Fusion-Routen bleibt auf Go zu prÃ¼fen. Host-Tests mit echten Bind-Anweisungen und PSP-Stubs bestanden. |
| Mesh, Skin, Rings und Banks | Standard aktives Static Flat liegt im Meshcache des geometrischen Besitzers und erbt dessen Retire-Gate. Normale Skin-Mesh-Ressourcen und gepinnte GE-Listen verwenden Fertigstellungs-/Bankgrenzen; Display-ACK und GE-DONE haben getrennte Rollen. Optional persistente CALLs oder Field-EigentÃ¼mer kommen im Standard nicht zur Anwendung. Keine globalen SynchronisationsÃ¤nderungen. Keine Hardwarefreigabe fÃ¼r reuse/eviction Ã¼ber Wechsel. |
| Video und Exit | Video wartet bis letzter GE-Frame bestÃ¤tigt ist und GU FINISH liefert, erst dann werden Mesh-/Skin-Caches verworfen und VRAM an den Video-Pfad abgegeben. RÃ¼ckkehr wÃ¤hlt die aktuell nicht dargestellte Bank. HOME sperrt die Card-Admission, wartet auf Abschluss eines aktiven Card-Kommandos und behandelt eine noch angehaltene GE-Queue. Save schreibt temporÃ¤r, synchronisiert und fÃ¼hrt Austausch mit Backup aus. Host-Tests fÃ¼r frischen Prozess, fehlgeschlagenen Austausch und Backup-Recovery bestanden. Saveâ†’HOMEâ†’kalter Reload und idle HOME auf Go bleiben Pflicht. |

**G2 P0_UNVERIFIED.** Kandidat fÃ¼r private QA. Ã–ffentliche Freigabe wartet auf die gezielten Go-PrÃ¼fungen fÃ¼r Ressourcenwechsel, mÃ¶gliche offene List-Aliasse, Save/HOME und die Schwere/Taktreproduzierbarkeit von T02/T21. Fehlende Crashmeldungen reichen dafÃ¼r allein nicht.

## Fehlerstatus

| ID | Status | Befund und nÃ¤chste PrÃ¼fung |
| --- | --- | --- |
| T01 Â· manuelle Fusion | Offen, Vanilla aktiv | Aktueller Compiler- und Linkpfad enthÃ¤lt Diagnose-Checkpoints fÃ¼r Preview/Confirm, keinen manuellen Inheritance-Modal oder Assetpatch. Vanilla nutzt fclFuseRollPersona bei der Resultatvorbereitung. Historischer 18/18-Hostvertrag modelliert Auswahl, Abbruch und Limits, ist eigenstÃ¤ndig. Keine bestÃ¤tigte frÃ¼here native Integration. FrÃ¼here Ãœbergaben, die das Feature als erhalten bezeichneten, werden hiermit korrigiert. 2er-/3er-Fusion, Abbruch und Persistenz auf Klonen prÃ¼fen, Manuell-Auswahl aus der Pre-Release-Funktionsliste streichen. |
| T02 Â· Schulweg | Release-Blocker bis Hardware-A/B | U14/U15 5,4â†’2,6 FPS, mit verÃ¤ndertem Dialogzustand. E460_001 liegt eng beim genannten f010_003, bestehende School-Smokes belegen diesen Eventabschnitt nicht. STAB_PROFILE mit identischer Szene, Takt und Kamera in getrennten Fenstern ohne/mit Dialog und mit UI03-Kontrolle erfassen. |
| T03 Â· Igor-Schatten | Offen | U01 belegt Raum und Tisch, keinen isolierten Schattenpass. kage/table/ref aus f008_003 getrennt mit PS2 Vanilla prÃ¼fen. |
| T04 Â· Margaret-Farbe | Offen | Sichtbar in U01 und fc_margaret-Ressourcen. Material-, Prelight- oder Beleuchtungsursache nicht isoliert. PS2-Vergleich getrennt vom UI-Portrait. |
| T05 Â· FusionslistenblÃ¶cke | Offen | U21 zeigt den Reveal. Ohne Bild der betroffenen Liste gibt es keinen Beleg fÃ¼r CLUT, Alpha oder Zeichenreihenfolge. Bestehende Karten- und Strahlpfade unverÃ¤ndert. |
| T06 Â· Garu/Cut-in | Offen | U08/U09 ohne isolierten Skill-/Framebeleg fÃ¼r den gemeldeten Fehler. Cut-in, Maske, Depth und VFX getrennt mit PS2 vergleichen. |
| T07 Â· All-Out | Offen | Zwei und vier Teilnehmer als Repro, drei als positive Kontrolle. EigenstÃ¤ndiger All-Out-Owner; die normale Roster-Korrektur wird hier nicht angewendet. |
| T08 Â· weiÃŸe Bodenlinien | Offen | Kein isolierter Frame und kein eindeutiger Materialowner. Guard/UV-NÃ¤hte und Z-PrÃ¤zision gemeinsam mit identischer Kameraposition prÃ¼fen. |
| T09 Â· Einkaufswagen | Offen | f009_004 benannt. Material-, Vordergrund- und Depth-Auswirkung fÃ¼r einzelne Wagen gegen Vanilla abgleichen. |
| T10 Â· Shopping District North | Offen | Ort genannt, kein bestÃ¤tigter Clip-/Depth-/Cache-Owner. South-Pfade bleiben unverÃ¤ndert. |
| T11 Â· Schulkorridor | Offen | Texturfilter, Mip/LOD und Quelltextur fÃ¼r den benannten Korridor getrennt vergleichen. Kein globaler Filterwechsel. |
| T12 Â· Verdeckung | Offen | U19 legt starke Verdeckung nahe. Gewollte Occlusion im gleichen PS2-View prÃ¼fen. |
| T13 Â· Return to Title | Offen | VideoÃ¼bergÃ¤nge bestÃ¤tigt, der einfarbige Frame ist aus dem Log nicht herleitbar. Clear, Fade und DisplayparitÃ¤t im Repro isolieren. |
| T14 Â· Traumnebelstreifen | Offen | FrÃ¼he Sequenz und Frame prÃ¤zisieren. U18 ist kein isolierter Streifenbeleg. Bestehende Dungeon-Nebelregeln weiter verwenden. |
| T15 Â· Dojima-Boden | Offen | Material- und Positionsbeleg am passenden Vanilla-Frame erforderlich. |
| T16 Â· Persona-Swoosh | Diagnose implementiert, auf Go offen | Display-Owner shdPersonaBeginOpen/EndSelection und shdPersonaBeginDisplay senden Bank 0, Gruppe 0, Stream 1 Cue 3/4 bzw. Stream 2 Cue 5. SFX_TRACE protokolliert Queue-Annahme, SQ-Sequenzoffset/-Status, Sample, SAS-Voice und RÃ¼ckgabecode bei Voice/KeyOn; max. 24/24/32 Zeilen je Stufe. Die bestÃ¤tigte Battle-Aktion reicht zusÃ¤tzlich Effektpaket 0x15 ein. Die Cue-Zuordnung des Effekts und der hÃ¶rbare Ausfall bleiben offen. Keine Ã„nderung an Cuewahl, Stop-Reihenfolge oder SFX-PrioritÃ¤t. |
| T17 Â· Izanagi-Lichtkugeln | Offen, niedriger | Self-/Summon-Effekt getrennt von bestÃ¤tigten Zio-Treffern an Gegnern. |
| T18 Â· kurzer TraumÃ¼bergang | Offen, niedriger | Schwarzes Bild vor Eingriff gegen PS2 prÃ¼fen. |
| T19 Â· BGM-Stille | Im UI03-Lauf nicht beobachtet | BGM_TRACE aktivierbar, Ursache offen. Wiedergabe unverÃ¤ndert. |
| T20 Â· Navigatorportrait | Im UI03-Lauf nicht beobachtet | U02â€“04 mit Portrait. UI_NAV_TRACE aktivierbar, Fehler bleibt sporadisch offen. |
| T21 Â· Zielwahl 20,3â†’6,7 FPS | Release-Blocker bis Hardware-A/B | U16/U17 zeigen unterschiedliche Kamera und Workload. Attack, Rad und Zielwahl im selben Kampf als Vergleich. Timing und tatsÃ¤chlicher Draw-Workload fehlen in UI03. |

## UI-Regressionen

| ID | Referenzen | Ergebnis |
| --- | --- | --- |
| UIR01 | U01â€“04/U18, G023/086/087/108 | Implementiert. Gemeinsamer Namensanker und Orange-Akzent auf UI02. Name von (29,192) auf (23,184) nominellen PSP-Quellanker zurÃ¼ck; sichtbare Baseline hÃ¤ngt vom Font ab. Graue Vorderbox und Body bleiben bei der vorhandenen UI02-Geometrie. BildprÃ¼fung auf Go offen. |
| UIR02 | U13, G097 | Implementiert. UI03-Fit der beiden Slot-Owner fÃ¼r Platten und Icons lokal zurÃ¼ckgesetzt. UI02-Kreis und alter Textanker bleiben als gemeinsame Geometrie. Lesbarkeit und alle Orte auf Go prÃ¼fen. |
| UIR03 | U07/U17/U10/U16, G077/078/080 | Implementiert. Basis-, Flash- und Pulse-Draw verwenden eine gemeinsame y-Kompression um 132px. Nominelles Envelope [0,272] wird ca. [13,258]. Originalanimation, Spielzustand, Reihenfolge und TV-Maske bleiben erhalten. HardwareprÃ¼fung der GrÃ¶ÃŸe bei Zielwahl und kleiner Party offen. |
| UIR04 | U07/U17, G078/080 | Offen. TARGET SELECT und roter Balken getrennt. Ziel Glyphen y258â€“268. Im Emulator weiterhin an der Unterkante angeschnitten. Kein ungesicherter Eingriff in 0x32/0x33-Button-Sprites. |
| UIR05 | U05/U06/U07/U17, G078/080/081 | Teilweise implementiert. Rush in den vier UntermenÃ¼pfaden um ca. 10,9 PSP-Pixel tiefer. Rad-Anker und Show Info separat gelassen, dessen exakter Owner/Zielwahlzustand offen. |
| UIR06 | U10/U16, G077/113 | Offen. Der regulÃ¤re ATTACK-Record aus UI03 beseitigte die gelben Punkte laut Hardware nicht. Weitere Ã„nderung wartet auf den Beleg fÃ¼r die Quelle des Ornaments. Targetmarker unverÃ¤ndert. |
| UIR07 | U11, G024 | Implementiert und im Emulator sichtbar. Gemeinsamen Camp-Block 25 px angehoben, einschlieÃŸlich aktiver Transition. Zeilenpitch von rund 20 px und unterer Beschreibungstext bleiben. In der Camp-Aufnahme beginnt Skill bei ca. y35 und Item bei ca. y61. |
| UIR08/09 | Shuffle Time, Social Link | Kosmetische Animationen fÃ¼r spÃ¤ter. |

## Verifikation und Kaltstart-Marker

Native PSP-Kompilation der 22 neu erzeugten Census-Objekte und Link mit 19 Importbibliotheken erfolgreich. Striktes QA07 fÃ¼r regulÃ¤ren Build, exakt eingebettete Dateien, gehashte Eingaben und Git-Provenienz bestanden. Vor der fixen PSP-Datensektion verbleiben 292 Byte im ELF-Layout. Das unabhÃ¤ngige Paket-Hashinventar bestÃ¤tigt 80 Dateien und 5.746.764.901 Byte einschlieÃŸlich der Klone 9, 10 und 16.

Host geprÃ¼ft: Rasterbindung und Revisionswechsel, direkter/aufgeschobener RTT-Screen-Bind, Camp-Translation/Raster, Roster und isolierte UI-Wheel-Anker. Fusion-Session-Hostvertrag 18/18. Drei Audio-Builder-FÃ¤lle und der Audio-Worker-Handoff-Test bestanden; fÃ¼r die Extraktionsfixture wurde BGM-Sampling separat gestubbt. Card-I/O-Test mit Zig und eingeschalteten Assertions bestanden. Tests mit Stubs bleiben auf ihre jeweilige Schnittstelle begrenzt.

PPSSPP 1.20.4 am festen Testpfad. Slot 9 lÃ¤dt, Camp-HauptmenÃ¼ und Skill-/Equip-Pfade erreichbar. Camp und Skill-List visuell gegen den jeweiligen Layoutvertrag geprÃ¼ft. Slot 16 erreicht Rad (aktiver Draw-Owner), ATTACK-Zielwahl und Skill-Liste (aktiver Listen-Owner). RTT-Alloc/Passes im Lauf belegt. TV-Rahmen, COMMAND und Auswahlstreifen im Rad sichtbar. Zielwahl-Clipping und ATTACK-Zierpunkte bestätigt. Diese Route liefert keine Hardware-Lifetime-Freigabe. Ein separater Kaltstart mit UI03 und STAB_PROFILE bestätigte den UI03-Layoutmarker, PERFHW1 status=OK (449 akzeptierte Frames, 0 Rejections) und die zusätzlichen Timingfenster bis zum Teilfenster am Ende. Die Werte in PPSSPP dienen der Funktionsprüfung.

Normale Nutzung: FPS. Ã„ltere Layouts am selben Build mit UI03, UI02, UI01 oder UI_LEGACY. UI03 vergleicht das Layout bei gleicher Runtime. Die GE-StabilitÃ¤tskorrektur bleibt dabei aktiv. VELOCITY_INDEXED_MESH schaltet auf Indexed. BGM_TRACE, UI_NAV_TRACE und SFX_TRACE nur fÃ¼r den jeweiligen Repro.

STAB_PROFILE erzeugt Mittelwerte je 16 Render-Frames im vorhandenen PERFHW1-Fenster. L+R+START halten, gemeinsam loslassen und stabil im Zustand bleiben. cpu_us enthÃ¤lt Aufbau und Arbeit vor der Present-Barriere, wait_us die Barriere. ge_tail_us schÃ¤tzt nur die noch ausstehende vorige GE-Liste und liegt innerhalb der Wartezeit. list_words misst ListengrÃ¶ÃŸe vor Abschluss. Keine isolierte GPU-Gesamtzeit und keine PSP-Go-Leistungsaussage aus Emulatorzeiten. Standard-Batchcounter fÃ¼r RTT, Skin, Clipping, Meshcache und Texturwartezeiten als Zusatzkontext sichern. Bei einem isolierten, kurzen Eventabschnitt statt eines erzwungenen stationÃ¤ren Fensters den Ablauf in beiden LÃ¤ufen identisch wiederholen.

Die Hardware-Zielroute nutzt Slot 9 fÃ¼r Feldâ†’Camp und anschlieÃŸende Stadtkarte/Dialoge, Slot 10 fÃ¼r Castle/Dungeonâ†’Camp und Velvet, Slot 16 fÃ¼r Rad, Zielwahl und Skill-Liste vor Izanami. FÃ¼r Schulweg den passenden Ã¤lteren Save vor f010_003/E460_001 verwenden. Erst mit Klonen Saveâ†’HOMEâ†’kalten Reload und idle HOME bestÃ¤tigen, danach die optionalen A/B- und Diagnosemarker setzen. Dieses Paket dient der privaten QA.
