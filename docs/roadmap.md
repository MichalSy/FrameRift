# Roadmap

## Zweck

Die Planung hält das gewünschte Produkt und die offenen technischen Fragen fest. Die Phasen sind keine Terminzusagen. Insbesondere die Spielfensterintegration und die Messschnittstelle müssen mit realen Programmen geprüft werden.

## Aktueller Fokus: Spielebibliothek und Cache

- Die native GTK4-Bibliothek liest ihre Einträge aus SQLite und bietet Hero-Auswahl, Suche, Sortierung und zuletzt gespielte Titel vor der Sammlung.
- Demo-Einträge verwenden reale Katalogspiele mit Covern, Hero-Bildern und Logos aus der SteamGridDB-API. Spielzeiten dienen der UI-Vorschau.
- Metadaten und Bilder werden lokal gecacht; die vorhandene Bibliothek bleibt offline nutzbar. Der API-Key kommt aus der Prozessumgebung und wird nicht im Cache gespeichert.
- Dieser Abschnitt enthält ausschließlich die Bibliothek. Spielansicht, Profiler, Spielstart, Fenstereinbettung und Messdaten bleiben geplant.
- Ein kleiner Cache-Check prüft Datenverhalten; die Oberfläche wird mit dem Referenzentwurf in einem virtuellen Desktop verglichen.

Dieser Umfang wird hinsichtlich Darstellung, Suche, Sortierung, Auswahl und Cache-Erhalt geprüft. Die folgenden Integrationsversuche sind spätere Planung. Weitere Module und Screens entstehen erst mit einem konkreten Bedarf.

## Phase 0: Technische Machbarkeit

### A. Spielstart und Metadaten

Ein kleines Programm startet eine vorhandene AnyPS5-Ausgabe mit explizitem Programm, Arbeitsverzeichnis und Argumenten. Es liest die verfügbaren Titelmetadaten, erfasst Logs und beobachtet den Prozesszustand.

**Erfolgskriterien:** Ein normaler Start ist reproduzierbar; Startfehler werden verständlich ausgegeben; Prozessende und Exit-Code stimmen; ein fehlendes Icon oder unvollständige Metadaten verhindern den Start nicht. Es wird festgehalten, welche Windows- und Linux-Ausgabe verwendet wurde.

### B. Spielfenster, Toolkit und Eingabe

Ein minimales Host-Fenster erhält einen Bereich für das laufende Spiel. Zunächst wird die aktuelle Arbeitsrichtung GTK4/libadwaita geprüft. Falls deren native Fensteranbindung den gewünschten Weg nicht zuverlässig ermöglicht, wird derselbe Versuch mit Qt 6 verglichen. Es geht um einen kleinen Integrationsnachweis, nicht um zwei vollständige Oberflächen.

Die Matrix enthält Windows und Linux/X11. Eine Wayland-Sitzung wird separat betrachtet, insbesondere der Weg über XWayland für den aktuellen X11-basierten AnyPS5-Build. Die Display- und Toolkit-Backends des Host- und Spielprozesses werden protokolliert.

**Erfolgskriterien:** Spielbild ohne problematische zusätzliche Bildkopie, korrektes Resize und DPI-Verhalten, gezielter Fokuswechsel zwischen Spiel und UI, funktionierende Tastatur/Maus/Xbox-Eingabe, nachvollziehbarer Vollbildpfad und sauberes Ende bei einem Spielabbruch. Wenn eine Plattform nur ein separates Fenster unterstützt, wird das Ergebnis ausdrücklich so dokumentiert.

**Entscheidung danach:** Toolkit, unterstützte erste Plattformkonfigurationen und Fensterstrategie festlegen. Eine verlässliche, eingebettete Spielansicht ist das gewünschte Produkt. Ein externes Fenster kann den frühen Launcher nutzbar machen, ersetzt aber keinen Nachweis dieses Ziels.

### C. Kleine Telemetrieschnittstelle

Ein optionaler Sender liefert einen Handshake und einen begrenzten Satz strukturierter Ereignisse. Der Empfänger kann Frames zuordnen und mit verzögert eintreffenden GPU-Daten umgehen. Vorhandene Timing-Dateien dienen als Referenz, nicht als unbegrenzt stabile API.

**Erfolgskriterien:** Messpunkt und Zeitbasis sind erklärt; Frame-IDs sind nachvollziehbar; Verbindungsabbruch und langsamer Empfänger blockieren das Spiel nicht; verworfene Ereignisse werden gezählt; Baseline und instrumentierter Lauf erlauben eine Aussage zum Overhead. Unverfügbare Werte werden nicht synthetisch als Messung ausgegeben.

### D. Artwork und Rendering

Ein Test importiert Metadaten eines Titels, sucht auf SteamGridDB, speichert eine manuelle Zuordnung und lädt Cover, Hero und Logo in einen Cache. Mindestens ein animiertes WebP wird unter den geplanten Plattformen geprüft.

