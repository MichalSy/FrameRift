// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "profiler.hpp"
#include "components.hpp"

#include <vte/vte.h>

#include <exception>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace framerift {
namespace {

using ui::label;
using ui::card;

void show_error(GtkWidget* widget, const char* message) noexcept {
    g_printerr("FrameRift: %s\n", message);
    auto* root = gtk_widget_get_root(widget);
    if (GTK_IS_WINDOW(root) && gtk_widget_get_mapped(widget)) {
        auto* dialog = gtk_alert_dialog_new("%s", message);
        gtk_alert_dialog_show(dialog, GTK_WINDOW(root));
        g_object_unref(dialog);
    }
}

std::string number(double value, unsigned precision = 1) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

std::string bytes(long double value) {
    const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    unsigned unit = 0;
    while (value >= 1024 && unit < 6) { value /= 1024; ++unit; }
    return number(static_cast<double>(value), unit ? 2 : 0) + " " + units[unit];
}

void chart_text(cairo_t* context, double x, double y, const std::string& text, bool right = false) {
    cairo_text_extents_t extent;
    cairo_text_extents(context, text.c_str(), &extent);
    cairo_move_to(context, right ? x - extent.width : x, y);
    cairo_show_text(context, text.c_str());
}

constexpr double chart_left = 54;
constexpr double chart_right = 22;
constexpr double chart_header_gap = 16;
constexpr double chart_bottom = 40;

std::optional<double> chart_value(const MonitoringSnapshot& snapshot, std::size_t index, bool fps) {
    if (fps) return monitoring_fps_at(snapshot.samples, index);
    const auto& sample = snapshot.samples[index];
    if (!sample.flip_interval_us) return {};
    return *sample.flip_interval_us / 1000.0;
}

std::uint64_t chart_end(const MonitoringSnapshot& snapshot) {
    if (snapshot.samples.empty()) return 0;
    // Advance the live axis during a stall rather than pinning an old completion to "now".
    const auto age = snapshot.connected && snapshot.enabled && snapshot.age_ms ? *snapshot.age_ms : 0.0;
    return snapshot.samples.back().timestamp_us + static_cast<std::uint64_t>(age * 1000.0);
}

double chart_maximum(const MonitoringSnapshot& snapshot, bool fps) {
    if (fps) return 75.0;
    double maximum = 0;
    const auto end = chart_end(snapshot);
    for (std::size_t i = 0; i < snapshot.samples.size(); ++i) {
        if (end - snapshot.samples[i].timestamp_us > 60000000) continue;
        if (const auto value = chart_value(snapshot, i, fps)) maximum = std::max(maximum, *value);
    }
    return std::max(25.0, std::ceil(maximum / 10.0) * 10.0);
}

} // namespace

