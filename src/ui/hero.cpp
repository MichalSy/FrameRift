// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#include "ui/hero.hpp"
#include "library/library.hpp"

#include <webp/demux.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace framerift {
namespace {

struct HeroArtwork {
    cairo_surface_t* image = nullptr;
    cairo_surface_t* previous = nullptr;
    std::string path;
    std::unique_ptr<gchar, decltype(&g_free)> bytes{nullptr, g_free};
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder{nullptr, WebPAnimDecoderDelete};
    WebPAnimInfo animation{};
    unsigned loops = 0;
    gint64 elapsed_us = 0;
    gint64 frame_end_ms = 0;
    gint64 last_tick = 0;
    gint64 started = 0;
    guint tick = 0;

    ~HeroArtwork() {
        if (image) cairo_surface_destroy(image);
        if (previous) cairo_surface_destroy(previous);
    }

    bool next_frame() {
        std::uint8_t* pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp)) return false;
        cairo_surface_flush(image);
        auto* destination = cairo_image_surface_get_data(image);
        const int stride = cairo_image_surface_get_stride(image);
        for (unsigned y = 0; y < animation.canvas_height; ++y) {
            const auto* row = pixels + y * animation.canvas_width * 4;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
            std::memcpy(destination + y * stride, row, animation.canvas_width * 4);
#else
            auto* output = reinterpret_cast<std::uint32_t*>(destination + y * stride);
            for (unsigned x = 0; x < animation.canvas_width; ++x) {
                const auto* pixel = row + x * 4;
                output[x] = (std::uint32_t(pixel[3]) << 24) | (std::uint32_t(pixel[2]) << 16) |
                    (std::uint32_t(pixel[1]) << 8) | pixel[0];
            }
#endif
        }
        cairo_surface_mark_dirty(image);
        frame_end_ms = std::max<gint64>(timestamp, frame_end_ms + 1);
        return true;
    }

    void stop_animation() {
        decoder.reset();
        bytes.reset();
    }

    bool advance(gint64 now) {
        if (!decoder) return false;
        if (last_tick) elapsed_us += std::clamp<gint64>(now - last_tick, 0, 100000);
        last_tick = now;
        bool changed = false;
        for (int frames = 0; decoder && elapsed_us >= frame_end_ms * 1000 && frames < 4; ++frames) {
            if (!WebPAnimDecoderHasMoreFrames(decoder.get())) {
                ++loops;
                if (animation.loop_count && loops >= animation.loop_count) {
                    stop_animation();
                    break;
                }
                elapsed_us -= frame_end_ms * 1000;
                frame_end_ms = 0;
                WebPAnimDecoderReset(decoder.get());
            }
            if (!next_frame()) {
                stop_animation();
                break;
            }
            changed = true;
        }
        if (decoder && elapsed_us >= frame_end_ms * 1000) elapsed_us = frame_end_ms * 1000 - 1;
        return changed;
    }
};

bool animations_enabled(GtkWidget* widget) {
    gboolean enabled = false;
    g_object_get(gtk_widget_get_settings(widget), "gtk-enable-animations", &enabled, nullptr);
    return enabled;
}

