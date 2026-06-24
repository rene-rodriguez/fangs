#include "cmdblocks.h"
#include "cmdblocks_osc.h"

#include <stdlib.h>
#include <string.h>
#include <ghostty/vt.h>

// Max command blocks remembered (older ones drop off the top of scrollback
// anyway; their tracked refs are freed on eviction).
#define CB_RING 256

typedef struct {
    GhosttyTrackedGridRef top;   // anchor at the command's prompt row
    int  code;                   // exit code, -1 if unknown
    bool done;                   // OSC D seen
} CbBlock;

static struct {
    CbParser parser;

    CbBlock ring[CB_RING];       // finished blocks (circular)
    int     head;                // index of the oldest
    int     count;               // number stored (<= CB_RING)

    // The live block: started at the most recent prompt (OSC A), not yet
    // followed by another prompt. Holds the running/idle command at the bottom.
    GhosttyTrackedGridRef cur_top;
    bool cur_has;
    int  cur_code;
    bool cur_done;
} g_cb;

// --- model -------------------------------------------------------------------

static void push_finished(GhosttyTrackedGridRef top, int code, bool done)
{
    int idx = (g_cb.head + g_cb.count) % CB_RING;
    if (g_cb.count == CB_RING) {
        if (g_cb.ring[idx].top) ghostty_tracked_grid_ref_free(g_cb.ring[idx].top);
        g_cb.head = (g_cb.head + 1) % CB_RING;
    } else {
        g_cb.count++;
    }
    g_cb.ring[idx].top  = top;
    g_cb.ring[idx].code = code;
    g_cb.ring[idx].done = done;
}

// A new prompt: retire the previous live block to the ring, anchor a fresh
// tracked ref at the current prompt row.
static void on_prompt(GhosttyTerminal term, uint16_t row)
{
    if (g_cb.cur_has)
        push_finished(g_cb.cur_top, g_cb.cur_code, g_cb.cur_done);

    g_cb.cur_top  = NULL;
    g_cb.cur_has  = false;
    g_cb.cur_code = -1;
    g_cb.cur_done = false;

    GhosttyPoint pt = { .tag = GHOSTTY_POINT_TAG_VIEWPORT };
    pt.value.coordinate.x = 0;
    pt.value.coordinate.y = row;

    GhosttyTrackedGridRef ref = NULL;
    if (ghostty_terminal_grid_ref_track(term, pt, &ref) == GHOSTTY_SUCCESS && ref) {
        g_cb.cur_top = ref;
        g_cb.cur_has = true;
    }
}

void cmdblocks_feed(TermEngine *te, const uint8_t *data, size_t len)
{
    GhosttyTerminal    term = term_engine_terminal(te);
    GhosttyRenderState rs   = term_engine_render_state(te);

    size_t flush = 0, pos = 0;
    CbHit hit;
    while (cb_parse_next(&g_cb.parser, data, len, &pos, &hit)) {
        if (hit.mark == CB_MARK_PROMPT) {
            // Flush through the A terminator so the engine's cursor is parked at
            // the new prompt row, then anchor a tracked ref there.
            if (hit.end > flush) {
                term_engine_write(te, data + flush, hit.end - flush);
                flush = hit.end;
            }
            ghostty_render_state_update(rs, term);
            uint16_t cy = 0;
            ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
            on_prompt(term, cy);
        } else if (hit.mark == CB_MARK_DONE) {
            if (g_cb.cur_has) {
                g_cb.cur_code = hit.code;
                g_cb.cur_done = true;
            }
        }
        // CB_MARK_CMD / CB_MARK_EXEC: reserved, no-op for now.
    }

    if (len > flush)
        term_engine_write(te, data + flush, len - flush);
}

void cmdblocks_reset(void)
{
    for (int k = 0; k < g_cb.count; k++) {
        int idx = (g_cb.head + k) % CB_RING;
        if (g_cb.ring[idx].top) ghostty_tracked_grid_ref_free(g_cb.ring[idx].top);
    }
    if (g_cb.cur_top) ghostty_tracked_grid_ref_free(g_cb.cur_top);
    memset(&g_cb, 0, sizeof(g_cb));
}

// --- helpers -----------------------------------------------------------------

static Color tc_color(ThemeColor c, unsigned char a)
{
    return (Color){ c.r, c.g, c.b, a };
}

// Tracked ref → current viewport row. False if it's scrolled out of view.
static bool top_vrow(GhosttyTrackedGridRef ref, int *out)
{
    if (!ref) return false;
    GhosttyPointCoordinate c;
    if (ghostty_tracked_grid_ref_point(ref, GHOSTTY_POINT_TAG_VIEWPORT, &c) != GHOSTTY_SUCCESS)
        return false;
    *out = (int)c.y;
    return true;
}

