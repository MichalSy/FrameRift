<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Toolkit and target platforms

[Project overview](../README.md) · [Game integration](game-window-integration.md) · [Artwork](artwork.md)

## Status and fixed requirements

FrameRift is a native desktop frontend with a local game library and a separate Profiler console. The application uses **GTK4**, C++20, CMake and SQLite for the library and artwork cache. Automatic scanning, Linux launch/rebuild, Profiler output, keyring credentials and animated heroes are implemented. Embedded game windows and timing analysis remain planned.

This document retains the toolkit comparison as architectural background. **libadwaita is not a current dependency.** **Qt 6 Quick/QML** remains an evaluated alternative for foreign native window embedding, rather than the implemented UI stack. Windows packaging and embedded-window support are not established by the existing library.

Fixed requirements:

- Windows and Linux as target platforms.
- A native desktop application with smooth animation and low background load.
- A distinct visual identity with graphite surfaces, cyan accents and prominent artwork.
- Separate library and game views; the working mode for a running game is **Profiler**.
- Recently played games first in the library; cover and hero artwork primarily from SteamGridDB.
- Local UI rendering. The UI stack does not use a browser, WebView or web server.

## What native performance means here

A toolkit name does not prove performance. Proposed FrameRift criteria:

1. Scrolling and changing the selected game must not block on file access or network requests.
2. Decode covers for their visible size. Hidden cards and animations should not cause continuous work.
3. Decode and present animated heroes under explicit limits. Multiple large animations must not run automatically at once.
4. Deliver measurements to Profiler at bounded update rates. The UI need not redraw for every measurement event.
5. Keep game and UI processes independent. A UI stall must not stop game logic.
6. Compare CPU, GPU and memory usage with an idle library, animation and active measurement.

The actual display refresh rate determines the rendering budget: approximately 16.7 ms per frame at 60 Hz and 8.3 ms at 120 Hz. These are arithmetic examples for later measurements, not promised FrameRift results.

## Current toolkit: GTK4; optional libadwaita

