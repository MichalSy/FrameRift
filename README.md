<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# FrameRift

**Launch games. Understand frames.**

Experimental native GTK4 game library and live runtime console for Linux.
Game execution requires the [modified AnyPS5 fork](https://github.com/MichalSy/AnyPS5).

## Current capabilities

- Scan configured game folders; use metadata titles and stable IDs, search, sorting and recently played games.
- SteamGridDB covers, logos and animated WebP heroes; SQLite/image cache works offline. Credentials stay in the desktop keyring.
- Play converts missing game code and launches one guarded game in an external window. Rebuild replaces only generated code.
- Profiler **Live**: estimated FPS, mean/P95/worst flip intervals, 60-second history, frame selection, shader/work activity and scoped memory.
- Profiler **Logs**: colored process output with bounded scrollback; complete logs remain on disk.
- Local HTTP MCP controls the same game session as the UI. UDP supplies independent live runtime telemetry.
- Windows launch, embedded windows and general game compatibility are unverified. Detailed per-frame CPU/GPU attribution, captures and input scripts are planned.

## Build and run

Dependencies and minimum versions: [technical stack](docs/technical-stack.md).

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
./build/framerift
```

- In **Settings**, add game folders and select the modified AnyPS5 checkout.
- Launch uses its newest complete, current patched CMake build in place; build `relinker` and `libs` there first.
- For live telemetry, configure that AnyPS5 build with `-DANYPS5_ENABLE_DEBUG_BRIDGE=ON`.
- Save the SteamGridDB key in Settings; `STEAMGRIDDB_API_KEY` overrides it.
- Local development: desktop launcher points at this checkout's `build/framerift`. Packaged installation: `cmake --install build`.
- All managed files: `$XDG_DATA_HOME/framerift` (default `~/.local/share/framerift`), with `library/`, `artwork/`, `compiled-games/`, `saves/` and `logs/`. Game directories use stable IDs. Existing `~/.cache/framerift` files and artwork references migrate automatically on startup.

## MCP and live connection

Keep FrameRift open; its default endpoint is `http://127.0.0.1:47653/mcp`.

```sh
codex mcp add framerift --url http://127.0.0.1:47653/mcp
```

- Game tools: `list_games`, `start_game`, `stop_game`, `get_game_status`, `get_game_log`.
- Bridge tools: `get_bridge_status`, `get_monitoring_snapshot`, `get_events`, `send_command`, `get_command`.
- Start/stop are asynchronous; poll status. Game tools work without UDP.
- Managed launches pass a free local UDP port and session identity to the game. After its handshake, FrameRift automatically requests monitoring.
- **Monitoring** toggles collection; tab changes keep the game and receiver active. Header status means a UDP connection, not rendering progress.
- [MCP arguments, configuration and protocol](docs/debug-bridge.md); [Live usage and measurement limits](docs/profiling.md).

## Preview and documentation

![FrameRift game library](assets/screenshots/library.png)

- [Architecture and source map](docs/architecture.md), [design](docs/design.md)
- [AnyPS5 launch/storage](docs/anyps5-integration.md), [build/platforms](docs/technical-stack.md)
- [Profiling](docs/profiling.md), [MCP/UDP](docs/debug-bridge.md), [native profiler screenshots](docs/profiler-design-review.md)
- [Artwork](docs/artwork.md), [input](docs/controller-input.md), [window integration](docs/game-window-integration.md)
- [Roadmap](docs/roadmap.md), [open decisions](docs/decisions-and-open-questions.md), [planned captures/scripts](docs/profiler-workspace.md)
- [Contributing](CONTRIBUTING.md), [design image sources](assets/design/README.md)

## License

- Original code/docs: [AGPL-3.0-or-later](LICENSE), with attribution terms in [NOTICE](NOTICE). Preserve FrameRift credit and origin link; distributed derivatives and modified network services must provide corresponding source under the license.
- AnyPS5: GPL-2.0-only. Third-party artwork and trademarks retain their own rights.