ProfilerView::ProfilerView(std::function<void(bool)> request_monitoring)
    : request_monitoring_(std::move(request_monitoring)) {
    root_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    g_object_ref_sink(root_);
    gtk_widget_add_css_class(root_, "profiler-page");
    auto* workspace = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(workspace), false);
    gtk_stack_set_vhomogeneous(GTK_STACK(workspace), false);
    gtk_widget_set_vexpand(workspace, true);
    auto* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(header, "profiler-session");
    title_ = label("Profiler", "profiler-title");
    gtk_label_set_ellipsize(GTK_LABEL(title_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_, true);
    gtk_box_append(GTK_BOX(header), title_);
    status_ = label("No session", "profiler-status");
    gtk_box_append(GTK_BOX(header), status_);
    bridge_connection_ = label("Disconnected", "bridge-connection");
    gtk_box_append(GTK_BOX(header), bridge_connection_);
    const auto tabs = ui::segmented_control("Live", "Logs");
    g_signal_connect_object(tabs.first, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer data) {
        if (gtk_toggle_button_get_active(button)) gtk_stack_set_visible_child_name(GTK_STACK(data), "live");
    }), workspace, G_CONNECT_DEFAULT);
    g_signal_connect_object(tabs.second, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer data) {
        if (gtk_toggle_button_get_active(button)) gtk_stack_set_visible_child_name(GTK_STACK(data), "logs");
    }), workspace, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(header), tabs.root);
    monitoring_toggle_ = gtk_check_button_new_with_label("Monitoring");
    gtk_widget_set_sensitive(monitoring_toggle_, false);
    g_signal_connect(monitoring_toggle_, "toggled", G_CALLBACK(+[](GtkCheckButton* button, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        if (self.updating_monitoring_) return;
        try {
            gtk_widget_set_sensitive(self.monitoring_toggle_, false);
            if (self.request_monitoring_) self.request_monitoring_(gtk_check_button_get_active(button));
        } catch (const std::exception& error) {
            show_error(self.root_, error.what());
            self.monitoring(self.monitoring_);
        } catch (...) {
            show_error(self.root_, "Could not change monitoring");
            self.monitoring(self.monitoring_);
        }
    }), this);
    gtk_box_append(GTK_BOX(header), monitoring_toggle_);
    gtk_box_append(GTK_BOX(header), label("Update 1 s", "monitor-control-label"));
    auto* bridge = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    bridge_activity_ = label("", "bridge-endpoint");
    gtk_label_set_wrap(GTK_LABEL(bridge_activity_), true);
    gtk_box_append(GTK_BOX(bridge), bridge_activity_);
    bridge_endpoint_ = label("", "bridge-endpoint");
    mcp_endpoint_ = label("", "bridge-endpoint");
    monitoring_state_ = label("Monitoring unavailable", "monitoring-state");
    for (auto* endpoint : {bridge_endpoint_, mcp_endpoint_, monitoring_state_}) {
        gtk_label_set_selectable(GTK_LABEL(endpoint), true);
        gtk_box_append(GTK_BOX(bridge), endpoint);
    }
    auto* popover = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(popover), bridge);
    auto* details = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(details), "dialog-information-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(details), popover);
    gtk_widget_set_tooltip_text(details, "Connection and monitoring details");
    gtk_box_append(GTK_BOX(header), details);
    gtk_box_append(GTK_BOX(root_), header);
    gtk_box_append(GTK_BOX(root_), workspace);

    auto* live_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(live_scroll), false);
    auto* live = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(live, "profiler-live");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(live_scroll), live);
    gtk_stack_add_titled(GTK_STACK(workspace), live_scroll, "live", "Live");
    auto* cards = card(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(cards, "metric-strip");
    auto* metric_values = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(metric_values), true);
    gtk_widget_set_hexpand(metric_values, true);
    const char* card_titles[] = {"FPS (estimated)", "Mean flip interval", "P95", "Worst"};
    for (std::size_t i = 0; i < metrics_.size(); ++i) {
        const auto card = ui::metric_card(card_titles[i], i == 0);
        metrics_[i] = card.value;
        gtk_box_append(GTK_BOX(metric_values), card.root);
    }
    gtk_box_append(GTK_BOX(cards), metric_values);
    auto* summary = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(summary, "metric-card");
    window_state_ = label("Last 1 s", "monitor-window");
    gtk_label_set_width_chars(GTK_LABEL(window_state_), 20);
    gtk_label_set_max_width_chars(GTK_LABEL(window_state_), 20);
    gtk_label_set_ellipsize(GTK_LABEL(window_state_), PANGO_ELLIPSIZE_END);
    coverage_ = label("– / – samples · –", "monitor-coverage");
    gtk_label_set_width_chars(GTK_LABEL(coverage_), 20);
    gtk_label_set_max_width_chars(GTK_LABEL(coverage_), 20);
    gtk_label_set_ellipsize(GTK_LABEL(coverage_), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(summary), window_state_);
    gtk_box_append(GTK_BOX(summary), coverage_);
    gtk_box_append(GTK_BOX(cards), summary);
    gtk_box_append(GTK_BOX(live), cards);

    auto* chart_panel = card();
    gtk_widget_add_css_class(chart_panel, "pacing-panel");
    auto* chart_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    auto* chart_title = label("Frame pacing", "monitor-heading");
    gtk_widget_set_hexpand(chart_title, true);
    gtk_box_append(GTK_BOX(chart_header), chart_title);
    frame_identity_ = label("Frame –", "frame-value");
    frame_interval_ = label("Flip interval –", "frame-value");
    for (auto* field : {frame_identity_, frame_interval_}) {
        gtk_label_set_ellipsize(GTK_LABEL(field), PANGO_ELLIPSIZE_END);
        gtk_box_append(GTK_BOX(chart_header), field);
    }
    const char* selections[] = {"Latest received frame", "Worst in last 1 s", nullptr};
    frame_selector_ = gtk_drop_down_new_from_strings(selections);
    g_signal_connect(frame_selector_, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        try { self.inspected_.reset(); self.update_frame_detail(); gtk_widget_queue_draw(self.chart_); }
        catch (const std::exception& error) { show_error(self.root_, error.what()); }
        catch (...) { show_error(self.root_, "Could not select a frame"); }
    }), this);
    gtk_box_append(GTK_BOX(chart_header), frame_selector_);
    const auto modes = ui::segmented_control("FPS", "Flip interval");
    chart_fps_button_ = modes.first;
    gtk_widget_set_tooltip_text(chart_fps_button_, "Switch between FPS and flip intervals");
    g_signal_connect(chart_fps_button_, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        self.chart_fps_ = gtk_toggle_button_get_active(button);
        gtk_widget_queue_draw(self.chart_);
    }), this);
    gtk_box_append(GTK_BOX(chart_header), modes.root);
    follow_live_ = gtk_check_button_new_with_label("Follow live");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(follow_live_), true);
    g_signal_connect(follow_live_, "toggled", G_CALLBACK(+[](GtkCheckButton* button, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        if (self.updating_follow_) return;
        try {
            if (gtk_check_button_get_active(button)) self.inspected_.reset();
            else if (!self.monitoring_.samples.empty()) self.inspected_ = self.monitoring_.samples.back();
            self.update_frame_detail();
            gtk_widget_queue_draw(self.chart_);
        } catch (const std::exception& error) { show_error(self.root_, error.what()); }
        catch (...) { show_error(self.root_, "Could not follow live frames"); }
    }), this);
    gtk_box_append(GTK_BOX(chart_header), follow_live_);
    chart_header_ = chart_header;
    gtk_widget_add_css_class(chart_header, "chart-header-overlay");
    chart_ = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(chart_), 190);
    gtk_widget_set_hexpand(chart_, true);
    gtk_widget_set_tooltip_text(chart_, "FPS counts completed flips in the preceding source second. Click a sample to inspect. Missing samples are not interpolated.");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(chart_), +[](GtkDrawingArea*, cairo_t* context, int width, int height, gpointer data) {
        static_cast<ProfilerView*>(data)->draw_chart(context, width, height);
    }, this, nullptr);
    chart_click_ = gtk_gesture_click_new();
    g_signal_connect(chart_click_, "pressed", G_CALLBACK(+[](GtkGestureClick*, gint, gdouble x, gdouble y, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        try { self.inspect_chart(x, y); }
        catch (const std::exception& error) { show_error(self.root_, error.what()); }
        catch (...) { show_error(self.root_, "Could not inspect a chart sample"); }
    }), this);
    gtk_widget_add_controller(chart_, GTK_EVENT_CONTROLLER(chart_click_));
    gtk_box_append(GTK_BOX(chart_panel), ui::header_overlay(chart_, chart_header));
    gtk_box_append(GTK_BOX(live), chart_panel);

    auto* lower = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(lower), GTK_SELECTION_NONE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(lower), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(lower), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(lower), 16);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(lower), 16);
    gtk_widget_add_css_class(lower, "resource-row");
    auto* consumers = card();
    gtk_widget_add_css_class(consumers, "consumers-panel");
    gtk_widget_set_size_request(consumers, 850, -1);
    gtk_widget_set_hexpand(consumers, true);
    auto* consumer_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(consumer_header), label("Top frame consumers", "monitor-heading"));
    consumer_frame_ = label("Frame –", "consumer-frame");
    gtk_box_append(GTK_BOX(consumer_header), consumer_frame_);
    auto* cpu = gtk_toggle_button_new_with_label("Driver CPU");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cpu), true);
    gtk_widget_set_sensitive(cpu, false);
    auto* gpu = gtk_toggle_button_new_with_label("GPU");
    gtk_widget_set_sensitive(gpu, false);
    gtk_box_append(GTK_BOX(consumer_header), cpu);
    gtk_box_append(GTK_BOX(consumer_header), gpu);
    auto* pending = label("Measurements pending", "consumer-pending");
    gtk_widget_set_hexpand(pending, true);
    gtk_label_set_xalign(GTK_LABEL(pending), 1);
    gtk_widget_set_tooltip_text(pending, "Per-frame phase attribution is not supplied by this runtime. Placeholder rows do not represent measured rankings.");
    gtk_box_append(GTK_BOX(consumer_header), pending);
    gtk_box_append(GTK_BOX(consumers), consumer_header);
    auto* table = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(table), 18);
    gtk_grid_set_row_spacing(GTK_GRID(table), 0);
    const char* headings[] = {"Phase", "", "Elapsed", "Calls", "Share of flip"};
    for (unsigned col = 0; col < 5; ++col) {
        auto* heading = label(headings[col], "consumer-cell");
        gtk_widget_add_css_class(heading, "consumer-column");
        if (col > 1) gtk_label_set_xalign(GTK_LABEL(heading), 1);
        gtk_grid_attach(GTK_GRID(table), heading, col, 0, 1, 1);
    }
    const char* phases[] = {"Packet decode", "Texture upload", "Pipeline preparation", "Descriptor update", "Command recording",
        "Buffer synchronization", "Render-target setup", "Shader specialization", "Draw submission", "Present preparation"};
    for (unsigned row = 0; row < 10; ++row) {
        auto* phase = label(phases[row], "consumer-cell");
        gtk_widget_set_size_request(phase, 205, -1);
        gtk_grid_attach(GTK_GRID(table), phase, 0, row + 1, 1, 1);
        auto* bar = gtk_progress_bar_new();
        gtk_widget_add_css_class(bar, "consumer-bar");
        gtk_widget_set_hexpand(bar, true);
        gtk_widget_set_size_request(bar, 140, -1);
        gtk_widget_set_valign(bar, GTK_ALIGN_CENTER);
        gtk_grid_attach(GTK_GRID(table), bar, 1, row + 1, 1, 1);
        for (unsigned col = 2; col < 5; ++col) {
            auto* value = label("–", "consumer-cell");
            gtk_widget_add_css_class(value, "numeric");
            gtk_label_set_xalign(GTK_LABEL(value), 1);
            gtk_widget_set_size_request(value, col == 2 ? 90 : col == 3 ? 75 : 110, -1);
            gtk_grid_attach(GTK_GRID(table), value, col, row + 1, 1, 1);
        }
    }
    gtk_box_append(GTK_BOX(consumers), table);
    gtk_flow_box_insert(GTK_FLOW_BOX(lower), consumers, -1);

    auto* side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(side, 430, -1);
    gtk_widget_set_hexpand(side, true);
    resource_panel_ = card();
    gtk_widget_add_css_class(resource_panel_, "resource-panel");
    auto* resource_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    auto* resource_title = label("Shader activity", "monitor-heading");
    gtk_widget_set_hexpand(resource_title, true);
    gtk_box_append(GTK_BOX(resource_header), resource_title);
    resource_state_ = label("–", "resource-state");
    gtk_box_append(GTK_BOX(resource_header), resource_state_);
    gtk_box_append(GTK_BOX(resource_panel_), resource_header);
    auto* work_table = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(work_table), 14);
    const char* work_headers[] = {"Work", "Active", "Done", "Elapsed / window"};
    for (unsigned col = 0; col < 4; ++col) {
        auto* heading = label(work_headers[col], "work-cell");
        gtk_widget_add_css_class(heading, "consumer-column");
        if (col) gtk_label_set_xalign(GTK_LABEL(heading), 1);
        gtk_grid_attach(GTK_GRID(work_table), heading, col, 0, 1, 1);
    }
    const char* work_labels[] = {"Translation", "Specialization", "Pipeline build", "Shader modules"};
    for (unsigned row = 0; row < 4; ++row) {
        auto* heading = label(work_labels[row], "work-cell");
        gtk_widget_set_hexpand(heading, true);
        gtk_grid_attach(GTK_GRID(work_table), heading, 0, row + 1, 1, 1);
        for (unsigned col = 0; col < 3; ++col) {
            auto* value = label("–", "work-cell");
            gtk_widget_add_css_class(value, "numeric");
            gtk_label_set_xalign(GTK_LABEL(value), 1);
            gtk_widget_set_size_request(value, col == 2 ? 100 : 45, -1);
            activity_values_[row][col] = value;
            gtk_grid_attach(GTK_GRID(work_table), value, col + 1, row + 1, 1, 1);
        }
    }
    gtk_box_append(GTK_BOX(resource_panel_), work_table);
    queues_ = label("Preparation – queued · Optimization – queued", "resource-scope");
    gtk_box_append(GTK_BOX(resource_panel_), queues_);
    auto* cache = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(cache), 16);
    gtk_grid_set_row_spacing(GTK_GRID(cache), 16);
    const char* cache_labels[] = {"Cached variants", "CPU payload", "Disk cache"};
    for (unsigned i = 0; i < 3; ++i) {
        auto* heading = label(cache_labels[i], "resource-label");
        gtk_widget_set_hexpand(heading, true);
        gtk_grid_attach(GTK_GRID(cache), heading, 0, i, 1, 1);
        shader_values_[i] = label("–", "resource-value");
        gtk_label_set_xalign(GTK_LABEL(shader_values_[i]), 1);
        gtk_grid_attach(GTK_GRID(cache), shader_values_[i], 1, i, 1, 1);
    }
    gtk_box_append(GTK_BOX(resource_panel_), cache);
    resource_scope_ = resource_title;
    gtk_box_append(GTK_BOX(side), resource_panel_);
    memory_panel_ = card();
    gtk_widget_add_css_class(memory_panel_, "resource-panel");
    gtk_box_append(GTK_BOX(memory_panel_), label("Memory", "monitor-heading"));
    auto* memory_rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    const char* memory_labels[] = {"Process RSS", "Sampled cache", "Storage cache", "GPU heap estimate"};
    for (unsigned i = 0; i < 4; ++i) {
        auto* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        auto* values = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
        auto* heading = label(memory_labels[i], "resource-label");
        gtk_widget_set_hexpand(heading, true);
        gtk_box_append(GTK_BOX(values), heading);
        memory_values_[i] = label("–", "resource-value");
        gtk_label_set_xalign(GTK_LABEL(memory_values_[i]), 1);
        gtk_box_append(GTK_BOX(values), memory_values_[i]);
        gtk_box_append(GTK_BOX(row), values);
        if (i) {
            memory_gauges_[i - 1] = gtk_progress_bar_new();
            gtk_widget_add_css_class(memory_gauges_[i - 1], "resource-gauge");
            gtk_widget_set_hexpand(memory_gauges_[i - 1], true);
            gtk_box_append(GTK_BOX(row), memory_gauges_[i - 1]);
        }
        gtk_box_append(GTK_BOX(memory_rows), row);
    }
    gtk_box_append(GTK_BOX(memory_panel_), memory_rows);
    memory_scope_ = label("GPU source – · received –", "resource-scope");
    gtk_box_append(GTK_BOX(memory_panel_), memory_scope_);
    gtk_box_append(GTK_BOX(side), memory_panel_);
    gtk_flow_box_insert(GTK_FLOW_BOX(lower), side, -1);
    gtk_box_append(GTK_BOX(live), lower);
    auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_add_css_class(footer, "profiler-footer");
    gtk_box_append(GTK_BOX(footer), label("Main VideoOut", "resource-scope"));
    freshness_ = label("Latest frame received –", "monitor-freshness");
    gtk_box_append(GTK_BOX(footer), freshness_);
    stall_state_ = label("", "resource-scope");
    gtk_widget_set_hexpand(stall_state_, true);
    gtk_label_set_xalign(GTK_LABEL(stall_state_), 1);
    gtk_box_append(GTK_BOX(footer), stall_state_);
    gtk_box_append(GTK_BOX(live), footer);

    auto* console = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_vexpand(console, true);
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    auto* heading = label("Session log", "monitor-heading");
    gtk_widget_set_hexpand(heading, true);
    gtk_box_append(GTK_BOX(actions), heading);
    follow_ = gtk_check_button_new_with_label("Follow output");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(follow_), true);
    gtk_widget_set_valign(follow_, GTK_ALIGN_CENTER);
    g_signal_connect(follow_, "toggled", G_CALLBACK(+[](GtkCheckButton* button, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        const bool follow = gtk_check_button_get_active(button);
        vte_terminal_set_scroll_on_output(VTE_TERMINAL(self.view_), follow);
        if (follow) self.scroll_to_end();
    }), this);
    gtk_box_append(GTK_BOX(actions), follow_);
    copy_ = gtk_button_new_with_label("Copy");
    gtk_widget_add_css_class(copy_, "toolbar-action");
    gtk_widget_set_sensitive(copy_, false);
    gtk_widget_set_tooltip_text(copy_, "Copy selected text (Ctrl+C or Ctrl+Shift+C)");
    g_signal_connect(copy_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        static_cast<ProfilerView*>(data)->copy_selection();
    }), this);
    gtk_box_append(GTK_BOX(actions), copy_);
    open_ = gtk_button_new_with_label("Open log");
    gtk_widget_add_css_class(open_, "toolbar-action");
    gtk_widget_set_sensitive(open_, false);
    g_signal_connect(open_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        try {
            self.open_log();
        } catch (const std::exception& error) {
            show_error(self.root_, error.what());
        } catch (...) {
            show_error(self.root_, "Could not open the log file");
        }
    }), this);
    gtk_box_append(GTK_BOX(actions), open_);
    gtk_box_append(GTK_BOX(console), actions);

    auto* overlay = gtk_overlay_new();
    gtk_widget_set_vexpand(overlay, true);
    auto* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_add_css_class(scroll, "console-frame");
    view_ = vte_terminal_new();
    gtk_widget_add_css_class(view_, "profiler-console");
    auto* terminal = VTE_TERMINAL(view_);
    vte_terminal_set_input_enabled(terminal, false);
    vte_terminal_set_scrollback_lines(terminal, 5000);
    vte_terminal_set_scroll_on_output(terminal, true);
    vte_terminal_set_scroll_on_keystroke(terminal, false);
    vte_terminal_set_enable_fallback_scrolling(terminal, false);
    vte_terminal_set_scroll_unit_is_pixels(terminal, true);
    vte_terminal_set_allow_hyperlink(terminal, false);
    vte_terminal_set_audible_bell(terminal, false);
    vte_terminal_set_cursor_blink_mode(terminal, VTE_CURSOR_BLINK_OFF);
    GdkRGBA foreground;
    GdkRGBA background;
    gdk_rgba_parse(&foreground, "#cedde7");
    gdk_rgba_parse(&background, "#08121a");
    vte_terminal_set_colors(terminal, &foreground, &background, nullptr, 0);
    auto* font = pango_font_description_from_string("Monospace");
    pango_font_description_set_absolute_size(font, 13 * PANGO_SCALE);
    vte_terminal_set_font(terminal, font);
    pango_font_description_free(font);
    vte_terminal_feed(terminal, "\033[?25l", -1);
    gtk_widget_set_hexpand(view_, true);
    gtk_widget_set_vexpand(view_, true);
    g_signal_connect(view_, "selection-changed", G_CALLBACK(+[](VteTerminal* terminal, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        gtk_widget_set_sensitive(self.copy_, vte_terminal_get_has_selection(terminal));
    }), this);
    keys_ = gtk_event_controller_key_new();
    // Handle copying before VTE processes keys in the output-only terminal.
    gtk_event_controller_set_propagation_phase(keys_, GTK_PHASE_CAPTURE);
    g_signal_connect(keys_, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType modifiers,
        gpointer data) -> gboolean {
        if ((modifiers & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) == GDK_CONTROL_MASK &&
            (key == GDK_KEY_c || key == GDK_KEY_C)) {
            auto& self = *static_cast<ProfilerView*>(data);
            if (!gtk_widget_get_mapped(self.view_)) return false;
            self.copy_selection();
            return true;
        }
        return false;
    }), this);
    gtk_widget_add_controller(root_, keys_);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), view_);
    adjustment_ = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view_));
    g_signal_connect(adjustment_, "changed", G_CALLBACK(+[](GtkAdjustment*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(self.follow_))) self.scroll_to_end();
    }), this);
    g_signal_connect(view_, "map", G_CALLBACK(+[](GtkWidget*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(self.follow_))) self.scroll_to_end();
    }), this);
    gtk_overlay_set_child(GTK_OVERLAY(overlay), scroll);
    empty_ = label("Start or rebuild a game to view its console output.", "console-empty");
    gtk_widget_set_halign(empty_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(empty_, GTK_ALIGN_CENTER);
    gtk_widget_set_can_target(empty_, false);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), empty_);
    gtk_box_append(GTK_BOX(console), overlay);
    gtk_stack_add_titled(GTK_STACK(workspace), console, "logs", "Logs");
}

