<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Contributing to FrameRift

FrameRift implements a native GTK4 library with automatic folder scanning, metadata import, a SQLite/artwork cache, animated heroes, Settings with keyring credentials, Linux launch/rebuild and Profiler Live/Logs with complete session logs, UDP telemetry and HTTP MCP game control. See the [README](README.md) for setup and the [roadmap](docs/roadmap.md) for planned extensions. Window embedding, detailed per-frame CPU/GPU attribution, captures, input scripts and controller navigation remain planned; Windows and gameplay compatibility remain unverified.

Read [AGENTS.md](AGENTS.md) before implementing changes. Keep changes small, write code, comments and UI text in English, and use the existing domains: database access in `src/library`, GTK presentation in `src/ui`, and conversion and process launching in `src/runtime`. Add files and abstractions only when the current need justifies them.

Prioritize the library and reliable launch behavior. Preserve original games and shared runtime libraries. Rebuild only generated code; keep saves and runtime data in the shared data directory and capture full session logs independently of the visible console. Use stable game IDs for managed paths. Technical experiments with launch, window integration, input focus or telemetry should identify the platform, display backend, compiler, toolkit and AnyPS5 source revision, with observed behavior and limitations. A Windows or X11 experiment does not establish native Wayland embedding support.

Documentation must distinguish implemented behavior, plans and unverified assumptions. Pin AnyPS5 references to a commit when describing specific behavior. Use official documentation and project sources as evidence for dependencies. The native UI direction is described in [Design](docs/design.md).

Keep game files, private API keys, user settings, large captures and unattributed artwork collections out of contributions. Game artwork comes from the API; generated replacement illustrations are not used. Document image sources, available author information and rights. Store API keys in the desktop keyring, outside command arguments, the database and logs; `STEAMGRIDDB_API_KEY` is an environment override. Read secrets outside the UI thread.

Original FrameRift code and documentation use [AGPL-3.0-or-later](LICENSE), including the attribution term in [NOTICE](NOTICE). Contributions must be compatible with these terms. Preserve the FrameRift credit and original project URL. Third-party artwork and dependencies retain their own licenses.

Use a few meaningful data checks and a native UI smoke check without a test framework. Preserve existing cache data and disable unavailable actions. Provider, demo and implementation explanations belong in documentation rather than the library UI. Avoid adding a large dependency chain for a visual effect.
