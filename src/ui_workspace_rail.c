// ui_workspace_rail — Raylib rendering for the left workspace rail.
//
// cmux-style chrome: a WORKSPACES header with a "+" button, two-line rows
// (title over branch — or over the attention message when one is unread),
// attention dots on the trailing edge, a SPLITS section that only appears
// when the active tab is split, a clickable notification strip, and footer
// shortcut hints. All rectangles come pre-computed from the model; click
// handling lives in main.c via workspace_rail_hit().
#include "ui_workspace_rail.h"

#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PAD_X          12      // left padding for header/section/notification text
#define NUM_X          13      // left edge of the row-number column
#define ROW_TEXT_X     34      // left edge of row text (after the number column)
#define ACTIVE_BAR_W    3      // accent bar width on the active row
#define DOT_R           4      // attention dot radius

// Font sizes (logical px).
#define FS_HEADER  10
#define FS_PRIMARY 13
#define FS_SUB     11

static Color attention_color(WorkspaceAttention level)
{
    switch (level) {
    case WORKSPACE_ATTENTION_INFO:
        return UI2RAY(g_ui_theme.accent);
    case WORKSPACE_ATTENTION_WARN:
        return UI2RAY(g_ui_theme.inline_error);
    case WORKSPACE_ATTENTION_ERROR:
        // Stronger red than inline_error for dead sessions.
        return (Color){ 220, 40, 40, 255 };
    default:
        return (Color){ 0, 0, 0, 0 };   // fully transparent = no marker
    }
}

static Color with_alpha(Color c, unsigned char a)
{
    c.a = a;
    return c;
}

static Color working_color(void)
{
    // Subtle green accent with a soft alpha pulse.
    float pulse = sinf(GetTime() * 3.0f) * 0.3f + 0.5f;   // 0.2 .. 0.8
    Color c = UI2RAY(g_ui_theme.accent);
    c.a = (unsigned char)(80 + (unsigned char)(pulse * 50));
    return c;
}

static bool in_rect(int mx, int my, int x, int y, int w, int h)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// Draw text left-anchored at (x, y), clipped to max_w with a trailing "..",
// never cutting a UTF-8 sequence in half.
static void draw_text_clipped(Font font, const char *text, float x, float y,
                              float size, Color color, float max_w)
{
    if (max_w <= 0) return;
    if (MeasureTextEx(font, text, size, 0).x <= max_w) {
        DrawTextEx(font, text, (Vector2){ x, y }, size, 0, color);
        return;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", text);
    int len = (int)strlen(buf);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80)
            len--;
        buf[len] = '\0';
        char tmp[168];
        snprintf(tmp, sizeof(tmp), "%s..", buf);
        if (MeasureTextEx(font, tmp, size, 0).x <= max_w || len == 0) {
            DrawTextEx(font, tmp, (Vector2){ x, y }, size, 0, color);
            return;
        }
    }
}

static void draw_row(Font font, const WorkspaceRailView *view,
                     const WorkspaceRailRow *row, bool hovered)
{
    int x = view->x, w = view->w;

    if (row->active) {
        DrawRectangle(x, row->y, w, row->h,
                      with_alpha(UI2RAY(g_ui_theme.selection), 60));
        DrawRectangle(x, row->y, ACTIVE_BAR_W, row->h, UI2RAY(g_ui_theme.accent));
    } else if (hovered) {
        DrawRectangle(x, row->y, w, row->h,
                      with_alpha(UI2RAY(g_ui_theme.selection), 28));
    }

    char num[8];
    snprintf(num, sizeof(num), "%d", row->index + 1);

    if (view->compact) {
        // Number centered, attention dot in the top-right corner.
        Vector2 sz = MeasureTextEx(font, num, FS_PRIMARY, 0);
        DrawTextEx(font, num,
                   (Vector2){ x + ((float)w - sz.x) / 2.0f,
                              row->y + ((float)row->h - sz.y) / 2.0f },
                   FS_PRIMARY, 0,
                   row->active ? UI2RAY(g_ui_theme.text)
                               : UI2RAY(g_ui_theme.subtitle));
        Color dot = attention_color(row->attention);
        if (dot.a)
            DrawCircle(x + w - 10, row->y + 10, 3, dot);
        if (row->working) {
            Color wc = working_color();
            DrawCircle(x + w - 10, row->y + row->h - 10, 3, wc);
        }
        return;
    }

    // Number column — the row's Cmd/Ctrl+<n> target.
    DrawTextEx(font, num,
               (Vector2){ (float)(x + NUM_X), (float)(row->y + 9) },
               FS_SUB, 0, UI2RAY(g_ui_theme.subtitle));

    // Attention dot on the trailing edge reserves label space.
    Color dot = attention_color(row->attention);
    float text_right = (float)(x + w - PAD_X);
    if (dot.a) {
        DrawCircle(x + w - PAD_X - DOT_R, row->y + row->h / 2, DOT_R, dot);
        text_right -= DOT_R * 2 + 8;
    }
    // Working marker: subtle pulsing dot inside the trailing area.
    if (row->working) {
        Color wc = working_color();
        float wx = dot.a ? text_right - DOT_R : (float)(x + w - PAD_X - DOT_R);
        DrawCircle((int)wx, row->y + row->h / 2, DOT_R, wc);
        text_right -= DOT_R * 2 + 8;
    }
    float max_w = text_right - (float)(x + ROW_TEXT_X);

    // Primary line: agent/window title or cwd label.
    const char *primary = row->label[0] ? row->label : "shell";
    Color primary_col = row->active ? UI2RAY(g_ui_theme.text)
                                    : with_alpha(UI2RAY(g_ui_theme.text), 215);
    draw_text_clipped(font, primary, (float)(x + ROW_TEXT_X),
                      (float)(row->y + 7), FS_PRIMARY, primary_col, max_w);

    // Secondary line: the unread message when there is one, else the branch.
    if (row->text[0] && row->attention != WORKSPACE_ATTENTION_NONE) {
        draw_text_clipped(font, row->text, (float)(x + ROW_TEXT_X),
                          (float)(row->y + 25), FS_SUB,
                          attention_color(row->attention), max_w);
    } else if (row->branch[0]) {
        draw_text_clipped(font, row->branch, (float)(x + ROW_TEXT_X),
                          (float)(row->y + 25), FS_SUB,
                          UI2RAY(g_ui_theme.subtitle), max_w);
    }
}

