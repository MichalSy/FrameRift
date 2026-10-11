// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "library/library.hpp"

#include <gio/gio.h>

#include <filesystem>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace framerift {

class ConversionError : public std::runtime_error {
public:
    ConversionError(const std::string& message, const std::filesystem::path& path)
        : std::runtime_error(message), log_path(path) {}

    std::filesystem::path log_path;
};

struct PreparedGame {
    std::string game_id;
    std::vector<std::string> command;
    std::filesystem::path code_directory;
    std::filesystem::path working_directory;
    std::filesystem::path log_path;
    std::filesystem::path conversion_log_path;
};

PreparedGame prepare_game(const Game& game, const LaunchSettings& settings,
    const std::filesystem::path& cache_root, const std::filesystem::path& data_root, bool rebuild = false,
    GCancellable* cancellable = nullptr);
using GameOutput = std::function<void(std::string_view)>;
using GameFinished = std::function<void(GSubprocess*, const std::string&)>;
struct BridgeLaunch {
    std::uint16_t port = 0;
    std::string session_id;
};
// The optional managed handle receives its own reference; the caller must unref it.
void launch_game(const PreparedGame& game, GameOutput output, GameFinished finished,
    const BridgeLaunch& bridge = {}, GSubprocess** managed_process = nullptr);

} // namespace framerift
