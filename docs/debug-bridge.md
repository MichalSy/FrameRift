<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Debug bridge and MCP

## Servers and runtime setup

- FrameRift owns both servers while the primary application is open; tab changes do not stop them.
- IPv4 loopback only: `127.0.0.1`. Profiler shows actual addresses and connection state.
- `FRAMERIFT_MCP_PORT`: default `47653`; `0` chooses a free HTTP port.
- `FRAMERIFT_UDP_PORT`: default `0` (free UDP port); set a fixed port for separately launched runtimes.
- Invalid/occupied ports report server errors; UI game launch remains available. MCP requires successful UDP-server construction, even for game tools.
- Managed launch supplies `FRAMERIFT_BRIDGE_HOST=127.0.0.1`, `FRAMERIFT_BRIDGE_PORT`, `FRAMERIFT_BRIDGE_SESSION` and `FRAMERIFT_GAME_ID` to the game only.
- Converter receives none of those variables. Spawn failure/exit detaches the managed identity and rejects late handshakes.
- AnyPS5: configure the selected build with `-DANYPS5_ENABLE_DEBUG_BRIDGE=ON`, then build `relinker libs`. Default bridge build option is OFF; `APS5_DEBUG_BRIDGE=0` disables a built-in worker for one run.
- Adapter starts when VideoOut initializes; all launch variables must be valid. It advertises `set_monitoring`, `resource_status`, `work_activity`.
- Monitoring initially OFF; FrameRift requests ON after a managed handshake. Manual OFF persists for that session; next managed launch requests ON again.
- Collection is independent of `APS5_ENABLE_TIMING_LOG`, `APS5_PROFILE_DRAW` and `APS5_PROFILE_GPU`. Bridge-disabled builds omit its hooks.

## MCP connection

- Streamable HTTP: `http://127.0.0.1:47653/mcp`. Keep FrameRift open.
- Stateless POST/JSON responses; no SSE stream. GET returns 405; notifications return empty 202.
- Initialization supports `2025-11-25` and `2025-03-26`; other requested revisions negotiate `2025-11-25`.
- POST headers: `Content-Type: application/json`, `Accept: application/json, text/event-stream`; subsequent requests include negotiated `MCP-Protocol-Version`.
- Nonlocal Host/Origin rejected; no authentication token. Limits: 64 KiB body, 32 simultaneous requests, 10-second deadline.

```sh
codex mcp add framerift --url http://127.0.0.1:47653/mcp
codex mcp get framerift
```

- Reload MCP servers or start a new Codex session to discover tools; custom ports need a matching URL.
- Server schemas/dispatch are authoritative in `src/runtime/mcp.cpp`.

## Available tools

| Tool | Arguments | Result / use |
| --- | --- | --- |
| `list_games` | `{}` | Scanned visible games, stable IDs, metadata titles, launch availability and current session |
| `start_game` | `{"game_id":"<listed ID>"}` | Background preparation/launch; returns observed session status |
| `stop_game` | `{"game_id":"<managed ID>"}` | Stop matching session or cancel preparation |
| `get_game_status` | `{}` | Current/latest session, state, IDs, log paths, exit/error |
| `get_game_log` | `{"lines":5}` | Current/latest managed log tail; default 5, range 1-200 |
| `get_bridge_status` | `{}` | UDP connection/session, capabilities, frame and packet counters |
| `get_monitoring_snapshot` | `{"include_history":false}` | Same normalized model as Live; optional bounded chart/recent frames |
| `get_events` | `{"after":0,"limit":100}` | Immediate buffered events; limit 1-100, continue with `next_event_id` |
| `send_command` | `{"name":"set_monitoring","args":{"enabled":true}}` | Command ID and initial pending state |
| `get_command` | `{"command_id":"<returned ID>"}` | Attempts, receipt/completion/failure and optional result/error |

