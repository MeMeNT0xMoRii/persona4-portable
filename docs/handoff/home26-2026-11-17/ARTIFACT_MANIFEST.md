# Artefaktinventar · home26

Inventur 17. November 2026. Der veröffentlichte Build-Pin lautet `9e1da616bc773c7fd5dc7246d58daf19c00f5fa0`. Private Dateien sind nach Namen aufgeführt. Der Git-Handoff enthält ausschließlich Markdown.

## Runtime für die bestehende US-Installation

| Datei | Exakte Bytes | SHA-256 |
| --- | ---: | --- |
| `EBOOT.PBP` | 5287282 | `ae2f52c6ebb69f3962eaeb9c27765d0bfa04a596bc75c01ab143a65bec07f580` |
| `p4data.bin` | 2320512 | `a305ad2488262010ca8f02742ea4a7a64908c9ba0f6a9cc654083cb15eb0063c` |
| `p4str.bin` | 9904 | `4bc65004bf091811ee679342eed10efe1dea01ef0fd02c0e189da45adf6e7ee4` |
| `b249-xmb-home26.zip` | 2622212 | `64424fbf65ecce333396ed52109b550cdadc52f34570836e5d7bfc660a761780` |
| `SHA256SUMS.txt` | 315 | `aa3ac0553c1fd246c1ffe101d33df1c4e1327247dcc5b5e311f9f1b2cf935c6c` |
| `b249-xmb-home26-Bericht.md` | 34669 | `2e0d5e673d7577c92648f71a65cd359bbc2821e6456f3135208106ead845fabd` |

Alle sechs Dateien liegen im Work-Ordner `b249-xmb-home26/` und sind bereits privat gesichert. Die vier Runtime-Hashes wurden erneut gegen die Prüfsummenliste geprüft. ZIP-CRC und Bytegleichheit seiner drei Inhalte mit den Einzeldateien sind bestätigt. Das ZIP enthält die drei Runtime-Dateien auf oberster Ebene.

## Private Vergleichs- und Nachweisarchive

| Archiv | Exakte Bytes | SHA-256 und Inhalt |
| --- | ---: | --- |
| `b249-xmb-home25-reference.zip` | 2628564 | `7f841b7bc1e78650bc8cd30718923dacff091a989df7d81fb4ec34eb480d62b0`. Drei vorhandene, gegen das home25-Referenzmanifest geprüfte Runtime-Dateien. |
| `home26-Playtest-Evidence.zip` | 55307627 | `3da353d1b31139afd803c2cf17286c83ee58b085f31d7b99b1fd797aa19ee283`. 20 home25-Originale, pro Datei SHA-256 und voriger home26-Bericht. |

Home25-Einzeldaten aus `persona4-home26/build/ab_home25/`: `EBOOT.PBP` 5287162 B, SHA-256 `a6e60d2c9ab2668ac57254fadd4a0a1c32d912ff1adb6430a60279bdb0bc6fcc`. `p4data.bin` 2320512 B, SHA-256 `39a7de5d21eb2ac9d5cac54d5faaee29a9a3e3211084d8f5c575e032f3ab7cad`. `p4str.bin` 9904 B, SHA-256 identisch mit home26. Beide ZIPs sind im Work-Ordner `home26-handoff-private/` und privat gesichert.

## Lokale Build-Inputs und Diagnoseausgaben

| Pfad im Build-Checkout | Bytes | SHA-256 / Status |
| --- | ---: | --- |
| `build/SLUS_217.82` | 8621084 | `f90675b8a38138ddb5dc5cdf641550f41800fcb86bb499c586073e964fb41836`. Proprietärer Disc-Input; privat lokal. Aus der eigenen US-ISO extrahieren. |
| `build/verified_remote_home26/p4.elf` | 8316336 | `47323bf6dc1006fd88728bc39833816d46677758fcbbab2a696b034cfe538c4b`. Lokal, optional für Debug. |
| `build/verified_remote_home26/p4.prx` | 4971790 | `dd54506a89395847f8c995817fd23ac84801ea38fe969c830b122d5b1bbc346e`. Lokal und im ausgelieferten EBOOT eingebettet. |
| `build/verified_remote_home26/manifest.json` | 95957 | `5d7e058cefc676c39120a9fafac6cc32ee9069dce8bc65b87f83778f3c244e1f`. Lokal. |
| `build/census/`, `build/obj/`, `build/rebase_table.tsv`, `build/address_audit/` | — | Lokale, regenerierbare Zwischenprodukte. Vollscan und Einzelhashes der Objektmenge waren für die Übergabe nicht nötig. |
| `ext/Persona4-Decompilation` | — | Git-Pin `363eab8a966d60a9773b45f330f2344329ec4f0a` lokal verifiziert. Separater Quellcode-Input. |
| Historischer Linux-PSPDEV, genaue SDK-Bibliotheksidentität | — | Voriger Pfad nicht mehr verfügbar. Dokumentierte Windows-Alternative und Compiler-/SDK-Referenzen in [BUILD_REPRODUCTION.md](BUILD_REPRODUCTION.md). |
| Eigene US-ISO, vollständiger `p4files.idx` + Container, `SOUND/`, `VIDEO/` | — | Vollständiges Set in diesem Workspace nicht verfügbar. Von eigener Disc privat vorbereiten. Erforderlich für Gameplay, zusätzlich zum geprüften Runtime-Update. |
| Passende Replay-Saves und Rohaufnahmen des home26-Hardwaretests | — | Nicht Teil der verfügbaren Übergabedateien. Schriftlicher Teststatus und ältere Zeitmarken in [TEST_EVIDENCE.md](TEST_EVIDENCE.md). |
