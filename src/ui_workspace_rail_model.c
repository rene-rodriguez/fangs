// ui_workspace_rail_model — Pure presentation model.
#include "ui_workspace_rail_model.h"

#include <stdio.h>
#include <string.h>

static float rail_scale(int font_size);
static int rail_scaled(int value, int font_size);

// Copy printable-ASCII bytes only, collapsing space runs. The UI font atlas
// carries just basic Latin (plus box drawing), so any other codepoint would
// render as '?' — agent titles like "✳ fixing tests" degrade to
// "fixing tests" instead.
static void copy_display(char *dst, int dst_size, const char *src)
{
    int n = 0;
    bool last_space = true;   // also trims leading spaces
    for (const char *p = src ? src : ""; *p && n < dst_size - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c > 0x7E) continue;
        if (c == ' ' && last_space) continue;
        last_space = (c == ' ');
        dst[n++] = (char)c;
    }
    while (n > 0 && dst[n - 1] == ' ') n--;
    dst[n] = '\0';
}

void workspace_rail_build(WorkspaceRailView *view,
                          const WorkspaceRailInput *tab_inputs, int tab_count,
                          const WorkspaceRailInput *pane_inputs, int pane_count,
                          const WorkspaceStatus *status,
                          int compact)
{
    memset(view, 0, sizeof(*view));
    view->compact = compact;
    view->drag_slot = -1;
    view->drag_from = -1;

    // Build tab rows.
    if (tab_count > WORKSPACE_RAIL_MAX_TABS)
        tab_count = WORKSPACE_RAIL_MAX_TABS;
    view->tab_count = tab_count;

    uint64_t notification_ids[WORKSPACE_RAIL_MAX_TABS + WORKSPACE_RAIL_MAX_PANES];
    int notification_id_count = 0;

    for (int i = 0; i < tab_count; i++) {
        WorkspaceRailRow *row = &view->tabs[i];
        const WorkspaceRailInput *in = &tab_inputs[i];
        row->id    = in->id;
        row->index = i;
        row->active = in->active;
        row->working = in->working ? 1 : 0;
        row->idle_ms = in->idle_ms;
        row->color_tag = in->color_tag;
        row->git_changed_count = in->git_changed_count > 0
            ? in->git_changed_count : 0;

        if (in->id != 0 && notification_id_count < (int)(sizeof(notification_ids) / sizeof(notification_ids[0])))
            notification_ids[notification_id_count++] = in->id;

        if (compact) {
            // Compact: numeric label only, no branch.
            snprintf(row->label, sizeof(row->label), "%d", i + 1);
            row->branch[0] = '\0';
        } else {
            // A user-set name beats the agent/window title, which beats the
            // cwd label. Agents like Claude Code keep the title updated with
            // their task; a rename pins the row regardless.
            copy_display(row->label, sizeof(row->label), in->name);
            if (row->label[0] == '\0')
                copy_display(row->label, sizeof(row->label), in->title);
            if (row->label[0] == '\0')
                copy_display(row->label, sizeof(row->label), in->label);
            copy_display(row->branch, sizeof(row->branch), in->branch);
        }

        // Armed-close flag.
        row->closing = in->closing ? 1 : 0;

        // Attention level from workspace status.
        row->attention = workspace_status_level(status, in->id);

        // Closing text wins over attention text.
        if (row->closing) {
            snprintf(row->text, sizeof(row->text), "click again to close");
        } else {
            copy_display(row->text, sizeof(row->text),
                         workspace_status_text(status, in->id));
        }

        // Copy dev-server ports.
        row->port_count = in->port_count;
        for (int p = 0; p < in->port_count && p < 3; p++)
            row->ports[p] = in->ports[p];
    }

    // Build pane rows for active tab.
    if (pane_count > WORKSPACE_RAIL_MAX_PANES)
        pane_count = WORKSPACE_RAIL_MAX_PANES;
    view->pane_count = pane_count;
    view->show_panes = (pane_count > 1) ? 1 : 0;

    for (int i = 0; i < pane_count; i++) {
        WorkspaceRailRow *row = &view->panes[i];
        const WorkspaceRailInput *in = &pane_inputs[i];
        row->id    = in->id;
        row->index = i;
        row->active = in->active;
        row->working = in->working ? 1 : 0;
        row->idle_ms = in->idle_ms;
        row->color_tag = in->color_tag;
        row->git_changed_count = in->git_changed_count > 0
            ? in->git_changed_count : 0;

        if (in->id != 0 && notification_id_count < (int)(sizeof(notification_ids) / sizeof(notification_ids[0])))
            notification_ids[notification_id_count++] = in->id;

        if (compact) {
            snprintf(row->label, sizeof(row->label), "%d", i + 1);
            row->branch[0] = '\0';
        } else {
            copy_display(row->label, sizeof(row->label), in->name);
            if (row->label[0] == '\0')
                copy_display(row->label, sizeof(row->label), in->title);
            if (row->label[0] == '\0')
                copy_display(row->label, sizeof(row->label), in->label);
            copy_display(row->branch, sizeof(row->branch), in->branch);
        }

        // Armed-close flag.
        row->closing = in->closing ? 1 : 0;

        row->attention = workspace_status_level(status, in->id);

        // Closing text wins over attention text.
        if (row->closing) {
            snprintf(row->text, sizeof(row->text), "click again to close");
        } else {
            copy_display(row->text, sizeof(row->text),
                         workspace_status_text(status, in->id));
        }

        // Copy dev-server ports.
        row->port_count = in->port_count;
        for (int p = 0; p < in->port_count && p < 3; p++)
            row->ports[p] = in->ports[p];
    }

    // Build notification string from tab representatives and visible pane IDs.
    char raw_note[128];
    workspace_status_notification(status, notification_ids, notification_id_count,
                                  raw_note, sizeof(raw_note));
    copy_display(view->notification, sizeof(view->notification), raw_note);
    view->notification_pane = workspace_status_top_pane(status, notification_ids,
                                                        notification_id_count);
    view->notification_level = workspace_status_highest(status, notification_ids,
                                                        notification_id_count);
}