- Game tools use existing Settings and shared UI lifecycle; UDP peer/adapter is not required.
- Only listed available IDs launch; no arbitrary executable/path/shell command. One active preparation/game.
- Start/stop return immediately. Poll `get_game_status`: `preparing`, `running`, `stopping`; terminal `exited`, `stopped`, `failed`, `rebuilt`.
- `running` means a process started, not UDP connection/rendering/gameplay success. Errors remain visible; canceled preparation never later starts the game.
- Stop uses the memory guard to terminate the managed scope and drain logs.
- Log tail identifies source/path and availability/error/truncation. It reads at most the last 256 KiB; invalid UTF-8 is replaced. Falls back to conversion output when game output is absent.
- No session log is available immediately after app startup; the tool does not search earlier app sessions. Complete files stay on disk.
- `send_command` allowlist: `set_monitoring`, `set_input`, `schedule_inputs`, `cancel_schedule`, `capture_frame`; each requires the runtime capability.
- Current AnyPS5 adapter implements only `set_monitoring` as a command. Resource/work capabilities identify telemetry, not commands. No script tools, input execution or bridge screenshots yet.

## Practical diagnosis

1. `list_games` -> use the returned ID with `start_game`.
2. Poll `get_game_status`; inspect `get_game_log` for preparation/process errors.
3. `get_bridge_status` -> check matching session, connected state and capabilities separately.
4. `get_monitoring_snapshot` -> check confirmed `enabled`, state, frames, coverage and independent resource ages.
5. To override monitoring, call `send_command`, then poll `get_command`; receipt is not completion. Hello/heartbeat also confirms enabled state/epoch.

- Process running/disconnected: check bridge-enabled patched runtime, launch variables, `APS5_DEBUG_BRIDGE` and UDP server error.
- Connected/waiting: handshake succeeded but no frame sample exists. Stale means retained samples stopped; heartbeats alone do not establish progress.
- Resource activity can continue during compilation/stalls without new frames. Active work is evidence, not a proven cause.

## UDP v1 essentials

- One UTF-8 JSON object per datagram, <=16 KiB; `protocol="framerift.bridge"`, `version=1`.
- Every envelope has `type`, nonempty `session_id` and independent increasing `sequence`; optional runtime `frame` is correlation, not a simulation tick.
- IDs: <=128 bytes. Nonnegative JSON integers: <=9223372036854775807.
- Runtime sends `hello` from its persistent command-receiving socket, with `game_id`, `capabilities` and monitoring state.
- Pin one session/source address+port; wrong peers/managed IDs cannot replace it. Before a managed launch, a separate local runtime may connect with its own valid IDs.
- Heartbeats at least once per second; five seconds without a valid packet disconnects. Repeated matching hello reconnects; goodbye disconnects explicitly.
- Telemetry is unacknowledged. Formatting/transport occurs on a worker; bounded producer overflow drops samples instead of blocking the game.

```json
{"protocol":"framerift.bridge","version":1,"type":"event","session_id":"session-123","sequence":3,"frame":500,"name":"frame_timing","data":{"output_handle":1,"output_generation":1,"completed_flip_count":500,"timestamp_us":123456789,"flip_interval_us":16667,"monitoring_epoch":1,"dropped_samples":0}}
```

- Frame identity: session + output handle/generation + completed flip count. `frame` preserves global FrameTiming correlation.
- Game monotonic microseconds drive intervals/windows. First completion after activation/reset has `flip_interval_us=null`.
- Epoch changes on monitoring switches; producer drops are cumulative within epoch. Do not mix sessions/epochs/output generations.
- Hello/heartbeat uses `monitoring:{"enabled":false,"epoch":0}`; command result uses the same fields.

## Resources and work contract

