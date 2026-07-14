#include "layout.h"
#include "pane.h"
#include "session.h"

#include <stdio.h>
#include <string.h>

// Stub session_destroy so the layout_test binary links without a full session.o.
void session_destroy(Session *s) { (void)s; }

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_INT(actual, expected) do { \
    int a__ = (actual); \
    int e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)


/* --- Pane gap tests (stack-allocated, no heap) --- */

typedef struct {
    int x[4], y[4], w[4], h[4];
    int count;
} GapRects;

static void gap_test_leaf_rect(const PaneNode *n, int x, int y, int w, int h, void *user)
{
    (void)n;
    GapRects *gr = (GapRects *)user;
    if (gr->count < 4) {
        gr->x[gr->count] = x;
        gr->y[gr->count] = y;
        gr->w[gr->count] = w;
        gr->h[gr->count] = h;
    }
    gr->count++;
}

static void setup_gap_tree(PaneKind split_kind, PaneNode *out_left, PaneNode *out_right, PaneNode *out_root)
{
    memset(out_left, 0, sizeof(*out_left));
    memset(out_right, 0, sizeof(*out_right));
    memset(out_root, 0, sizeof(*out_root));

    out_left->kind = PANE_LEAF;
    out_left->parent = out_root;
    out_left->leaf.session = NULL;

    out_right->kind = PANE_LEAF;
    out_right->parent = out_root;
    out_right->leaf.session = NULL;

    out_root->kind = split_kind;
    out_root->parent = NULL;
    out_root->split.left = out_left;
    out_root->split.right = out_right;
    out_root->split.ratio = 0.5f;
}

static void test_pane_gap_hsplit_reserves_gap(void)
{
    PaneNode left, right, root;
    setup_gap_tree(PANE_HSPLIT, &left, &right, &root);
    GapRects gr = {0};

    layout_compute_panes(&root, 0, 0, 100, 10, 6, gap_test_leaf_rect, &gr);

    EXPECT_INT(gr.count, 2);
    EXPECT_INT(gr.x[0], 0);
    EXPECT_INT(gr.y[0], 0);
    EXPECT_INT(gr.h[0], 10);
    EXPECT_INT(gr.y[1], 0);
    EXPECT_INT(gr.h[1], 10);
    EXPECT_INT(gr.w[0] + 6 + gr.w[1], 100);
    EXPECT_INT(gr.x[1], gr.w[0] + 6);
}

static void test_pane_gap_vsplit_reserves_gap(void)
{
    PaneNode left, right, root;
    setup_gap_tree(PANE_VSPLIT, &left, &right, &root);
    GapRects gr = {0};

    layout_compute_panes(&root, 0, 0, 10, 100, 6, gap_test_leaf_rect, &gr);

    EXPECT_INT(gr.count, 2);
    EXPECT_INT(gr.x[0], 0);
    EXPECT_INT(gr.y[0], 0);
    EXPECT_INT(gr.w[0], 10);
    EXPECT_INT(gr.x[1], 0);
    EXPECT_INT(gr.w[1], 10);
    EXPECT_INT(gr.h[0] + 6 + gr.h[1], 100);
    EXPECT_INT(gr.y[1], gr.h[0] + 6);
}

static void test_pane_gap_zero_takes_full_space(void)
{
    PaneNode left, right, root;
    setup_gap_tree(PANE_HSPLIT, &left, &right, &root);
    GapRects gr = {0};

    layout_compute_panes(&root, 0, 0, 100, 10, 0, gap_test_leaf_rect, &gr);

    EXPECT_INT(gr.count, 2);
    EXPECT_INT(gr.w[0] + gr.w[1], 100);
    EXPECT_INT(gr.x[1] - gr.x[0], gr.w[0]);
}

static void test_pane_gap_negative_uses_negative_raw_in_layout(void)
{
    PaneNode left, right, root;
    setup_gap_tree(PANE_HSPLIT, &left, &right, &root);
    GapRects gr = {0};

    /* layout_compute_panes no longer clamps; passing -5 expands the gap by 5px,
       so the children overlap the bounds by 5px total. This verifies the
       caller is responsible for clamping. */
    layout_compute_panes(&root, 0, 0, 100, 10, -5, gap_test_leaf_rect, &gr);

    EXPECT_INT(gr.count, 2);
    EXPECT_INT(gr.w[0] + gr.w[1], 100 - (-5)); /* 105 */
    EXPECT_INT(gr.x[1] - gr.x[0], gr.w[0] + (-5));
}

