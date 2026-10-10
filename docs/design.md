# Design und Bedienung

## Produktgefühl

FrameRift soll sich wie eine moderne Gaming-Anwendung anfühlen und gleichzeitig präzise Analysewerkzeuge anbieten. Die Bibliothek darf visuell ansprechend sein, ohne zur schwer bedienbaren Posterwand zu werden. Der Profiler soll viele Informationen zeigen können, ohne das laufende Spiel aus dem Mittelpunkt zu verdrängen.

Die Gestaltung ist individuell: dunkle Graphitflächen, zurückhaltende Trennlinien, kompakte Typografie, klare Größenverhältnisse und ein Cyan-Akzent. Das kantige F-Symbol im Entwurf ist eine Richtung für das spätere App-Icon. Ein endgültiger Vektor-Master, kleine Icon-Größen und eine vollständige Wortmarke müssen noch ausgearbeitet werden. Die Bilder allein sind kein fertiges Icon-Paket.

Die Oberfläche wird nativ umgesetzt. Gute Animationen bedeuten kurze, gleichmäßige Übergänge und unmittelbare Rückmeldung. Bibliotheksbilder dürfen lebendig wirken; Analysewerkzeuge müssen ruhig und ablesbar bleiben. Die Performance der UI wird gemessen, besonders während das Spiel und der Profiler gleichzeitig aktiv sind.

## Ansicht 1: Bibliothek

![Bibliothek](../assets/design/library.png)

### Aufbau und Reihenfolge

1. **Kopfzeile:** FrameRift-Marke, Suche und wenige globale Aktionen. Die Kopfzeile bleibt kompakt und verbraucht nicht den Platz eines zusätzlichen Dashboards.
2. **Hero des ausgewählten Spiels:** breites Artwork, Titellogo und eine verständliche Startaktion. Das Artwork wird mit einem dunklen Verlauf so eingebunden, dass Text und Buttons unabhängig vom Motiv lesbar bleiben.
3. **Zuletzt gespielt:** eine Reihe breiter Karten. Diese ist der schnellste Weg zurück in ein Spiel und steht vor der vollständigen Sammlung.
4. **Deine Spiele:** ein dichteres Raster aus Hochformat-Covern mit Suche, Filter und Sortierung.

Der Hero ersetzt keine separate Detailseite. Er dient als visuelle Auswahl und unmittelbarer Einstieg. Eine dauerhaft geöffnete Informationsspalte, eine vertikale Spieleliste und die Profiler-Seitenleiste gehören nicht in die Bibliotheksansicht.

### Spiel auswählen und starten

Ein Klick auf ein Cover wählt das Spiel aus und aktualisiert den Hero. **Spielen** startet mit dem normalen Spielprofil. **Mit Profiler starten** startet mit den dafür erforderlichen Instrumentierungsoptionen, sofern die konfigurierte AnyPS5-Runtime sie unterstützt. Die Beschriftung soll den tatsächlichen Startmodus wiedergeben und keine Messfähigkeit behaupten, die der Build nicht besitzt.

Optionen eines Spiels können über ein Kontextmenü oder eine kleine, vorübergehende Ansicht erreichbar werden: ausführbare Datei, Spielverzeichnis, Startprofil, Artwork-Zuordnung und bisherige Aufzeichnungen. Damit bleibt die Standardansicht auf Auswahl und Start fokussiert.

Für den ersten nutzbaren Umfang reicht ein klarer Import über eine Ordner- oder Programmauswahl. Automatisches Scannen mehrerer Bibliothekswurzeln kann folgen. Gleiche Titel in verschiedenen Builds müssen unterscheidbar bleiben, ohne die Sammlung mit technischen Dateinamen zu überladen.

### Größenverhältnisse

Das Bild zeigt eine mögliche Desktop-Komposition und keine verpflichtende Pixelgeometrie. Der Hero darf auf kleineren Fenstern niedriger werden. Covergrößen müssen sich über sinnvolle Zwischenstufen anpassen; übergroße Buttons und Karten sind zu vermeiden. Die Bibliothek braucht auch bei vielen Titeln eine brauchbare Informationsdichte.

Lange Namen, fehlende Logos, sehr helle Bilder und fehlendes Artwork werden ausdrücklich mitgestaltet. Ohne Online-Verbindung bleiben gespeicherte Bilder und lokale Metadaten nutzbar. Fehlende Bilder erhalten eine ruhige Ersatzdarstellung mit Titel und verfügbarem lokalen Icon.

## Ansicht 2: Spiel und Profiler

![Spiel und Profiler](../assets/design/profiler.png)

### Spielen

Nach dem Start wechselt die Anwendung in eine eigenständige Spielansicht. Das Spielfenster beziehungsweise die Spieloberfläche füllt den zentralen Bereich. Die wichtigsten Sitzungsaktionen sind erreichbar: Spiel beenden, zur Bibliothek zurückkehren, Profiler öffnen, Darstellung ändern und den Sitzungsstatus sehen.

