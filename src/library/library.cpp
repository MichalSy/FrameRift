// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library.hpp"

#include <sqlite3.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace framerift {
namespace {

constexpr const char* games_query =
    "SELECT id, title, subtitle, genre, cover_path, hero_path, landscape_path, last_played, is_demo, "
    "steamgriddb_id, logo_path, source_url, installation_path "
    "FROM games ORDER BY last_played DESC, title COLLATE NOCASE, id";

struct LaunchField {
    const char* key;
    std::string LaunchSettings::* path;
};

constexpr std::array launch_fields{
    LaunchField{"launch.anyps5_directory", &LaunchSettings::anyps5_directory},
    LaunchField{"launch.runtime_directory", &LaunchSettings::runtime_directory},
    LaunchField{"launch.relinker_path", &LaunchSettings::relinker_path},
};

void execute(sqlite3* database, const char* sql) {
    if (sqlite3_exec(database, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("Library database: ") + sqlite3_errmsg(database));
    }
}

class Statement {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        if (sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("Library database: ") + sqlite3_errmsg(database));
        }
    }

    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(int index, const std::string& value) {
        check(sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT));
    }

    void bind(int index, std::int64_t value) {
        check(sqlite3_bind_int64(statement_, index, value));
    }

    bool next() {
        const int result = sqlite3_step(statement_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        check(result);
        return false;
    }

    std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }

    void reset() {
        check(sqlite3_reset(statement_));
        check(sqlite3_clear_bindings(statement_));
    }

private:
    void check(int result) const {
        if (result != SQLITE_OK) {
            throw std::runtime_error(std::string("Library database: ") + sqlite3_errmsg(database_));
        }
    }

    sqlite3* database_;
    sqlite3_stmt* statement_ = nullptr;
};

class Transaction {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (!committed_) sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        execute(database_, "COMMIT");
        committed_ = true;
    }

private:
    sqlite3* database_;
    bool committed_ = false;
};

void validate_schema(sqlite3* database, std::int64_t version) {
    Statement games(database,
        "SELECT id, title, subtitle, genre, cover_path, hero_path, landscape_path, last_played, is_demo FROM games LIMIT 0");
    Statement metadata(database, "SELECT key, value FROM cache_metadata LIMIT 0");
    if (version >= 2) {
        Statement artwork(database, "SELECT steamgriddb_id, logo_path, source_url FROM games LIMIT 0");
    }
    if (version >= 3) {
        Statement installation(database, "SELECT installation_path FROM games LIMIT 0");
        Statement folders(database, "SELECT path, position FROM game_folders LIMIT 0");
    }
}

bool safe_game_id(const std::string& id) {
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_';
    });
}

std::vector<std::string> normalized_folders(const std::vector<std::string>& folders) {
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    for (const auto& folder : folders) {
        if (folder.empty() || folder.find('\0') != std::string::npos) {
            throw std::runtime_error("A game folder must contain a valid path");
        }
        const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(folder)).string();
        if (seen.insert(path).second) result.push_back(path);
    }
    return result;
}

JsonNode* member(JsonObject* object, const char* name) {
    return json_object_has_member(object, name) ? json_object_get_member(object, name) : nullptr;
}

JsonObject* object_member(JsonObject* object, const char* name) {
    auto* node = member(object, name);
    return node && JSON_NODE_HOLDS_OBJECT(node) ? json_node_get_object(node) : nullptr;
}

std::string text_member(JsonObject* object, const char* name) {
    auto* node = member(object, name);
    if (!node || !JSON_NODE_HOLDS_VALUE(node) || json_node_get_value_type(node) != G_TYPE_STRING) return {};
    const std::string value = json_node_get_string(node);
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}

std::string metadata_title(JsonObject* metadata) {
    auto* localized = object_member(metadata, "localizedParameters");
    if (localized) {
        const auto localized_title = [&](const std::string& language) {
            auto* entry = object_member(localized, language.c_str());
            return entry ? text_member(entry, "titleName") : std::string{};
        };
        for (const char* language : {"en-US", "en-GB", "en"}) {
            const auto title = localized_title(language);
            if (!title.empty()) return title;
        }
        const auto title = localized_title(text_member(localized, "defaultLanguage"));
        if (!title.empty()) return title;
        std::vector<std::string> languages;
        const std::unique_ptr<GList, decltype(&g_list_free)> members(json_object_get_members(localized), g_list_free);
        for (auto* entry = members.get(); entry; entry = entry->next) {
            languages.emplace_back(static_cast<const char*>(entry->data));
        }
        std::sort(languages.begin(), languages.end());
        for (const auto& language : languages) {
            const auto fallback = localized_title(language);
            if (!fallback.empty()) return fallback;
        }
    }
    return text_member(metadata, "titleName");
}

