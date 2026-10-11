<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# FrameRift working rules

- Read Atlas Start through Proxy MCP: `06b0f012-18a4-44a5-bfcc-64e29d4632d3`; search Atlas for existing project knowledge.
- Re-read these bullets before implementing a new slice.
- Commit completed task changes before finishing; never discard unrelated user changes to make Git clean.
- For local development, keep the desktop launcher pointed at this checkout's `build/framerift`; do not install a duplicate executable.
- Keep it simple: implement the current need, with no scaffolding for future features.
- Write code, identifiers, comments and UI text in English.
- Keep data and behavior in their domain; reuse existing code before adding a file or abstraction.
- Keep SQLite game/cache queries in `src/library`; keep GTK presentation in `src/ui`.
- Build the app header once outside the page stack; keep its layout, styles and controls identical on every page. Pages own only their content.
- Keep conversion and process launching in `src/runtime`; reuse the AnyPS5 launcher with explicit original-game, generated-code and shared-runtime paths.
- Rebuild only generated game code; preserve original files, runtime data, saves, shader caches and shared runtime libraries.
- Use VTE only for profiler output; keep direct process capture and full session logs in `src/runtime`, bound scrollback and keep the game independent of tab navigation.
- Let the app scan configured game folders and import metadata; never seed actual installations manually.
- Use stable game IDs for managed cache, save and log directories; never derive paths from game titles or installation folder names.
- Keep saves and logs in `saves/` and `logs/` beneath the shared FrameRift data directory.
- Display game titles from metadata in the UI; never use installation folder names as display titles.
- Focus the current slice on the game library; add other screens only when requested.
- Keep cover sizes stable; use wider windows for additional grid columns.
- Prefer the standard library and dependencies already in the native GTK stack.
- Build the UI from database records; disable unavailable actions.
- Keep provider, demo and implementation explanations out of the library UI.
- Use real game artwork from the API; never create or generate game illustrations.
- Preserve user cache records; replace demo records transactionally after successful API loading.
- Keep API keys outside the repository, database, logs and command arguments.
- Configure artwork credentials in Settings and store them in the desktop keyring; read secrets outside the UI thread.
- Keep callbacks and resource ownership explicit; never let exceptions cross C callbacks.
- Use a few meaningful checks for data behavior and a native UI smoke check, without a test framework.
- Build and verify the changed behavior; keep documentation about the current state concise.
