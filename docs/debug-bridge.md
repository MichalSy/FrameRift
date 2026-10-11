<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Debug bridge and MCP

FrameRift implements the receiving/command side of an optional runtime bridge. A loopback UDP worker receives events independently of GTK rendering. A local MCP server exposes those events and queues commands for the connected runtime. AnyPS5 has an optional connection-only sender; game measurements, input execution and screenshot capture remain separate work.

## Current capabilities

The running FrameRift app exposes nine MCP tools. Library listing, managed game start/stop, asynchronous process status and recent log lines work today using the same game lifecycle as the UI. These five tools do not require a connected game bridge. Process output continues into complete session log files.

The four bridge tools expose connection state, buffered events and asynchronous command receipt/completion. FrameRift's UDP receiving/command server is implemented. A bridge-enabled AnyPS5 build can connect and maintain liveness, with an empty capability list. It produces no measurements and cannot apply automated inputs or capture frames. A connection alone does not prove rendering progress. The current server neither stores input scripts nor offers script-list/run tools.

The proposed live profiler, durable game scripts, snapshots and future script MCP tools are specified separately in [Profiler workspace and game input scripts](profiler-workspace.md). That document and its ImageGen references describe planned functionality.

## Server lifetime and configuration

Both servers start with the primary FrameRift application and remain active across tab changes. The header indicates whether a runtime actually completed the UDP handshake. The Profiler shows connection state, addresses, latest reported frame, event count and detected packet gaps. A running process without a handshake stays disconnected.

| Environment variable | Default | Meaning |
| --- | --- | --- |
| `FRAMERIFT_UDP_PORT` | `0` | Bind a free UDP port; set a fixed port for a separately launched runtime |
| `FRAMERIFT_MCP_PORT` | `47653` | MCP HTTP port; `0` selects a free port |

Both bind only to `127.0.0.1`. Invalid ports or occupied addresses appear as unavailable; normal library use and game launch still work. The UI shows the bound address when port `0` is used. Windows transport support uses GIO/libsoup APIs but has not been tested on Windows.

Before a managed game starts, FrameRift expects its stable game ID and the session ID used by its log directory. It sets these variables **only on that game's subprocess**:

```text
FRAMERIFT_BRIDGE_HOST=127.0.0.1
FRAMERIFT_BRIDGE_PORT=<bound UDP port>
FRAMERIFT_BRIDGE_SESSION=<session ID>
FRAMERIFT_GAME_ID=<stable game ID>
```

Conversion receives none of these bridge variables. Spawn failure or process exit detaches the session and rejects late handshakes for that session. Before any managed launch, a separately launched local runtime can connect using its own nonempty session/game IDs. Only one peer is connected at a time.

### Connection-only AnyPS5 build

Configure AnyPS5 with `-DANYPS5_ENABLE_DEBUG_BRIDGE=ON` and rebuild its patched runtime libraries. The default is `OFF`: ordinary builds contain neither the worker nor the startup call. The included bridge starts when the video output driver initializes, using the managed launch variables above. All four variables must be valid; only `127.0.0.1` is accepted. Set `APS5_DEBUG_BRIDGE=0` to disable the worker for a run.

One independent thread uses a persistent nonblocking UDP socket. It sends `hello`, one-second heartbeats and periodic repeated `hello` packets without waiting for a reply. Normal runtime shutdown sends a best-effort `goodbye`; forced termination uses the existing disconnect/timeout behavior. A missing receiver does not stop the game or alter its default logs.

This first adapter advertises `capabilities: []` and omits `frame`. `get_bridge_status` can show connected while `get_events` is empty and `received_events` remains zero. Monitoring, inputs and capture commands are rejected as unavailable. Structured telemetry and command execution are later integration steps.

## MCP client

Use **Streamable HTTP** at `http://127.0.0.1:47653/mcp` while FrameRift is running. Initialization supports MCP `2025-11-25` and `2025-03-26`; other requested revisions negotiate `2025-11-25`. This endpoint is stateless, returns JSON responses and does not offer an SSE stream. GET returns 405 and notifications return an empty 202. Clients poll `get_events` as needed; runtime-to-FrameRift delivery is always pushed over UDP.

