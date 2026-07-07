// ui_menu_draw — Raylib rendering for the generic popover menu.
//
// This module only paints a laid-out UiMenu. Geometry and click
// hit-testing live in ui_menu.h, and main.c resolves clicks
// through ui_menu_hit() before drawing.

#ifndef FANGS_UI_MENU_DRAW_H
#define FANGS_UI_MENU_DRAW_H

#include "raylib.h"
#include "ui_menu.h"

void ui_menu_draw(Font font, const UiMenu *m, int mouse_x, int mouse_y,
                  const UiTheme *theme);

#endif // FANGS_UI_MENU_DRAW_H
