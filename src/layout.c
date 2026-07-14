#include "layout.h"
#include "pane.h"

static int clamp_nonnegative(int value)
{
    return value < 0 ? 0 : value;
}

Layout layout_compute(int window_w, int window_h, bool sidebar_visible,
                      int sidebar_width, int pad, int min_terminal_w)
{
    return layout_compute_with_rail(window_w, window_h, false, 260, 56,
                                    sidebar_visible, sidebar_width,
                                    pad, min_terminal_w);
}

Layout layout_compute_with_rail(int window_w, int window_h,
                                bool rail_enabled, int rail_width, int rail_compact_width,
                                bool sidebar_visible, int sidebar_width,
                                int pad, int min_terminal_w)
{
    (void)pad;

    window_w = clamp_nonnegative(window_w);
    window_h = clamp_nonnegative(window_h);
    rail_width = clamp_nonnegative(rail_width);
    rail_compact_width = clamp_nonnegative(rail_compact_width);
    sidebar_width = clamp_nonnegative(sidebar_width);
    min_terminal_w = clamp_nonnegative(min_terminal_w);

    Layout lo = {
        .terminal = {0, 0, window_w, window_h},
        .sidebar = {window_w, 0, 0, window_h},
        .sidebar_visible = false,
        .rail = {0, 0, 0, window_h},
        .rail_visible = false,
        .rail_compact = false,
    };

    /* Determine rail visibility and width */
    int rail_actual_w = 0;
    if (rail_enabled && window_w > min_terminal_w) {
        /* Check if full rail fits:
         * Full rail needs: rail_width + min_terminal_w + (sidebar if visible) */
        int needed_for_full = rail_width + min_terminal_w;
        if (needed_for_full <= window_w) {
            rail_actual_w = rail_width;
            lo.rail_compact = false;
        } else {
            /* Try compact rail */
            int needed_for_compact = rail_compact_width + min_terminal_w;
            if (needed_for_compact <= window_w) {
                rail_actual_w = rail_compact_width;
                lo.rail_compact = true;
            }
        }
    }

    if (rail_actual_w > 0) {
        lo.rail_visible = true;
        lo.rail.x = 0;
        lo.rail.y = 0;
        lo.rail.w = rail_actual_w;
        lo.rail.h = window_h;
    }

    /* Remaining width for terminal + sidebar */
    int remaining_w = window_w - rail_actual_w;

    /* Sidebar logic (same as before, using remaining_w) */
    if (!sidebar_visible || remaining_w <= min_terminal_w || sidebar_width == 0) {
        lo.terminal.x = rail_actual_w;
        lo.terminal.w = remaining_w;
        lo.sidebar.x = window_w;
        lo.sidebar.w = 0;
        return lo;
    }

    int available_sidebar_w = remaining_w - min_terminal_w;
    int actual_sidebar_w = sidebar_width < available_sidebar_w
        ? sidebar_width
        : available_sidebar_w;

    if (actual_sidebar_w <= 0) {
        lo.terminal.x = rail_actual_w;
        lo.terminal.w = remaining_w;
        lo.sidebar.x = window_w;
        lo.sidebar.w = 0;
        return lo;
    }

    lo.sidebar_visible = true;
    lo.terminal.x = rail_actual_w;
    lo.terminal.w = remaining_w - actual_sidebar_w;
    lo.sidebar.x = lo.terminal.x + lo.terminal.w;
    lo.sidebar.w = actual_sidebar_w;
    return lo;
}

// ---------------------------------------------------------------------------
// Pane layout (§16.4): compute pixel rects for each leaf in a PaneNode tree
// ---------------------------------------------------------------------------

static void compute_panes_rec(const PaneNode *n,
                              int x, int y, int w, int h,
                              int pane_gap,
                              PaneRectFn cb, void *user)
{
    if (!n) return;

    if (n->kind == PANE_LEAF) {
        cb(n, x, y, w, h, user);
        return;
    }

    // Internal node — split. Reserve a configurable gap between children.
    const int gap = pane_gap;

    if (n->kind == PANE_HSPLIT) {
        // Horizontal split: left/right
        int left_w = (int)((float)(w - gap) * n->split.ratio);
        if (left_w < 1) left_w = 1;
        int right_w = w - gap - left_w;
        if (right_w < 1) right_w = 1;

        compute_panes_rec(n->split.left,  x, y, left_w, h, pane_gap, cb, user);
        compute_panes_rec(n->split.right, x + left_w + gap, y, right_w, h, pane_gap, cb, user);
    } else if (n->kind == PANE_VSPLIT) {
        // Vertical split: top/bottom
        int top_h = (int)((float)(h - gap) * n->split.ratio);
        if (top_h < 1) top_h = 1;
        int bot_h = h - gap - top_h;
        if (bot_h < 1) bot_h = 1;

        compute_panes_rec(n->split.left,  x, y, w, top_h, pane_gap, cb, user);
        compute_panes_rec(n->split.right, x, y + top_h + gap, w, bot_h, pane_gap, cb, user);
    }
}

void layout_compute_panes(const PaneNode *root,
                          int term_x, int term_y, int term_w, int term_h,
                          int pane_gap,
                          PaneRectFn cb, void *user)
{
    compute_panes_rec(root, term_x, term_y, term_w, term_h, pane_gap, cb, user);
}

int layout_pane_header_height(int pane_count, int pane_h, float scale)
{
    if (pane_count <= 1)
        return 0;
    if (scale <= 0.0f)
        scale = 1.0f;

    int min_h = (int)(48.0f * scale);
    if (pane_h < min_h)
        return 0;
    return (int)(24.0f * scale);
}

Rect layout_terminal_content_rect(Rect pane_rect, int header_h)
{
    if (header_h < 0)
        header_h = 0;

    Rect content = {
        .x = pane_rect.x + 1,
        .y = pane_rect.y + header_h + 1,
        .w = pane_rect.w - 2,
        .h = pane_rect.h - header_h - 2,
    };

    if (content.w < 1) content.w = 1;
    if (content.h < 1) content.h = 1;
    return content;
}

bool layout_terminal_cell_at(Rect terminal_content, int pad,
                             int cell_width, int cell_height,
                             int mouse_x, int mouse_y,
                             int *col, int *row)
{
    if (cell_width <= 0 || cell_height <= 0)
        return false;

    int local_x = mouse_x - terminal_content.x;
    int local_y = mouse_y - terminal_content.y;
    if (local_x < 0 || local_x >= terminal_content.w
        || local_y < 0 || local_y >= terminal_content.h)
        return false;

    int cc = (local_x - pad) / cell_width;
    int cr = (local_y - pad) / cell_height;
    if (cc < 0) cc = 0;
    if (cr < 0) cr = 0;

    if (col) *col = cc;
    if (row) *row = cr;
    return true;
}
