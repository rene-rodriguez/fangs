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

#define RING_PULSE_DECAY (1.0f / 0.7f) // decay over ~700 ms

// Font sizes (logical px).
#define FS_HEADER  10
#define FS_PRIMARY 13
#define FS_SUB     11

// Port chip constants.
#define PORT_CHIP_H 14
#define PORT_CHIP_PAD 6
#define PORT_CHIP_R 3

static float rail_scale(int font_size)
{
    return (float)font_size / 16.0f;   // baseline is 16
}

static float rail_scaled_f(int value, int font_size)
{
    return (float)value * rail_scale(font_size);
}

static int rail_scaled(int value, int font_size)
{
    return (int)(rail_scaled_f(value, font_size) + 0.5f);
}

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
                              float size, float spacing, Color color, float max_w)
{
    if (max_w <= 0) return;
    if (MeasureTextEx(font, text, size, spacing).x <= max_w) {
        DrawTextEx(font, text, (Vector2){ x, y }, size, spacing, color);
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
        if (MeasureTextEx(font, tmp, size, spacing).x <= max_w || len == 0) {
            DrawTextEx(font, tmp, (Vector2){ x, y }, size, spacing, color);
            return;
        }
    }
}

// Draw a small "panel" glyph: a rounded-rect outline split by one divider
// line. Used for the rail-collapse ("sidebar"), split-right (two columns),
// and split-down (two rows) header icons — mirrors cmux's toolbar
// iconography. `vertical` selects a vertical divider (columns) vs
// horizontal (rows); `frac` is the divider's position as a fraction of the
// glyph's width/height; `fill_first` lightly fills the region before the
// divider (used only by the sidebar-toggle glyph, per its "left segment
// filled" spec).
static void draw_panel_glyph(int bx, int by, int bw, int bh, Color fg,
                             bool vertical, float frac, bool fill_first,
                             int font_size)
{
    int inset = rail_scaled(4, font_size);
    Rectangle g = { (float)(bx + inset), (float)(by + inset),
                    (float)(bw - inset * 2), (float)(bh - inset * 2) };
    DrawRectangleRoundedLines(g, 0.2f, 3, fg);
    if (vertical) {
        int div_x = (int)(g.x + g.width * frac);
        if (fill_first)
            DrawRectangle((int)g.x + 1, (int)g.y + 1,
                         div_x - (int)g.x - 1, (int)g.height - 2,
                         with_alpha(fg, 70));
        DrawRectangle(div_x, (int)g.y, 1, (int)g.height, fg);
    } else {
        int div_y = (int)(g.y + g.height * frac);
        if (fill_first)
            DrawRectangle((int)g.x + 1, (int)g.y + 1,
                         (int)g.width - 2, div_y - (int)g.y - 1,
                         with_alpha(fg, 70));
        DrawRectangle((int)g.x, div_y, (int)g.width, 1, fg);
    }
}

// Draw port chips for a row: right-aligned on the secondary line, only in
// full (non-compact) mode.
static void draw_port_chips(Font font, const WorkspaceRailRow *row,
                            int max_w, int mouse_x, int mouse_y, int font_size)
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
        int chip_font = rail_scaled(FS_SUB, font_size);
        Vector2 sz = MeasureTextEx(font, chip_label, chip_font, 0);
        DrawTextEx(font, chip_label,
                   (Vector2){ (float)(px + (pw - (int)sz.x) / 2),
                              (float)(py + (ph - (int)sz.y) / 2) },
                   chip_font, 0, chip_fg);
    }
}

