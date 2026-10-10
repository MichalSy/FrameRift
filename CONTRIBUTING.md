# Beiträge zu FrameRift

FrameRift befindet sich in der Design- und Planungsphase. Die zwei UI-Entwürfe und die technischen Dokumente sind die aktuelle Grundlage. Es gibt noch keinen Anwendungsbuild, dessen Installation oder Tests man ausführen könnte.

Besonders hilfreich sind konkrete Ergebnisse zu Spielstart, Fensterintegration, Eingabefokus und Telemetrie. Ein kleiner technischer Versuch sollte seine Plattform, Display-Backend, Compiler, Toolkit und den verwendeten AnyPS5-Quellenstand nennen. Beschreibe das beobachtete Verhalten und die Grenzen des Versuchs. Eine funktionierende Windows- oder X11-Lösung ist noch kein Nachweis für native Wayland-Fenstereinbettung.

Bei Dokumentationsänderungen bitte zwischen vorhandenem Upstream-Verhalten, FrameRift-Plan und ungeprüfter Annahme unterscheiden. Quellverweise auf AnyPS5 sollten einen Commit verwenden, wenn ein konkretes Verhalten beschrieben wird. Für Bibliotheken sind offizielle Dokumentationen und Projektquellen geeignete Belege.

Die UI-Richtung steht in [Design und Bedienung](docs/design.md). Eine Änderung an der Technik sollte die Produktziele weiter unterstützen: native Windows-/Linux-Anwendung, getrennte Bibliotheks- und Spielansicht, unmittelbare Bedienung und korrekt interpretierte Messwerte.

Spieldateien, private API-Schlüssel, Nutzerkonfigurationen, umfangreiche Capture-Daten und unzugeordnete Artwork-Sammlungen gehören nicht in Beiträge. Für Bildmaterial bitte Fundstelle, Urheberangaben soweit verfügbar und Rechtekontext dokumentieren. Die Auswahl einer Projektlizenz ist noch offen.

Vor einer Implementierung wird der kleine erste Umfang anhand der [Roadmap](docs/roadmap.md) festgelegt. Bitte keine umfangreiche neue Abhängigkeitskette allein für einen isolierten UI-Effekt einführen. Neue Tests sollten echte Semantik prüfen, etwa Frame-Korrelation, Parserfehler oder Startzustände.
