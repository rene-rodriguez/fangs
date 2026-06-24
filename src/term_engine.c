#include "term_engine.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ghostty/vt.h>

struct TermEngine {
    GhosttyTerminal                       terminal;
    GhosttyKeyEncoder                     key_encoder;
    GhosttyKeyEvent                       key_event;
    GhosttyMouseEncoder                   mouse_encoder;
    GhosttyMouseEvent                     mouse_event;
    GhosttyRenderState                    render_state;
    GhosttyRenderStateRowIterator         row_iter;
    GhosttyRenderStateRowCells            row_cells;
    GhosttyKittyGraphicsPlacementIterator placement_iter;
};

TermEngine *term_engine_create(uint16_t cols, uint16_t rows,
                               int cell_width, int cell_height,
                               int max_scrollback)
{
    TermEngine *te = calloc(1, sizeof(*te));
    if (!te) return NULL;

    if (max_scrollback < 1)
        max_scrollback = 1000;

    GhosttyTerminalOptions opts = {
        .cols = cols, .rows = rows, .max_scrollback = (uint32_t)max_scrollback,
    };
    if (ghostty_terminal_new(NULL, &te->terminal, opts) != GHOSTTY_SUCCESS)
        goto fail;

    // Cell pixel dims aren't in the options struct; set via an initial resize
    // (also prevents divide-by-zero in Kitty placement math).
    ghostty_terminal_resize(te->terminal, cols, rows,
                            (uint32_t)cell_width, (uint32_t)cell_height);

    // Enable Kitty graphics (storage limit + non-inline transmit mediums).
    uint64_t kitty_limit = 64 * 1024 * 1024;
    ghostty_terminal_set(te->terminal,
        GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_STORAGE_LIMIT, &kitty_limit);
    bool on = true;
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_FILE, &on);
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_TEMP_FILE, &on);
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_KITTY_IMAGE_MEDIUM_SHARED_MEM, &on);

    if (ghostty_key_encoder_new(NULL, &te->key_encoder) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_key_event_new(NULL, &te->key_event) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_mouse_encoder_new(NULL, &te->mouse_encoder) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_mouse_event_new(NULL, &te->mouse_event) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_render_state_new(NULL, &te->render_state) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_render_state_row_iterator_new(NULL, &te->row_iter) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_render_state_row_cells_new(NULL, &te->row_cells) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_kitty_graphics_placement_iterator_new(NULL, &te->placement_iter) != GHOSTTY_SUCCESS) goto fail;

    return te;

fail:
    term_engine_destroy(te);
    return NULL;
}

void term_engine_destroy(TermEngine *te)
{
    if (!te) return;
    if (te->placement_iter) ghostty_kitty_graphics_placement_iterator_free(te->placement_iter);
    if (te->mouse_event)    ghostty_mouse_event_free(te->mouse_event);
    if (te->mouse_encoder)  ghostty_mouse_encoder_free(te->mouse_encoder);
    if (te->key_event)      ghostty_key_event_free(te->key_event);
    if (te->key_encoder)    ghostty_key_encoder_free(te->key_encoder);
    if (te->row_cells)      ghostty_render_state_row_cells_free(te->row_cells);
    if (te->row_iter)       ghostty_render_state_row_iterator_free(te->row_iter);
    if (te->render_state)   ghostty_render_state_free(te->render_state);
    if (te->terminal)       ghostty_terminal_free(te->terminal);
    free(te);
}

void term_engine_write(TermEngine *te, const uint8_t *data, size_t len)
{
    ghostty_terminal_vt_write(te->terminal, data, len);
}

void term_engine_resize(TermEngine *te, uint16_t cols, uint16_t rows,
                        int cell_width, int cell_height)
{
    ghostty_terminal_resize(te->terminal, cols, rows,
                            (uint32_t)cell_width, (uint32_t)cell_height);
}

void term_engine_update(TermEngine *te)
{
    ghostty_render_state_update(te->render_state, te->terminal);
}

void term_engine_apply_theme(TermEngine *te, const Theme *theme)
{
    if (!te || !theme)
        return;

    ThemeColor pal[256];
    theme_build_palette256(theme, pal);

    GhosttyColorRgb gpal[256];
    for (int i = 0; i < 256; i++) {
        gpal[i].r = pal[i].r;
        gpal[i].g = pal[i].g;
        gpal[i].b = pal[i].b;
    }
    GhosttyColorRgb fg  = { theme->fg.r,     theme->fg.g,     theme->fg.b };
    GhosttyColorRgb bg  = { theme->bg.r,     theme->bg.g,     theme->bg.b };
    GhosttyColorRgb cur = { theme->cursor.r, theme->cursor.g, theme->cursor.b };

    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, gpal);
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
    ghostty_terminal_set(te->terminal, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, &cur);
}

char *term_engine_dump_text(TermEngine *te)
{
    GhosttyFormatterTerminalOptions opts =
        GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
    opts.emit   = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    opts.unwrap = true;
    opts.trim   = true;

    GhosttyFormatter fmt = NULL;
    if (ghostty_formatter_terminal_new(NULL, &fmt, te->terminal, opts) != GHOSTTY_SUCCESS)
        return NULL;

    uint8_t *out = NULL;
    size_t   out_len = 0;
    GhosttyResult r = ghostty_formatter_format_alloc(fmt, NULL, &out, &out_len);

    char *copy = NULL;
    if (r == GHOSTTY_SUCCESS && out) {
        copy = malloc(out_len + 1);
        if (copy) {
            memcpy(copy, out, out_len);
            copy[out_len] = '\0';
        }
        ghostty_free(NULL, out, out_len);
    }
    ghostty_formatter_free(fmt);
    return copy;
}

GhosttyTerminal               term_engine_terminal(TermEngine *te)      { return te->terminal; }
GhosttyRenderState            term_engine_render_state(TermEngine *te)  { return te->render_state; }
GhosttyRenderStateRowIterator term_engine_row_iter(TermEngine *te)      { return te->row_iter; }
GhosttyRenderStateRowCells    term_engine_row_cells(TermEngine *te)     { return te->row_cells; }
GhosttyKittyGraphicsPlacementIterator term_engine_placement_iter(TermEngine *te) { return te->placement_iter; }
GhosttyKeyEncoder             term_engine_key_encoder(TermEngine *te)   { return te->key_encoder; }
GhosttyKeyEvent               term_engine_key_event(TermEngine *te)     { return te->key_event; }
GhosttyMouseEncoder           term_engine_mouse_encoder(TermEngine *te) { return te->mouse_encoder; }
GhosttyMouseEvent             term_engine_mouse_event(TermEngine *te)   { return te->mouse_event; }
