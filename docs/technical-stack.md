<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Build and technical stack

## Required dependencies

| Dependency | Minimum / purpose |
| --- | --- |
| C++ compiler | C++20 |
| CMake | 3.20; Ninja is the documented generator |
| GTK4 | 4.12; native UI |
| GTK4 VTE | 0.70, `vte-2.91-gtk4`; output-only Profiler console |
| libcurl | 7.85; artwork API/downloads |
| libsoup | 3.2, `libsoup-3.0`; local HTTP MCP |
| SQLite3 | Library/settings/artwork cache |
| JSON-GLib, GIO | JSON, async I/O, subprocesses and UDP |
| libsecret | Desktop keyring |
| libwebp, libwebpdemux | Static/animated WebP |
| pkg-config, glib-compile-resources | Dependency discovery and embedded assets |

- Install development packages, not only runtime libraries. Exact checks/targets: `CMakeLists.txt`.
- Game launch additionally needs Python 3, Linux cgroup v2, a systemd user manager and the modified AnyPS5 launcher/guard.
- SDL/Vulkan belong to AnyPS5; the FrameRift frontend does not render the game.
- libadwaita, Qt, Tracy, spdlog and zstd are not current dependencies.

## Commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
./build/framerift
```

- CTest checks: `library_cache`, `game_launch`, `debug_bridge`, `game_mcp`; sources in `tests/`.
- Local desktop entry points at this checkout's `build/framerift`.
- Packaged installation: `cmake --install build`; Linux executable, desktop entry and icon follow the configured prefix.
- AnyPS5 is built separately with targets `relinker libs`; live bridge requires `ANYPS5_ENABLE_DEBUG_BRIDGE=ON`.
- [Runtime setup/storage](anyps5-integration.md), [MCP setup](debug-bridge.md), [platform limits](toolkit-and-platforms.md).
