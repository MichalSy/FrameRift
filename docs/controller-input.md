# Controller und Eingaben

FrameRift soll mit Maus, Tastatur und Controller bedienbar sein und laufende Spiele mit ihrem vorhandenen AnyPS5-Eingabepfad verbinden. Die Navigation im Launcher, die Eingabe im Spiel und die Konfiguration eines Controllers sind getrennte Aufgaben. Die entsprechende FrameRift-Funktionalität ist geplant, noch nicht implementiert.

Die folgenden AnyPS5-Fakten beziehen sich auf [Upstream `197ffac97203d715a258a28b432fa2eedbf8be6f`](https://github.com/boykopovar/AnyPS5/tree/197ffac97203d715a258a28b432fa2eedbf8be6f). Sie wurden am Code geprüft; daraus folgt keine Hardware- oder Spielkompatibilitätsgarantie. Siehe [AnyPS5-Integration](anyps5-integration.md), [Profiling](profiling.md) und [Projektübersicht](../README.md).

## Bestätigte vorhandene Controller-Unterstützung

AnyPS5 besitzt bereits einen SDL-GameController-Pfad. Die Laufzeit öffnet das erste geeignete SDL-Gerät, liest standardisierte Buttons und Achsen und veröffentlicht einen Pad-Zustand an die Gastfunktionen. Xbox-Controller benötigen damit grundsätzlich keine erst von FrameRift geschriebene Controllerimplementierung. Ob ein konkretes Modell und seine USB-/Bluetooth-Verbindung korrekt erkannt werden, hängt vom verwendeten SDL-Backend und dessen Mapping ab. Quelle: [Geräteöffnung und Abfrage](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L25).

Im betrachteten Build sind `SDL_JOYSTICK` und `SDL_HIDAPI` eingeschaltet. `SDL_HAPTIC` und das eigenständige `SDL_SENSOR`-Subsystem sind ausgeschaltet. Das deaktivierte Sensor-Subsystem beweist für sich keine Sperre der GameController-Sensoren: Der gepinnte PS5-HIDAPI-Joysticktreiber registriert Beschleunigungs- und Gyrosensoren selbst. Quellen: [AnyPS5 CMake](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/CMakeLists.txt#L68), [SDL-PS5-Sensorregistrierung](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/hidapi/SDL_hidapi_ps5.c#L802).

### Standardisierte Xbox-/PlayStation-Zuordnung

| SDL-/übliche Xbox-Eingabe | Im vorhandenen AnyPS5-Pfad |
|---|---|
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB | L1 / R1 |
| Linker / rechter Stickklick | L3 / R3 |
| D-Pad | Entsprechende Richtung |
| Start / Menu | Options |
| Back / View bei einem Nicht-PS4-/Nicht-PS5-Gerät | Touchpad-Klick und ein simulierter mittiger Touchkontakt |
| Linker / rechter Stick | Linke / rechte Stickachsen |
| LT / RT | Analoger Triggerpfad und L2-/R2-Buttonbit, mit der unten beschriebenen Grenze |

Die Zuordnung folgt den von SDL gelieferten logischen Eingaben, nicht ungeprüften physischen Buttonnummern. Der View-Ersatz entspricht keinem frei beweglichen Touchpad: Er setzt einen Kontakt bei `(960, 471)`. Guide/Home wird in diesem Zuordnungsblock nicht abgebildet. Quelle: [Buttons, Achsen und Touchersatz](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L120).

### Rückmeldungen und Zusatzfunktionen

Der Code sendet gewöhnliches Zweimotor-Rumble. LED-Ausgabe erfolgt bei vorhandener LED-Fähigkeit. Für PS5-Geräte existiert ein Effektpfad für adaptive Trigger; andere Geräte bekommen gegebenenfalls Trigger-Rumble, wenn SDL diese Fähigkeit meldet. Die Rückgabefehler der entsprechenden Ausgabeaufrufe werden dort nicht ausgewertet. FrameRift darf daraus keine Zusage ableiten, dass jede Xbox-Generation über jeden Verbindungstyp Trigger-Rumble unterstützt. Quellen: [Ausgabepfad](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L72), [SDL-XboxOne-Treiber](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/hidapi/SDL_hidapi_xboxone.c#L531).

Echte Bewegungsdaten benötigen passende Sensoren und deren Aktivierung. Der vorhandene Touchpfad liest höchstens zwei Finger von Touchpad 0. Ein gewöhnlicher Xbox-Controller gewinnt durch ein anderes Mapping keine physisch nicht vorhandenen Touch- oder Bewegungssensoren. Quelle: [Sensor- und Touchabfrage](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L171).

## Bestätigte Grenzen des aktuellen Pfads

### Analoge Trigger

Der SDL-Eingabepfad skaliert die Triggerwerte auf `0..255` und setzt bei jedem positiven Ergebnis zusätzlich das L2-/R2-Buttonbit. `Pad::ReadState()` verwendet anschließend das Maximum aus Analogwert und `255`, sobald das entsprechende Buttonbit gesetzt ist. In dieser Kombination werden positive Controller-Triggerwerte beim Gastlesen effektiv auf `255` angehoben. Quellen: [Host-Abfrage](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L147), [Gast-Ausgabe](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/src/PadState.cpp#L100).

Das ist ein konkreter Codebefund im betrachteten Stand. FrameRift soll analoge Trigger deshalb nicht als vollständig geprüftes Merkmal bewerben. Eine Korrektur müsste den analogen Controllerpfad und die digitale Tastatur-/Mauseingabe unterscheiden und gehört in eine gesonderte AnyPS5-Core-Änderung mit Tests. Ein Einstellungsdialog allein behebt diesen Datenverlust nicht.

### Deadzones und Geräteauswahl

Die Controllersticks werden in diesem Pfad direkt auf `0..255` skaliert. Eine eigene einstellbare Deadzone ist dort nicht zu sehen; die Gastinformationen melden Deadzonewerte von `2`. Eine tatsächliche Gerätefilterung, Kalibrierung oder Stickkurve ist davon zu unterscheiden. Quellen: [Stickskalierung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L156), [Gast-Geräteinformationen](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/Export.cpp#L66).

`PadInput` hält einen Controllerpointer und wählt das erste passende Gerät. Hotplug und die Suche nach einem Ersatzgerät nach Trennung sind vorhanden. Der Gastport akzeptiert nur Index `0`. Ein gemeldeter Maximalwert von vier Verbindungen ist keine implementierte Unterstützung für vier Spieler. Außerdem liefert `ReadState()` derzeit einen statischen Gast-Verbindungsstatus `connected=true`, `connected_count=1`; er ist kein zuverlässiger Nachweis der physischen Geräteverbindung. Quellen: [Controllerpointer](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/PadInput.hpp#L35), [Hotplug](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L197), [Portprüfung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/Export.cpp#L20), [Gast-Verbindungsstatus](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/src/PadState.cpp#L139).

## Vorhandene Konfigurationswege

### Tastatur und Maus

`anyps5-input.ini` beziehungsweise der über `ANYPS5_INPUT_CONFIG` angegebene Pfad konfigurieren Tastatur-/Mausbindungen. Der Parser verarbeitet `KEY`, `MOUSE` und `WHEEL`. Dieses Format ist kein bereits existierender Editor für logische Xbox-Buttons, Controllerdeadzones oder die Auswahl mehrerer Pads. Quellen: [InputMapping-Parser](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/InputMapping.cpp#L95), [AnyPS5-Eingabedokumentation](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/docs/user/INPUT_MAPPING.md).

### Physisches SDL-Mapping

Das AnyPS5-SDL-Submodule ist im betrachteten Stand auf `4b69833bc54abf3dd3288d4aa7afbba527775e5b` gepinnt. Dessen Initialisierung lädt neben eingebauten Mappings auch `SDL_GAMECONTROLLERCONFIG_FILE` und anschließend `SDL_GAMECONTROLLERCONFIG`. Die Datei stellt Zuordnungen von physischen SDL-Geräten zu logischen SDL-GameController-Eingaben bereit. Quelle: [SDL-Mappinginitialisierung](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/SDL_gamecontroller.c#L1892).

FrameRift kann einen vorhandenen oder nativ erzeugten SDL-Mappingdatensatz als Teil der Prozessumgebung bereitstellen. Die Variable muss vor dem Spielstart gesetzt sein: AnyPS5 initialisiert `SDL_INIT_GAMECONTROLLER` bereits beim Aufbau des `VideoOutDriver`. Dafür ist grundsätzlich keine neue FrameRift-Telemetrieschnittstelle erforderlich. Quellen: [SDL-Hint](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/include/SDL_hints.h#L502), [AnyPS5-Initialisierung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L252).

Ein physisches SDL-Mapping und die anschließende logische PlayStation-Zuordnung sind verschiedene Ebenen. FrameRift soll diese Ebenen auch in einer späteren Konfigurationsoberfläche erklären. Beliebige Buttonindizes aus anderen APIs sind kein automatisch gültiger SDL-Mappingdatensatz.

## Geplante FrameRift-Eingabeaufteilung

| Kontext | Geplantes Verhalten |
|---|---|
| Bibliothek | Controller kann Auswahl, Navigation und Spielstart bedienen. |
| Aktives Spiel | Spiel erhält die vorgesehenen Eingaben über seinen bestehenden Laufzeitpfad. |
| Profileransicht | Navigation und Ansichtsaktionen bedienen FrameRift, ohne unbeabsichtigt Spielaktionen auszulösen. |
| Texteingabe/Modaler Dialog | Eindeutiger Fokus und klarer Weg zurück zum Spiel. |
| Geräteverlust | Sichtbare Diagnose, neutraler Zustand und nachvollziehbare Wiederverbindung. |

Die bestehende Xbox-Unterstützung soll erhalten bleiben. FrameRift muss keinen zweiten Gastpad-Emulator schreiben, um ein bereits erkanntes Gerät spielen zu lassen. Native Launcher-Navigation kann separat aufgebaut werden. Eine dauerhafte doppelte Verarbeitung derselben Eingabe im Launcher und im Spiel wäre dagegen unerwünscht.

### Fokus ist für Controller nicht ausreichend

AnyPS5 setzt ausdrücklich `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` auf `1`. Dadurch ist eine auf Fensterfokus beruhende Annahme „der Controller bedient jetzt nur die Bibliothek“ nicht belastbar. Die Laufzeit fragt Controllerzustände ab; ein im Hintergrund laufendes Spiel kann weiterhin entsprechende Eingaben erhalten. Quelle: [Hintergrundeingaben](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L25).

Für Tastatur und Maus muss bei einer Einbettung geprüft werden, welches native Fenster OS-Ereignisse empfängt. AnyPS5 verarbeitet bereits Fokusverlust, leert dabei gehaltene Tastatur-/Mausbindungen und beendet gegebenenfalls den relativen Mausmodus. Eine automatische korrekte Weiterleitung von FrameRift an ein fremdes Spielfenster ist trotzdem nicht implementiert. Quelle: [Fokusverlustbehandlung](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L216).

Ein möglicher späterer Laufzeitvertrag könnte den Spiel-Eingabekontext ausdrücklich aktivieren oder deaktivieren. Das wäre eine neue optionale Integration. Welche Eingaben pausiert werden, wie gehaltene Buttons neutralisiert werden und wie das Spiel nach Rückkehr den aktuellen Zustand erhält, muss vor der Umsetzung festgelegt werden. Ein virtueller Controller oder synthetische OS-Ereignisse sind keine vorausgesetzte MVP-Lösung.

## Geplanter MVP und mögliche Core-Arbeit

FrameRift kann zunächst Startprofile, Tastatur-/Maus-Konfigurationspfade und vorhandene SDL-Mappingdateien verwalten. Die Bibliothek soll erkannte Fähigkeiten verständlich zeigen und einen eigenen Spielfenstermodus anbieten, während die Einbettung untersucht wird.

Die folgenden erweiterten Wünsche benötigen zusätzliche Prüfung oder Änderungen außerhalb eines einfachen Launcher-Dialogs:

- Verlässlich analoge Gasttrigger im aktuellen Controllerpfad.
- Explizite Geräteauswahl und Zuordnung mehrerer Controller zu Gastports.
- Definierte Stickdeadzones und Kalibrierung an der richtigen Eingabeebene.
- Korrekte physische Verbindungsinformationen, getrennt vom Gastpadstatus.
- Ausdrückliche Controller-Eingabefreigabe beim Wechsel zwischen Spiel und Launcher.
- Rückmeldung über nicht unterstützte oder fehlgeschlagene Rumble-/LED-/Triggereffekte.

Solche AnyPS5-Änderungen sollen klein, getrennt und unabhängig von der FrameRift-GUI diskutiert werden. Dieses Dokument beschreibt Bedarf und Grenzen; es enthält keine bereits umgesetzten Core-Patches.

## Offene Spikes und Tests

1. **Xbox-Hardware:** Mindestens USB und Bluetooth mit konkreten Modellen auf Windows und Linux prüfen; Buttonzuordnung, Achsen und normale Vibration dokumentieren.
2. **Triggerwerte:** Eine Messfolge mit losgelassenem, teilweise gedrücktem und vollständig gedrücktem Trigger bis zur Gastfunktion verfolgen. Ein sinnvoller Regressionstest muss Zwischenwerte und digitale Ersatzbindungen unterscheiden.
3. **Konfiguration:** Mappingdatei vor SDL-Initialisierung, fehlende Datei, fehlerhafte Datensätze und Neustart nach Profilwechsel prüfen.
4. **Fokus:** Bibliothek, Spielbereich, Profiler, modale Dialoge, Alt-Tab und relativen Mausmodus gemeinsam prüfen. Ein Controller darf dabei nicht unbemerkt beide Kontexte steuern.
5. **Lifecycle:** Hotplug, Geräteverlust, Ersatzgerät, Spielabbruch und Neustart ohne hängen gebliebene Buttons prüfen.
6. **Einbettung:** Native Resize-/Fokus-/Vollbild-Übergänge mit dem tatsächlichen SDL-/Vulkan-Fenster unter Windows und Linux/X11 erproben; Wayland separat bewerten.

Diese Prüfungen sind geplant. Vorhandene Gastpad-Tests belegen einzelne Zustands- und Touchfunktionen, ersetzen aber keinen vollständigen Weg von einem physischen Xbox-Gerät über SDL bis ins Spiel. Quelle: [AnyPS5 GuestPad-Test](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/tests/GuestPad.cpp).
