<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Roadmap

## Purpose

This plan describes the intended product and open technical questions. Phases are not delivery commitments. Game window integration and the measurement interface need validation with real programs.

## Implemented scope: Library and external Linux launch

- The native GTK4 library reads SQLite records and provides hero selection, search, sorting and recently played titles before the collection.
- Configured game folders are scanned automatically. Entries use titles and stable IDs from local metadata; SteamGridDB supplies covers, heroes and logos.
- Metadata and images are cached locally for offline use. Settings stores the API key in the desktop keyring; the environment can override it.
- Linux launch/rebuild uses the modified AnyPS5 fork. The Profiler console displays captured output and retains complete session logs. Animated WebP heroes and Linux desktop installation are implemented.
- Embedded game rendering, structured telemetry and frame-analysis charts remain planned.
- Small cache and launch checks verify data behavior; native UI smoke checks cover library navigation and reaching an external game window.

The current scope is checked for library behavior, cache preservation and launch preparation. Actual gameplay compatibility remains unverified. The integration experiments below describe remaining validation and extensions. Additional modules and screens need a concrete use case.

## Phase 0: Technical feasibility

### A. Game launch and metadata

A small program launches existing output from the modified AnyPS5 fork with an explicit executable, working directory and arguments. It reads available title metadata, captures logs and observes process state. The Linux launcher implements this direction, including relinking missing code and an explicit Rebuild action; Windows launch remains unverified.

**Success criteria:** Normal launches are reproducible; errors are understandable; process termination and exit codes are correct; missing icons or incomplete metadata do not prevent launching. Record the Windows and Linux outputs used.

### B. Game window, toolkit and input

A minimal host window receives an area for the running game. First test the current GTK4 direction; libadwaita remains optional. If its native window integration is unreliable, compare the same experiment with Qt 6. This is a small integration proof, not two complete frontends.

The matrix includes Windows and Linux/X11. Treat Wayland separately, especially XWayland with the current X11-based AnyPS5 build. Record display and toolkit backends for host and game processes.

**Success criteria:** Game rendering without a problematic extra image copy, correct resizing and DPI behavior, deliberate focus switching between game and UI, working keyboard/mouse/Xbox input, a clear fullscreen path and clean handling of game crashes. Explicitly document platforms that support only a separate window.

**Decision afterward:** Settle the toolkit, initial supported platform configurations and window strategy. Reliable embedded gameplay is the intended product. A separate window can make an early launcher usable while embedded integration remains unproven.

### C. Small telemetry interface

An optional sender provides a handshake and a limited set of structured events. The receiver associates frames and handles delayed GPU data. Existing timing files provide a reference without becoming an indefinitely stable API.

**Success criteria:** Explain measurement points and clock basis; make frame IDs traceable; ensure disconnection and a slow receiver do not block the game; count dropped events; compare baseline and instrumented runs for overhead. Never present synthetic unavailable values as measurements.

### D. Artwork and rendering

Validate title metadata import, SteamGridDB search, a persisted manual mapping and cached cover, hero and logo assets. Test at least one animated WebP on the intended platforms. The API/cache implementation and hero player provide a starting point; manual selection and the full platform matrix still need work.

**Success criteria:** Offline cache display, correctable mismatches, readable hero text, reliable animation pausing and no blocking decoding on the UI thread. API access and image-loader packaging are documented.

## Phase 1: Library extensions

Game-folder import, search, hero selection, "Recently played" before the full collection, Linux launch/rebuild, process state and logs are implemented. The remaining library goals are richer launch profiles, manual artwork selection and local-icon fallbacks. A future integrated game view must expose only verified presentation capabilities.

One active game process is supported. Missing generated code is automatically relinked; explicit Rebuild recreates only generated code. Richer launch profiles, extensive test runners, cloud features and complete GPU event analysis remain outside the current scope.

**Success criteria:** A new user can add and launch an existing game without guessing internal file structures. A second launch is never created silently. Missing network access, artwork or profiler support does not make the library unusable.

## Phase 2: Profiler foundation

Add frame times, basic CPU/GPU association, live history and stable frame selection. Available runtime capabilities determine frame details. Recordings can be started, stopped, saved and reopened after the game ends.

Each recording includes runtime/game build, selected launch profile, measurement mode, relevant display settings and reported data loss. The profiler display can pause while the game continues.

**Success criteria:** The chart and right-hand details refer to the same frame; estimated FPS and measured GPU time are distinguishable; incomplete frames remain visible; a slow UI does not cause unbounded memory growth. Examine recording overhead in a controlled comparison.

## Phase 3: Deeper analysis

Possible extensions include draw/pipeline counters, shader/pipeline hotspots, separate submission and wait times, multiple CPU threads, comparison of two recordings and additional export formats. Every feature requires a verified data source and suitable interpretation.

The design's Imports area could combine logged runtime calls, unresolved symbols and their frequencies. A Tests area could offer repeatable launches with defined profiles and result comparisons. Both remain later ideas, outside the first profiler's required scope.

Frame screenshots, GPU capture integration and investigation of selected rendering events are other possibilities. These do not automatically imply deterministic replay, reverse gameplay or arbitrary game pause.

## Phase 4: Quality and distribution

Linux installation already includes a desktop entry and icon. Clean end-user packages, CI and broader platform checks remain planned. Cover high DPI, long titles, large libraries, missing files, controller hotplug, changing input focus, slow disks and interrupted telemetry. Publish the verified platform matrix.

The launch view and profiler need good keyboard access. Animations can be reduced. Recordings and artwork have configurable limits. Check the app icon, resources and runtime libraries in clean Windows and Linux packages.

## Next focus

Build on the implemented library and console: verify actual gameplay, establish a window-integration strategy and display a few correctly interpreted measurements. A small, accurate frame analysis is more useful than many tabs with uncertain data sources.

Broader requests remain planned extensions. Practical results determine the next implementation's scope.
