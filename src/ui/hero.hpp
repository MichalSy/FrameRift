// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Michal Sytko and FrameRift contributors
// Original project: https://github.com/MichalSy/FrameRift
// Additional attribution terms: see NOTICE in the repository root.

#pragma once

#include <gtk/gtk.h>

namespace framerift {

struct Game;

GtkWidget* hero_canvas();
void set_hero_art(GtkWidget* widget, const Game* game);

} // namespace framerift
