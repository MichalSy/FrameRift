<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Architecture

## Current boundaries

- Native GTK4 UI; the converted AnyPS5 game runs in a separate process/window.
- One managed game or preparation at a time. UI and MCP share its lifecycle.
- Process state, UDP connection, confirmed monitoring and measurement freshness are independent.
- Tab navigation changes presentation; it does not end the game, log capture or telemetry reception.
- No embedded renderer, saved recordings or input-script manager yet.

## Where to find things

| Area | Location | Responsibility |
| --- | --- | --- |
| Library | `src/library/library.cpp` | SQLite, configured folders, metadata scanning, titles/IDs and play history |
| Artwork | `src/library/steamgriddb.cpp` | API matching, downloads and cached assets |
| App shell | `src/ui/app.cpp` | Shared header, Library, Settings and action wiring |
| Hero | `src/ui/hero.cpp` | Selected artwork, WebP animation and crossfade |
| Profiler | `src/ui/profiler.cpp` | Live dashboard, selection, resources and VTE Logs |
| Launch | `src/runtime/launch.cpp` | Build selection, conversion, guarded process launch and generated-code rebuild |
| Session | `src/runtime/session.cpp` | Shared UI/MCP state, cancellation/stop and log tails |
| Output | `src/runtime/log.cpp` | Direct pipe capture, complete raw logs and bounded UI delivery |
| UDP/model | `src/runtime/bridge.cpp`, `bridge.hpp` | Peer/session pinning, events/commands, normalized frame/resource history |
| MCP | `src/runtime/mcp.cpp` | Loopback HTTP transport and tool schemas |
| Resources | `assets/ui`, `assets/framerift.gresource.xml` | Bundled styles and app marks |
| Checks | `tests`, `CMakeLists.txt` | Library, launch, UDP/HTTP and managed-session checks |

## Data flow

- Settings/folder scan -> SQLite records -> Library UI and MCP game listing.
- Play/MCP start -> background preparation -> memory guard -> AnyPS5 launcher -> external game.
- Game stdout/stderr -> runtime capture -> disk logs and bounded VTE display.
- Runtime UDP -> worker/model -> Live UI and MCP snapshot; no log parsing for live metrics.
- Commands -> UDP worker -> runtime receipt/completion -> shared observed state.
- Detailed transport and bounds: [MCP/UDP](debug-bridge.md). Storage and launch: [AnyPS5 integration](anyps5-integration.md).

## Maintenance rules

- Database queries stay in `src/library`; GTK stays in `src/ui`; launch/transport stays in `src/runtime`.
- Header is built once outside the page stack; connection status beside Profiler, search/refresh only in Library.
- Background work owns blocking scans, secrets, downloads and preparation. GTK updates stay on the main thread.
- Preserve original games, saves, logs and shared runtime/cache data. Use stable IDs for managed directories.
- Keep callback ownership explicit; exceptions must not cross C callbacks. Full rules: [AGENTS.md](../AGENTS.md).
