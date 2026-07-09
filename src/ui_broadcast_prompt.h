// ui_broadcast_prompt — single-line modal to collect a command/message to
// send to every live session's PTY across all open workspaces.
//
// Mirrors ui_rename_prompt: the host opens it, draws it every frame, and
// polls take() for the accepted text. There is no per-tab target (it
// applies to every open workspace), unlike ui_rename_prompt.
#ifndef FANGS_UI_BROADCAST_PROMPT_H
#define FANGS_UI_BROADCAST_PROMPT_H

#include <stdbool.h>

#include "raylib.h"

#define BROADCAST_PROMPT_TEXT_MAX 256

void ui_broadcast_prompt_open(void);
bool ui_broadcast_prompt_active(void);
void ui_broadcast_prompt_cancel(void);
void ui_broadcast_prompt_draw(Font font, float scale);

// Returns true exactly once after Enter, filling out with the accepted
// text (may be empty, in which case the host should skip the broadcast).
bool ui_broadcast_prompt_take(char *out, int out_size);

#endif // FANGS_UI_BROADCAST_PROMPT_H