Game read_game(const std::filesystem::path& directory) {
    const auto path = directory / "sce_sys/param.json";
    constexpr std::size_t maximum_metadata = 1024 * 1024;
    const auto size = std::filesystem::file_size(path);
    if (size == 0 || size > maximum_metadata) throw std::runtime_error("Invalid game metadata size");
    std::string body(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    if (!input.read(body.data(), static_cast<std::streamsize>(size))) {
        throw std::runtime_error("Could not read game metadata");
    }
    const std::unique_ptr<JsonParser, decltype(&g_object_unref)> parser(json_parser_new(), g_object_unref);
    if (!json_parser_load_from_data(parser.get(), body.data(), static_cast<gssize>(body.size()), nullptr)) {
        throw std::runtime_error("Invalid game metadata JSON");
    }
    auto* root = json_parser_get_root(parser.get());
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) throw std::runtime_error("Invalid game metadata object");
    auto* metadata = json_node_get_object(root);
    Game game;
    game.id = text_member(metadata, "titleId");
    game.title = metadata_title(metadata);
    if (!safe_game_id(game.id) || game.title.empty()) {
        throw std::runtime_error("Game metadata requires a valid title ID and game title");
    }
    game.subtitle = "PlayStation 5";
    game.genre = "Game";
    game.is_demo = false;
    game.installation_path = std::filesystem::canonical(directory).string();
    return game;
}

} // namespace

