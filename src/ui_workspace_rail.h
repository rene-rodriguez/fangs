// ui_workspace_rail — Raylib rendering for the left workspace rail.
//
// This module only paints a laid-out WorkspaceRailView. Geometry and click
// hit-testing live in ui_workspace_rail_model, and main.c resolves clicks
// through workspace_rail_hit() before drawing, so painting and hit targets
// can never drift apart.
#ifndef FANGS_UI_WORKSPACE_RAIL_H
#define FANGS_UI_WORKSPACE_RAIL_H

#include "raylib.h"
#include "ui_workspace_rail_model.h"

// Draw the workspace rail. workspace_rail_layout() must have run on the view.
// The mouse position drives hover feedback only.
void ui_workspace_rail_draw(Font font, const WorkspaceRailView *view,
                            int mouse_x, int mouse_y);

#endif // FANGS_UI_WORKSPACE_RAIL_H
