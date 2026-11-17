# perf01 auf PSP Go · Hardwarebericht und perf02-Messplan

Getestet wurde `Persona4-PORTABLE-v0.27-dev-perf01` aus Quellstand `fa97b4d`. Quelle ist der Spielerbericht auf echter PSP Go, aufgenommen in die Projektdokumentation am 18. November 2026 (Europe/Vienna). Die FPS sind grobe Overlay-Ablesungen. Für diesen Lauf liegen keine kontrollierten Durchschnitts-, Frametime- oder gepaarten home26-Messungen vor.

## Funktion und Performance

CRIWARE bleibt zentriert. XMB-Start, Speichern, Laden und HOME funktionierten in den getesteten Abläufen. Die getesteten Abschnitte waren spielbar. Der Bericht nennt keine neuen schwerwiegenden Stabilitätsprobleme. Ein Gameplay-FPS-Gewinn gegenüber home26 ist bisher offen.

| Szene | Beobachtung auf perf01 |
| --- | --- |
| Intro und Titel | Übergang kurz ca. 6,5 FPS, danach ca. 9,8–10 FPS. Nach Tastendruck im Menü „New Game / Load Game / Config“ 30 FPS. Der einfarbige Blitz bei Return to Title bleibt sichtbar. |
| Junes | Im Stand ca. 12,8–13 FPS. Beim Laufen niedrigster beobachteter Wert 11,2 FPS. Einkaufswagen weiterhin grafisch fehlerhaft. |
| Kampfbeginn | Beim Einblenden zweier Gegner kurz 8,8 FPS. |
| Kampf- und Skillauswahl | Skillauswahl ca. 27–28 FPS. Zio kurz ca. 16 FPS. All-Out-Yes/No ca. 17–18 FPS, Animation ca. 19–20 FPS, anschließend beim Verschwinden der Gegner ca. 17,7 FPS. |
| Schulkorridor | Vor dem Klassenzimmer im Stand ca. 21,6–22,2 FPS. Bei Bewegung niedrigster beobachteter Wert 19,7 FPS. |
| Shopping District North | Beim Eintritt kurz 13,5 FPS. In Bewegung je nach Position ca. 13,8–30 FPS. |

## Darstellung und Zustandsabhängigkeit

- **Fusion und Zio.** Helle Lichtkugeln bleiben sichtbar. Die neu fusionierte Apsaras flackert weiterhin. Der Schatten unter Igors Tisch bleibt auffällig, Margaret wirkt weiterhin matt-bläulich.
- **Nebel und Dungeon.** Weiße Linien am Boden in Yukikos Dungeon und schwarze Streifen in der frühen Traum-/Nebelwelt wurden beobachtet. Die beiden Szenen getrennt untersuchen.
- **UI.** Rush sitzt weiterhin zu weit oben. Die TV-Wetter-Icons ragen über den Vorhersagerahmen. All-Out-Porträts sind bei zwei und vier Charakteren teilweise abgeschnitten. Drei Charaktere wirken im beobachteten Fall korrekt.
- **Teddies Porträt.** Beim Eintritt in Yukikos Castle fehlte es zweimal hintereinander. Nach vollständigem Spielende und Neustart erschien es wieder. Die Wiederholung im laufenden Prozess und die Änderung nach dem Neustart sind der Ansatz für ein Vergleichsreplay. Der betroffene Zustand und die Ursache sind offen.
- **Schulkorridor.** Der Boden wirkt aus der Entfernung matschig und wird beim Annähern sichtbar klarer. Das Material, die effektive Mip-Stufe, Filterung, UVs und der Geometrie-/LOD-Pfad sind zu prüfen.
- **Widescreen.** Velvet-Room-UI und Social-Link-Porträts wirken möglicherweise zu breit. Das bleibt eine Nutzerbeobachtung bis zum phasengleichen Vergleich mit PS2-Material. Passende PS2-Widescreen-Patches können dabei als zusätzliche Referenz dienen.

## Technische Reihenfolge für perf02