void ui_workspace_rail_draw(Font font, const WorkspaceRailView *view,
                            int mouse_x, int mouse_y)
{
    int x = view->x, y = view->y, w = view->w, h = view->h;

    // Background + right separator.
    DrawRectangle(x, y, w, h, UI2RAY(g_ui_theme.panel_bg));
    DrawRectangle(x + w - 1, y, 1, h, UI2RAY(g_ui_theme.sidebar_separator));

    // --- Header: WORKSPACES + [+] ---
    if (!view->compact) {
        DrawTextEx(font, "WORKSPACES",
                   (Vector2){ (float)(x + PAD_X),
                              (float)(view->header_y + (view->header_h - FS_HEADER) / 2 - 1) },
                   FS_HEADER, 1.5f, UI2RAY(g_ui_theme.subtitle));
    }
    {
        bool hov = in_rect(mouse_x, mouse_y, view->plus_x, view->plus_y,
                           view->plus_w, view->plus_h);
        if (hov)
            DrawRectangleRounded((Rectangle){ (float)view->plus_x, (float)view->plus_y,
                                              (float)view->plus_w, (float)view->plus_h },
                                 0.3f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 50));
        Color pc = hov ? UI2RAY(g_ui_theme.text) : UI2RAY(g_ui_theme.subtitle);
        int cx = view->plus_x + view->plus_w / 2;
        int cy = view->plus_y + view->plus_h / 2;
        DrawRectangle(cx - 5, cy, 11, 1, pc);
        DrawRectangle(cx, cy - 5, 1, 11, pc);
    }

    // --- Notification strip (clickable: jumps to the offending pane) ---
    if (view->notif_h > 0) {
        Color sev = attention_color(view->notification_level);
        if (sev.a == 0)
            sev = UI2RAY(g_ui_theme.accent);
        bool hov = view->notification_pane != 0
                   && in_rect(mouse_x, mouse_y, x, view->notif_y, w, view->notif_h);
        DrawRectangle(x, view->notif_y, w, view->notif_h,
                      with_alpha(sev, hov ? 48 : 28));
        DrawRectangle(x, view->notif_y, ACTIVE_BAR_W, view->notif_h, sev);
        draw_text_clipped(font, view->notification, (float)(x + PAD_X),
                          (float)(view->notif_y + (view->notif_h - FS_SUB) / 2 - 1),
                          FS_SUB, UI2RAY(g_ui_theme.text),
                          (float)(w - PAD_X - 8));
    }

    // --- Workspace rows ---
    for (int i = 0; i < view->tab_count; i++) {
        const WorkspaceRailRow *row = &view->tabs[i];
        bool hov = in_rect(mouse_x, mouse_y, x, row->y, w, row->h);
        draw_row(font, view, row, hov);
    }

    // --- Splits section (only when the active tab is split) ---
    if (view->show_panes) {
        if (!view->compact) {
            DrawRectangle(x, view->section_y + 3, w, 1,
                          UI2RAY(g_ui_theme.sidebar_separator));
            DrawTextEx(font, "SPLITS",
                       (Vector2){ (float)(x + PAD_X),
                                  (float)(view->section_y + view->section_h - FS_HEADER - 3) },
                       FS_HEADER, 1.5f, UI2RAY(g_ui_theme.subtitle));
        } else {
            DrawRectangle(x + 8, view->section_y + view->section_h / 2,
                          w - 16, 1, UI2RAY(g_ui_theme.sidebar_separator));
        }
        for (int i = 0; i < view->pane_count; i++) {
            const WorkspaceRailRow *row = &view->panes[i];
            bool hov = in_rect(mouse_x, mouse_y, x, row->y, w, row->h);
            draw_row(font, view, row, hov);
        }
    }

    // --- Footer: shortcut hints ---
    if (view->footer_h > 0) {
        DrawRectangle(x, view->footer_y, w, 1, UI2RAY(g_ui_theme.sidebar_separator));
#ifdef __APPLE__
        const char *hints = "cmd+T new   cmd+1-9 go   cmd+sh+U unread";
#else
        const char *hints = "ctrl+sh+T new   ctrl+sh+1-9 go   +U unread";
#endif
        draw_text_clipped(font, hints, (float)(x + PAD_X),
                          (float)(view->footer_y + (view->footer_h - FS_HEADER) / 2),
                          FS_HEADER, with_alpha(UI2RAY(g_ui_theme.subtitle), 200),
                          (float)(w - PAD_X * 2));
    }
}