void sync_tick(GtkWidget* widget) {
    auto& artwork = *static_cast<HeroArtwork*>(g_object_get_data(G_OBJECT(widget), "artwork"));
    if (artwork.tick) gtk_widget_remove_tick_callback(widget, artwork.tick);
    artwork.tick = 0;
    artwork.last_tick = 0;
    if (!animations_enabled(widget)) {
        if (artwork.previous) cairo_surface_destroy(artwork.previous);
        artwork.previous = nullptr;
        gtk_widget_queue_draw(widget);
        return;
    }
    if (!gtk_widget_get_mapped(widget) || (!artwork.decoder && !artwork.previous)) return;
    artwork.tick = gtk_widget_add_tick_callback(widget, +[](GtkWidget* widget, GdkFrameClock* clock, gpointer data) -> gboolean {
        auto& artwork = *static_cast<HeroArtwork*>(data);
        auto* native = gtk_widget_get_native(widget);
        auto* surface = native ? gtk_native_get_surface(native) : nullptr;
        if (surface && GDK_IS_TOPLEVEL(surface) &&
            (gdk_toplevel_get_state(GDK_TOPLEVEL(surface)) & GDK_TOPLEVEL_STATE_MINIMIZED)) {
            artwork.last_tick = 0;
            return G_SOURCE_CONTINUE;
        }
        const auto now = gdk_frame_clock_get_frame_time(clock);
        bool changed = artwork.advance(now);
        if (artwork.previous) {
            changed = true;
            if (now - artwork.started >= 180000) {
                cairo_surface_destroy(artwork.previous);
                artwork.previous = nullptr;
            }
        }
        if (changed) gtk_widget_queue_draw(widget);
        if (artwork.decoder || artwork.previous) return G_SOURCE_CONTINUE;
        artwork.tick = 0;
        return G_SOURCE_REMOVE;
    }, &artwork, nullptr);
}

std::unique_ptr<HeroArtwork> load_artwork(const std::string& path) {
    auto artwork = std::make_unique<HeroArtwork>();
    artwork->path = path;
    if (path.empty()) return artwork;
    if (std::filesystem::path(path).extension() == ".webp") {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > 64 * 1024 * 1024) return artwork;
        gchar* bytes = nullptr;
        gsize length = 0;
        if (!g_file_get_contents(path.c_str(), &bytes, &length, nullptr)) return artwork;
        artwork->bytes.reset(bytes);
        WebPBitstreamFeatures features{};
        if (length > 64 * 1024 * 1024 || WebPGetFeatures(reinterpret_cast<const std::uint8_t*>(bytes), length, &features) != VP8_STATUS_OK ||
            features.width <= 0 || features.height <= 0 || std::uint64_t(features.width) * features.height > 16 * 1024 * 1024) {
            artwork->stop_animation();
            return artwork;
        }
        const WebPData data{reinterpret_cast<const std::uint8_t*>(bytes), length};
        WebPAnimDecoderOptions options;
        if (!WebPAnimDecoderOptionsInit(&options)) return artwork;
        options.color_mode = MODE_bgrA;
        options.use_threads = true;
        artwork->decoder.reset(WebPAnimDecoderNew(&data, &options));
        if (!artwork->decoder || !WebPAnimDecoderGetInfo(artwork->decoder.get(), &artwork->animation)) {
            artwork->stop_animation();
            return artwork;
        }
        const auto& info = artwork->animation;
        artwork->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, info.canvas_width, info.canvas_height);
        if (cairo_surface_status(artwork->image) != CAIRO_STATUS_SUCCESS) throw std::runtime_error("Could not prepare game artwork");
        if (!artwork->next_frame()) {
            artwork->stop_animation();
            cairo_surface_destroy(artwork->image);
            artwork->image = nullptr;
        } else if (info.frame_count <= 1) {
            artwork->stop_animation();
        }
        return artwork;
    }
    auto* texture = gdk_texture_new_from_filename(path.c_str(), nullptr);
    if (!texture) return artwork;
    artwork->image = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gdk_texture_get_width(texture), gdk_texture_get_height(texture));
    auto* downloader = gdk_texture_downloader_new(texture);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_DEFAULT);
    if (cairo_surface_status(artwork->image) == CAIRO_STATUS_SUCCESS) {
        gdk_texture_downloader_download_into(downloader, cairo_image_surface_get_data(artwork->image), cairo_image_surface_get_stride(artwork->image));
        cairo_surface_mark_dirty(artwork->image);
    }
    gdk_texture_downloader_free(downloader);
    g_object_unref(texture);
    if (cairo_surface_status(artwork->image) != CAIRO_STATUS_SUCCESS) throw std::runtime_error("Could not prepare game artwork");
    return artwork;
}

} // namespace

