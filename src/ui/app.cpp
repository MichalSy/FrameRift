// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library/library.hpp"
#include "library/steamgriddb.hpp"
#include "runtime/launch.hpp"
#include "runtime/bridge.hpp"
#include "runtime/mcp.hpp"
#include "runtime/session.hpp"
#include "ui/profiler.hpp"
#include "ui/hero.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace framerift {
namespace {

GtkWidget* label(const char* text, const char* style = nullptr) {
    auto* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    if (style) gtk_widget_add_css_class(widget, style);
    return widget;
}

GtkWidget* box(GtkOrientation orientation, int spacing = 0) {
    return gtk_box_new(orientation, spacing);
}

GtkWidget* button(const char* text, const char* style) {
    auto* widget = gtk_button_new_with_label(text);
    gtk_widget_add_css_class(widget, style);
    return widget;
}

void set_art(GtkWidget* picture, const std::string& path) {
    if (path.empty()) {
        gtk_picture_set_paintable(GTK_PICTURE(picture), nullptr);
    } else if (path.starts_with("resource://")) {
        gtk_picture_set_resource(GTK_PICTURE(picture), path.c_str() + 11);
    } else {
        gtk_picture_set_filename(GTK_PICTURE(picture), path.c_str());
    }
}

GtkWidget* picture(const std::string& path, const std::string& description) {
    auto* widget = gtk_picture_new();
    gtk_picture_set_can_shrink(GTK_PICTURE(widget), true);
    gtk_picture_set_content_fit(GTK_PICTURE(widget), GTK_CONTENT_FIT_COVER);
    gtk_picture_set_alternative_text(GTK_PICTURE(widget), description.c_str());
    gtk_widget_set_hexpand(widget, true);
    gtk_widget_set_vexpand(widget, true);
    set_art(widget, path);
    return widget;
}

std::string folded(const std::string& text) {
    auto* value = g_utf8_casefold(text.c_str(), -1);
    std::string result(value);
    g_free(value);
    return result;
}

std::vector<std::string> folded_titles(const std::vector<Game>& games) {
    std::vector<std::string> result;
    result.reserve(games.size());
    for (const auto& game : games) result.push_back(folded(game.title + " " + game.genre));
    return result;
}

std::uint16_t server_port(const char* name, std::uint16_t fallback) {
    const auto* value = g_getenv(name);
    if (!value) return fallback;
    const std::string_view text(value);
    unsigned int port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || port > 65535) {
        throw std::runtime_error(std::string(name) + " must be a port between 0 and 65535");
    }
    return static_cast<std::uint16_t>(port);
}

} // namespace

class Application {
public:
    explicit Application(const std::string& cache_path)
        : library_(cache_path), database_path_(cache_path), artwork_directory_(std::filesystem::path(cache_path).parent_path() / "artwork") {
        games_ = visible_games();
        search_text_ = folded_titles(games_);
        if (!games_.empty()) selected_id_ = games_.front().id;
    }

    ~Application() {
        if (bridge_source_) g_source_remove(bridge_source_);
    }

    int run(int argc, char** argv) {
        auto* app = gtk_application_new("io.github.framerift.FrameRift", G_APPLICATION_DEFAULT_FLAGS);
        g_signal_connect(app, "activate", G_CALLBACK(+[](GtkApplication* app, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.activate(app); });
        }), this);
        const int result = g_application_run(G_APPLICATION(app), argc, argv);
        g_object_unref(app);
        return result;
    }

