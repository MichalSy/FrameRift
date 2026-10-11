// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "profiler.hpp"

#include <vte/vte.h>

#include <exception>
#include <fstream>
#include <algorithm>

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

} // namespace

ProfilerView::ProfilerView() {
    root_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
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

    auto* bridge = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(bridge, "bridge-details");
    auto* connection = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    bridge_connection_ = label("Waiting for game connection", "bridge-connection");
    gtk_widget_set_hexpand(bridge_connection_, true);
    gtk_box_append(GTK_BOX(connection), bridge_connection_);
    bridge_activity_ = label("", "bridge-endpoint");
    gtk_box_append(GTK_BOX(connection), bridge_activity_);
    gtk_box_append(GTK_BOX(bridge), connection);
    bridge_endpoint_ = label("", "bridge-endpoint");
    mcp_endpoint_ = label("", "bridge-endpoint");
    for (auto* endpoint : {bridge_endpoint_, mcp_endpoint_}) {
        gtk_label_set_selectable(GTK_LABEL(endpoint), true);
        gtk_box_append(GTK_BOX(bridge), endpoint);
    }
    gtk_box_append(GTK_BOX(root_), bridge);

    auto* console = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_vexpand(console, true);
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    auto* heading = label("Console", "section-heading");
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
            static_cast<ProfilerView*>(data)->copy_selection();
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
    gtk_box_append(GTK_BOX(root_), console);
}

ProfilerView::~ProfilerView() {
    g_signal_handlers_disconnect_by_data(follow_, this);
    g_signal_handlers_disconnect_by_data(copy_, this);
    g_signal_handlers_disconnect_by_data(open_, this);
    g_signal_handlers_disconnect_by_data(keys_, this);
    g_signal_handlers_disconnect_by_data(view_, this);
    g_signal_handlers_disconnect_by_data(adjustment_, this);
    g_object_unref(root_);
}

void ProfilerView::bridge_status(const BridgeStatus& status, const std::string& mcp_endpoint, const std::string& error) {
    const auto connection = status.connected ? "Game connected · " + status.game_id :
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
    const auto activity = detail.empty() ? "Frame " + std::to_string(status.frame) + " · " +
        std::to_string(status.received_events) + " events · " + std::to_string(status.missing_events) + " missing packets" : detail;
    gtk_label_set_text(GTK_LABEL(bridge_activity_), activity.c_str());
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
