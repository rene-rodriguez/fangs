#include "layout.h"

#include <stdio.h>

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

    if (failures != 0) {
        fprintf(stderr, "%d layout test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
