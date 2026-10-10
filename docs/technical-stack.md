# Bibliotheken und Build

## Status

Dieses Dokument beschreibt vorgeschlagene Bausteine für FrameRift. Es gibt noch keine CMake-Konfiguration, keine festgeschriebenen Versionsnummern und keine installierte Abhängigkeitskette im Repository. Die eigentliche Toolkit-Abwägung steht in [Toolkit und Plattformen](toolkit-and-platforms.md). Die Entscheidung soll durch einen kleinen Windows-/Linux-Prototyp gestützt werden.

## Arbeitsrichtung

| Bereich | Vorschlag | Aufgabe und Einordnung |
| --- | --- | --- |
| Sprache | C++20 | Bibliotheksmodell, Prozessverwaltung, Datenverarbeitung und Plattformadapter; vorgeschlagener gemeinsamer Kern |
| Build | CMake + Ninja | Abhängigkeiten und Targets beschreiben; schnelle lokale Builds; konkrete Compiler- und Paketierungswege noch prüfen |
| UI | GTK4 | Native Widgets, Rendering und Eingabe; aktuelle Arbeitsrichtung |
| Anwendungsbausteine | libadwaita | Adaptive Komponenten und Anwendungsstruktur; einzelne Komponenten passend zum eigenen Design auswählen |
| GTK-Umfeld | GLib/GObject/GIO | Main Loop, Signals, Datei-/Prozess- und asynchrone APIs im GTK-Pfad |
| UI-Alternative | Qt 6 + Qt Quick/QML | Native, individuell gestaltbare UI und plattformbezogene Fensterintegration; Alternative beim Toolkit-Gate |
| HTTP | libcurl | SteamGridDB-API und Bild-Downloads im Hintergrund; vorgeschlagener, portabler HTTP-Baustein |
| JSON | nlohmann/json | Metadaten, Konfiguration und frühe Telemetrie-Nachrichten; zunächst für moderate Datenmengen |
| Lokaler Katalog | SQLite | Spiele, Startprofile, Artwork-Zuordnungen und Sitzungsindex |
| Eigene Logs | spdlog, optional | Strukturierte Diagnose von FrameRift; Spielausgabe bleibt separat zuordenbar |
| Capture-Kompression | zstd, optional | Größere Messaufzeichnungen in einem Worker komprimieren |
| Entwicklungsprofiling | Tracy, optional | FrameRift-Overhead und gezielte Runtime-Instrumentierung untersuchen |
| Tests | Catch2, möglicher Kandidat | Spätere Tests für Parser, Protokoll und Messberechnungen |

GTK4/libadwaita und Qt sind alternative Frontend-Wege. Beide parallel in die erste Version einzubauen wäre ohne konkreten Nutzen unnötiger Aufwand. Ebenso sollte ein HTTP-Client ausgewählt werden, statt libcurl und weitere Clients für dieselbe Aufgabe vorzusehen.

## GTK4 und libadwaita im C++-Projekt

GTK4 und libadwaita stellen C-APIs bereit. Eine C++-Anwendung kann diese nutzen, sollte aber Besitzverhältnisse von GObject-Referenzen, Callback-Lebensdauer und Thread-Zugriff in kleinen Wrappern verständlich halten. gtkmm ist eine mögliche C++-Bindung für GTK; es wird hier keine gleichermaßen etablierte libadwaita-C++-Bindung vorausgesetzt.

Die eigene Gestaltung soll nicht aus einer großen Menge undokumentierter Theme-Overrides bestehen. Wiederverwendbare Karten, Hero-Bereich, Toolbars, Zeitleiste und Frame-Details brauchen eine bewusst entworfene Komponentenbasis. libadwaita wird nach konkretem Nutzen ausgewählt und legt nicht automatisch jedes Detail der Oberfläche fest.

Windows-Unterstützung ist sowohl eine Backend- als auch eine Auslieferungsfrage. DLLs, Bild-Loader, Ressourcen, Schriftverhalten und Icons müssen in einem sauberen Paket funktionieren. Eine auf dem Entwicklungsrechner sichtbare Oberfläche ist noch keine validierte Endnutzerinstallation.

## Datenverarbeitung

nlohmann/json eignet sich als verständliche Ausgangsbasis für Konfigurations- und Metadatenobjekte. Ein Hochfrequenzkanal muss seine tatsächliche Nachrichtenrate und Kosten separat prüfen. JSON kann im ersten Prototyp helfen; für große Ereignismengen kann später ein kompakteres, versioniertes Format sinnvoll werden. Ein Formatwechsel darf die Bedeutung der Messwerte nicht verändern.

