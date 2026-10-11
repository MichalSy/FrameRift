<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Native profiler visual review

Reference: [profiler-live-v6](../assets/design/profiler-live-v6.png). The native workspace retains its layout, with the requested lighter navy palette, restrained rounding and more generous spacing. The application owns the shared Library-style header; connection status sits beside Profiler, and Library search/refresh are hidden on the Profiler page. The Live content scrolls instead of compressing resource cards. The scrollbar uses a reserved lane at the right page edge, outside card padding, with overlay scrolling disabled. Screenshots show real bridge observations, not example telemetry; consumer timing has no source yet and intentionally shows `–` and empty bars.

![Native FPS dashboard](../assets/screenshots/profiler-live.png)

![Native flip-interval dashboard](../assets/screenshots/profiler-flip-intervals.png)

![Scrolled shader and memory cards](../assets/screenshots/profiler-resources.png)

## Visual direction

The original v6 implementation was manually assessed at 84/100 against its flat-panel concept. The current palette, 8 px card corners, more spacious rows and scrolling intentionally follow the newer requested styling. That earlier score is not a pixel-similarity claim for the revised appearance.

Cards use 20 px internal padding and 16 px section/column gaps. Shader work and cache rows have deliberate vertical spacing. Each memory label/value pair aligns across the card, with its budget bar on a separate full-width line. Numeric columns retain tabular digits, right alignment and stable overview widths.

## Verification

- Native shell check: connection status remains beside Profiler on both pages; search and refresh are visible in Library and hidden in Profiler. Branding, Settings and window controls keep the shared shell styling.
- Native build succeeds (`cmake --build build -j2`); the previous workspace implementation also passed a full clean build.
- Bridge and game MCP checks pass. Added frame-rate checks cover 60 FPS, a 2.5 ms catch-up interval, a five-second stall, loss and epoch changes.
- Native scrolling checked at a shorter height. The right-edge scrollbar stays outside card padding, and scrolling reaches the complete shader/memory cards and footer. A taller window keeps the same resource spacing.
- Native monitoring, FPS/interval toggle, selection/Follow live, Logs and Library navigation checked while the game continued running; session control used Codex FrameRift MCP.
- Done counters use process totals; inactive windows do not reset them. Elapsed uses the separately labeled observed window.
- Missing data uses `–`; actual zero memory remains `0 B`. Native telemetry does not manufacture example rankings or timings.

The runtime can still stall independently of the dashboard. A low flip rate or active/queued shader count of zero does not identify its cause; the UI reports observation age and available work evidence.
