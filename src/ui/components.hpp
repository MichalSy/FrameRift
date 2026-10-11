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

inline GtkWidget* card(GtkOrientation orientation = GTK_ORIENTATION_VERTICAL, int spacing = 16) {
    auto* widget = gtk_box_new(orientation, spacing);
    gtk_widget_add_css_class(widget, "profiler-card");
    return widget;
}

struct MetricCard {
    GtkWidget* root;
    GtkWidget* value;
};

inline MetricCard metric_card(const char* title, bool primary = false) {
    auto* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(root, "metric-card");
    gtk_widget_set_hexpand(root, true);
    auto* heading = label(title, "metric-label");
    gtk_widget_set_hexpand(heading, true);
    gtk_label_set_ellipsize(GTK_LABEL(heading), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(heading, title);
    gtk_box_append(GTK_BOX(root), heading);
    auto* value = label("–", "metric-value");
    gtk_label_set_xalign(GTK_LABEL(value), 1);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
    if (primary) gtk_widget_add_css_class(value, "metric-primary");
    gtk_box_append(GTK_BOX(root), value);
    return {root, value};
}

struct SegmentedControl {
    GtkWidget* root;
    GtkWidget* first;
    GtkWidget* second;
};

inline SegmentedControl segmented_control(const char* first, const char* second) {
    auto* root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(root, "segmented-control");
    auto* a = gtk_toggle_button_new_with_label(first);
    auto* b = gtk_toggle_button_new_with_label(second);
    gtk_toggle_button_set_group(GTK_TOGGLE_BUTTON(b), GTK_TOGGLE_BUTTON(a));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(a), true);
    gtk_box_append(GTK_BOX(root), a);
    gtk_box_append(GTK_BOX(root), b);
    return {root, a, b};
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