static bool hit_rect(int mx, int my, int rx, int ry, int rw, int rh)
{
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}

// Compute port chip rects for a single row. Chips are right-aligned inside the
// row (just left of the right-padding), using estimated font metrics: 8px per
// char + 6px internal padding, 14px tall, bottom-aligned in the row.
static void row_layout_port_chips(WorkspaceRailRow *row, int row_w,
                                    int font_size)
{
    const int gap = rail_scaled(4, font_size);
    const int chip_h = rail_scaled(14, font_size);
    int cx = row_w - rail_scaled(8, font_size); // right padding

    row->port_y = row->y + row->h - chip_h - rail_scaled(4, font_size); // bottom of secondary line area
    row->port_h = chip_h;

    for (int i = row->port_count - 1; i >= 0; i--) {
        int p = row->ports[i];
        if (p <= 0) { row->port_w[i] = 0; continue; }
        int digits = 1;
        if (p >= 10000) digits = 5; else if (p >= 1000) digits = 4;
        else if (p >= 100) digits = 3; else if (p >= 10) digits = 2;
        // width = ":" prefix + digits, scaled 8px per char, + scaled 6px padding
        int cw = (digits + 1) * rail_scaled(8, font_size) + rail_scaled(6, font_size);
        cx -= cw;
        row->port_x[i] = cx;
        row->port_w[i] = cw;
        cx -= gap;
    }
}

// Estimated width (px) reserved by the trailing indicators drawn to the
// right of the badge/label area on the row's secondary line — the dot
// (attention) and the working-pulse/idle-duration text. Fixed estimates
// (not MeasureTextEx) for the same reason row_layout_port_chips uses them:
// this module stays Raylib-free. A few px of slack here only shifts the
// badge's click zone slightly; it doesn't affect what's drawn.
static int row_estimate_trailing_w(const WorkspaceRailRow *row, int font_size)
{
    int w = 0;
    if (row->attention != WORKSPACE_ATTENTION_NONE)
        w += rail_scaled(16, font_size); // attention dot + gap
    if (row->working)
        w += rail_scaled(16, font_size); // working pulse + gap
    else if (row->idle_ms >= 0)
        w += rail_scaled(36, font_size); // idle duration text ("12m", "3h", ...) + gap
    return w;
}

