# Artwork, SteamGridDB und Animationen

[Zur Projektübersicht](../README.md) · [Toolkit und Plattformen](toolkit-and-platforms.md) · [Spielintegration](game-window-integration.md)

## Status und Produktentscheidung

**SteamGridDB ist die primäre Artwork-Quelle von FrameRift.** Die geplante Bibliothek verwendet Portrait-Cover in breiten Reihen und eine große Hero-Fläche für das ausgewählte Spiel. Ein separat verfügbares Titel-Logo kann auf dieser Fläche platziert werden. Animierte Hero-Bilder sollen als bewusst unterstütztes Format geplant werden.

Dieses Dokument definiert den geplanten Datenfluss. Es gibt noch keinen implementierten API-Client, Cache, Decoder oder Downloadmanager. Die Bibliothek startet zunächst mit lokalen Einträgen; Artwork lädt die Darstellung auf, bestimmt aber nicht die Spielbarkeit.

## Quellenkategorien

SteamGridDB unterscheidet **Grids**, **Heroes**, **Logos** und **Icons**. Die API dokumentiert unter anderem Portrait-Grids mit 600×900 und Hero-Formate wie 1920×620. Diese Kategorien werden im FrameRift-Modell getrennt gespeichert. Ein quadratisches Icon wird nicht allein durch Skalieren zu einem Portrait-Cover. [SteamGridDB API](https://www.steamgriddb.com/api/v2)

| FrameRift-Verwendung | SGDB-Kategorie | Vorgeschlagene Behandlung |
|---|---|---|
| Bibliothekskarte | Grid | Portrait bevorzugen; Seitenverhältnis erhalten |
| Ausgewähltes Spiel / breite Bannerfläche | Hero | Breite Grafik; kontrollierter Ausschnitt |
| Titel auf der Hero-Fläche | Logo | Transparenz und Kontrast prüfen |
| Kleine Listen-/Statusdarstellung | Icon | Passende kleine Auflösung verwenden |

Ein Cover mit eingezeichnetem Logo und ein separates transparentes Logo sind unterschiedliche Ressourcen. Für die Hero-Fläche soll die Datenstruktur beide unabhängig erlauben, damit kein Titel doppelt über einem bereits beschrifteten Hintergrund erscheint.

## Spiele sicher zuordnen

Die API bietet Namenssuche und Zugriffe über SGDB-Spiel-IDs sowie externe Plattform-IDs. Dokumentiert sind Steam, Origin, EGS, Battle.net, Ubisoft, Flashpoint und eShop. PS5-/PPSA-IDs sind in dieser Plattformliste nicht enthalten. Für eine PS5-Bibliothek ist daher **Name suchen, Treffer prüfen und die SGDB-ID dauerhaft zuordnen** die vorgeschlagene Richtung. [Offizielle OpenAPI-Spezifikation](https://www.steamgriddb.com/static/openapi.yml)

Der Suchprozess soll nicht jeden Start erneut anhand des ersten Namenshits entscheiden. Vorgeschlagener Ablauf:

1. Lokale Metadaten liefern einen Spielnamen und, soweit vorhanden, weitere unterscheidende Angaben.
2. Die Suche erzeugt Kandidaten. Titelvarianten, Erscheinungsjahr, Edition und Spielserie helfen bei der Auswahl.
3. Mehrdeutige Ergebnisse werden sichtbar als nicht bestätigte Zuordnung behandelt.
4. Die bestätigte SGDB-Spiel-ID wird am lokalen Bibliothekseintrag gespeichert.
5. Spätere Aktualisierungen verwenden diese Zuordnung; eine manuelle Korrektur bleibt möglich.

Beispiel: **ASTRO BOT (2024)** und **ASTRO's PLAYROOM** sind unterschiedliche Bibliothekseinträge. Ein ähnlicher Name oder derselbe Charakter darf die Zuordnung nicht automatisch zusammenführen.

Die lokale Spielidentität bleibt eigenständig. Ein einzelner SGDB-Eintrag kann sich auf eine andere Edition oder auf mehrere Plattformfassungen beziehen. Die Artwork-Zuordnung verändert weder die lokale Startkonfiguration noch einen Kompatibilitätsstatus.

## API-Zugriff

SteamGridDB verlangt für die API einen API-Key im HTTP-Header `Authorization: Bearer <token>`. Ein Schlüssel kann über die Benutzerpräferenzen erzeugt werden. Das Lesen öffentlicher Website-Einträge und ein späterer authentifizierter API-Client sind getrennte Zugriffswege. [API-Authentifizierung](https://www.steamgriddb.com/static/openapi.yml), [offizieller Client-Einstieg](https://github.com/SteamGridDB/node-steamgriddb#readme)

Für FrameRift ist vorgesehen:

- Den persönlichen Schlüssel zur Laufzeit konfigurieren; keinen produktiven Schlüssel in das öffentliche Repository schreiben.
- Authentifizierung nur an den dokumentierten API-Zielhost senden, nicht an beliebige Artwork-URLs.
- Fehler wie fehlende Authentifizierung, fehlender Treffer und vorübergehende Netzwerkprobleme getrennt anzeigen.
- Kataloganfragen bündeln, Ergebnisse zwischenspeichern und Parallelität begrenzen.
- Bereits vorhandene Bilder weiterhin anzeigen, wenn neue Anfragen fehlschlagen.
- Die Bibliothek und der Start lokaler Spiele bleiben auch ohne funktionierenden Artwork-Dienst bedienbar.

Die konkrete Netzwerkbibliothek ist offen und folgt der [Toolkitentscheidung](toolkit-and-platforms.md). Asynchrones I/O und klare Abbruch-/Lebenszeitregeln sind erforderlich; eine synchron wartende UI ist nicht die Arbeitsrichtung.

## Asset-Auswahl

Die automatische Auswahl soll eine nachvollziehbare Voreinstellung liefern, aber keine dauerhafte Bindung an einen einzigen Upload erzwingen. Vorgeschlagene Prioritäten:

1. Eine manuell gewählte Ressource.
2. Eine passende lokal gecachte Ressource mit bestätigter Spielzuordnung.
3. Ein geeignetes SteamGridDB-Asset in der gewünschten Kategorie und Größe.
4. Ein vorhandenes lokales Spielbild oder ein neutraler Platzhalter, wenn kein passendes Asset verfügbar ist.

Scores und Kategorien sind Auswahlhilfen. Sie sind kein Nachweis einer offiziellen Herkunft oder einer freien Lizenz. Die gespeicherte Asset-ID und der ursprüngliche Eintragslink sollen eine spätere Korrektur möglich machen.

Ein fehlendes separates Logo wird nicht durch ein unsichtbares oder verzerrtes Bild ersetzt. Der Spielname kann als normale UI-Schrift erscheinen. Bei falschem Artwork soll eine einzelne Zuordnung zurückgesetzt werden können, ohne die gesamte Bibliothek oder Startkonfiguration zu löschen.

## Animationen: WebP ausdrücklich einplanen

Die API kennt die Filter `static` und `animated`; ohne ausdrücklichen Filter gilt `static`. Für animierte Heroes muss der Client Animation daher aktiv anfordern. SteamGridDB erklärt in seiner FAQ, dass aktuelle animierte Assets **WebP** verwenden und APNG eingestellt wurde. GIF ist keine Grundlage dieses Plans. [API](https://www.steamgriddb.com/api/v2), [SteamGridDB FAQ](https://www.steamgriddb.com/faq)

**Eine Datei anzeigen und ihre Animation abspielen sind zwei unterschiedliche Fähigkeiten.** Ein statischer WebP-Decoder oder ein gewöhnliches Image-Widget bestätigt keine korrekte Wiedergabe mehrerer Frames. Nachzuweisen sind Frame-Zeitpunkte, Blend-/Dispose-Verhalten, Transparenz, Schleifen und Pausieren.

Als Toolkit-unabhängiger Kandidat besitzt libwebp APIs für animierte WebP-Container und Animation-Decoding. Der geplante Decoder kann daraus zeitlich gesteuerte Bildframes erzeugen, die das ausgewählte Toolkit präsentiert. Ob ein Toolkit bereits einen passenden vollständigen Pfad liefert, ist vor einer zusätzlichen Abhängigkeit zu prüfen. [WebP Container-/Animations-API](https://developers.google.com/speed/webp/docs/container-api)

### Vorgeschlagene Wiedergaberegeln

- Nur der sichtbare ausgewählte Hero animiert standardmäßig.
- Verdeckte oder nicht ausgewählte Heroes pausieren; Bibliothekskarten bleiben zunächst statisch.
- Minimieren, Verlassen der Bibliothek und Deaktivieren von Animationen stoppen unnötige Decoderarbeit.
- Eine statische Ersatzdarstellung bleibt verfügbar, auch wenn ein animiertes Asset nicht vollständig geladen oder dekodiert ist.
- Der Player behandelt Frame-Dauer und Schleifen explizit; er zeichnet nicht ungebremst mit einer unabhängigen Dauerschleife.
- Für reduzierte Bewegung soll eine ruhende Variante wählbar sein.

**Vorschlag für Messungen:** Artwork-Animationen während einer aktiven Profiler-Aufzeichnung pausieren oder standardmäßig ausblenden. So werden dekorative UI-Kosten nicht unbemerkt Teil eines Performance-Vergleichs. Dies ist eine vorgeschlagene Produktregel, noch keine fest beschlossene Automatik. Die Entscheidung wird zusammen mit den tatsächlichen Messkosten getroffen.

## Vorgeschlagener Cache

Ein lokaler Cache soll Offline-Nutzung, schnelle Auswahlwechsel und begrenzte Netzwerklast ermöglichen. Der Cache ist geplant; hier ist noch kein Datenbankschema verbindlich.

| Gespeicherte Angabe | Zweck |
|---|---|
| Lokale Spiel-ID und bestätigte SGDB-Spiel-ID | Stabile Zuordnung |
| Kategorie und SGDB-Asset-ID | Grid/Hero/Logo/Icon getrennt verwalten |
| Original-URL und öffentlicher Eintragslink | Herkunft und spätere Aktualisierung |
| Upload-/Autorangabe, soweit verfügbar | Provenienz erhalten |
| MIME-Type, Abmessungen und Animationsstatus | Geeigneten Darstellungspfad wählen |
| Cachegeneration und Inhaltsprüfung | Teilweise oder veraltete Dateien erkennen |
| Manuelle Auswahl / lokale Ersatzquelle | Nutzerentscheidung erhalten |
| Downloadzustand und letzte Fehlerklasse | Fehler nicht als gültiges Bild behandeln |

Originaldatei und abgeleitete Anzeigegröße sollen getrennt sein. Ein Thumbnail ist für die schnelle Auswahl geeignet, aber kein Ersatz für ein gewünschtes hochauflösendes Hero-Bild. Das Frontend dekodiert möglichst für die tatsächlich sichtbare Auflösung.

Vorgeschlagene technische Regeln:

- Begrenzter Plattencache mit einer dokumentierten Verdrängungsstrategie; manuelle lokale Quellen separat erhalten.
- Begrenzter RAM-/GPU-Cache für aktive und unmittelbar folgende Ansichten.
- Downloads zunächst in eine temporäre Ressource schreiben und erst nach erfolgreicher Validierung übernehmen.
- Dateiformat, Abmessungen und Speicherbedarf prüfen, bevor große Bilder vollständig aufgebaut werden.
- Nicht sämtliche Frames aller Animationen dauerhaft dekodiert im Speicher halten.
- Gleichzeitige Anfragen für dasselbe Asset zusammenführen.
- Fehlende Assets zeitweise als fehlend merken, aber eine manuelle erneute Suche erlauben.
- Eine Cachebereinigung verändert keine Bibliothekseinträge und keine Spielzuordnung.

Eine Größenordnung verdeutlicht den Bedarf: Ein dekodiertes 1920×620-Bild mit vier Bytes pro Pixel belegt ungefähr 4,5 MiB; mehrere Dutzend vollständig gespeicherte Frames können entsprechend viel RAM beanspruchen. Komprimierte Dateigröße und dekodierter Speicherbedarf sind getrennte Größen. Das Beispiel ist eine Berechnung, keine zugesagte Cachegrenze.

## Manuelle Auswahl und lokale Ersatzbilder

Der Nutzer soll Cover, Hero und Logo unabhängig überschreiben können. Diese Fähigkeit ist als lokale Bibliotheksfunktion geplant, nicht als SteamGridDB-Uploadworkflow.

Eine manuelle Auswahl speichert die eindeutige Ressource und ihre Herkunft. Ein neues automatisches Suchergebnis soll sie nicht still überschreiben. Für lokal ausgewählte Dateien wird entschieden, ob eine Kopie verwaltet oder die ursprüngliche Quelle referenziert wird; das Verhalten muss in der Oberfläche klar sein.

Format- oder Decoderfehler dürfen weder die Bibliothek blockieren noch eine bestehende funktionierende Auswahl verdrängen. Ein sichtbarer Platzhalter und ein verständlicher Fehlerzustand sind dem wiederholten unbegrenzten Laden vorzuziehen.

## Herkunft, Rechte und Veröffentlichung

SteamGridDB ist eine Artwork-Datenbank mit Beiträgen von Nutzern. Eine erfolgreiche API-Anfrage erteilt keine pauschale Lizenz für jedes Bild. Die Rechte am dargestellten Spiel, am Titel-Logo und an einer Bearbeitung können sich unterscheiden. Ein Kategoriebegriff wie „official“ wird nicht als allgemeine Wiederveröffentlichungserlaubnis ausgewertet. Die Nutzungsbedingungen und die Angaben am jeweiligen Asset sind separat zu beachten. [SteamGridDB Terms](https://www.steamgriddb.com/terms)

Für das öffentliche FrameRift-Repository ist vorgeschlagen:

- Designreferenzen ausdrücklich als Konzeptbilder kennzeichnen.
- Für reale Spielebilder den öffentlichen Originaleintrag und die Herkunft nennen.
- Die Lizenz des FrameRift-Codes nicht auf fremdes Spielartwork übertragen.
- Fremde Artwork-Sammlungen nicht pauschal als Bestandteil eines Release-Pakets bündeln.
- Demo-/Testmaterial mit klarer Nutzungserlaubnis oder eigenen neutralen Beispielen getrennt führen.

Eine Darstellung von ASTRO BOT oder einem anderen realen Spiel in einem Designbild ist **keine Kompatibilitätsbehauptung**. Spielbarkeit und Profiler-Fähigkeiten werden an anderer Stelle anhand tatsächlicher Integrationsnachweise beschrieben.

## Offene Entscheidungen und Prüfpunkte

| Frage | Nachweis vor Implementierungsfreigabe |
|---|---|
| Wie wird die Spielzuordnung bestätigt? | Mehrdeutige Titel und Editionen bleiben korrigierbar |
| Welcher Decoder spielt WebP-Animationen? | Windows und Linux zeigen Timing, Alpha und Schleifen korrekt |
| Welche Cachegrenzen gelten? | RAM-/GPU-/Plattenverbrauch mit realistischer Bibliothek gemessen |
| Wann pausieren Animationen? | Verdeckte UI bleibt ruhig; Messregeln bewusst beschlossen |
| Welche lokale Persistenz wird genutzt? | Zuordnung und manuelle Auswahl über Neustarts erhalten |
| Wie sehen Netzwerkausfälle aus? | Bibliothek und Spielstart funktionieren mit Cache/Platzhalter |
| Welche Assets werden veröffentlicht? | Provenienz und Rechte je veröffentlichtem Material geklärt |

Der spätere Artwork-Prototyp soll zunächst einen bestätigten Spieltreffer, ein Portrait-Grid, einen statischen Hero, ein transparentes Logo und einen animierten WebP-Hero kontrolliert laden. Erst danach wird die Bibliotheksbreite und automatische Auswahl erweitert.
