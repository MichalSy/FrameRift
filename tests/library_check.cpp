// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library/library.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::int64_t execute(const std::filesystem::path& path, const char* sql) {
    sqlite3* database = nullptr;
    std::int64_t value = 0;
    const int opened = sqlite3_open(path.string().c_str(), &database);
    const int result = opened == SQLITE_OK ? sqlite3_exec(database, sql,
        [](void* output, int count, char** values, char**) {
            if (count > 0 && values[0]) {
                *static_cast<std::int64_t*>(output) = std::strtoll(values[0], nullptr, 10);
            }
            return 0;
        }, &value, nullptr) : opened;
    const std::string error = database ? sqlite3_errmsg(database) : "Could not open check database";
    sqlite3_close(database);
    if (result != SQLITE_OK) throw std::runtime_error(error);
    return value;
}

template <typename Action>
void require_rejected(Action action) {
    try {
        action();
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("An invalid cache or preview update was accepted");
}

framerift::Game preview(std::int64_t id, const char* title, std::int64_t last_played) {
    framerift::Game game;
    game.id = "sgdb-" + std::to_string(id);
    game.title = title;
    game.subtitle = "SteamGridDB preview";
    game.genre = "Game";
    game.cover_path = "/cache/" + game.id + "/cover.png";
    game.hero_path = "/cache/" + game.id + "/hero.png";
    game.landscape_path = "/cache/" + game.id + "/landscape.png";
    game.last_played = last_played;
    game.steamgriddb_id = id;
    game.logo_path = "/cache/" + game.id + "/logo.png";
    game.source_url = "https://www.steamgriddb.com/game/" + std::to_string(id);
    return game;
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("framerift-cache-check-" + std::to_string(unique));
    try {
        std::filesystem::create_directories(directory);
        const auto demo_path = directory / "nested/demo.sqlite";
        const std::vector<framerift::Game> previews{
            preview(5454475, "Astro Bot", 42),
            preview(5266059, "Ratchet & Clank: Rift Apart", 0),
        };
        {
            framerift::Library library(demo_path.string());
            require(library.games().empty(), "A new cache should start empty");
            library.cache_demo(previews);
            const auto games = library.games();
            require(games.size() == 2 && games.front().id == previews.front().id,
                "Preview cache or recent ordering is wrong");
            require(games.front().is_demo && games.front().title == "Astro Bot", "Demo fields did not round-trip");
            require(games.front().cover_path == previews.front().cover_path &&
                games.front().hero_path == previews.front().hero_path &&
                games.front().landscape_path == previews.front().landscape_path &&
                games.front().logo_path == previews.front().logo_path &&
                games.front().source_url == previews.front().source_url && games.front().steamgriddb_id == 5454475,
                "Artwork paths and catalog provenance did not round-trip");
            require(games.front().last_played == 42 && games.back().last_played == 0,
                "Recent games were not persisted");
        }
        require(execute(demo_path, "PRAGMA user_version") == 2, "New cache schema version is wrong");
        {
            framerift::Library library(demo_path.string());
            require(library.games().size() == 2, "Cached API records disappeared on restart");
            auto invalid = previews;
            invalid.front().cover_path.clear();
            require_rejected([&] { library.cache_demo(invalid); });
            require_rejected([&] { library.cache_demo({previews.front(), previews.front()}); });
            require(library.games().size() == 2, "Invalid preview updates changed the cache");

            library.cache_demo({previews.back()});
            require(library.games().size() == 1 && library.games().front().id == previews.back().id,
                "Refreshing previews did not replace previous preview rows");
        }

        const auto existing_path = directory / "existing.sqlite";
        execute(existing_path,
            "CREATE TABLE games (id TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL, subtitle TEXT NOT NULL,"
            "genre TEXT NOT NULL, cover_path TEXT NOT NULL, hero_path TEXT NOT NULL, landscape_path TEXT NOT NULL,"
            "last_played INTEGER NOT NULL DEFAULT 0, is_demo INTEGER NOT NULL DEFAULT 0 CHECK (is_demo IN (0, 1)));"
            "CREATE TABLE cache_metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
            "INSERT INTO games VALUES ('demo-astro', 'Astro Bot', 'User game', 'Platformer', 'cover.png', 'hero.png', 'wide.png', 99, 0);"
            "INSERT INTO games VALUES ('demo-rally', 'Pixel Rally', 'Generated preview', 'Racing', 'old.svg', 'old.svg', 'old.svg', 0, 1);"
            "INSERT INTO games VALUES ('other-preview', 'Existing preview', 'Preserve on migration', 'Game', 'a.png', 'a.png', 'a.png', 0, 1);"
            "INSERT INTO cache_metadata VALUES ('demo_seeded', '1'), ('user_setting', 'keep');"
            "PRAGMA user_version = 1;");
        {
            framerift::Library library(existing_path.string());
            const auto games = library.games();
            require(games.size() == 2 && games.front().id == "demo-astro" && !games.front().is_demo &&
                games.front().last_played == 99 && games.front().steamgriddb_id == 0 &&
                games.back().id == "other-preview", "Migration changed existing records or kept generated artwork");
            library.cache_demo(previews);
            require(library.games().size() == 3 && library.games().front().subtitle == "User game",
                "Preview refresh changed a user game");
            auto collision = previews;
            collision.front().id = "demo-astro";
            require_rejected([&] { library.cache_demo(collision); });
            require(library.games().size() == 3 && library.games().front().last_played == 99,
                "Failed preview refresh did not roll back");
        }
        require(execute(existing_path, "PRAGMA user_version") == 2, "Legacy cache was not migrated");
        require(execute(existing_path, "SELECT COUNT(*) FROM cache_metadata WHERE key = 'demo_seeded'") == 0 &&
            execute(existing_path, "SELECT COUNT(*) FROM cache_metadata WHERE key = 'user_setting' AND value = 'keep'") == 1,
            "Migration did not preserve user metadata or remove the old seed flag");

        const auto invalid_path = directory / "invalid.sqlite";
        execute(invalid_path, "PRAGMA user_version = 3");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 0; CREATE TABLE games (id TEXT)");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 1");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 2");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        const auto unrelated_path = directory / "unrelated.sqlite";
        execute(unrelated_path, "CREATE TABLE unrelated (value TEXT)");
        require_rejected([&] { framerift::Library library(unrelated_path.string()); });
        const auto corrupt_path = directory / "corrupt.sqlite";
        std::ofstream(corrupt_path) << "This is not a SQLite database";
        require_rejected([&] { framerift::Library library(corrupt_path.string()); });

        std::filesystem::remove_all(directory);
        std::cout << "Library cache check passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "Library cache check failed: " << error.what() << '\n';
        return 1;
    }
}