GTK4 builds a GSK scene graph and provides OpenGL, Vulkan and Cairo renderers. Its frame clock coordinates input, animation, layout and drawing. These APIs support animated local UI elements; the active renderer and its costs still require measurement on target systems. [GTK drawing model](https://docs.gtk.org/gtk4/drawing-model.html)

GTK has a Win32 backend. Its official installation documentation includes MSYS2, providing a possible Windows build approach. This does not establish a finished FrameRift package or a working libadwaita runtime on every intended Windows system. [GTK on Windows](https://docs.gtk.org/gtk4/windows.html), [Windows installation](https://www.gtk.org/docs/installations/windows/)

Libadwaita offers dark-style management, additional widgets, CSS variables and custom accent colors. It remains optional. Custom rendering must respect high contrast and readable states. The proposed use is an application-owned graphite/cyan design with narrowly scoped CSS classes, without broad overrides of standard-widget internals. [Styles and appearance](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/styles-and-appearance.html)

### Technical approach

- C++20 for the library model, caches and process-management code; integration adapters are planned.
- GTK **C APIs** from C++, with small RAII helpers for lifetimes. Any future libadwaita use would follow the same approach.
- GLib/GObject/GIO for the event loop, signals, asynchronous I/O and process management.
- GTK resource bundles and application-owned styles for the UI.
- GTK list models and suitable list/grid views are the proposed approach for large libraries, rather than unlimited permanently active widgets. [GtkListView](https://docs.gtk.org/gtk4/class.ListView.html)

A C++ wrapper for libadwaita, such as an assumed `libadwaitamm` stack, is **not a confirmed dependency**. Any future wrapper choice must assess availability, maintenance, API coverage and Windows packaging separately. This plan does not require one.

Most GTK objects may only be used on the main thread. Long scans, downloads and decoding belong in asynchronous operations or workers; results must be transferred to the main thread explicitly. [GTK threading](https://docs.gtk.org/gtk4/section-threading.html)

### Open points

- A custom gaming launcher should not be defined solely by a standard GNOME application layout. Libadwaita is an optional building block, not a complete FrameRift layout.
- Windows packaging, including fonts, image decoders and runtime libraries, must work on a clean test system.
- GTK3's `GtkSocket`/`GtkPlug` supported cross-process embedding specifically on X11. GTK4 cannot assume these GTK3 APIs; it needs a modern platform adapter or another image transport. [GTK3 GtkSocket](https://docs.gtk.org/gtk3/class.Socket.html), [GTK4 API](https://docs.gtk.org/gtk4/)
- A rectangular GTK placeholder does not automatically create a native child window for SDL. Focus, clipping, scaling and lifetimes need validation for the selected integration path.

## Evaluated alternative: Qt 6 Quick/QML

Qt Quick provides a locally rendered scene-graph UI. Qt 6 uses native graphics backends through its rendering abstraction. QML describes UI structure, states and animation; C++ can provide data and integration models. **Qt Quick/QML is not a web UI.** The separate Chromium-based Qt WebEngine module is not planned for FrameRift. [Qt graphics](https://doc.qt.io/qt-6/topics-graphics.html), [Qt Quick scene graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html), [Qt WebEngine overview](https://doc.qt.io/qt-6/qtwebengine-overview.html)

The relevant feature is `QWindow::fromWinId()`, which can create a local representation of a foreign native window, including one from another process. Support depends on the platform plugin and may fail. Qt does not provide general state monitoring of that foreign window. [QWindow::fromWinId](https://doc.qt.io/qt-6/qwindow.html#fromWinId)

Qt Quick has provided `WindowContainer` since Qt 6.8. It manages the geometry of an embedded `QWindow`, but the child window sits above the Quick scene. Ordinary QML items cannot automatically overlay it. This favors Profiler panels beside and below the game area; overlays within that area require separate validation. [WindowContainer](https://doc.qt.io/qt-6/qml-qtquick-windowcontainer.html)

### Proposed technical approach if selected

- C++20 for the shared domain model and platform adapters.
- QML/Qt Quick for the library, mode changes and Profiler panels.
- Application-owned Qt Quick Controls components for graphite/cyan styling. [Customizing Qt Quick Controls](https://doc.qt.io/qt-6/qtquickcontrols-customize.html)
- Qt Core/Gui/Quick/Qml and, if needed, Network as runtime candidates.
- Use process, network and event-loop abstractions appropriate to Qt; do not add another GLib main loop solely by convention.

### Open points

- The convenient API still requires Windows/X11/Wayland validation.
- A foreign window is not a texture in the UI scene. Transformations, rounded corners and overlays do not automatically apply to game content.
- Animated WebP needs testing with the distributed image plugins and their animation support.
- Define required Qt modules, plugins, runtime distribution and their license terms before selecting this alternative. [Qt licensing overview](https://doc.qt.io/qt-6/licensing.html)

## Architectural comparison

| Criterion | GTK4, optionally libadwaita | Qt 6 Quick/QML |
|---|---|---|
| Project role | Implemented library toolkit; libadwaita optional | Evaluated alternative |
| Custom graphite/cyan design | Custom widgets/styles; selective libadwaita use if adopted | Custom Quick Controls and components |
| Local animation | Frame clock/GSK; measure actual renderer | Quick scene graph; measure actual backend |
| Windows | Win32 backend documented; packaging open | Validate target platform and plugin packaging |
| Linux | Test X11 and Wayland separately | Test XCB and Wayland separately |
| Foreign Windows/X11 window | Custom platform path with more validation work | `fromWinId` + `WindowContainer` as a concrete candidate |
| Foreign native Wayland window | No universal reparenting path | Qt also remains subject to Wayland limits |
| C++ API approach | GTK C APIs from C++; libadwaita and wrappers optional | Qt C++ API plus QML |
| Current adoption | GTK4 used for the library | Not adopted |

This is a technical assessment for FrameRift, not a popularity ranking, benchmark result or guarantee of embedding on all platforms.

## Dependencies and build approach

The build requires **C++20**, **CMake 3.20 or newer**, GTK4 4.12 or newer, GTK4 VTE 0.70 or newer, libcurl 7.85 or newer, JSON-GLib, SQLite3, GIO, libsecret, libwebp/libwebpdemux, pkg-config and `glib-compile-resources`. Game launch also requires Python 3. C++20 is requested through compile features with language extensions disabled. Ninja is the documented build generator. CMake installs the executable and, on Linux, a desktop menu entry and icon under the configured prefix. [CMake CXX_STANDARD](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html), [CMake Ninja generator](https://cmake.org/cmake/help/latest/generator/Ninja.html)

| Component | Status | Purpose | Remaining validation |
|---|---|---|---|
| C++20 compiler | Current | Library core and runtime | Windows/Linux toolchains and warning rules |
| CMake + Ninja | Current build approach | Build and reproducible configuration | Presets and packaging |
| GTK4 | Current | Native UI | Renderer, Windows package and window adapter |
| libadwaita | Optional | Widgets, styles and animation | Exact APIs, stable version and Windows distribution |
| GLib/GIO | Current | Event loop, files and processes | Required APIs and asynchronous lifetimes |
| GTK/libadwaita C++ wrappers | Not assumed | Optional API ergonomics | Maintenance, API coverage and additional costs |
| Qt 6 Quick/QML | Evaluated alternative | Custom UI and window containers | Qt 6.8+ for WindowContainer; platform tests |
| libwebp / animation decoder | Current | Animated heroes | Decoding, memory limits and packaging |
| libcurl + JSON-GLib | Current | SteamGridDB and metadata | Bounded asynchronous work |
| SQLite | Current | Library, artwork mapping and history | Migration and crash behavior |
| VTE | Current | Profiler output | Bounded scrollback and packaging |
| libsecret | Current | Artwork credentials in the desktop keyring | Keyring availability and reads outside the UI thread |
| SDL/Vulkan in the frontend | Not an assumed dependency | Only for validated integration needs | The frontend is not the game renderer |

A game-process dependency is not automatically a frontend dependency. The planned integration uses a small versioned protocol and platform-specific data; see [Game integration](game-window-integration.md).

## Proposed platform validation

Before committing to embedded game rendering, assess the current toolkit and any proposed alternative against the same small reference:

1. Library with realistic cover sizes, recently played games first and an animated hero.
2. Profiler layout with a large game area, analysis on the right and measurements below.
3. External cooperative SDL test process on Windows and Linux/X11.
4. Resize, window changes, DPI changes and focus transitions between game and Profiler controls.
5. Entering and leaving Profiler, normal game exit and unexpected process termination.
6. Explicit Wayland test with documented results and a working external game window.
7. Clean Windows package and a defined Linux packaging trial.

Use the results to decide embedding paths, any toolkit change, required versions and active renderers. Library animation is a separate test; a working library does not prove reliable game embedding.