// Compute the git-changed badge rect for a single row. Mirrors
// row_layout_port_chips' char-width estimate ("+" plus digit count, 8px per
// char + 6px padding) and must run after that function since the badge sits
// immediately left of the port chip block when one is present.
static void row_layout_git_badge(WorkspaceRailRow *row, int row_w,
                                   int font_size)
{
    if (row->git_changed_count <= 0) {
        row->git_badge_x = 0;
        row->git_badge_y = 0;
        row->git_badge_w = 0;
        row->git_badge_h = 0;
        return;
    }

    int right_edge = row_w - rail_scaled(8, font_size) - row_estimate_trailing_w(row, font_size);
    if (row->port_count > 0 && row->port_w[0] > 0) {
        int chip_area_left = row->port_x[0] - rail_scaled(6, font_size);
        if (chip_area_left < right_edge)
            right_edge = chip_area_left;
    }

    int count = row->git_changed_count;
    int digits = 1;
    if (count >= 10000) digits = 5; else if (count >= 1000) digits = 4;
    else if (count >= 100) digits = 3; else if (count >= 10) digits = 2;
    int bw = (digits + 1) * rail_scaled(8, font_size) + rail_scaled(6, font_size); // "+" prefix + digits

    row->git_badge_h = rail_scaled(14, font_size);
    row->git_badge_y = row->y + row->h - row->git_badge_h - rail_scaled(4, font_size);
    row->git_badge_w = bw;
    row->git_badge_x = right_edge - bw;
}

static float rail_scale(int font_size)
{
    return (float)font_size / (float)WORKSPACE_RAIL_BASE_FONT_SIZE;
}

static int rail_scaled(int value, int font_size)
{
    return (int)(value * rail_scale(font_size) + 0.5f);
}

void workspace_rail_layout(WorkspaceRailView *view, int x, int y, int w, int h,
                           int font_size)
{
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;

    int row_h = view->compact
        ? rail_scaled(WORKSPACE_RAIL_ROW_H_COMPACT, font_size)
        : rail_scaled(WORKSPACE_RAIL_ROW_H, font_size);
    int cur = y;

    // Header with the "+" new-workspace button and bell (history) button.
    view->header_y = cur;
    view->header_h = rail_scaled(WORKSPACE_RAIL_HEADER_H, font_size);
    view->plus_w = rail_scaled(22, font_size);
    view->plus_h = rail_scaled(22, font_size);
    view->plus_y = cur + (view->header_h - view->plus_h) / 2;
    view->plus_x = view->compact ? x + (w - view->plus_w) / 2
                                 : x + w - view->plus_w - rail_scaled(8, font_size);
    // Bell button: right side of the header, left of "+", only in full mode
    // with unseen events. (The left side belongs to the WORKSPACES title.)
    if (!view->compact && view->bell_unseen > 0) {
        view->bell_w = rail_scaled(22, font_size);
        view->bell_h = rail_scaled(22, font_size);
        view->bell_y = cur + (view->header_h - view->bell_h) / 2;
        view->bell_x = view->plus_x - view->bell_w - rail_scaled(6, font_size);
    } else {
        view->bell_x = 0;
        view->bell_y = 0;
        view->bell_w = 0;
        view->bell_h = 0;
    }

    // Header icon cluster (full mode only): rail-collapse toggle, then the
    // two split-direction buttons, positioned left of the bell/plus cluster
    // using the same "subtract from the current leftmost edge" pattern
    // already used above for bell_x relative to plus_x.
    if (!view->compact) {
        int cluster_left = (view->bell_w > 0) ? view->bell_x : view->plus_x;

        view->split_down_w = rail_scaled(22, font_size);
        view->split_down_h = rail_scaled(22, font_size);
        view->split_down_y = cur + (view->header_h - view->split_down_h) / 2;
        view->split_down_x = cluster_left - view->split_down_w - rail_scaled(6, font_size);

        view->split_right_w = rail_scaled(22, font_size);
        view->split_right_h = rail_scaled(22, font_size);
        view->split_right_y = cur + (view->header_h - view->split_right_h) / 2;
        view->split_right_x = view->split_down_x - view->split_right_w - rail_scaled(6, font_size);

        view->toggle_w = rail_scaled(22, font_size);
        view->toggle_h = rail_scaled(22, font_size);
        view->toggle_y = cur + (view->header_h - view->toggle_h) / 2;
        view->toggle_x = view->split_right_x - view->toggle_w - rail_scaled(6, font_size);
    } else {
        view->toggle_x = view->toggle_y = view->toggle_w = view->toggle_h = 0;
        view->split_right_x = view->split_right_y = view->split_right_w = view->split_right_h = 0;
        view->split_down_x = view->split_down_y = view->split_down_w = view->split_down_h = 0;
    }

    cur += view->header_h;

    // Notification strip (full mode, only when there is something to say).
    if (view->notification[0] != '\0' && !view->compact) {
        view->notif_y = cur;
        view->notif_h = rail_scaled(WORKSPACE_RAIL_NOTIF_H, font_size);
        cur += view->notif_h;
    } else {
        view->notif_y = cur;
        view->notif_h = 0;
    }

    // Workspace (tab) rows.
    for (int i = 0; i < view->tab_count; i++) {
        view->tabs[i].y = cur;
        view->tabs[i].h = row_h;
        cur += row_h;
    }

    // Splits section — only when the active tab actually has splits.
    if (view->show_panes) {
        view->section_y = cur;
        view->section_h = view->compact ? rail_scaled(9, font_size)
                                        : rail_scaled(WORKSPACE_RAIL_SECTION_H, font_size);
        cur += view->section_h;
        for (int i = 0; i < view->pane_count; i++) {
            view->panes[i].y = cur;
            view->panes[i].h = row_h;
            cur += row_h;
        }
    } else {
        view->section_y = cur;
        view->section_h = 0;
        for (int i = 0; i < view->pane_count; i++) {
            view->panes[i].y = cur;
            view->panes[i].h = 0;
        }
    }

    // Footer hints pinned to the bottom (full mode, when there is room).
    if (!view->compact && cur + rail_scaled(WORKSPACE_RAIL_FOOTER_H, font_size) <= y + h) {
        view->footer_h = rail_scaled(WORKSPACE_RAIL_FOOTER_H, font_size);
        view->footer_y = y + h - view->footer_h;
    } else {
        view->footer_y = y + h;
        view->footer_h = 0;
    }

    // Layout port chips for all visible rows.
    if (!view->compact) {
        for (int i = 0; i < view->tab_count; i++)
            row_layout_port_chips(&view->tabs[i], w, font_size);
        for (int i = 0; i < view->pane_count; i++)
            if (view->panes[i].h > 0)
                row_layout_port_chips(&view->panes[i], w, font_size);

        // Git-changed badge rects — depends on port chip rects above, so it
        // must run after the port chip layout loop.
        for (int i = 0; i < view->tab_count; i++)
            row_layout_git_badge(&view->tabs[i], w, font_size);
        for (int i = 0; i < view->pane_count; i++)
            if (view->panes[i].h > 0)
                row_layout_git_badge(&view->panes[i], w, font_size);
    }
}

