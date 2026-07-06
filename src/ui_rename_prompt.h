// ui_rename_prompt — single-line modal to rename a workspace (tab).
//
// Mirrors ui_workflow_prompt: the host opens it, draws it every frame, and
// polls take() for the accepted value. An empty accepted name means "clear
// the custom name" (the rail falls back to the agent title / cwd label).
#ifndef FANGS_UI_RENAME_PROMPT_H
#define FANGS_UI_RENAME_PROMPT_H

#include <stdbool.h>

#include "raylib.h"

#define RENAME_PROMPT_NAME_MAX 64

// Open the prompt for tab `tab_index`, prefilled with `current` (may be "").
void ui_rename_prompt_open(int tab_index, const char *current);
bool ui_rename_prompt_active(void);
void ui_rename_prompt_cancel(void);
void ui_rename_prompt_draw(Font font, float scale);

// Returns true exactly once after Enter, filling *tab_index and out. The
// accepted name may be empty (= reset to the automatic label).
bool ui_rename_prompt_take(int *tab_index, char *out, int out_size);

#endif // FANGS_UI_RENAME_PROMPT_H
