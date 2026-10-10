// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "library/library.hpp"
#include "library/steamgriddb.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
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

struct HeroArtwork {
    cairo_surface_t* image = nullptr;
    cairo_surface_t* previous = nullptr;
    std::string path;
    gint64 started = 0;
    guint tick = 0;

    ~HeroArtwork() {
        if (image) cairo_surface_destroy(image);
        if (previous) cairo_surface_destroy(previous);
    }
};

GtkWidget* hero_canvas() {
    auto* widget = gtk_drawing_area_new();
    auto* artwork = new HeroArtwork;
    g_object_set_data(G_OBJECT(widget), "artwork", artwork);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget), +[](GtkDrawingArea*, cairo_t* context, int width, int height, gpointer data) {
        const auto& artwork = *static_cast<HeroArtwork*>(data);
        cairo_rectangle(context, 0, 0, width, height);
        cairo_clip(context);
        const auto paint = [&](cairo_surface_t* image, double opacity) {
            if (!image) return;
            const int image_width = cairo_image_surface_get_width(image);
            const int image_height = cairo_image_surface_get_height(image);
            const double scale = std::max(static_cast<double>(width) / image_width, static_cast<double>(height) / image_height);
            cairo_save(context);
            // Keep the upper artwork visible when a wide banner crops the canvas.
            cairo_translate(context, (width - image_width * scale) / 2, 0);
            cairo_scale(context, scale, scale);
            cairo_set_source_surface(context, image, 0, 0);
            cairo_paint_with_alpha(context, opacity);
            cairo_restore(context);
        };
        const double progress = artwork.previous ? std::clamp((g_get_monotonic_time() - artwork.started) / 180000.0, 0.0, 1.0) : 1.0;
        paint(artwork.previous, 1);
        paint(artwork.image, progress * progress * (3 - 2 * progress));
    }, artwork, +[](gpointer data) { delete static_cast<HeroArtwork*>(data); });
    return widget;
}

void set_hero_art(GtkWidget* widget, const Game* game) {
    auto& artwork = *static_cast<HeroArtwork*>(g_object_get_data(G_OBJECT(widget), "artwork"));
    auto path = game ? game->hero_path : std::string{};
    if (artwork.path == path) return;
    auto* texture = game && !game->hero_path.empty() ? gdk_texture_new_from_filename(game->hero_path.c_str(), nullptr) : nullptr;
    cairo_surface_t* image = nullptr;
    if (texture) {
        image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
        auto* downloader = gdk_texture_downloader_new(texture);
        gdk_texture_downloader_set_format(downloader, GDK_MEMORY_DEFAULT);
        if (cairo_surface_status(image) == CAIRO_STATUS_SUCCESS) {
            gdk_texture_downloader_download_into(downloader, cairo_image_surface_get_data(image), cairo_image_surface_get_stride(image));
            cairo_surface_mark_dirty(image);
        }
        gdk_texture_downloader_free(downloader);
        g_object_unref(texture);
        if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(image);
            throw std::runtime_error("Could not prepare game artwork");
        }
    }
    if (artwork.tick) gtk_widget_remove_tick_callback(widget, artwork.tick);
    artwork.tick = 0;
    if (artwork.previous) cairo_surface_destroy(artwork.previous);
    artwork.previous = artwork.image;
    artwork.image = image;
    artwork.path = std::move(path);
    gboolean animate = false;
    g_object_get(gtk_widget_get_settings(widget), "gtk-enable-animations", &animate, nullptr);
    if (artwork.previous && artwork.image && animate) {
        artwork.started = g_get_monotonic_time();
        artwork.tick = gtk_widget_add_tick_callback(widget, +[](GtkWidget* widget, GdkFrameClock* clock, gpointer data) -> gboolean {
            auto& artwork = *static_cast<HeroArtwork*>(data);
            gtk_widget_queue_draw(widget);
            if (gdk_frame_clock_get_frame_time(clock) - artwork.started < 180000) return G_SOURCE_CONTINUE;
            cairo_surface_destroy(artwork.previous);
            artwork.previous = nullptr;
            artwork.tick = 0;
            return G_SOURCE_REMOVE;
        }, &artwork, nullptr);
    } else if (artwork.previous) {
        cairo_surface_destroy(artwork.previous);
        artwork.previous = nullptr;
    }
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL,
        game ? game->title.c_str() : "Selected game artwork", -1);
    gtk_widget_queue_draw(widget);
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

} // namespace

