#ifndef NOVA_LAYOUT_H
#define NOVA_LAYOUT_H

#include <stdbool.h>

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
} Layout;

Layout layout_compute(int window_w, int window_h, bool sidebar_visible,
                      int sidebar_width, int pad, int min_terminal_w);

#endif // NOVA_LAYOUT_H
