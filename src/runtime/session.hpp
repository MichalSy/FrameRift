// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "bridge.hpp"
#include "launch.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace framerift {

struct GameSessionStatus {
    std::string state = "idle";
    std::string game_id;
    std::string title;
    std::string session_id;
    std::string error;
    bool rebuilding = false;
    PreparedGame prepared;
    int exit_code = -1;
    int term_signal = 0;

    bool busy() const;
};

// Public methods and callbacks run on the application's main context.
class GameSession {
public:
    GameSession(Library& library, std::filesystem::path cache_root, std::filesystem::path data_root,
        GApplication* application, BridgeServer* bridge,
        std::function<void(const GameSessionStatus&)> changed, GameOutput output);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    void start(const std::string& game_id, bool rebuild = false);
    void stop(const std::string& game_id);
    void set_bridge(BridgeServer* bridge);
    const GameSessionStatus& status() const;
    std::string games_json() const;
    std::string status_json() const;
    std::string log_json(std::size_t lines = 5) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace framerift