ProfilerView::~ProfilerView() {
    g_signal_handlers_disconnect_by_data(monitoring_toggle_, this);
    g_signal_handlers_disconnect_by_data(frame_selector_, this);
    g_signal_handlers_disconnect_by_data(chart_fps_button_, this);
    g_signal_handlers_disconnect_by_data(follow_live_, this);
    g_signal_handlers_disconnect_by_data(chart_click_, this);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(chart_), nullptr, nullptr, nullptr);
    g_signal_handlers_disconnect_by_data(follow_, this);
    g_signal_handlers_disconnect_by_data(copy_, this);
    g_signal_handlers_disconnect_by_data(open_, this);
    g_signal_handlers_disconnect_by_data(keys_, this);
    g_signal_handlers_disconnect_by_data(view_, this);
    g_signal_handlers_disconnect_by_data(adjustment_, this);
    g_object_unref(root_);
}

void ProfilerView::bridge_status(const BridgeStatus& status, const std::string& mcp_endpoint, const std::string& error) {
    const std::string connection = status.connected ? "■ Connected" :
        status.port ? (status.error.empty() ? "Disconnected" : "Connection lost") : "Bridge unavailable";
    gtk_label_set_text(GTK_LABEL(bridge_connection_), connection.c_str());
    if (status.connected) gtk_widget_add_css_class(bridge_connection_, "connected");
    else gtk_widget_remove_css_class(bridge_connection_, "connected");
    const auto udp = status.port ? "UDP  127.0.0.1:" + std::to_string(status.port) : "UDP  Unavailable";
    const auto mcp = mcp_endpoint.empty() ? "MCP  Unavailable" : "MCP  " + mcp_endpoint;
    gtk_label_set_text(GTK_LABEL(bridge_endpoint_), udp.c_str());
    gtk_label_set_text(GTK_LABEL(mcp_endpoint_), mcp.c_str());
    const auto detail = error.empty() ? status.error : status.error.empty() ? error : error + " · " + status.error;
    gtk_widget_set_tooltip_text(bridge_activity_, detail.empty() ? nullptr : detail.c_str());
    gtk_label_set_ellipsize(GTK_LABEL(bridge_activity_), PANGO_ELLIPSIZE_END);
    const auto activity = detail.empty() ? (status.has_frame ? "Frame " + std::to_string(status.frame) : "Frame unavailable") + " · " +
        std::to_string(status.received_events) + " events · " + std::to_string(status.missing_events) + " missing packets" : detail;
    gtk_label_set_text(GTK_LABEL(bridge_activity_), activity.c_str());
}

