# Toolkit und Zielplattformen

[Zur Projektübersicht](../README.md) · [Spielintegration](game-window-integration.md) · [Artwork](artwork.md)

## Status und feste Anforderungen

FrameRift ist ein geplantes eigenständiges Desktop-Frontend für eine lokale Spielebibliothek und eine separate Profiler-Ansicht. Dieses Dokument beschreibt eine Auswahlgrundlage. Es bestätigt weder eine implementierte Anwendung noch installierte oder getestete Abhängigkeiten.

Festgelegt sind:

- Windows und Linux als Zielplattformen.
- Eine native Desktop-Anwendung mit flüssigen Animationen und niedriger Hintergrundlast.
- Eine individuelle visuelle Identität mit Graphitflächen, Cyanakzenten und großzügiger Bildwirkung.
- Getrennte Bibliothek und Spielansicht; der Arbeitsmodus für das laufende Spiel heißt **Profiler**.
- In der Bibliothek stehen zuletzt gespielte Spiele zuerst. Cover und Hero-Artwork kommen primär von SteamGridDB.
- Die Oberfläche wird lokal gerendert. Ein Browser, eine WebView oder ein Webserver gehört nicht zum geplanten UI-Stack.

Die bisherige Arbeitsrichtung **GTK4 mit libadwaita** bleibt offen. **Qt 6 Quick/QML** ist eine ernsthafte Alternative, insbesondere für die Einbettung fremder nativer Fenster. Die Entscheidung soll aus einem kleinen Plattformvergleich hervorgehen, bevor ein vollständiges Frontend entsteht.

## Was native Performance hier bedeutet

Ein Toolkitname ist kein Performance-Nachweis. Für FrameRift gelten folgende vorgeschlagene Kriterien:

1. Die Bibliothek reagiert beim Scrollen und Wechseln des ausgewählten Spiels ohne blockierende Dateizugriffe oder Netzwerkanfragen.
2. Cover werden passend zur sichtbaren Größe dekodiert. Unsichtbare Karten und Animationen verursachen keine fortlaufende Arbeit.
3. Ein animierter Hero wird kontrolliert dekodiert und präsentiert. Mehrere große Animationen laufen nicht automatisch gleichzeitig.
4. Messdaten erreichen die Profiler-Ansicht in begrenzten Aktualisierungsraten. Die Oberfläche muss nicht für jedes Messereignis neu zeichnen.
5. Der Spielprozess bleibt unabhängig vom UI-Prozess. Ein UI-Stall darf nicht die Spiellogik anhalten.
6. CPU-, GPU- und Speicherverbrauch werden mit ruhender Bibliothek, mit Animation und während einer Messung verglichen.

Für flüssige Darstellung ist das Budget der tatsächlichen Bildschirmrate maßgeblich: Ein 60-Hz-Ausgabegerät bietet ungefähr 16,7 ms pro Bild, ein 120-Hz-Gerät ungefähr 8,3 ms. Diese Werte sind Rechenbeispiele für die spätere Messung, keine zugesagten FrameRift-Ergebnisse.

## Kandidat A: GTK4 und libadwaita

