// ui_workspace_rail_model — Pure presentation model.
#include "ui_workspace_rail_model.h"

#include <stdio.h>
#include <string.h>

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

        // Attention level from workspace status.
        row->attention = workspace_status_level(status, in->id);
        copy_display(row->text, sizeof(row->text),
                     workspace_status_text(status, in->id));
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

        row->attention = workspace_status_level(status, in->id);
        copy_display(row->text, sizeof(row->text),
                     workspace_status_text(status, in->id));
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

void workspace_rail_layout(WorkspaceRailView *view, int x, int y, int w, int h)
{
    view->x = x;
    view->y = y;
    view->w = w;
    view->h = h;

    int row_h = view->compact ? WORKSPACE_RAIL_ROW_H_COMPACT : WORKSPACE_RAIL_ROW_H;
    int cur = y;

    // Header with the "+" new-workspace button.
    view->header_y = cur;
    view->header_h = WORKSPACE_RAIL_HEADER_H;
    view->plus_w = 22;
    view->plus_h = 22;
    view->plus_y = cur + (view->header_h - view->plus_h) / 2;
    view->plus_x = view->compact ? x + (w - view->plus_w) / 2
                                 : x + w - view->plus_w - 8;
    cur += view->header_h;

    // Notification strip (full mode, only when there is something to say).
    if (view->notification[0] != '\0' && !view->compact) {
        view->notif_y = cur;
        view->notif_h = WORKSPACE_RAIL_NOTIF_H;
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
        view->section_h = view->compact ? 9 : WORKSPACE_RAIL_SECTION_H;
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
    if (!view->compact && cur + WORKSPACE_RAIL_FOOTER_H <= y + h) {
        view->footer_h = WORKSPACE_RAIL_FOOTER_H;
        view->footer_y = y + h - view->footer_h;
    } else {
        view->footer_y = y + h;
        view->footer_h = 0;
    }
}

static bool hit_rect(int mx, int my, int rx, int ry, int rw, int rh)
{
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}

WorkspaceRailAction workspace_rail_hit(const WorkspaceRailView *view,
                                       int mx, int my)
{
    WorkspaceRailAction a = { WORKSPACE_RAIL_ACTION_NONE, 0, 0 };

    if (!hit_rect(mx, my, view->x, view->y, view->w, view->h))
        return a;

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
        if (hit_rect(mx, my, view->x, view->tabs[i].y, view->w, view->tabs[i].h)) {
            a.type = WORKSPACE_RAIL_ACTION_SWITCH_TAB;
            a.index = i;
            return a;
        }
    }

    if (view->show_panes) {
        for (int i = 0; i < view->pane_count; i++) {
            if (hit_rect(mx, my, view->x, view->panes[i].y, view->w, view->panes[i].h)) {
                a.type = WORKSPACE_RAIL_ACTION_FOCUS_PANE;
                a.index = i;
                a.pane_id = view->panes[i].id;
                return a;
            }
        }
    }

    return a;
}