**Erfolgskriterien:** Offline-Anzeige aus dem Cache, korrigierbarer Fehlmatch, gute Lesbarkeit des Hero-Textes, verlässliches Pausieren einer Animation und keine blockierende Dekodierung auf dem UI-Thread. API-Zugang und Paketierung der Bild-Loader sind nachvollziehbar.

## Phase 1: Erste nutzbare Bibliothek

Der erste begrenzte Umfang umfasst Spielimport, Suche, Hero-Auswahl, „Zuletzt gespielt“ vor der gesamten Sammlung, Startprofile, Prozessstatus und Logs. SteamGridDB ist integriert, mit manueller Auswahl und lokalem Fallback. Es gibt eine übersichtliche Spielansicht mit den tatsächlich bestätigten Darstellungsfähigkeiten.

Ein einzelner aktiver Spielprozess reicht zunächst. Automatisches Relinken, umfangreiche Testrunner, Cloudfunktionen und eine vollständige GPU-Ereignisanalyse sind dafür nicht nötig.

**Erfolgskriterien:** Ein neuer Nutzer kann ein vorhandenes Spiel hinzufügen und starten, ohne interne Dateistrukturen erraten zu müssen. Ein zweiter Start wird nicht unbemerkt erzeugt. Fehlende Online-Verbindung, Artwork oder Profiler-Unterstützung machen die Bibliothek nicht unbrauchbar.

## Phase 2: Profiler-Grundlage

Frame-Zeiten, eine einfache CPU-/GPU-Zuordnung, Live-Verlauf und stabile Frame-Auswahl kommen hinzu. Verfügbare Runtime-Fähigkeiten bestimmen den Umfang der Frame-Details. Die Aufzeichnung kann gestartet, beendet, gespeichert und nach Spielende erneut geöffnet werden.

Zum Datensatz gehören Runtime-/Spielbuild, gewähltes Startprofil, Messmodus, relevante Anzeigeeinstellungen und gemeldete Datenverluste. Die Profiler-Anzeige kann pausieren, während das Spiel weiterläuft.

**Erfolgskriterien:** Diagramm und rechte Detailansicht beziehen sich auf denselben Frame; geschätzte FPS und gemessene GPU-Zeit sind unterscheidbar; unvollständige Frames bleiben erkennbar; eine langsame UI führt nicht zu unkontrolliertem Speicherwachstum. Der eigene Einfluss der Aufzeichnung wird in einem kontrollierten Vergleich untersucht.

## Phase 3: Vertiefte Analyse

Mögliche Erweiterungen sind Draw-/Pipeline-Zähler, Shader-/Pipeline-Hotspots, differenzierte Submit- und Wartezeiten, mehrere CPU-Threads, Vergleich zweier Aufzeichnungen und Export in weitere Formate. Jede Funktion benötigt eine nachgewiesene Datenquelle und eine passende Interpretation.

Der im Design gezeigte Imports-Bereich könnte geloggte Runtime-Aufrufe, ungelöste Symbole und deren Häufigkeit zusammenführen. Ein Tests-Bereich könnte wiederholbare Starts mit definierten Profilen und Ergebnisvergleich anbieten. Beide sind vorerst nachgelagerte Ideen; sie müssen kein Teil des ersten Profilers sein.

Frame-Screenshots, GPU-Capture-Anbindungen und die Untersuchung ausgewählter Rendering-Ereignisse sind weitere Möglichkeiten. Deterministisches Replay, rückwärts laufendes Spiel oder beliebige Spielpause werden daraus nicht automatisch abgeleitet.

## Phase 4: Qualität und Auslieferung

Mit dem stabilen Kern werden Pakete, CI und gezielte Tests aufgebaut. Prüfpunkte sind hohe DPI, lange Titel, viele Spiele, fehlende Dateien, Controller-Hotplug, wechselnder Eingabefokus, langsame Datenträger und unterbrochene Telemetrie. Die funktionierende Plattformmatrix wird veröffentlicht.

Die Startansicht und der Profiler müssen mit Tastatur gut bedienbar sein. Animationen sind reduzierbar. Aufzeichnung und Artwork haben konfigurierbare Grenzen. App-Icon, Ressourcen und Laufzeitbibliotheken werden in einem sauberen Windows- und Linux-Paket überprüft.

## Fokus nach UI und Cache

Nach den Spikes sollte der erste konkrete Entwicklungsplan auf drei Ergebnisse reduziert werden: ein vorhandenes Spiel zuverlässig starten, die beiden Ansichten nativ abbilden und wenige korrekte Messwerte verständlich anzeigen. Eine gute kleine Frame-Analyse bringt mehr als viele Tabs mit unklarer Datenquelle.

Die weitergehenden Wünsche bleiben als geplante Erweiterungen erhalten. Praktische Ergebnisse bestimmen den Umfang der nächsten Implementierung.
