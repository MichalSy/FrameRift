<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Profiler ImageGen prompts

These prompts were used with the built-in ImageGen tool, with opaque backgrounds. They produce design references with illustrative values, not implemented UI or benchmark results. No game artwork was generated. The selected outputs are `profiler-live-v2.png` and `profiler-input-scripts.png`.

## Live workspace generation

```text
Use case: ui-mockup
Asset type: FrameRift native desktop profiler design reference, not a screenshot of implemented functionality.
Primary request: Produce one polished, flat, straight-on high-fidelity desktop UI mockup, wide landscape about 1800 by 1100, with sharply readable English labels and professional information density. Match the current FrameRift visual system: dark navy graphite background #08141c, header #0e2231, panels #102330, quiet thin borders #345267, white/off-white Segoe/Inter-like typography, restrained cyan #00d1e8 active controls, amber for pending information. No glowing cyberpunk decoration, no 3D hardware, no perspective.
Composition: one complete application window, all edges visible. Top header: simple angular cyan F symbol, FrameRift, Library, Profiler selected, Settings, native window buttons. Below: "SUPER BOMBERMAN R 2" with a small subtitle "PPSA07191", a status pill "Game connected", "Stop game". Include a clearly visible subdued banner "DESIGN PREVIEW · EXAMPLE VALUES".
Subnavigation: "Live" selected, "Captures", "Input scripts", "Logs". On the right "Monitoring" toggle enabled, "Update: 1 s", "Save snapshot" and "Screenshot" buttons.
Four compact summary panels all explicitly scoped to "Last 1 s": "Flip interval" 16.8 ms, "P95" 20.4 ms, "Worst" 26.7 ms, "Estimated flip rate" 59.5 /s. Add small "58 samples · 2 missing" freshness label, not a fabricated monitor FPS label.
Main workspace, left about 72 percent, right about 28 percent. Main workspace upper left panel titled "Frame detail", selector "Worst in last 1 s", selected "Frame 4812", live-follow toggle "Follow live", secondary "Inspect" action. Make the individual frame visually distinct from the last-second cards. Show "Flip interval 26.7 ms", "Driver worker 8.6 ms", "GPU timing pending", "Frame incomplete". No combined CPU+GPU stacked total.
Below in the main area a wide "Top consumers" panel, tabs "Driver CPU" selected and "GPU programs", scope pill "Frame 4812". Display a readable three-column table "Phase", "Elapsed", "Calls" with three rows and short quiet cyan horizontal bars: "Packet decode" 4.2 ms 412, "Texture upload" 2.1 ms 18, "Pipeline lookup" 1.3 ms 56. Small footer "Inclusive elapsed times · overlaps possible". Also show a small secondary GPU table labeled "GPU programs · last 10 s", with "Program", "GPU time", "Calls": "0x7FA120" 340 ms 220, "0x8BC340" 210 ms 128, and "Unattributed" 95 ms. Do not invent resolved shader names. This aggregate table is visibly NOT the selected-frame timing.
Right panel titled "Input tests", dropdown "Arena smoke", small status "Ready", button "Run script", quiet "Edit script" text action. Show a concise preview: "Start: acceptance +60 frames", then "0   Cross down", "+20   Cross up", "+0   D-pad right down", "+40   D-pad right up", "+0   Snapshot". Show a thin run progress row labeled "No active run". Below a "Capture" panel with a completely empty gray 16:9 placeholder, a camera icon and "No screenshot attached"; controls "Capture next frame" and "Open captures". Do not create game imagery.
Bottom of main workspace spans left and right: a thin clear chart titled "Flip intervals · last 60 s", cyan irregular line, y-axis "ms", visible 16.7 ms reference line, selected amber vertical marker "4812", small "Missing data" gap. Controls "Follow live" and "Inspect selection". Lowest compact "Recent log" strip with two short monochrome sample lines, "Open full log", no fake actual error severity.
Constraints: only application interface. No actual game illustration, no covers, no fictional game screenshot, no mouse cursor, no watermark. This is a future bridge-capable design with illustrative values, honestly labeled. All labels crisp, useful whitespace, subtle dividers, readable chart and table, native GTK desktop feel.
```

## Live FPS label and consistent follow controls

Edit applied to the generated live workspace.

```text
Use case: precise-object-edit / ui-mockup. Edit the attached FrameRift profiler UI design in only these two ways, preserving the exact application layout, English labels, all other tables, panels, typography, colors, sizes and values: (1) Replace the four overview metric cards left to right with "FPS (estimated) (last 1 s)" showing the large bold value "59.5 FPS"; "Flip interval (last 1 s)" with "16.8 ms"; "P95 (last 1 s)" with "20.4 ms"; "Worst (last 1 s)" with "26.7 ms". The first FPS card must be the prominent primary overview metric, with the number in restrained cyan. Keep the fifth samples card unchanged. Spell FPS exactly F P S. Remove the old "Estimated flip rate" and "/s" text. (2) Make the "Follow live" toggle in the Frame detail header cyan/on, consistent with the existing on-state toggle in the chart. Keep the "DESIGN PREVIEW · EXAMPLE VALUES" banner and empty camera placeholder. Do not change anything else, do not create game imagery.
```

## Live immediate script start

Final edit applied to the live workspace.

```text
Use case: precise-object-edit. Edit the attached FrameRift live-profiler design only in its Input tests preview panel: replace the exact line "Start: acceptance +60 frames" with "Start: immediately". Preserve every other pixel/layout/label/value including the prominent "FPS (estimated)" 59.5 FPS card, +20/+0/+40 relative input offsets, all frame metrics and charts, design-preview banner, synchronized Follow live controls and empty camera placeholder. No other changes.
```