private:
    enum class GameAction { Play, Rebuild };
    struct PendingAction {
        std::string game_id;
        GameAction action;
    };

    template<class Function>
    void guard(Function&& function) {
        try {
            function();
        } catch (const std::exception& error) {
            std::cerr << "FrameRift: " << error.what() << '\n';
            auto* dialog = gtk_alert_dialog_new("%s", error.what());
            if (window_) gtk_alert_dialog_show(dialog, GTK_WINDOW(window_));
            g_object_unref(dialog);
        }
    }

    const Game* selected() const {
        const auto it = std::find_if(games_.begin(), games_.end(), [&](const Game& game) {
            return game.id == selected_id_;
        });
        return it == games_.end() ? nullptr : &*it;
    }

    void activate(GtkApplication* app) {
        if (window_) {
            gtk_window_present(GTK_WINDOW(window_));
            return;
        }
        auto* css = gtk_css_provider_new();
        gtk_css_provider_load_from_resource(css, "/io/github/framerift/ui/style.css");
        gtk_style_context_add_provider_for_display(gdk_display_get_default(),
            GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", true, nullptr);

        game_cards_.clear();
        window_ = gtk_application_window_new(app);
        g_object_add_weak_pointer(G_OBJECT(window_), reinterpret_cast<gpointer*>(&window_));
        g_signal_connect(window_, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
            static_cast<Application*>(data)->pending_action_.reset();
            return false;
        }), this);
        gtk_window_set_title(GTK_WINDOW(window_), "FrameRift");
        gtk_window_set_default_size(GTK_WINDOW(window_), 1440, 900);
        pages_ = gtk_stack_new();
        gtk_stack_set_hhomogeneous(GTK_STACK(pages_), false);
        gtk_stack_set_vhomogeneous(GTK_STACK(pages_), false);
        gtk_stack_set_transition_type(GTK_STACK(pages_), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
        gtk_stack_set_transition_duration(GTK_STACK(pages_), 140);
        gtk_widget_set_vexpand(pages_, true);
        gtk_window_set_child(GTK_WINDOW(window_), pages_);
        gtk_window_set_titlebar(GTK_WINDOW(window_), header());
        gtk_stack_add_named(GTK_STACK(pages_), library_view(), "library");
        if (!profiler_) profiler_ = std::make_unique<ProfilerView>([this](bool enabled) {
            if (!bridge_) throw std::runtime_error("The game bridge is unavailable");
            bridge_->send_command("set_monitoring", enabled ? "{\"enabled\":true}" : "{\"enabled\":false}");
        });
        gtk_stack_add_named(GTK_STACK(pages_), profiler_->widget(), "profiler");
        start_servers();
        show_page("library");
        auto* keys = gtk_event_controller_key_new();
        g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType state, gpointer data) -> gboolean {
            auto& self = *static_cast<Application*>(data);
            if ((state & GDK_CONTROL_MASK) && (key == GDK_KEY_f || key == GDK_KEY_F)) {
                self.show_page("library");
                gtk_widget_grab_focus(self.search_);
                return true;
            }
            return false;
        }), this);
        gtk_widget_add_controller(window_, keys);
        select_game(selected_id_, highlight_cover_);
        gtk_window_present(GTK_WINDOW(window_));
        if (!library_.game_folders().empty()) refresh_library(app);
    }

    GtkWidget* header() {
        auto* handle = gtk_window_handle_new();
        auto* row = box(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_add_css_class(row, "topbar");
        gtk_window_handle_set_child(GTK_WINDOW_HANDLE(handle), row);
        auto* mark = gtk_image_new_from_resource("/io/github/framerift/ui/mark.svg");
        gtk_image_set_pixel_size(GTK_IMAGE(mark), 32);
        gtk_box_append(GTK_BOX(row), mark);
        gtk_box_append(GTK_BOX(row), label("FrameRift", "brand"));
        gtk_box_append(GTK_BOX(row), gtk_separator_new(GTK_ORIENTATION_VERTICAL));
        library_tab_ = navigation_tab("Library", "library");
        profiler_tab_ = navigation_tab("Profiler", "profiler");
        gtk_box_append(GTK_BOX(row), library_tab_);
        gtk_box_append(GTK_BOX(row), profiler_tab_);
        auto* spacer = box(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_hexpand(spacer, true);
        gtk_box_append(GTK_BOX(row), spacer);
        bridge_indicator_ = label("Game not connected", "bridge-indicator");
        g_object_add_weak_pointer(G_OBJECT(bridge_indicator_), reinterpret_cast<gpointer*>(&bridge_indicator_));
        gtk_widget_set_valign(bridge_indicator_, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), bridge_indicator_);
        search_ = gtk_search_entry_new();
        gtk_widget_add_css_class(search_, "game-search");
        gtk_editable_set_width_chars(GTK_EDITABLE(search_), 16);
        gtk_editable_set_max_width_chars(GTK_EDITABLE(search_), 28);
        gtk_widget_set_valign(search_, GTK_ALIGN_CENTER);
        g_object_set(search_, "placeholder-text", "Search games...", nullptr);
        g_signal_connect(search_, "search-changed", G_CALLBACK(+[](GtkSearchEntry*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] {
                self.refresh_filter();
            });
        }), this);
        gtk_box_append(GTK_BOX(row), search_);
        refresh_ = gtk_button_new_from_icon_name("view-refresh-symbolic");
        gtk_widget_add_css_class(refresh_, "toolbar-action");
        gtk_widget_set_valign(refresh_, GTK_ALIGN_CENTER);
        gtk_widget_set_sensitive(refresh_, !library_.game_folders().empty());
        gtk_widget_set_tooltip_text(refresh_, "Refresh library");
        g_signal_connect(refresh_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.refresh_library(gtk_window_get_application(GTK_WINDOW(self.window_)), true); });
        }), this);
        gtk_box_append(GTK_BOX(row), refresh_);
        folders_action_ = button("Settings", "toolbar-action");
        gtk_widget_set_valign(folders_action_, GTK_ALIGN_CENTER);
        g_signal_connect(folders_action_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.show_game_folders(); });
        }), this);
        gtk_box_append(GTK_BOX(row), folders_action_);
        auto* controls = gtk_window_controls_new(GTK_PACK_END);
        gtk_widget_add_css_class(controls, "window-buttons");
        gtk_box_append(GTK_BOX(row), controls);
        return handle;
    }

    void start_servers() {
        if (!bridge_) {
            try {
                bridge_ = std::make_unique<BridgeServer>(server_port("FRAMERIFT_UDP_PORT", 0));
            } catch (const std::exception& error) {
                server_error_ = error.what();
                g_printerr("FrameRift bridge: %s\n", error.what());
            }
        }
        if (!game_session_) {
            game_session_ = std::make_unique<GameSession>(library_, std::filesystem::path(database_path_).parent_path(),
                std::filesystem::path(g_get_user_data_dir()) / "framerift",
                G_APPLICATION(gtk_window_get_application(GTK_WINDOW(window_))), bridge_.get(),
                [this](const GameSessionStatus& status) { guard([&] { session_changed(status); }); },
                [this](std::string_view output) { profiler_->output(output); });
        }
        game_session_->set_bridge(bridge_.get());
        if (bridge_ && !mcp_) {
            try {
                mcp_ = std::make_unique<McpServer>(*bridge_, server_port("FRAMERIFT_MCP_PORT", 47653), game_session_.get());
                server_error_.clear();
            } catch (const std::exception& error) {
                server_error_ = error.what();
                g_printerr("FrameRift MCP: %s\n", error.what());
            }
        }
        update_bridge_status();
        if (!bridge_source_) {
            bridge_source_ = g_timeout_add(250, +[](gpointer data) -> gboolean {
                auto& self = *static_cast<Application*>(data);
                self.guard([&] { self.update_bridge_status(); });
                return G_SOURCE_CONTINUE;
            }, this);
        }
    }

    void update_bridge_status() {
        const auto status = bridge_ ? bridge_->status() : BridgeStatus{};
        if (bridge_indicator_) {
            gtk_label_set_text(GTK_LABEL(bridge_indicator_), status.connected ? "Game connected" :
                status.port ? "Game not connected" : "Game bridge unavailable");
            if (status.connected) gtk_widget_add_css_class(bridge_indicator_, "connected");
            else gtk_widget_remove_css_class(bridge_indicator_, "connected");
        }
        if (profiler_) profiler_->bridge_status(status, mcp_ ? mcp_->endpoint() : "", server_error_);
        const auto now = g_get_monotonic_time();
        if (profiler_ && now - monitoring_updated_ >= G_USEC_PER_SEC) {
            profiler_->monitoring(bridge_ ? bridge_->monitoring_snapshot() : MonitoringSnapshot{});
            monitoring_updated_ = now;
        }
    }

    GtkWidget* navigation_tab(const char* title, const char* page) {
        auto* tab = button(title, "nav-tab");
        gtk_widget_add_css_class(tab, "flat");
        g_object_set_data(G_OBJECT(tab), "page", const_cast<char*>(page));
        g_signal_connect(tab, "clicked", G_CALLBACK(+[](GtkButton* tab, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.show_page(static_cast<const char*>(g_object_get_data(G_OBJECT(tab), "page")));
        }), this);
        return tab;
    }

    void show_page(const char* page) {
        gtk_stack_set_visible_child_name(GTK_STACK(pages_), page);
        const bool library = g_str_equal(page, "library");
        gtk_widget_set_visible(search_, library);
        gtk_widget_set_visible(refresh_, library);
        for (auto* tab : {library_tab_, profiler_tab_}) {
            if ((tab == library_tab_) == library) gtk_widget_add_css_class(tab, "active");
            else gtk_widget_remove_css_class(tab, "active");
        }
    }

    GtkWidget* library_view() {
        library_view_ = box(GTK_ORIENTATION_VERTICAL);

        auto* hero = gtk_overlay_new();
        gtk_widget_set_vexpand(hero, false);
        gtk_widget_add_css_class(hero, "hero");
        hero_picture_ = hero_canvas();
        gtk_overlay_set_child(GTK_OVERLAY(hero), box(GTK_ORIENTATION_VERTICAL));
        gtk_overlay_add_overlay(GTK_OVERLAY(hero), hero_picture_);
        auto* scrim = box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(scrim, "hero-scrim");
        gtk_overlay_add_overlay(GTK_OVERLAY(hero), scrim);
        auto* hero_content = box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(hero_content, "hero-content");
        gtk_widget_set_valign(hero_content, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(hero_content, GTK_ALIGN_START);
        hero_title_ = label("", "hero-title");
        gtk_label_set_wrap(GTK_LABEL(hero_title_), true);
        gtk_label_set_max_width_chars(GTK_LABEL(hero_title_), 20);
        hero_logo_frame_ = gtk_overlay_new();
        gtk_widget_set_size_request(hero_logo_frame_, 280, 118);
        gtk_overlay_set_child(GTK_OVERLAY(hero_logo_frame_), box(GTK_ORIENTATION_VERTICAL));
        hero_logo_ = picture("", "Selected game logo");
        gtk_picture_set_content_fit(GTK_PICTURE(hero_logo_), GTK_CONTENT_FIT_CONTAIN);
        gtk_widget_set_halign(hero_logo_, GTK_ALIGN_START);
        gtk_overlay_add_overlay(GTK_OVERLAY(hero_logo_frame_), hero_logo_);
        gtk_box_append(GTK_BOX(hero_content), hero_logo_frame_);
        gtk_box_append(GTK_BOX(hero_content), hero_title_);
        auto* actions = box(GTK_ORIENTATION_HORIZONTAL, 14);
        gtk_widget_add_css_class(actions, "hero-actions");
        play_ = button("▶   Play", "play-button");
        gtk_widget_set_size_request(play_, 216, -1);
        g_signal_connect(play_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.prepare_selected(GameAction::Play); });
        }), this);
        gtk_box_append(GTK_BOX(actions), play_);
        rebuild_ = button("Rebuild", "toolbar-action");
        gtk_widget_add_css_class(rebuild_, "rebuild-button");
        g_signal_connect(rebuild_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.prepare_selected(GameAction::Rebuild); });
        }), this);
        gtk_box_append(GTK_BOX(actions), rebuild_);
        gtk_box_append(GTK_BOX(hero_content), actions);
        gtk_overlay_add_overlay(GTK_OVERLAY(hero), hero_content);
        gtk_box_append(GTK_BOX(library_view_), hero);

        auto* collection = box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_add_css_class(collection, "collection");
        auto* library_scroll = gtk_scrolled_window_new();
        gtk_widget_set_vexpand(library_scroll, true);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(library_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(library_scroll), collection);
        auto* viewport = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(library_scroll));
        gtk_scrollable_set_vscroll_policy(GTK_SCROLLABLE(viewport), GTK_SCROLL_NATURAL);
        gtk_box_append(GTK_BOX(library_view_), library_scroll);
        recent_section_ = box(GTK_ORIENTATION_VERTICAL);
        gtk_box_append(GTK_BOX(collection), recent_section_);
        gtk_box_append(GTK_BOX(recent_section_), section_header("Recently played"));
        auto* recent_row = box(GTK_ORIENTATION_HORIZONTAL, 18);
        auto* recent_scroll = gtk_scrolled_window_new();
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recent_scroll), GTK_POLICY_EXTERNAL, GTK_POLICY_NEVER);
        gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(recent_scroll), true);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(recent_scroll), recent_row);
        gtk_widget_add_css_class(recent_row, "recent-row");
        gtk_box_append(GTK_BOX(recent_section_), recent_scroll);
        int recent_count = 0;
        for (const auto& game : games_) {
            if (game.last_played <= 0) continue;
            gtk_box_append(GTK_BOX(recent_row), game_card(game, false));
            if (++recent_count == 5) break;
        }
        has_recent_ = recent_count > 0;
        gtk_widget_set_visible(recent_section_, has_recent_);
        auto* games_header = section_header("Your games");
        const char* sorts[] = {"Recently played", "Title A–Z", nullptr};
        sort_ = gtk_drop_down_new_from_strings(sorts);
        gtk_widget_add_css_class(sort_, "sort-picker");
        gtk_widget_set_valign(sort_, GTK_ALIGN_CENTER);
        g_signal_connect(sort_, "notify::selected", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { gtk_flow_box_invalidate_sort(GTK_FLOW_BOX(self.grid_)); });
        }), this);
        gtk_box_append(GTK_BOX(games_header), sort_);
        gtk_box_append(GTK_BOX(collection), games_header);
        grid_ = gtk_flow_box_new();
        gtk_widget_set_vexpand(grid_, false);
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(grid_), GTK_SELECTION_NONE);
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(grid_), true);
        gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(grid_), 1);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(grid_), std::max(1U, static_cast<guint>(games_.size())));
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(grid_), 24);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(grid_), 28);
        gtk_widget_add_css_class(grid_, "cover-grid");
        gtk_widget_set_halign(grid_, GTK_ALIGN_START);
        gtk_flow_box_set_sort_func(GTK_FLOW_BOX(grid_), +[](GtkFlowBoxChild* left, GtkFlowBoxChild* right, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            const auto& a = *static_cast<Game*>(g_object_get_data(G_OBJECT(gtk_flow_box_child_get_child(left)), "game"));
            const auto& b = *static_cast<Game*>(g_object_get_data(G_OBJECT(gtk_flow_box_child_get_child(right)), "game"));
            if (gtk_drop_down_get_selected(GTK_DROP_DOWN(self.sort_)) == 0 && a.last_played != b.last_played) {
                return a.last_played > b.last_played ? -1 : 1;
            }
            return g_utf8_collate(a.title.c_str(), b.title.c_str());
        }, this, nullptr);
        gtk_flow_box_set_filter_func(GTK_FLOW_BOX(grid_), +[](GtkFlowBoxChild* child, gpointer data) -> gboolean {
            auto& self = *static_cast<Application*>(data);
            const auto* game = static_cast<Game*>(g_object_get_data(G_OBJECT(gtk_flow_box_child_get_child(child)), "game"));
            return self.matches(*game);
        }, this, nullptr);
        gtk_box_append(GTK_BOX(collection), grid_);
        for (const auto& game : games_) gtk_flow_box_append(GTK_FLOW_BOX(grid_), game_card(game, true));
        empty_ = label(games_.empty() ? "Choose game folders to add your games." : "No games match your search.", "empty-state");
        gtk_widget_set_halign(empty_, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(collection), empty_);
        gtk_widget_set_visible(empty_, games_.empty());
        return library_view_;
    }

    GtkWidget* section_header(const char* text) {
        auto* row = box(GTK_ORIENTATION_HORIZONTAL, 16);
        gtk_widget_add_css_class(row, "section-header");
        auto* heading = label(text, "section-heading");
        gtk_widget_set_hexpand(heading, true);
        gtk_box_append(GTK_BOX(row), heading);
        return row;
    }

    GtkWidget* game_card(const Game& game, bool portrait) {
        auto* card = gtk_button_new();
        gtk_widget_add_css_class(card, "game-card");
        gtk_widget_set_tooltip_text(card, game.title.c_str());
        g_object_set_data(G_OBJECT(card), "game", const_cast<Game*>(&game));
        g_object_set_data(G_OBJECT(card), "portrait", GINT_TO_POINTER(portrait));
        gtk_widget_set_hexpand(card, false);
        gtk_widget_set_halign(card, GTK_ALIGN_START);
        gtk_widget_set_valign(card, GTK_ALIGN_START);
        auto* content = box(GTK_ORIENTATION_VERTICAL);
        gtk_button_set_child(GTK_BUTTON(card), content);
        auto* frame = box(GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_vexpand(frame, false);
        gtk_widget_set_size_request(frame, portrait ? 194 : 300, portrait ? 291 : 140);
        gtk_widget_add_css_class(frame, "art-frame");
        gtk_widget_set_overflow(frame, GTK_OVERFLOW_HIDDEN);
        auto* art = picture(portrait ? game.cover_path : game.landscape_path, game.title + " cover");
        auto* overlay = gtk_overlay_new();
        gtk_widget_set_vexpand(overlay, true);
        // Artwork must not dictate the card's natural size.
        gtk_overlay_set_child(GTK_OVERLAY(overlay), box(GTK_ORIENTATION_VERTICAL));
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), art);
        gtk_box_append(GTK_BOX(frame), overlay);
        gtk_box_append(GTK_BOX(content), frame);
        auto* caption = label(game.title.c_str(), "card-caption");
        gtk_label_set_ellipsize(GTK_LABEL(caption), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(caption), portrait ? 22 : 38);
        gtk_box_append(GTK_BOX(content), caption);
        g_signal_connect(card, "clicked", G_CALLBACK(+[](GtkButton* widget, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            const auto* game = static_cast<Game*>(g_object_get_data(G_OBJECT(widget), "game"));
            const bool portrait = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "portrait")) != 0;
            self.guard([&] { self.select_game(game->id, portrait); });
        }), this);
        game_cards_.push_back(card);
        return card;
    }

    bool matches(const Game& game) const {
        // Unicode case folding lets search work independently of title capitalization.
        const auto index = static_cast<std::size_t>(&game - games_.data());
        return search_text_[index].find(query_) != std::string::npos;
    }

    void refresh_filter() {
        query_ = folded(gtk_editable_get_text(GTK_EDITABLE(search_)));
        gtk_flow_box_invalidate_filter(GTK_FLOW_BOX(grid_));
        gtk_widget_set_visible(recent_section_, has_recent_ && query_.empty());
        gtk_label_set_text(GTK_LABEL(empty_), games_.empty() ? (loading_ ? "Scanning games and loading covers…" : "No games found. Choose a folder containing your games.") : "No games match your search.");
        gtk_widget_set_visible(empty_, std::none_of(games_.begin(), games_.end(), [&](const Game& game) {
            return matches(game);
        }));
    }

    void select_game(const std::string& id, bool highlight_cover = true) {
        highlight_cover_ = highlight_cover;
        selected_id_ = id;
        const auto* game = selected();
        update_game_actions();
        gtk_widget_set_visible(hero_logo_frame_, game && !game->logo_path.empty());
        gtk_widget_set_visible(hero_title_, !game || game->logo_path.empty());
        set_art(hero_logo_, game ? game->logo_path : "");
        set_hero_art(hero_picture_, game);
        if (!game) {
            gtk_label_set_text(GTK_LABEL(hero_title_), "Your next adventure");
            return;
        }
        gtk_picture_set_alternative_text(GTK_PICTURE(hero_logo_), game->title.c_str());
        gtk_label_set_text(GTK_LABEL(hero_title_), game->title.c_str());
        for (auto* card : game_cards_) {
            const auto* entry = static_cast<Game*>(g_object_get_data(G_OBJECT(card), "game"));
            const bool portrait = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(card), "portrait")) != 0;
            if (portrait == highlight_cover_ && entry->id == id) gtk_widget_add_css_class(card, "selected");
            else gtk_widget_remove_css_class(card, "selected");
        }
    }

    std::vector<Game> visible_games() const {
        return library_.visible_games();
    }

    void rebuild_library() {
        auto games = visible_games();
        auto search_text = folded_titles(games);
        const auto sort = gtk_drop_down_get_selected(GTK_DROP_DOWN(sort_));
        const bool showing_library = gtk_stack_get_visible_child(GTK_STACK(pages_)) == library_view_;
        gtk_stack_remove(GTK_STACK(pages_), library_view_);
        game_cards_.clear();
        games_ = std::move(games);
        search_text_ = std::move(search_text);
        if (!selected()) selected_id_ = games_.empty() ? "" : games_.front().id;
        gtk_stack_add_named(GTK_STACK(pages_), library_view(), "library");
        if (showing_library) show_page("library");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(sort_), sort);
        refresh_filter();
        select_game(selected_id_, highlight_cover_);
    }

    void show_game_folders() {
        if (folders_window_) {
            gtk_window_present(GTK_WINDOW(folders_window_));
            return;
        }
        if (saving_settings_) return;
        folders_window_ = gtk_window_new();
        g_object_add_weak_pointer(G_OBJECT(folders_window_), reinterpret_cast<gpointer*>(&folders_window_));
        gtk_window_set_title(GTK_WINDOW(folders_window_), "Library settings");
        gtk_window_set_transient_for(GTK_WINDOW(folders_window_), GTK_WINDOW(window_));
        gtk_window_set_modal(GTK_WINDOW(folders_window_), true);
        gtk_window_set_destroy_with_parent(GTK_WINDOW(folders_window_), true);
        gtk_window_set_default_size(GTK_WINDOW(folders_window_), 720, 700);
        auto* content = box(GTK_ORIENTATION_VERTICAL, 20);
        gtk_widget_add_css_class(content, "folder-settings");
        gtk_window_set_child(GTK_WINDOW(folders_window_), content);
        gtk_box_append(GTK_BOX(content), label("Game folders", "section-heading"));
        gtk_box_append(GTK_BOX(content), label("Choose one or more folders containing your games."));
        folder_list_ = box(GTK_ORIENTATION_VERTICAL, 12);
        g_object_add_weak_pointer(G_OBJECT(folder_list_), reinterpret_cast<gpointer*>(&folder_list_));
        gtk_box_append(GTK_BOX(content), folder_list_);
        gtk_box_append(GTK_BOX(content), label("Runtime", "section-heading"));
        const auto settings = library_.launch_settings();
        anyps5_entry_ = path_entry(content, "AnyPS5 folder", settings.anyps5_directory);
        gtk_box_append(GTK_BOX(content), label("Use the latest complete build directly from this folder."));
        gtk_box_append(GTK_BOX(content), label("Artwork", "section-heading"));
        auto* artwork_row = box(GTK_ORIENTATION_HORIZONTAL, 12);
        auto* artwork_label = label("SteamGridDB API key");
        gtk_widget_set_size_request(artwork_label, 160, -1);
        gtk_box_append(GTK_BOX(artwork_row), artwork_label);
        api_key_entry_ = gtk_password_entry_new();
        gtk_widget_set_hexpand(api_key_entry_, true);
        g_object_set(api_key_entry_, "placeholder-text", "Enter API key", nullptr);
        gtk_box_append(GTK_BOX(artwork_row), api_key_entry_);
        gtk_box_append(GTK_BOX(artwork_row), gtk_link_button_new_with_label(
            "https://www.steamgriddb.com/profile/preferences/api", "Get API key"));
        gtk_box_append(GTK_BOX(content), artwork_row);
        load_artwork_settings();
        add_folders_ = button("Add folders…", "toolbar-action");
        g_object_add_weak_pointer(G_OBJECT(add_folders_), reinterpret_cast<gpointer*>(&add_folders_));
        gtk_widget_set_halign(add_folders_, GTK_ALIGN_START);
        g_signal_connect(add_folders_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.choose_game_folders(); });
        }), this);
        auto* actions = box(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_hexpand(add_folders_, true);
        gtk_box_append(GTK_BOX(actions), add_folders_);
        auto* done = button(pending_action_ ? (pending_action_->action == GameAction::Rebuild ? "Save and rebuild" : "Save and play") : "Save", "toolbar-action");
        g_signal_connect(done, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.save_settings(); });
        }), this);
        g_signal_connect(folders_window_, "close-request", G_CALLBACK(+[](GtkWindow*, gpointer data) -> gboolean {
            auto& self = *static_cast<Application*>(data);
            if (self.saving_settings_) return true;
            self.pending_action_.reset();
            return false;
        }), this);
        gtk_box_append(GTK_BOX(actions), done);
        gtk_box_append(GTK_BOX(content), actions);
        rebuild_folder_list();
        gtk_window_present(GTK_WINDOW(folders_window_));
    }

    void load_artwork_settings() {
        auto* app = gtk_window_get_application(GTK_WINDOW(window_));
        g_application_hold(G_APPLICATION(app));
        auto* task = g_task_new(app, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto* entry = GTK_WIDGET(data);
            GError* error = nullptr;
            const bool configured = g_task_propagate_boolean(G_TASK(result), &error);
            g_object_set(entry, "placeholder-text", configured && !error ? "API key saved" : "Enter API key", nullptr);
            g_clear_error(&error);
            g_object_unref(entry);
            g_application_release(G_APPLICATION(source));
        }, g_object_ref(api_key_entry_));
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer, GCancellable*) {
            try {
                g_task_return_boolean(task, !read_steamgrid_api_key().empty());
            } catch (...) {
                g_task_return_new_error(task, g_quark_from_static_string("framerift-artwork"), 1,
                    "Could not read the saved API key");
            }
        });
        g_object_unref(task);
    }

    void finish_settings() {
        const auto action = pending_action_;
        pending_action_.reset();
        if (folders_window_) gtk_window_destroy(GTK_WINDOW(folders_window_));
        if (!window_) return;
        if (action) {
            if (std::none_of(games_.begin(), games_.end(), [&](const Game& game) { return game.id == action->game_id; })) {
                throw std::runtime_error("The game is no longer in your library. Choose a game and try again.");
            }
            select_game(action->game_id, highlight_cover_);
            prepare_selected(action->action);
        } else {
            refresh_library(gtk_window_get_application(GTK_WINDOW(window_)), true);
        }
    }

    void save_settings() {
        if (saving_settings_) return;
        library_.set_launch_settings(LaunchSettings{
            gtk_editable_get_text(GTK_EDITABLE(anyps5_entry_))});
        const std::string key(gtk_editable_get_text(GTK_EDITABLE(api_key_entry_)));
        if (key.empty()) {
            finish_settings();
            return;
        }
        saving_settings_ = true;
        gtk_widget_set_sensitive(gtk_window_get_child(GTK_WINDOW(folders_window_)), false);
        auto* app = gtk_window_get_application(GTK_WINDOW(window_));
        g_application_hold(G_APPLICATION(app));
        auto* task = g_task_new(app, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            GError* error = nullptr;
            g_task_propagate_boolean(G_TASK(result), &error);
            self.saving_settings_ = false;
            self.guard([&] {
                if (error) {
                    if (self.folders_window_) gtk_widget_set_sensitive(
                        gtk_window_get_child(GTK_WINDOW(self.folders_window_)), true);
                    throw std::runtime_error(error->message);
                }
                self.finish_settings();
            });
            g_clear_error(&error);
            g_application_release(G_APPLICATION(source));
        }, this);
        g_task_set_task_data(task, new std::string(key), +[](gpointer data) { delete static_cast<std::string*>(data); });
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
            try {
                save_steamgrid_api_key(*static_cast<std::string*>(data));
                g_task_return_boolean(task, true);
            } catch (const std::exception& error) {
                g_task_return_new_error(task, g_quark_from_static_string("framerift-artwork"), 1, "%s", error.what());
            }
        });
        g_object_unref(task);
    }

    GtkWidget* path_entry(GtkWidget* content, const char* title, const std::string& value) {
        auto* row = box(GTK_ORIENTATION_HORIZONTAL, 12);
        auto* name = label(title);
        gtk_widget_set_size_request(name, 160, -1);
        gtk_box_append(GTK_BOX(row), name);
        auto* entry = gtk_entry_new();
        gtk_widget_set_hexpand(entry, true);
        gtk_editable_set_text(GTK_EDITABLE(entry), value.c_str());
        gtk_box_append(GTK_BOX(row), entry);
        gtk_box_append(GTK_BOX(content), row);
        return entry;
    }

    void rebuild_folder_list() {
        if (!folder_list_) return;
        while (auto* child = gtk_widget_get_first_child(folder_list_)) gtk_box_remove(GTK_BOX(folder_list_), child);
        const auto folders = library_.game_folders();
        if (folders.empty()) gtk_box_append(GTK_BOX(folder_list_), label("No folders selected."));
        for (const auto& path : folders) {
            auto* row = box(GTK_ORIENTATION_HORIZONTAL, 12);
            auto* name = label(path.c_str());
            gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
            gtk_widget_set_hexpand(name, true);
            gtk_widget_set_tooltip_text(name, path.c_str());
            gtk_box_append(GTK_BOX(row), name);
            auto* remove = gtk_button_new_from_icon_name("list-remove-symbolic");
            gtk_widget_set_tooltip_text(remove, "Remove folder");
            g_object_set_data_full(G_OBJECT(remove), "folder", g_strdup(path.c_str()), g_free);
            g_signal_connect(remove, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
                auto& self = *static_cast<Application*>(data);
                self.guard([&] {
                    auto folders = self.library_.game_folders();
                    const std::string path(static_cast<const char*>(g_object_get_data(G_OBJECT(button), "folder")));
                    std::erase(folders, path);
                    self.library_.set_game_folders(folders);
                    self.rebuild_folder_list();
                    self.rebuild_library();
                    gtk_widget_set_sensitive(self.refresh_, !folders.empty());
                });
            }), this);
            gtk_box_append(GTK_BOX(row), remove);
            gtk_box_append(GTK_BOX(folder_list_), row);
        }
        gtk_widget_set_sensitive(folder_list_, !loading_);
        if (add_folders_) gtk_widget_set_sensitive(add_folders_, !loading_);
    }

    void choose_game_folders() {
        auto* dialog = gtk_file_dialog_new();
        gtk_file_dialog_set_title(dialog, "Add game folders");
        const auto folders = library_.game_folders();
        const auto initial = folders.empty() ? std::filesystem::path(g_get_home_dir()) / "Games" : std::filesystem::path(folders.front());
        auto* folder = g_file_new_for_path(initial.string().c_str());
        gtk_file_dialog_set_initial_folder(dialog, folder);
        g_object_unref(folder);
        auto* app = gtk_window_get_application(GTK_WINDOW(window_));
        g_application_hold(G_APPLICATION(app));
        g_object_set_data_full(G_OBJECT(dialog), "application", g_object_ref(app), g_object_unref);
        gtk_file_dialog_select_multiple_folders(dialog, GTK_WINDOW(folders_window_), nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer data) {
                auto& self = *static_cast<Application*>(data);
                GError* error = nullptr;
                auto* selected = gtk_file_dialog_select_multiple_folders_finish(GTK_FILE_DIALOG(source), result, &error);
                self.guard([&] {
                    if (error) {
                        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED)) throw std::runtime_error(error->message);
                        return;
                    }
                    auto folders = self.library_.game_folders();
                    for (guint index = 0; index < g_list_model_get_n_items(selected); ++index) {
                        auto* file = G_FILE(g_list_model_get_item(selected, index));
                        auto* path = g_file_get_path(file);
                        if (path) folders.emplace_back(path);
                        g_free(path);
                        g_object_unref(file);
                    }
                    self.library_.set_game_folders(folders);
                    self.rebuild_folder_list();
                    if (self.window_) {
                        self.rebuild_library();
                        self.refresh_library(gtk_window_get_application(GTK_WINDOW(self.window_)));
                    }
                });
                if (selected) g_object_unref(selected);
                g_clear_error(&error);
                auto* app = static_cast<GApplication*>(g_object_get_data(source, "application"));
                g_application_release(app);
            }, this);
        g_object_unref(dialog);
    }

    void update_game_actions() {
        if (!play_ || !rebuild_) return;
        const auto* game = selected();
#ifdef __linux__
        const bool available = game && !game->is_demo && !game->installation_path.empty();
#else
        const bool available = false;
        (void)game;
#endif
        const auto* session = game_session_ ? &game_session_->status() : nullptr;
        const bool busy = session && session->busy();
        const bool rebuilding = session && session->rebuilding;
        gtk_widget_set_sensitive(play_, available && !busy);
        gtk_widget_set_sensitive(rebuild_, available && !busy);
        const auto settings = library_.launch_settings();
        const bool configured = !settings.anyps5_directory.empty();
        gtk_widget_set_tooltip_text(play_, configured ? "Launch with the latest AnyPS5 build" : "Configure the AnyPS5 folder in Settings");
        gtk_widget_set_tooltip_text(rebuild_, "Rebuild generated game code; keep saves and runtime data");
        const char* launch_label = "▶   Play";
        if (busy && !rebuilding) {
            launch_label = session->state == "preparing" ? "Preparing…" : session->state == "stopping" ? "Stopping…" : "Running";
        }
        gtk_button_set_label(GTK_BUTTON(play_), launch_label);
        gtk_button_set_label(GTK_BUTTON(rebuild_), busy && rebuilding ? "Rebuilding…" : "Rebuild");
    }

    void session_changed(const GameSessionStatus& status) {
        if (status.state == "preparing") {
            profiler_->preparing(status.title, status.rebuilding);
            if (window_) show_page("profiler");
        } else if (status.state == "running") {
            profiler_->running(status.prepared);
            if (window_) rebuild_library();
        } else if (status.state == "stopping") {
            profiler_->finished("Stopping…");
        } else if (status.state == "rebuilt") {
            profiler_->rebuilt(status.prepared);
        } else if (status.state == "failed") {
            if (status.exit_code >= 0 || status.term_signal)
                profiler_->failed(status.error);
            else
                profiler_->failed(status.error, status.prepared.conversion_log_path);
            g_printerr("FrameRift game: %s\n", status.error.c_str());
        } else if (status.state == "stopped") {
            profiler_->finished("Stopped");
        } else if (status.state == "exited") {
            profiler_->finished("Exited normally");
        }
        update_bridge_status();
        if (window_) update_game_actions();
    }

    void prepare_selected(GameAction action) {
        if (!game_session_ || game_session_->status().busy()) return;
        const auto* game = selected();
        if (!game || game->is_demo || game->installation_path.empty()) return;
        const auto settings = library_.launch_settings();
        if (settings.anyps5_directory.empty()) {
            pending_action_ = PendingAction{game->id, action};
            show_game_folders();
            return;
        }
        game_session_->start(game->id, action == GameAction::Rebuild);
    }

    struct Refresh {
        std::string database;
        std::vector<std::string> folders;
        std::filesystem::path artwork;
        bool refresh_heroes = false;
    };

    void refresh_library(GtkApplication* app, bool refresh_heroes = false) {
        if (loading_) {
            refresh_pending_ = true;
            refresh_heroes_pending_ = refresh_heroes_pending_ || refresh_heroes;
            return;
        }
        refresh_pending_ = false;
        refresh_heroes = refresh_heroes || refresh_heroes_pending_;
        refresh_heroes_pending_ = false;
        auto folders = library_.game_folders();
        if (folders.empty()) return;
        auto refresh = std::make_unique<Refresh>(Refresh{database_path_, std::move(folders), artwork_directory_, refresh_heroes});
        loading_ = true;
        gtk_widget_set_sensitive(refresh_, false);
        gtk_widget_set_sensitive(folders_action_, false);
        gtk_widget_set_tooltip_text(refresh_, "Refreshing library…");
        rebuild_folder_list();
        refresh_filter();
        g_application_hold(G_APPLICATION(app));
        auto* task = g_task_new(app, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            GError* error = nullptr;
            std::unique_ptr<std::vector<std::string>> warnings(static_cast<std::vector<std::string>*>(g_task_propagate_pointer(G_TASK(result), &error)));
            self.loading_ = false;
            self.guard([&] {
                if (self.window_) {
                    gtk_widget_set_sensitive(self.refresh_, !self.library_.game_folders().empty());
                    gtk_widget_set_sensitive(self.folders_action_, true);
                    gtk_widget_set_tooltip_text(self.refresh_, "Refresh library");
                    self.rebuild_library();
                    self.rebuild_folder_list();
                }
                if (error) throw std::runtime_error(error->message);
                for (const auto& warning : *warnings) std::cerr << "FrameRift: " << warning << '\n';
            });
            g_clear_error(&error);
            if (self.refresh_pending_ && self.window_) {
                self.refresh_pending_ = false;
                self.guard([&] { self.refresh_library(GTK_APPLICATION(source)); });
            }
            g_application_release(G_APPLICATION(source));
        }, this);
        g_task_set_task_data(task, refresh.release(), +[](gpointer data) { delete static_cast<Refresh*>(data); });
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
            const auto& refresh = *static_cast<Refresh*>(data);
            try {
                Library library(refresh.database);
                auto scan = scan_game_folders(refresh.folders);
                library.cache_scanned(scan.games);
                auto games = library.games();
                std::erase_if(games, [&](const Game& game) {
                    return std::none_of(scan.games.begin(), scan.games.end(), [&](const Game& scanned) { return scanned.id == game.id; });
                });
                if (!games.empty()) {
                    auto artwork = download_game_artwork(games, read_steamgrid_api_key(), refresh.artwork, scan.warnings,
                        refresh.refresh_heroes);
                    std::erase_if(artwork, [](const Game& game) { return game.cover_path.empty() || game.steamgriddb_id <= 0; });
                    library.cache_artwork(artwork);
                }
                auto warnings = std::make_unique<std::vector<std::string>>(std::move(scan.warnings));
                g_task_return_pointer(task, warnings.release(), +[](gpointer data) { delete static_cast<std::vector<std::string>*>(data); });
            } catch (const std::exception& error) {
                g_task_return_new_error(task, g_quark_from_static_string("framerift-library"), 1, "%s", error.what());
            }
        });
        g_object_unref(task);
    }

    Library library_;
    std::vector<Game> games_;
    std::vector<std::string> search_text_;
    std::string selected_id_;
    std::string query_;
    bool has_recent_ = false;
    GtkWidget* window_ = nullptr;
    GtkWidget* pages_ = nullptr;
    GtkWidget* library_tab_ = nullptr;
    GtkWidget* profiler_tab_ = nullptr;
    std::unique_ptr<ProfilerView> profiler_;
    std::unique_ptr<BridgeServer> bridge_;
    std::unique_ptr<GameSession> game_session_;
    std::unique_ptr<McpServer> mcp_;
    guint bridge_source_ = 0;
    gint64 monitoring_updated_ = 0;
    GtkWidget* bridge_indicator_ = nullptr;
    std::string server_error_;
    GtkWidget* refresh_ = nullptr;
    GtkWidget* folders_action_ = nullptr;
    GtkWidget* folders_window_ = nullptr;
    GtkWidget* folder_list_ = nullptr;
    GtkWidget* add_folders_ = nullptr;
    GtkWidget* anyps5_entry_ = nullptr;
    GtkWidget* api_key_entry_ = nullptr;
    bool saving_settings_ = false;
    GtkWidget* play_ = nullptr;
    GtkWidget* rebuild_ = nullptr;
    std::optional<PendingAction> pending_action_;
    bool highlight_cover_ = true;
    bool loading_ = false;
    bool refresh_pending_ = false;
    bool refresh_heroes_pending_ = false;
    std::string database_path_;
    std::filesystem::path artwork_directory_;
    GtkWidget* search_ = nullptr;
    GtkWidget* sort_ = nullptr;
    GtkWidget* library_view_ = nullptr;
    GtkWidget* hero_picture_ = nullptr;
    GtkWidget* hero_title_ = nullptr;
    GtkWidget* hero_logo_ = nullptr;
    GtkWidget* hero_logo_frame_ = nullptr;
    GtkWidget* recent_section_ = nullptr;
    GtkWidget* grid_ = nullptr;
    GtkWidget* empty_ = nullptr;
    std::vector<GtkWidget*> game_cards_;
};

} // namespace framerift

int main(int argc, char** argv) {
    try {
        const auto cache = std::filesystem::path(g_get_user_cache_dir()) / "framerift" / "games.sqlite3";
        framerift::Application app(cache.string());
        return app.run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "FrameRift: " << error.what() << '\n';
        return 1;
    }
}
