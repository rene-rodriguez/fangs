// ui_palette — Command palette overlay for discovering and running host actions.
#ifndef FANGS_UI_PALETTE_H
#define FANGS_UI_PALETTE_H

#include <stdbool.h>

#include "raylib.h"
#include "ui_palette_model.h"
#include "workflows.h"

void ui_palette_open(void);
void ui_palette_close(void);
bool ui_palette_is_open(void);
void ui_palette_set_workflows(const WorkflowRegistry *workflows);

// Draws the palette and handles palette-local input. Returns true when the
// user chose an action or workflow; *out_selection receives the selection.
bool ui_palette_draw(Font font, float scale, UiPaletteSelection *out_selection);

#endif // FANGS_UI_PALETTE_H