GTK4 erstellt einen GSK-Szenengraphen und besitzt Renderer für OpenGL, Vulkan und Cairo. Der Frame-Clock-Mechanismus koordiniert Eingabe, Animation, Layout und Zeichnen. Diese APIs bieten eine Grundlage für animierte lokale UI-Elemente; der tatsächlich verwendete Renderer und dessen Kosten müssen auf den Zielsystemen gemessen werden. [GTK-Zeichenmodell](https://docs.gtk.org/gtk4/drawing-model.html)

GTK besitzt ein Win32-Backend auf Windows. Die offizielle Installationsanleitung beschreibt unter anderem MSYS2. Daraus folgt eine mögliche Windows-Buildrichtung, aber noch kein fertiges FrameRift-Paket und keine bestätigte libadwaita-Laufzeit auf jedem gewünschten Windows-System. [GTK unter Windows](https://docs.gtk.org/gtk4/windows.html), [Installation auf Windows](https://www.gtk.org/docs/installations/windows/)

Libadwaita liefert unter anderem Dark-Style-Verwaltung, zusätzliche Widgets, CSS-Variablen und Möglichkeiten für eigene Akzentfarben. Eigene Darstellung muss auch hohen Kontrast und lesbare Zustände berücksichtigen. Für FrameRift ist deshalb ein app-eigenes Graphit/Cyan-Design mit klar begrenzten CSS-Klassen vorgeschlagen. Interna von Standardwidgets sollten nicht großflächig überschrieben werden. [Styles und Appearance](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/styles-and-appearance.html)

### Vorgeschlagene technische Form

- C++20 für Bibliotheksmodell, Prozessverwaltung, Caches und Integrationsadapter.
- GTK- und libadwaita-**C-APIs** aus C++, mit kleinen eigenen RAII-Hilfen für Lebenszeiten.
- GLib/GObject/GIO für Eventloop, Signale, asynchrones I/O und Prozessverwaltung.
- GTK-Builder-Ressourcen und app-eigene Styles für die Oberfläche; Detailformat erst nach der Toolkitentscheidung.
- GTK-Listenmodelle und geeignete List-/Grid-Views für große Bibliotheken statt einer unbegrenzt wachsenden Ansammlung dauerhaft aktiver Widgets. [GtkListView](https://docs.gtk.org/gtk4/class.ListView.html)

Ein C++-Wrapper für libadwaita, etwa ein angenommener `libadwaitamm`-Stack, ist **keine bestätigte Abhängigkeit**. Eine spätere Wrapperentscheidung muss Verfügbarkeit, Pflege, benötigte API-Abdeckung und Windows-Paketierung separat prüfen. Der Plan setzt keinen solchen Wrapper voraus.

Die meisten GTK-Objekte dürfen nur im Hauptthread verwendet werden. Lang laufende Scans, Downloads und Dekodierung gehören in asynchrone Operationen oder Worker; Ergebnisse werden kontrolliert in den Hauptthread übernommen. [GTK-Threading](https://docs.gtk.org/gtk4/section-threading.html)

### Offene Punkte

- Ein individuell gestalteter Gaming-Launcher soll nicht allein durch die Standardanordnung einer GNOME-App definiert werden. Libadwaita ist ein möglicher Baustein, kein fertiges FrameRift-Layout.
- Windows-Paketierung einschließlich Fonts, Bilddekodern und benötigter Laufzeitbibliotheken muss auf einem sauberen Testsystem funktionieren.
- GTK3s `GtkSocket`/`GtkPlug` waren ein X11-spezifischer Weg zur prozessübergreifenden Einbettung. Die GTK4-Planung kann diese GTK3-APIs nicht voraussetzen; ein moderner Plattformadapter oder ein anderer Bildtransport ist erforderlich. [GTK3 GtkSocket](https://docs.gtk.org/gtk3/class.Socket.html), [GTK4-API](https://docs.gtk.org/gtk4/)
- Ein GTK-Widget als rechteckiger Platzhalter erzeugt nicht automatisch ein natives Kindfenster für SDL. Fokus, Clipping, Skalierung und Lebenszeit müssen für den konkreten Integrationsweg nachgewiesen werden.

## Kandidat B: Qt 6 Quick/QML

Qt Quick stellt eine lokal gerenderte Szenengraph-Oberfläche bereit. Qt 6 verwendet verschiedene native Grafik-Backends über seine Rendering-Abstraktion. QML beschreibt UI-Struktur, Zustände und Animationen; C++ kann das Daten- und Integrationsmodell liefern. **Qt Quick/QML ist keine Weboberfläche.** Das gesonderte Chromium-basierte Qt-WebEngine-Modul ist für FrameRift nicht vorgesehen. [Qt-Graphics](https://doc.qt.io/qt-6/topics-graphics.html), [Qt-Quick-Szenengraph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html), [Qt-WebEngine-Abgrenzung](https://doc.qt.io/qt-6/qtwebengine-overview.html)

Die relevante Besonderheit ist `QWindow::fromWinId()`: Qt kann eine lokale Darstellung eines fremden nativen Fensters erstellen, auch wenn dieses aus einem anderen Prozess stammt. Die Unterstützung hängt vom Plattformplugin ab und kann fehlschlagen. Qt übernimmt dabei keine allgemeine Zustandsbeobachtung des fremden Fensters. [QWindow::fromWinId](https://doc.qt.io/qt-6/qwindow.html#fromWinId)

Seit Qt 6.8 gibt es `WindowContainer` in Qt Quick. Das Element verwaltet die Geometrie eines eingebetteten `QWindow`. Das Kindfenster liegt jedoch über der Quick-Szene; gewöhnliche QML-Items können es nicht automatisch überlagern. Für FrameRift spricht das für Profiler-Panels neben und unter dem Spielfeld, während ein Overlay im Spielfeld einen gesonderten Nachweis braucht. [WindowContainer](https://doc.qt.io/qt-6/qml-qtquick-windowcontainer.html)

### Vorgeschlagene technische Form

- C++20 für das gemeinsame Fachmodell und Plattformadapter.
- QML/Qt Quick für Bibliothek, Moduswechsel und Profiler-Panels.
- App-eigene Qt-Quick-Controls-Komponenten für Graphit/Cyan statt ausschließlich eines Standardstyles. [Qt Quick Controls anpassen](https://doc.qt.io/qt-6/qtquickcontrols-customize.html)
- Qt Core/Gui/Quick/Qml und bei Bedarf Network als Kandidaten für die Laufzeit.
- Keine zusätzliche GLib-Hauptschleife allein aus Gewohnheit: Bei einer Qt-Entscheidung werden Prozess-, Netzwerk- und Eventloop-Abstraktionen passend zum Qt-Stack bewertet.

### Offene Punkte

- Die bequeme API ersetzt keine Windows-/X11-/Wayland-Prüfung.
- Ein fremdes Fenster ist keine Textur in der UI-Szene; Transformationen, Rundungen und Überlagerungen gelten nicht automatisch für den Spielinhalt.
- Animiertes WebP muss mit den tatsächlich ausgelieferten Bildplugins und ihrer Animation-Unterstützung geprüft werden.
- Benötigte Qt-Module, Plugins, Laufzeitverteilung und deren jeweilige Lizenzbedingungen werden vor der finalen Auswahl festgelegt. [Qt-Lizenzübersicht](https://doc.qt.io/qt-6/licensing.html)

## Vergleich für die Entscheidung

| Kriterium | GTK4 + libadwaita | Qt 6 Quick/QML |
|---|---|---|
| Bisherige Arbeitsrichtung | Ausgangspunkt der bisherigen UI-Idee | Vollwertige Alternative |
| Individuelles Graphit/Cyan-Design | Eigene Widgets/Styles; libadwaita gezielt nutzen | Eigene Quick-Controls und Komponenten |
| Lokale Animationen | Frame-Clock/GSK; tatsächlichen Renderer messen | Quick-Szenengraph; tatsächlichen Backend messen |
| Windows | Win32-Backend dokumentiert; Paketierung offen | Zielplattform und Plugin-Paketierung prüfen |
| Linux | X11 und Wayland getrennt prüfen | XCB und Wayland getrennt prüfen |
| Fremdes Windows-/X11-Fenster | Eigener Plattformweg mit höherem Nachweisaufwand | `fromWinId` + `WindowContainer` als konkreter Kandidat |
| Fremdes natives Wayland-Fenster | Kein pauschaler Reparent-Weg | Auch Qt hebt Wayland-Grenzen nicht auf |
| C++-API-Plan | GTK/libadwaita-C-API aus C++; Wrapper offen | Qt-C++-API plus QML |
| Entscheidung | Noch offen | Noch offen |

Die Einträge sind eine technische Bewertung für FrameRift. Sie sind keine Rangfolge der Verbreitung, keine Benchmark-Ergebnisse und keine Zusage, dass ein komplettes Einbetten auf allen Plattformen gelingt.

## Abhängigkeiten und Buildrichtung

**C++20, CMake und Ninja sind eine plausible Arbeitsannahme.** Noch gibt es daraus keine implementierte Buildkonfiguration, keinen Lockfile und keine festgelegte Mindestversion. CMake unterstützt die Anforderung von C++20; die finale Konfiguration soll eine unerwünschte Herabstufung des Standards verhindern. [CMake CXX_STANDARD](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html), [CMake Ninja-Generator](https://cmake.org/cmake/help/latest/generator/Ninja.html)

| Bestandteil | Status | Vorgesehener Zweck | Vor Festlegung nachzuweisen |
|---|---|---|---|
| C++20-Compiler | Kandidat | Gemeinsamer Kern und Adapter | Windows-/Linux-Toolchains und Warnungsregeln |
| CMake + Ninja | Kandidat | Build und reproduzierbare Konfiguration | Mindestversion, Presets und Paketierung |
| GTK4 | Toolkit-Kandidat | Native Oberfläche | Renderer, Windows-Paket und Fensteradapter |
| libadwaita | Zusatz zum GTK-Kandidaten | Widgets, Styles und Animationen | Exakte APIs, stabile Version und Windows-Verteilung |
| GLib/GIO | Zum GTK-Weg geplant | Eventloop, Dateien und Prozesse | Benötigte APIs und asynchrone Lebenszeiten |
| GTK-/libadwaita-C++-Wrapper | Nicht vorausgesetzt | Optionale API-Ergonomie | Pflege, API-Abdeckung und Zusatzkosten |
| Qt 6 Quick/QML | Alternativer Toolkit-Kandidat | Eigene UI und Fenstercontainer | Version ab 6.8 bei Nutzung von WindowContainer; Plattformtests |
| libwebp / Animationsdecoder | Kandidat | Animierte Hero-Bilder | Dekodierung, Speichergrenzen und Paketierung |
| HTTP-/JSON-Bibliothek | Noch auszuwählen | SteamGridDB und Metadaten | Auswahl passend zum Toolkit; asynchron und begrenzt |
| SQLite oder anderes lokales Speicherformat | Noch auszuwählen | Bibliothek, Zuordnung und Verlauf | Migration, Crash-Verhalten und lokale Datenhaltung |
| SDL/Vulkan im Frontend | Keine pauschale Abhängigkeit | Nur bei nachgewiesenem Integrationsbedarf | Frontend ist nicht der Renderer des Spielprozesses |

Eine Abhängigkeit im Spielprozess ist nicht automatisch eine Abhängigkeit des Frontends. Die Verbindung erfolgt über ein kleines, versioniertes Protokoll und plattformspezifische Integrationsdaten; siehe [Spielintegration](game-window-integration.md).

## Vorgeschlagene Auswahlprüfung

Vor produktiver UI-Implementierung sollen beide Kandidaten dieselbe kleine Referenz erfüllen:

1. Bibliothek mit realistischen Covergrößen, zuletzt gespielten Spielen zuerst und einem animierten Hero.
2. Profiler-Layout mit einem großen Spielfeld, Analyse rechts und Messdaten unten.
3. Externer kooperativer SDL-Testprozess unter Windows und Linux/X11.
4. Größenänderung, Fensterwechsel, DPI-Wechsel und Fokuswechsel zwischen Spiel und Profiler-Bedienelementen.
5. Ein- und Ausstieg aus dem Profiler, normaler Spielabschluss und unerwartetes Prozessende.
6. Expliziter Wayland-Test mit dokumentiertem Ergebnis und funktionierender externer Fensterdarstellung.
7. Sauberes Windows-Paket und eine festgelegte Linux-Paketierungsprobe.

Erst danach werden Toolkit, Mindestversionen, aktive Renderer und die Reihenfolge der Integrationswege beschlossen. Reine Bibliotheks-Animationen sind ein eigener Test; eine funktionierende Bibliothek beweist noch keine fehlerfreie Einbettung des Spiels.
