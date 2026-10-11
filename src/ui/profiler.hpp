// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include "runtime/launch.hpp"
#include "runtime/bridge.hpp"

#include <gtk/gtk.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace framerift {

class ProfilerView {
public:
    ProfilerView();
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

private:
    void show_conversion_log(const std::filesystem::path& path);
    void update_log_action();
    void copy_selection();
    void scroll_to_end();
    void open_log();

    GtkWidget* root_ = nullptr;
    GtkWidget* title_ = nullptr;
    GtkWidget* status_ = nullptr;
    GtkWidget* bridge_connection_ = nullptr;
    GtkWidget* bridge_endpoint_ = nullptr;
    GtkWidget* mcp_endpoint_ = nullptr;
    GtkWidget* bridge_activity_ = nullptr;
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
};

} // namespace framerift
