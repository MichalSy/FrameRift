<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Roadmap

## Available now

- Native Library/Settings, scanned metadata, search/sorting/recent games and cached SteamGridDB artwork.
- Linux guarded Play/Rebuild, complete logs and one shared UI/MCP game session.
- HTTP MCP game/bridge tools; optional AnyPS5 UDP sender with independent monitoring.
- Live frame pacing/history/selection, shader/work activity and scoped memory; Logs console.
- Linux desktop entry/icon. General gameplay compatibility, Windows and embedded mode remain unverified.

## Remaining work

- Per-frame Driver CPU consumers and delayed GPU program/batch attribution with verified sources/windows.
- Save/open/export measurement snapshots; optional asynchronous screenshots with requested/actual frame identity.
- Durable per-game input scripts, runtime scheduling/cancellation and capability-gated UI/MCP tools.
- Window integration and focus/controller validation; Windows packages and clean Linux distribution checks.
- Manual artwork correction, local-icon fallback and richer launch/multiple-installation profiles.
- Controlled monitoring-overhead comparisons using the same scene, settings and cache state.

## Scope rules and references

- Plans are not delivery promises; add collectors/actions only when their source and semantics are verified.
- Keep one active game, bounded telemetry/log presentation and unavailable states explicit.
- Preserve original games, saves, logs and caches; avoid speculative framework work.
- Current behavior: [README](../README.md), [Profiler](profiling.md), [MCP/UDP](debug-bridge.md).
- Later capture/script contracts: [workspace plan](profiler-workspace.md). Open choices: [decisions](decisions-and-open-questions.md).