void ProfilerView::monitoring(const MonitoringSnapshot& snapshot) {
    if (snapshot.session_id != monitoring_.session_id ||
        (snapshot.enabled && snapshot.monitoring_epoch != monitoring_.monitoring_epoch) ||
        (!snapshot.samples.empty() && !monitoring_.samples.empty() &&
        (snapshot.samples.back().monitoring_epoch != monitoring_.samples.back().monitoring_epoch ||
         snapshot.samples.back().output_handle != monitoring_.samples.back().output_handle ||
         snapshot.samples.back().output_generation != monitoring_.samples.back().output_generation))) inspected_.reset();
    monitoring_ = snapshot;
    const bool retained = snapshot.has_frames && (!snapshot.connected || !snapshot.enabled || snapshot.stale);
    std::string state;
    if (snapshot.pending) state = snapshot.requested_enabled.value_or(false) ? "Starting monitoring…" : "Stopping monitoring…";
    else if (!snapshot.connected) state = "Monitoring disconnected";
    else if (!snapshot.supported) state = "Monitoring unavailable";
    else if (!snapshot.enabled) state = "Monitoring off";
    else if (snapshot.stale) state = "No new frame data";
    else if (!snapshot.has_frames) state = "Waiting for frame data";
    else if (!snapshot.stats.estimated_fps) state = "Waiting for frame intervals";
    else state = "Monitoring active";
    if (!snapshot.error.empty()) state += " · " + snapshot.error;
    gtk_label_set_text(GTK_LABEL(monitoring_state_), state.c_str());
    gtk_widget_set_tooltip_text(monitoring_state_, state.c_str());
    updating_monitoring_ = true;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(monitoring_toggle_), snapshot.pending && snapshot.requested_enabled ?
        *snapshot.requested_enabled : snapshot.enabled);
    updating_monitoring_ = false;
    gtk_widget_set_sensitive(monitoring_toggle_, snapshot.connected && snapshot.supported && !snapshot.pending &&
        static_cast<bool>(request_monitoring_));
    gtk_widget_set_tooltip_text(monitoring_toggle_, !snapshot.connected ? "Connect a game to change monitoring" :
        !snapshot.supported ? "Frame pacing measurements are unavailable for this session" :
        snapshot.pending ? "Waiting for the game to confirm monitoring state" : "Collect frame pacing measurements");
    for (const auto* style : {"active", "pending"}) gtk_widget_remove_css_class(monitoring_state_, style);
    if (snapshot.pending) gtk_widget_add_css_class(monitoring_state_, "pending");
    else if (snapshot.state == "active") gtk_widget_add_css_class(monitoring_state_, "active");

    const auto& stats = snapshot.stats;
    const std::optional<double> values[] = {stats.estimated_fps, stats.mean_interval_us,
        stats.p95_interval_us, stats.worst_interval_us};
    for (std::size_t i = 0; i < metrics_.size(); ++i) {
        const auto text = values[i] ? number(*values[i] / (i ? 1000.0 : 1.0), i ? 2 : 1) + (i ? " ms" : " FPS") : "–";
        gtk_label_set_text(GTK_LABEL(metrics_[i]), text.c_str());
        gtk_widget_set_tooltip_text(metrics_[i], text.c_str());
        if (retained) gtk_widget_add_css_class(metrics_[i], "stale");
        else gtk_widget_remove_css_class(metrics_[i], "stale");
    }
    const auto window = std::string("Last 1 s") + (retained ? " · retained observation" :
        snapshot.has_frames && !stats.complete ? " · partial window" : "");
    gtk_label_set_text(GTK_LABEL(window_state_), window.c_str());
    gtk_widget_set_tooltip_text(window_state_, window.c_str());
    std::string coverage = "– / – samples · –";
    if (snapshot.has_frames) {
        coverage = std::to_string(stats.observed_samples) + " / " + std::to_string(stats.expected_samples) + " samples · " +
            (stats.coverage ? number(*stats.coverage * 100, 1) + "%" : "–");
    }
    const auto coverage_tip = std::to_string(stats.sample_count) + " valid intervals; " + std::to_string(stats.missing_samples) +
        " missing samples; " + std::to_string(stats.sender_dropped_samples) + " sender drops; " + std::to_string(stats.packet_gaps) +
        " packet gaps. " + (stats.complete ? "Complete source window." : "Incomplete source window.");
    gtk_widget_set_tooltip_text(coverage_, coverage_tip.c_str());
    gtk_label_set_text(GTK_LABEL(coverage_), coverage.c_str());
    std::string freshness = "Latest frame received –";
    if (snapshot.age_ms) freshness = (retained ? "Stale · last received " : "Latest frame received ") +
        number(*snapshot.age_ms / 1000.0) + " s ago";
    gtk_label_set_text(GTK_LABEL(freshness_), freshness.c_str());
    if (retained) gtk_widget_add_css_class(freshness_, "stale");
    else gtk_widget_remove_css_class(freshness_, "stale");
    const auto stall = snapshot.stall_evidence == "instrumented_work_active" ? "Missing frames · instrumented work active" :
        snapshot.stall_evidence == "unknown" ? "Missing frames · cause unknown" : "";
    gtk_label_set_text(GTK_LABEL(stall_state_), stall);
    update_frame_detail();
    update_resources();
    gtk_widget_queue_draw(chart_);
}

