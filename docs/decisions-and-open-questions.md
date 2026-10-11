<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Decisions and open questions

## Decisions

- FrameRift is a native desktop library and performance workspace; Linux is the current implementation platform.
- GTK4/C++20/CMake; SQLite/artwork cache; desktop-keyring credentials.
- Separate game process/window; modified AnyPS5 runtime required.
- Library and Profiler Live/Logs share one shell. UDP connection is separate from process/monitoring state.
- UI and MCP share one managed session and normalized measurement model.
- Stable game IDs for code/save/log paths. Rebuild touches generated code only.
- SteamGridDB supplies real artwork; generated concepts are design references.
- Original code/docs use [AGPL-3.0-or-later](../LICENSE) and attribution terms in [NOTICE](../NOTICE).

## Open questions

- Verified gameplay/platform matrix and clean Windows/Linux packaging.
- Window embedding, backend choice, resizing, high DPI and input focus across Windows/X11/Wayland.
- Accurate per-frame CPU and delayed GPU attribution; shared time domains and measurement overhead.
- Snapshot/export format, asynchronous capture costs and artifact association.
- Input hook/scheduling semantics, recovery after lost events and controller navigation.
- Multiple installations/builds, manual artwork matching and local-icon fallback.
- Dependency packaging/license inventory; AnyPS5 remains separately licensed.

## Where to look

- [Current architecture](architecture.md), [runtime integration](anyps5-integration.md), [implemented bridge](debug-bridge.md).
- [Profiler limits](profiling.md), [capture/script plan](profiler-workspace.md), [window plan](game-window-integration.md).