- `resource_status` event has no root frame; data contains `timestamp_us`, `monitoring_epoch`, `shaders`, `memory` and optional `activity`.
- Shader groups: translation/preparation gauges and totals; retained variants/source/SPIR-V payload bytes; disk-cache operation/failure counters; pipeline build/optimization gauges/totals.
- Memory fields: RSS/value timestamp; sampled/storage cache bytes/budgets; device-local heap usage/budget/source timestamp. Unsupported memory values are null; preserve value/time pairs.
- RSS samples at most once/second on Linux. Heap data reuses driver budget queries; it is not total VRAM/system GPU utilization. Source-cache bytes exclude IR/metadata/temporary/driver allocations.
- Counters/timings are process lifetime; active/pending/retained bytes are gauges. Monitoring OFF does not reset process totals.
- Updates coalesce to at most four/second, with initial activation state and five-second full refresh. Resource receipt becomes stale at ten seconds; RSS/heap source ages stay separate.
- OFF/disconnect retains resources; new session/epoch clears observations. Output-generation reset clears frames only.
- `work_activity` requires eleven records: preparation, translation, specialization, pipeline_build, pipeline_optimization, shader_module, gpu_present_wait, gpu_acquire_wait, gpu_idle_wait, gpu_graphics_wait, gpu_submit.
- Each record: nonnegative `active`, `started`, `completed`, `failed`, `total_us`, `max_us`. Completed/failed attempts contribute whole wall duration when they finish; ongoing duration is not yet counted.
- Atomic fields are sampled independently; timings overlap/nest and include waits/scheduling. They are not GPU execution time or additive frame/CPU totals.
- MCP `resources.activity` retains lifetime records; `activity_window` has actual start/end/duration and counter deltas. `active`/`max_us` keep latest/lifetime meaning.
- Delta history: <=64 resource records / roughly ten seconds; normally spans the latest source second, possibly shorter at startup or longer with sparse events. First/decreasing-counter observations give unavailable deltas.
- With stale frames but connected enabled monitoring, `stall_evidence` is `instrumented_work_active` for fresh active resource work, otherwise `unknown`; outside that condition it is null.
- Exact payload names/validation: `src/runtime/bridge.hpp`, `bridge.cpp`; producer: AnyPS5 `core/libs/prx/libkernel/System/src/DebugBridge.cpp`.

## Commands, loss and bounds

- Command envelope adds `command_id`, `name`, object `args`. Runtime replies `command_received`, then `command_completed` with optional object `result`, or `command_failed` with optional error <=1024 bytes.
- At most three sends, roughly 500 ms apart, same ID/packet. Receipt stops retries. Runtime deduplicates by session/command ID and repeats acknowledgements.
- States: pending -> received -> completed/failed; no receipt -> timed_out; disconnect -> disconnected. Received commands can remain received after a lost completion; UDP cannot guarantee outcome delivery.
- Diagnostic ring: 512 events; `oldest_event_id`/`lost_history` report overwritten history. Duplicate/late telemetry discarded; packet gaps, malformed packets and ring loss are different counters.
- Normalized frames: <=60 source seconds / 8192 samples. Optional MCP history: <=600 chart records and 20 recent frames.
- Frame coverage distinguishes missing stream completions, packet gaps and epoch producer drops. Missing baseline/full source second marks a partial window.
- At most 64 commands await outcomes; retain 128 records. Worker services outgoing commands after at most 32 received packets.
- Bridge data stays in memory; complete process logs remain on disk. UI/MCP reads do not change monitoring or save captures.

## Code and checks

- `src/runtime/mcp.cpp`: schemas/HTTP; `bridge.cpp`: worker/model/validation; `session.cpp`: game ownership; `src/ui/profiler.cpp`: presentation.
- `tests/bridge_check.cpp`: real UDP/HTTP with mock runtime, loss/reset/resources/freshness/work deltas. `tests/session_check.cpp`: managed MCP lifecycle/log tails.
- AnyPS5 `core/libs/tests/DebugBridge.cpp` and `libSceAgcDriver/tests/BridgeResources.cpp`: producer/monitoring/resource checks.
- [Profiler usage/scopes](profiling.md), [launch/storage](anyps5-integration.md), [planned capture/scripts](profiler-workspace.md).
