#include "ui_palette.h"

#include <stdio.h>
#include <string.h>

#include "raygui.h"
#include "ui_palette_model.h"
#include "ui_theme.h"

static UiPaletteModel g_palette;
static bool g_palette_init = false;
static char g_query_buf[UI_PALETTE_QUERY_MAX] = "";

static void ensure_palette(void)
{
    if (!g_palette_init) {
        ui_palette_model_init(&g_palette);
        g_palette_init = true;
    }
}

void ui_palette_open(void)
{
    ensure_palette();
    ui_palette_model_open(&g_palette);
    g_query_buf[0] = '\0';
}

void ui_palette_close(void)
{
    ensure_palette();
    ui_palette_model_close(&g_palette);
    g_query_buf[0] = '\0';
}

void ui_palette_set_workflows(const WorkflowRegistry *workflows)
{
    ensure_palette();
    ui_palette_model_set_workflows(&g_palette, workflows);
}

void ui_palette_set_workspaces(const WorkspacePaletteEntry *entries, int count)
{
    ensure_palette();
    ui_palette_model_set_workspaces(&g_palette, entries, count);
}

bool ui_palette_is_open(void)
{
    ensure_palette();
    return ui_palette_model_is_open(&g_palette);
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int selected_row_start(int selected, int count, int max_rows)
{
    if (count <= max_rows)
        return 0;
    int start = selected - max_rows + 1;
    if (start < 0)
        start = 0;
    if (start > count - max_rows)
        start = count - max_rows;
    return start;
}

static const char *entry_section_label(int type)
{
    if (type == UI_PALETTE_ENTRY_WORKFLOW)
        return "Runbooks";
    if (type == UI_PALETTE_ENTRY_WORKSPACE)
        return "Workspaces";
    return "Actions";
}

bool ui_palette_draw(Font font, float scale, UiPaletteSelection *out_selection)
{
    ensure_palette();
    if (out_selection) {
        *out_selection = (UiPaletteSelection){
            .type = UI_PALETTE_SELECTION_NONE,
            .action_id = FANGS_ACTION_NONE,
            .workflow_index = -1,
        };
    }
    if (!ui_palette_model_is_open(&g_palette))
        return false;

    float s = (scale > 0.1f) ? scale : 1.0f;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    int panel_w = clamp_int((int)(720 * s), 320, sw - 32);
    int max_rows = 8;
    int row_h = (int)(48 * s);
    int header_h = (int)(22 * s);
    int input_h = (int)(38 * s);
    int chrome_h = (int)(82 * s);
    int count = ui_palette_model_match_count(&g_palette);
    int visible_rows = count < max_rows ? count : max_rows;
    if (visible_rows < 3)
        visible_rows = 3;
    int panel_h = chrome_h + visible_rows * row_h + header_h * 2;
    if (panel_h > sh - 32)
        panel_h = sh - 32;

    int x = (sw - panel_w) / 2;
    int y = (int)(56 * s);
    if (y + panel_h > sh - 16)
        y = (sh - panel_h) / 2;
    if (y < 16)
        y = 16;

    DrawRectangle(0, 0, sw, sh, UI2RAY(g_ui_theme.modal_overlay));

    Rectangle panel = { (float)x, (float)y, (float)panel_w, (float)panel_h };
    DrawRectangleRec(panel, UI2RAY(g_ui_theme.inline_bg));
    DrawRectangleLinesEx(panel, 1.0f, UI2RAY(g_ui_theme.panel_border));

    DrawTextEx(font, "Command Palette",
               (Vector2){ panel.x + 14*s, panel.y + 10*s },
               15.0f * s, 0, UI2RAY(g_ui_theme.subtitle));

    Rectangle input = {
        panel.x + 14*s,
        panel.y + 34*s,
        panel.width - 28*s,
        (float)input_h,
    };

    char before[UI_PALETTE_QUERY_MAX];
    snprintf(before, sizeof(before), "%s", g_query_buf);
    int prev_ts = GuiGetStyle(DEFAULT, TEXT_SIZE);
    GuiSetStyle(DEFAULT, TEXT_SIZE, (int)(17 * s));
    GuiTextBox(input, g_query_buf, UI_PALETTE_QUERY_MAX, true);
    GuiSetStyle(DEFAULT, TEXT_SIZE, prev_ts);
    if (strcmp(before, g_query_buf) != 0)
        ui_palette_model_set_query(&g_palette, g_query_buf);

    if (IsKeyPressed(KEY_ESCAPE)) {
        ui_palette_close();
        return false;
    }
    if (IsKeyPressed(KEY_UP))
        ui_palette_model_move(&g_palette, -1);
    if (IsKeyPressed(KEY_DOWN))
        ui_palette_model_move(&g_palette, 1);

    bool accept_key = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);

    count = ui_palette_model_match_count(&g_palette);
    int selected = ui_palette_model_selected(&g_palette);
    int start = selected_row_start(selected, count, max_rows);
    int rows_to_draw = count - start;
    if (rows_to_draw > max_rows)
        rows_to_draw = max_rows;

    float list_y = panel.y + chrome_h;
    float draw_y = list_y;
    Vector2 mouse = GetMousePosition();
    int last_section_type = 0;
    for (int row = 0; row < rows_to_draw; row++) {
        int idx = start + row;
        UiPaletteEntry entry = ui_palette_model_match_entry_at(&g_palette, idx);
        const FangsAction *a = NULL;
        const Workflow *w = NULL;
        WorkspacePaletteEntry ws = { .tab_index = -1, .label = "" };
        const char *label = "";
        const char *detail = "";
        const char *shortcut = "";

        if (entry.type == UI_PALETTE_ENTRY_ACTION) {
            a = ui_palette_model_match_at(&g_palette, idx);
            if (!a)
                continue;
            label = a->label;
            detail = a->detail;
            shortcut = a->shortcut;
        } else if (entry.type == UI_PALETTE_ENTRY_WORKFLOW) {
            w = ui_palette_model_match_workflow_at(&g_palette, idx);
            if (!w)
                continue;
            label = w->label;
            detail = w->detail;
            shortcut = "Runbook";
        } else if (entry.type == UI_PALETTE_ENTRY_WORKSPACE) {
            ws = ui_palette_model_match_workspace_at(&g_palette, idx);
            if (ws.tab_index < 0)
                continue;
            label = ws.label;
            detail = "";
            shortcut = "Workspace";
        } else {
            continue;
        }

        if (row == 0 || entry.type != last_section_type) {
            DrawTextEx(font, entry_section_label(entry.type),
                       (Vector2){ panel.x + 18*s, draw_y + 3*s },
                       11.0f * s, 0, UI2RAY(g_ui_theme.subtitle));
            draw_y += header_h;
            last_section_type = entry.type;
        }

        Rectangle rr = {
            panel.x + 8*s,
            draw_y,
            panel.width - 16*s,
            (float)row_h,
        };
        draw_y += row_h;

        bool hover = CheckCollisionPointRec(mouse, rr);
        if (hover)
            g_palette.selected = idx;

        bool is_selected = idx == ui_palette_model_selected(&g_palette);
        if (is_selected || hover) {
            Color bg = UI2RAY(is_selected ? g_ui_theme.selection : g_ui_theme.search_hit);
            DrawRectangleRec(rr, bg);
        }

        DrawTextEx(font, label,
                   (Vector2){ rr.x + 10*s, rr.y + 7*s },
                   16.0f * s, 0, UI2RAY(g_ui_theme.text));
        DrawTextEx(font, detail,
                   (Vector2){ rr.x + 10*s, rr.y + 27*s },
                   12.0f * s, 0, UI2RAY(g_ui_theme.subtitle));

        if (shortcut && shortcut[0]) {
            Vector2 ss = MeasureTextEx(font, shortcut, 12.0f * s, 0);
            DrawTextEx(font, shortcut,
                       (Vector2){ rr.x + rr.width - ss.x - 10*s, rr.y + 17*s },
                       12.0f * s, 0, UI2RAY(g_ui_theme.subtitle));
        }

        if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            accept_key = true;
    }

    if (count == 0) {
        DrawTextEx(font, "No matching actions",
                   (Vector2){ panel.x + 18*s, list_y + 18*s },
                   16.0f * s, 0, UI2RAY(g_ui_theme.subtitle));
    }

    if (accept_key) {
        UiPaletteSelection selection = ui_palette_model_accept_selection(&g_palette);
        if (selection.type != UI_PALETTE_SELECTION_NONE) {
            if (out_selection)
                *out_selection = selection;
            return true;
        }
    }

    return false;
}
