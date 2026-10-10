// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "library.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace framerift {

// Call from a worker: keyring operations have a ten-second cancellation deadline.
// A nonempty STEAMGRIDDB_API_KEY overrides the saved key without changing it.
std::string read_steamgrid_api_key();
void save_steamgrid_api_key(const std::string& api_key);

std::vector<Game> download_demo_games(const std::string& api_key,
    const std::filesystem::path& artwork_directory);

// Explicit hero refresh prefers animated WebP while preserving existing artwork on failure.
std::vector<Game> download_game_artwork(const std::vector<Game>& games, const std::string& api_key,
    const std::filesystem::path& artwork_directory, std::vector<std::string>& warnings,
    bool refresh_heroes = false);

} // namespace framerift
