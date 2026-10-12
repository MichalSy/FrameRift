// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library/library.hpp"
#include "library/steamgriddb.hpp"

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

void game_fixture(const std::filesystem::path& directory, const char* metadata) {
    std::filesystem::create_directories(directory / "sce_sys");
    std::ofstream(directory / "eboot.bin") << "Original executable";
    std::ofstream(directory / "sce_sys/param.json") << metadata;
}

} // namespace

int main() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("framerift-cache-check-" + std::to_string(unique));
    try {
        require_rejected([] { framerift::save_steamgrid_api_key(""); });
        require_rejected([] { framerift::save_steamgrid_api_key("invalid\nkey"); });
        require_rejected([] { framerift::save_steamgrid_api_key(std::string("invalid\0key", 11)); });
#ifdef __linux__
        const auto* previous_key = std::getenv("STEAMGRIDDB_API_KEY");
        const bool had_key = previous_key != nullptr;
        const std::string saved_key = previous_key ? previous_key : "";
        require(setenv("STEAMGRIDDB_API_KEY", "framerift-check-override", 1) == 0, "Could not set API key override");
        require(framerift::read_steamgrid_api_key() == "framerift-check-override", "API key environment override was ignored");
        if (had_key) setenv("STEAMGRIDDB_API_KEY", saved_key.c_str(), 1);
        else unsetenv("STEAMGRIDDB_API_KEY");
#endif
        std::filesystem::create_directories(directory);
        const auto demo_path = directory / "nested/demo.sqlite";
        const framerift::LaunchSettings launch_settings{
            (directory / "Any PS5").string(),
        };
        std::filesystem::create_directories(launch_settings.anyps5_directory);
        const std::vector<framerift::Game> previews{
            preview(5454475, "Astro Bot", 42),
            preview(5266059, "Ratchet & Clank: Rift Apart", 0),
        };
        {
            framerift::Library library(demo_path.string());
            require(library.games().empty(), "A new cache should start empty");
            const auto unset = library.launch_settings();
            require(unset.anyps5_directory.empty(),
                "A new cache should not invent launch settings");
            auto settings = launch_settings;
            settings.anyps5_directory += "/.";
            execute(demo_path, "INSERT INTO cache_metadata (key, value) VALUES "
                "('launch.runtime_directory', '/old/runtime'), ('launch.relinker_path', '/old/relinker')");
            library.set_launch_settings(settings);
            require(execute(demo_path, "SELECT COUNT(*) FROM cache_metadata WHERE key IN "
                "('launch.runtime_directory', 'launch.relinker_path')") == 0,
                "Saving the checkout retained obsolete build overrides");
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
        require(execute(demo_path, "PRAGMA user_version") == 3, "New cache schema version is wrong");
        {
            framerift::Library library(demo_path.string());
            require(library.games().size() == 2, "Cached API records disappeared on restart");
            const auto settings = library.launch_settings();
            require(settings.anyps5_directory == launch_settings.anyps5_directory,
                "Launch settings were not canonicalized or persisted");
            auto invalid_settings = settings;
            invalid_settings.anyps5_directory = std::string("invalid\0path", 12);
            require_rejected([&] { library.set_launch_settings(invalid_settings); });
            require(library.launch_settings().anyps5_directory == settings.anyps5_directory,
                "Rejected launch settings partially replaced saved paths");
            library.set_launch_settings({});
            require(library.launch_settings().anyps5_directory.empty(), "Launch settings could not be cleared");
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
            library.set_launch_settings(launch_settings);
        }
        require(execute(existing_path, "PRAGMA user_version") == 3, "Legacy cache was not migrated");
        require(execute(existing_path, "SELECT COUNT(*) FROM cache_metadata WHERE key = 'demo_seeded'") == 0 &&
            execute(existing_path, "SELECT COUNT(*) FROM cache_metadata WHERE key = 'user_setting' AND value = 'keep'") == 1,
            "Migration did not preserve user metadata or remove the old seed flag");

        const auto invalid_path = directory / "invalid.sqlite";
        execute(invalid_path, "PRAGMA user_version = 4");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 0; CREATE TABLE games (id TEXT)");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 1");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 2");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        execute(invalid_path, "PRAGMA user_version = 3");
        require_rejected([&] { framerift::Library library(invalid_path.string()); });
        const auto unrelated_path = directory / "unrelated.sqlite";
        execute(unrelated_path, "CREATE TABLE unrelated (value TEXT)");
        require_rejected([&] { framerift::Library library(unrelated_path.string()); });
        const auto corrupt_path = directory / "corrupt.sqlite";
        std::ofstream(corrupt_path) << "This is not a SQLite database";
        require_rejected([&] { framerift::Library library(corrupt_path.string()); });

        const auto first_folder = directory / "games";
        const auto second_folder = directory / "other games";
        const auto first_game = first_folder / "not-the-game-title";
        game_fixture(first_game,
            R"({"titleId":"PPSA01288","localizedParameters":{"defaultLanguage":"de-DE", "de-DE":{"titleName":"German title"},"en-GB":{"titleName":"Metadata title"}}})");
        game_fixture(first_game / "assets/hidden game",
            R"({"titleId":"PPSA99999","titleName":"Do not scan assets"})");
        game_fixture(first_folder / "nested/second",
            R"({"titleId":"PPSA01880","localizedParameters":{"defaultLanguage":"fr-FR","fr-FR":{"titleName":"Default title"},"de-DE":{"titleName":"Other title"}}})");
        game_fixture(second_folder / "duplicate",
            R"({"titleId":"PPSA01288","titleName":"Duplicate installation"})");
        game_fixture(second_folder / "invalid-id", R"({"titleId":"../unsafe","titleName":"Invalid"})");
        game_fixture(second_folder / "missing-title", R"({"titleId":"PPSA00001"})");
        std::filesystem::create_directory_symlink(first_folder, first_folder / "loop");
        const std::vector<std::string> folders{first_folder.string(), (first_folder / ".").string(),
            first_game.string(), second_folder.string()};
        const auto scanned = framerift::scan_game_folders(folders);
        require(scanned.games.size() == 2 && scanned.warnings.size() == 2,
            "Folder scan did not deduplicate IDs, ignore asset subtrees or report invalid metadata");
        require(scanned.games.front().id == "PPSA01288" && scanned.games.front().title == "Metadata title" &&
            scanned.games.back().title == "Default title" && !scanned.games.front().is_demo &&
            scanned.games.front().installation_path == std::filesystem::canonical(first_game).string(),
            "Scan did not use stable metadata IDs, localized titles and canonical installation paths");
        const auto partial = framerift::scan_game_folders({(directory / "unavailable").string(), first_folder.string()});
        require(partial.games.size() == 2 && partial.warnings.size() == 1,
            "An unavailable game folder prevented valid folders from being scanned");
        const auto scanned_path = directory / "scanned.sqlite";
        {
            framerift::Library library(scanned_path.string());
            library.set_game_folders(folders);
            require(library.game_folders().size() == 3 && library.game_folders().front() == first_folder.string(),
                "Game folder configuration was not canonicalized and deduplicated");
            require_rejected([&] { library.set_game_folders({""}); });
            require(library.game_folders().size() == 3, "Invalid folder configuration changed saved folders");
            library.cache_demo(previews);
            library.cache_scanned(scanned.games);
            require(library.games().size() == 4, "Offline scanning removed previews or omitted installed games");
            execute(scanned_path, "UPDATE games SET last_played = 100, cover_path = 'cached.png', "
                "steamgriddb_id = 123 WHERE id = 'PPSA01288'");
            library.cache_scanned(partial.games);
            const auto games = library.games();
            require(games.front().id == "PPSA01288" && games.front().last_played == 100 &&
                games.front().cover_path == "cached.png" && games.front().steamgriddb_id == 123,
                "A scan overwrote play history or cached artwork");
            library.cache_scanned({});
            require(library.games().size() == 4, "An empty scan erased cache records");
            require_rejected([&] { library.cache_scanned({scanned.games.front(), scanned.games.front()}); });
            require(library.games().size() == 4, "A rejected scan changed cached records");
            require_rejected([&] { library.mark_played(previews.front().id); });
            require_rejected([&] { library.mark_played("PPSA99999"); });
            require(library.games().front().last_played == 100, "A rejected launch changed play history");

            auto artwork = scanned.games.front();
            artwork.title = "Provider title must not replace metadata";
            artwork.cover_path = "api-cover.png";
            artwork.hero_path = "api-hero.png";
            artwork.steamgriddb_id = 456;
            artwork.source_url = "https://www.steamgriddb.com/game/456";
            auto stale = artwork;
            stale.installation_path = (directory / "old installation").string();
            library.cache_artwork({stale});
            require(library.games().size() == 4 && library.games().front().cover_path == "cached.png",
                "Stale artwork changed an installed game or erased previews");
            library.cache_artwork({artwork});
            const auto updated = library.games();
            require(updated.size() == 2 && updated.front().title == "Metadata title" &&
                updated.front().cover_path == "api-cover.png" && updated.front().hero_path == "api-hero.png" &&
                updated.front().last_played == 100 && updated.front().steamgriddb_id == 456,
                "API artwork did not replace previews while preserving metadata and play history");
            library.cache_scanned(partial.games);
            require(library.games().front().cover_path == "api-cover.png", "A later scan replaced downloaded covers");
            const auto before_launch = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            library.mark_played("PPSA01880");
            const auto played = library.games();
            require(played.front().id == "PPSA01880" && played.front().last_played >= before_launch &&
                played.front().title == "Default title" && played.back().last_played == 100 &&
                played.back().cover_path == "api-cover.png", "Play time did not update recent ordering or changed other fields");
        }
        {
            framerift::Library library(scanned_path.string());
            require(library.game_folders().size() == 3 && library.games().size() == 2,
                "Installed games or configured folders disappeared on restart");
        }

        const auto previous_path = directory / "version-two.sqlite";
        execute(previous_path,
            "CREATE TABLE games (id TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL, subtitle TEXT NOT NULL,"
            "genre TEXT NOT NULL, cover_path TEXT NOT NULL, hero_path TEXT NOT NULL, landscape_path TEXT NOT NULL,"
            "last_played INTEGER NOT NULL DEFAULT 0, is_demo INTEGER NOT NULL DEFAULT 0,"
            "steamgriddb_id INTEGER NOT NULL DEFAULT 0, logo_path TEXT NOT NULL DEFAULT '', source_url TEXT NOT NULL DEFAULT '');"
            "CREATE TABLE cache_metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
            "INSERT INTO games VALUES ('PPSA01288', 'Existing game', '', 'Game', 'cover.png', 'hero.png', 'wide.png', 99, 0, 123, 'logo.png', 'source');"
            "PRAGMA user_version = 2;");
        {
            framerift::Library library(previous_path.string());
            const auto games = library.games();
            require(games.size() == 1 && games.front().last_played == 99 && games.front().cover_path == "cover.png" &&
                games.front().steamgriddb_id == 123 && games.front().installation_path.empty() && library.game_folders().empty(),
                "Version two migration changed cached records");
        }
        require(execute(previous_path, "PRAGMA user_version") == 3, "Version two cache was not migrated");

        const auto legacy = directory / "legacy-cache";
        const auto unified = directory / "unified-data";
        std::filesystem::create_directories(legacy / "artwork/PPSA01288");
        std::filesystem::create_directories(legacy / "games/PPSA01288/shader_cache");
        std::filesystem::create_directories(unified / "saves/PPSA01288");
        std::ofstream(legacy / "artwork/PPSA01288/cover.png") << "cover";
        std::ofstream(legacy / "games/PPSA01288/game.elf") << "generated";
        std::ofstream(legacy / "games/PPSA01288/shader_cache/cache.bin") << "shader";
        std::ofstream(unified / "saves/PPSA01288/slot.bin") << "save";
        {
            framerift::Library library((legacy / "games.sqlite3").string());
            std::vector<framerift::Game> records{preview(123, "Existing title", 99)};
            records.front().cover_path = (legacy / "artwork/PPSA01288/cover.png").string();
            library.cache_demo(records);
            library.set_launch_settings({"/runtime"});
        }
        framerift::migrate_library_storage(legacy, unified);
        framerift::migrate_library_storage(legacy, unified);
        require(!std::filesystem::exists(legacy) &&
            std::filesystem::exists(unified / "compiled-games/PPSA01288/game.elf") &&
            std::filesystem::exists(unified / "compiled-games/PPSA01288/shader_cache/cache.bin") &&
            std::filesystem::exists(unified / "saves/PPSA01288/slot.bin"), "Unified storage migration lost files or was not repeatable");
        {
            framerift::Library library((unified / "library/games.sqlite3").string());
            const auto records = library.games();
            require(records.front().cover_path == (unified / "artwork/PPSA01288/cover.png").string() &&
                library.launch_settings().anyps5_directory == "/runtime", "Storage migration lost settings or artwork references");
        }
        std::filesystem::create_directories(legacy / "games");
        std::ofstream(legacy / "games/preserved.bin") << "conflict";
        bool conflict = false;
        try { framerift::migrate_library_storage(legacy, unified); }
        catch (const std::runtime_error&) { conflict = true; }
        require(conflict && std::filesystem::exists(legacy / "games/preserved.bin") &&
            std::filesystem::exists(unified / "compiled-games/PPSA01288/game.elf"), "Migration conflict overwrote existing data");

        std::filesystem::remove_all(directory);
        std::cout << "Library cache check passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(directory);
        std::cerr << "Library cache check failed: " << error.what() << '\n';
        return 1;
    }
}
