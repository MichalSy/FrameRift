# Profiling und Frame-Analyse

FrameRift soll Spielen und technische Analyse in einer nativen Oberfläche verbinden: eine laufende Spielsitzung, verständliche Timingkurven und eine Profiler-Ansicht für die vorhandenen AnyPS5-Diagnosen. Die Oberfläche und ihre Telemetrieschnittstelle sind geplant, noch nicht implementiert.

Bestätigte Angaben zu AnyPS5 beziehen sich auf [Upstream `197ffac97203d715a258a28b432fa2eedbf8be6f`](https://github.com/boykopovar/AnyPS5/tree/197ffac97203d715a258a28b432fa2eedbf8be6f). Dieser Quellenstand ist kein Laufzeit-Benchmark von FrameRift. Siehe [AnyPS5-Integration](anyps5-integration.md), [Controller und Eingaben](controller-input.md) und [Projektübersicht](../README.md).

## Bestätigte vorhandene Messquellen

AnyPS5 enthält bereits Instrumentierung. FrameRift muss deshalb zwischen dem Anzeigen vorhandener Daten und einer neuen Messschnittstelle unterscheiden.

| Quelle | Aktivierung im betrachteten AnyPS5-Stand | Vorhandene Ausgabe | Bedeutung für FrameRift |
|---|---|---|---|
| Frame-Timing | Buildoption `APS5_ENABLE_TIMING_LOG=ON`, standardmäßig aus. | `frame-timing.log`. | Grundlage für einen ersten Dateiimport und Timingkurven. |
| Draw-/Treiberprofil | Umgebungsvariable `APS5_PROFILE_DRAW` vorhanden. | Diagnosen und Zähler, überwiegend über den Profilausgabepfad nach `stderr`. | Grundlage für eine Logansicht und ausgewählte Statistiken. |
| GPU-Timing | Umgebungsvariable `APS5_PROFILE_GPU` vorhanden. | Vulkan-Timestamp-Auswertung, `[gputime]`-Aggregate. | Grundlage für GPU-Programm-/Klassen-Hotspots. |
| Allgemeine Diagnosen | Je nach Laufzeitpfad und Einstellungen. | Standard-/Fehlerausgabe, Fehler und Exitstatus. | Fehlersuche und zeitliche Einordnung einer Sitzung. |

Die beiden Profilvariablen werden in den betrachteten Pfaden auf **Vorhandensein** geprüft. Eine auf `0` gesetzte Variable ist dort weiterhin vorhanden. FrameRift soll beim Deaktivieren die Variable entfernen, statt pauschal den String `0` zu setzen. Quellen: [Buildoption](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/CMakeLists.txt#L26), [Draw-Aktivierung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L38), [GPU-Aktivierung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L1790).

Das Vorhandensein des Codes beweist nicht, dass jeder ausgelieferte Bibliothekssatz mit Frame-Timing gebaut wurde. Die geplante Oberfläche muss Fähigkeiten einer konkreten Sitzung erkennen und fehlende Messquellen als nicht verfügbar anzeigen.

## Vorhandenes Frame-Timingformat

`PerformanceTimer.hpp` erfasst unter anderem:

- Frame-ID, zugehörigen Submission-Bereich, Flip-Submission und Packet-Offset.
- Videoausgabe-Handle, Buffer-Index und Flip-Argument.
- Zeitpunkte von erster Submission, erster Ausführung, Flip-Packet und Flip-Abschluss.
- Abstände zwischen diesen Zeitpunkten und das Intervall zwischen Flip-Abschlüssen.
- Worker-Zeit, zugeordnete und nicht zugeordnete Worker-Zeit.
- GPU-Busy-/Gap-Werte, Batch-Zähler, Packet-Anzahl und einen langsamen Packet-Eintrag.
- Phasenmetriken als Anzahl, Gesamtdauer, Maximaldauer und gegebenenfalls Bytes.

Der Header beschreibt Metriken ausdrücklich als inklusive Werte. Hintergrundmetriken werden außerdem als seit dem vorherigen Flip abgeschlossene Arbeit zusammengefasst. Quelle: [Capture und Metrikformat](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L154).

Die Present-Schleife ermittelt das Intervall zwischen aufeinanderfolgenden Flip-Abschlüssen und gibt den Datensatz an den Timing-Logger weiter. Der Logger schreibt asynchron in eine gepufferte Datei und leert den Dateipuffer periodisch. Eine Dateiüberwachung kann deshalb hinter dem aktuellen Spielzustand liegen. Quellen: [Present-Schleife](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L587), [Timing-Logger](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/FrameTimingLog.hpp#L18).

## FPS und Frame-Time richtig benennen

Der Datensatz enthält `endpoint=flip_complete` und `display_confirmed=0`. Das ist eine explizite Grenze: Der Messpunkt bestätigt nicht, wann das Bild tatsächlich auf dem Monitor angezeigt wurde. FrameRift soll deshalb zunächst **Flip-Intervall** und **geschätzte Flip-Rate** anzeigen, mit dem Messpunkt im Tooltip oder in den Messdetails. Quelle: [Kennzeichnung des Endpunkts](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L159).

Für ein gültiges positives Intervall ist eine einfache abgeleitete Rate `1000 / flip_interval_ms`. Für eine Übersicht ist die Rate über ein Zeitfenster aussagekräftiger: Anzahl gültiger Intervalle geteilt durch ihre gesamte Dauer. Diese Berechnungen sind geplante Auswertungen von FrameRift. Sie messen weder automatisch neue Spielbilder noch die tatsächliche Monitor-FPS, Displaylatenz oder Eingabe-zu-Bild-Latenz.

Fehlende Intervalle, der erste Frame einer Sitzung, unvollständige Daten und verworfene Messwerte sollen als Lücken erkennbar bleiben. Ein fehlender Wert wird nicht als `0 ms` und nicht als unendliche FPS dargestellt. Wiederholte Präsentationen und mehrere Ausgabe-Handles müssen getrennt bewertet werden.

## CPU-Messgrenzen

Die vorhandenen CPU-Timings verwenden `std::chrono::steady_clock`. Sie messen verstrichene Zeit in instrumentierten Phasen. Solche Zeit kann Arbeit, Wartezeit, Synchronisation oder Unterbrechungen enthalten. Sie ist kein vollständiges CPU-Samplingprofil aller Spielthreads. Quelle: [Clock und Metrikstruktur](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L20).

Inklusive Phasen können sich verschachteln oder überlappen. FrameRift darf sie nicht ohne Prüfung zu einer vermeintlichen Gesamt-CPU-Framezeit addieren. Hintergrundarbeit, die zwischen zwei Flips beendet wurde, ist außerdem nicht zwangsläufig kausal nur einem dieser Frames zuzuordnen.

Ein späterer Prozessmonitor könnte Betriebssystemwerte wie CPU-Auslastung und Speicherverbrauch ergänzen. Diese würden als eigene Messquelle mit ihrem Abtastintervall gekennzeichnet. CPU-Auslastung des gesamten Spielprozesses, Kosten einer instrumentierten Treiberphase und CPU-Zeit eines bestimmten Spielthreads sind unterschiedliche Größen.

## Vorhandenes GPU-Profiling und seine Grenzen

Der Recorder erzeugt Vulkan-Timestamp-Query-Pools und setzt Anfangs-/Endmarken an instrumentierten Bereichen. Die Ausgabe aggregiert GPU-Zeiten nach Programm und Befehlsklasse über ungefähr zehn Sekunden. Sie enthält Batch-Spannen, abgedeckte Bereiche, nicht zugeordnete Zeit und einen Zähler für wegen des Bereichslimits verworfene Messungen. Die Hotspot-Ausgabe zeigt eine begrenzte Liste der teuersten Programme. Quellen: [Query-Erzeugung und Marken](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L1880), [Auswertung und Aggregate](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L2333).

Daraus kann FrameRift eine GPU-Hotspot-Tabelle aufbauen. Die vorhandenen Aggregate sind jedoch keine bereits verfügbare vollständige GPU-Timeline pro Frame. Programm-Adressen sind keine automatisch aufgelösten Shadernamen. Instrumentierte GPU-Dauer ist außerdem keine allgemeine GPU-Auslastung, keine Temperatur und kein VRAM-Belegungswert.

Vulkan-Queries liefern Ergebnisse asynchron. Verfügbarkeit, Queue-Unterstützung, Timestamp-Auflösung und Abfragekosten müssen für die tatsächliche GPU geprüft werden. Eine gemeinsame CPU-/GPU-Zeitleiste benötigt eine definierte Zuordnung ihrer Zeitdomänen; rohe GPU-Ticks und Hostzeit dürfen nicht einfach gleichgesetzt werden. Diese Erweiterung ist offen. Quelle: [Vulkan: Queries und Timestamp Queries](https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps).

GPU-Ergebnisse können erst nach einem späteren Flip vorliegen. Der vorhandene Timingcode beschreibt GPU-Busy-/Gap-Daten zu bereits abgeschlossenen Batches; das ist keine Zusage, dass jeder Wert ausschließlich den gerade angezeigten Frame betrifft. Quelle: [Zuordnung abgeschlossener Batches](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L145).

## Geplante erste Profiler-Ansicht

Ein erster Funktionsumfang kann vorhandene Daten nutzen, ohne einen vollständigen Debugger vorauszusetzen:

1. Spielsitzung mit Prozessstatus und Live-Logs.
2. Import oder laufende Beobachtung der Timingdatei eines passenden Bibliothekssatzes.
3. Kurve der Flip-Intervalle und nachvollziehbar berechnete Statistiken.
4. Details eines ausgewählten Frame-Timingdatensatzes.
5. Ausgewählte Draw-/GPU-Aggregate mit Messzeitfenster und Quellenkennzeichnung.
6. Export einer Sitzung mit Metadaten, Rohdaten und aktivierten Messoptionen.

Das Pausieren der Kurvenanzeige soll zunächst nur die FrameRift-Ansicht einfrieren. Es bedeutet nicht, dass der Spielprozess oder die GPU angehalten wird. Breakpoints, Einzelschritte, Callstacks, Speicherinspektion und Shader-Debugging erfordern weitere Debugger- oder Capture-Integration. Sie sind keine vorhandenen Fähigkeiten eines Fensters mit dem Titel „Profiler“.

## Geplante strukturierte Telemetrie

Langfristig ist eine ausdrückliche optionale Telemetrieschnittstelle sinnvoller als das dauerhafte Interpretieren menschlich lesbarer Logzeilen. Sie müsste an geeigneten AnyPS5-Messpunkten Daten erzeugen; FrameRift alleine kann interne Treiberphasen nicht zuverlässig aus einem fremden Fenster auslesen.

Die folgenden Felder sind ein **Entwurf**, kein bereits unterstütztes AnyPS5-Protokoll:

| Feldgruppe | Geplante Inhalte |
|---|---|
| Protokoll | Version, Datensatztyp, vorhandene Fähigkeiten. |
| Sitzung | FrameRift-Sitzungs-ID, Prozessidentität, Titel-ID, Artefakt-/Bibliotheksversion. |
| Zuordnung | Messquelle, Frame-ID, Ausgabe-Handle, Submission-/Batch-ID, Queue-ID soweit verfügbar. |
| Zeit | Definierter Zeitursprung, Zeiteinheit, Zeitdomäne, Beginn/Ende oder Intervall. |
| Qualität | Vollständigkeit, Drop-Zähler, Messauflösung, gültig/noch nicht verfügbar. |
| Metrik | Stabiler Name, Einheit, inklusive/exklusive Semantik, Anzahl, Summe, Maximum. |

Eine Frame-ID gilt innerhalb einer eindeutig benannten Sitzung und Messquelle. Sie darf nicht als global eindeutige ID zwischen Neustarts behandelt werden. Spät eintreffende GPU-Daten sollen die ursprüngliche Submission-/Batch-Zuordnung behalten; die Empfangszeit im Launcher ist keine Frame-Zuordnung.

Für den Transport sind ein lokaler Socket, eine Windows Named Pipe oder ein begrenzter Shared-Memory-Kanal zu untersuchen. Die Auswahl ist offen. Der erste Protokollentwurf soll möglichst wenig Daten und eine klare Versions-/Fähigkeitsaushandlung enthalten. Diagnosetext, Logereignisse und präzise Messdaten werden im Datenmodell getrennt.

## Overhead und Backpressure

Instrumentierung kann Laufzeiten verändern. Zusätzliche Clock-Abfragen, Locks, Formatierung, Speicherallokationen, Query-Pools und Datenübertragung gehören zum Messaufwand. FrameRift soll diesen Aufwand messen und Messmodi eindeutig kennzeichnen, ohne vorab einen unbelegten Prozentwert zu versprechen.

Die vorhandenen Ausgabepfade haben unterschiedliche Überlastregeln:

- `FrameTimingLog` begrenzt seine Queue auf 256 Reports und wirft bei voller Queue. Die Present-Schleife behandelt Timingfehler als Fehler und kann den Prozess beenden.
- `ProfileOutput` besitzt einen begrenzten Puffer, verwirft bei Kapazitätsmangel Chunks und berichtet die Drops. Schreibfehler können trotzdem als Fehler weitergegeben werden.

Quellen: [Timing-Queue](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/FrameTimingLog.hpp#L36), [Timing-Fehlerbehandlung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L619), [ProfileOutput](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp#L41).

Für die geplante IPC-Verbindung ist ein begrenzter Producer-Puffer mit sichtbaren Drop-Zählern vorgesehen. Ein langsamer oder geschlossener Launcher darf das Spiel nicht dauerhaft blockieren. Datenformatierung und UI-Aktualisierung sollen außerhalb der heißen Render-/Treiberpfade liegen. Detaillierte Messung soll gezielt aktivierbar sein; eine Bibliotheksansicht benötigt keine permanente GPU-Instrumentierung.

## Offene Spikes und spätere Validierung

| Spike | Zu klärende Frage |
|---|---|
| Timingimport | Können Version, Einheiten, teilweise geschriebene Zeilen und Datenlücken zuverlässig erkannt werden? |
| Frame-Korrelation | Wie werden mehrere Ausgaben, Queue-Arbeit und spät abgeschlossene Batches zugeordnet? |
| GPU-Timeline | Welche neuen Ereignisse und Zeitkalibrierung sind zusätzlich zu den Aggregaten erforderlich? |
| IPC | Welcher lokale Transport verursacht wenig Aufwand und bleibt bei Verbindungsabbruch robust? |
| Overhead | Wie unterscheiden sich Spiel allein, Logs, Frame-Timing, GPU-Profiling und FrameRift-Anzeige? |
| UI | Bleiben Navigation und Kurven flüssig, wenn viele Messwerte oder Logs eintreffen? |

Spätere Tests sollen mathematische Auswertungen mit bekannten Datensätzen, fehlende/ungültige Werte, Protokollversionen, Überlast, Verbindungsverlust und Prozessneustarts abdecken. Integrationsmessungen sollen identische Spiel-/Treiber-/Bibliotheksbedingungen und mehrere Durchläufe verwenden. Ein Ergebnis aus einem Designbild ist kein Benchmark und keine bestätigte Spielkompatibilität.
