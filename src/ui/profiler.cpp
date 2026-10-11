// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "profiler.hpp"

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

GtkWidget* label(const char* text, const char* style) {
    auto* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_widget_add_css_class(widget, style);
    return widget;
}

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

GtkWidget* panel() {
    auto* widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(widget, "monitor-panel");
    return widget;
}

void chart_text(cairo_t* context, double x, double y, const std::string& text, bool right = false) {
    cairo_text_extents_t extent;
    cairo_text_extents(context, text.c_str(), &extent);
    cairo_move_to(context, right ? x - extent.width : x, y);
    cairo_show_text(context, text.c_str());
}

constexpr double chart_left = 54;
constexpr double chart_right = 22;
constexpr double chart_top = 24;
constexpr double chart_bottom = 40;

double chart_maximum(const MonitoringSnapshot& snapshot) {
    double maximum = 0;
    for (const auto& sample : snapshot.samples)
        if (sample.flip_interval_us) maximum = std::max(maximum, *sample.flip_interval_us / 1000.0);
    return std::max(25.0, std::ceil(maximum / 10.0) * 10.0);
}

} // namespace

ProfilerView::ProfilerView(std::function<void(bool)> request_monitoring)
    : request_monitoring_(std::move(request_monitoring)) {
    root_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    g_object_ref_sink(root_);
    gtk_widget_add_css_class(root_, "profiler-page");
    auto* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    title_ = label("Profiler", "profiler-title");
    gtk_label_set_ellipsize(GTK_LABEL(title_), PANGO_ELLIPSIZE_END);
    gtk_widget_set_hexpand(title_, true);
    gtk_box_append(GTK_BOX(header), title_);
    status_ = label("No session", "profiler-status");
    gtk_widget_set_valign(status_, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(header), status_);
    gtk_box_append(GTK_BOX(root_), header);

    auto* connection = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(connection, "monitor-session-strip");
    bridge_connection_ = label("Waiting for game connection", "bridge-connection");
    gtk_box_append(GTK_BOX(connection), bridge_connection_);
    auto* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, true);
    gtk_box_append(GTK_BOX(connection), spacer);
    monitoring_state_ = label("Monitoring unavailable", "monitoring-state");
    gtk_label_set_ellipsize(GTK_LABEL(monitoring_state_), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(connection), monitoring_state_);
    gtk_box_append(GTK_BOX(connection), label("Monitoring", "monitor-control-label"));
    monitoring_switch_ = gtk_switch_new();
    gtk_widget_set_valign(monitoring_switch_, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(monitoring_switch_, false);
    g_signal_connect(monitoring_switch_, "state-set", G_CALLBACK(+[](GtkSwitch*, gboolean enabled, gpointer data) -> gboolean {
        auto& self = *static_cast<ProfilerView*>(data);
        if (self.updating_switch_) return false;
        auto restore = [&]() noexcept {
            self.updating_switch_ = true;
            gtk_switch_set_active(GTK_SWITCH(self.monitoring_switch_), self.monitoring_.enabled);
            gtk_switch_set_state(GTK_SWITCH(self.monitoring_switch_), self.monitoring_.enabled);
            self.updating_switch_ = false;
            gtk_widget_set_sensitive(self.monitoring_switch_, self.monitoring_.connected && self.monitoring_.supported &&
                !self.monitoring_.pending && static_cast<bool>(self.request_monitoring_));
            gtk_label_set_text(GTK_LABEL(self.monitoring_state_), "Monitoring request failed");
        };
        try {
            gtk_widget_set_sensitive(self.monitoring_switch_, false);
            gtk_label_set_text(GTK_LABEL(self.monitoring_state_), enabled ? "Starting monitoring…" : "Stopping monitoring…");
            if (self.request_monitoring_) self.request_monitoring_(enabled);
        } catch (const std::exception& error) {
            show_error(self.root_, error.what());
            restore();
        } catch (...) {
            show_error(self.root_, "Could not change monitoring");
            restore();
        }
        return true;
    }), this);
    gtk_box_append(GTK_BOX(connection), monitoring_switch_);
    gtk_box_append(GTK_BOX(root_), connection);

    auto* bridge = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    bridge_activity_ = label("", "bridge-endpoint");
    gtk_label_set_wrap(GTK_LABEL(bridge_activity_), true);
    gtk_box_append(GTK_BOX(bridge), bridge_activity_);
    bridge_endpoint_ = label("", "bridge-endpoint");
    mcp_endpoint_ = label("", "bridge-endpoint");
    for (auto* endpoint : {bridge_endpoint_, mcp_endpoint_}) {
        gtk_label_set_selectable(GTK_LABEL(endpoint), true);
        gtk_box_append(GTK_BOX(bridge), endpoint);
    }
    auto* bridge_details = gtk_expander_new("Connection details");
    gtk_widget_add_css_class(bridge_details, "monitor-connection-details");
    gtk_expander_set_child(GTK_EXPANDER(bridge_details), bridge);
    gtk_box_append(GTK_BOX(root_), bridge_details);

    auto* workspace = gtk_stack_new();
    gtk_stack_set_hhomogeneous(GTK_STACK(workspace), false);
    gtk_stack_set_vhomogeneous(GTK_STACK(workspace), false);
    gtk_widget_set_vexpand(workspace, true);
    auto* tabs = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(tabs), GTK_STACK(workspace));
    gtk_widget_set_halign(tabs, GTK_ALIGN_START);
    gtk_widget_add_css_class(tabs, "profiler-tabs");
    gtk_box_append(GTK_BOX(root_), tabs);
    gtk_box_append(GTK_BOX(root_), workspace);

    auto* live_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(live_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    auto* live = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(live_scroll), live);
    gtk_stack_add_titled(GTK_STACK(workspace), live_scroll, "live", "Live");
    auto* overview = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    window_state_ = label("Last 1 s", "monitor-window");
    gtk_widget_set_tooltip_text(window_state_, "One second ending at the latest received game monotonic timestamp; freshness is shown separately");
    gtk_widget_set_hexpand(window_state_, true);
    gtk_box_append(GTK_BOX(overview), window_state_);
    freshness_ = label("No frame data", "monitor-freshness");
    gtk_label_set_wrap(GTK_LABEL(freshness_), true);
    gtk_box_append(GTK_BOX(overview), freshness_);
    gtk_box_append(GTK_BOX(live), overview);

    auto* cards = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(cards), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(cards), true);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(cards), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(cards), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(cards), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(cards), 12);
    gtk_widget_add_css_class(cards, "metric-cards");
    const char* card_titles[] = {"FPS (estimated)", "Mean flip interval", "P95 flip interval", "Worst flip interval"};
    for (std::size_t i = 0; i < metrics_.size(); ++i) {
        auto* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_add_css_class(card, "metric-card");
        gtk_widget_set_size_request(card, 190, -1);
        gtk_box_append(GTK_BOX(card), label(card_titles[i], "metric-label"));
        metrics_[i] = label("Unavailable", "metric-value");
        if (!i) {
            gtk_widget_add_css_class(metrics_[i], "metric-primary");
            gtk_widget_set_tooltip_text(metrics_[i], "Completed runtime flips, not confirmed monitor scanout");
        }
        gtk_box_append(GTK_BOX(card), metrics_[i]);
        gtk_box_append(GTK_BOX(card), label("Last 1 s", "metric-scope"));
        gtk_flow_box_insert(GTK_FLOW_BOX(cards), card, -1);
    }
    gtk_box_append(GTK_BOX(live), cards);
    coverage_ = label("Sample coverage unavailable", "monitor-coverage");
    gtk_label_set_wrap(GTK_LABEL(coverage_), true);
    gtk_box_append(GTK_BOX(live), coverage_);

    auto* detail = panel();
    auto* detail_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    auto* detail_title = label("Frame detail", "monitor-heading");
    gtk_widget_set_hexpand(detail_title, true);
    gtk_box_append(GTK_BOX(detail_header), detail_title);
    const char* selections[] = {"Latest received frame", "Worst in last 1 s", nullptr};
    frame_selector_ = gtk_drop_down_new_from_strings(selections);
    gtk_widget_add_css_class(frame_selector_, "frame-picker");
    g_signal_connect(frame_selector_, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        try { self.inspected_.reset(); self.update_frame_detail(); gtk_widget_queue_draw(self.chart_); }
        catch (const std::exception& error) { show_error(self.root_, error.what()); }
        catch (...) { show_error(self.root_, "Could not select a frame"); }
    }), this);
    gtk_box_append(GTK_BOX(detail_header), frame_selector_);
    follow_live_ = gtk_button_new_with_label("Follow live");
    gtk_widget_add_css_class(follow_live_, "toolbar-action");
    gtk_widget_set_sensitive(follow_live_, false);
    g_signal_connect(follow_live_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        auto& self = *static_cast<ProfilerView*>(data);
        try { self.inspected_.reset(); self.update_frame_detail(); gtk_widget_queue_draw(self.chart_); }
        catch (const std::exception& error) { show_error(self.root_, error.what()); }
        catch (...) { show_error(self.root_, "Could not follow live frames"); }
    }), this);
    gtk_box_append(GTK_BOX(detail_header), follow_live_);
    gtk_box_append(GTK_BOX(detail), detail_header);
    auto* fields = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(fields), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(fields), true);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(fields), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(fields), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(fields), 20);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(fields), 12);
    gtk_widget_add_css_class(fields, "frame-fields");
    const char* field_titles[] = {"Frame", "Output stream", "Flip interval"};
    GtkWidget** values[] = {&frame_identity_, &frame_stream_, &frame_interval_};
    for (unsigned i = 0; i < 3; ++i) {
        auto* field = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
        gtk_widget_set_size_request(field, 180, -1);
        gtk_box_append(GTK_BOX(field), label(field_titles[i], "metric-label"));
        *values[i] = label("Unavailable", "frame-value");
        gtk_box_append(GTK_BOX(field), *values[i]);
        gtk_flow_box_insert(GTK_FLOW_BOX(fields), field, -1);
    }
    gtk_box_append(GTK_BOX(detail), fields);
    frame_scope_ = label("No frame selected", "monitor-frame-scope");
    gtk_label_set_wrap(GTK_LABEL(frame_scope_), true);
    gtk_box_append(GTK_BOX(detail), frame_scope_);
    gtk_box_append(GTK_BOX(live), detail);

    auto* chart_panel = panel();
    auto* chart_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    auto* chart_title = label("Flip intervals · last 60 s", "monitor-heading");
    gtk_widget_set_hexpand(chart_title, true);
    gtk_box_append(GTK_BOX(chart_header), chart_title);
    gtk_box_append(GTK_BOX(chart_header), label("Click a sample to inspect", "monitor-chart-hint"));
    gtk_box_append(GTK_BOX(chart_panel), chart_header);
    chart_ = gtk_drawing_area_new();
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(chart_), 180);
    gtk_widget_set_hexpand(chart_, true);
    gtk_widget_set_tooltip_text(chart_, "Intervals between completed runtime flips; gaps indicate incomplete data.");
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
    gtk_box_append(GTK_BOX(chart_panel), chart_);
    gtk_box_append(GTK_BOX(chart_panel), label("Runtime flip completion · game monotonic clock · gaps are not interpolated", "monitor-chart-hint"));
    gtk_box_append(GTK_BOX(live), chart_panel);

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
    g_signal_handlers_disconnect_by_data(monitoring_switch_, this);
    g_signal_handlers_disconnect_by_data(frame_selector_, this);
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
    const std::string connection = status.connected ? "Game connected" :
        status.port ? (status.error.empty() ? "Waiting for game connection" : "Game connection lost") : "Game bridge unavailable";
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
    updating_switch_ = true;
    gtk_switch_set_active(GTK_SWITCH(monitoring_switch_), snapshot.pending && snapshot.requested_enabled ?
        *snapshot.requested_enabled : snapshot.enabled);
    gtk_switch_set_state(GTK_SWITCH(monitoring_switch_), snapshot.enabled);
    updating_switch_ = false;
    gtk_widget_set_sensitive(monitoring_switch_, snapshot.connected && snapshot.supported && !snapshot.pending &&
        static_cast<bool>(request_monitoring_));
    gtk_widget_set_tooltip_text(monitoring_switch_, !snapshot.connected ? "Connect a game to change monitoring" :
        !snapshot.supported ? "Frame pacing measurements are unavailable for this session" :
        snapshot.pending ? "Waiting for the game to confirm monitoring state" : "Collect frame pacing measurements");
    for (const auto* style : {"active", "pending"}) gtk_widget_remove_css_class(monitoring_state_, style);
    if (snapshot.pending) gtk_widget_add_css_class(monitoring_state_, "pending");
    else if (snapshot.state == "active") gtk_widget_add_css_class(monitoring_state_, "active");

    const auto& stats = snapshot.stats;
    const std::optional<double> values[] = {stats.estimated_fps, stats.mean_interval_us,
        stats.p95_interval_us, stats.worst_interval_us};
    for (std::size_t i = 0; i < metrics_.size(); ++i) {
        const auto text = values[i] ? number(*values[i] / (i ? 1000.0 : 1.0)) + (i ? " ms" : " FPS") : "Unavailable";
        gtk_label_set_text(GTK_LABEL(metrics_[i]), text.c_str());
        if (retained) gtk_widget_add_css_class(metrics_[i], "stale");
        else gtk_widget_remove_css_class(metrics_[i], "stale");
    }
    const auto window = std::string("Last 1 s") + (retained ? " · retained observation" :
        snapshot.has_frames && !stats.complete ? " · partial window" : "");
    gtk_label_set_text(GTK_LABEL(window_state_), window.c_str());
    std::string coverage = "Sample coverage unavailable";
    if (snapshot.has_frames) {
        coverage = std::to_string(stats.sample_count) + " valid intervals";
        if (stats.coverage) coverage += " · " + std::to_string(stats.observed_samples) + " / " +
            std::to_string(stats.expected_samples) + " samples · " + number(*stats.coverage * 100) + "% coverage";
        else coverage += " · coverage unavailable";
        if (!stats.complete) coverage += " · incomplete window";
        if (stats.missing_samples) coverage += " · " + std::to_string(stats.missing_samples) + " missing samples";
        if (stats.sender_dropped_samples) coverage += " · " + std::to_string(stats.sender_dropped_samples) + " sender drops (epoch)";
        if (stats.packet_gaps) coverage += " · " + std::to_string(stats.packet_gaps) + " packet gaps (epoch)";
        if (snapshot.history_truncated) coverage += " · older detail discarded";
    }
    gtk_label_set_text(GTK_LABEL(coverage_), coverage.c_str());
    std::string freshness = "No frame data";
    if (snapshot.age_ms) freshness = (retained ? "Stale · last received " : "Last received ") +
        number(*snapshot.age_ms / 1000.0) + " s ago";
    gtk_label_set_text(GTK_LABEL(freshness_), freshness.c_str());
    if (retained) gtk_widget_add_css_class(freshness_, "stale");
    else gtk_widget_remove_css_class(freshness_, "stale");
    update_frame_detail();
    gtk_widget_queue_draw(chart_);
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
    gtk_widget_set_sensitive(follow_live_, inspected_.has_value());
    const auto frame = sample ? std::to_string(sample->frame) : "Unavailable";
    const auto stream = sample ? std::to_string(sample->output_handle) + " · generation " +
        std::to_string(sample->output_generation) : "Unavailable";
    const auto interval = sample && sample->flip_interval_us ? number(*sample->flip_interval_us / 1000.0) + " ms" : "Unavailable";
    gtk_label_set_text(GTK_LABEL(frame_identity_), frame.c_str());
    gtk_label_set_text(GTK_LABEL(frame_stream_), stream.c_str());
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
    gtk_label_set_text(GTK_LABEL(frame_scope_), scope.c_str());
}