// Check port chips inside a row; returns true and sets \`a\` if hit.
static bool hit_row_port_chips(const WorkspaceRailRow *row, int row_x, int mx, int my,
                               WorkspaceRailAction *a)
{
    if (row->port_h <= 0 || row->port_count <= 0)
        return false;
    for (int i = 0; i < row->port_count; i++) {
        if (row->port_w[i] > 0 && hit_rect(mx, my, row_x + row->port_x[i],
                                           row->port_y, row->port_w[i], row->port_h)) {
            a->type = WORKSPACE_RAIL_ACTION_OPEN_PORT;
            a->port = row->ports[i];
            return true;
        }
    }
    return false;
}

// Check the git-changed badge; returns true and sets `a` if hit.
static bool hit_row_git_badge(const WorkspaceRailRow *row, int row_x, int mx, int my,
                              WorkspaceRailAction *a)
{
    if (row->git_badge_w <= 0)
        return false;
    if (!hit_rect(mx, my, row_x + row->git_badge_x, row->git_badge_y,
                 row->git_badge_w, row->git_badge_h))
        return false;
    a->type = WORKSPACE_RAIL_ACTION_VIEW_DIFF;
    a->pane_id = row->id;
    return true;
}

WorkspaceRailAction workspace_rail_hit(const WorkspaceRailView *view,
                                       int mx, int my)
{
    WorkspaceRailAction a = { WORKSPACE_RAIL_ACTION_NONE, 0, 0 };

    if (!hit_rect(mx, my, view->x, view->y, view->w, view->h))
        return a;

    // Bell button (notification history) — highest priority since it overlaps
    // the header area.
    if (view->bell_w > 0 && view->bell_h > 0
        && hit_rect(mx, my, view->bell_x, view->bell_y,
                    view->bell_w, view->bell_h)) {
        a.type = WORKSPACE_RAIL_ACTION_HISTORY;
        return a;
    }

    if (view->toggle_w > 0 && hit_rect(mx, my, view->toggle_x, view->toggle_y,
                                       view->toggle_w, view->toggle_h)) {
        a.type = WORKSPACE_RAIL_ACTION_COLLAPSE_RAIL;
        return a;
    }

    if (view->split_right_w > 0 && hit_rect(mx, my, view->split_right_x, view->split_right_y,
                                            view->split_right_w, view->split_right_h)) {
        a.type = WORKSPACE_RAIL_ACTION_SPLIT_RIGHT;
        return a;
    }

    if (view->split_down_w > 0 && hit_rect(mx, my, view->split_down_x, view->split_down_y,
                                           view->split_down_w, view->split_down_h)) {
        a.type = WORKSPACE_RAIL_ACTION_SPLIT_DOWN;
        return a;
    }

    if (hit_rect(mx, my, view->plus_x, view->plus_y, view->plus_w, view->plus_h)) {
        a.type = WORKSPACE_RAIL_ACTION_NEW_TAB;
        return a;
    }

    if (view->notif_h > 0 && view->notification_pane != 0
        && hit_rect(mx, my, view->x, view->notif_y, view->w, view->notif_h)) {
        a.type = WORKSPACE_RAIL_ACTION_JUMP_ATTENTION;
        a.pane_id = view->notification_pane;
        return a;
    }

    for (int i = 0; i < view->tab_count; i++) {
        if (!hit_rect(mx, my, view->x, view->tabs[i].y, view->w, view->tabs[i].h))
            continue;
        // Git badge and port chips take priority over tab switch.
        if (hit_row_git_badge(&view->tabs[i], view->x, mx, my, &a))
            return a;
        if (hit_row_port_chips(&view->tabs[i], view->x, mx, my, &a))
            return a;
        a.type = WORKSPACE_RAIL_ACTION_SWITCH_TAB;
        a.index = i;
        return a;
    }

    if (view->show_panes) {
        for (int i = 0; i < view->pane_count; i++) {
            if (!hit_rect(mx, my, view->x, view->panes[i].y, view->w, view->panes[i].h))
                continue;
            if (hit_row_git_badge(&view->panes[i], view->x, mx, my, &a))
                return a;
            if (hit_row_port_chips(&view->panes[i], view->x, mx, my, &a))
                return a;
            a.type = WORKSPACE_RAIL_ACTION_FOCUS_PANE;
            a.index = i;
            a.pane_id = view->panes[i].id;
            return a;
        }
    }

    return a;
}

