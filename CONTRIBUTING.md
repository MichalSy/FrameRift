<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Contributing to FrameRift

Published `main` contains the native GTK4 library, SQLite cache and SteamGridDB sample artwork. Folder scanning, Linux launch and rebuild, artwork settings and the Profiler console are development work. See the [README](README.md) for build instructions and status, and the [roadmap](docs/roadmap.md) for planned extensions.

Read [AGENTS.md](AGENTS.md) before implementing changes. Keep changes small, write code, comments and UI text in English, and use the existing domains: database access in `src/library`, GTK presentation in `src/ui`, and conversion and process launching in `src/runtime`. Add files and abstractions only when the current need justifies them.

Prioritize library presentation, search, sorting, selection and cache preservation. Technical experiments with launch, window integration, input focus or telemetry should identify the platform, display backend, compiler, toolkit and AnyPS5 source revision. Describe observed behavior and limitations. A working Windows or X11 experiment does not establish native Wayland embedding support.

Documentation must distinguish published behavior, development work, plans and unverified assumptions. Pin AnyPS5 references to a commit when describing specific behavior. Use official documentation and project sources as evidence for dependencies.

The UI direction is described in [Design](docs/design.md). Technical changes should support the product goals: a native Windows/Linux application, a library and a separate game/profiler workspace, direct controls and accurately interpreted measurements.

Keep game files, private API keys, user settings, large captures and unattributed artwork collections out of contributions. Game artwork comes from the API; generated replacement illustrations are not used. Document image sources, available author information and rights. API keys belong outside command arguments, the database and logs. Development artwork settings use the desktop keyring; `STEAMGRIDDB_API_KEY` is an environment override.

Original FrameRift code and documentation use [AGPL-3.0-or-later](LICENSE), including the attribution term in [NOTICE](NOTICE). Contributions must be compatible with these terms. Preserve the FrameRift credit and original project URL. Third-party artwork and dependencies retain their own licenses.

Use a few meaningful data checks and a native UI smoke check without a test framework. Preserve existing cache data and disable unavailable actions. Provider, demo and implementation explanations belong in documentation rather than the library UI. Avoid adding a large dependency chain for a visual effect.
