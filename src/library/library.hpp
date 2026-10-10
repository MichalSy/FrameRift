// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace framerift {

struct Game {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string genre;
    std::string cover_path;
    std::string hero_path;
    std::string landscape_path;
    std::int64_t last_played = 0;
    bool is_demo = true;
    std::int64_t steamgriddb_id = 0;
    std::string logo_path;
    std::string source_url;
};

class Library {
public:
    explicit Library(const std::string& database_path);
    ~Library();

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    std::vector<Game> games() const;
    void cache_demo(const std::vector<Game>& games);

private:
    sqlite3* database_ = nullptr;
};

} // namespace framerift
