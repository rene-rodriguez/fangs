// ui_menu — Generic popover menu (pure model + raylib draw).

#include "ui_menu_draw.h"

#include <raylib.h>
#include <string.h>

// ---- Pure model -----------------------------------------------------------

void ui_menu_open(UiMenu *m, const UiMenuItem *items, int count,
                  int anchor_x, int anchor_y)
{
    if (!m) return;
    if (count > UI_MENU_MAX_ITEMS)
        count = UI_MENU_MAX_ITEMS;
    m->count = count;
    for (int i = 0; i < count; i++)
        m->items[i] = items[i];
    m->x = anchor_x;
    m->y = anchor_y;
    m->item_h = UI_MENU_ITEM_H;
    m->open = true;
}

void ui_menu_layout(UiMenu *m, int win_w, int win_h)
{
    if (!m || !m->open) return;

    // Compute width: max label width + padding, minimum UI_MENU_MIN_W.
    // 10 px/char covers the mono glyph advance (~8.4 px at the 14 px draw
    // size) plus the 1 px letter spacing the draw layer uses.
    int max_w = UI_MENU_MIN_W;
    for (int i = 0; i < m->count; i++) {
        int lw = (int)strlen(m->items[i].label) * 10;
        int need = lw + UI_MENU_PAD * 2;
        if (need > max_w) max_w = need;
    }
    m->w = max_w;
    m->h = m->count * m->item_h + UI_MENU_PAD * 2;

    // Clamp to window bounds.
    if (m->w > win_w) m->w = win_w;
    if (m->x + m->w > win_w)
        m->x = win_w - m->w;
    if (m->x < 0) m->x = 0;
    if (m->y + m->h > win_h)
        m->y = win_h - m->h;
    if (m->y < 0) m->y = 0;
}

int ui_menu_hit(const UiMenu *m, int mx, int my)
{
    if (!m || !m->open) return -1;
    if (mx < m->x || mx >= m->x + m->w) return -1;
    if (my < m->y || my >= m->y + m->h) return -1;
    int rel = my - m->y;
    if (rel < UI_MENU_PAD) return -1; // top padding
    int idx = (rel - UI_MENU_PAD) / m->item_h;
    if (idx < 0 || idx >= m->count) return -1;
    if (m->items[idx].separator) return -1; // separators can't be hit
    return idx;
}

void ui_menu_close(UiMenu *m)
{
    if (m) m->open = false;
}

bool ui_menu_active(const UiMenu *m)
{
    return m && m->open;
}

// ---- Raylib draw layer ----------------------------------------------------

void ui_menu_draw(Font font, const UiMenu *m, int mouse_x, int mouse_y,
                  const UiTheme *theme)
{
    if (!m || !m->open) return;

    // Panel background + border.
    Color bg = UI2RAY(theme->inline_bg);
    Color border = UI2RAY(theme->panel_border);

    // Roundness is a 0..1 fraction of the short edge in raylib, not pixels.
    DrawRectangleRounded((Rectangle){ (float)m->x, (float)m->y,
                                      (float)m->w, (float)m->h },
                         0.12f, 4, bg);
    DrawRectangleRoundedLines((Rectangle){ (float)m->x, (float)m->y,
                                           (float)m->w, (float)m->h },
                              0.12f, 4, border);

    // Items.
    int font_size = 14;
    for (int i = 0; i < m->count; i++) {
        int iy = m->y + UI_MENU_PAD + i * m->item_h;

        if (m->items[i].separator) {
            // Separator line.
            int sep_y = iy + m->item_h / 2;
            DrawLine(m->x + UI_MENU_PAD, sep_y,
                     m->x + m->w - UI_MENU_PAD, sep_y, border);
            continue;
        }

        // Hover highlight (selection wash — the bg color would be invisible
        // painted over itself).
        if (mouse_x >= m->x && mouse_x < m->x + m->w &&
            mouse_y >= iy && mouse_y < iy + m->item_h) {
            Color hover = UI2RAY(theme->selection);
            hover.a = 70;
            DrawRectangle(m->x + 2, iy, m->w - 4, m->item_h, hover);
        }

        // Label text.
        Color tint = UI2RAY(m->items[i].tint);
        DrawTextEx(font, m->items[i].label,
                   (Vector2){ (float)(m->x + UI_MENU_PAD), (float)(iy + 4) },
                   (float)font_size, 1.0f, tint);
    }
}
