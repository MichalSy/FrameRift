// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace framerift {

struct BridgeStatus {
    bool connected = false;
    std::uint16_t port = 0;
    std::string session_id;
    std::string game_id;
    std::string error;
    std::uint64_t frame = 0;
    std::uint64_t received_events = 0;
    std::uint64_t missing_events = 0;
    std::uint64_t malformed_packets = 0;
};

// Owns a loopback UDP socket and its worker. Public methods are thread-safe.
class BridgeServer {
public:
    explicit BridgeServer(std::uint16_t port = 0);
    ~BridgeServer();
    BridgeServer(const BridgeServer&) = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    std::uint16_t port() const;
    void expect_session(const std::string& session_id, const std::string& game_id);
    void end_session(const std::string& session_id);
    BridgeStatus status() const;
    std::string status_json() const;
    std::string events_json(std::uint64_t after = 0, std::size_t limit = 100) const;
    std::string send_command(const std::string& name, const std::string& arguments_json);
    std::string command_json(const std::string& command_id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace framerift