static void draw_row(Font font, const WorkspaceRailView *view,
                     const WorkspaceRailRow *row, bool hovered,
                     int mouse_x, int mouse_y, int font_size)
{
    int x = view->x, w = view->w;
    int fs_header = rail_scaled(FS_HEADER, font_size);
    int fs_primary = rail_scaled(FS_PRIMARY, font_size);
    int fs_sub = rail_scaled(FS_SUB, font_size);
    int pad_x = rail_scaled(PAD_X, font_size);
    int num_x = rail_scaled(NUM_X, font_size);
    int row_text_x = rail_scaled(ROW_TEXT_X, font_size);
    int active_bar_w = rail_scaled(ACTIVE_BAR_W, font_size);
    int dot_r_px = rail_scaled(DOT_R, font_size);

    Color tag_bar = (row->color_tag > 0 && row->color_tag <= WORKSPACE_RAIL_COLOR_TAG_COUNT)
        ? UI2RAY(WORKSPACE_RAIL_COLOR_TAG_COLORS[row->color_tag - 1])
        : UI2RAY(g_ui_theme.accent);
    if (row->active) {
        DrawRectangle(x, row->y, w, row->h,
                      with_alpha(UI2RAY(g_ui_theme.selection), 60));
        DrawRectangle(x, row->y, active_bar_w, row->h, tag_bar);
    } else if (row->color_tag > 0) {
        // Not the active row, but tagged: show the group color regardless,
        // so tagged workspaces stay identifiable at a glance.
        DrawRectangle(x, row->y, active_bar_w, row->h, tag_bar);
    } else if (hovered) {
        DrawRectangle(x, row->y, w, row->h,
                      with_alpha(UI2RAY(g_ui_theme.selection), 28));
    }

    char num[8];
    snprintf(num, sizeof(num), "%d", row->index + 1);

    if (view->compact) {
        // Number centered, attention dot in the top-right corner.
        Vector2 sz = MeasureTextEx(font, num, fs_primary, 0);
        DrawTextEx(font, num,
                   (Vector2){ x + ((float)w - sz.x) / 2.0f,
                              row->y + ((float)row->h - sz.y) / 2.0f },
                   fs_primary, 0,
                   row->active ? UI2RAY(g_ui_theme.text)
                               : UI2RAY(g_ui_theme.subtitle));
        Color dot = attention_color(row->attention);
        if (dot.a)
            DrawCircle(x + w - rail_scaled(10, font_size),
                       row->y + rail_scaled(10, font_size),
                       rail_scaled(3, font_size), dot);
        if (row->working) {
            Color wc = working_color();
            DrawCircle(x + w - rail_scaled(10, font_size),
                       row->y + row->h - rail_scaled(10, font_size),
                       rail_scaled(3, font_size), wc);
        }
        if (row->git_changed_count > 0) {
            char dirty[16];
            git_badge_label(row->git_changed_count, dirty, sizeof(dirty));
            Vector2 dsz = MeasureTextEx(font, dirty, fs_header, 0);
            DrawTextEx(font, dirty,
                       (Vector2){ x + ((float)w - dsz.x) / 2.0f,
                                  (float)(row->y + row->h - rail_scaled(13, font_size)) },
                       fs_header, 0, UI2RAY(g_ui_theme.accent));
        }
        return;
    }

    // Attention dot on the trailing edge reserves label space.
    Color sev = attention_color(row->attention);
    if (sev.a == 0) sev = UI2RAY(g_ui_theme.accent);

    // Persistent unread: keep the dot fully opaque and bright.
    if (row->attention != WORKSPACE_ATTENTION_NONE) {
        sev.a = 255;
    }

    float pulse = 0.0f;
    if (view->ring_pulse > 0.0f && row->attention != WORKSPACE_ATTENTION_NONE) {
        pulse = view->ring_pulse;
    }

    float base_r = (float)dot_r_px;
    float dot_r = base_r + base_r * 0.35f * pulse;

    // Number column — the row's Cmd/Ctrl+<n> target.
    DrawTextEx(font, num,
               (Vector2){ (float)(x + num_x),
                          (float)(row->y + rail_scaled(9, font_size)) },
               fs_sub, 0, UI2RAY(g_ui_theme.subtitle));

    float text_right = (float)(x + w - pad_x);

    // Glow ring behind the attention dot when pulsing.
    if (pulse > 0.0f) {
        Color glow = sev;
        glow.a = (unsigned char)(80 * pulse);
        DrawCircle((int)(x + w - pad_x - base_r), row->y + row->h / 2,
                   dot_r + rail_scaled_f(5, font_size), glow);
    }

    // Draw attention dot only when there is an attention state.
    if (row->attention != WORKSPACE_ATTENTION_NONE) {
        DrawCircle((int)(x + w - pad_x - base_r), row->y + row->h / 2,
                   (int)dot_r, sev);
        text_right -= base_r * 2 + rail_scaled(8, font_size);
    }

    // Reserve space for port chips: secondary text ends before the first chip.
    if (row->port_count > 0 && row->port_w[0] > 0) {
        float chip_area_left = (float)(row->port_x[0] - rail_scaled(6, font_size));
        if (chip_area_left < text_right)
            text_right = chip_area_left;
    }

    if (row->working) {
        Color wc = working_color();
        float wx = text_right - dot_r_px;
        DrawCircle((int)wx, row->y + row->h / 2, dot_r_px, wc);
        text_right -= dot_r_px * 2 + rail_scaled(8, font_size);
    } else if (row->idle_ms >= 0) {
        // Not currently working: show how long it's been quiet, so you can
        // triage several running agents at a glance (which one's gone
        // quietest vs. which just finished).
        char idle_label[8];
        format_idle_label(row->idle_ms, idle_label, sizeof(idle_label));
        Vector2 isz = MeasureTextEx(font, idle_label, fs_sub, 0);
        float ix = text_right - isz.x;
        DrawTextEx(font, idle_label,
                   (Vector2){ ix, (float)row->y + (float)row->h / 2.0f - isz.y / 2.0f },
                   fs_sub, 0, UI2RAY(g_ui_theme.subtitle));
        text_right = ix - rail_scaled(8, font_size);
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
        if (git_badge_x >= x + row_text_x) {
            text_right = (float)(git_badge_x - rail_scaled(6, font_size));
        } else {
            git_badge_w = 0;
        }
    }

    float max_w = text_right - (float)(x + row_text_x);

    // Primary line: agent/window title or cwd label.
    const char *primary = row->label[0] ? row->label : "shell";
    Color primary_col = row->active ? UI2RAY(g_ui_theme.text)
                                    : with_alpha(UI2RAY(g_ui_theme.text), 215);
    draw_text_clipped(font, primary, (float)(x + row_text_x),
                      (float)(row->y + rail_scaled(7, font_size)),
                      fs_primary, 0.0f, primary_col, max_w);

    // Secondary line: closing text (warn tint), attention text, or branch.
    if (row->closing) {
        // Warn tint for armed-close "click again to close".
        Color warn = UI2RAY(g_ui_theme.inline_error);
        draw_text_clipped(font, row->text, (float)(x + row_text_x),
                          (float)(row->y + rail_scaled(25, font_size)),
                          fs_sub, 0.0f, warn, max_w);
    } else if (row->text[0] && row->attention != WORKSPACE_ATTENTION_NONE) {
        draw_text_clipped(font, row->text, (float)(x + row_text_x),
                          (float)(row->y + rail_scaled(25, font_size)),
                          fs_sub, 0.0f,
                          attention_color(row->attention), max_w);
    } else if (row->branch[0]) {
        draw_text_clipped(font, row->branch, (float)(x + row_text_x),
                          (float)(row->y + rail_scaled(25, font_size)),
                          fs_sub, 0.0f,
                          UI2RAY(g_ui_theme.subtitle), max_w);
    }

    if (git_badge_w > 0) {
        DrawRectangleRounded((Rectangle){ (float)git_badge_x, (float)git_badge_y,
                                          (float)git_badge_w, (float)git_badge_h },
                             0.5f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 38));
        Vector2 gsz = MeasureTextEx(font, git_badge, fs_sub, 0);
        DrawTextEx(font, git_badge,
                   (Vector2){ (float)(git_badge_x + (git_badge_w - (int)gsz.x) / 2),
                              (float)(git_badge_y + (git_badge_h - (int)gsz.y) / 2) },
                   fs_sub, 0, UI2RAY(g_ui_theme.accent));
    }

    // Port chips on the secondary line (right-aligned).
    draw_port_chips(font, row, (int)(x + w - row_text_x), mouse_x, mouse_y, font_size);
}

