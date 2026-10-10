// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <stdexcept>
#include <unordered_set>

namespace framerift {
namespace {

constexpr const char* games_query =
    "SELECT id, title, subtitle, genre, cover_path, hero_path, landscape_path, last_played, is_demo, "
    "steamgriddb_id, logo_path, source_url "
    "FROM games ORDER BY last_played DESC, title COLLATE NOCASE, id";

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

void validate_schema(sqlite3* database, bool legacy = false) {
    Statement games(database, legacy ?
        "SELECT id, title, subtitle, genre, cover_path, hero_path, landscape_path, last_played, is_demo FROM games LIMIT 0" :
        games_query);
    Statement metadata(database, "SELECT key, value FROM cache_metadata LIMIT 0");
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
        if (schema_version < 0 || schema_version > 2) {
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
                "source_url TEXT NOT NULL DEFAULT '');"
                "CREATE TABLE cache_metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);");
            validate_schema(database_);
            execute(database_, "PRAGMA user_version = 2");
            transaction.commit();
        } else if (schema_version == 1) {
            validate_schema(database_, true);
            Transaction transaction(database_);
            execute(database_,
                "ALTER TABLE games ADD COLUMN steamgriddb_id INTEGER NOT NULL DEFAULT 0;"
                "ALTER TABLE games ADD COLUMN logo_path TEXT NOT NULL DEFAULT '';"
                "ALTER TABLE games ADD COLUMN source_url TEXT NOT NULL DEFAULT '';"
                "DELETE FROM games WHERE is_demo = 1 AND id IN "
                "('demo-astro', 'demo-rally', 'demo-skybound', 'demo-puzzle', 'demo-neon', 'demo-orbit');"
                "DELETE FROM cache_metadata WHERE key = 'demo_seeded';"
                "PRAGMA user_version = 2;");
            validate_schema(database_);
            transaction.commit();
        } else {
            validate_schema(database_);
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
            query.integer(9), query.text(10), query.text(11)});
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

} // namespace framerift