int workspace_rail_row_at(const WorkspaceRailView *view, int mx, int my,
                         bool *out_is_pane)
{
    if (out_is_pane) *out_is_pane = false;

    if (!hit_rect(mx, my, view->x, view->y, view->w, view->h))
        return -1;

    for (int i = 0; i < view->tab_count; i++) {
        if (hit_rect(mx, my, view->x, view->tabs[i].y, view->w, view->tabs[i].h))
            return i;
    }

    if (view->show_panes) {
        for (int i = 0; i < view->pane_count; i++) {
            if (hit_rect(mx, my, view->x, view->panes[i].y, view->w, view->panes[i].h)) {
                if (out_is_pane) *out_is_pane = true;
                return i;
            }
        }
    }

    return -1;
}

int workspace_rail_drop_index(const WorkspaceRailView *view, int my,
                              int font_size)
{
    if (view->tab_count == 0)
        return 0;
    int row_h = view->compact
        ? rail_scaled(WORKSPACE_RAIL_ROW_H_COMPACT, font_size)
        : rail_scaled(WORKSPACE_RAIL_ROW_H, font_size);

    if (my <= view->tabs[0].y + row_h / 2)
        return 0;

    for (int i = 0; i < view->tab_count - 1; i++) {
        int mid = view->tabs[i].y + row_h / 2;
        int next_mid = view->tabs[i + 1].y + row_h / 2;
        if (my >= mid && my < next_mid)
            return i + 1;
    }

    return view->tab_count;
}
