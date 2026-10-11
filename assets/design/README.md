<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Design images and sources

These images document FrameRift's visual direction. They were created with the built-in ImageGen tool as UI concepts. They are not screenshots of a working application. The current profiler workspace and input script editor are specified in [Profiler workspace and game input scripts](../../docs/profiler-workspace.md), with their [generation/edit prompts](profiler-prompts.md).

| File | View | Size |
| --- | --- | --- |
| [library.png](library.png) | Library, hero, recently played, game covers | 1672 × 941 pixels |
| [profiler.png](profiler.png) | Running game, frame analysis, frame times | 1672 × 941 pixels |
| [profiler-live-v6.png](profiler-live-v6.png) | Current native target: FPS/flip chart, ten consumers, shader and memory panels | 1578 × 997 pixels |
| [profiler-live-v2.png](profiler-live-v2.png) | FPS overview, live frame detail, scoped top consumers, script/capture pane | 1605 × 980 pixels |
| [profiler-input-scripts.png](profiler-input-scripts.png) | Per-game scripts, optional start frame, relative actions and capture checkpoints | 1604 × 980 pixels |

The profiler values are illustrative examples. The new workspace concepts contain empty screenshot placeholders and no generated game artwork. The native Live dashboard now implements the v6 composition with frame pacing, shader work and scoped memory; [native screenshots and visual review](../../docs/profiler-design-review.md) record its verification. The FPS history counts completed flips over one source second, while the overview estimate uses mean valid flip intervals. Per-frame consumer attribution, captures, saved scripts and script MCP tools remain unimplemented. Input scripts are planned to run immediately by default, with optional absolute/delayed starts. Other fictional games and covers in the earlier concepts serve the design. ASTRO BOT in the library is an artwork example, not a claim that it runs with AnyPS5.

## Artwork references

The following references informed the ASTRO BOT design. ImageGen may alter details and labels; the composite image is not a faithful reproduction of each individual asset.

- **Official hero/panorama:** [ASTRO BOT on PlayStation](https://www.playstation.com/de-de/games/astro-bot/), [direct hero asset](https://gmedia.playstation.com/is/image/SIEPDC/astro-bot-hero-desktop-01-en-23may24?wid=2560&fmt=jpeg&qlt=95).
- **Official landscape artwork:** [PlayStation image asset](https://image.api.playstation.com/vulcan/ap/rnd/202407/0200/aac40cfde55b67abf081d5d9ebcc5e3e08aaa65a4c99424e.jpg).
- **Title logo:** [Wikimedia Commons file page](https://commons.wikimedia.org/wiki/File:Astro_Bot_Logo.svg), [SVG](https://upload.wikimedia.org/wikipedia/commons/c/c0/Astro_Bot_Logo.svg). The file page identifies Sony Interactive Entertainment as the creator; Wikimedia is the reference location.
- **SteamGridDB cover:** [Grid 483425 by Mingebag](https://www.steamgriddb.com/grid/483425), [image asset](https://cdn2.steamgriddb.com/grid/a350d0242847ce6bf914261c6e2712c1.png). Related [SteamGridDB game entry](https://www.steamgriddb.com/game/5454475).

SteamGridDB is the application's primary artwork source. The official PlayStation and Wikimedia references explain this specific concept's origins; they are not additional automatic download sources for the app.

## Rights and reuse

Game artwork, title logos and trademarks retain their respective rights. Public availability or inclusion on SteamGridDB does not grant a general license for every reuse. FrameRift's [AGPL-3.0-or-later license](../../LICENSE) does not cover third-party artwork, logos or trademarks. This directory contains composite design images, not a collection of original game assets.
