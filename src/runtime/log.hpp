// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "launch.hpp"

#include <array>
#include <fstream>

namespace framerift {

class GameLog {
public:
    GameLog(const std::filesystem::path& path, GameOutput output, GameFinished finished);
    ~GameLog();
    void start(GSubprocess* process) noexcept;

private:
    void read_next() noexcept;
    void read_ready(GInputStream* stream, GAsyncResult* result) noexcept;
    void wait_ready(GAsyncResult* result) noexcept;
    void fail(const char* message) noexcept;
    void complete() noexcept;

    GSubprocess* process_ = nullptr;
    std::ofstream file_;
    GameOutput output_;
    GameFinished finished_;
    std::array<char, 64 * 1024> buffer_{};
    std::string error_;
    std::string fallback_error_ = "Game output capture failed";
    bool failed_ = false;
    bool output_done_ = false;
    bool wait_done_ = false;
};

} // namespace framerift
