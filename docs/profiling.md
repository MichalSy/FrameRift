<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Profiling and frame analysis

The Profiler has **Live** and **Logs** views for an externally launched game. The Logs view's output-only VTE terminal receives merged stdout/stderr directly, interprets terminal colors and progress updates, and retains 5000 scrollback lines. The runtime also writes the complete raw session log at `<framerift-data-root>/logs/<game-id>/<session-id>/game.log`; display-only LF-to-CRLF normalization leaves that file unchanged. There is no shell, PTY or file polling. Disable Follow output and scroll up to inspect earlier output; the Copy button, Ctrl+C or Ctrl+Shift+C copies selected text. Logs remain visible after process exit and the terminal clears for the next session.

The Live view uses the [UDP debug bridge and MCP server](debug-bridge.md). Turn **Monitoring** on to receive successful main-output flip summaries from a bridge-enabled AnyPS5 build. The dashboard updates once per second with estimated FPS, mean/P95/worst flip interval, sample coverage, latest/worst frame details and a bounded 60-second history. Selecting a chart point preserves that frame until Follow live is resumed. Collection continues across tab changes; stop/disconnect retains the last observation with an explicit inactive/stale state.

The same normalized model is available through `get_monitoring_snapshot`. Game-side monotonic timestamps determine intervals and source windows; UDP receive age determines freshness. The first completion after activation or output reset has no interval. Per-stream completion IDs identify missing samples separately from general packet gaps and producer drops. The dashboard does not acknowledge telemetry, parse timing files or enable existing timing/draw/GPU log settings. CPU hotspots, GPU execution tables, saved snapshots and screenshots remain later increments.