// Extract a finished command's output text via the engine's select_output API.
// Scans the block's body rows for the first that the engine reports as command
// output, then formats that contiguous output region. Caller frees the result.
static char *block_output_text(GhosttyTerminal term, int top_v, int next_v, int rows)
{
    int hi = next_v < rows ? next_v : rows;
    for (int r = top_v + 1; r < hi; r++) {
        GhosttyPoint pt = { .tag = GHOSTTY_POINT_TAG_VIEWPORT };
        pt.value.coordinate.x = 0;
        pt.value.coordinate.y = (uint32_t)r;

        GhosttyGridRef ref;
        if (ghostty_terminal_grid_ref(term, pt, &ref) != GHOSTTY_SUCCESS)
            continue;

        GhosttySelection sel = GHOSTTY_INIT_SIZED(GhosttySelection);
        if (ghostty_terminal_select_output(term, ref, &sel) != GHOSTTY_SUCCESS)
            continue;

        GhosttyTerminalSelectionFormatOptions o =
            GHOSTTY_INIT_SIZED(GhosttyTerminalSelectionFormatOptions);
        o.emit      = GHOSTTY_FORMATTER_FORMAT_PLAIN;
        o.unwrap    = true;
        o.trim      = true;
        o.selection = &sel;

        uint8_t *out = NULL;
        size_t   n   = 0;
        if (ghostty_terminal_selection_format_alloc(term, NULL, o, &out, &n) == GHOSTTY_SUCCESS
            && out) {
            char *s = malloc(n + 1);
            if (s) { memcpy(s, out, n); s[n] = '\0'; }
            ghostty_free(NULL, out, n);
            return s;
        }
    }
    return NULL;
}

// --- draw --------------------------------------------------------------------

typedef struct { int row; int code; bool live; } CbItem;

bool cmdblocks_draw(TermEngine *te, Font font, const Theme *th,
                    int cell_w, int cell_h, int font_size,
                    int pad, int term_area_w, int rows,
                    int mouse_x, int mouse_y, bool click)
{
    (void)cell_w;
    GhosttyTerminal term = term_engine_terminal(te);

    CbItem items[CB_RING + 1];
    int n = 0;

    for (int k = 0; k < g_cb.count; k++) {
        CbBlock *b = &g_cb.ring[(g_cb.head + k) % CB_RING];
        int r;
        if (!top_vrow(b->top, &r) || r < 0 || r >= rows) continue;
        items[n].row = r; items[n].code = b->code; items[n].live = false; n++;
    }
    if (g_cb.cur_has) {
        int r;
        if (top_vrow(g_cb.cur_top, &r) && r >= 0 && r < rows) {
            items[n].row = r; items[n].code = -1; items[n].live = true; n++;
        }
    }
    if (n == 0) return false;

    // Sort ascending by viewport row (small n; insertion sort).
    for (int i = 1; i < n; i++) {
        CbItem t = items[i];
        int j = i - 1;
        while (j >= 0 && items[j].row > t.row) { items[j + 1] = items[j]; j--; }
        items[j + 1] = t;
    }

    Color ok   = tc_color(th->ansi[2], 255);   // green
    Color bad  = tc_color(th->ansi[1], 255);   // red
    Color neut = tc_color(th->ansi[8], 255);   // grey
    Color bg   = tc_color(th->bg, 255);

    int badge_r  = cell_h / 3;
    if (badge_r < 4) badge_r = 4;
    if (badge_r > 7) badge_r = 7;
    int badge_cx = term_area_w - pad - 8 - badge_r;   // left of the scrollbar margin

    int hovered = -1;
    bool consumed = false;

    for (int i = 0; i < n; i++) {
        int row    = items[i].row;
        int y      = pad + row * cell_h;
        int next_y = (i + 1 < n) ? pad + items[i + 1].row * cell_h : pad + rows * cell_h;
        Color stc  = items[i].live ? neut
                   : (items[i].code == 0 ? ok : (items[i].code > 0 ? bad : neut));

        // Left gutter strip spanning the whole block region.
        DrawRectangle(0, y, 3, next_y - y, Fade(stc, 0.55f));

        // Separator line across the block top.
        int sep_x1 = badge_cx - badge_r - 6;
        if (sep_x1 > pad)
            DrawRectangle(pad, y, sep_x1 - pad, 1, Fade(stc, 0.45f));

        // Status badge on the top row (none for the in-progress live block).
        if (!items[i].live) {
            float cxb = (float)badge_cx;
            float cyb = (float)y + (float)cell_h / 2.0f;
            DrawCircle((int)cxb, (int)cyb, (float)badge_r, stc);
            if (items[i].code == 0) {           // ✓ success
                DrawLineEx((Vector2){cxb - badge_r * 0.45f, cyb + badge_r * 0.05f},
                           (Vector2){cxb - badge_r * 0.10f, cyb + badge_r * 0.42f}, 1.7f, bg);
                DrawLineEx((Vector2){cxb - badge_r * 0.10f, cyb + badge_r * 0.42f},
                           (Vector2){cxb + badge_r * 0.50f, cyb - badge_r * 0.42f}, 1.7f, bg);
            } else if (items[i].code > 0) {     // ✗ non-zero exit
                float o = badge_r * 0.42f;
                DrawLineEx((Vector2){cxb - o, cyb - o}, (Vector2){cxb + o, cyb + o}, 1.7f, bg);
                DrawLineEx((Vector2){cxb + o, cyb - o}, (Vector2){cxb - o, cyb + o}, 1.7f, bg);
            }
            // code < 0 (unknown exit): leave the neutral circle bare.
        }

        if (mouse_x >= 0 && mouse_x < term_area_w && mouse_y >= y && mouse_y < next_y)
            hovered = i;
    }

    // Hover affordance: a "copy" button that copies the block's output text.
    if (hovered >= 0 && !items[hovered].live) {
        int row    = items[hovered].row;
        int y      = pad + row * cell_h;
        int next_v = (hovered + 1 < n) ? items[hovered + 1].row : rows;

        int btn_fs = (int)(font_size * 0.82f);
        if (btn_fs < 8) btn_fs = 8;
        const char *label = "copy";
        Vector2 ts   = MeasureTextEx(font, label, (float)btn_fs, 0);
        int     padx = 6;
        int     btn_w = (int)ts.x + 2 * padx;
        int     btn_h = cell_h - 2;
        int     btn_x = badge_cx - badge_r - 8 - btn_w;
        int     btn_y = y + 1;
        Rectangle btn = { (float)btn_x, (float)btn_y, (float)btn_w, (float)btn_h };

        bool over = (mouse_x >= btn.x && mouse_x < btn.x + btn.width
                     && mouse_y >= btn.y && mouse_y < btn.y + btn.height);
        Color st = items[hovered].code == 0 ? ok
                 : (items[hovered].code > 0 ? bad : neut);

        DrawRectangleRounded(btn, 0.35f, 6, Fade(st, over ? 0.45f : 0.22f));
        DrawTextEx(font, label,
                   (Vector2){ btn.x + padx, btn.y + (btn_h - ts.y) / 2 },
                   (float)btn_fs, 0, tc_color(th->fg, 230));

        if (over && click) {
            char *txt = block_output_text(term, row, next_v, rows);
            if (txt) { SetClipboardText(txt); free(txt); }
            consumed = true;
        }
    }

    return consumed;
}