void ui_workspace_rail_draw(Font font, WorkspaceRailView *view,
                            int mouse_x, int mouse_y, float dt, int font_size)
{
    if (view->ring_pulse > 0.0f) {
        view->ring_pulse -= dt * RING_PULSE_DECAY;
        if (view->ring_pulse < 0.0f) view->ring_pulse = 0.0f;
    }

    int x = view->x, y = view->y, w = view->w, h = view->h;
    int fs_header = rail_scaled(FS_HEADER, font_size);
    int fs_sub = rail_scaled(FS_SUB, font_size);
    int pad_x = rail_scaled(PAD_X, font_size);
    int active_bar_w = rail_scaled(ACTIVE_BAR_W, font_size);

    // Background + right separator.
    DrawRectangle(x, y, w, h, UI2RAY(g_ui_theme.panel_bg));
    DrawRectangle(x + w - 1, y, 1, h, UI2RAY(g_ui_theme.sidebar_separator));

    // --- Header: WORKSPACES + [toggle] [split-right] [split-down] + [+] + bell ---
    if (!view->compact) {
        float label_max_w = (float)(view->toggle_x - rail_scaled(6, font_size) - (x + pad_x));
        draw_text_clipped(font, "WORKSPACES", (float)(x + pad_x),
                          (float)(view->header_y + (view->header_h - fs_header) / 2 - 1),
                          fs_header, 1.5f, UI2RAY(g_ui_theme.subtitle), label_max_w);
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
        Vector2 bsz = MeasureTextEx(font, btext, fs_sub, 0);
        DrawTextEx(font, btext,
                   (Vector2){ (float)(view->bell_x + (view->bell_w - (int)bsz.x) / 2),
                              (float)(view->bell_y + (view->bell_h - (int)bsz.y) / 2) },
                   fs_sub, 0, bfg);
    }

    // Rail-collapse ("sidebar") toggle button.
    if (view->toggle_w > 0) {
        bool hov = in_rect(mouse_x, mouse_y, view->toggle_x, view->toggle_y,
                           view->toggle_w, view->toggle_h);
        if (hov)
            DrawRectangleRounded((Rectangle){ (float)view->toggle_x, (float)view->toggle_y,
                                              (float)view->toggle_w, (float)view->toggle_h },
                                 0.3f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 50));
        Color fg = hov ? UI2RAY(g_ui_theme.text) : UI2RAY(g_ui_theme.subtitle);
        draw_panel_glyph(view->toggle_x, view->toggle_y, view->toggle_w, view->toggle_h,
                         fg, true, 0.4f, true, font_size);
    }

    // Split-right ("two columns") button.
    if (view->split_right_w > 0) {
        bool hov = in_rect(mouse_x, mouse_y, view->split_right_x, view->split_right_y,
                           view->split_right_w, view->split_right_h);
        if (hov)
            DrawRectangleRounded((Rectangle){ (float)view->split_right_x, (float)view->split_right_y,
                                              (float)view->split_right_w, (float)view->split_right_h },
                                 0.3f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 50));
        Color fg = hov ? UI2RAY(g_ui_theme.text) : UI2RAY(g_ui_theme.subtitle);
        draw_panel_glyph(view->split_right_x, view->split_right_y,
                         view->split_right_w, view->split_right_h, fg, true, 0.5f, false, font_size);
    }

    // Split-down ("two rows") button.
    if (view->split_down_w > 0) {
        bool hov = in_rect(mouse_x, mouse_y, view->split_down_x, view->split_down_y,
                           view->split_down_w, view->split_down_h);
        if (hov)
            DrawRectangleRounded((Rectangle){ (float)view->split_down_x, (float)view->split_down_y,
                                              (float)view->split_down_w, (float)view->split_down_h },
                                 0.3f, 4, with_alpha(UI2RAY(g_ui_theme.accent), 50));
        Color fg = hov ? UI2RAY(g_ui_theme.text) : UI2RAY(g_ui_theme.subtitle);
        draw_panel_glyph(view->split_down_x, view->split_down_y,
                         view->split_down_w, view->split_down_h, fg, false, 0.5f, false, font_size);
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
        int half_bar = rail_scaled(5, font_size);
        int bar_len  = rail_scaled(11, font_size);
        int bar_w    = rail_scaled(1, font_size);
        if (bar_w < 1) bar_w = 1;
        DrawRectangle(cx - half_bar, cy, bar_len, bar_w, pc);
        DrawRectangle(cx, cy - half_bar, bar_w, bar_len, pc);
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
        DrawRectangle(x, view->notif_y, active_bar_w, view->notif_h, sev);
        draw_text_clipped(font, view->notification, (float)(x + pad_x),
                          (float)(view->notif_y + (view->notif_h - fs_sub) / 2 - 1),
                          fs_sub, 0.0f, UI2RAY(g_ui_theme.text),
                          (float)(w - pad_x - rail_scaled(8, font_size)));
    }

    // --- Workspace rows ---
    for (int i = 0; i < view->tab_count; i++) {
        const WorkspaceRailRow *row = &view->tabs[i];
        bool hov = in_rect(mouse_x, mouse_y, x, row->y, w, row->h);
        draw_row(font, view, row, hov, mouse_x, mouse_y, font_size);
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
            DrawRectangle(x, view->section_y + rail_scaled(3, font_size), w, 1,
                          UI2RAY(g_ui_theme.sidebar_separator));
            DrawTextEx(font, "SPLITS",
                       (Vector2){ (float)(x + pad_x),
                                  (float)(view->section_y + view->section_h - fs_header - rail_scaled(3, font_size)) },
                       fs_header, 1.5f, UI2RAY(g_ui_theme.subtitle));
        } else {
            int half = rail_scaled(8, font_size);
            int sep_y = view->section_y + view->section_h / 2;
            DrawRectangle(x + half, sep_y,
                          w - half * 2, 1, UI2RAY(g_ui_theme.sidebar_separator));
        }
        for (int i = 0; i < view->pane_count; i++) {
            const WorkspaceRailRow *row = &view->panes[i];
            bool hov = in_rect(mouse_x, mouse_y, x, row->y, w, row->h);
            draw_row(font, view, row, hov, mouse_x, mouse_y, font_size);
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
        draw_text_clipped(font, hints, (float)(x + pad_x),
                          (float)(view->footer_y + (view->footer_h - fs_header) / 2),
                          fs_header, 0.0f, with_alpha(UI2RAY(g_ui_theme.subtitle), 200),
                          (float)(w - pad_x * 2));
    }
}
