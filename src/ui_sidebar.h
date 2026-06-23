#ifndef NOVA_UI_SIDEBAR_H
#define NOVA_UI_SIDEBAR_H

#include <stdbool.h>

#include "raylib.h"
#include "layout.h"

typedef enum {
    MSG_USER,
    MSG_ASSISTANT,
    MSG_SYSTEM
} MsgRole;

void ui_sidebar_toggle(void);
bool ui_sidebar_visible(void);
bool ui_sidebar_focused(void);
void ui_sidebar_focus(bool on);

// Append a complete message to the history.
void ui_sidebar_push(MsgRole role, const char *text);

// Streaming assistant message (Phase 4). begin pushes an empty assistant
// message; append grows its reasoning (thinking) or answer region; end marks it
// complete and extracts any single-line fenced command for a Run button.
void ui_sidebar_begin_assistant(void);
void ui_sidebar_append_assistant(const char *delta, bool is_reasoning);
void ui_sidebar_end_assistant(void);
bool ui_sidebar_is_streaming(void);

// Read-only history access, for assembling multi-turn AI requests.
int         ui_sidebar_count(void);
MsgRole     ui_sidebar_role(int index);
const char *ui_sidebar_text(int index);

// Draw + handle the panel. Returns true on submit (out_prompt holds the typed
// text). out_run is set to a command when a Run button was clicked this frame
// (empty otherwise) — the host injects it into the PTY, staged (no newline).
// scale: HiDPI content scale; widget/text sizes are multiplied by it to match
// the (scaled) terminal font.
bool ui_sidebar_draw(Font font, Rect bounds, char *out_prompt, int out_prompt_size,
                     char *out_run, int out_run_size, float scale);

#endif // NOVA_UI_SIDEBAR_H
