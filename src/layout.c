#include "layout.h"

static int clamp_nonnegative(int value)
{
    return value < 0 ? 0 : value;
}

Layout layout_compute(int window_w, int window_h, bool sidebar_visible,
                      int sidebar_width, int pad, int min_terminal_w)
{
    (void)pad;

    window_w = clamp_nonnegative(window_w);
    window_h = clamp_nonnegative(window_h);
    sidebar_width = clamp_nonnegative(sidebar_width);
    min_terminal_w = clamp_nonnegative(min_terminal_w);

    Layout lo = {
        .terminal = {0, 0, window_w, window_h},
        .sidebar = {window_w, 0, 0, window_h},
        .sidebar_visible = false,
    };

    if (!sidebar_visible || window_w <= min_terminal_w || sidebar_width == 0)
        return lo;

    int available_sidebar_w = window_w - min_terminal_w;
    int actual_sidebar_w = sidebar_width < available_sidebar_w
        ? sidebar_width
        : available_sidebar_w;

    if (actual_sidebar_w <= 0)
        return lo;

    lo.sidebar_visible = true;
    lo.terminal.w = window_w - actual_sidebar_w;
    lo.sidebar.x = lo.terminal.w;
    lo.sidebar.w = actual_sidebar_w;
    return lo;
}
