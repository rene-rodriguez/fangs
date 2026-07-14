#ifndef FANGS_LAYOUT_H
#define FANGS_LAYOUT_H

#include <stdbool.h>

// Forward declaration — defined in pane.h (included by caller at point of use).
typedef struct PaneNode PaneNode;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Rect;

typedef struct {
    Rect terminal;
    Rect sidebar;
    bool sidebar_visible;
    Rect rail;
    bool rail_visible;
    bool rail_compact;
} Layout;

Layout layout_compute(int window_w, int window_h, bool sidebar_visible,
                      int sidebar_width, int pad, int min_terminal_w);

Layout layout_compute_with_rail(int window_w, int window_h,
                                bool rail_enabled, int rail_width, int rail_compact_width,
                                bool sidebar_visible, int sidebar_width,
                                int pad, int min_terminal_w);

// Callback invoked for each leaf pane with its assigned pixel rect.
typedef void (*PaneRectFn)(const PaneNode *, int x, int y, int w, int h, void *user);

// Compute pixel rects for every leaf in the pane tree within the given
// terminal area. Calls `cb` for each leaf with its assigned rect and userdata.
// Root is the current tab's pane tree; x/y/w/h is the terminal area in pixels.
void layout_compute_panes(const PaneNode *root,
                          int term_x, int term_y, int term_w, int term_h,
                          int pane_gap,
                          PaneRectFn cb, void *user);

// Header is useful only when multiple panes need labels.
int layout_pane_header_height(int pane_count, int pane_h, float scale);

// Compute the drawable terminal content rect inside a pane chrome rect.
Rect layout_terminal_content_rect(Rect pane_rect, int header_h);

// Convert a screen-space mouse point to a terminal grid cell using the drawable
// terminal content rect. Returns false when the point is outside that content.
bool layout_terminal_cell_at(Rect terminal_content, int pad,
                             int cell_width, int cell_height,
                             int mouse_x, int mouse_y,
                             int *col, int *row);

#endif // FANGS_LAYOUT_H
