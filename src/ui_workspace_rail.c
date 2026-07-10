// ui_workspace_rail — Raylib rendering for the left workspace rail.
//
// cmux-style chrome: a WORKSPACES header with a "+" / bell button, two-line
// rows (title over branch — or over the attention message when one is unread),
// attention dots on the trailing edge, dev-server port chips, a SPLITS section
// that only appears when the active tab is split, a clickable notification
// strip, footer shortcut hints, and a drag insertion line.
// All rectangles come pre-computed from the model; click handling lives in
// main.c via workspace_rail_hit().
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

// Port chip constants.
#define PORT_CHIP_H 14
#define PORT_CHIP_PAD 6
#define PORT_CHIP_R 3

const char *WORKSPACE_RAIL_COLOR_TAG_NAMES[WORKSPACE_RAIL_COLOR_TAG_COUNT] = {
    "Red", "Orange", "Yellow", "Green", "Blue", "Purple",
};
const UiColor WORKSPACE_RAIL_COLOR_TAG_COLORS[WORKSPACE_RAIL_COLOR_TAG_COUNT] = {
    { 224,  90,  90, 255 },   // Red
    { 224, 150,  70, 255 },   // Orange
    { 214, 194,  70, 255 },   // Yellow
    { 100, 190, 120, 255 },   // Green
    {  90, 150, 224, 255 },   // Blue
    { 170, 120, 214, 255 },   // Purple
};

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

static void git_badge_label(int count, char *out, int out_size)
{
    if (!out || out_size <= 0)
        return;
    if (count < 0)
        count = 0;
    snprintf(out, (size_t)out_size, "+%d", count);
}

