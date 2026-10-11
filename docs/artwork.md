<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Artwork

## Current behavior

- SteamGridDB is the primary source for real covers, heroes and title logos.
- Folder scans supply metadata titles/IDs; API matching and asset paths are cached in SQLite.
- Cached images/metadata work offline. Downloads run in background work; failed loading preserves user records.
- Successful API loading replaces demo records transactionally; installed user records remain intact.
- Animated WebP heroes use libwebp/libwebpdemux; only the selected visible animation advances. Hidden/disabled animations pause.
- Missing artwork stays usable with a quiet placeholder. Manual match/asset correction and local `icon0.png` fallback remain planned.

## Credentials and storage

- Save the SteamGridDB API key in Settings; libsecret stores it in the desktop keyring.
- `STEAMGRIDDB_API_KEY` overrides the saved key. Read secrets outside the UI thread.
- Keep keys out of SQLite, logs, command arguments and game environments.
- Cache/database: `$XDG_CACHE_HOME/framerift`, normally `~/.cache/framerift`; database is `games.sqlite3`.
- Public source pages and attribution belong in artwork metadata/documentation, not technical Library labels.

## Where to find it

- API/matching/downloads: `src/library/steamgriddb.cpp`; persistence/scanning: `src/library/library.cpp`.
- Settings/worker wiring: `src/ui/app.cpp`; animation/rendering: `src/ui/hero.cpp`.
- [Design image sources and rights](../assets/design/README.md), [SteamGridDB API documentation](https://www.steamgriddb.com/api/v2).
- Use real API artwork in the application; never generate substitute game illustrations. Third-party rights are separate from the app license.