Die Bibliotheks-Cover bleiben nicht als große Seitenleiste sichtbar. Ein Wechsel zurück in die Bibliothek ist eine Navigation, keine Einladung zum unbemerkten Start einer zweiten Instanz. Ein laufendes Spiel braucht einen eindeutigen Status und eine klare Zuordnung zur Sitzung.

### Profiler

Im Modus **Profiler** bleiben Bild und Eingabe des Spiels aktiv. Rechts erscheint die **Frame-Analyse**, unten ein skalierbarer Analysebereich. Die Aufteilung soll per Splitter veränderbar und als Layout gespeichert werden können. Analysebereiche können geschlossen werden, damit mehr Platz für das Spiel bleibt.

Die rechte Seite soll den ausgewählten Frame erläutern: Frame-ID, verfügbare CPU-/GPU-Zeiten, Submit-Informationen und später Draw-Calls, Pipeline-Wechsel oder Shader-/Pipeline-Hotspots. Eine fehlende Datenquelle zeigt „nicht verfügbar“ oder eine konkrete Messgrenze. Eine Zahl aus einem Zehn-Sekunden-Aggregat darf nicht wie eine Einzel-Frame-Messung aussehen.

Der untere Bereich enthält zunächst **Frame-Zeiten** und **Logs**. Die im Entwurf sichtbaren Bereiche **Imports** und **Tests** beschreiben weitere mögliche Werkzeuge. Sie sind noch kein festgelegter Umfang der ersten Version. Imports könnten Runtime-Aufrufe und fehlende Symbole erklären; Tests könnten wiederholbare Starts und Messläufe bündeln. Deren Datenquellen und Bedienung sind separat zu entwerfen.

### Live-Daten und Frame-Auswahl

Die Zeitleiste läuft zunächst live. Wählt man einen Frame oder Zeitbereich aus, bleibt die Auswahl stabil, während neue Daten eintreffen. Ein sichtbarer Zustand unterscheidet „Live folgen“ von „Auswahl ansehen“. Zum Live-Ende gelangt man mit einer eindeutigen Aktion zurück.

Eine angehaltene Profiler-Anzeige hält das Spiel nicht automatisch an. Spielpause wäre eine eigene Funktion mit einer tatsächlich unterstützten Runtime-Schnittstelle. Eine Aufzeichnung muss deutlich anzeigen, ob sie nur Telemetrie erfasst oder zusätzlich Bilder enthält. Die Speicherung eines Frames ist ebenfalls keine Zusage für deterministisches Replay.

## Animationen, Artwork und Messbetrieb

Für Karten sind leichte Hover-Reaktionen und dezente Auswahlübergänge geplant. Der Hero-Wechsel kann kurz überblendet werden. Seitenleisten und Analysebereiche dürfen weich ein- und ausblenden, müssen aber unmittelbar bedienbar sein. Große Parallax-Effekte, dauerhaft animierte Diagrammhintergründe und ständig springende Ziffern würden die Bedienung erschweren.

SteamGridDB bleibt die primäre Artwork-Quelle. Animierte Hero-Bilder sollen möglich sein, wenn die konkrete UI-Bibliothek und der Decoder sie zuverlässig darstellen. Für reproduzierbare Messungen sollte FrameRift eigene Artwork-Animationen pausieren oder auf ein statisches Bild wechseln können. Weitere Details stehen in [Artwork](artwork.md).

## Eingabe und Zugänglichkeit

Die Bibliothek braucht vollständige Tastaturnavigation mit sichtbarem Fokus. Controller-Navigation ist eine mögliche Ergänzung; Controller-Eingabe im Spiel wird separat betrachtet. Die Startaktion darf nicht durch einen Fokuswechsel unabsichtlich ausgelöst werden.

Im Profiler muss erkennbar sein, ob Tastatur und Maus gerade das Spiel oder ein Analysefeld bedienen. Suchfelder und Tastenkürzel dürfen keine Spielaktionen auslösen. Ein Fokuswechsel sollte gezielt möglich sein, ohne das Spielbild zu verlassen. Die Plattformversuche prüfen genau diese Übergänge.

Zu berücksichtigen sind Schrift- und DPI-Skalierung, ausreichender Textkontrast, reduzierte Animationen, verständliche Symbole mit Textalternativen und farbunabhängige Darstellung von Ausreißern. Die kleinen Texte in den Entwürfen definieren keine minimale Schriftgröße für die Implementierung.

## Einordnung der Bilder

Die beiden PNGs sind visuelle Zielbilder. Sie zeigen noch keine implementierte Fensterintegration, kein existierendes Telemetrieprotokoll und keine reale Messung des dargestellten Spiels. Die Quellen und die Verwendung des ASTRO-BOT-Artworks sind in den [Bildquellen](../assets/design/README.md) dokumentiert.
