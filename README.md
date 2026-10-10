# FrameRift

**Launch games. Understand frames.**

FrameRift is an experimental native game library and performance workspace for AnyPS5.

**Game execution requires our [modified AnyPS5 fork](https://github.com/MichalSy/AnyPS5). FrameRift only works with this modified version.**

## Status

- Available on `main`: a GTK4 library with search, sorting, recently played games, a SQLite cache and SteamGridDB artwork. The current sample library does not represent installed or compatible games.
- In development: automatic game-folder scanning, Linux launch and rebuild, artwork settings with keyring storage, and a Profiler console with session logs.
- Planned: embedded game windows, frame timing and CPU/GPU analysis.

Linux is the current development platform; Windows support remains unverified. Game compatibility depends on AnyPS5 and is not guaranteed by a library entry.

## Build and run

Requires a C++20 compiler, CMake 3.20+, Ninja, pkg-config, GTK4 4.12+, SQLite3, libcurl 7.85+, JSON-GLib and `glib-compile-resources`, including development packages. The launcher/profiler work also requires GTK4 VTE 0.70+ (`vte-2.91-gtk4`), GIO, libsecret and libwebp/libwebpdemux; game launch requires Python 3.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/framerift
```

Supply `STEAMGRIDDB_API_KEY` through the process environment to fetch artwork. Cached metadata and images remain available offline. Keep the key outside the repository, database, logs and command arguments.

## Library preview

Screenshot of the development build.

![FrameRift game library](assets/screenshots/library.png)

## Documentation

- [Design](docs/design.md) and [architecture](docs/architecture.md)
- [Build dependencies](docs/technical-stack.md) and [platforms](docs/toolkit-and-platforms.md)
- [AnyPS5 integration](docs/anyps5-integration.md) and [game-window integration](docs/game-window-integration.md)
- [Profiling](docs/profiling.md), [input](docs/controller-input.md) and [artwork](docs/artwork.md)
- [Roadmap](docs/roadmap.md), [open questions](docs/decisions-and-open-questions.md) and [image sources](assets/design/README.md)

Contributions are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

FrameRift code and documentation are licensed under [MIT](LICENSE). AnyPS5 remains under [GPL-2.0-only](https://github.com/MichalSy/AnyPS5/blob/main/LICENSE). Third-party artwork and trademarks retain their respective rights and are excluded from FrameRift's MIT license.
