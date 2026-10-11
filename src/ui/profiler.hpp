// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "runtime/launch.hpp"
#include "runtime/bridge.hpp"

#include <gtk/gtk.h>

#include <filesystem>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace framerift {

class ProfilerView {
public:
    explicit ProfilerView(std::function<void(bool)> request_monitoring = {});
    ~ProfilerView();

    ProfilerView(const ProfilerView&) = delete;
    ProfilerView& operator=(const ProfilerView&) = delete;

    GtkWidget* widget() const { return root_; }
    void preparing(const std::string& title, bool rebuilding = false);
    void rebuilt(const PreparedGame& game);
    void running(const PreparedGame& game);
    void output(std::string_view text);
    void finished(const std::string& status);
    void failed(const std::string& message, const std::filesystem::path& conversion_log = {});
    void bridge_status(const BridgeStatus& status, const std::string& mcp_endpoint, const std::string& error);
    void monitoring(const MonitoringSnapshot& snapshot);

private:
    void show_conversion_log(const std::filesystem::path& path);
    void update_log_action();
    void copy_selection();
    void scroll_to_end();
    void open_log();
    void update_frame_detail();
    void update_resources();
    void inspect_chart(double x, double y);
    void draw_chart(cairo_t* context, int width, int height) noexcept;

    GtkWidget* root_ = nullptr;
    GtkWidget* title_ = nullptr;
    GtkWidget* status_ = nullptr;
    GtkWidget* bridge_connection_ = nullptr;
    GtkWidget* bridge_endpoint_ = nullptr;
    GtkWidget* mcp_endpoint_ = nullptr;
    GtkWidget* bridge_activity_ = nullptr;
    GtkWidget* monitoring_toggle_ = nullptr;
    GtkWidget* monitoring_state_ = nullptr;
    GtkWidget* window_state_ = nullptr;
    GtkWidget* coverage_ = nullptr;
    GtkWidget* freshness_ = nullptr;
    std::array<GtkWidget*, 4> metrics_{};
    GtkWidget* frame_selector_ = nullptr;
    GtkWidget* follow_live_ = nullptr;
    GtkWidget* frame_identity_ = nullptr;
    GtkWidget* frame_interval_ = nullptr;
    GtkWidget* chart_ = nullptr;
    GtkWidget* chart_mode_toggle_ = nullptr;
    GtkWidget* chart_header_ = nullptr;
    GtkGesture* chart_click_ = nullptr;
    GtkWidget* resource_panel_ = nullptr;
    GtkWidget* resource_state_ = nullptr;
    GtkWidget* resource_scope_ = nullptr;
    std::array<GtkWidget*, 3> shader_values_{};
    std::array<std::array<GtkWidget*, 3>, 4> activity_values_{};
    GtkWidget* queues_ = nullptr;
    GtkWidget* consumer_frame_ = nullptr;
    GtkWidget* memory_panel_ = nullptr;
    GtkWidget* memory_scope_ = nullptr;
    GtkWidget* stall_state_ = nullptr;
    bool chart_fps_ = true;
    bool updating_follow_ = false;
    std::array<GtkWidget*, 4> memory_values_{};
    std::array<GtkWidget*, 3> memory_gauges_{};
    GtkWidget* empty_ = nullptr;
    GtkWidget* view_ = nullptr;
    GtkWidget* follow_ = nullptr;
    GtkWidget* copy_ = nullptr;
    GtkWidget* open_ = nullptr;
    GtkEventController* keys_ = nullptr;
    GtkAdjustment* adjustment_ = nullptr;
    std::filesystem::path log_path_;
    bool has_output_ = false;
    bool previous_carriage_return_ = false;
    bool updating_monitoring_ = false;
    MonitoringSnapshot monitoring_;
    std::optional<MonitoringSample> inspected_;
    std::function<void(bool)> request_monitoring_;
};

} // namespace framerift
