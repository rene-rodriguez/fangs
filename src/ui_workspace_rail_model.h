// ui_workspace_rail_model — Pure presentation model for the workspace rail.
//
// This module builds row descriptors from session snapshots. It contains no
// Raylib code and has dedicated tests.
#ifndef FANGS_UI_WORKSPACE_RAIL_MODEL_H
#define FANGS_UI_WORKSPACE_RAIL_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#include "workspace_status.h"

#define WORKSPACE_RAIL_MAX_TABS  9
#define WORKSPACE_RAIL_MAX_PANES 64

typedef enum {
    WORKSPACE_RAIL_ACTION_NONE = 0,
    WORKSPACE_RAIL_ACTION_SWITCH_TAB,
    WORKSPACE_RAIL_ACTION_FOCUS_PANE,
} WorkspaceRailActionType;

typedef struct {
    uint64_t id;               // pane_id for focus actions
    int      index;            // 0-based tab or pane index
    int      active;           // 1 if this row is the focused item
    WorkspaceAttention attention;
    char     label[64];        // display label (cwd basename or ~ or /)
    char     branch[64];       // git branch (empty if none)
    char     text[128];        // attention text for this row
} WorkspaceRailRow;

typedef struct {
    WorkspaceRailRow tabs[WORKSPACE_RAIL_MAX_TABS];
    int  tab_count;
    WorkspaceRailRow panes[WORKSPACE_RAIL_MAX_PANES];
    int  pane_count;
    char notification[128];    // highest-priority unread notification
    int  compact;              // non-zero for compact mode (numeric only)
} WorkspaceRailView;

// Input descriptor — keeps the builder pure (no App dependency).
typedef struct {
    uint64_t     id;
    const char  *label;        // cwd label, may be NULL
    const char  *branch;       // git branch, may be NULL or empty
    int          active;       // 1 if focused
} WorkspaceRailInput;

// Build the view model from input arrays and workspace status.
void workspace_rail_build(WorkspaceRailView *view,
                          const WorkspaceRailInput *tab_inputs, int tab_count,
                          const WorkspaceRailInput *pane_inputs, int pane_count,
                          const WorkspaceStatus *status,
                          int compact);

#endif