static void test_terminal_content_rect_accounts_for_pane_chrome(void)
{
    Rect pane = { .x = 40, .y = 100, .w = 320, .h = 220 };

    Rect content = layout_terminal_content_rect(pane, 24);

    EXPECT_INT(content.x, 41);
    EXPECT_INT(content.y, 125);
    EXPECT_INT(content.w, 318);
    EXPECT_INT(content.h, 194);
}

static void test_terminal_cell_at_uses_content_origin_not_outer_pane(void)
{
    Rect pane = { .x = 40, .y = 100, .w = 320, .h = 220 };
    Rect content = layout_terminal_content_rect(pane, 24);
    int col = -1;
    int row = -1;

    bool inside = layout_terminal_cell_at(content, 3, 8, 16,
                                          content.x + 3 + 4 * 8,
                                          content.y + 3 + 3 * 16,
                                          &col, &row);

    EXPECT_TRUE(inside);
    EXPECT_INT(col, 4);
    EXPECT_INT(row, 3);
}

static void test_terminal_grid_size_uses_content_rect(void)
{
    Rect content = { .x = 41, .y = 125, .w = 318, .h = 194 };
    int cols = 0;
    int rows = 0;

    layout_terminal_grid_size(content, 3, 8, 16, &cols, &rows);

    EXPECT_INT(cols, 39);
    EXPECT_INT(rows, 11);
}

static void test_pane_header_hidden_for_single_pane(void)
{
    EXPECT_INT(layout_pane_header_height(1, 220, 1.0f), 0);
}

static void test_pane_header_shown_for_multiple_panes(void)
{
    EXPECT_INT(layout_pane_header_height(2, 220, 1.0f), 24);
    EXPECT_INT(layout_pane_header_height(3, 220, 1.5f), 36);
}

static void test_pane_header_hidden_when_pane_is_too_short(void)
{
    EXPECT_INT(layout_pane_header_height(2, 47, 1.0f), 0);
}


static void test_hidden_sidebar_uses_full_window(void)
{
    Layout lo = layout_compute(1200, 800, false, 380, 4, 320);

    EXPECT_TRUE(!lo.sidebar_visible);
    EXPECT_INT(lo.terminal.x, 0);
    EXPECT_INT(lo.terminal.y, 0);
    EXPECT_INT(lo.terminal.w, 1200);
    EXPECT_INT(lo.terminal.h, 800);
    EXPECT_INT(lo.sidebar.w, 0);
}

static void test_visible_sidebar_splits_right_side(void)
{
    Layout lo = layout_compute(1200, 800, true, 380, 4, 320);

    EXPECT_TRUE(lo.sidebar_visible);
    EXPECT_INT(lo.terminal.x, 0);
    EXPECT_INT(lo.terminal.y, 0);
    EXPECT_INT(lo.terminal.w, 820);
    EXPECT_INT(lo.terminal.h, 800);
    EXPECT_INT(lo.sidebar.x, 820);
    EXPECT_INT(lo.sidebar.y, 0);
    EXPECT_INT(lo.sidebar.w, 380);
    EXPECT_INT(lo.sidebar.h, 800);
    EXPECT_INT(lo.terminal.w + lo.sidebar.w, 1200);
}

static void test_sidebar_clamps_to_preserve_min_terminal_width(void)
{
    Layout lo = layout_compute(640, 480, true, 380, 4, 320);

    EXPECT_TRUE(lo.sidebar_visible);
    EXPECT_INT(lo.terminal.w, 320);
    EXPECT_INT(lo.sidebar.x, 320);
    EXPECT_INT(lo.sidebar.w, 320);
}

static void test_too_narrow_window_hides_sidebar(void)
{
    Layout lo = layout_compute(300, 480, true, 380, 4, 320);

    EXPECT_TRUE(!lo.sidebar_visible);
    EXPECT_INT(lo.terminal.w, 300);
    EXPECT_INT(lo.sidebar.w, 0);
}

