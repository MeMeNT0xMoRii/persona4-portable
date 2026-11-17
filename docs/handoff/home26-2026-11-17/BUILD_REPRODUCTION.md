# Build-Reproduktion · b249-xmb-home26

Referenz `9e1da616bc773c7fd5dc7246d58daf19c00f5fa0`. Diese Anweisungen dokumentieren den bereits abgeschlossenen Build. Bei der Übergabe wurden die vorhandenen Dateien geprüft.

## Eingaben

| Eingabe | Festlegung |
| --- | --- |
| Portquellcode | Verifizierten home26-SHA in einem neuen sauberen Checkout verwenden. Git und Python ab 3.10 gemäß [Build-Dokumentation](../../building.md). |
| Decomp | [Raikaru/Persona4-Decompilation](https://github.com/Raikaru/Persona4-Decompilation), `363eab8a966d60a9773b45f330f2344329ec4f0a`, im Checkout unter `ext/Persona4-Decompilation`. Der lokale Checkout dieses Pins wurde bei der Übergabe bestätigt. |
| Retail-Executable | `build/SLUS_217.82` aus der eigenen unveränderten Persona-4-US-PS2-ISO. Erforderliche SHA-1 `4eeec0360cf2715535d9f7e52eb69d786fb0158c`. Lokal vorhanden, separat von den öffentlichen Quellen. Andere Regionen sind für diesen Prozess nicht qualifiziert. |
| Historischer exakter home26-Compiler | Linux PSPDEV mit GCC 15.2.0 und passendem PSPSDK. Voriger Pfad `/workspace/scratch/866781bc41e0/psp-build-tools/native/pspdev`; native `.exe`-Symlinks für Skriptnamen. Dieser Pfad ist im aktuellen Workspace nicht mehr verfügbar. Die genaue native SDK-/Bibliotheksidentität ist hier nicht erneut erhoben. |
| Dokumentierter Windows-Pfad | PSPDEV GCC 15.2.0 (Quellsnapshot `1a3399792491`), Binutils `a8b53fe2b582`, PSPSDK `09f02b88b9f3`. Archiv-SHA-256 `ba3adc5a33a9119b59b029f7d10df17f2658dc89f9c3fcec660b235a91ff5020` laut `tools/build_reference.json`. Windows-Neubau gegen exakt dieselben home26-Hashes ist ungetestet. [Toolchain-Release](https://github.com/dreikk91/persona4-psp/releases/tag/toolchain-gcc15.2) |
| Disc-Ressourcen für Erstinstallation | Eigene US-ISO, `tools/requirements-video.txt`, FFmpeg/libx264, vgmstream CLI r2168 mit DLLs und Windows-.NET-C#-Compiler für VAG. Diese Konverter sind für das Runtime-Update aus vorhandenem ELF nicht erforderlich. [Installationsvorbereitung](../../prepare_game.md) |

## Schritte für einen späteren Neubau

In einem neuen Checkout starten und existierende Baseline-Dateien separat aufheben. Die Skripte erwarten PSPDEV in der Umgebung. Unter PowerShell `$env:PSPDEV` und `$env:PATH` setzen, unter Bash `export PSPDEV=...` und `export PATH="$PSPDEV/bin:$PATH"`. Benötigt werden GCC, GNU Binutils, PSPSDK-Header und -Bibliotheken, `psp-fixup-imports`, `psp-prxgen`, `mksfoex` und `pack-pbp`. Wo Skripte `.exe` verlangen, verwendete der qualifizierte Linux-Aufbau native Symlinks. Ein Windows-Executable ist unter Linux dafür ungeeignet.

```sh
git clone https://github.com/MeMeNT0xMoRii/persona4-portable.git persona4-home26
cd persona4-home26
git checkout --detach 9e1da616bc773c7fd5dc7246d58daf19c00f5fa0
git clone https://github.com/Raikaru/Persona4-Decompilation.git ext/Persona4-Decompilation
git -C ext/Persona4-Decompilation checkout --detach 363eab8a966d60a9773b45f330f2344329ec4f0a
python tools/extract_elf.py "/pfad/zur/unveraenderten-US-ISO.iso"
```

Danach den vollen ELF-SHA-1 prüfen, etwa mit `sha1sum build/SLUS_217.82` oder `Get-FileHash build/SLUS_217.82 -Algorithm SHA1`. Alternativ eine bereits extrahierte, hashgleiche Kopie als einzigen anfänglichen Build-Input einlegen. Für einen frischen Build gilt die folgende Reihenfolge, jeweils nur nach erfolgreichem vorherigem Schritt.

```sh
python tools/psp_census.py --compile
# build/census/failed_compile.txt darf keine fehlgeschlagene Datei enthalten.
python tools/psp_ptr_proof.py
python tools/psp_link.py
python tools/psp_hw_debug.py --release --out build/verified_remote_home26
```

Census erstellt den PSP-kompatiblen Quellspiegel und 1.416 Compile-Einheiten. Pointer-Proof und Link erzeugen Rebase-Daten und Adress-Audit. Der letzte Schritt baut den Release-PRX/EBOOT und sein passendes Datenpaar. Der frühere Full-Build hatte 19 validierte PSP-Importbibliotheken, 5.857 Rebase-Einträge, 168 Switch-Ziele und keine ungelösten nativen Funktionszeigerziele. Die 821 generierten Spielobjekte waren gegenüber home25 hashgleich. Nach Abruf des veröffentlichten Commits wurde der Release-Link wiederholt; EBOOT und beide Daten-Dateien stimmten bytegenau mit dem ausgelieferten Update überein. Referenzhashes siehe [ARTIFACT_MANIFEST.md](ARTIFACT_MANIFEST.md). Dateigröße, Hashes, Startmarke im PBP und ZIP-Inhalte prüfen. Andere ZIP-Metadaten können einen anderen ZIP-Hash ergeben, selbst wenn die drei Nutzdateien bytegleich sind.

## Update und vollständige Installation

Für ein Update in einer bestehenden US-Installation nur `EBOOT.PBP`, `p4data.bin` und `p4str.bin` **aus einem gemeinsamen Satz** nach `PSP/GAME/P4` übernehmen. Für eine neue Installation zusätzlich den Index `p4files.idx`, alle entpackten Containerteile und die vollständigen Ordner `SOUND/` und `VIDEO/` erzeugen. Ausführungsschritte für die eigene Disc stehen in [building.md](../../building.md#4-recreate-game-resources-from-the-disc): `psp_disc.py DATA BTL ENV BGM ZZZZZ`, `psp_video.py --all`, `psp_audio.py --all --vgmstream … --pack build/audio_pack/SOUND`, `psp_release.py --no-build`. Referenz-Ausgabepfad der vollständigen Installation ist `build/psp_dist/PSP/GAME/P4`. Ein Runtime-Update bleibt unvollständig für Gameplay ohne diese Ressourcen. Proprietäre Eingaben und erzeugte Runtime-Dateien privat halten.
