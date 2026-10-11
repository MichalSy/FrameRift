<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Libraries and build

## Status

FrameRift implements the game library with C++20, CMake, GTK4, SQLite, libcurl and JSON-GLib. GTK4 VTE provides the Profiler console, libsecret stores artwork credentials, libwebp plays animated heroes and GIO subprocesses handle Linux launch. Build and run commands are in the [README](../README.md). Linux is the development platform; Windows builds and distribution remain unverified.

## Implemented components

The table lists the implemented stack.

| Area | Component | Purpose |
| --- | --- | --- |
| Language | C++20 | Library domain and native UI |
| Build | CMake 3.20+, Ninja, pkg-config | Build the program, cache check and resources |
| UI | GTK4 4.12+ | Native widgets, rendering and input through the C API |
| Console | GTK4 VTE 0.70+ | Output-only terminal display, ANSI handling and bounded scrollback |
| GTK stack | GLib/GObject/GIO | Application, signals, resources, cache locations and subprocesses |
| Local cache | SQLite3 | Game metadata, SteamGridDB mappings and local artwork paths |
| HTTP | libcurl 7.85+ | SteamGridDB API and image downloads outside UI callbacks |
| MCP HTTP server | libsoup 3.2+ | Serve the local Streamable HTTP endpoint on the GLib main context |
| Debug transport | GIO UDP socket and a C++ worker | Receive events and send commands independently of GTK rendering |
| JSON | JSON-GLib | Read artwork responses and the versioned bridge/MCP messages |
| Credentials | libsecret | Store the artwork API key in the desktop keyring |
| Hero animation | libwebp and libwebpdemux | Decode and composite timed WebP frames for the selected GTK hero |
| Resources | `glib-compile-resources` | Embed CSS and app marks as GResources |
| Checks | CTest and small C++ programs | Check cache, launch and actual UDP/HTTP exchanges without a test framework |

FrameRift has a native UI process and no browser runtime. Dependencies are added only for a concrete need. [Toolkit and platforms](toolkit-and-platforms.md) covers the toolkit decision for later platform integration.

## Other proposed components

| Area | Proposal | Purpose and scope |
| --- | --- | --- |
| Application components | libadwaita | Adaptive components and application structure; select components that suit the design |
| UI alternative | Qt 6 + Qt Quick/QML | Native, customizable UI and platform window integration; alternative at the toolkit decision point |
| App logs | spdlog, optional | Structured FrameRift diagnostics; keep game output separately identifiable |
| Capture compression | zstd, optional | Compress larger measurement recordings in a worker |
| Development profiling | Tracy, optional | Investigate FrameRift overhead and selected runtime instrumentation |

GTK4/libadwaita and Qt are alternative frontend paths. Including both in the first version without a concrete benefit would add unnecessary work. Likewise, select one HTTP client rather than using libcurl and another client for the same task.

## GTK4 and libadwaita in C++

GTK4 and libadwaita provide C APIs. The application uses GTK4 directly from C++. GObject reference ownership and callback lifetimes stay explicit; C++ exceptions must not escape C callbacks. gtkmm and libadwaita are not included and will be considered only when they offer a concrete benefit.

The custom design should not depend on a large set of undocumented theme overrides. Reusable cards, hero content, toolbars, timelines and frame details need a deliberately designed component base. libadwaita should be selected for specific benefits rather than defining every UI detail automatically.

Windows support concerns both the backend and distribution. DLLs, image loaders, resources, fonts and icons must work in a clean package. A working UI on the development machine does not validate an end-user installation.

## Data processing

JSON-GLib reads SteamGridDB, UDP bridge and MCP messages. The bridge uses bounded JSON datagrams; performance under real runtime event rates remains to be measured. A compact format may suit larger event volumes, while preserving measurement semantics.

SQLite stores the catalog and indexes. The primary measurement store will be designed separately. zstd is a possible capture-compression component; compression and file writes must stay outside the game thread and UI drawing routine.

