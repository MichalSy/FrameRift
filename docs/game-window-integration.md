<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Game-window integration

## Current behavior

- Game runs in a separate process and external SDL/Vulkan window.
- FrameRift manages launch, stop, logs and optional telemetry; it does not embed or render game pixels.
- Library/Profiler navigation leaves the game running. UDP telemetry does not require window embedding.
- Linux external launch is implemented; Windows and embedded modes remain unverified.

## Planned validation

- A cooperative native handle/host adapter is a candidate for Windows and Linux/X11.
- GTK4 cannot assume GTK3 GtkSocket/GtkPlug. A game-process SDL pointer is not a launcher handle.
- Native Wayland has no universal foreign-window reparenting path; test XWayland and native Wayland separately.
- Validate resize, DPI, clipping, focus, fullscreen, replacement windows, normal exit and crashes.
- Keep process/telemetry management independent; report unsupported embedding explicitly.
- Qt Quick foreign-window hosting is an evaluated alternative, not the selected stack.

## Where to find it

- Process ownership: `src/runtime/session.cpp`; launch: `src/runtime/launch.cpp`.
- AnyPS5 window/input: `core/libs/prx/libSceVideoOut/src/DisplayWindow.cpp`, `PadInput.cpp` and `VideoOutDriver.cpp`.
- [Toolkit/platform status](toolkit-and-platforms.md), [input](controller-input.md), [architecture](architecture.md).
