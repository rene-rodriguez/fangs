// ui_workspace_rail — Raylib rendering and mouse hit handling.
#include "ui_workspace_rail.h"

#include "ui_theme.h"
#include "workspace_status.h"

#include <string.h>

// Row heights in logical px.
#define ROW_H_FULL    32
#define ROW_H_COMPACT 36
#define RING_RADIUS    4
#define RING_PAD_X     8       // left padding for the ring
#define TEXT_PAD_X     22      // left edge of row text (after ring)
#define LABEL_PAD_Y     6      // top padding for label text baseline
#define BRANCH_PAD_Y   20      // top padding for branch text baseline (below label)

// Colors derived from theme constants.
// error color = a stronger red than inline_error
static Color ring_color(WorkspaceAttention level)
{
    switch (level) {
    case WORKSPACE_ATTENTION_INFO:
        return UI2RAY(g_ui_theme.accent);
    case WORKSPACE_ATTENTION_WARN:
        return UI2RAY(g_ui_theme.inline_error);
    case WORKSPACE_ATTENTION_ERROR:
        // Stronger red: use inline_error but more saturated
        return (Color){ 220, 40, 40, 255 };
    default:
        return (Color){ 0, 0, 0, 0 };   // fully transparent = no ring
    }
}

static void draw_ring(int cx, int cy, WorkspaceAttention level)
{
    Color c = ring_color(level);
    if (c.a == 0) return;
    DrawCircle(cx, cy, RING_RADIUS, c);
}