void ProfilerView::update_resources() {
    const auto& resources = monitoring_.resources;
    const bool available = resources.sample.has_value();
    const bool retained = available && (!monitoring_.connected || !monitoring_.enabled || resources.stale);
    for (auto* panel : {resource_panel_, memory_panel_}) {
        if (retained) gtk_widget_add_css_class(panel, "stale");
        else gtk_widget_remove_css_class(panel, "stale");
    }
    const auto scope = resources.work_delta ? "Window " + number(resources.work_window_us / 1000000.0) + " s" :
        resources.state == "disabled" ? std::string("Off") : std::string("Window –");
    gtk_label_set_text(GTK_LABEL(resource_state_), scope.c_str());
    const unsigned work_indices[] = {1, 2, 3, 5};
    for (unsigned row = 0; row < activity_values_.size(); ++row) {
        std::array<std::string, 3> values{"–", "–", "–"};
        std::string tip = "Work timing unavailable";
        bool active = false;
        if (available && resources.sample->activity) {
            const auto& work = (*resources.sample->activity)[work_indices[row]];
            active = work.active > 0;
            values[0] = std::to_string(work.active);
            values[1] = std::to_string(work.completed);
            if (resources.work_delta) {
                const auto& delta = (*resources.work_delta)[work_indices[row]];
                values[2] = number(delta.total_us / 1000.0, 2) + " ms";
            }
            tip = "Process lifetime: " + std::to_string(work.started) + " started, " + std::to_string(work.completed) +
                " completed, " + std::to_string(work.failed) + " failed; elapsed " + number(work.total_us / 1000.0, 2) +
                " ms; longest finished attempt " + number(work.max_us / 1000.0, 2) +
                " ms.\nDone counts completed attempts since process start. Window elapsed includes completed and failed attempts. Nested scopes overlap; not GPU execution time.";
        } else if (available && row == 0) {
            values[0] = std::to_string(resources.sample->shaders.translations_active);
            values[1] = std::to_string(resources.sample->shaders.translations_completed);
        } else if (available && row == 2) {
            values[0] = std::to_string(resources.sample->shaders.pipeline_builds_active);
            values[1] = std::to_string(resources.sample->shaders.pipeline_builds_completed);
        }
        for (unsigned col = 0; col < 3; ++col) {
            gtk_label_set_text(GTK_LABEL(activity_values_[row][col]), values[col].c_str());
            gtk_widget_set_tooltip_text(activity_values_[row][col], tip.c_str());
            gtk_widget_remove_css_class(activity_values_[row][col], "work-active");
        }
        if (active) gtk_widget_add_css_class(activity_values_[row][0], "work-active");
    }
    const auto queues = available ? "Preparation " + std::to_string(resources.sample->shaders.preparation_queue_pending) +
        " queued · Optimization " + std::to_string(resources.sample->shaders.pipeline_optimization_queue_pending) + " queued" :
        "Preparation – queued · Optimization – queued";
    gtk_label_set_text(GTK_LABEL(queues_), queues.c_str());
    std::array<std::string, 3> cache{"–", "–", "–"};
    for (auto* widget : shader_values_) gtk_widget_set_tooltip_text(widget, nullptr);
    if (available) {
        const auto& shader = resources.sample->shaders;
        cache = {std::to_string(shader.cached_variants_count), bytes(static_cast<long double>(shader.source_code_bytes) + shader.variant_spirv_bytes),
            std::to_string(shader.disk_cache_hits) + " hits / " + std::to_string(shader.disk_cache_misses) + " misses"};
        const auto tip = "Process lifetime cache totals. Copied code " + bytes(shader.source_code_bytes) +
            "; retained variant SPIR-V " + bytes(shader.variant_spirv_bytes) + ". Excludes IR, metadata and native driver allocations.\n" +
            std::to_string(shader.disk_cache_writes) + " writes; " + std::to_string(shader.disk_cache_load_failures) + " load failures; " +
            std::to_string(shader.disk_cache_write_failures) + " write failures.";
        for (auto* widget : shader_values_) gtk_widget_set_tooltip_text(widget, tip.c_str());
    }
    for (unsigned i = 0; i < cache.size(); ++i) gtk_label_set_text(GTK_LABEL(shader_values_[i]), cache[i].c_str());
    std::string details = "Elapsed wall time · overlapping scopes";
    if (available && resources.sample->activity) {
        details += "\nOther process work (not selected-frame attribution):";
        for (unsigned i : {0u, 4u, 6u, 7u, 8u, 9u, 10u}) {
            const auto& work = (*resources.sample->activity)[i];
            details += "\n" + std::string(resource_work_names[i]) + ": " + std::to_string(work.active) + " active";
            if (resources.work_delta) details += "; " + number((*resources.work_delta)[i].total_us / 1000.0, 2) + " ms in observed window";
        }
    }
    gtk_widget_set_tooltip_text(resource_scope_, details.c_str());
    for (unsigned i = 0; i < memory_values_.size(); ++i) {
        std::string text = "–";
        gtk_widget_remove_css_class(memory_values_[i], "stale");
        if (available) {
            const auto& m = resources.sample->memory;
            const std::optional<std::uint64_t> values[] = {m.process_rss_bytes, m.sampled_cache_bytes, m.storage_cache_bytes, m.gpu_heap_usage_bytes};
            const std::optional<std::uint64_t> budgets[] = {m.sampled_cache_budget_bytes, m.storage_cache_budget_bytes, m.gpu_heap_budget_bytes};
            if (values[i]) text = bytes(*values[i]);
            if (i) text += " / " + (budgets[i - 1] ? bytes(*budgets[i - 1]) : "–");
            if (i) {
                const bool known = values[i] && budgets[i - 1];
                const double fraction = known ? static_cast<double>(*values[i]) / *budgets[i - 1] : 0;
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(memory_gauges_[i - 1]), std::clamp(fraction, 0.0, 1.0));
                if (fraction > 1) gtk_widget_add_css_class(memory_gauges_[i - 1], "over-budget");
                else gtk_widget_remove_css_class(memory_gauges_[i - 1], "over-budget");
            }
        } else if (i) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(memory_gauges_[i - 1]), 0);
        gtk_label_set_text(GTK_LABEL(memory_values_[i]), text.c_str());
    }
    const auto age = resources.gpu_heap_age_ms ? number(*resources.gpu_heap_age_ms / 1000.0, 1) + " s old" : "–";
    const auto receipt = resources.age_ms ? number(*resources.age_ms / 1000.0, 1) + " s ago" : "–";
    const auto memory_scope = "GPU sample " + age + (retained ? " · retained" : "");
    gtk_label_set_text(GTK_LABEL(memory_scope_), memory_scope.c_str());
    if (resources.gpu_heap_age_ms && *resources.gpu_heap_age_ms >= 10000) gtk_widget_add_css_class(memory_scope_, "work-active");
    else gtk_widget_remove_css_class(memory_scope_, "work-active");
    const auto memory_tip = "Received " + receipt + ". Process RSS and texture-cache accounting have separate scopes. GPU heap is a driver estimate for the largest device-local heap.\nRSS source: " +
        (resources.process_rss_age_ms ? number(*resources.process_rss_age_ms / 1000.0) + " s old" : "–");
    gtk_widget_set_tooltip_text(memory_panel_, memory_tip.c_str());
}

