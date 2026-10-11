<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Design and interaction

## Shared shell

- Native graphite background, cyan accent, angular F mark and compact typography.
- One header outside the page stack: navigation, connection beside Profiler, Settings and window controls.
- Search/refresh appear only in Library; pages own their content.
- Stable cover sizes; wider windows add grid columns. Animations pause when hidden/disabled.

## Library

- Selected-game hero, Recently played, then Your games.
- Cover selection changes hero; metadata provides display titles.
- Play prepares/launches the external game; Rebuild replaces generated code without launching.
- Quiet missing-artwork placeholders; technical/provider explanations belong in documentation.
- Cached metadata/artwork works offline; manual matching and local-icon fallback remain planned.

## Profiler

- Live/Logs; game keeps its external window and independent lifecycle.
- Overview metrics, full-width FPS/interval chart, selected-frame header and separate shader/memory panels.
- Follow live and Monitoring are independent. Info/tooltips expose scope, coverage and age.
- Missing/stale values stay explicit; consumer card shows No data until attribution exists.
- Shared card/control styling, 4 px corners, 16 px card padding/gaps and tabular right-aligned values.
- Narrow windows stack resource panels; vertical scrolling preserves row sizes.
- Captures/input scripts and embedded game content remain planned.

## References and code

- [Library screenshot](../assets/screenshots/library.png), [native profiler references](profiler-design-review.md).
- [Design concepts and artwork sources](../assets/design/README.md); concepts do not establish gameplay or measurements.
- `src/ui/app.cpp`, `src/ui/profiler.cpp`, `src/ui/components.hpp`, `src/ui/hero.cpp`, `assets/ui/style.css`.
- [Measurement semantics](profiling.md), [remaining workspace plan](profiler-workspace.md).