Library::Library(const std::string& database_path) {
    const auto parent = std::filesystem::path(database_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    const int result = sqlite3_open_v2(database_path.c_str(), &database_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "could not open database";
        sqlite3_close(database_);
        database_ = nullptr;
        throw std::runtime_error("Library database: " + message);
    }

    try {
        if (sqlite3_busy_timeout(database_, 3000) != SQLITE_OK) {
            throw std::runtime_error(std::string("Library database: ") + sqlite3_errmsg(database_));
        }
        std::int64_t schema_version = 0;
        {
            Statement version(database_, "PRAGMA user_version");
            version.next();
            schema_version = version.integer(0);
        }
        if (schema_version < 0 || schema_version > 3) {
            throw std::runtime_error("Library database has an unsupported FrameRift schema version");
        }
        if (schema_version == 0) {
            Transaction transaction(database_);
            Statement unrelated(database_,
                "SELECT EXISTS(SELECT 1 FROM sqlite_schema WHERE name NOT GLOB 'sqlite_*')");
            unrelated.next();
            if (unrelated.integer(0) != 0) {
                throw std::runtime_error("Library database does not contain a FrameRift cache");
            }
            execute(database_,
                "CREATE TABLE games ("
                "id TEXT PRIMARY KEY NOT NULL, title TEXT NOT NULL, subtitle TEXT NOT NULL,"
                "genre TEXT NOT NULL, cover_path TEXT NOT NULL, hero_path TEXT NOT NULL, landscape_path TEXT NOT NULL,"
                "last_played INTEGER NOT NULL DEFAULT 0,"
                "is_demo INTEGER NOT NULL DEFAULT 0 CHECK (is_demo IN (0, 1)),"
                "steamgriddb_id INTEGER NOT NULL DEFAULT 0, logo_path TEXT NOT NULL DEFAULT '',"
                "source_url TEXT NOT NULL DEFAULT '', installation_path TEXT NOT NULL DEFAULT '');"
                "CREATE TABLE cache_metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);"
                "CREATE TABLE game_folders (path TEXT PRIMARY KEY NOT NULL, position INTEGER NOT NULL);");
            validate_schema(database_, 3);
            execute(database_, "PRAGMA user_version = 3");
            transaction.commit();
        } else if (schema_version < 3) {
            validate_schema(database_, schema_version);
            Transaction transaction(database_);
            if (schema_version == 1) {
                execute(database_,
                    "ALTER TABLE games ADD COLUMN steamgriddb_id INTEGER NOT NULL DEFAULT 0;"
                    "ALTER TABLE games ADD COLUMN logo_path TEXT NOT NULL DEFAULT '';"
                    "ALTER TABLE games ADD COLUMN source_url TEXT NOT NULL DEFAULT '';"
                    "DELETE FROM games WHERE is_demo = 1 AND id IN "
                    "('demo-astro', 'demo-rally', 'demo-skybound', 'demo-puzzle', 'demo-neon', 'demo-orbit');"
                    "DELETE FROM cache_metadata WHERE key = 'demo_seeded';");
            }
            execute(database_,
                "ALTER TABLE games ADD COLUMN installation_path TEXT NOT NULL DEFAULT '';"
                "CREATE TABLE game_folders (path TEXT PRIMARY KEY NOT NULL, position INTEGER NOT NULL);"
                "PRAGMA user_version = 3;");
            validate_schema(database_, 3);
            transaction.commit();
        } else {
            validate_schema(database_, 3);
        }
    } catch (...) {
        sqlite3_close(database_);
        database_ = nullptr;
        throw;
    }
}

Library::~Library() { sqlite3_close(database_); }

std::vector<Game> Library::games() const {
    Statement query(database_, games_query);
    std::vector<Game> result;
    while (query.next()) {
        result.push_back(Game{query.text(0), query.text(1), query.text(2), query.text(3),
            query.text(4), query.text(5), query.text(6), query.integer(7), query.integer(8) != 0,
            query.integer(9), query.text(10), query.text(11), query.text(12)});
    }
    return result;
}

void Library::cache_demo(const std::vector<Game>& games) {
    std::unordered_set<std::string> ids;
    for (const auto& game : games) {
        if (game.id.empty() || game.title.empty() || game.cover_path.empty() || game.hero_path.empty() ||
            game.landscape_path.empty() || game.steamgriddb_id <= 0 || !game.is_demo) {
            throw std::runtime_error("Library cache requires preview games with SteamGridDB IDs, titles and artwork");
        }
        if (!ids.insert(game.id).second) {
            throw std::runtime_error("Library cache contains duplicate preview game IDs");
        }
    }

    Transaction transaction(database_);
    execute(database_, "DELETE FROM games WHERE is_demo = 1");
    Statement insert(database_,
        "INSERT INTO games (id, title, subtitle, genre, cover_path, hero_path, landscape_path, last_played, is_demo, "
        "steamgriddb_id, logo_path, source_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?, ?)");
    for (const auto& game : games) {
        insert.bind(1, game.id);
        insert.bind(2, game.title);
        insert.bind(3, game.subtitle);
        insert.bind(4, game.genre);
        insert.bind(5, game.cover_path);
        insert.bind(6, game.hero_path);
        insert.bind(7, game.landscape_path);
        insert.bind(8, game.last_played);
        insert.bind(9, game.steamgriddb_id);
        insert.bind(10, game.logo_path);
        insert.bind(11, game.source_url);
        insert.next();
        insert.reset();
    }
    transaction.commit();
}

std::vector<std::string> Library::game_folders() const {
    Statement query(database_, "SELECT path FROM game_folders ORDER BY position, path");
    std::vector<std::string> result;
    while (query.next()) result.push_back(query.text(0));
    return result;
}

void Library::set_game_folders(const std::vector<std::string>& folders) {
    const auto normalized = normalized_folders(folders);
    Transaction transaction(database_);
    execute(database_, "DELETE FROM game_folders");
    Statement insert(database_, "INSERT INTO game_folders (path, position) VALUES (?, ?)");
    for (std::size_t index = 0; index < normalized.size(); ++index) {
        insert.bind(1, normalized[index]);
        insert.bind(2, static_cast<std::int64_t>(index));
        insert.next();
        insert.reset();
    }
    transaction.commit();
}

void Library::cache_scanned(const std::vector<Game>& games) {
    std::unordered_set<std::string> ids;
    for (const auto& game : games) {
        if (!safe_game_id(game.id) || game.title.empty() || game.is_demo ||
            game.installation_path.empty() || game.installation_path.find('\0') != std::string::npos ||
            !std::filesystem::path(game.installation_path).is_absolute() || !ids.insert(game.id).second) {
            throw std::runtime_error("Library scan requires unique game IDs, metadata titles and installation paths");
        }
    }
    Transaction transaction(database_);
    Statement insert(database_,
        "INSERT INTO games (id, title, subtitle, genre, cover_path, hero_path, landscape_path, is_demo, installation_path) "
        "VALUES (?, ?, ?, ?, '', '', '', 0, ?) ON CONFLICT(id) DO UPDATE SET "
        "title = excluded.title, subtitle = excluded.subtitle, genre = excluded.genre, "
        "is_demo = 0, installation_path = excluded.installation_path");
    for (const auto& game : games) {
        insert.bind(1, game.id);
        insert.bind(2, game.title);
        insert.bind(3, game.subtitle);
        insert.bind(4, game.genre);
        insert.bind(5, game.installation_path);
        insert.next();
        insert.reset();
    }
    transaction.commit();
}

void Library::cache_artwork(const std::vector<Game>& games) {
    std::unordered_set<std::string> ids;
    for (const auto& game : games) {
        if (!safe_game_id(game.id) || game.cover_path.empty() || game.steamgriddb_id <= 0 || game.is_demo ||
            game.installation_path.empty() || !ids.insert(game.id).second) {
            throw std::runtime_error("Library artwork requires unique installed game IDs and API covers");
        }
    }
    Transaction transaction(database_);
    Statement update(database_,
        "UPDATE games SET cover_path = ?, hero_path = COALESCE(NULLIF(?, ''), hero_path), "
        "landscape_path = COALESCE(NULLIF(?, ''), landscape_path), logo_path = COALESCE(NULLIF(?, ''), logo_path), "
        "steamgriddb_id = ?, source_url = COALESCE(NULLIF(?, ''), source_url) "
        "WHERE id = ? AND is_demo = 0 AND installation_path = ? AND installation_path != ''");
    bool installed_cover = false;
    for (const auto& game : games) {
        update.bind(1, game.cover_path);
        update.bind(2, game.hero_path);
        update.bind(3, game.landscape_path);
        update.bind(4, game.logo_path);
        update.bind(5, game.steamgriddb_id);
        update.bind(6, game.source_url);
        update.bind(7, game.id);
        update.bind(8, game.installation_path);
        update.next();
        installed_cover = installed_cover || sqlite3_changes(database_) > 0;
        update.reset();
    }
    if (installed_cover) execute(database_, "DELETE FROM games WHERE is_demo = 1");
    transaction.commit();
}

LaunchSettings Library::launch_settings() const {
    LaunchSettings result;
    Statement query(database_, "SELECT key, value FROM cache_metadata WHERE key LIKE 'launch.%'");
    while (query.next()) {
        for (const auto& field : launch_fields) {
            if (query.text(0) == field.key) result.*field.path = query.text(1);
        }
    }
    return result;
}

void Library::set_launch_settings(const LaunchSettings& settings) {
    auto normalized = settings;
    for (const auto& field : launch_fields) {
        auto& path = normalized.*field.path;
        if (path.find('\0') != std::string::npos) throw std::runtime_error("Launch settings contain an invalid path");
        if (!path.empty()) path = normalized_folders({path}).front();
    }
    Transaction transaction(database_);
    Statement update(database_,
        "INSERT INTO cache_metadata (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    for (const auto& field : launch_fields) {
        update.bind(1, std::string(field.key));
        update.bind(2, normalized.*field.path);
        update.next();
        update.reset();
    }
    transaction.commit();
}

void Library::mark_played(const std::string& game_id) {
    if (!safe_game_id(game_id)) throw std::runtime_error("Cannot record play time for an invalid game ID");
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    Statement update(database_,
        "UPDATE games SET last_played = ? WHERE id = ? AND is_demo = 0 AND installation_path != ''");
    update.bind(1, now);
    update.bind(2, game_id);
    update.next();
    if (sqlite3_changes(database_) != 1) {
        throw std::runtime_error("Cannot record play time for a game that is not installed in the library");
    }
}

ScanResult scan_game_folders(const std::vector<std::string>& folders) {
    ScanResult result;
    std::vector<std::filesystem::path> roots;
    for (const auto& folder : folders) {
        try {
            const auto normalized = normalized_folders({folder}).front();
            if (!std::filesystem::is_directory(normalized)) throw std::runtime_error("Game folder is unavailable");
            roots.emplace_back(std::filesystem::canonical(normalized));
        } catch (const std::exception& error) {
            result.warnings.push_back(folder + ": " + error.what());
        }
    }
    std::sort(roots.begin(), roots.end());
    std::vector<std::filesystem::path> distinct_roots;
    for (const auto& root : roots) {
        const bool covered = std::any_of(distinct_roots.begin(), distinct_roots.end(), [&](const auto& ancestor) {
            return std::mismatch(ancestor.begin(), ancestor.end(), root.begin(), root.end()).first == ancestor.end();
        });
        if (!covered) distinct_roots.push_back(root);
    }
    std::unordered_set<std::string> directories;
    std::unordered_set<std::string> ids;
    for (const auto& root : distinct_roots) {
        std::vector<std::filesystem::path> pending{root};
        while (!pending.empty()) {
            const auto directory = std::move(pending.back());
            pending.pop_back();
            if (!directories.insert(directory.string()).second) continue;
            try {
                if (std::filesystem::is_regular_file(directory / "eboot.bin")) {
                    auto game = read_game(directory);
                    if (ids.insert(game.id).second) result.games.push_back(std::move(game));
                    continue;
                }
                std::vector<std::filesystem::path> children;
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    if (!entry.is_symlink() && entry.is_directory()) children.push_back(entry.path());
                }
                std::sort(children.rbegin(), children.rend());
                pending.insert(pending.end(), children.begin(), children.end());
            } catch (const std::exception& error) {
                result.warnings.push_back(directory.string() + ": " + error.what());
            }
        }
    }
    std::sort(result.games.begin(), result.games.end(), [](const Game& first, const Game& second) {
        return first.id < second.id;
    });
    return result;
}

} // namespace framerift