class Application {
public:
    explicit Application(const std::string& cache_path)
        : library_(cache_path), artwork_directory_(std::filesystem::path(cache_path).parent_path() / "artwork") {
        games_ = library_.games();
        search_text_ = folded_titles(games_);
        if (!games_.empty()) selected_id_ = games_.front().id;
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
        gtk_window_set_title(GTK_WINDOW(window_), "FrameRift");
        gtk_window_set_default_size(GTK_WINDOW(window_), 1440, 900);
        root_ = box(GTK_ORIENTATION_VERTICAL);
        gtk_window_set_child(GTK_WINDOW(window_), root_);
        gtk_window_set_titlebar(GTK_WINDOW(window_), header());
        gtk_box_append(GTK_BOX(root_), library_view());
        auto* keys = gtk_event_controller_key_new();
        g_signal_connect(keys, "key-pressed", G_CALLBACK(+[](GtkEventControllerKey*, guint key, guint, GdkModifierType state, gpointer data) -> gboolean {
            auto& self = *static_cast<Application*>(data);
            if ((state & GDK_CONTROL_MASK) && (key == GDK_KEY_f || key == GDK_KEY_F)) {
                gtk_widget_grab_focus(self.search_);
                return true;
            }
            return false;
        }), this);
        gtk_widget_add_controller(window_, keys);
        select_game(selected_id_, highlight_cover_);
        gtk_window_present(GTK_WINDOW(window_));
        if (games_.empty()) load_examples(app);
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
        auto* navigation = button("Library", "nav-tab");
        gtk_widget_add_css_class(navigation, "flat");
        gtk_box_append(GTK_BOX(row), navigation);
        auto* spacer = box(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_hexpand(spacer, true);
        gtk_box_append(GTK_BOX(row), spacer);
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
        gtk_widget_set_sensitive(refresh_, has_api_key());
        gtk_widget_set_tooltip_text(refresh_, "Refresh library");
        g_signal_connect(refresh_, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            self.guard([&] { self.load_examples(gtk_window_get_application(GTK_WINDOW(self.window_))); });
        }), this);
        gtk_box_append(GTK_BOX(row), refresh_);
        auto* controls = gtk_window_controls_new(GTK_PACK_END);
        gtk_widget_add_css_class(controls, "window-buttons");
        gtk_box_append(GTK_BOX(row), controls);
        return handle;
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
        auto* play = button("▶   Play", "play-button");
        gtk_widget_set_size_request(play, 216, -1);
        gtk_widget_set_sensitive(play, false);
        gtk_widget_set_tooltip_text(play, "Game launch is not available yet.");
        gtk_box_append(GTK_BOX(actions), play);
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
        empty_ = label(games_.empty() ? "No games yet." : "No games match your search.", "empty-state");
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
        gtk_label_set_text(GTK_LABEL(empty_), games_.empty() ? (loading_ ? "Loading games…" : "No games yet.") : "No games match your search.");
        gtk_widget_set_visible(empty_, std::none_of(games_.begin(), games_.end(), [&](const Game& game) {
            return matches(game);
        }));
    }

    void select_game(const std::string& id, bool highlight_cover = true) {
        highlight_cover_ = highlight_cover;
        selected_id_ = id;
        const auto* game = selected();
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

    bool has_api_key() const {
        const auto* key = g_getenv("STEAMGRIDDB_API_KEY");
        return key && *key;
    }

    void rebuild_library() {
        auto games = library_.games();
        auto search_text = folded_titles(games);
        const auto sort = gtk_drop_down_get_selected(GTK_DROP_DOWN(sort_));
        gtk_box_remove(GTK_BOX(root_), library_view_);
        game_cards_.clear();
        games_ = std::move(games);
        search_text_ = std::move(search_text);
        if (!selected()) selected_id_ = games_.empty() ? "" : games_.front().id;
        gtk_box_append(GTK_BOX(root_), library_view());
        gtk_drop_down_set_selected(GTK_DROP_DOWN(sort_), sort);
        refresh_filter();
        select_game(selected_id_, highlight_cover_);
    }

    struct Download {
        std::string key;
        std::filesystem::path directory;
    };

    void load_examples(GtkApplication* app) {
        if (loading_ || !has_api_key()) return;
        auto download = std::make_unique<Download>(Download{g_getenv("STEAMGRIDDB_API_KEY"), artwork_directory_});
        loading_ = true;
        gtk_widget_set_sensitive(refresh_, false);
        gtk_widget_set_tooltip_text(refresh_, "Refreshing library…");
        refresh_filter();
        g_application_hold(G_APPLICATION(app));
        auto* task = g_task_new(app, nullptr, +[](GObject* source, GAsyncResult* result, gpointer data) {
            auto& self = *static_cast<Application*>(data);
            GError* error = nullptr;
            std::unique_ptr<std::vector<Game>> games(static_cast<std::vector<Game>*>(g_task_propagate_pointer(G_TASK(result), &error)));
            self.loading_ = false;
            self.guard([&] {
                if (self.window_) {
                    gtk_widget_set_sensitive(self.refresh_, self.has_api_key());
                    gtk_widget_set_tooltip_text(self.refresh_, "Refresh library");
                    self.refresh_filter();
                }
                if (error) throw std::runtime_error(error->message);
                self.library_.cache_demo(*games);
                if (self.window_) self.rebuild_library();
            });
            g_clear_error(&error);
            g_application_release(G_APPLICATION(source));
        }, this);
        g_task_set_task_data(task, download.release(), +[](gpointer data) { delete static_cast<Download*>(data); });
        g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable*) {
            const auto& download = *static_cast<Download*>(data);
            try {
                auto games = std::make_unique<std::vector<Game>>(download_demo_games(download.key, download.directory));
                g_task_return_pointer(task, games.release(), +[](gpointer data) { delete static_cast<std::vector<Game>*>(data); });
            } catch (const std::exception& error) {
                g_task_return_new_error(task, g_quark_from_static_string("framerift-artwork"), 1, "%s", error.what());
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
    GtkWidget* root_ = nullptr;
    GtkWidget* refresh_ = nullptr;
    bool highlight_cover_ = true;
    bool loading_ = false;
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