void ProfilerView::update_frame_detail() {
    const MonitoringSample* sample = inspected_ ? &*inspected_ : nullptr;
    if (!sample && !monitoring_.samples.empty()) {
        sample = &monitoring_.samples.back();
        if (gtk_drop_down_get_selected(GTK_DROP_DOWN(frame_selector_)) == 1) {
            const MonitoringSample* worst = nullptr;
            for (const auto& candidate : monitoring_.samples) {
                if (!candidate.flip_interval_us || candidate.timestamp_us <= monitoring_.stats.window_start_us ||
                    candidate.timestamp_us > monitoring_.stats.window_end_us) continue;
                if (!worst || *candidate.flip_interval_us > *worst->flip_interval_us) worst = &candidate;
            }
            sample = worst;
        }
    }
    gtk_widget_set_sensitive(frame_selector_, !monitoring_.samples.empty());
    updating_follow_ = true;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(follow_live_), !inspected_.has_value());
    updating_follow_ = false;
    gtk_widget_set_sensitive(follow_live_, monitoring_.has_frames);
    const auto frame = sample ? "Frame " + std::to_string(sample->frame) : "Frame –";
    const auto stream = sample ? "Output " + std::to_string(sample->output_handle) + " / generation " +
        std::to_string(sample->output_generation) : "Output – / generation –";
    const auto interval = sample && sample->flip_interval_us ? "Flip interval " + number(*sample->flip_interval_us / 1000.0, 2) + " ms" : "Flip interval –";
    gtk_label_set_text(GTK_LABEL(frame_identity_), frame.c_str());
    gtk_label_set_text(GTK_LABEL(consumer_frame_), frame.c_str());
    gtk_label_set_text(GTK_LABEL(frame_interval_), interval.c_str());
    std::string scope = "No frame selected";
    if (sample) {
        scope = inspected_ ? "Inspect selection" : "Following live";
        scope += " · completed flip " + std::to_string(sample->completed_flip_count) + " · epoch " +
            std::to_string(sample->monitoring_epoch);
        if (!sample->flip_interval_us) scope += " · first completion establishes the interval clock";
        else if (sample->missing_before) scope += " · " + std::to_string(sample->missing_before) + " samples missing before this frame";
        if (monitoring_.stale || !monitoring_.enabled || !monitoring_.connected) scope += " · retained data";
    }
    scope += "\n" + stream;
    gtk_widget_set_tooltip_text(frame_identity_, scope.c_str());
    gtk_widget_set_tooltip_text(frame_interval_, interval.c_str());
}

