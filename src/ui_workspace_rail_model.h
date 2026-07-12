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
    WORKSPACE_RAIL_ACTION_OPEN_PORT,      // click a port chip
    WORKSPACE_RAIL_ACTION_HISTORY,        // bell button click
    WORKSPACE_RAIL_ACTION_VIEW_DIFF,      // click a row's git-changed badge
    WORKSPACE_RAIL_ACTION_COLLAPSE_RAIL,  // rail-toggle icon click (sidebar glyph)
    WORKSPACE_RAIL_ACTION_SPLIT_RIGHT,    // split-right icon click (two columns)
    WORKSPACE_RAIL_ACTION_SPLIT_DOWN,     // split-down icon click (two rows)
} WorkspaceRailActionType;

// Click result — pure data shared by main.c and the raylib layer.
typedef struct {
    WorkspaceRailActionType type;
    int      index;    // 0-based tab/pane index for SWITCH_TAB / FOCUS_PANE
    uint64_t pane_id;  // target pane for FOCUS_PANE / JUMP_ATTENTION
    int      port;     // port for OPEN_PORT
} WorkspaceRailAction;

typedef struct {
    uint64_t id;               // pane_id for focus actions
    int      index;            // 0-based tab or pane index
    int      active;           // 1 if this row is the focused item
    int      working;          // 1 if recent output detected
    int      idle_ms;          // ms since last output; -1 = never had output
    int      color_tag;        // 0 = none, else 1-based palette index
    int      git_changed_count;// dirty/untracked file count for +N badge
    WorkspaceAttention attention;
    char     label[64];        // primary line: agent/window title, else cwd label
    char     branch[64];       // secondary line: git branch (empty if none)
    char     text[128];        // attention text — replaces the branch line when set
    int      closing;          // 1 if row is in armed-close state (set by host)
    int      y, h;             // row rect (set by workspace_rail_layout)
    int      ports[3];         // port numbers, ascending (0 = unused)
    int      port_count;
    int      port_x[3];        // chip rect x-positions (set by layout)
    int      port_w[3];        // chip rect widths (set by layout)
    int      port_y, port_h;   // chip rect common y/h (set by layout)
    int      git_badge_x, git_badge_y; // git-changed badge rect (set by layout)
    int      git_badge_w, git_badge_h; // 0 width = not shown/not clickable
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

    // Header icon cluster (full mode only): rail-collapse toggle + the two
    // split-direction buttons, left of the bell/plus buttons. Hidden (all
    // dims 0) in compact mode — mirrors how bell_w/h are zeroed when hidden.
    int toggle_x, toggle_y, toggle_w, toggle_h;
    int split_right_x, split_right_y, split_right_w, split_right_h;
    int split_down_x, split_down_y, split_down_w, split_down_h;

    // Bell button (notification history). Host sets bell_seen to the count
    // of events at last open; layout computes rect; hidden when bell_unseen
    // is 0 (layout zeroes bell_w/bell_h).
    int bell_unseen;                      // events newer than last popover open
    int bell_x, bell_y, bell_w, bell_h;   // bell button rect (set by layout)

    // Drag reorder: host sets drag_slot to the insertion index (0..tab_count)
    // when a tab is being dragged; -1 means no active drag.  Draw layer
    // renders an insertion line at that slot.  drag_from is the source tab
    // index (drawn dimmed during drag).
    int drag_slot;
    int drag_from;

    // Geometry, set by workspace_rail_layout. Hidden parts have height 0.
    int x, y, w, h;                       // rail bounds
    int header_y, header_h;               // "WORKSPACES" header
    int plus_x, plus_y, plus_w, plus_h;   // "+" new-workspace button
    int notif_y, notif_h;                 // notification strip
    int section_y, section_h;             // "SPLITS" section header
    int footer_y, footer_h;               // shortcut hints

    // Transient animation state set by host on a new notification ring.
    float ring_pulse;                     // 0..1, decays to 0 over ~700 ms
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
    int          idle_ms;      // ms since last output; -1 = never had output
    int          color_tag;    // 0 = none, else 1-based palette index
    int          git_changed_count; // dirty/untracked file count for +N badge
    int          closing;      // 1 if row is in armed-close state (set by host)
    int          ports[3];     // dev-server port numbers, ascending (0 = unused)
    int          port_count;
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

// Return the drop slot index (0..tab_count) for a vertical position my,
// relative to the tab rows. Requires workspace_rail_layout to have run.
// 0 = before the first tab, tab_count = after the last.
int workspace_rail_drop_index(const WorkspaceRailView *view, int my);

// Row (tab or pane) under (mx, my), restricted to row y-ranges only (not
// header/footer/notification/bell areas) — the pure hover hit test.
// Returns the row's 0-based index within its own array (tabs[] or panes[],
// selected via *out_is_pane), or -1 if (mx, my) is not over any row.
int workspace_rail_row_at(const WorkspaceRailView *view, int mx, int my,
                         bool *out_is_pane);

#endif
