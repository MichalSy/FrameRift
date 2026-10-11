<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Toolkit and platforms

## Current choice

- GTK4 C APIs from C++20, with GLib/GIO ownership and asynchronous work.
- Native local UI, custom graphite/cyan styling, GResources and WebP heroes.
- libsoup supplies an automation endpoint; the visible UI does not use a browser/WebView.
- libadwaita is optional future work. Qt Quick/QML remains an evaluated window-integration alternative, not an implementation.
- Dependencies/commands: [technical stack](technical-stack.md). Domain/source map: [architecture](architecture.md).

## Platform status

| Platform | Status |
| --- | --- |
| Linux | Current development/build and guarded external launch path |
| Linux/X11 | Native UI/game-window checks exist; embedded mode is not implemented |
| Wayland/XWayland | External presentation remains the current approach; focus/backend combinations need validation |
| Windows | Target platform; packaging, transport and launch unverified |

## Open validation

- Clean packages, dependency provenance, fonts/decoders and desktop-keyring availability.
- High DPI, long titles, large libraries, interrupted downloads and heavy logs/telemetry.
- Actual renderer/load with idle library, animated hero and active Profiler; toolkit choice is not a performance benchmark.
- Embedded Windows/X11 resize, focus, fullscreen and crash handling; native Wayland needs its own strategy.
- Qt's foreign-window APIs are a candidate only if an explicit integration experiment justifies a toolkit change.
- [Window integration plan](game-window-integration.md), [open decisions](decisions-and-open-questions.md).
