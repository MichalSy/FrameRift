<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# AnyPS5 integration

## Required runtime

- Use the [modified AnyPS5 fork](https://github.com/MichalSy/AnyPS5); stock upstream is not a supported substitute.
- Settings stores the checkout path. Each launch/rebuild selects its newest complete local CMake build with current patched libraries.
- Build `relinker` and `libs`; runtime libraries are used directly from `<build>/core/libs/libs`, without copying.
- The build must include executable relinker/patcher and all manifest-listed patched PRX libraries. Incomplete/stale builds are skipped.
- The converted game loads AnyPS5 libraries in its own process and opens its own window. Windows launch and embedding remain unverified.

## Play and Rebuild

- Scanner reads `sce_sys/param.json` titles/title IDs and original installation paths. Add game folders in Settings; do not seed installations manually.
- Play reuses `game.elf`; missing code triggers relinking of original `eboot.bin` with `--to-intel` and upstream `--module-dir` arguments for additional PRX directories.
- Module discovery stays within the selected installation and skips nested directories containing their own `eboot.bin`, such as complete decrypted copies.
- Plaintext SELF inputs are unpacked into temporary ELF files for upstream relinking without a version/signing-mode allowlist or a fixed 2 GiB limit. Only required program data is checked for encryption, compression, bounds and conflicting bytes; unused signature entries are ignored. Temporary inputs are removed after conversion; original files are never rewritten.
- FrameRift uses `tools/run_game.py` with separate original-game, generated executable, working-directory and shared-runtime paths.
- `tools/run_with_memory_guard.py`, Python 3, Linux cgroup v2 and a systemd user manager are required. Unguarded launch is disabled.
- Rebuild recreates generated code only; it does not launch the game. Original files, saves, shader caches and shared libraries remain intact.
- Start/stop/preparation are asynchronous and shared by UI/MCP. Only one active game/preparation; errors remain visible in status/logs.
- Update runtime libraries while no game is running. FrameRift does not build AnyPS5 automatically.
- This pipeline does not establish general PS5 package extraction or compatibility with arbitrary game inputs.

## Storage

| Data | Path |
| --- | --- |
| Library/settings/artwork metadata | `<cache-root>/games.sqlite3` |
| Generated game code | `<cache-root>/games/<game-id>/` |
| Persistent working directory/saves | `<data-root>/saves/<game-id>/` |
| Session output/conversion/guard logs | `<data-root>/logs/<game-id>/<session-id>/` |

- Cache root: `$XDG_CACHE_HOME/framerift`, normally `~/.cache/framerift`.
- Data root: `$XDG_DATA_HOME/framerift`, normally `~/.local/share/framerift`.
- Stable metadata IDs are path keys; titles and installation-folder names are display/source data.
- `_sd/`, `_sd_mem/` and other relative runtime data belong in the persistent working directory. Some executable-relative runtime assets/caches remain beside generated code.
- Legacy data migration preserves conflicting paths and reports an error. A configurable data root and multiple-installation identity model remain planned.

## Live bridge

- Configure AnyPS5 with `-DANYPS5_ENABLE_DEBUG_BRIDGE=ON`; rebuild patched `libs`.
- Managed launch supplies loopback address, game ID and session ID only to the game, not the converter.
- Adapter starts with VideoOut, advertises monitoring/resource/work activity and maintains heartbeats while monitoring is off.
- FrameRift requests monitoring after the handshake; no timing-file setting is needed.
- [MCP/UDP configuration](debug-bridge.md), [Profiler semantics](profiling.md).

## Where to inspect

- FrameRift: `src/runtime/launch.cpp` (build/preparation), `session.cpp` (lifecycle), `log.cpp` (output), `bridge.cpp` (telemetry).
- AnyPS5: `tools/run_game.py`, `run_with_memory_guard.py`, `stage_guest_runtime.py` (build manifests), `docs/user/USAGE.md` (inputs/layout).
- Runtime bridge: `core/libs/prx/libkernel/System/src/DebugBridge.cpp`; flip hook: `libSceVideoOut/src/VideoOutDriver.cpp`.
- File ownership: AnyPS5 `docs/dev/GuestFileDescriptors.md`; input: [controller notes](controller-input.md).
