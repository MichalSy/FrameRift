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

std::vector<Game> download_demo_games(const std::string& api_key,
    const std::filesystem::path& artwork_directory);

} // namespace framerift