// --- navigation --------------------------------------------------------------

bool cmdblocks_navigate(TermEngine *te, int dir)
{
    GhosttyTerminal term = term_engine_terminal(te);

    // Current viewport top in absolute screen coordinates.
    GhosttyPoint top = { .tag = GHOSTTY_POINT_TAG_VIEWPORT };
    top.value.coordinate.x = 0;
    top.value.coordinate.y = 0;
    GhosttyGridRef tref;
    if (ghostty_terminal_grid_ref(term, top, &tref) != GHOSTTY_SUCCESS) return false;
    GhosttyPointCoordinate tc;
    if (ghostty_terminal_point_from_grid_ref(term, &tref, GHOSTTY_POINT_TAG_SCREEN, &tc)
        != GHOSTTY_SUCCESS) return false;
    long vp = (long)tc.y;

    bool found = false;
    long best = 0;
    for (int k = 0; k <= g_cb.count; k++) {
        GhosttyTrackedGridRef ref = (k < g_cb.count)
            ? g_cb.ring[(g_cb.head + k) % CB_RING].top
            : g_cb.cur_top;
        if (!ref) continue;

        GhosttyPointCoordinate sc;
        if (ghostty_tracked_grid_ref_point(ref, GHOSTTY_POINT_TAG_SCREEN, &sc) != GHOSTTY_SUCCESS)
            continue;
        long sy = (long)sc.y;

        if (dir < 0) {                       // previous: nearest above
            if (sy < vp && (!found || sy > best)) { best = sy; found = true; }
        } else {                             // next: nearest below
            if (sy > vp && (!found || sy < best)) { best = sy; found = true; }
        }
    }
    if (!found) return false;

    GhosttyTerminalScrollViewport sv = {
        .tag   = GHOSTTY_SCROLL_VIEWPORT_DELTA,
        .value = { .delta = (int)(best - vp) },
    };
    ghostty_terminal_scroll_viewport(term, sv);
    return true;
}
