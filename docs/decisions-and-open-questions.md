<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Decisions and open questions

## Product decisions

| Topic | Decision |
| --- | --- |
| Name | **FrameRift** |
| Audience | Players and users investigating game performance |
| Platforms | Native Windows and Linux desktop application; Linux is the current development platform |
| UI | Custom, modern and compact, with dark graphite surfaces, cyan accents and restrained animations |
| Views | Library first; a Profiler console is in development, with an embedded game/profiler view planned |
| Library | Selected-game hero, then **Recently played**, then **Your games** |
| Planned game view | Game at the center, with optional analysis panels |
| Planned game-view modes | **Playing** and **Profiler** |
| Artwork | SteamGridDB as the primary source, including animated assets when available |
| Project boundary | Independent launcher with a small integration to the modified AnyPS5 fork |
| Storage | Games and measurements are local; the UI does not add redundant local-game labels |
| License | Original code and documentation use [AGPL-3.0-or-later](../LICENSE), including the attribution term in [NOTICE](../NOTICE) |

The two [design concepts](../assets/design/README.md) illustrate this direction. Their game artwork and measurements do not establish compatibility or implemented profiling features.

## Technical choices and assumptions

| Choice or proposal | Purpose | Remaining validation |
| --- | --- | --- |
| C++20 and CMake | Current native implementation and platform integration | Reproducible Windows build alongside Linux |
| GTK4 | Current library toolkit | Windows packaging, window integration and animation behavior |
| Optional libadwaita | Adaptive native components if needed | Styling and deployment requirements |
| Qt 6 Quick/QML alternative | Custom native UI and relevant window APIs | Equivalent integration experiment if the GTK approach proves unsuitable |
| libcurl, JSON-GLib and SQLite | Artwork requests, metadata and library cache | Packaging and maintenance requirements |
| Optional telemetry protocol | Measurements with clear semantics and frame correlation | Small runtime sender with measurable overhead |
| Cooperative window host | Running game inside the application | Separate evidence for Windows, X11 and a Wayland desktop session |

GTK4 is implemented; optional dependencies and embedding approaches remain proposals. Window integration may influence future toolkit choices.

## Open technical questions

- Which window approach works with real AnyPS5 artifacts: an exported native handle, a cooperative adapter or a hook for a limited platform case?
- Can host and game reliably use a shared X11/XWayland path in a Wayland session? What limits remain for a native Wayland UI?
- How should game, keyboard/mouse and controller focus behave while analysis fields are active? Background controller input needs explicit handling.
- Which CPU/GPU measurement points should the first timing profiler provide? What does a frame ID mean across multiple submissions and delayed GPU results?
- What overhead is acceptable for each measurement mode, and how should comparisons be reproduced?
- Which capture and export formats should come first? Image capture is an additional data source with its own costs.
- Can the development WebP animation path be verified reliably and efficiently on Windows?
- How should multiple game outputs, builds and launch profiles be imported and displayed with minimal complexity?
- How should manual artwork selection work, and how should desktop-keyring settings be packaged on Windows?
- What distribution requirements apply to the dependency licenses? AnyPS5 stays separately licensed under GPL-2.0-only; VTE uses LGPL-3.0-or-later.

## Extensions without a first-release commitment

Detailed GPU timelines, shader hotspots, import analysis, automated performance tests, comprehensive controller navigation and broader build/relink automation remain extension ideas. Keep their descriptions concrete enough to prioritize later.

The ASTRO BOT artwork in the design concepts determines neither game support nor measurement availability. Both require evidence from real builds.
