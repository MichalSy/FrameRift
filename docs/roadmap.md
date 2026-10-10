<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Roadmap

## Purpose

This plan describes the intended product and open technical questions. Phases are not delivery commitments. Game window integration and the measurement interface need validation with real programs.

## Current focus: Game library and cache

- The native GTK4 library on `main` reads SQLite records and provides hero selection, search, sorting and recently played titles before the collection.
- Demo entries use real catalog games with covers, heroes and logos from the SteamGridDB API. Playtime values are for UI preview.
- Metadata and images are cached locally, keeping the existing library usable offline. On `main`, the API key comes from the process environment and is not stored in the cache.
- Scanning configured game folders, launching through the modified AnyPS5 fork, profiler console output, desktop keyring settings and animated heroes are development work.
- Embedded game rendering, structured telemetry and frame-analysis charts remain planned.
- A small cache check verifies data behavior; a virtual desktop smoke check compares the UI with the reference design.

The current library scope is checked for presentation, search, sorting, selection and cache preservation. The integration experiments below describe requirements beyond that committed scope. Additional modules and screens need a concrete use case.

## Phase 0: Technical feasibility

### A. Game launch and metadata

A small program launches existing output from the modified AnyPS5 fork with an explicit executable, working directory and arguments. It reads available title metadata, captures logs and observes process state. The development launcher implements this direction; it is not yet part of the committed library scope.

**Success criteria:** Normal launches are reproducible; errors are understandable; process termination and exit codes are correct; missing icons or incomplete metadata do not prevent launching. Record the Windows and Linux outputs used.

### B. Game window, toolkit and input

A minimal host window receives an area for the running game. First test the current GTK4/libadwaita direction. If its native window integration is unreliable, compare the same experiment with Qt 6. This is a small integration proof, not two complete frontends.

The matrix includes Windows and Linux/X11. Treat Wayland separately, especially XWayland with the current X11-based AnyPS5 build. Record display and toolkit backends for host and game processes.

**Success criteria:** Game rendering without a problematic extra image copy, correct resizing and DPI behavior, deliberate focus switching between game and UI, working keyboard/mouse/Xbox input, a clear fullscreen path and clean handling of game crashes. Explicitly document platforms that support only a separate window.

**Decision afterward:** Settle the toolkit, initial supported platform configurations and window strategy. Reliable embedded gameplay is the intended product. A separate window can make an early launcher usable while embedded integration remains unproven.

### C. Small telemetry interface

An optional sender provides a handshake and a limited set of structured events. The receiver associates frames and handles delayed GPU data. Existing timing files provide a reference without becoming an indefinitely stable API.

**Success criteria:** Explain measurement points and clock basis; make frame IDs traceable; ensure disconnection and a slow receiver do not block the game; count dropped events; compare baseline and instrumented runs for overhead. Never present synthetic unavailable values as measurements.

### D. Artwork and rendering

Validate title metadata import, SteamGridDB search, a persisted manual mapping and cached cover, hero and logo assets. Test at least one animated WebP on the intended platforms. The API/cache implementation and development hero player provide a starting point; manual selection and the full platform matrix still need work.

**Success criteria:** Offline cache display, correctable mismatches, readable hero text, reliable animation pausing and no blocking decoding on the UI thread. API access and image-loader packaging are documented.

## Phase 1: First usable library

The target scope includes game import, search, hero selection, "Recently played" before the full collection, launch profiles, process state and logs. SteamGridDB provides artwork, with manual selection and local fallbacks. A clear game view exposes only verified presentation capabilities.

One active game process is enough initially. Automatic relinking, extensive test runners, cloud features and complete GPU event analysis are unnecessary for this scope.

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

With a stable core, build packaging, CI and focused checks. Cover high DPI, long titles, large libraries, missing files, controller hotplug, changing input focus, slow disks and interrupted telemetry. Publish the verified platform matrix.

The launch view and profiler need good keyboard access. Animations can be reduced. Recordings and artwork have configurable limits. Check the app icon, resources and runtime libraries in clean Windows and Linux packages.

## Focus after UI and cache

After the feasibility experiments, narrow the first concrete development plan to three outcomes: reliably launch an existing game, implement the two native views and clearly display a few correct measurements. A small, accurate frame analysis is more useful than many tabs with uncertain data sources.

Broader requests remain planned extensions. Practical results determine the next implementation's scope.