void ProfilerView::inspect_chart(double x, double y) {
    const auto width = gtk_widget_get_width(chart_);
    const auto height = gtk_widget_get_height(chart_);
    const double plot_width = width - chart_left - chart_right;
    const double plot_height = height - chart_top - chart_bottom;
    if (monitoring_.samples.empty() || plot_width <= 0 || plot_height <= 0 || x < chart_left ||
        x > width - chart_right || y < chart_top || y > height - chart_bottom) return;
    const auto end = monitoring_.samples.back().timestamp_us;
    const double maximum = chart_maximum(monitoring_);
    const MonitoringSample* closest = nullptr;
    double distance = 24 * 24;
    for (const auto& sample : monitoring_.samples) {
        if (!sample.flip_interval_us || sample.timestamp_us > end || end - sample.timestamp_us > 60000000) continue;
        const double px = chart_left + plot_width * (1.0 - (end - sample.timestamp_us) / 60000000.0);
        const double py = chart_top + plot_height * (1.0 - *sample.flip_interval_us / 1000.0 / maximum);
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
        const double plot_width = width - chart_left - chart_right;
        const double plot_height = height - chart_top - chart_bottom;
        if (plot_width <= 0 || plot_height <= 0) return;
        const double maximum = chart_maximum(monitoring_);
        cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(context, 11);
        for (unsigned tick = 0; tick <= 4; ++tick) {
            const double y = chart_top + plot_height * tick / 4.0;
            cairo_set_source_rgb(context, 0.14, 0.24, 0.30);
            cairo_set_line_width(context, 1);
            cairo_move_to(context, chart_left, y);
            cairo_line_to(context, width - chart_right, y);
            cairo_stroke(context);
            cairo_set_source_rgb(context, 0.56, 0.68, 0.76);
            chart_text(context, chart_left - 10, y + 4, number(maximum * (1 - tick / 4.0), 0), true);
        }
        chart_text(context, 10, 12, "ms");
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
        const auto end = monitoring_.samples.back().timestamp_us;
        auto position = [&](const MonitoringSample& sample) {
            return chart_left + plot_width * (1.0 - (end - sample.timestamp_us) / 60000000.0);
        };
        const MonitoringSample* previous = nullptr;
        bool has_interval = false;
        cairo_save(context);
        cairo_rectangle(context, chart_left, chart_top - 1, plot_width + 1, plot_height + 2);
        cairo_clip(context);
        for (const auto& sample : monitoring_.samples) {
            if (sample.timestamp_us > end || end - sample.timestamp_us > 60000000) continue;
            if (!sample.flip_interval_us) { previous = nullptr; continue; }
            has_interval = true;
            const double x = position(sample);
            const double y = chart_top + plot_height * (1.0 - *sample.flip_interval_us / 1000.0 / maximum);
            const bool gap = sample.missing_before || (previous &&
                (sample.completed_flip_count != previous->completed_flip_count + 1 ||
                 sample.monitoring_epoch != previous->monitoring_epoch || sample.output_handle != previous->output_handle ||
                 sample.output_generation != previous->output_generation));
            if (gap && previous) {
                cairo_set_source_rgba(context, 0.97, 0.72, 0.28, 0.13);
                cairo_rectangle(context, position(*previous), chart_top, std::max(2.0, x - position(*previous)), plot_height);
                cairo_fill(context);
            }
            cairo_set_source_rgb(context, 0.0, 0.83, 0.92);
            cairo_set_line_width(context, 1.5);
            if (previous && !gap) {
                const double py = chart_top + plot_height * (1.0 - *previous->flip_interval_us / 1000.0 / maximum);
                cairo_move_to(context, position(*previous), py);
                cairo_line_to(context, x, y);
                cairo_stroke(context);
            } else {
                cairo_arc(context, x, y, 2, 0, 6.283185307);
                cairo_fill(context);
            }
            previous = &sample;
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
            chart_text(context, chart_left + 18, chart_top + plot_height / 2, "First completion received · waiting for a valid interval");
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
