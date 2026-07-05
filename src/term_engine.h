// term_engine — THE SEAM around libghostty-vt.
//
// This is the single place that creates, configures, and destroys the VT
// engine handles, and the only place that knows how to produce AI context
// text. If libghostty-vt's API breaks, or we ever swap to libvterm / a Rust
// core, this file is what changes — not the host (main.c).
//
// The host borrows the underlying handles via accessors for its per-frame
// input/render code; it must never free them.
#ifndef FANGS_TERM_ENGINE_H
#define FANGS_TERM_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ghostty/vt.h>

#include "theme.h"

typedef struct TermEngine TermEngine;

// Create the engine: terminal + encoders + render state + iterators.
// Kitty graphics can be disabled or bounded by the storage limit. Returns
// NULL on failure.
TermEngine *term_engine_create(uint16_t cols, uint16_t rows,
                               int cell_width, int cell_height,
                               int max_scrollback,
                               bool kitty_images,
                               int kitty_image_storage_mb);
void        term_engine_destroy(TermEngine *te);

// Feed PTY output into the VT parser.
void        term_engine_write(TermEngine *te, const uint8_t *data, size_t len);

// Resize the grid (also updates cell pixel dims for Kitty placement math).
void        term_engine_resize(TermEngine *te, uint16_t cols, uint16_t rows,
                               int cell_width, int cell_height);

// Snapshot the terminal into the render state (call once per frame).
void        term_engine_update(TermEngine *te);

// Push a color theme into the engine: default fg/bg/cursor + the full 256-color
// palette (so palette-indexed cell colors are themed too). Call on theme change.
void        term_engine_apply_theme(TermEngine *te, const Theme *theme);

// AI context (Phase 4 prep): plain-text dump of the active screen +
// scrollback via libghostty-vt's formatter. Returns a malloc'd,
// NUL-terminated UTF-8 string the caller must free(), or NULL on failure.
char       *term_engine_dump_text(TermEngine *te);

// --- borrowed-handle accessors for host input/render code --------------------
GhosttyTerminal                       term_engine_terminal(TermEngine *te);
GhosttyRenderState                    term_engine_render_state(TermEngine *te);
GhosttyRenderStateRowIterator         term_engine_row_iter(TermEngine *te);
GhosttyRenderStateRowCells            term_engine_row_cells(TermEngine *te);
GhosttyKittyGraphicsPlacementIterator term_engine_placement_iter(TermEngine *te);
GhosttyKeyEncoder                     term_engine_key_encoder(TermEngine *te);
GhosttyKeyEvent                       term_engine_key_event(TermEngine *te);
GhosttyMouseEncoder                   term_engine_mouse_encoder(TermEngine *te);
GhosttyMouseEvent                     term_engine_mouse_event(TermEngine *te);

#endif // FANGS_TERM_ENGINE_H