void ProfilerView::inspect_chart(double x, double y) {
    const auto width = gtk_widget_get_width(chart_);
    const auto height = gtk_widget_get_height(chart_);
    const double chart_top = gtk_widget_get_height(chart_header_) + chart_header_gap;
    const double plot_width = width - chart_left - chart_right;
    const double plot_height = height - chart_top - chart_bottom;
    if (monitoring_.samples.empty() || plot_width <= 0 || plot_height <= 0 || x < chart_left ||
        x > width - chart_right || y < chart_top || y > height - chart_bottom) return;
    const auto end = chart_end(monitoring_);
    const double maximum = chart_maximum(monitoring_, chart_fps_);
    const MonitoringSample* closest = nullptr;
    double distance = 24 * 24;
    for (std::size_t i = 0; i < monitoring_.samples.size(); ++i) {
        const auto& sample = monitoring_.samples[i];
        if (!sample.flip_interval_us || sample.timestamp_us > end || end - sample.timestamp_us > 60000000) continue;
        const double px = chart_left + plot_width * (1.0 - (end - sample.timestamp_us) / 60000000.0);
        const auto value = chart_value(monitoring_, i, chart_fps_);
        if (!value) continue;
        const double py = chart_top + plot_height * (1.0 - *value / maximum);
        const double candidate = (px - x) * (px - x) + (py - y) * (py - y);
        if (candidate < distance) { closest = &sample; distance = candidate; }
    }
    if (!closest) return;
    inspected_ = *closest;
    update_frame_detail();
    gtk_widget_queue_draw(chart_);
}

void ProfilerView::draw_chart(cairo_t* context, int width, int height) noexcept {
    try {
        const double chart_top = gtk_widget_get_height(chart_header_) + chart_header_gap;
        const double plot_width = width - chart_left - chart_right;
        const double plot_height = height - chart_top - chart_bottom;
        if (plot_width <= 0 || plot_height <= 0) return;
        const double maximum = chart_maximum(monitoring_, chart_fps_);
        cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(context, 12);
        const unsigned ticks = chart_fps_ ? 5 : 4;
        for (unsigned tick = 0; tick <= ticks; ++tick) {
            const double y = chart_top + plot_height * tick / ticks;
            cairo_set_source_rgb(context, 0.14, 0.24, 0.30);
            cairo_set_line_width(context, 1);
            cairo_move_to(context, chart_left, y);
            cairo_line_to(context, width - chart_right, y);
            cairo_stroke(context);
            cairo_set_source_rgb(context, 0.56, 0.68, 0.76);
            chart_text(context, chart_left - 10, y + 4, number(maximum * (1 - static_cast<double>(tick) / ticks), 0), true);
        }
        chart_text(context, 10, chart_top - 8, chart_fps_ ? "FPS" : "ms");
        for (unsigned tick = 0; tick <= 6; ++tick) {
            const double x = chart_left + plot_width * tick / 6.0;
            cairo_set_source_rgb(context, 0.12, 0.21, 0.28);
            cairo_move_to(context, x, chart_top);
            cairo_line_to(context, x, height - chart_bottom);
            cairo_stroke(context);
            cairo_set_source_rgb(context, 0.56, 0.68, 0.76);
            const auto text = tick == 6 ? "0 s" : "−" + std::to_string(60 - tick * 10) + " s";
            chart_text(context, x - (tick == 6 ? 0 : 12), height - 16, text, tick == 6);
        }
        if (monitoring_.samples.empty()) {
            cairo_set_source_rgb(context, 0.58, 0.69, 0.77);
            const std::string text = monitoring_.enabled ? "Waiting for frame interval data" : "No frame interval data";
            cairo_text_extents_t extent;
            cairo_text_extents(context, text.c_str(), &extent);
            chart_text(context, chart_left + (plot_width - extent.width) / 2, chart_top + plot_height / 2, text);
            return;
        }
        const auto end = chart_end(monitoring_);
        auto position = [&](const MonitoringSample& sample) {
            return chart_left + plot_width * (1.0 - (end - sample.timestamp_us) / 60000000.0);
        };
        const MonitoringSample* previous = nullptr;
        bool has_interval = false;
        double previous_value = 0;
        cairo_save(context);
        cairo_rectangle(context, chart_left, chart_top - 1, plot_width + 1, plot_height + 2);
        cairo_clip(context);
        for (std::size_t i = 0; i < monitoring_.samples.size(); ++i) {
            const auto& sample = monitoring_.samples[i];
            if (sample.timestamp_us > end || end - sample.timestamp_us > 60000000) continue;
            const auto value = chart_value(monitoring_, i, chart_fps_);
            if (!value) { previous = nullptr; continue; }
            has_interval = true;
            const double x = position(sample);
            const double y = chart_top + plot_height * (1.0 - *value / maximum);
            const bool gap = sample.missing_before || (previous &&
                (sample.completed_flip_count != previous->completed_flip_count + 1 ||
                 sample.monitoring_epoch != previous->monitoring_epoch || sample.output_handle != previous->output_handle ||
                 sample.output_generation != previous->output_generation ||
                 sample.timestamp_us - previous->timestamp_us >= 1000000));
            if (gap && previous) {
                cairo_set_source_rgba(context, 0.97, 0.72, 0.28, 0.13);
                cairo_rectangle(context, position(*previous), chart_top, std::max(2.0, x - position(*previous)), plot_height);
                cairo_fill(context);
            }
            cairo_set_source_rgb(context, 0.0, 0.83, 0.92);
            cairo_set_line_width(context, 1.5);
            if (previous && !gap) {
                const double py = chart_top + plot_height * (1.0 - previous_value / maximum);
                cairo_move_to(context, position(*previous), py);
                cairo_line_to(context, x, y);
                cairo_stroke(context);
            } else {
                cairo_arc(context, x, y, 2, 0, 6.283185307);
                cairo_fill(context);
            }
            previous = &sample;
            previous_value = *value;
        }
        if (inspected_ && inspected_->timestamp_us <= end && end - inspected_->timestamp_us <= 60000000) {
            const double x = position(*inspected_);
            cairo_set_source_rgb(context, 1.0, 0.76, 0.31);
            const double dash[] = {4, 4};
            cairo_set_dash(context, dash, 2, 0);
            cairo_move_to(context, x, chart_top);
            cairo_line_to(context, x, height - chart_bottom);
            cairo_stroke(context);
            cairo_set_dash(context, nullptr, 0, 0);
        }
        cairo_restore(context);
        if (!has_interval) {
            cairo_set_source_rgb(context, 0.58, 0.69, 0.77);
            chart_text(context, chart_left + 18, chart_top + plot_height / 2, chart_fps_ ? "Waiting for one second of reliable flip completions" : "First completion received · waiting for a valid interval");
        }
    } catch (...) {
        g_printerr("FrameRift: could not draw frame interval history\n");
    }
}

