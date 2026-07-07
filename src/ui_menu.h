// ui_menu — Generic popover menu (pure model + raylib draw).
//
// The model (UiMenu) owns items, layout, and hit-testing — no raylib
// dependency. The draw layer (ui_menu_draw) is a separate call, matching
// the rail model/paint split.

#ifndef FANGS_UI_MENU_H
#define FANGS_UI_MENU_H

#include "ui_theme.h"
#include <stdbool.h>
#include <stdint.h>

#define UI_MENU_MAX_ITEMS 32
#define UI_MENU_LABEL_MAX 112
#define UI_MENU_ITEM_H 28
#define UI_MENU_PAD 6
#define UI_MENU_MIN_W 160

typedef struct {
    char label[UI_MENU_LABEL_MAX];
    int  tag;                  // caller-defined action identifier
    UiColor tint;              // text tint (UI_COLOR_TEXT for default)
    bool separator;            // true = draw a separator line (label unused)
} UiMenuItem;

typedef struct {
    UiMenuItem items[UI_MENU_MAX_ITEMS];
    int  count;
    int  x, y;                 // anchor point (top-left after layout)
    int  w, h;                 // total menu rect
    int  item_h;               // per-item height (UI_MENU_ITEM_H)
    bool open;
} UiMenu;

// Open the menu at anchor_x, anchor_y (top-left of the popover).
// Items are copied into the menu (the caller may free its array).
void ui_menu_open(UiMenu *m, const UiMenuItem *items, int count,
                  int anchor_x, int anchor_y);

// Layout: clamp the menu rect inside (0, 0, win_w, win_h).
// Call after ui_menu_open and before drawing / hit-testing.
void ui_menu_layout(UiMenu *m, int win_w, int win_h);

// Hit test: returns item index (0..count-1) or -1 if outside the menu.
int  ui_menu_hit(const UiMenu *m, int mx, int my);

// Close the menu.
void ui_menu_close(UiMenu *m);

// Returns true if the menu is open (for input-guard chains).
bool ui_menu_active(const UiMenu *m);

#endif // FANGS_UI_MENU_H
