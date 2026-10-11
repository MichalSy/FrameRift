// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace framerift {

struct BridgeStatus {
    bool connected = false;
    std::uint16_t port = 0;
    std::string session_id;
    std::string game_id;
    std::string error;
    bool has_frame = false;
    std::uint64_t frame = 0;
    std::uint64_t received_events = 0;
    std::uint64_t missing_events = 0;
    std::uint64_t malformed_packets = 0;
};

struct MonitoringSample {
    std::uint64_t frame = 0;
    std::uint64_t output_handle = 0;
    std::uint64_t output_generation = 0;
    std::uint64_t completed_flip_count = 0;
    std::uint64_t timestamp_us = 0;
    std::optional<std::uint64_t> flip_interval_us;
    std::uint64_t monitoring_epoch = 0;
    std::uint64_t dropped_samples = 0;
    std::uint64_t missing_before = 0;
};

struct MonitoringStats {
    std::size_t sample_count = 0;
    std::uint64_t observed_samples = 0;
    std::uint64_t expected_samples = 0;
    std::uint64_t missing_samples = 0;
    std::uint64_t sender_dropped_samples = 0;
    std::uint64_t packet_gaps = 0;
    std::uint64_t window_start_us = 0;
    std::uint64_t window_end_us = 0;
    bool complete = false;
    std::optional<double> coverage;
    std::optional<double> estimated_fps;
    std::optional<double> mean_interval_us;
    std::optional<double> p95_interval_us;
    std::optional<double> worst_interval_us;
};

struct MonitoringSnapshot {
    bool connected = false;
    bool supported = false;
    bool enabled = false;
    bool pending = false;
    bool has_frames = false;
    bool stale = false;
    bool history_truncated = false;
    std::optional<bool> requested_enabled;
    std::string command_id;
    std::string command_status;
    std::string error;
    std::string state;
    std::string session_id;
    std::string game_id;
    std::uint64_t monitoring_epoch = 0;
    std::uint64_t output_handle = 0;
    std::uint64_t output_generation = 0;
    std::optional<double> age_ms;
    MonitoringStats stats;
    std::vector<MonitoringSample> samples;
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
    MonitoringSnapshot monitoring_snapshot() const;
    std::string monitoring_snapshot_json(bool include_history = false) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace framerift