SQLite speichert den Katalog und Indizes. Die primäre Messdatenablage wird unabhängig davon entworfen. Für Capture-Kompression ist zstd ein möglicher Baustein; Kompression und Dateischreiben bleiben außerhalb des Spielthreads und der UI-Zeichenroutine.

libcurl bietet eine portable C-API für HTTP(S). Artwork-Abfragen brauchen Abbruch, Zeitlimits, Rate-Limit-Behandlung und wiederverwendbaren Cache. Die konkrete asynchrone Einbindung kann über einen Worker oder eine passende Event-Loop-Integration erfolgen. Netzwerkzugriffe dürfen weder Spielstart noch Offline-Nutzung voraussetzen.

## SDL2, Vulkan und Bildformate

Die untersuchte AnyPS5-Runtime verwendet SDL2 und Vulkan. Diese gehören zuerst zum Spielprozess. FrameRift benötigt SDL2 nur dann selbst als Abhängigkeit, wenn ein konkreter Adapter oder eine eigene Controller-Navigation darauf aufbaut. Ein Profiler-Diagramm muss nicht die Grafik-API des Spiels übernehmen.

SteamGridDB liefert statische und animierte Bildvarianten. Die tatsächlich benötigten Loader für PNG, JPEG, WebP und animiertes WebP werden am gewählten Toolkit geprüft. libwebp kann als zusätzlicher Decoder infrage kommen, falls die vorhandenen Loader das benötigte Format nicht ausreichend unterstützen. Die Existenz eines WebP-Decoders allein garantiert keine korrekte Animation, Frame-Disposition oder günstige GPU-Nutzung.

Die integrierte Spieloberfläche ist ein eigener Plattformbereich. Ein normaler Vulkan-Header oder ein SDL-Linkziel löst nicht automatisch das Teilen einer Spieloberfläche zwischen zwei Prozessen. Dafür gelten die im [Fensterdokument](game-window-integration.md) beschriebenen Anforderungen.

## Tracy und FrameRift-Profiler

Tracy ist ein möglicher Entwicklungshelfer und unterstützt unter anderem CPU- und Vulkan-GPU-Profiling. Damit können gezielte instrumentierte Builds und der Einfluss des Launchers untersucht werden. Es ist keine automatische Quelle für alle im Design gezeigten Messwerte und kein bereits vorhandener FrameRift-Backend-Vertrag.

FrameRifts Benutzeroberfläche braucht auch ohne eine umfassende Tracy-Integration verständliche, korrekt zugeordnete Daten. Ein späterer Import oder Export zu externen Profilern kann sinnvoll sein, sobald Sitzungsformat und Messsemantik stabil sind.

## Versionen und Auslieferung

Vor dem ersten implementierten Prototyp werden konkrete unterstützte Versionen festgelegt. Dann sollen reproduzierbare Build-Definitionen und CI für Windows und Linux entstehen. Linux/X11 und Linux/Wayland werden gesondert geprüft, weil erfolgreiche Paketinstallation keine identischen Fähigkeiten bei der Spielfensterintegration bedeutet.

Ein Packaging-Spike entscheidet zwischen passenden Werkzeugen für den GTK- oder Qt-Pfad. MSYS2, gvsbuild, Paketmanager oder andere Dependency-Strategien sind zu bewerten; es gibt derzeit keine festgelegte Kombination mit einem bestimmten Windows-Compiler. Ein Endnutzerpaket sollte seine nötigen Laufzeitbibliotheken nachvollziehbar mitbringen.

Ein vollständiges Lizenzinventar wird mit der tatsächlich gewählten Abhängigkeitsliste erstellt. Besonders ein Toolkit-Wechsel muss die jeweiligen Auslieferungsbedingungen berücksichtigen. Eine pauschale Projektlizenz ist in dieser Planungsphase noch nicht beschlossen.

## Primärquellen

- [GTK4-Dokumentation](https://docs.gtk.org/gtk4/) und [GTK unter Windows](https://www.gtk.org/docs/installations/windows)
- [libadwaita-Dokumentation](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/)
- [GIO-Dokumentation](https://docs.gtk.org/gio/)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [CMake](https://cmake.org/cmake/help/latest/guide/tutorial/index.html) und [Ninja](https://ninja-build.org/)
- [libcurl](https://curl.se/libcurl/)
- [nlohmann/json](https://nlohmann.github.io/json/) und [CMake-Einbindung](https://nlohmann.github.io/json/integration/cmake/)
- [SQLite als Anwendungsformat](https://www.sqlite.org/appfileformat.html)
- [spdlog](https://github.com/gabime/spdlog)
- [zstd](https://github.com/facebook/zstd)
- [Tracy](https://github.com/wolfpld/tracy)
- [libwebp](https://github.com/webmproject/libwebp)
- [Catch2](https://github.com/catchorg/Catch2)
