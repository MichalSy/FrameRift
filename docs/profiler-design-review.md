<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Native profiler visual review

Reference: [profiler-live-v6](../assets/design/profiler-live-v6.png), 1578 × 997. The native screenshots use the same window dimensions and real bridge observations. Consumer timing has no source yet and intentionally shows `–` and empty bars. The latest requested simplification removes permanent scope explanations and the normal-state missing-frame warning; details remain in tooltips.

![Native FPS dashboard](../assets/screenshots/profiler-live.png)

![Native flip-interval dashboard](../assets/screenshots/profiler-flip-intervals.png)

## Visual acceptance

Manual weighted assessment: **84/100**, exceeding the requested 75% threshold. This is a visual estimate against the reference, not a pixel-similarity measurement or independent usability score. Actual telemetry values and absent consumer measurements are excluded from the appearance comparison.

| Area | Score | Evidence / differences |
| --- | --- | --- |
| Composition | 30/35 | Session strip, four metrics, full-width chart, ten-row consumer table, shader/memory right column and status rail match. No automation. |
| Geometry and spacing | 20/25 | Flat rectangular panels and approximately 64/36 lower split. Native decorations add an inset; chart baseline and row heights differ slightly. |
| Palette and surfaces | 17/20 | Dark matte navy, cyan active controls, thin slate borders, restrained amber activity/stale states. Native surface is less textured. |
| Typography and numeric alignment | 13/15 | Right-aligned tabular numbers and consistent decimal precision. Metric columns and summary width remain stable as values change. Native text is slightly smaller. Screenshots before/after the 3000 ms → 16 ms change retain the same metric dividers. |
| Controls | 4/5 | FPS default, flip toggle, Live/Logs, monitoring and Follow live. Unsupported CPU/GPU measurement controls are disabled; update rate is fixed at one second. |

## Verification

- Full clean native build (`cmake --build build --clean-first -j2`).
- Bridge and game MCP checks pass. Added frame-rate checks cover 60 FPS, a 2.5 ms catch-up interval, a five-second stall, loss and epoch changes.
- Native monitoring, FPS/interval toggle, selection/Follow live, Logs and Library navigation checked while the game continued running; session control used Codex FrameRift MCP.
- Done counters use process totals; inactive windows do not reset them. Elapsed uses the separately labeled observed window.
- Missing data uses `–`; actual zero memory remains `0 B`. Native telemetry does not manufacture example rankings or timings.

The runtime can still stall independently of the dashboard. A low flip rate or active/queued shader count of zero does not identify its cause; the UI reports observation age and available work evidence.