WorkspaceRailAction ui_workspace_rail_draw(Font font, Rect bounds,
                                           const WorkspaceRailView *view,
                                           int mouse_x, int mouse_y,
                                           bool mouse_pressed)
{
    WorkspaceRailAction result = { WORKSPACE_RAIL_ACTION_NONE, 0, 0 };
    int row_h = view->compact ? ROW_H_COMPACT : ROW_H_FULL;
    int y = bounds.y;

    // Fill background.
    DrawRectangle(bounds.x, bounds.y, bounds.w, bounds.h,
                  UI2RAY(g_ui_theme.panel_bg));

    // Right separator.
    DrawRectangle(bounds.x + bounds.w - 1, bounds.y, 1, bounds.h,
                  UI2RAY(g_ui_theme.sidebar_separator));

    // --- Notification line (only when non-empty) ---
    if (view->notification[0] != '\0' && !view->compact) {
        // A subtle background for the notification row.
        Color note_bg = UI2RAY(g_ui_theme.inline_error);
        note_bg.a = 30;
        DrawRectangle(bounds.x, y, bounds.w, row_h, note_bg);

        Vector2 tsz = MeasureTextEx(font, view->notification, 12, 0);
        float tx = (float)(bounds.x + TEXT_PAD_X);
        float ty = (float)(y + (row_h - (int)tsz.y) / 2);
        // Clamp text width.
        float max_tw = (float)(bounds.w - TEXT_PAD_X - 4);
        if (tsz.x > max_tw) tsz.x = max_tw;
        DrawTextEx(font, view->notification,
                   (Vector2){ tx, ty }, 12, 0, UI2RAY(g_ui_theme.text));
        y += row_h + 1;
    }

    // --- Section header: Tabs ---
    if (!view->compact) {
        DrawRectangle(bounds.x, y, bounds.w, 1, UI2RAY(g_ui_theme.sidebar_separator));
        y += 3;
    }

    // --- Tab rows ---
    for (int i = 0; i < view->tab_count; i++) {
        const WorkspaceRailRow *row = &view->tabs[i];
        int row_y = y;

        // Active highlight.
        if (row->active) {
            Color sel = UI2RAY(g_ui_theme.selection);
            sel.a = 60;
            DrawRectangle(bounds.x, row_y, bounds.w, row_h, sel);
        }

        // Notification ring on the leading edge.
        int ring_cy = row_y + row_h / 2;
        draw_ring(bounds.x + RING_PAD_X, ring_cy, row->attention);

        if (!view->compact) {
            // Row label and branch.
            char full[128];
            if (row->branch[0] != '\0')
                snprintf(full, sizeof(full), "%s %s", row->label, row->branch);
            else
                snprintf(full, sizeof(full), "%s", row->label);

            // Truncate if needed.
            Vector2 sz = MeasureTextEx(font, full, 13, 0);
            float max_w = (float)(bounds.w - TEXT_PAD_X - 4);
            if (sz.x > max_w) {
                // Truncate from end.
                char truncated[128];
                int len = (int)strlen(full);
                while (len > 0) {
                    truncated[len] = '\0';
                    truncated[len - 1] = full[len - 1];
                    Vector2 ts = MeasureTextEx(font, truncated, 13, 0);
                    if (ts.x <= max_w - 6) break;
                    len--;
                }
                // Add ellipsis.
                if (len >= 0) {
                    truncated[len] = '\0';
                    if (len >= 3) {
                        truncated[len - 1] = '.';
                        truncated[len - 2] = '.';
                        truncated[len - 3] = '.';
                    }
                    DrawTextEx(font, truncated,
                               (Vector2){ (float)(bounds.x + TEXT_PAD_X),
                                          (float)(row_y + LABEL_PAD_Y) },
                               13, 0, UI2RAY(g_ui_theme.text));
                }
            } else {
                DrawTextEx(font, full,
                           (Vector2){ (float)(bounds.x + TEXT_PAD_X),
                                      (float)(row_y + LABEL_PAD_Y) },
                           13, 0, UI2RAY(g_ui_theme.text));
            }
        }

        // Click detection.
        if (mouse_pressed && mouse_x >= bounds.x && mouse_x < bounds.x + bounds.w
            && mouse_y >= row_y && mouse_y < row_y + row_h) {
            result.type = WORKSPACE_RAIL_ACTION_SWITCH_TAB;
            result.index = row->index;
        }

        y = row_y + row_h + 1;
    }

    // --- Section separator before panes ---
    if (view->pane_count > 0 && !view->compact) {
        y += 2;
        DrawRectangle(bounds.x, y, bounds.w, 1, UI2RAY(g_ui_theme.sidebar_separator));
        y += 3;
    }

    // --- Pane rows ---
    for (int i = 0; i < view->pane_count; i++) {
        const WorkspaceRailRow *row = &view->panes[i];
        int row_y = y;

        // Active highlight.
        if (row->active) {
            Color sel = UI2RAY(g_ui_theme.selection);
            sel.a = 60;
            DrawRectangle(bounds.x, row_y, bounds.w, row_h, sel);
        }

        // Notification ring.
        int ring_cy = row_y + row_h / 2;
        draw_ring(bounds.x + RING_PAD_X, ring_cy, row->attention);

        if (!view->compact) {
            char full[128];
            if (row->branch[0] != '\0')
                snprintf(full, sizeof(full), "%s %s", row->label, row->branch);
            else
                snprintf(full, sizeof(full), "%s", row->label);

            Vector2 sz = MeasureTextEx(font, full, 13, 0);
            float max_w = (float)(bounds.w - TEXT_PAD_X - 4);
            if (sz.x > max_w) {
                char truncated[128];
                int len = (int)strlen(full);
                while (len > 0) {
                    truncated[len] = '\0';
                    truncated[len - 1] = full[len - 1];
                    Vector2 ts = MeasureTextEx(font, truncated, 13, 0);
                    if (ts.x <= max_w - 6) break;
                    len--;
                }
                if (len >= 0) {
                    truncated[len] = '\0';
                    if (len >= 3) {
                        truncated[len - 1] = '.';
                        truncated[len - 2] = '.';
                        truncated[len - 3] = '.';
                    }
                    DrawTextEx(font, truncated,
                               (Vector2){ (float)(bounds.x + TEXT_PAD_X),
                                          (float)(row_y + LABEL_PAD_Y) },
                               13, 0, UI2RAY(g_ui_theme.text));
                }
            } else {
                DrawTextEx(font, full,
                           (Vector2){ (float)(bounds.x + TEXT_PAD_X),
                                      (float)(row_y + LABEL_PAD_Y) },
                           13, 0, UI2RAY(g_ui_theme.text));
            }
        }

        // Click detection.
        if (mouse_pressed && mouse_x >= bounds.x && mouse_x < bounds.x + bounds.w
            && mouse_y >= row_y && mouse_y < row_y + row_h) {
            result.type = WORKSPACE_RAIL_ACTION_FOCUS_PANE;
            result.index = row->index;
            result.pane_id = row->id;
        }

        y = row_y + row_h + 1;
    }

    return result;
}
