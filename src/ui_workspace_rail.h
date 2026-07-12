// ui_workspace_rail — Raylib rendering for the left workspace rail.
//
// This module only paints a laid-out WorkspaceRailView. Geometry and click
// hit-testing live in ui_workspace_rail_model, and main.c resolves clicks
// through workspace_rail_hit() before drawing, so painting and hit targets
// can never drift apart.
#ifndef FANGS_UI_WORKSPACE_RAIL_H
#define FANGS_UI_WORKSPACE_RAIL_H

#include "raylib.h"
#include "ui_theme.h"
#include "ui_workspace_rail_model.h"

// Draw the workspace rail. workspace_rail_layout() must have run on the view.
// The mouse position drives hover feedback only.
void ui_workspace_rail_draw(Font font, WorkspaceRailView *view,
                            int mouse_x, int mouse_y, float dt);

static inline void ui_workspace_rail_set_ring_pulse(WorkspaceRailView *view, float v)
{
    if (view) view->ring_pulse = v;
}

// Fixed color-tag palette for grouping related workspaces at a glance.
// Deliberately NOT theme-derived (unlike ui_theme's semantic colors) so tags
// stay recognizable across theme switches. WorkspaceRailRow.color_tag is a
// 1-based index into these arrays (0 = untagged). UiColor (not raylib Color)
// so callers building UiMenuItem.tint (e.g. the color-picker submenu in
// main.c) can use these directly; ui_workspace_rail.c wraps with UI2RAY().
#define WORKSPACE_RAIL_COLOR_TAG_COUNT 6
extern const char   *WORKSPACE_RAIL_COLOR_TAG_NAMES[WORKSPACE_RAIL_COLOR_TAG_COUNT];
extern const UiColor WORKSPACE_RAIL_COLOR_TAG_COLORS[WORKSPACE_RAIL_COLOR_TAG_COUNT];

#endif // FANGS_UI_WORKSPACE_RAIL_H
