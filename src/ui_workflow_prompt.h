// ui_workflow_prompt — Collect values for parameterized runbooks.
#ifndef FANGS_UI_WORKFLOW_PROMPT_H
#define FANGS_UI_WORKFLOW_PROMPT_H

#include <stdbool.h>

#include "raylib.h"
#include "workflows.h"

bool ui_workflow_prompt_open(const Workflow *workflow);
bool ui_workflow_prompt_active(void);
void ui_workflow_prompt_cancel(void);
void ui_workflow_prompt_draw(Font font, float scale);

// Returns the expanded command exactly once after the final variable is
// accepted, then NULL. The command is staged by the host, not executed.
const char *ui_workflow_prompt_take_command(void);

#endif // FANGS_UI_WORKFLOW_PROMPT_H
