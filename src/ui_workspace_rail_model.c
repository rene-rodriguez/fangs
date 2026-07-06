// ui_workspace_rail_model — Pure presentation model.
#include "ui_workspace_rail_model.h"

#include <stdio.h>
#include <string.h>

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

    uint64_t tab_pane_ids[WORKSPACE_RAIL_MAX_PANES];
    int tab_pane_count = 0;

    for (int i = 0; i < tab_count; i++) {
        WorkspaceRailRow *row = &view->tabs[i];
        const WorkspaceRailInput *in = &tab_inputs[i];
        row->id    = in->id;
        row->index = i;
        row->active = in->active;

        // Store tab pane ID for highest-level aggregation.
        tab_pane_ids[tab_pane_count++] = in->id;

        if (compact) {
            // Compact: numeric label only, no branch.
            snprintf(row->label, sizeof(row->label), "%d", i + 1);
            row->branch[0] = '\0';
        } else {
            snprintf(row->label, sizeof(row->label), "%s",
                     in->label ? in->label : "");
            snprintf(row->branch, sizeof(row->branch), "%s",
                     in->branch && in->branch[0] ? in->branch : "");
        }

        // Attention level from workspace status.
        row->attention = workspace_status_level(status, in->id);
        const char *t = workspace_status_text(status, in->id);
        if (t) {
            snprintf(row->text, sizeof(row->text), "%s", t);
        }
    }

    // Build pane rows for active tab.
    if (pane_count > WORKSPACE_RAIL_MAX_PANES)
        pane_count = WORKSPACE_RAIL_MAX_PANES;
    view->pane_count = pane_count;

    uint64_t pane_ids[WORKSPACE_RAIL_MAX_PANES];
    int pane_id_count = 0;

    for (int i = 0; i < pane_count; i++) {
        WorkspaceRailRow *row = &view->panes[i];
        const WorkspaceRailInput *in = &pane_inputs[i];
        row->id    = in->id;
        row->index = i;
        row->active = in->active;

        pane_ids[pane_id_count++] = in->id;

        if (compact) {
            snprintf(row->label, sizeof(row->label), "%d", i + 1);
            row->branch[0] = '\0';
        } else {
            snprintf(row->label, sizeof(row->label), "%s",
                     in->label ? in->label : "");
            snprintf(row->branch, sizeof(row->branch), "%s",
                     in->branch && in->branch[0] ? in->branch : "");
        }

        row->attention = workspace_status_level(status, in->id);
        const char *t = workspace_status_text(status, in->id);
        if (t) {
            snprintf(row->text, sizeof(row->text), "%s", t);
        }
    }

    // Build notification string from all visible pane IDs.
    workspace_status_notification(status, pane_ids, pane_id_count,
                                  view->notification, sizeof(view->notification));
}
