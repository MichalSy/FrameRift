# Beiträge zu FrameRift

FrameRift entwickelt zuerst ausschließlich die native GTK4-Spielebibliothek und ihren SQLite-Cache. Demo-Einträge verwenden echte Katalogspiele und lokal gecachte SteamGridDB-Bilder. Build, Start und isolierte Cache-Nutzung stehen in der [README](README.md). Weitere Screens und Integrationen bleiben geplant.

Lies vor jeder Implementierung [AGENTS.md](AGENTS.md). Halte Änderungen klein, schreibe Code und UI-Texte auf Englisch und verwende vorhandene Domänen: Datenzugriff in `src/library`, GTK-Oberfläche in `src/ui`. Prüfe neue Dateien auf ihre fachliche Zuordnung und vermeide zusätzliche Abstraktionen ohne aktuellen Bedarf.

Im aktuellen Abschnitt sind Darstellung, Suche, Sortierung, Auswahl und Cache-Erhalt relevant. Spätere technische Versuche zu Spielstart, Fensterintegration, Eingabefokus und Telemetrie sollten Plattform, Display-Backend, Compiler, Toolkit und den verwendeten AnyPS5-Quellenstand nennen. Beschreibe das beobachtete Verhalten und die Grenzen des Versuchs. Eine funktionierende Windows- oder X11-Lösung ist noch kein Nachweis für native Wayland-Fenstereinbettung.

Bei Dokumentationsänderungen bitte zwischen vorhandenem Upstream-Verhalten, FrameRift-Plan und ungeprüfter Annahme unterscheiden. Quellverweise auf AnyPS5 sollten einen Commit verwenden, wenn ein konkretes Verhalten beschrieben wird. Für Bibliotheken sind offizielle Dokumentationen und Projektquellen geeignete Belege.

Die UI-Richtung steht in [Design und Bedienung](docs/design.md). Eine Änderung an der Technik sollte die Produktziele weiter unterstützen: native Windows-/Linux-Anwendung, getrennte Bibliotheks- und Spielansicht, unmittelbare Bedienung und korrekt interpretierte Messwerte.

Spieldateien, private API-Schlüssel, Nutzerkonfigurationen, umfangreiche Capture-Daten und unzugeordnete Artwork-Sammlungen gehören nicht in Beiträge. Spielgrafiken stammen aus der API; eigene oder generierte Ersatzillustrationen sind nicht vorgesehen. Für Bildmaterial bitte Fundstelle, Urheberangaben soweit verfügbar und Rechtekontext dokumentieren. Der SteamGridDB-Schlüssel wird über `STEAMGRIDDB_API_KEY` übergeben und gehört weder in Kommandoargumente noch in Datenbank oder Logs. FrameRift code and documentation use the [MIT license](LICENSE); contributions must be compatible with it.

Der aktuelle Umfang steht in der [Roadmap](docs/roadmap.md). Führe keine umfangreiche Abhängigkeitskette allein für einen UI-Effekt ein. Nutze wenige aussagekräftige Datenchecks und einen nativen UI-Smoke-Check; für die Vorschau ist kein Testframework erforderlich. Bewahre vorhandene Cache-Daten und deaktiviere unverfügbare Aktionen. Erklärungen zu Demo-Daten und Artwork-Anbietern gehören nicht in die Bibliotheksoberfläche.
