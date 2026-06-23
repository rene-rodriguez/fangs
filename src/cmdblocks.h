// cmdblocks — Warp-style command blocks on top of OSC-133 semantic marks.
//
// The shell (see docs/shell-integration.md) emits OSC 133 marks: A at prompt
// start, D;<code> when a command finishes. We anchor a *tracked* grid ref at
// each prompt row (so it survives scrollback/reflow) and remember the exit code
// from the preceding D. Each frame those anchors become viewport rows that
// drive the overlay: status-colored separators, a left gutter strip, ✓/✗
// badges, a hover "copy output" button, and Ctrl+Up/Down navigation.
//
// Everything is observation-only: bytes are forwarded to the VT engine
// unmodified (the engine does its own OSC-133 tracking, which powers the
// select_output API we use for copy). Without shell integration there are no
// marks and the overlay simply never draws.
#ifndef NOVA_CMDBLOCKS_H
#define NOVA_CMDBLOCKS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "raylib.h"
#include "term_engine.h"
#include "theme.h"

// PTY sink: forward a chunk to the engine while tracking command boundaries.
// Drop-in replacement for a bare term_engine_write() in the read loop.
void cmdblocks_feed(TermEngine *te, const uint8_t *data, size_t len);

// Draw the block overlay over the already-rendered grid. Call inside the
// terminal scissor each frame. mouse_x/y are window coords; click is true on
// the frame the left button was pressed. Returns true when it consumed the
// click (host should then skip selection / PTY mouse forwarding).
bool cmdblocks_draw(TermEngine *te, Font font, const Theme *theme,
                    int cell_w, int cell_h, int font_size,
                    int pad, int term_area_w, int rows,
                    int mouse_x, int mouse_y, bool click);

// Scroll the viewport to the previous (dir < 0) or next (dir > 0) command.
// Returns true only if there was a target to scroll to, so the caller can let
// the key fall through to the child (e.g. a TUI) when there's nothing to do.
bool cmdblocks_navigate(TermEngine *te, int dir);

// Free all tracked grid refs (call once at shutdown).
void cmdblocks_reset(void);

#endif // NOVA_CMDBLOCKS_H