void ProfilerView::preparing(const std::string& title, bool rebuilding) {
    log_path_.clear();
    has_output_ = false;
    previous_carriage_return_ = false;
    gtk_label_set_text(GTK_LABEL(title_), title.c_str());
    gtk_label_set_text(GTK_LABEL(status_), rebuilding ? "Rebuilding" : "Preparing");
    auto* terminal = VTE_TERMINAL(view_);
    // VTE ignores API reset while input is disabled.
    vte_terminal_set_input_enabled(terminal, true);
    vte_terminal_reset(terminal, true, true);
    vte_terminal_set_input_enabled(terminal, false);
    vte_terminal_feed(terminal, "\033[?25l", -1);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(follow_), true);
    gtk_label_set_text(GTK_LABEL(empty_), rebuilding ? "Rebuilding game…" : "Waiting for console output…");
    gtk_widget_set_visible(empty_, true);
    update_log_action();
}

void ProfilerView::rebuilt(const PreparedGame& game) {
    show_conversion_log(game.conversion_log_path);
    finished("Rebuild completed");
}

void ProfilerView::show_conversion_log(const std::filesystem::path& path) {
    log_path_ = path;
    std::ifstream log(log_path_, std::ios::binary | std::ios::ate);
    if (log) {
        const auto size = log.tellg();
        constexpr std::streamoff limit = 512 * 1024;
        if (size > limit) output("[Earlier conversion output is available in the full log.]\n");
        if (size > 0) {
            log.seekg(size > limit ? size - limit : std::streampos(0));
            std::string text(static_cast<std::size_t>(std::min<std::streamoff>(size, limit)), '\0');
            log.read(text.data(), static_cast<std::streamsize>(text.size()));
            text.resize(static_cast<std::size_t>(log.gcount()));
            output(text);
        }
    }
    update_log_action();
}

void ProfilerView::running(const PreparedGame& game) {
    log_path_ = game.log_path;
    gtk_label_set_text(GTK_LABEL(status_), "Running");
    update_log_action();
}

void ProfilerView::finished(const std::string& status) {
    gtk_label_set_text(GTK_LABEL(status_), status.c_str());
    if (!has_output_) gtk_label_set_text(GTK_LABEL(empty_), "No console output was recorded.");
    update_log_action();
}

void ProfilerView::failed(const std::string& message, const std::filesystem::path& conversion_log) {
    if (!conversion_log.empty()) show_conversion_log(conversion_log);
    gtk_label_set_text(GTK_LABEL(status_), "Failed");
    output(message + "\n");
}

void ProfilerView::output(std::string_view text) {
    if (text.empty()) return;
    std::string display;
    display.reserve(text.size());
    for (const char character : text) {
        if (character == '\n' && !previous_carriage_return_) display.push_back('\r');
        display.push_back(character);
        previous_carriage_return_ = character == '\r';
    }
    vte_terminal_feed(VTE_TERMINAL(view_), display.data(), static_cast<gssize>(display.size()));
    has_output_ = true;
    gtk_widget_set_visible(empty_, false);
}

void ProfilerView::update_log_action() {
    std::error_code error;
    const bool available = !log_path_.empty() && std::filesystem::is_regular_file(log_path_, error);
    gtk_widget_set_sensitive(open_, available);
    gtk_widget_set_tooltip_text(open_, available ? log_path_.string().c_str() : "No log file is available yet");
}

void ProfilerView::copy_selection() {
    auto* terminal = VTE_TERMINAL(view_);
    if (vte_terminal_get_has_selection(terminal)) vte_terminal_copy_clipboard_format(terminal, VTE_FORMAT_TEXT);
}

void ProfilerView::scroll_to_end() {
    gtk_adjustment_set_value(adjustment_, gtk_adjustment_get_upper(adjustment_) - gtk_adjustment_get_page_size(adjustment_));
}

void ProfilerView::open_log() {
    update_log_action();
    if (!gtk_widget_get_sensitive(open_)) return;
    auto* file = g_file_new_for_path(log_path_.string().c_str());
    auto* launcher = gtk_file_launcher_new(file);
    g_object_unref(file);
    auto* root = gtk_widget_get_root(root_);
    auto* parent = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : nullptr;
    gtk_file_launcher_launch(launcher, parent, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
        auto* widget = GTK_WIDGET(data);
        GError* error = nullptr;
        if (!gtk_file_launcher_launch_finish(GTK_FILE_LAUNCHER(source), result, &error) && error) {
            show_error(widget, error->message);
        }
        g_clear_error(&error);
        g_object_unref(widget);
    }, g_object_ref(root_));
    g_object_unref(launcher);
}

} // namespace framerift