The transport follows the [MCP Streamable HTTP specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

POST requests use `Content-Type: application/json` and `Accept: application/json, text/event-stream`. After initialization, clients supply their negotiated `MCP-Protocol-Version`. Nonlocal Host/Origin values are rejected. Requests are capped at 64 KiB, with at most 32 concurrent requests and a 10-second request deadline.

| Tool | Arguments | Result |
| --- | --- | --- |
| `list_games` | `{}` | Visible library records with stable IDs, metadata titles, availability and current session |
| `start_game` | `{ "game_id": "PPSA01234" }` | Starts preparation in the background and returns current session status |
| `stop_game` | `{ "game_id": "PPSA01234" }` | Requests termination of that managed game, or cancels its pending preparation |
| `get_game_status` | `{}` | Current/last managed session state, IDs, log paths, exit status and error |
| `get_game_log` | `{ "lines": 5 }` | Last requested log lines; defaults to 5, maximum 200 |
| `get_bridge_status` | `{}` | Connection, session/game IDs, UDP port, frame, capabilities and counters |
| `get_events` | `{ "after": 0, "limit": 100 }` | Events after a local cursor; limit 1–100, immediately returns available data |
| `send_command` | `{ "name": "set_monitoring", "args": { "enabled": true } }` | Command ID and initial `pending` status |
| `get_command` | `{ "command_id": "<returned ID>" }` | Latest status, attempts, optional result/error |

`send_command` supports names `set_monitoring`, `set_input`, `schedule_inputs`, `cancel_schedule` and `capture_frame` **only when the runtime advertises that capability**. The server transports `args` as an object; command-specific validation and execution belong to the runtime. Disconnected games, unknown names and unavailable capabilities return MCP tool errors.

The five game tools use the same runtime session as the Play/Rebuild UI and work without a UDP connection. `list_games` follows configured library folders; previews and unavailable installations cannot be launched. `start_game` accepts only a listed game ID and uses existing Settings. It never accepts an arbitrary executable, directory or command. Only one game/preparation can be active.

Start and stop return immediately. Poll `get_game_status` for `preparing`, `running`, `stopping` and terminal states `exited`, `stopped`, `failed` or `rebuilt`. Preparation and process failures remain visible in `error`; receipt of a start request is not proof the game started. Stop signals the existing AnyPS5 memory guard so it can terminate the managed scope and drain logs. A canceled preparation never subsequently starts the game. UI navigation does not alter the session.

`get_game_log` reads the current or last managed session's game output, falling back to its conversion log when no game output is available. Omit `lines` for the last five lines, or use an integer from 1 to 200. The result identifies `game_id`, `source` and `path`, includes a `lines` array, and reports `available`, `error` and `truncated`. Each request reads at most the last 256 KiB, invalid UTF-8 is replaced, and full files remain on disk. No arbitrary file path is accepted. Immediately after app startup, before any managed session, there is no session log to read; this tool does not search earlier app sessions.

### Codex setup

Open FrameRift, then register its local HTTP endpoint:

```sh
codex mcp add framerift --url http://127.0.0.1:47653/mcp
codex mcp get framerift
```

This stores the connection in the user Codex configuration. Reload MCP servers or begin a new Codex session to discover the tools. Keep FrameRift open for requests; no authentication token is needed for this loopback-only endpoint. A custom `FRAMERIFT_MCP_PORT` also needs a matching Codex URL. See the [official Codex MCP configuration](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).

Example initialization:

```sh
curl http://127.0.0.1:47653/mcp \
  -H 'Content-Type: application/json' \
  -H 'Accept: application/json, text/event-stream' \
  --data '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"bridge-client","version":"1"}}}'
```

Then send the `notifications/initialized` notification and use `tools/list` or `tools/call`:

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_bridge_status","arguments":{}}}
```

## UDP protocol v1

One UTF-8 JSON object per datagram, at most **16 KiB**, without an extra framing layer. Every packet contains:

```json
{"protocol":"framerift.bridge","version":1,"type":"heartbeat","session_id":"session-123","sequence":2,"frame":500}
```

`sequence` is a nonnegative integer increasing independently in each direction. Use values up to `9223372036854775807`; `frame`, when supplied, has the same integer range. Identifiers must be nonempty strings of at most 128 bytes. Frames are runtime-provided correlation values, not frames counted by FrameRift.

The runtime sends `hello` first from the same UDP socket that will receive commands:

```json
{"protocol":"framerift.bridge","version":1,"type":"hello","session_id":"session-123","sequence":1,"game_id":"PPSA01234","capabilities":["set_monitoring","schedule_inputs"],"frame":500}
```

FrameRift pins that session and source address/port. Wrong peers or managed-session identities cannot replace an active connection. Send valid heartbeats or events at least once per second while idle; five seconds without a valid packet marks the connection lost. A repeated matching `hello` can reconnect after a timeout. Repeating hello periodically also tolerates a lost initial handshake; hello and telemetry are not acknowledged.

An event carries a name and an object payload:

```json
{"protocol":"framerift.bridge","version":1,"type":"event","session_id":"session-123","sequence":3,"frame":500,"name":"frame_timing","data":{"flip_interval_ms":16.8}}
```

Metric names/units are adapter-defined. No log parsing or synthetic measurements occur here. `goodbye` explicitly disconnects; it uses the same envelope as a heartbeat with `type` changed.

FrameRift sends commands asynchronously from the UDP worker:

```json
{"protocol":"framerift.bridge","version":1,"type":"command","session_id":"session-123","sequence":1,"command_id":"<UUID>","name":"set_monitoring","args":{"enabled":true}}
```

The runtime acknowledges **receipt** separately from completion:

```json
{"protocol":"framerift.bridge","version":1,"type":"command_received","session_id":"session-123","sequence":4,"command_id":"<UUID>"}
{"protocol":"framerift.bridge","version":1,"type":"command_completed","session_id":"session-123","sequence":5,"command_id":"<UUID>","result":{"enabled":true}}
```

Failure uses `type: "command_failed"` and an optional `error` string of at most 1024 bytes. `result`, when present, is an object. Receipt means the runtime accepted responsibility for the command; it does not mean input was applied or a screenshot was captured.

Commands are sent at most three times, roughly 500 ms apart, with the same `command_id` and packet. Receipt stops retries. The runtime must deduplicate by session/command ID and resend acknowledgements for duplicates. FrameRift accepts reordered command acknowledgements and cannot guarantee completion delivery over UDP. A command without receipt becomes `timed_out`; received commands remain `received` until completion, failure or disconnect. Terminal statuses also include `completed`, `failed` and `disconnected`.

## Buffers and automation

The worker processes at most 32 received datagrams before servicing outgoing commands. Telemetry receives no acknowledgement and never waits for MCP clients. The event ring keeps the newest 512 events, including accepted command acknowledgements. Each gets a local increasing `event_id`; pass returned `next_event_id` as the next `after` cursor. `oldest_event_id` and `lost_history` expose overwritten history. Duplicate/late telemetry is discarded, forward packet gaps increment `missing_events`, and invalid/oversized datagrams increment `malformed_packets`. These counters are separate from ring-history loss.

At most 64 commands can await receipt/completion, with 128 command records retained. Older terminal records are removed when capacity is needed. Bridge events remain in memory; complete process logs continue through the existing session-log path.

For input automation, the intended runtime contract is to upload a whole sequence before it starts: an optional absolute start frame such as `500`, followed by offsets such as `+20` relative to the previous **planned** action, with explicit button down/up. Omitting a start uses the next available input boundary after acceptance, as specified in the [script design](profiler-workspace.md). FrameRift can forward a sequence in `schedule_inputs.args` when a runtime advertises support; it does not interpret those offsets or execute inputs. The connection-only adapter advertises no such support. Screenshot commands likewise require runtime support; image transfer and a dashboard are not implemented in this slice.

The native `debug_bridge` check exchanges actual UDP packets and HTTP MCP requests with a mock runtime. `game_mcp` checks library queries, managed start/stop, preparation errors and log tails using temporary game/runtime fixtures. AnyPS5's connection checks exercise actual loopback datagrams, liveness, shutdown and disabled/invalid configuration. Real gameplay timing, overhead and end-to-end input/screenshot behavior remain to be measured after those capabilities exist.
