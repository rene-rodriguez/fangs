#include "ui_rename_prompt.h"

#include <stdio.h>
#include <string.h>

#include "raygui.h"
#include "ui_theme.h"

static bool active = false;
static int  target_tab = -1;
static char input_value[RENAME_PROMPT_NAME_MAX] = "";
static char pending_name[RENAME_PROMPT_NAME_MAX] = "";
static int  pending_tab = -1;
static bool pending_ready = false;

void ui_rename_prompt_open(int tab_index, const char *current)
{
    target_tab = tab_index;
    snprintf(input_value, sizeof(input_value), "%s", current ? current : "");
    pending_ready = false;
    active = true;
}

bool ui_rename_prompt_active(void)
{
    return active;
}

void ui_rename_prompt_cancel(void)
{
    active = false;
    target_tab = -1;
    input_value[0] = '\0';
}

bool ui_rename_prompt_take(int *tab_index, char *out, int out_size)
{
    if (!pending_ready)
        return false;
    pending_ready = false;
    if (tab_index)
        *tab_index = pending_tab;
    if (out && out_size > 0)
        snprintf(out, out_size, "%s", pending_name);
    return true;
}

void ui_rename_prompt_draw(Font font, float scale)
{
    if (!active)
        return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        ui_rename_prompt_cancel();
        return;
    }

    float s = (scale > 0.1f) ? scale : 1.0f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int panel_w = (int)(480 * s);
    if (panel_w > sw - 32)
        panel_w = sw - 32;
    int panel_h = (int)(138 * s);
    int x = (sw - panel_w) / 2;
    int y = (sh - panel_h) / 2;

    DrawRectangle(0, 0, sw, sh, UI2RAY(g_ui_theme.modal_overlay));
    Rectangle panel = { (float)x, (float)y, (float)panel_w, (float)panel_h };
    DrawRectangleRec(panel, UI2RAY(g_ui_theme.inline_bg));
    DrawRectangleLinesEx(panel, 1.0f, UI2RAY(g_ui_theme.panel_border));

    char title[64];
    snprintf(title, sizeof(title), "Rename Workspace %d", target_tab + 1);
    DrawTextEx(font, title, (Vector2){panel.x + 14*s, panel.y + 12*s},
               15.0f * s, 0, UI2RAY(g_ui_theme.text));

    Rectangle input = {
        panel.x + 14*s,
        panel.y + 42*s,
        panel.width - 28*s,
        34*s,
    };
    bool enter = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
    int prev_ts = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(16 * s));
    GuiTextBox(input, input_value, RENAME_PROMPT_NAME_MAX, true);
    GuiSetStyle(DEFAULT, TEXT_SIZE, prev_ts);

    DrawTextEx(font, "Enter to save - Esc to cancel - empty resets to auto",
               (Vector2){panel.x + 14*s, panel.y + panel.height - 24*s},
               12.0f * s, 0, UI2RAY(g_ui_theme.subtitle));

    if (enter) {
        pending_tab = target_tab;
        snprintf(pending_name, sizeof(pending_name), "%s", input_value);
        pending_ready = true;
        ui_rename_prompt_cancel();
    }
}
