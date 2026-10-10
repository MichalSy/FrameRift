# FrameRift

**Spiele starten. Frames verstehen.**

FrameRift soll eine native Spielebibliothek und ein Performance-Workspace für [AnyPS5](https://github.com/boykopovar/AnyPS5) werden. Man wählt ein Spiel in einer visuellen Bibliothek aus, startet es und wechselt bei Bedarf in den Profiler, während das Spiel weiterläuft. Gaming und Performance-Analyse gehören zum selben Produkt, bekommen aber jeweils eine eigene Ansicht.

Der aktuelle Umfang ist die native GTK4-Spielebibliothek mit SQLite-Cache: Hero-Auswahl, zuletzt gespielte Titel vor der Sammlung, Suche und Sortierung. Die Beispielbibliothek verwendet echte Katalogspiele und Cover, Hero-Bilder und Logos aus der SteamGridDB-API. Die Bilder werden lokal gecacht. Demo-Metadaten und Spielzeiten dienen der UI-Entwicklung; die Einträge bestätigen keine installierten oder mit AnyPS5 startbaren Spiele. Spielansicht und Profiler sind spätere Planung.

Die folgenden Bilder zeigen das angestrebte Design. Ihre Messwerte und Spielgrafiken belegen keine vorhandene Laufzeitfunktion oder Spielkompatibilität mit AnyPS5. Die aktuelle Oberfläche verwendet englische Texte.

## Designentwürfe

### Bibliothek

![FrameRift-Bibliothek mit ASTRO-BOT-Hero, zuletzt gespielten Titeln und Spiele-Covern](assets/design/library.png)

Eine moderne, kompakte Bibliothek mit einem großen Hero-Bild des ausgewählten Spiels, transparentem Titellogo und direktem Start. **Zuletzt gespielt** steht vor **Deine Spiele**. Die Bibliothek orientiert sich an der visuellen Hierarchie von Steam und Netflix, bekommt aber eine eigene Gestaltung. Es gibt keine dauerhaft offene Detailspalte neben der Sammlung. Die Spiele sind lokal; ein zusätzliches „Lokales Spiel“-Etikett bringt keinen Nutzen.

### Geplant: Spiel und Profiler

![FrameRift-Spielansicht mit laufendem Spiel, Frame-Analyse und Frame-Zeiten](assets/design/profiler.png)

Im Entwurf öffnet sich nach dem Start eine eigene Spielansicht. Das Spiel nimmt den größten Bereich ein. Im Modus **Profiler** kommen eine Frame-Analyse rechts und ein Analysebereich unten hinzu. Geplant sind Frame-Zeiten, CPU-/GPU-Messungen, Logs und später zusätzliche Analysewerkzeuge. Die Begriffe für die Hauptmodi sind **Spielen** und **Profiler**.

Die Einbettung des laufenden Spiels in dieselbe Oberfläche ist ein zentrales Ziel und zugleich ein technischer Prüfpunkt. Ein separater Launcher kann ein fremdes Spielfenster nicht auf jeder Plattform beliebig übernehmen. Windows, Linux/X11 und Linux/Wayland müssen dafür getrennt untersucht werden.

## Was wir vorhaben

- Eine schnelle, native Anwendung für Windows und Linux, mit eigenen Komponenten und sauberen Animationen.
- Eine lokale Bibliothek aus vorhandenen AnyPS5-Spielausgaben und Metadaten, mit Suche, Sortierung und zuletzt gespielten Titeln.
- [SteamGridDB](https://www.steamgriddb.com/) als primäre Quelle für Cover, Hero-Bilder, Logos und Icons; auch animierte Artwork-Varianten sollen berücksichtigt werden.
- Spielstart mit nachvollziehbarer Konfiguration, Prozessverwaltung, Logs und einer übersichtlichen Sitzung.
- Spielen und Live-Profiling in einem Workspace, einschließlich Frame-Auswahl, Aufzeichnung und späterem Vergleich von Messläufen.
- Eine kleine, optionale Integrationsschicht zu AnyPS5 statt einer verpflichtenden GUI-Abhängigkeit in dessen Runtime.
- Ehrliche Messwerte: verfügbare Daten anzeigen, fehlende Messungen benennen und den Einfluss des Profilers auf das Spiel messen.

## Technische Arbeitsrichtung

Die Bibliothek verwendet **C++20, CMake, GTK4 und SQLite**. GLib/GIO und die GTK-C-API bilden die Anwendungsbasis. **libcurl und JSON-GLib** verarbeiten SteamGridDB-Abfragen und Bild-Downloads. libadwaita bleibt eine mögliche Ergänzung für adaptive Komponenten. Windows-Paketierung und Spielfensterintegration müssen in eigenen kleinen Versuchen geprüft werden.

**Qt 6 mit Qt Quick/QML** bleibt eine ernsthafte Alternative, falls die plattformübergreifende Fensterintegration oder der Aufwand für die gewünschte Oberfläche mit GTK4 nicht überzeugt. QML würde eine native UI mit Qt darstellen und keinen Browser voraussetzen.

Weitere vorgeschlagene Bausteine sind optional spdlog, zstd und Tracy für klar begrenzte Aufgaben. Diese sind noch nicht eingebaut. SDL2 und Vulkan sind zunächst Teil des AnyPS5-Spielprozesses und werden nicht automatisch zum UI-Toolkit von FrameRift.

## Bauen und starten

Benötigt werden ein C++20-Compiler, CMake ab 3.20, Ninja, pkg-config, GTK4 ab 4.12, SQLite3, libcurl, JSON-GLib und `glib-compile-resources`, jeweils mit den Entwicklungsdateien. Die aktuelle Entwicklungsplattform ist Linux; Windows ist noch ungeprüft.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/framerift
```

Ein kompakter C++-Check prüft Cache-Persistenz, Migration und Datenerhalt ohne Testframework. Die Bibliothek wird zusätzlich in einem virtuellen Desktop mit dem Referenzentwurf verglichen. CSS und App-Zeichen sind als GResource eingebunden; Spielgrafiken kommen ausschließlich aus dem lokalen API-Cache.

Der Cache liegt unter `g_get_user_cache_dir()/framerift/games.sqlite3`, auf normalem Linux unter `~/.cache/framerift/games.sqlite3`. Die Bilddateien liegen im zugehörigen Artwork-Verzeichnis. SQLite speichert die SteamGridDB-Zuordnung, die öffentliche Quellseite und die lokalen Bildpfade. Bestehende Nutzereinträge bleiben beim Laden von Demo-Daten erhalten; Schema 2 entfernt die alten gebündelten Demo-Einträge.

Für das erstmalige Laden der Beispielbibliothek wird `STEAMGRIDDB_API_KEY` in der Prozessumgebung benötigt. Der Schlüssel wird nicht in SQLite oder im Repository gespeichert. Danach kann die Bibliothek ihre vorhandenen Metadaten und Bilder offline anzeigen. Für einen isolierten Cache:

```sh
XDG_CACHE_HOME=$(mktemp -d) ./build/framerift
```

## Dokumentation

| Dokument | Inhalt |
| --- | --- |
| [Design und Bedienung](docs/design.md) | Die zwei Ansichten, visuelle Regeln, Interaktionen und Animationen |
| [Architektur](docs/architecture.md) | Module, Prozessgrenzen, Datenfluss, Zustände und Fehlerbehandlung |
| [Bibliotheken und Build](docs/technical-stack.md) | Aktuelle Abhängigkeiten und weitere technische Kandidaten |
| [Toolkit und Plattformen](docs/toolkit-and-platforms.md) | GTK4/libadwaita, Qt-Alternative und Windows-/Linux-Fragen |
| [Spielfensterintegration](docs/game-window-integration.md) | SDL-Hooks, native Fenster, X11/Wayland und alternative Bildübergabe |
| [AnyPS5-Anbindung](docs/anyps5-integration.md) | Was im Upstream vorhanden ist und wie FrameRift daran anknüpfen kann |
| [Profiling](docs/profiling.md) | Vorhandene Messungen, geplante Telemetrie, Frame-Korrelation und Messgrenzen |
| [Controller und Eingabe](docs/controller-input.md) | Xbox-/SDL-Anbindung, Fokus und Eingabe im eingebetteten Spiel |
| [Artwork](docs/artwork.md) | SteamGridDB, Zuordnung, Cache, Animationen und manuelle Auswahl |
| [Roadmap](docs/roadmap.md) | Technische Spikes, erster nutzbarer Umfang und spätere Erweiterungen |
| [Entscheidungen und offene Fragen](docs/decisions-and-open-questions.md) | Festgelegte Produktziele und noch zu prüfende technische Vorschläge |
| [Bildquellen](assets/design/README.md) | Herkunft und Einordnung der beiden Entwürfe |

## Abgrenzung und aktueller Stand

FrameRift ist ein eigenständiges Begleitprojekt. AnyPS5 relinkt Spiele zu nativen Programmen; FrameRift soll diese starten und beobachten. Eine Spielebibliothek ergibt deshalb auch ohne eine zentrale Emulator-Oberfläche Sinn. Das Repository ist kein offizielles AnyPS5-Produkt und verspricht keine automatische Unterstützung jedes Spiels.

Der aktuelle Entwicklungsabschnitt konzentriert sich ausschließlich auf die Spielebibliothek und ihren lokalen Cache. Spielstart, weitere Screens, Fensterdarstellung, Eingabefokus und Messschnittstelle gehören zu späteren Abschnitten. Die ausführliche Planung enthält bewusst auch diese späteren Ideen.

Beiträge zur UI, zum Cache und zu klar abgegrenzten technischen Versuchen sind willkommen; siehe [CONTRIBUTING.md](CONTRIBUTING.md). Die Projektlizenz ist noch festzulegen. Fremde Spielgrafiken und Marken in den Entwürfen behalten ihre jeweiligen Rechte.