The **Shader & memory** panel shows registration-preparation jobs, translation activity/completions, cached variants, shader CPU payload, disk-cache counters and scoped memory gauges. Resource changes are coalesced independently of frames, with a five-second refresh and ten-second freshness limit. Linux RSS is sampled once per second by the bridge worker. Texture-cache accounting and the last existing Vulkan budget query are published at their own update sites; GPU source age remains visible. Resource data belongs to the latest resource sample, not to an inspected frame. See the [resource field contract](debug-bridge.md#shader-and-memory-resources) for units, scopes and unavailable values.

The panel also shows specialization, pipeline build/optimization, shader-module creation, presentation/acquisition/idle/graphics waits and queue submission. Rows show current active calls plus completed/failed calls and elapsed milliseconds in the explicitly labeled observed resource window. Hover for process totals and longest completed attempt. Work includes nested elapsed wall time, not GPU execution or additive CPU utilization. Missing fresh frames with no instrumented active work are labeled **stall cause unknown**. This extension is available through MCP as `resources.activity`, `resources.activity_window` and `stall_evidence`; older adapters keep their original gauges with unavailable extra timings.

Confirmed AnyPS5 details refer to [upstream `197ffac97203d715a258a28b432fa2eedbf8be6f`](https://github.com/boykopovar/AnyPS5/tree/197ffac97203d715a258a28b432fa2eedbf8be6f). This source snapshot is not a FrameRift runtime benchmark. See [AnyPS5 integration](anyps5-integration.md), [Controller and input](controller-input.md) and the [project overview](../README.md).

## Confirmed measurement sources

AnyPS5 already contains instrumentation. FrameRift must distinguish displaying existing data from introducing a new measurement interface.

| Source | Activation in the examined AnyPS5 version | Existing output | Potential use in FrameRift |
| --- | --- | --- | --- |
| Frame timing | Build option `APS5_ENABLE_TIMING_LOG=ON`, disabled by default | `frame-timing.log` | Basis for an initial file import and timing charts |
| Draw/driver profiling | Presence of environment variable `APS5_PROFILE_DRAW` | Diagnostics and counters, mainly through the profiling output path to `stderr` | Basis for a log view and selected statistics |
| GPU timing | Presence of environment variable `APS5_PROFILE_GPU` | Vulkan timestamp results and `[gputime]` aggregates | Basis for GPU program and command-class hotspots |
| General diagnostics | Runtime path and settings | stdout/stderr, errors and exit status | Troubleshooting and placing events within a session |

Both profiling variables are checked for **presence** in the examined paths. A variable set to `0` remains present. FrameRift should remove the variable when disabling profiling rather than setting it to the string `0`. Sources: [Build option](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/CMakeLists.txt#L26), [Draw activation](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L38), [GPU activation](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L1790).

The code's presence does not prove that every distributed runtime library set includes frame timing. The Live UI checks the session's monitoring capability and marks missing measurements as unavailable.

## Existing frame-timing format

`PerformanceTimer.hpp` captures fields including:

- Frame ID, associated submission range, flip submission and packet offset.
- Video-output handle, buffer index and flip argument.
- Timestamps for first submission, first execution, flip packet and flip completion.
- Durations between those timestamps and the interval between flip completions.
- Worker time, attributed and unattributed worker time.
- GPU busy/gap values, batch counters, packet count and a slow-packet entry.
- Phase metrics as count, total duration, maximum duration and, where applicable, bytes.

The header explicitly describes metrics as inclusive. Background metrics also summarize work completed since the preceding flip. Source: [Capture and metric format](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L154).

The present loop determines the interval between consecutive flip completions and passes the record to the timing logger. If enabled, the runtime writes `frame-timing.log` relative to its game working directory, currently `<framerift-data-root>/saves/<game-id>/`; FrameRift does not yet import this file. The logger writes asynchronously to a buffered file and flushes periodically. File monitoring can therefore lag behind the current game state. Sources: [Present loop](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L587), [Timing logger](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/FrameTimingLog.hpp#L18).

## Naming FPS and frame time accurately

The record contains `endpoint=flip_complete` and `display_confirmed=0`. This is an explicit limit: the measurement does not confirm when the image appeared on the monitor. FrameRift should initially display **flip interval** and **estimated flip rate**, with the measurement point explained in a tooltip or details. Source: [Endpoint labeling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L159).

For a valid positive interval, a simple derived rate is `1000 / flip_interval_ms`. The dashboard overview divides the number of received valid intervals by their total duration over the last source second. P95 uses the nearest-rank percentile. Sample coverage accompanies these values; a partial observation is not a complete benchmark. These calculations do not measure new game images, actual monitor FPS, display latency or input-to-image latency.

Missing intervals, a session's first frame, incomplete data and discarded measurements should remain visible as gaps. A missing value must not appear as `0 ms` or infinite FPS. Repeated presentations and multiple output handles need separate evaluation.

## CPU measurement limits

Existing CPU timings use `std::chrono::steady_clock` to measure elapsed time in instrumented phases. This can include work, waiting, synchronization or interruptions. It is not a complete CPU sampling profile of all game threads. Source: [Clock and metric structure](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L20).

Inclusive phases may nest or overlap. FrameRift must not add them into an apparent total CPU frame time without checking their relationships. Background work completed between two flips is not necessarily attributable to just one of those frames.

The resource panel already includes Linux process RSS from the bridge worker, with its own sampling age. Process CPU utilization remains a possible later source. Whole-process CPU utilization, the cost of an instrumented driver phase and the CPU time of a particular game thread are different quantities.

## Existing GPU profiling and its limits

The recorder creates Vulkan timestamp query pools and places start/end markers around instrumented regions. Output aggregates GPU times by program and command class over roughly ten seconds. It includes batch spans, covered ranges, unattributed time and a counter for measurements dropped at the range limit. Hotspot output lists a limited number of the most expensive programs. Sources: [Query creation and markers](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L1880), [Evaluation and aggregates](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp#L2333).

FrameRift could build a GPU hotspot table from this output. Existing aggregates do not provide a complete GPU timeline for each frame. Program addresses are not automatically resolved shader names. Instrumented GPU duration is not overall GPU utilization, temperature or VRAM usage.

Vulkan queries return results asynchronously. Availability, queue support, timestamp resolution and query cost must be checked on the actual GPU. A shared CPU/GPU timeline needs a defined mapping between time domains; raw GPU ticks and host time cannot simply be equated. This extension remains open. Source: [Vulkan queries and timestamp queries](https://docs.vulkan.org/spec/latest/chapters/queries.html#queries-timestamps).

GPU results may become available only after a later flip. The existing timing code describes GPU busy/gap data for completed batches; it does not guarantee that every value belongs exclusively to the currently displayed frame. Source: [Completed-batch attribution](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp#L145).

## Planned measurement view

The proposed [profiler workspace](profiler-workspace.md) uses the following order, without requiring a full debugger:

1. Game session with process status and live logs; available in the console.
2. Structured frame summaries from the implemented bridge adapter; timing-file import remains a separate possible source.
3. The implemented **FPS (estimated)** card, flip-interval chart and statistics with explicit calculations.
4. Implemented selected-frame details and latest shader/memory state; detailed driver/GPU execution tables remain planned, with their actual measurement window and source.
5. Immutable telemetry snapshots and optional asynchronous screenshots with frame/capture association.
6. Durable per-game input scripts, optional scheduled starts and capture checkpoints, shared by UI and future MCP tools.

Pausing charts should initially freeze only the FrameRift view. It does not stop the game process or GPU. Breakpoints, single-stepping, call stacks, memory inspection and shader debugging require further debugger or capture integration. A window titled "Profiler" does not provide those capabilities by itself.

## Planned structured telemetry

In the longer term, an explicit optional telemetry interface is preferable to continually interpreting human-readable log lines. It would need to produce data at suitable AnyPS5 measurement points; FrameRift alone cannot reliably read internal driver phases from another process's window.

The following fields are a **proposal**, not an existing AnyPS5 protocol:

| Field group | Proposed contents |
| --- | --- |
| Protocol | Version, record type, available capabilities |
| Session | FrameRift session ID, process identity, title ID, artifact/library version |
| Correlation | Measurement source, frame ID, output handle, submission/batch ID, queue ID where available |
| Time | Defined time origin, unit, time domain, start/end or interval |
| Quality | Completeness, drop counter, measurement resolution, valid/not yet available |
| Metric | Stable name, unit, inclusive/exclusive semantics, count, sum, maximum |

A frame ID is valid within a uniquely named session and measurement source. It is not globally unique across restarts. Late GPU data should retain its original submission/batch correlation; arrival time in the launcher is not frame attribution.

FrameRift's transport is IPv4 loopback UDP with versioned JSON packets, session/peer pinning and advertised capabilities. The [implemented protocol](debug-bridge.md) defines frame and shader/resource payloads; runtime execution belongs to the adapter. The detailed CPU/GPU execution records described above remain proposals.

## Overhead and backpressure

Instrumentation can change runtimes. Extra clock queries, locks, formatting, allocations, query pools and data transfer all contribute to measurement overhead. FrameRift should measure this overhead and label measurement modes clearly without promising an unsupported percentage in advance.

Existing output paths have different overload policies:

- `FrameTimingLog` limits its queue to 256 reports and throws when it is full. The present loop treats timing errors as errors and may terminate the process.
- `ProfileOutput` has a bounded buffer, drops chunks when capacity is insufficient and reports those drops. Write failures can still propagate as errors.

Sources: [Timing queue](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/FrameTimingLog.hpp#L36), [Timing error handling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L619), [ProfileOutput](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp#L41).

The optional bridge uses a bounded producer buffer with visible drop counters. A full buffer drops samples; UDP transmission never waits for receipt. Data formatting and UI updates happen outside hot rendering and driver paths. Detailed CPU/GPU measurements remain future work and should be enabled deliberately.

## Open experiments and later validation

| Experiment | Question to resolve |
| --- | --- |
| Timing import | Can versions, units, partially written lines and data gaps be detected reliably? |
| Frame correlation | How are multiple outputs, queued work and late batches attributed? |
| GPU timeline | Which new events and time calibration are needed beyond the existing aggregates? |
| IPC | What event rate and runtime overhead can the implemented UDP transport sustain? |
| Overhead | How do game-only execution, logs, frame timing, GPU profiling and FrameRift display compare? |
| UI | Do navigation and charts remain responsive under heavy measurement or log traffic? |

Later checks should cover calculations against known datasets, missing/invalid values, protocol versions, overload, connection loss and process restarts. Integration measurements should use identical game/driver/library conditions and multiple runs. A design image is neither a benchmark nor confirmation of game compatibility.