libcurl handles HTTPS requests and image downloads. Metadata and images are then stored in the local cache. Settings saves the API key through libsecret in the desktop keyring; `STEAMGRIDDB_API_KEY` takes precedence. Caches and diagnostics exclude the key. Keyring access has a timeout and runs outside the GTK thread, as do network requests. Manual matching, refresh behavior and rate limits belong in small, focused extensions.

libsoup provides the embedded localhost HTTP server for MCP; it does not replace the artwork HTTP client. [Debug bridge and MCP](debug-bridge.md) defines the implemented transport and limits.

## SDL2, Vulkan and image formats

The examined AnyPS5 runtime uses SDL2 and Vulkan. These primarily belong to the game process. FrameRift needs SDL2 itself only if a concrete adapter or controller-navigation feature depends on it. A profiler chart does not need to use the game's graphics API.

GTK loads static PNG and JPEG images. The hero module in `src/ui` uses libwebpdemux for animated WebP: the decoder composites frames with transparency and disposal rules, while a GTK tick controls frame durations and loops. Compressed files are limited to 64 MiB, and WebP canvases to 16 million pixels before decoder allocation. Hidden heroes and disabled GTK animations pause playback. Windows playback remains unverified.

An integrated game view is a separate platform concern. A Vulkan header or SDL link target does not automatically allow two processes to share a game surface. [Game-window integration](game-window-integration.md) describes the requirements.

## Tracy and the FrameRift profiler

Tracy is a possible development tool with CPU and Vulkan GPU profiling support. It could help investigate selected instrumented builds and launcher overhead. It is neither an automatic source for every measurement shown in the designs nor an existing FrameRift backend contract.

The FrameRift UI needs understandable, correctly correlated data even without a comprehensive Tracy integration. Importing or exporting external profiler data may be useful once the session format and measurement semantics are stable.

## Versions and distribution

CMake checks the minimum versions listed above. CMake installs the executable and, on Linux, a desktop menu entry and icon under the configured prefix. Configure `CMAKE_INSTALL_PREFIX` before building so the desktop entry points to the installed binary. CI and clean end-user packages for Windows and Linux remain planned. Linux/X11 and Linux/Wayland need separate integration checks: successful package installation does not imply identical game-window integration capabilities.

A packaging experiment should select tools for the GTK or Qt path. MSYS2, gvsbuild, package managers and other dependency strategies need evaluation; no combination with a particular Windows compiler has been chosen. An end-user package should include its required runtime libraries with clear provenance.

A full license inventory will accompany distribution, and toolkit changes must account for their distribution terms. Original FrameRift code and documentation use [AGPL-3.0-or-later](../LICENSE). The root [NOTICE](../NOTICE) requires preservation of FrameRift attribution and its origin link under section 7(b). Dependencies retain their own licenses: AnyPS5 remains GPL-2.0-only and runs separately; VTE is LGPL-3.0-or-later, which prevents licensing the VTE-linked FrameRift build as GPL-2.0-only. Third-party artwork and trademarks retain their respective rights.

See [VTE license metadata](https://gnome.pages.gitlab.gnome.org/vte/gtk4/index.html) and [GNU license compatibility guidance](https://www.gnu.org/licenses/license-list.html#LGPLv3).

## Primary sources

- [GTK4 documentation](https://docs.gtk.org/gtk4/) and [GTK on Windows](https://www.gtk.org/docs/installations/windows)
- [libadwaita documentation](https://gnome.pages.gitlab.gnome.org/libadwaita/doc/main/)
- [GIO documentation](https://docs.gtk.org/gio/)
- [Qt Quick](https://doc.qt.io/qt-6/qtquick-index.html)
- [CMake](https://cmake.org/cmake/help/latest/guide/tutorial/index.html) and [Ninja](https://ninja-build.org/)
- [libcurl](https://curl.se/libcurl/)
- [JSON-GLib](https://gnome.pages.gitlab.gnome.org/json-glib/)
- [SQLite as an application file format](https://www.sqlite.org/appfileformat.html)
- [spdlog](https://github.com/gabime/spdlog)
- [zstd](https://github.com/facebook/zstd)
- [Tracy](https://github.com/wolfpld/tracy)
- [libwebp](https://github.com/webmproject/libwebp)
