// ui_workspace_rail_model — Pure presentation model for the workspace rail.
//
// This module builds row descriptors from session snapshots, assigns row
// geometry, and answers click hit tests. It contains no Raylib code and has
// dedicated tests. The raylib layer (ui_workspace_rail.c) only renders the
// laid-out view; main.c uses the same layout for pre-draw click handling, so
// drawing and hit-testing can never drift apart.
#ifndef FANGS_UI_WORKSPACE_RAIL_MODEL_H
#define FANGS_UI_WORKSPACE_RAIL_MODEL_H

#include <stdint.h>
#include <stdbool.h>

#include "workspace_status.h"

#define WORKSPACE_RAIL_MAX_TABS  9
#define WORKSPACE_RAIL_MAX_PANES 64

// Section geometry (logical px) — shared by layout, hit-testing, and drawing.
#define WORKSPACE_RAIL_HEADER_H       32
#define WORKSPACE_RAIL_NOTIF_H        26
#define WORKSPACE_RAIL_SECTION_H      24
#define WORKSPACE_RAIL_FOOTER_H       26
#define WORKSPACE_RAIL_ROW_H          44
#define WORKSPACE_RAIL_ROW_H_COMPACT  40

typedef enum {
    WORKSPACE_RAIL_ACTION_NONE = 0,
    WORKSPACE_RAIL_ACTION_SWITCH_TAB,
    WORKSPACE_RAIL_ACTION_FOCUS_PANE,
    WORKSPACE_RAIL_ACTION_NEW_TAB,        // "+" button
    WORKSPACE_RAIL_ACTION_JUMP_ATTENTION, // notification strip click
} WorkspaceRailActionType;

// Click result — pure data shared by main.c and the raylib layer.
typedef struct {
    WorkspaceRailActionType type;
    int      index;    // 0-based tab/pane index for SWITCH_TAB / FOCUS_PANE
    uint64_t pane_id;  // target pane for FOCUS_PANE / JUMP_ATTENTION
} WorkspaceRailAction;

typedef struct {
    uint64_t id;               // pane_id for focus actions
    int      index;            // 0-based tab or pane index
    int      active;           // 1 if this row is the focused item
    int      working;          // 1 if recent output detected
    WorkspaceAttention attention;
    char     label[64];        // primary line: agent/window title, else cwd label
    char     branch[64];       // secondary line: git branch (empty if none)
    char     text[128];        // attention text — replaces the branch line when set
    int      y, h;             // row rect (set by workspace_rail_layout)
} WorkspaceRailRow;

typedef struct {
    WorkspaceRailRow tabs[WORKSPACE_RAIL_MAX_TABS];
    int  tab_count;
    WorkspaceRailRow panes[WORKSPACE_RAIL_MAX_PANES];
    int  pane_count;
    char notification[128];    // highest-priority unread notification
    uint64_t notification_pane;// pane the notification refers to (0 = none)
    WorkspaceAttention notification_level; // severity of that notification
    int  compact;              // non-zero for compact mode (numeric only)
    int  show_panes;           // pane section visible (active tab has >1 pane)

    // Geometry, set by workspace_rail_layout. Hidden parts have height 0.
    int x, y, w, h;                       // rail bounds
    int header_y, header_h;               // "WORKSPACES" header
    int plus_x, plus_y, plus_w, plus_h;   // "+" new-workspace button
    int notif_y, notif_h;                 // notification strip
    int section_y, section_h;             // "SPLITS" section header
    int footer_y, footer_h;               // shortcut hints
} WorkspaceRailView;

// Input descriptor — keeps the builder pure (no App dependency).
// Primary-line precedence: name (user rename) > title (agent) > label (cwd).
typedef struct {
    uint64_t     id;
    const char  *label;        // cwd label, may be NULL
    const char  *branch;       // git branch, may be NULL or empty
    const char  *title;        // OSC 0/2 window title, may be NULL or empty
    const char  *name;         // user-set workspace name, may be NULL or empty
    int          active;       // 1 if focused
    int          working;      // 1 if recent output detected
} WorkspaceRailInput;

// Build the view model from input arrays and workspace status.
void workspace_rail_build(WorkspaceRailView *view,
                          const WorkspaceRailInput *tab_inputs, int tab_count,
                          const WorkspaceRailInput *pane_inputs, int pane_count,
                          const WorkspaceStatus *status,
                          int compact);

// Assign row and section rectangles inside the given bounds.
void workspace_rail_layout(WorkspaceRailView *view, int x, int y, int w, int h);

// Pure click hit test; requires workspace_rail_layout to have run.
WorkspaceRailAction workspace_rail_hit(const WorkspaceRailView *view,
                                       int mx, int my);

#endif