GtkWidget* hero_canvas() {
    auto* widget = gtk_drawing_area_new();
    auto* artwork = new HeroArtwork;
    g_object_set_data(G_OBJECT(widget), "artwork", artwork);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget), +[](GtkDrawingArea*, cairo_t* context, int width, int height, gpointer data) {
        const auto& artwork = *static_cast<HeroArtwork*>(data);
        cairo_rectangle(context, 0, 0, width, height);
        cairo_clip(context);
        const auto paint = [&](cairo_surface_t* image, double opacity) {
            if (!image || opacity <= 0) return;
            const int image_width = cairo_image_surface_get_width(image);
            const int image_height = cairo_image_surface_get_height(image);
            const double scale = std::max(static_cast<double>(width) / image_width, static_cast<double>(height) / image_height);
            cairo_save(context);
            cairo_translate(context, width - image_width * scale, (height - image_height * scale) / 2);
            cairo_scale(context, scale, scale);
            cairo_set_source_surface(context, image, 0, 0);
            auto* fade = cairo_pattern_create_linear(0, 0, image_width * 0.12, 0);
            cairo_pattern_add_color_stop_rgba(fade, 0, 0, 0, 0, 0);
            cairo_pattern_add_color_stop_rgba(fade, 1, 0, 0, 0, opacity);
            cairo_mask(context, fade);
            cairo_pattern_destroy(fade);
            cairo_restore(context);
        };
        if (!artwork.previous) {
            paint(artwork.image, 1);
            return;
        }
        const double progress = std::clamp((g_get_monotonic_time() - artwork.started) / 180000.0, 0.0, 1.0);
        const double blend = progress * progress * (3 - 2 * progress);
        cairo_save(context);
        cairo_push_group(context);
        paint(artwork.previous, 1 - blend);
        cairo_set_operator(context, CAIRO_OPERATOR_ADD);
        paint(artwork.image, blend);
        cairo_pop_group_to_source(context);
        cairo_set_operator(context, CAIRO_OPERATOR_OVER);
        cairo_paint(context);
        cairo_restore(context);
    }, artwork, +[](gpointer data) { delete static_cast<HeroArtwork*>(data); });
    g_signal_connect(widget, "unmap", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
        auto& artwork = *static_cast<HeroArtwork*>(g_object_get_data(G_OBJECT(widget), "artwork"));
        if (artwork.tick) gtk_widget_remove_tick_callback(widget, artwork.tick);
        artwork.tick = 0;
        artwork.last_tick = 0;
    }), nullptr);
    g_signal_connect(widget, "map", G_CALLBACK(+[](GtkWidget* widget, gpointer) { sync_tick(widget); }), nullptr);
    g_signal_connect_object(gtk_widget_get_settings(widget), "notify::gtk-enable-animations",
        G_CALLBACK(+[](GObject*, GParamSpec*, gpointer widget) { sync_tick(GTK_WIDGET(widget)); }), widget, G_CONNECT_DEFAULT);
    return widget;
}

void set_hero_art(GtkWidget* widget, const Game* game) {
    auto& artwork = *static_cast<HeroArtwork*>(g_object_get_data(G_OBJECT(widget), "artwork"));
    const auto path = game ? game->hero_path : std::string{};
    if (artwork.path == path) return;
    auto next = load_artwork(path);
    if (artwork.tick) gtk_widget_remove_tick_callback(widget, artwork.tick);
    artwork.tick = 0;
    if (artwork.previous) cairo_surface_destroy(artwork.previous);
    artwork.previous = artwork.image;
    artwork.image = std::exchange(next->image, nullptr);
    artwork.stop_animation();
    artwork.bytes = std::move(next->bytes);
    artwork.decoder = std::move(next->decoder);
    artwork.animation = next->animation;
    artwork.path = path;
    artwork.loops = 0;
    artwork.elapsed_us = 0;
    artwork.frame_end_ms = next->frame_end_ms;
    artwork.started = g_get_monotonic_time();
    if (!artwork.image && artwork.previous) {
        cairo_surface_destroy(artwork.previous);
        artwork.previous = nullptr;
    }
    sync_tick(widget);
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL,
        game ? game->title.c_str() : "Selected game artwork", -1);
    gtk_widget_queue_draw(widget);
}

} // namespace framerift
