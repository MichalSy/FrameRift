<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Profiler: Live and Logs

## Connect and use

1. Select the modified AnyPS5 checkout in Settings; build its `relinker` and patched `libs` with `ANYPS5_ENABLE_DEBUG_BRIDGE=ON`.
2. Start a scanned game with Play or MCP `start_game`.
3. The game receives FrameRift's UDP address/session. Its `hello` establishes the connection; FrameRift then requests monitoring automatically.
4. In **Live**, use **Monitoring** to switch collection, **FPS / Flip interval** for the chart and a chart point to inspect a frame. **Follow live** resumes selection updates.
5. Use **Logs** for process output. Switching pages leaves the game, receiver and log capture running.

- Header status reports the UDP handshake. Process `running` alone does not mean connected, rendering or playable.
- The runtime starts monitoring off; receipt of the request differs from confirmed activation. Manual OFF remains for that session; a new managed launch requests ON again.
- Collection is independent of timing-file/draw/GPU logging. Connection-only/older adapters expose only their supported measurements.
- Addresses, command status, protocol and failure diagnosis: [MCP/UDP](debug-bridge.md).

## What Live measures

| Display | Meaning |
| --- | --- |
| Estimated FPS | Valid flip-interval count divided by summed duration, over the latest source-time second |
| Mean / P95 / worst | Main-output flip-completion intervals; P95 uses nearest rank |
| FPS history | Completed flips in the preceding full source second; a reliable baseline is required |
| Selected / latest / worst frame | Runtime correlation ID and output stream; selection does not pause the game |
| Shader activity | Translation, specialization, pipeline build and shader-module work; active gauges and process-lifetime completed counts |
| Elapsed / window | Completed/failed work wall time over the displayed observed resource window |
| Memory | Linux process RSS, texture-cache accounting/budgets and driver-estimated device-local heap usage/budget |

- UI refresh: at most once per second. History: up to 60 source seconds / 8192 samples; MCP returns bounded presentation history.
- FPS plot uses 0-75 with labels at 13, 30 and 60; values above the plot range remain in measurements. Interval scale is automatic.
- Catch-up flips do not become individual reciprocal-interval FPS spikes. Missing samples and long pauses break the line; the time axis advances during stalls.
- Coverage, gaps, source window and ages are in **Info**. Tooltips retain precise values; displayed milliseconds are rounded.
- Frame data becomes stale after one second without a sample, even with heartbeats. Resources have independent freshness and source ages.
- OFF/disconnect retains the last observation visibly inactive/stale; new session/epoch clears history. Output-generation changes clear frames but preserve process resources.
- Resource updates do not belong to the selected frame. A cached GPU sample keeps its original age.
- The consumer card shows **No data** until per-frame attribution exists; unavailable Driver CPU/GPU modes stay disabled.

## Interpretation limits

- Flip completion is not confirmed monitor scanout, simulation/input ticks or input-to-image latency.
- Work durations include waits and scheduling, overlap and nest. Do not add them into frame time, CPU utilization or GPU execution time.
- All instrumented waits/submission/preparation records are exposed in MCP `resources.activity`; `activity_window` carries observed deltas and actual window duration.
- A stale frame stream with fresh active work reports evidence, not a proven cause. Otherwise the cause is unknown; zero active work does not prove idle.
- Cache bytes and heap estimates have different scopes; do not sum them into total RAM/VRAM.
- No detailed CPU sampling, GPU program timeline, timing-file import, saved snapshots, screenshots or input scripts yet. [Remaining workspace plan](profiler-workspace.md).

## Logs and files

- Output-only VTE; merged stdout/stderr, ANSI colors/progress and 5000 scrollback lines. No shell, PTY or file polling.
- Turn off **Follow output** to inspect history; Copy, Ctrl+C or Ctrl+Shift+C copies selected text.
- Complete raw output: `<data-root>/logs/<game-id>/<session-id>/game.log`; conversion: `conversion.log`; guard samples: `memory.jsonl`.
- Logs remain after exit and the console clears on the next session. MCP `get_game_log` defaults to five lines from the current/latest managed session.
- AnyPS5's optional `frame-timing.log` belongs to the game's working directory; FrameRift does not import it.

## Source map

- Receiver/model: `src/runtime/bridge.cpp`; dashboard/console: `src/ui/profiler.cpp`; process output: `src/runtime/log.cpp`.
- AnyPS5: `core/libs/prx/libkernel/System/src/DebugBridge.cpp`, `libSceVideoOut/src/VideoOutDriver.cpp` and `libSceAgcDriver/Execution/include/PerformanceTimer.hpp`.
- GPU queries/aggregates: AnyPS5 `libSceAgcDriver/Graphics/src/Recorder.cpp`; cache/budget accounting: `Graphics/src/ShaderResources.cpp`.
- Native screenshots: [profiler visual reference](profiler-design-review.md). No monitoring-overhead benchmark is established.
