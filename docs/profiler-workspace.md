<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Profiler workspace: remaining plan

## Implemented foundation

- Live/Logs, frame pacing/selection/history, shader/work activity, memory and shared MCP snapshots.
- Game remains in its external window; process, connection and monitoring states are independent.
- Current usage/limits: [Profiler](profiling.md). Current tool/protocol contract: [MCP/UDP](debug-bridge.md).

## Planned analysis and captures

- Per-frame Driver CPU consumers and delayed GPU program/batch records need accurate source/window attribution.
- Inclusive/nested work is not additive CPU/GPU frame time; GPU results retain original batch/frame identity.
- Save immutable retained measurements with session, stream, coverage, source versions and actual aggregate windows.
- Optional screenshot requests report requested and actual frame separately; retaining telemetry does not capture an old image.
- Readback/encoding is asynchronous but still has GPU cost; mark capture work for benchmark exclusion.
- Proposed artifacts: `<data-root>/logs/<game-id>/<session-id>/captures/<capture-id>/`, with manifest, telemetry and optional image.
- Pixels do not fit the UDP envelope; use owned local artifact IDs, not arbitrary runtime paths.
- Captures tab, save/open/compare/export and capture commands are not implemented.

## Planned input scripts

- Structured digital-pad actions, not shell/Python code; game-scoped saved definitions and immutable run revisions.
- Proposed durable store: `<data-root>/profiles.sqlite3`; SQL/validation belongs in `src/library`. Cache cleanup must preserve scripts.
- Default start: next available runtime input boundary after acceptance. Optional delay or absolute start frame.
- Offsets are relative to the previous planned action. Explicit button down/up; preserve row order and neutral final state.
- Example: absolute start 500, down at +0, up at +20, right down at +0, right up at +40 -> targets 500, 520, 520, 560.
- Flip IDs are not simulation ticks; runtime must define the input boundary and report requested/applied frames.
- Validate the complete plan before installation: matching game/session, balanced transitions, supported actions, safe integer totals and no past start.
- Proposed bounds: 128 steps and complete command <=16 KiB; no fragmentation. Missed targets fail rather than silently shift.
- One run owns pad 0; release injected buttons on completion/cancel/failure/exit. Accepted finite runs execute locally without waiting for the UI/network.
- Runtime registration receipt/completion differs from action completion; capture outcomes remain separate.
- Planned events cover start, applied steps, capture outcomes and terminal run status. Disconnect/lost events mean unknown status until recovery.
- Proposed recovery command `get_input_run` needs a future allowlist/capability extension; it is unavailable today.

## Proposed MCP tools (not available)

- `list_game_scripts`, `run_game_script`, `get_input_run`, `cancel_input_run`.
- Use saved IDs/revisions and capability checks; no arbitrary executable paths/code.
- Current adapter supports monitoring only, not inputs/captures. Current MCP listing is in [debug bridge](debug-bridge.md).

## Source anchors for later work

- AnyPS5 frame/driver scopes: `libSceAgcDriver/Execution/include/PerformanceTimer.hpp`, `Execution/src/Driver/Presentation.cpp`, `Execution/include/Driver/Draw/DrawTiming.hpp`.
- GPU query/aggregate records: `libSceAgcDriver/Graphics/src/Recorder.cpp`.
- Cache/budget accounting: `Graphics/src/ShaderResources.cpp`; guard samples: `tools/run_with_memory_guard.py`.
- Readback/dumps: `Execution/src/PresentationScaler.cpp`, `Execution/src/VulkanDevice.cpp`; existing dumps are not bridge capture commands.
- Paths under `core/libs/prx/` in the modified AnyPS5 checkout unless stated otherwise.
- [Concept images/prompts](../assets/design/README.md), [input constraints](controller-input.md), [roadmap](roadmap.md).
