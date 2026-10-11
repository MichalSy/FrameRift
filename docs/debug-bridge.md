<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Debug bridge and MCP

FrameRift implements the receiving/command side of an optional runtime bridge. A loopback UDP worker receives events independently of GTK rendering. A local MCP server exposes those events and queues commands for the connected runtime. The AnyPS5 sender, game instrumentation, input execution and screenshot capture are separate work; this change does not add them to AnyPS5.

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

## MCP client

Use **Streamable HTTP** at `http://127.0.0.1:47653/mcp` while FrameRift is running. Initialization supports MCP `2025-11-25` and `2025-03-26`; other requested revisions negotiate `2025-11-25`. This endpoint is stateless, returns JSON responses and does not offer an SSE stream. GET returns 405 and notifications return an empty 202. Clients poll `get_events` as needed; runtime-to-FrameRift delivery is always pushed over UDP.

The transport follows the [MCP Streamable HTTP specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

POST requests use `Content-Type: application/json` and `Accept: application/json, text/event-stream`. After initialization, clients supply their negotiated `MCP-Protocol-Version`. Nonlocal Host/Origin values are rejected. Requests are capped at 64 KiB, with at most 32 concurrent requests and a 10-second request deadline.

| Tool | Arguments | Result |
| --- | --- | --- |
| `get_bridge_status` | `{}` | Connection, session/game IDs, UDP port, frame, capabilities and counters |
| `get_events` | `{ "after": 0, "limit": 100 }` | Events after a local cursor; limit 1–100, immediately returns available data |
| `send_command` | `{ "name": "set_monitoring", "args": { "enabled": true } }` | Command ID and initial `pending` status |
| `get_command` | `{ "command_id": "<returned ID>" }` | Latest status, attempts, optional result/error |

`send_command` supports names `set_monitoring`, `set_input`, `schedule_inputs`, `cancel_schedule` and `capture_frame` **only when the runtime advertises that capability**. The server transports `args` as an object; command-specific validation and execution belong to the runtime. Disconnected games, unknown names and unavailable capabilities return MCP tool errors.

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

For input automation, the intended runtime contract is to upload a whole sequence before it starts: an absolute start frame such as `500`, followed by offsets such as `+20` relative to the previous **planned** action, with explicit button down/up. FrameRift currently forwards the sequence in `schedule_inputs.args`; it does not interpret those offsets or execute inputs. The exact action schema must be agreed with the AnyPS5 adapter. Screenshot commands likewise require runtime support; image transfer and a dashboard are not implemented in this slice.

The native `debug_bridge` check exchanges actual UDP packets and HTTP MCP requests with a mock runtime. Real gameplay timing, overhead and end-to-end input/screenshot behavior remain to be measured after the adapter exists.
