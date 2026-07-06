// ui_workspace_rail — Raylib rendering and mouse hit handling for the left
// workspace rail.
//
// This module draws the rail inside the Layout.rail rectangle and returns
// click actions. It uses Font, UiTheme, and row data from WorkspaceRailView.
#ifndef FANGS_UI_WORKSPACE_RAIL_H
#define FANGS_UI_WORKSPACE_RAIL_H

#include "layout.h"
#include "raylib.h"
#include "ui_workspace_rail_model.h"

typedef struct {
    WorkspaceRailActionType type;
    int index;
    uint64_t pane_id;
} WorkspaceRailAction;

// Draw the workspace rail and return any click action that occurred.
// mouse_pressed should be true on the frame IsMouseButtonPressed(MOUSE_BUTTON_LEFT).
WorkspaceRailAction ui_workspace_rail_draw(Font font, Rect bounds,
                                           const WorkspaceRailView *view,
                                           int mouse_x, int mouse_y,
                                           bool mouse_pressed);

#endif // FANGS_UI_WORKSPACE_RAIL_H
