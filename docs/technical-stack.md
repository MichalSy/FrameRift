# Bibliotheken und Build

## Status

Die Spielebibliothek ist mit C++20, CMake, GTK4, SQLite, libcurl und JSON-GLib implementiert. Build- und Startbefehle stehen in der [README](../README.md). Die Entwicklungsplattform ist Linux. Windows-Build und Auslieferung sind noch ungeprüft. Weitere Screens sind nicht Teil des aktuellen Abschnitts.

## Eingebaute Bausteine

| Bereich | Baustein | Aufgabe |
| --- | --- | --- |
| Sprache | C++20 | Bibliotheksdomäne und native UI |
| Build | CMake ab 3.20, Ninja, pkg-config | Programm, Cache-Check und Ressourcen bauen |
| UI | GTK4 ab 4.12 | Native Widgets, Rendering und Eingabe über die C-API |
| GTK-Umfeld | GLib/GObject/GIO | Anwendung, Signals, Ressourcen und Cache-Verzeichnis |
| Lokaler Cache | SQLite3 | Spielmetadaten, SteamGridDB-Zuordnung und lokale Bildpfade |
| HTTP | libcurl | SteamGridDB-API und Bild-Downloads außerhalb der UI-Callbacks |
| JSON | JSON-GLib | SteamGridDB-Antworten mit vorhandenen GLib-Bausteinen lesen |
| Ressourcen | `glib-compile-resources` | CSS und App-Zeichen als GResource einbetten |
| Checks | CTest und kleines C++-Programm | Cache-Verhalten ohne Testframework prüfen |

Es gibt einen nativen UI-Prozess und keine Browser-Laufzeit. Weitere Abhängigkeiten werden erst bei einem konkreten Bedarf hinzugefügt. Die Toolkit-Abwägung für spätere Plattformintegration steht in [Toolkit und Plattformen](toolkit-and-platforms.md).

## Weitere vorgeschlagene Bausteine

| Bereich | Vorschlag | Aufgabe und Einordnung |
| --- | --- | --- |
| Anwendungsbausteine | libadwaita | Adaptive Komponenten und Anwendungsstruktur; einzelne Komponenten passend zum eigenen Design auswählen |
| UI-Alternative | Qt 6 + Qt Quick/QML | Native, individuell gestaltbare UI und plattformbezogene Fensterintegration; Alternative beim Toolkit-Gate |
| Eigene Logs | spdlog, optional | Strukturierte Diagnose von FrameRift; Spielausgabe bleibt separat zuordenbar |
| Capture-Kompression | zstd, optional | Größere Messaufzeichnungen in einem Worker komprimieren |
| Entwicklungsprofiling | Tracy, optional | FrameRift-Overhead und gezielte Runtime-Instrumentierung untersuchen |

GTK4/libadwaita und Qt sind alternative Frontend-Wege. Beide parallel in die erste Version einzubauen wäre ohne konkreten Nutzen unnötiger Aufwand. Ebenso sollte ein HTTP-Client ausgewählt werden, statt libcurl und weitere Clients für dieselbe Aufgabe vorzusehen.

## GTK4 und libadwaita im C++-Projekt

GTK4 und libadwaita stellen C-APIs bereit. Die Vorschau verwendet GTK4 direkt aus C++. Besitzverhältnisse von GObject-Referenzen und Callback-Lebensdauer bleiben explizit; C++-Ausnahmen dürfen keine C-Callbacks verlassen. gtkmm und libadwaita sind nicht eingebaut und werden nur bei einem konkreten Nutzen erwogen.

Die eigene Gestaltung soll nicht aus einer großen Menge undokumentierter Theme-Overrides bestehen. Wiederverwendbare Karten, Hero-Bereich, Toolbars, Zeitleiste und Frame-Details brauchen eine bewusst entworfene Komponentenbasis. libadwaita wird nach konkretem Nutzen ausgewählt und legt nicht automatisch jedes Detail der Oberfläche fest.

Windows-Unterstützung ist sowohl eine Backend- als auch eine Auslieferungsfrage. DLLs, Bild-Loader, Ressourcen, Schriftverhalten und Icons müssen in einem sauberen Paket funktionieren. Eine auf dem Entwicklungsrechner sichtbare Oberfläche ist noch keine validierte Endnutzerinstallation.

## Datenverarbeitung

JSON-GLib liest derzeit die SteamGridDB-Antworten. Die Bibliothek verwendet damit vorhandene GLib-Bausteine und braucht keine zweite JSON-Bibliothek. Ein späterer Hochfrequenzkanal muss seine tatsächliche Nachrichtenrate und Kosten separat prüfen. Für große Ereignismengen kann ein kompakteres, versioniertes Format sinnvoll werden. Ein Formatwechsel darf die Bedeutung der Messwerte nicht verändern.