// Short elapsed-time label for the idle-duration badge, e.g. "12s", "3m",
// "2h". idle_ms is assumed >= 0 (callers gate on the -1 "never had output"
// sentinel before calling this).
static void format_idle_label(int idle_ms, char *out, int out_size)
{
    if (!out || out_size <= 0)
        return;
    long secs = idle_ms / 1000;
    if (secs < 60)
        snprintf(out, (size_t)out_size, "%lds", secs);
    else if (secs < 3600)
        snprintf(out, (size_t)out_size, "%ldm", secs / 60);
    else
        snprintf(out, (size_t)out_size, "%ldh", secs / 3600);
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

// Draw port chips for a row: right-aligned on the secondary line, only in
// full (non-compact) mode.
static void draw_port_chips(Font font, const WorkspaceRailRow *row,
                            int max_w, int mouse_x, int mouse_y)
{
    if (row->port_count == 0 || row->port_w[0] <= 0)
        return;

    for (int i = 0; i < row->port_count && i < 3; i++) {
        int px = row->port_x[i];
        int py = row->port_y;
        int pw = row->port_w[i];
        int ph = row->port_h;

        if (pw <= 0) break;

        bool hov = in_rect(mouse_x, mouse_y, px, py, pw, ph);
        Color chip_bg = hov ? with_alpha(UI2RAY(g_ui_theme.subtitle), 60)
                            : with_alpha(UI2RAY(g_ui_theme.subtitle), 30);
        // Roundness is a 0..1 fraction of the short edge, not pixels.
        DrawRectangleRounded((Rectangle){ (float)px, (float)py,
                                          (float)pw, (float)ph },
                             0.5f, 4, chip_bg);

        char chip_label[16];
        snprintf(chip_label, sizeof(chip_label), ":%d", row->ports[i]);
        Color chip_fg = hov ? UI2RAY(g_ui_theme.text)
                            : UI2RAY(g_ui_theme.subtitle);
        Vector2 sz = MeasureTextEx(font, chip_label, FS_SUB, 0);
        DrawTextEx(font, chip_label,
                   (Vector2){ (float)(px + (pw - (int)sz.x) / 2),
                              (float)(py + (ph - (int)sz.y) / 2) },
                   FS_SUB, 0, chip_fg);
    }
}

static void draw_row(Font font, const WorkspaceRailView *view,
                     const WorkspaceRailRow *row, bool hovered,
                     int mouse_x, int mouse_y)
{
    int x = view->x, w = view->w;

    Color tag_bar = (row->color_tag > 0 && row->color_tag <= WORKSPACE_RAIL_COLOR_TAG_COUNT)
        ? UI2RAY(WORKSPACE_RAIL_COLOR_TAG_COLORS[row->color_tag - 1])
        : UI2RAY(g_ui_theme.accent);
    if (row->active) {
        DrawRectangle(x, row->y, w, row->h,
                      with_alpha(UI2RAY(g_ui_theme.selection), 60));
        DrawRectangle(x, row->y, ACTIVE_BAR_W, row->h, tag_bar);
    } else if (row->color_tag > 0) {
        // Not the active row, but tagged: show the group color regardless,
        // so tagged workspaces stay identifiable at a glance.
        DrawRectangle(x, row->y, ACTIVE_BAR_W, row->h, tag_bar);
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
        if (row->git_changed_count > 0) {
            char dirty[16];
            git_badge_label(row->git_changed_count, dirty, sizeof(dirty));
            Vector2 dsz = MeasureTextEx(font, dirty, FS_HEADER, 0);
            DrawTextEx(font, dirty,
                       (Vector2){ x + ((float)w - dsz.x) / 2.0f,
                                  (float)(row->y + row->h - 13) },
                       FS_HEADER, 0, UI2RAY(g_ui_theme.accent));
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

    // Reserve space for port chips: secondary text ends before the first chip.
    if (row->port_count > 0 && row->port_w[0] > 0) {
        float chip_area_left = (float)(row->port_x[0] - 6);
        if (chip_area_left < text_right)
            text_right = chip_area_left;
    }

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
    } else if (row->idle_ms >= 0) {
        // Not currently working: show how long it's been quiet, so you can
        // triage several running agents at a glance (which one's gone
        // quietest vs. which just finished).
        char idle_label[8];
        format_idle_label(row->idle_ms, idle_label, sizeof(idle_label));
        Vector2 isz = MeasureTextEx(font, idle_label, FS_SUB, 0);
        float ix = text_right - isz.x;
        DrawTextEx(font, idle_label,
                   (Vector2){ ix, (float)row->y + (float)row->h / 2.0f - isz.y / 2.0f },
                   FS_SUB, 0, UI2RAY(g_ui_theme.subtitle));
        text_right = ix - 8;
    }

    // Rect comes from the model (workspace_rail_layout), same as port chips —
    // drawing and hit-testing read the same numbers so they can't drift apart.
    char git_badge[16] = "";
    int git_badge_x = row->git_badge_x;
    int git_badge_y = row->git_badge_y;
    int git_badge_w = row->git_badge_w;
    int git_badge_h = row->git_badge_h;
    if (git_badge_w > 0) {
        git_badge_label(row->git_changed_count, git_badge, sizeof(git_badge));
        if (git_badge_x >= x + ROW_TEXT_X) {
            text_right = (float)(git_badge_x - 6);
        } else {
            git_badge_w = 0;
        }
    }

    float max_w = text_right - (float)(x + ROW_TEXT_X);

    // Primary line: agent/window title or cwd label.
    const char *primary = row->label[0] ? row->label : "shell";
    Color primary_col = row->active ? UI2RAY(g_ui_theme.text)
                                    : with_alpha(UI2RAY(g_ui_theme.text), 215);
    draw_text_clipped(font, primary, (float)(x + ROW_TEXT_X),
                      (float)(row->y + 7), FS_PRIMARY, primary_col, max_w);

    // Secondary line: closing text (warn tint), attention text, or branch.
    if (row->closing) {
        // Warn tint for armed-close "click again to close".
        Color warn = UI2RAY(g_ui_theme.inline_error);
        draw_text_clipped(font, row->text, (float)(x + ROW_TEXT_X),
                          (float)(row->y + 25), FS_SUB, warn, max_w);
    } else if (row->text[0] && row->attention != WORKSPACE_ATTENTION_NONE) {
        draw_text_clipped(font, row->text, (float)(x + ROW_TEXT_X),
                          (float)(row->y + 25), FS_SUB,
                          attention_color(row->attention), max_w);
    } else if (row->branch[0]) {
        draw_text_clipped(font, row->branch, (float)(x + ROW_TEXT_X),
                          (float)(row->y + 25), FS_SUB,
                          UI2RAY(g_ui_theme.subtitle), max_w);
    }

    if (git_badge_w > 0) {
        DrawRectangleRounded((Rectangle){ (float)git_badge_x, (float)git_badge_y,
                                          (float)git_badge_w, (float)git_badge_h },
                             0.5f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 38));
        Vector2 gsz = MeasureTextEx(font, git_badge, FS_SUB, 0);
        DrawTextEx(font, git_badge,
                   (Vector2){ (float)(git_badge_x + (git_badge_w - (int)gsz.x) / 2),
                              (float)(git_badge_y + (git_badge_h - (int)gsz.y) / 2) },
                   FS_SUB, 0, UI2RAY(g_ui_theme.accent));
    }

    // Port chips on the secondary line (right-aligned).
    draw_port_chips(font, row, (int)(x + w - ROW_TEXT_X), mouse_x, mouse_y);
}

void ui_workspace_rail_draw(Font font, const WorkspaceRailView *view,
                            int mouse_x, int mouse_y)
{
    int x = view->x, y = view->y, w = view->w, h = view->h;

    // Background + right separator.
    DrawRectangle(x, y, w, h, UI2RAY(g_ui_theme.panel_bg));
    DrawRectangle(x + w - 1, y, 1, h, UI2RAY(g_ui_theme.sidebar_separator));

    // --- Header: WORKSPACES + [+] + bell ---
    if (!view->compact) {
        DrawTextEx(font, "WORKSPACES",
                   (Vector2){ (float)(x + PAD_X),
                              (float)(view->header_y + (view->header_h - FS_HEADER) / 2 - 1) },
                   FS_HEADER, 1.5f, UI2RAY(g_ui_theme.subtitle));
    }

    // Bell button (notification history).
    if (view->bell_w > 0 && view->bell_h > 0) {
        bool bhov = in_rect(mouse_x, mouse_y, view->bell_x, view->bell_y,
                            view->bell_w, view->bell_h);
        Color bb = bhov ? with_alpha(UI2RAY(g_ui_theme.accent), 50)
                        : with_alpha(UI2RAY(g_ui_theme.subtitle), 30);
        DrawRectangleRounded((Rectangle){ (float)view->bell_x, (float)view->bell_y,
                                          (float)view->bell_w, (float)view->bell_h },
                             0.3f, 4, bb);
        // Badge count.
        char btext[8];
        snprintf(btext, sizeof(btext), "%d", view->bell_unseen);
        Color bfg = bhov ? UI2RAY(g_ui_theme.text) : UI2RAY(g_ui_theme.subtitle);
        Vector2 bsz = MeasureTextEx(font, btext, FS_SUB, 0);
        DrawTextEx(font, btext,
                   (Vector2){ (float)(view->bell_x + (view->bell_w - (int)bsz.x) / 2),
                              (float)(view->bell_y + (view->bell_h - (int)bsz.y) / 2) },
                   FS_SUB, 0, bfg);
    }

    // "+" new-workspace button.
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
        draw_row(font, view, row, hov, mouse_x, mouse_y);
    }

    // --- Drag insertion line ---
    if (view->drag_slot >= 0 && view->tab_count > 0) {
        int slot_y;
        if (view->drag_slot == 0) {
            slot_y = view->tabs[0].y;
        } else if (view->drag_slot >= view->tab_count) {
            slot_y = view->tabs[view->tab_count - 1].y
                   + view->tabs[view->tab_count - 1].h;
        } else {
            slot_y = view->tabs[view->drag_slot].y;
        }
        // Dim the dragged row.
        int drag_idx = view->drag_from;
        if (drag_idx >= 0 && drag_idx < view->tab_count) {
            DrawRectangle(x, view->tabs[drag_idx].y, w,
                          view->tabs[drag_idx].h,
                          (Color){ 0, 0, 0, 120 });
        }
        // Accent line at the slot.
        DrawRectangle(x, slot_y - 1, w, 2, UI2RAY(g_ui_theme.accent));
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
            draw_row(font, view, row, hov, mouse_x, mouse_y);
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
