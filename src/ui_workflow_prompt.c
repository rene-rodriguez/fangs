#include "ui_workflow_prompt.h"

#include <stdio.h>
#include <string.h>

#include "raygui.h"
#include "ui_theme.h"

static bool active = false;
static char template_command[WORKFLOW_COMMAND_MAX] = "";
static WorkflowVar vars[WORKFLOW_VAR_MAX];
static WorkflowValue values[WORKFLOW_VAR_MAX];
static int var_count = 0;
static int current_var = 0;
static char input_value[WORKFLOW_VAR_VALUE_MAX] = "";
static char status_text[160] = "";
static char pending_command[WORKFLOW_COMMAND_MAX] = "";
static bool pending_ready = false;

static void load_current_input(void)
{
    if (current_var < 0 || current_var >= var_count) {
        input_value[0] = '\0';
        return;
    }
    snprintf(input_value, sizeof(input_value), "%s",
             values[current_var].value[0] ? values[current_var].value
             : vars[current_var].has_default ? vars[current_var].default_value
             : "");
}

bool ui_workflow_prompt_open(const Workflow *workflow)
{
    if (!workflow || workflow->command[0] == '\0')
        return false;

    memset(vars, 0, sizeof(vars));
    memset(values, 0, sizeof(values));
    var_count = workflows_collect_vars(workflow->command, vars, WORKFLOW_VAR_MAX);
    if (var_count <= 0)
        return false;

    snprintf(template_command, sizeof(template_command), "%s", workflow->command);
    for (int i = 0; i < var_count; i++) {
        snprintf(values[i].name, sizeof(values[i].name), "%s", vars[i].name);
        if (vars[i].has_default)
            snprintf(values[i].value, sizeof(values[i].value), "%s", vars[i].default_value);
    }

    current_var = 0;
    status_text[0] = '\0';
    pending_ready = false;
    pending_command[0] = '\0';
    active = true;
    load_current_input();
    return true;
}

bool ui_workflow_prompt_active(void)
{
    return active;
}

void ui_workflow_prompt_cancel(void)
{
    active = false;
    template_command[0] = '\0';
    input_value[0] = '\0';
    status_text[0] = '\0';
    var_count = 0;
    current_var = 0;
}

const char *ui_workflow_prompt_take_command(void)
{
    if (!pending_ready)
        return NULL;
    pending_ready = false;
    return pending_command;
}

static void accept_current_value(void)
{
    if (current_var < 0 || current_var >= var_count)
        return;

    if (input_value[0] == '\0' && vars[current_var].has_default) {
        snprintf(input_value, sizeof(input_value), "%s",
                 vars[current_var].default_value);
    }
    if (input_value[0] == '\0' && !vars[current_var].has_default) {
        snprintf(status_text, sizeof(status_text), "Value required.");
        return;
    }

    snprintf(values[current_var].value, sizeof(values[current_var].value),
             "%s", input_value);

    if (current_var + 1 < var_count) {
        current_var++;
        load_current_input();
        status_text[0] = '\0';
        return;
    }

    if (!workflows_expand_command(template_command, values, var_count,
                                  pending_command, (int)sizeof(pending_command))) {
        snprintf(status_text, sizeof(status_text), "Could not expand command.");
        return;
    }

    pending_ready = true;
    ui_workflow_prompt_cancel();
}

void ui_workflow_prompt_draw(Font font, float scale)
{
    if (!active)
        return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        ui_workflow_prompt_cancel();
        return;
    }

    float s = (scale > 0.1f) ? scale : 1.0f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = (int)(560 * s);
    if (panel_w > sw - 32)
        panel_w = sw - 32;
    int panel_h = (int)(168 * s);
    int x = (sw - panel_w) / 2;
    int y = (sh - panel_h) / 2;

    DrawRectangle(0, 0, sw, sh, UI2RAY(g_ui_theme.modal_overlay));
    Rectangle panel = { (float)x, (float)y, (float)panel_w, (float)panel_h };
    DrawRectangleRec(panel, UI2RAY(g_ui_theme.inline_bg));
    DrawRectangleLinesEx(panel, 1.0f, UI2RAY(g_ui_theme.panel_border));

    char title[160];
    snprintf(title, sizeof(title), "Runbook Variable %d/%d",
             current_var + 1, var_count);
    DrawTextEx(font, title, (Vector2){panel.x + 14*s, panel.y + 12*s},
               15.0f * s, 0, UI2RAY(g_ui_theme.subtitle));

    const WorkflowVar *v = &vars[current_var];
    DrawTextEx(font, v->name, (Vector2){panel.x + 14*s, panel.y + 40*s},
               17.0f * s, 0, UI2RAY(g_ui_theme.text));

    Rectangle input = {
        panel.x + 14*s,
        panel.y + 68*s,
        panel.width - 28*s,
        34*s,
    };
    bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    int prev_ts = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(16 * s));
    GuiTextBox(input, input_value, WORKFLOW_VAR_VALUE_MAX, true);
    GuiSetStyle(DEFAULT, TEXT_SIZE, prev_ts);

    if (status_text[0]) {
        DrawTextEx(font, status_text,
                   (Vector2){panel.x + 14*s, panel.y + 110*s},
                   12.0f * s, 0, UI2RAY(g_ui_theme.inline_error));
    } else if (v->has_default) {
        char hint[WORKFLOW_VAR_VALUE_MAX + 32];
        snprintf(hint, sizeof(hint), "Default: %s", v->default_value);
        DrawTextEx(font, hint, (Vector2){panel.x + 14*s, panel.y + 110*s},
                   12.0f * s, 0, UI2RAY(g_ui_theme.subtitle));
    }

    DrawTextEx(font, "Enter to continue · Esc to cancel",
               (Vector2){panel.x + 14*s, panel.y + panel.height - 24*s},
               12.0f * s, 0, UI2RAY(g_ui_theme.subtitle));

    if (enter)
        accept_current_value();
}
