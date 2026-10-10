<!--
SPDX-License-Identifier: AGPL-3.0-or-later
Copyright (C) 2026 Michal Sytko and FrameRift contributors
Original project: https://github.com/MichalSy/FrameRift
Additional attribution terms: see NOTICE in the repository root.
-->

# Artwork, SteamGridDB and animations

[Project overview](../README.md) · [Toolkit and platforms](toolkit-and-platforms.md) · [Game integration](game-window-integration.md)

## Status and product direction

**SteamGridDB is FrameRift's primary artwork source.** The library uses portrait covers and a large hero area with a separate title logo for the selected game. The API client and local SQLite/file cache are implemented. Settings stores the API key in the desktop keyring; `STEAMGRIDDB_API_KEY` overrides it.

Animated WebP heroes use static images as a fallback. An explicit library refresh also searches for animations when a hero already exists. In the implementation, `src/ui/hero.cpp` uses libwebpdemux for timed banner playback, pauses when the library is hidden and respects GTK's animation setting. The image fills the header width proportionally, with vertically centered cropping and a bottom gradient. The remaining sections describe requirements and further planned extensions. Artwork does not determine whether a game is playable.

## Asset categories

SteamGridDB distinguishes **Grids**, **Heroes**, **Logos** and **Icons**. Its API documents portrait grids such as 600×900 and hero formats such as 1920×620. FrameRift stores these categories separately. Scaling a square icon alone does not make it a portrait cover. [SteamGridDB API](https://www.steamgriddb.com/api/v2)

| FrameRift use | SGDB category | Proposed handling |
|---|---|---|
| Library card | Grid | Prefer portrait; preserve aspect ratio |
| Selected game / wide banner | Hero | Wide artwork with controlled cropping |
| Title in the hero area | Logo | Check transparency and contrast |
| Small list/status display | Icon | Use an appropriate small resolution |

A cover with an embedded logo and a separate transparent logo are different resources. The hero data model should allow both independently to avoid displaying a title twice over an already labeled background.

## Reliable game matching

The API supports name searches, SGDB game IDs and external platform IDs. Documented platforms include Steam, Origin, EGS, Battle.net, Ubisoft, Flashpoint and eShop. PS5/PPSA IDs are absent from that list. For a PS5 library, the proposed approach is therefore to **search by name, verify the result and persist the SGDB ID**. [Official OpenAPI specification](https://www.steamgriddb.com/static/openapi.yml)

Matching should not choose the first name result again on every launch. Proposed flow:

1. Local metadata supplies the game title and any available distinguishing details.
2. Search produces candidates. Title variants, release year, edition and series help select a match.
3. Ambiguous results remain visibly unconfirmed.
4. The confirmed SGDB game ID is stored with the local library entry.
5. Later updates reuse that mapping; manual correction remains possible.

For example, **ASTRO BOT (2024)** and **ASTRO's PLAYROOM** are separate library entries. A similar title or shared character must not automatically merge them.

Local game identity remains independent. One SGDB entry may describe another edition or several platform versions. Artwork matching changes neither local launch configuration nor compatibility status.

## API access

SteamGridDB requires an API key in the HTTP header `Authorization: Bearer <token>`. Users can generate a key in their account preferences. Reading public website entries and using the authenticated API are separate access paths. [API authentication](https://www.steamgriddb.com/static/openapi.yml), [official client introduction](https://github.com/SteamGridDB/node-steamgriddb#readme)

FrameRift's access requirements are:

- Configure the personal key at runtime; never place a production key in the public repository.
- Send authentication only to the documented API host, never to arbitrary artwork URLs.
- Distinguish missing authentication, missing matches and temporary network failures.
- Batch catalog requests, cache results and limit concurrency.
- Keep existing images visible when new requests fail.
- Keep the library and local game launching usable without a working artwork service.

The implemented client uses libcurl outside the GTK thread, following the [toolkit direction](toolkit-and-platforms.md). Explicit cancellation and lifetime rules remain necessary.

## Asset selection

Automatic selection should provide a predictable default without permanently binding a game to one upload. Proposed priority:

1. A manually selected resource.
2. A suitable cached resource with a confirmed game mapping.
3. A suitable SteamGridDB asset in the requested category and size.
4. An existing local game image or a neutral placeholder when no suitable asset exists.

Scores and categories help selection; they do not establish official origin or a free license. Storing the asset ID and original entry link should allow later correction.

If a separate logo is missing, display the game title as ordinary UI text rather than an invisible or distorted image. Incorrect artwork should be reset for one mapping without deleting the library or launch configuration.

## Explicit WebP animation support

The API supports `static` and `animated` filters; `static` is the default. The client must explicitly request animated heroes. SteamGridDB's FAQ states that current animated assets use **WebP** and that APNG was discontinued. GIF is not part of this plan. [API](https://www.steamgriddb.com/api/v2), [SteamGridDB FAQ](https://www.steamgriddb.com/faq)

**Displaying a file and playing its animation are separate capabilities.** A static WebP decoder or ordinary image widget does not establish correct multi-frame playback. Frame timing, blending/disposal, transparency, loops and pausing need verification.

libwebp provides toolkit-independent APIs for animated WebP containers and decoding. The hero player uses these to produce timed frames for GTK. Windows playback remains unverified. Check whether a toolkit already provides a complete playback path before adding another dependency. [WebP container/animation API](https://developers.google.com/speed/webp/docs/container-api)

### Proposed playback rules

- Animate only the visible selected hero by default.
- Pause hidden or unselected heroes; library cards remain static initially.
- Minimize, leave the library or disable animations to stop unnecessary decoder work.
- Keep a static fallback available when an animated asset is incomplete or cannot be decoded.
- Handle frame duration and loops explicitly; avoid an unrestricted independent render loop.
- Offer a still image for reduced motion.

**Proposal for profiling:** Pause or hide artwork animations by default during an active profiler recording, so decorative UI costs do not silently affect a performance comparison. This remains a proposed product rule. The decision depends on measured overhead.

## Cache extensions

The implemented local cache supports offline use and quick selection changes. The following fields and limits describe proposed extensions rather than a complete implemented schema.

| Stored information | Purpose |
|---|---|
| Local game ID and confirmed SGDB game ID | Stable mapping |
| Category and SGDB asset ID | Manage Grid/Hero/Logo/Icon separately |
| Original URL and public entry link | Preserve origin and support updates |
| Upload/author details, where available | Retain provenance |
| MIME type, dimensions and animation status | Select a suitable display path |
| Cache generation and content validation | Detect partial or stale files |
| Manual selection / local fallback source | Preserve user choices |
| Download state and last error class | Avoid treating errors as valid images |

Keep original files separate from derived display sizes. A thumbnail helps quick selection but does not replace a requested high-resolution hero. Decode for the resolution actually visible where possible.

Proposed technical rules:

- Bound the disk cache with a documented eviction policy; preserve manual local sources separately.
- Bound RAM/GPU caches for active and immediately upcoming views.
- Download to a temporary resource and accept it only after successful validation.
- Check format, dimensions and memory requirements before constructing large images.
- Avoid keeping every frame of every animation permanently decoded in memory.
- Combine concurrent requests for the same asset.
- Cache missing assets temporarily while allowing a manual retry.
- Cache cleanup must not change library entries or game mappings.

For scale, a decoded 1920×620 image at four bytes per pixel uses about 4.5 MiB; several dozen fully retained frames can consume substantial RAM. Compressed file size and decoded memory use are different quantities. This is a calculation, not a promised cache limit.

## Manual selection and local fallbacks

Users should be able to override cover, hero and logo independently. This is a planned local library feature, not a SteamGridDB upload workflow.

A manual selection stores the specific resource and its origin. New automatic search results should not silently replace it. For local files, decide whether FrameRift manages a copy or references the original source, and make that behavior clear in the UI.

Format or decoder errors must neither block the library nor replace a working selection. Use a visible placeholder and clear error state instead of repeated unlimited loading.

## Provenance, rights and publication

SteamGridDB is an artwork database with user contributions. A successful API request does not grant a blanket license for every image. Rights in the game, title logo and edited image may differ. A category such as "official" is not general permission to republish. Check the terms and each asset's information separately. [SteamGridDB Terms](https://www.steamgriddb.com/terms)

Rules for the public FrameRift repository:

- Clearly label design references as concept images.
- Identify the public original entry and origin of real game artwork.
- FrameRift's [AGPL-3.0-or-later license](../LICENSE) does not cover third-party game artwork, logos or trademarks.
- Do not bundle third-party artwork collections indiscriminately in releases.
- Keep demo/test material with clear permission or original neutral examples separate.

Showing ASTRO BOT or another real game in a design image is **not a compatibility claim**. Playability and profiler capabilities are documented separately using actual integration evidence.

## Open decisions and checks

| Question | Required evidence |
|---|---|
| How is a game mapping confirmed? | Ambiguous titles and editions remain correctable |
| Which decoder plays WebP animations? | Windows and Linux handle timing, alpha and loops correctly |
| What cache limits apply? | Measure RAM/GPU/disk use with a realistic library |
| When do animations pause? | Hidden UI is quiet; profiling rules are deliberately chosen |
| What local persistence is used? | Mappings and manual selections survive restarts |
| How do network failures behave? | Library and game launching work with cache/placeholders |
| Which assets are published? | Provenance and rights are established for each item |

Further artwork validation should cover a confirmed game match, portrait grid, static hero, transparent logo and animated WebP hero before expanding library coverage and automatic selection.