static void test_grid_columns_are_derived_from_terminal_width(void)
{
    Layout lo = layout_compute(1200, 800, true, 380, 4, 320);

    int cols = (lo.terminal.w - 2 * 4) / 8;
    EXPECT_INT(cols, 101);
}

/* --- Rail tests --- */

static void test_rail_disabled_uses_full_window(void)
{
    Layout lo = layout_compute_with_rail(1200, 800, false, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(!lo.rail_visible);
    EXPECT_INT(lo.terminal.x, 0);
    EXPECT_INT(lo.terminal.w, 1200);
}

static void test_rail_full_width(void)
{
    Layout lo = layout_compute_with_rail(1200, 800, true, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(!lo.rail_compact);
    EXPECT_INT(lo.rail.w, 260);
    EXPECT_INT(lo.terminal.x, 260);
    EXPECT_INT(lo.terminal.w, 940);
}

static void test_rail_compact_on_narrow_window(void)
{
    /* 500 wide: full 260 rail + 320 min terminal = 580 > 500, won't fit.
     * Compact 56 rail + 320 min terminal = 376 <= 500, so compact fits. */
    Layout lo = layout_compute_with_rail(500, 480, true, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(lo.rail_compact);
    EXPECT_INT(lo.rail.w, 56);
    EXPECT_INT(lo.terminal.x, 56);
    EXPECT_INT(lo.terminal.w, 444);
}

static void test_rail_hidden_on_too_narrow_window(void)
{
    Layout lo = layout_compute_with_rail(300, 480, true, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(!lo.rail_visible);
    EXPECT_INT(lo.terminal.x, 0);
    EXPECT_INT(lo.terminal.w, 300);
}

static void test_rail_and_sidebar_together(void)
{
    Layout lo = layout_compute_with_rail(1200, 800, true, 260, 56, true, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(lo.sidebar_visible);
    EXPECT_INT(lo.rail.w, 260);
    EXPECT_INT(lo.terminal.x, 260);
    /* Remaining: 1200 - 260 = 940, sidebar 380 fits within min_terminal constraint */
    EXPECT_INT(lo.terminal.w, 1200 - 260 - 380);
    EXPECT_INT(lo.sidebar.x, 1200 - 380);
    EXPECT_INT(lo.sidebar.w, 380);
}

static void test_rail_and_sidebar_preserve_min_terminal(void)
{
    /* 640 wide: full 260 rail + 320 min terminal = 580, 60 left for sidebar */
    Layout lo = layout_compute_with_rail(640, 480, true, 260, 56, true, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(lo.sidebar_visible);
    EXPECT_INT(lo.rail.w, 260);
    EXPECT_INT(lo.terminal.w, 320);
    EXPECT_INT(lo.sidebar.w, 60);
}

int main(void)
{
    test_hidden_sidebar_uses_full_window();
    test_visible_sidebar_splits_right_side();
    test_sidebar_clamps_to_preserve_min_terminal_width();
    test_too_narrow_window_hides_sidebar();
    test_grid_columns_are_derived_from_terminal_width();

    test_rail_disabled_uses_full_window();
    test_rail_full_width();
    test_rail_compact_on_narrow_window();
    test_rail_hidden_on_too_narrow_window();
    test_rail_and_sidebar_together();
    test_rail_and_sidebar_preserve_min_terminal();

    test_pane_gap_hsplit_reserves_gap();
    test_pane_gap_vsplit_reserves_gap();
    test_pane_gap_zero_takes_full_space();
    test_pane_gap_negative_uses_negative_raw_in_layout();
    test_terminal_content_rect_accounts_for_pane_chrome();
    test_terminal_cell_at_uses_content_origin_not_outer_pane();
    test_terminal_grid_size_uses_content_rect();
    test_pane_header_hidden_for_single_pane();
    test_pane_header_shown_for_multiple_panes();
    test_pane_header_hidden_when_pane_is_too_short();

    if (failures != 0) {
        fprintf(stderr, "%d layout test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