SQLite speichert den Katalog und Indizes. Die primäre Messdatenablage wird unabhängig davon entworfen. Für Capture-Kompression ist zstd ein möglicher Baustein; Kompression und Dateischreiben bleiben außerhalb des Spielthreads und der UI-Zeichenroutine.

libcurl verarbeitet die HTTPS-Abfragen und Bild-Downloads. Metadaten und Bilder liegen anschließend im lokalen Cache. Der Zugangsschlüssel wird zur Laufzeit über `STEAMGRIDDB_API_KEY` übergeben; Cache und Diagnose enthalten ihn nicht. Netzwerkzugriffe dürfen die GTK-Oberfläche nicht blockieren. Weitere Anforderungen wie manuelle Zuordnung, Aktualisierung und Rate-Limit-Verhalten gehören in kleine, gezielte Erweiterungen.

## SDL2, Vulkan und Bildformate

Die untersuchte AnyPS5-Runtime verwendet SDL2 und Vulkan. Diese gehören zuerst zum Spielprozess. FrameRift benötigt SDL2 nur dann selbst als Abhängigkeit, wenn ein konkreter Adapter oder eine eigene Controller-Navigation darauf aufbaut. Ein Profiler-Diagramm muss nicht die Grafik-API des Spiels übernehmen.

SteamGridDB liefert statische und animierte Bildvarianten. Die tatsächlich benötigten Loader für PNG, JPEG, WebP und animiertes WebP werden am gewählten Toolkit geprüft. libwebp kann als zusätzlicher Decoder infrage kommen, falls die vorhandenen Loader das benötigte Format nicht ausreichend unterstützen. Die Existenz eines WebP-Decoders allein garantiert keine korrekte Animation, Frame-Disposition oder günstige GPU-Nutzung.

Die integrierte Spieloberfläche ist ein eigener Plattformbereich. Ein normaler Vulkan-Header oder ein SDL-Linkziel löst nicht automatisch das Teilen einer Spieloberfläche zwischen zwei Prozessen. Dafür gelten die im [Fensterdokument](game-window-integration.md) beschriebenen Anforderungen.

## Tracy und FrameRift-Profiler

Tracy ist ein möglicher Entwicklungshelfer und unterstützt unter anderem CPU- und Vulkan-GPU-Profiling. Damit können gezielte instrumentierte Builds und der Einfluss des Launchers untersucht werden. Es ist keine automatische Quelle für alle im Design gezeigten Messwerte und kein bereits vorhandener FrameRift-Backend-Vertrag.

FrameRifts Benutzeroberfläche braucht auch ohne eine umfassende Tracy-Integration verständliche, korrekt zugeordnete Daten. Ein späterer Import oder Export zu externen Profilern kann sinnvoll sein, sobald Sitzungsformat und Messsemantik stabil sind.

## Versionen und Auslieferung

Die Mindestversionen der eingebauten Bausteine stehen oben und werden von CMake geprüft. CI und Endnutzerpakete für Windows und Linux sind noch geplant. Linux/X11 und Linux/Wayland brauchen getrennte Integrationsprüfungen, weil erfolgreiche Paketinstallation keine identischen Fähigkeiten bei der Spielfensterintegration bedeutet.

Ein Packaging-Spike entscheidet zwischen passenden Werkzeugen für den GTK- oder Qt-Pfad. MSYS2, gvsbuild, Paketmanager oder andere Dependency-Strategien sind zu bewerten; es gibt derzeit keine festgelegte Kombination mit einem bestimmten Windows-Compiler. Ein Endnutzerpaket sollte seine nötigen Laufzeitbibliotheken nachvollziehbar mitbringen.

Ein vollständiges Lizenzinventar wird mit der Auslieferung erstellt. Besonders ein Toolkit-Wechsel muss die jeweiligen Auslieferungsbedingungen berücksichtigen. Original FrameRift code and documentation use the [MIT license](../LICENSE); dependencies retain their own licenses.

## Primärquellen

- [GTK4-Dokumentation](https://docs.gtk.org/gtk4/) und [GTK unter Windows](https://www.gtk.org/docs/installations/windows)
- [libadwaita-Dokumentation](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/)
- [GIO-Dokumentation](https://docs.gtk.org/gio/)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [CMake](https://cmake.org/cmake/help/latest/guide/tutorial/index.html) und [Ninja](https://ninja-build.org/)
- [libcurl](https://curl.se/libcurl/)
- [JSON-GLib](https://gnome.pages.gitlab.gnome.org/json-glib/)
- [SQLite als Anwendungsformat](https://www.sqlite.org/appfileformat.html)
- [spdlog](https://github.com/gabime/spdlog)
- [zstd](https://github.com/facebook/zstd)
- [Tracy](https://github.com/wolfpld/tracy)
- [libwebp](https://github.com/webmproject/libwebp)
