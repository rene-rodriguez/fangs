// Inline AI command generation (Ctrl+Space): a small floating prompt over the
// terminal. The user types a request; the host streams a single command from the
// model and stages it at the shell prompt (no newline — the user presses Enter).
//
// Reuses the Phase 4 AI seam wholesale; this is just the input surface + state.
#ifndef NOVA_UI_INLINE_H
#define NOVA_UI_INLINE_H

#include <stdbool.h>

#include "raylib.h"

typedef enum {
    INLINE_IDLE,     // closed
    INLINE_INPUT,    // capturing the user's request
    INLINE_WAITING   // request sent, streaming/awaiting (or showing an error)
} InlineState;

// Open the prompt anchored near (anchor_x, anchor_y) in pixels (e.g. the cursor).
void ui_inline_open(int anchor_x, int anchor_y);

bool        ui_inline_active(void);   // INPUT or WAITING — host gates PTY input on this
InlineState ui_inline_state(void);

// Returns the typed request exactly once, on the frame the user pressed Enter
// (then NULL). The host uses it to start the AI request.
const char *ui_inline_take_prompt(void);

void ui_inline_set_waiting(const char *status);  // host: after the stream starts
void ui_inline_set_error(const char *msg);       // host: no key / request failed
void ui_inline_cancel(void);                     // close, inject nothing

// scale: HiDPI content scale; widget/text sizes are multiplied by it.
void ui_inline_draw(Font font, float scale);   // draw + handle the prompt this frame

#endif // NOVA_UI_INLINE_H
