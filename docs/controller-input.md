<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Controllers and input

FrameRift is intended to support mouse, keyboard and controller navigation and connect running games to their existing AnyPS5 input path. Launcher navigation, game input and controller configuration are separate tasks. These FrameRift input features are planned and not yet implemented.

The AnyPS5 findings below refer to [upstream `197ffac97203d715a258a28b432fa2eedbf8be6f`](https://github.com/boykopovar/AnyPS5/tree/197ffac97203d715a258a28b432fa2eedbf8be6f). They were checked against source code and do not guarantee hardware or game compatibility. FrameRift's launcher requires the [modified AnyPS5 fork](https://github.com/MichalSy/AnyPS5); these references establish the input baseline. See [AnyPS5 integration](anyps5-integration.md), [Profiling](profiling.md) and [Project overview](../README.md).

## Confirmed existing controller support

AnyPS5 already has an SDL GameController path. The runtime opens the first suitable SDL device, reads standardized buttons and axes, and exposes a pad state to guest functions. Xbox controllers therefore do not inherently need a new controller implementation in FrameRift. Detection of a specific model over USB or Bluetooth depends on the SDL backend and its mapping. Source: [Device opening and polling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L25).

The inspected build enables `SDL_JOYSTICK` and `SDL_HIDAPI`, while disabling `SDL_HAPTIC` and the standalone `SDL_SENSOR` subsystem. Disabling the sensor subsystem alone does not prove that GameController sensors are unavailable: the pinned PS5 HIDAPI joystick driver registers accelerometer and gyroscope sensors itself. Sources: [AnyPS5 CMake](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/CMakeLists.txt#L68), [SDL PS5 sensor registration](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/hidapi/SDL_hidapi_ps5.c#L802).

### Standard Xbox/PlayStation mapping

| SDL / common Xbox input | Existing AnyPS5 mapping |
|---|---|
| A / B / X / Y | Cross / Circle / Square / Triangle |
| LB / RB | L1 / R1 |
| Left / right stick click | L3 / R3 |
| D-pad | Corresponding direction |
| Start / Menu | Options |
| Back / View on a non-PS4/non-PS5 device | Touchpad click and a simulated central touch contact |
| Left / right stick | Left / right stick axes |
| LT / RT | Analog trigger path and L2/R2 button bit, subject to the limitation below |

The mapping follows logical inputs reported by SDL, rather than unchecked physical button numbers. The View substitute is not a freely movable touchpad: it sets a contact at `(960, 471)`. Guide/Home is not mapped in this block. Source: [Buttons, axes and touch substitute](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L120).

### Feedback and additional features

The code sends ordinary dual-motor rumble and LED output when the device reports LED capability. PS5 devices have an adaptive-trigger effect path; other devices may receive trigger rumble if SDL reports that capability. Return errors from these output calls are not checked there. FrameRift cannot infer that every Xbox generation supports trigger rumble over every connection type. Sources: [Output path](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L72), [SDL Xbox One driver](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/hidapi/SDL_hidapi_xboxone.c#L531).

Real motion data requires suitable sensors and their activation. The existing touch path reads at most two fingers from touchpad 0. A different mapping cannot give an ordinary Xbox controller physical touch or motion sensors it does not have. Source: [Sensor and touch polling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L171).

## Confirmed limitations of the inspected path

### Analog triggers

The SDL input path scales trigger values to `0..255` and also sets the L2/R2 button bit for any positive result. `Pad::ReadState()` then takes the maximum of the analog value and `255` when the corresponding bit is set. Together, these operations effectively raise positive controller trigger values to `255` when the guest reads them. Sources: [Host polling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L147), [Guest output](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/src/PadState.cpp#L100).

This is a concrete source finding in the inspected version. FrameRift should not advertise analog triggers as fully validated. A fix must distinguish analog controller input from digital keyboard/mouse input and belongs in a separate AnyPS5 core change with tests. A settings dialog alone cannot fix this data loss.

### Deadzones and device selection

Controller sticks in this path are scaled directly to `0..255`. No configurable deadzone is apparent there; guest device information reports deadzone values of `2`. Actual device filtering, calibration and stick curves are separate matters. Sources: [Stick scaling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L156), [Guest device information](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/Export.cpp#L66).

`PadInput` holds one controller pointer and selects the first suitable device. Hotplug and replacement-device discovery after disconnection exist. The guest port accepts only index `0`; a reported maximum of four connections does not mean four-player support is implemented. `ReadState()` also returns static guest connection status, `connected=true`, `connected_count=1`, which does not reliably establish physical device connectivity. Sources: [Controller pointer](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/include/PadInput.hpp#L35), [Hotplug](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L197), [Port validation](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/Export.cpp#L20), [Guest connection status](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libScePad/src/PadState.cpp#L139).

## Existing configuration paths

### Keyboard and mouse

`anyps5-input.ini`, or the path specified by `ANYPS5_INPUT_CONFIG`, configures keyboard/mouse bindings. The parser handles `KEY`, `MOUSE` and `WHEEL`. This format is not an existing editor for logical Xbox buttons, controller deadzones or multiple-pad selection. Sources: [InputMapping parser](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/InputMapping.cpp#L95), [AnyPS5 input documentation](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/docs/user/INPUT_MAPPING.md).

### Physical SDL mapping

The inspected AnyPS5 SDL submodule is pinned to `4b69833bc54abf3dd3288d4aa7afbba527775e5b`. Alongside built-in mappings, its initialization loads `SDL_GAMECONTROLLERCONFIG_FILE` and then `SDL_GAMECONTROLLERCONFIG`. The file maps physical SDL devices to logical SDL GameController inputs. Source: [SDL mapping initialization](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/src/joystick/SDL_gamecontroller.c#L1892).

FrameRift could supply an existing or natively created SDL mapping through the process environment. The variable must be set before game launch: AnyPS5 initializes `SDL_INIT_GAMECONTROLLER` while constructing `VideoOutDriver`. This does not inherently require a new FrameRift telemetry interface. Sources: [SDL hint](https://github.com/libsdl-org/SDL/blob/4b69833bc54abf3dd3288d4aa7afbba527775e5b/include/SDL_hints.h#L502), [AnyPS5 initialization](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/VideoOutDriver.cpp#L252).

Physical SDL mapping and the subsequent logical PlayStation mapping are different layers. A future FrameRift configuration UI should explain them. Arbitrary button indices from other APIs are not automatically valid SDL mapping records.

## Planned FrameRift input contexts

| Context | Planned behavior |
|---|---|
| Library | Controller supports selection, navigation and game launch. |
| Active game | Game receives intended inputs through its existing runtime path. |
| Profiler view | Navigation and view actions control FrameRift without unintended game actions. |
| Text entry / modal dialog | Explicit focus and a clear return path to the game. |
| Device loss | Visible diagnostics, neutral state and understandable reconnection. |

Existing Xbox support should be preserved. FrameRift does not need a second guest-pad emulator to use an already recognized device. Native launcher navigation can be developed separately. Persistent duplicate handling of the same input in the launcher and game is undesirable.

### Focus alone does not control controller input

AnyPS5 explicitly sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` to `1`. Window focus therefore cannot reliably establish that a controller now controls only the library. The runtime polls controller state, and a game in the background can continue to receive those inputs. Source: [Background input](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L25).

For keyboard and mouse, embedding requires checking which native window receives OS events. AnyPS5 already handles focus loss, clears held keyboard/mouse bindings and exits relative mouse mode when necessary. Correct automatic forwarding from FrameRift to a foreign game window is still not implemented. Source: [Focus-loss handling](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/prx/libSceVideoOut/src/PadInput.cpp#L216).

A future runtime contract could explicitly enable or disable the game's input context. That would be a new optional integration. Define which inputs pause, how held buttons become neutral and how the game receives current state after returning before implementing it. Virtual controllers and synthetic OS events are not assumed MVP requirements.

## Planned MVP and possible core work

FrameRift could first manage launch profiles, keyboard/mouse configuration paths and existing SDL mapping files. The library should make detected capabilities understandable and offer an external game-window mode while embedding is investigated.

These extended goals need further validation or work beyond a launcher dialog:

- Reliable analog guest triggers in the current controller path.
- Explicit device selection and assignment of multiple controllers to guest ports.
- Defined stick deadzones and calibration at the appropriate input layer.
- Accurate physical connection information, separate from guest-pad status.
- Explicit controller input permission when switching between game and launcher.
- Feedback for unsupported or failed rumble, LED and trigger effects.

AnyPS5 changes should remain small, separate and independent of the FrameRift GUI. This document describes requirements and limitations; it does not contain implemented core patches.

## Open investigations and tests

1. **Xbox hardware:** Test at least USB and Bluetooth with specific models on Windows and Linux; document button mapping, axes and ordinary vibration.
2. **Trigger values:** Trace released, partially pressed and fully pressed triggers through to the guest function. A meaningful regression test must distinguish intermediate values from digital substitute bindings.
3. **Configuration:** Check mapping before SDL initialization, missing files, malformed records and restart after profile changes.
4. **Focus:** Test library, game area, Profiler, modal dialogs, Alt-Tab and relative mouse mode together. A controller must not silently control both contexts.
5. **Lifecycle:** Test hotplug, device loss, replacement devices, game cancellation and restart without stuck buttons.
6. **Embedding:** Test native resize/focus/fullscreen transitions with the actual SDL/Vulkan window on Windows and Linux/X11; assess Wayland separately.

These checks are planned. Existing guest-pad tests cover individual state and touch functions, but do not establish the complete path from a physical Xbox device through SDL to the game. Source: [AnyPS5 GuestPad test](https://github.com/boykopovar/AnyPS5/blob/197ffac97203d715a258a28b432fa2eedbf8be6f/core/libs/tests/GuestPad.cpp).