| Priorität | Nächste Messung | Entscheidung aus der Messung |
| --- | --- | --- |
| **1 · Junes West** | Home26 → perf01 → home26 bei 333 MHz, gleicher Save, Eintritt, Blickwinkel und Laufweg. Nach fünf Sekunden Stabilisierung 20 Sekunden im Stand und zehn Sekunden auf derselben Route. Overlay und Video pro Abschnitt erfassen. Mit vergleichbar instrumentierten Pipeline-Läufen CPU, GE-Warten einschließlich Synchronisation, Present und Textur-Transfers getrennt erfassen. CALL-Hits, Mesh-/Skin-Cache und Draw-Zahlen je Fenster zur Einordnung heranziehen. | Bei stabiler CPU-Dominanz gezielt den statischen Mesh-/CALL-Pfad untersuchen. Bei GE- oder Transfer-Dominanz Material, Draw-Fläche und Uploads ermitteln. Wagen separat mit derselben Kamera gegen PS2 vergleichen. |
| **2 · Titel und Kampfeinstieg** | Komplettes Intro, Intro-Skip und Return to Title getrennt nachstellen. Movie-Ende, erste fertige GE-Liste, tatsächliche Present- und Draw-Bank, Clears und Menüeingabe auf eine Zeitachse legen. Im Zweigegnerkampf Archiv-/Textur-Reads, Modell-/Animationsaufbau, GE-Warten und Present vom Eintritt bis zur Skillauswahl erfassen. All-Out-Dialog, Animation und Gegner-Ende separat markieren. | Übergangsblitz und niedrige FPS getrennt einem Zeitfenster bzw. dem ersten auffälligen Frame zuordnen. Einbrüche beim Laden und dauerhafte Renderkosten getrennt bewerten. |
| **3 · Teddie und Effekt-Rendering** | Teddie mit gleichem Save/Route mehrfach im selben Prozess versuchen. Den ersten Fehllauf und den kalten Neustart jeweils mit Video und `p4.log` erfassen. Bei Bedarf gezielt UI-Draw, Raster-/CLUT-Bindung, Texture-Generation sowie Lade-/Unlock-Ereignisse gegenüberstellen. Für Zio und Apsaras denselben Effektschritt treffen. Bei Apsaras benachbarte helle und dunkle Frames anhand des Fusionslogs und separater GE-Dumps vergleichen. Writer/Reader, `TEST/ALPHA`, Z-Write und Feedback-Bank erfassen. | Bei Teddie zwischen fehlendem Draw, fehlenden Bilddaten und einem vorhandenen Draw mit falschem Zustand unterscheiden. Für Lichtkugeln oder b344 zuerst den konkreten verantwortlichen Draw ermitteln. |
| **4 · Bildqualität und UI** | Schulkorridor am gleichen Standpunkt und in derselben Bewegung aufnehmen. Erst Bodentextur und Mip-Kette identifizieren, dann gezielte Mip-/Filter-A/B mit konstanten UVs und identischer Kamera. Traumstreifen und weiße Dungeon-Linien separat auf Clip-, Tiefen- und Feedback-Grenzen prüfen. Für Rush, TV-Wetter und All-Out je dieselbe Animationsphase erfassen. All-Out mit 2/3/4 Figuren sowie Velvet Room und Social Links gegen natives PS2-4:3 vergleichen und die gewünschte 16:9-Korrektur an ausgewählten Elementen prüfen. | Textur- und UI-Änderungen auf den belegten Raster- bzw. Layoutpfad beschränken. Der Dreierfall bei All-Out dient als Kontrollfall. |

Die Pipeline bleibt für vergleichende FPS-Aufnahmen aktiv. `hwperf` liefert aggregierte Fenster. Für die kurzen Einbrüche braucht es zusätzlich Zeitmarken oder einen begrenzten Trace. GE-Dumps im seriellen Modus dienen der Draw-Analyse. Vor einer perf02-Änderung sollten der betroffene Pfad oder Engpass und ein wiederholbarer Vergleich feststehen. Dieser Bericht ist die Planungsgrundlage für die nächste Freigabe.
