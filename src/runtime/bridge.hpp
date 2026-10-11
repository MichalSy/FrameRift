// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <array>
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

// Completed flips in the preceding source second; unavailable without a reliable baseline.
std::optional<double> monitoring_fps_at(const std::vector<MonitoringSample>& samples, std::size_t index);

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

struct ResourceShaderState {
    std::uint64_t translations_active = 0;
    std::uint64_t translations_completed = 0;
    std::uint64_t cached_variants_count = 0;
    std::uint64_t source_code_bytes = 0;
    std::uint64_t variant_spirv_bytes = 0;
    std::uint64_t disk_cache_hits = 0;
    std::uint64_t disk_cache_misses = 0;
    std::uint64_t disk_cache_writes = 0;
    std::uint64_t disk_cache_load_failures = 0;
    std::uint64_t disk_cache_write_failures = 0;
    std::uint64_t preparation_queue_pending = 0;
    std::uint64_t preparation_active = 0;
    std::uint64_t pipeline_builds_active = 0;
    std::uint64_t pipeline_builds_completed = 0;
    std::uint64_t pipeline_optimization_queue_pending = 0;
    std::uint64_t pipeline_optimizations_active = 0;
};

struct ResourceMemoryState {
    std::optional<std::uint64_t> process_rss_bytes;
    std::optional<std::uint64_t> process_rss_timestamp_us;
    std::optional<std::uint64_t> sampled_cache_bytes;
    std::optional<std::uint64_t> sampled_cache_budget_bytes;
    std::optional<std::uint64_t> storage_cache_bytes;
    std::optional<std::uint64_t> storage_cache_budget_bytes;
    std::optional<std::uint64_t> gpu_heap_usage_bytes;
    std::optional<std::uint64_t> gpu_heap_budget_bytes;
    std::optional<std::uint64_t> gpu_heap_timestamp_us;
};

inline constexpr std::array<const char*, 11> resource_work_names = {
    "preparation", "translation", "specialization", "pipeline_build", "pipeline_optimization",
    "shader_module", "gpu_present_wait", "gpu_acquire_wait", "gpu_idle_wait", "gpu_graphics_wait", "gpu_submit"
};

struct ResourceWorkState {
    std::uint64_t active = 0;
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t total_us = 0;
    std::uint64_t max_us = 0;
};

using ResourceActivity = std::array<ResourceWorkState, resource_work_names.size()>;

struct ResourceSample {
    std::uint64_t timestamp_us = 0;
    std::uint64_t monitoring_epoch = 0;
    ResourceShaderState shaders;
    ResourceMemoryState memory;
    std::optional<ResourceActivity> activity;
};

struct ResourceSnapshot {
    bool activity_supported = false;
    std::uint64_t work_window_us = 0;
    std::optional<ResourceActivity> work_delta;
    bool supported = false;
    bool has_sample = false;
    bool stale = false;
    std::string state;
    std::optional<double> age_ms;
    std::optional<double> process_rss_age_ms;
    std::optional<double> gpu_heap_age_ms;
    std::optional<ResourceSample> sample;
};

struct MonitoringSnapshot {
    bool connected = false;
    bool supported = false;
    bool enabled = false;
    bool pending = false;
    bool has_frames = false;
    bool stale = false;
    bool history_truncated = false;
    std::string stall_evidence;
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
    ResourceSnapshot resources;
};

// Owns a loopback UDP socket and its worker. Public methods are thread-safe.
class BridgeServer {
public:
    explicit BridgeServer(std::uint16_t port = 0);
    ~BridgeServer();
    BridgeServer(const BridgeServer&) = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    std::uint16_t port() const;
    void expect_session(const std::string& session_id, const std::string& game_id, bool enable_monitoring = false);
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
