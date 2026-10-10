# FrameRift

**Spiele starten. Frames verstehen.**

FrameRift soll eine native Spielebibliothek und ein Performance-Workspace für [AnyPS5](https://github.com/boykopovar/AnyPS5) werden. Man wählt ein Spiel in einer visuellen Bibliothek aus, startet es und wechselt bei Bedarf in den Profiler, während das Spiel weiterläuft. Gaming und Performance-Analyse gehören zum selben Produkt, bekommen aber jeweils eine eigene Ansicht.

Dieses Repository enthält die Produktplanung, zwei UI-Entwürfe und die technische Ausgangsbasis. Eine lauffähige FrameRift-Anwendung ist noch nicht implementiert. Die Bilder zeigen das angestrebte Design; Messwerte, Spielkacheln und Bedienelemente im Entwurf sind keine zugesicherten Funktionen einer vorhandenen Software. Insbesondere belegt die Darstellung von ASTRO BOT keine Spielkompatibilität mit AnyPS5.

## Die beiden Ansichten

### Bibliothek

![FrameRift-Bibliothek mit ASTRO-BOT-Hero, zuletzt gespielten Titeln und Spiele-Covern](assets/design/library.png)

Eine moderne, kompakte Bibliothek mit einem großen Hero-Bild des ausgewählten Spiels, transparentem Titellogo und direktem Start. **Zuletzt gespielt** steht vor **Deine Spiele**. Die Bibliothek orientiert sich an der visuellen Hierarchie von Steam und Netflix, bekommt aber eine eigene Gestaltung. Es gibt keine dauerhaft offene Detailspalte neben der Sammlung. Die Spiele sind lokal; ein zusätzliches „Lokales Spiel“-Etikett bringt keinen Nutzen.

### Spiel und Profiler

![FrameRift-Spielansicht mit laufendem Spiel, Frame-Analyse und Frame-Zeiten](assets/design/profiler.png)

Nach dem Start öffnet sich eine eigene Spielansicht. Das Spiel nimmt den größten Bereich ein. Im Modus **Profiler** kommen eine Frame-Analyse rechts und ein Analysebereich unten hinzu. Geplant sind Frame-Zeiten, CPU-/GPU-Messungen, Logs und später zusätzliche Analysewerkzeuge. Die Begriffe für die Hauptmodi sind **Spielen** und **Profiler**.

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

Die aktuelle Arbeitsrichtung ist **C++20, CMake und eine native UI mit GTK4/libadwaita**. Die Gestaltung soll bewusst individuell bleiben. GTK4 bietet die UI-Basis; libadwaita ist eine mögliche Ergänzung für Anwendungsstruktur und adaptive Komponenten. Windows-Paketierung, Animationen und insbesondere die Spielfensterintegration müssen in einem kleinen Prototyp geprüft werden, bevor die Toolkit-Entscheidung festgeschrieben wird.

**Qt 6 mit Qt Quick/QML** bleibt eine ernsthafte Alternative, falls die plattformübergreifende Fensterintegration oder der Aufwand für die gewünschte Oberfläche mit GTK4 nicht überzeugt. QML würde eine native UI mit Qt darstellen und keinen Browser voraussetzen.

Weitere vorgeschlagene Bausteine sind libcurl für Artwork-Abfragen, nlohmann/json für Metadaten und Protokolle, SQLite für Bibliothek und Sitzungsindex sowie optional spdlog, zstd und Tracy für klar begrenzte Aufgaben. Das sind Planungsvorschläge, keine bereits eingebauten Abhängigkeiten. SDL2 und Vulkan sind zunächst Teil des AnyPS5-Spielprozesses und werden nicht automatisch zum UI-Toolkit von FrameRift.

## Dokumentation

| Dokument | Inhalt |
| --- | --- |
| [Design und Bedienung](docs/design.md) | Die zwei Ansichten, visuelle Regeln, Interaktionen und Animationen |
| [Architektur](docs/architecture.md) | Module, Prozessgrenzen, Datenfluss, Zustände und Fehlerbehandlung |
| [Bibliotheken und Build](docs/technical-stack.md) | Vorgeschlagene Abhängigkeiten und ihre konkreten Aufgaben |
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

Der erste Entwicklungsabschnitt soll die schwierigen Integrationspunkte belegen: Spielstart, Fensterdarstellung, Eingabefokus und eine kleine, belastbare Messschnittstelle. Die ausführliche Planung enthält bewusst auch spätere Ideen. Sie ist keine Zusage, alle gezeigten Profiler-Funktionen in der ersten Version zu liefern.

Es gibt derzeit keine Build- oder Installationsanleitung für FrameRift, da noch kein Anwendungsquellcode vorliegt. Beiträge zur Planung und zu klar abgegrenzten technischen Versuchen sind willkommen; siehe [CONTRIBUTING.md](CONTRIBUTING.md). Die Projektlizenz für zukünftigen Code ist noch festzulegen. Fremde Spielgrafiken und Marken in den Entwürfen behalten ihre jeweiligen Rechte.
