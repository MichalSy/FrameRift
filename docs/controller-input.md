<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Controllers and input

## Current boundary

- Game input belongs to AnyPS5's SDL/pad path; FrameRift controls launch, logs and telemetry.
- Launcher controller navigation, an input-mapping editor and scripted injection are not implemented.
- Current bridge advertises no input capability. `set_input` / `schedule_inputs` are capability-gated transport names, not available game actions.
- Xbox/PlayStation device behavior depends on SDL mapping, hardware and runtime support; general hardware compatibility is unverified.

## Runtime configuration and source

- AnyPS5 `anyps5-input.ini` / `ANYPS5_INPUT_CONFIG` configures keyboard/mouse bindings; it is not a controller-deadzone/device editor.
- SDL `SDL_GAMECONTROLLERCONFIG_FILE` / `SDL_GAMECONTROLLERCONFIG` maps physical devices to logical buttons before runtime input starts.
- AnyPS5 `core/libs/prx/libSceVideoOut/src/PadInput.cpp`, `InputMapping.cpp`, `libScePad/src/PadState.cpp` and `libScePad/Export.cpp` contain polling/mapping/guest state.
- Runtime user guide: AnyPS5 `docs/user/INPUT_MAPPING.md`.

## Previously inspected upstream baseline

- Findings refer to [upstream snapshot](https://github.com/boykopovar/AnyPS5/tree/197ffac97203d715a258a28b432fa2eedbf8be6f); recheck the selected fork before changing behavior.
- SDL A/B/X/Y map to Cross/Circle/Square/Triangle; shoulders/sticks/D-pad/Start map to corresponding logical controls.
- Non-PS View/Back substitutes a fixed central touch contact, not free touchpad motion. Guide/Home is not mapped in that block.
- One selected physical controller and guest pad 0; reported connection capacity is not proof of multiplayer/hardware connectivity.
- Positive analog trigger values become full-scale in the inspected host/guest combination; analog fidelity needs runtime verification.
- No configurable stick deadzone was established. Rumble, LED, trigger and sensor support depend on device capabilities.

## Planned work

- Define focus between Library, Profiler controls and external game; test hotplug and backend differences.
- Reuse existing input mappings; hardware features cannot be created through remapping.
- Script execution needs a defined input boundary, balanced transitions and reported applied frames. [Script plan](profiler-workspace.md).
- [Window plan](game-window-integration.md), [runtime integration](anyps5-integration.md).