## Input script editor generation

```text
Use case: ui-mockup
Asset type: FrameRift desktop Input scripts editor design reference, not implemented functionality.
Primary request: Create one complete, flat high-fidelity native desktop application UI mockup, wide landscape about 1800 by 1100, with crisp readable English text. Exactly the same FrameRift styling as a dark navy GTK application: #08141c background, #0e2231 header, #102330 panels, quiet #345267 dividers, restrained #00d1e8 accent, white Inter/Segoe-like text, small amber badges. No perspective, no 3D, no neon decoration.
Header: simple cyan angular F symbol, FrameRift, Library, Profiler selected, Settings, native window buttons.
Session strip: "SUPER BOMBERMAN R 2", subtitle "PPSA07191", connected status, "Current frame 421", "Stop game". Clearly visible subdued banner "DESIGN PREVIEW · EXAMPLE VALUES". Subtabs "Live", "Captures", "Input scripts" selected, "Logs".
Main three-pane editor. Left narrow panel titled "Saved scripts" with game-scoped entries "Arena smoke" selected, "Menu navigation", "Screenshot check"; small "New script" and "Duplicate" buttons. No game cover imagery or library grid.
Center wide pane titled "Arena smoke", subtitle "Saved · Revision 3", "Rename", "Save", cyan "Run script" button. Below a two-column form: "Start mode" dropdown "Absolute frame", "First frame" numeric field 500, small alternate option "After acceptance"; field "On late action" reads "Reject run". A clearly readable helper text "Offsets are relative to the previous planned action." Show no game pause or breakpoint control.
Large structured table editor with columns "Step", "Frame offset", "Resolved frame", "Action", "Input". Six rows:
1 | 0 | 500 | Button down | Cross
2 | +20 | 520 | Button up | Cross
3 | +0 | 520 | Button down | D-pad right
4 | +40 | 560 | Button up | D-pad right
5 | +0 | 560 | Save snapshot | —
6 | +0 | 560 | Screenshot | —
Show small row reorder grip and remove icon, "Add action" button beneath. The clear 500 -> 520 -> 560 progression is an essential part of the design. A small horizontal timeline below with markers "500", "520", "560", colored lanes "Cross", "D-pad", "Captures" illustrating held buttons spanning 500-520 and 520-560. All text readable, no decorative diagrams.
Right pane titled "Execution", status "Ready", "No active run", and a compact validation checklist "Saved revision", "Game matches", "Frame 500 is ahead", "Inputs balanced". A separate subsection "Capture steps" states "Snapshot at 560" and "Screenshot at 560", with a totally empty gray 16:9 camera placeholder "No screenshot yet". Do not generate gameplay art. Footer small button "View last result".
Lower center panel titled "Run result" with a quiet empty state "Run this script to see requested and applied frames." Show column headers "Action", "Requested", "Applied", "Status", no invented success results. At the bottom a compact secondary "Recent log" strip with "Open full log".
Constraints: UI labels and identifiers in English. Structured game input editor, not a shell or source-code editor. No command-line syntax, no code execution panel, no generic chatbot, no generated game artwork, no screenshot imagery, no watermark. Emphasize game profile persistence, relative frame offsets, validation and separation of receipt versus execution. All action controls look native, restrained, practical and visually consistent.
```

## Input editor optional start and capture alignment

Final edit applied to the generated input editor.

```text
Use case: precise-object-edit. Edit the attached FrameRift Input scripts UI concept to make an optional start frame clear. Preserve the complete dark navy/cyan visual system, native app header, saved script list, six action rows, button inputs, side-panel structure, empty screenshot placeholder and example-values banner. Make only these linked functional changes:
1. In the top session strip, keep "Current frame 421" and add "FPS (estimated)" with cyan value "59.5" in the empty space between Stop game and the preview banner.
2. In the editor form, show "Start mode" dropdown selected "Immediately". Replace the mandatory "First frame 500" field with a muted optional control labeled "Scheduled start (optional)" containing a disabled empty field with placeholder "Frame or offset". This control is inactive because Immediately is selected. Keep "On late action: Reject run". Replace the small helper text with "Run now; the runtime chooses the next input boundary. Offsets remain relative to the previous planned action." No absolute start frame is required.
3. Keep the column "Frame offset" values EXACTLY 0, +20, +0, +40, +0, +0. Rename the "Resolved frame" column to "Planned target" and show row targets EXACTLY "Start", "Start +20", "Start +20", "Start +60", "Start +60", "Start +60". Preserve actions exactly Button down Cross, Button up Cross, Button down D-pad right, Button up D-pad right, Save snapshot, Screenshot. Thus button hold spans are 20 then 40 frames, not extra delays.
4. Timeline tick labels become "Start", "Start +20", "Start +60". Cross lane spans Start to Start+20; D-pad lane spans Start+20 to Start+60. Both captures occur at the same Start+60 tick: show one yellow dot vertically aligned to that tick with one label "Snapshot + Screenshot".
5. In the right Validation checklist replace "Frame 500 is ahead" with "Start resolves at acceptance". In Capture steps show "Snapshot at Start +60" and "Screenshot at Start +60". Keep Ready / No active run and empty run-result state.
All labels in English, sharply readable. Do not change any other UI, no game artwork, no fictional gameplay, no shell scripting. This remains a design preview with example values.
```
