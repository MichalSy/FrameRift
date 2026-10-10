# Entscheidungen und offene Fragen

## Festgelegte Produktziele

| Thema | Entscheidung |
| --- | --- |
| Name | **FrameRift** |
| Zielgruppe | Spieler und Nutzer, die Spielperformance untersuchen möchten |
| Plattformen | Native Desktop-Anwendung für Windows und Linux |
| UI-Gefühl | Individuell, modern, kompakt, dunkle Graphitflächen mit Cyan-Akzent und sauberen Animationen |
| Ansichten | Bibliothek zuerst; nach Spielauswahl eine eigene Spiel-/Profiler-Ansicht |
| Bibliothek | Hero des ausgewählten Titels, dann „Zuletzt gespielt“, dann „Deine Spiele“ |
| Spielansicht | Spiel im Zentrum; zuschaltbare Analysebereiche |
| Modusnamen | **Spielen** und **Profiler** |
| Artwork | SteamGridDB als primäre Quelle, einschließlich möglicher Animationen |
| Projektgrenze | Eigenständiger Launcher; optionale, kleine Anbindung an AnyPS5 |
| Datenhaltung | Spiele und Messdaten lokal; lokale Verwendung wird nicht als zusätzliches Spielmerkmal etikettiert |

Die beiden veröffentlichten Bilder beschreiben diese Richtung. Sie enthalten keine Kennzeichnung als Designstudie innerhalb der Oberfläche. Ihre Einordnung als Konzept wird in der Dokumentation erklärt.

## Technische Arbeitsannahmen

| Vorschlag | Warum er im Plan steht | Was ihn bestätigen muss |
| --- | --- | --- |
| C++20 und CMake | Gemeinsamer nativer Kern und passende Runtime-/Plattformanbindung | Kleiner, reproduzierbarer Windows-/Linux-Build |
| GTK4/libadwaita | Native UI und Interesse an der aktuellen GNOME-Technik | Eigene Gestaltung, Windows-Auslieferung, Fensterintegration und Animationen |
| Qt 6 Quick/QML als Alternative | Individuelle native UI und relevante Fenster-APIs | Derselbe Integrationsversuch bei Problemen mit dem GTK-Pfad |
| libcurl, JSON und SQLite | Klar abgegrenzte Aufgaben für Artwork, Metadaten und Katalog | Tatsächlich benötigter Umfang, Paketierung und Wartungsaufwand |
| Optionales Telemetrieprotokoll | Messwerte mit eindeutiger Semantik und Frame-Zuordnung | Kleiner Runtime-Sender mit messbarem Overhead |
| Kooperativer Window Host | Laufendes Spiel innerhalb des Programms | Getrennte Nachweise für Windows, X11 und eine Wayland-Desktop-Sitzung |

Arbeitsannahmen sind keine endgültigen Abhängigkeitsentscheidungen. Besonders die Fensterintegration soll die Toolkit-Auswahl beeinflussen, bevor viel UI-Code geschrieben wird.

## Offene technische Fragen

- Welcher Fensterpfad funktioniert mit den tatsächlichen AnyPS5-Artefakten? Reicht ein exportiertes natives Handle, braucht es einen kooperativen Adapter oder ist ein Hook für einen begrenzten Plattformfall sinnvoll?
- Können Host und Spiel in einer Wayland-Sitzung verlässlich über einen gemeinsamen X11-/XWayland-Pfad arbeiten? Welche Grenzen bleiben bei einer nativen Wayland-UI?
- Wie werden Spiel-, Tastatur-/Maus- und Controller-Fokus geregelt, wenn Analysefelder aktiv sind? Controller-Hintergrundeingaben müssen ausdrücklich berücksichtigt werden.
- Welche CPU-/GPU-Messpunkte werden für den ersten Profiler verbindlich angeboten? Was bedeutet die Frame-ID bei mehreren Einreichungen und verzögerten GPU-Ergebnissen?
- Wie groß darf der Overhead je Messmodus sein, und wie wird ein Vergleich reproduzierbar durchgeführt?
- Welches Capture-Format und welche Exportformate lohnen sich zuerst? Bildaufnahme ist eine zusätzliche Datenquelle mit eigenen Kosten.
- Welche UI-Bibliothek stellt animiertes WebP auf beiden Zielplattformen zuverlässig und effizient dar?
- Wie werden Spielausgaben, unterschiedliche Builds und Startprofile möglichst einfach importiert und angezeigt?
- Wie werden SteamGridDB-Zugangsdaten lokal gespeichert, und welche Variante der Artwork-Auswahl ist für Nutzer am verständlichsten?
- Original FrameRift code and documentation use [MIT](../LICENSE). Dependency distribution requirements remain to be reviewed.

## Noch nicht für den ersten Release zugesagt

Die detaillierte Frame-GPU-Timeline, Shader-Hotspots, Imports-Analyse, automatisierte Tests, umfassende Controller-Navigation der Bibliothek und Build-/Relink-Automation bleiben Erweiterungsideen. Die Dokumentation soll sie konkret genug festhalten, um später gezielt priorisieren zu können.

Die ASTRO-BOT-Darstellung entscheidet weder über Spielunterstützung noch über die verfügbare Messinstrumentierung. Beide müssen an realen Builds nachgewiesen werden.
