// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <gtk/gtk.h>

namespace framerift::ui {

inline GtkWidget* label(const char* text, const char* style = nullptr) {
    auto* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    if (style) gtk_widget_add_css_class(widget, style);
    return widget;
}

inline GtkWidget* panel() {
    auto* widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_add_css_class(widget, "monitor-panel");
    return widget;
}

struct MetricCard {
    GtkWidget* root;
    GtkWidget* value;
};

inline MetricCard metric_card(const char* title, bool primary = false) {
    auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(root, "metric-card");
    gtk_widget_set_hexpand(root, true);
    gtk_box_append(GTK_BOX(root), label(title, "metric-label"));
    auto* value = label("–", "metric-value");
    gtk_label_set_xalign(GTK_LABEL(value), 1);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
    if (primary) gtk_widget_add_css_class(value, "metric-primary");
    gtk_box_append(GTK_BOX(root), value);
    return {root, value};
}

// The widget owns both captions; inactive selects the first, active the second.
inline GtkWidget* mode_toggle(const char* first, const char* second) {
    auto* button = gtk_toggle_button_new_with_label(first);
    gtk_widget_add_css_class(button, "mode-toggle");
    g_object_set_data_full(G_OBJECT(button), "mode-first", g_strdup(first), g_free);
    g_object_set_data_full(G_OBJECT(button), "mode-second", g_strdup(second), g_free);
    g_signal_connect(button, "toggled", G_CALLBACK(+[](GtkToggleButton* button, gpointer) {
        const auto* key = gtk_toggle_button_get_active(button) ? "mode-second" : "mode-first";
        gtk_button_set_label(GTK_BUTTON(button), static_cast<const char*>(g_object_get_data(G_OBJECT(button), key)));
    }), nullptr);
    return button;
}

inline GtkWidget* header_overlay(GtkWidget* content, GtkWidget* header) {
    auto* overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), content);
    gtk_widget_set_valign(header, GTK_ALIGN_START);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), header);
    gtk_overlay_set_measure_overlay(GTK_OVERLAY(overlay), header, true);
    return overlay;
}

} // namespace framerift::ui
