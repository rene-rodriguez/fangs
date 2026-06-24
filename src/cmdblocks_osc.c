#include "cmdblocks_osc.h"

#include <string.h>

void cb_parser_reset(CbParser *p)
{
    memset(p, 0, sizeof(*p));
}

// Parse a non-negative decimal exit code out of the buffer tail (after
// "133;D;"). Stops at the first non-digit. Returns -1 if there are no digits.
static int parse_code(const char *s)
{
    if (s[0] < '0' || s[0] > '9')
        return -1;
    int v = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (*s - '0');
        if (v > 100000) { v = 100000; break; } // clamp; exit codes are 0-255
    }
    return v;
}

// Interpret the accumulated OSC payload. Returns true (and fills *hit) only for
// a recognized 133 mark. Always clears the OSC state for the next sequence.
static bool finalize(CbParser *p, CbHit *hit, size_t end)
{
    bool matched = false;

    // Need at least "133;X" (5 chars).
    if (!p->overflow && p->len >= 5) {
        p->buf[p->len] = '\0';
        if (strncmp(p->buf, "133;", 4) == 0) {
            CbMark mark = CB_MARK_NONE;
            int code = -1;
            switch (p->buf[4]) {
                case 'A': mark = CB_MARK_PROMPT; break;
                case 'B': mark = CB_MARK_CMD;    break;
                case 'C': mark = CB_MARK_EXEC;   break;
                case 'D':
                    mark = CB_MARK_DONE;
                    // "133;D" or "133;D;<code>"
                    if (p->len >= 6 && p->buf[5] == ';')
                        code = parse_code(p->buf + 6);
                    break;
                default: break;
            }
            if (mark != CB_MARK_NONE) {
                hit->mark = mark;
                hit->code = code;
                hit->end  = end;
                matched = true;
            }
        }
    }

    p->in_osc   = false;
    p->esc      = false;
    p->len      = 0;
    p->overflow = false;
    return matched;
}

bool cb_parse_next(CbParser *p, const uint8_t *data, size_t len,
                   size_t *pos, CbHit *hit)
{
    for (size_t i = *pos; i < len; i++) {
        uint8_t b = data[i];

        if (!p->in_osc) {
            if (p->esc) {
                p->esc = false;
                if (b == ']') {              // ESC ] — OSC introducer
                    p->in_osc   = true;
                    p->len      = 0;
                    p->overflow = false;
                } else if (b == 0x1b) {      // ESC ESC — stay armed
                    p->esc = true;
                }
            } else if (b == 0x1b) {
                p->esc = true;
            }
            continue;
        }

        // Inside an OSC string.
        if (p->esc) {
            p->esc = false;
            if (b == '\\') {                 // ESC \ — ST terminator
                if (finalize(p, hit, i + 1)) { *pos = i + 1; return true; }
                continue;                    // not a 133 mark; OSC state cleared
            }
            // Stray ESC that isn't ST: abandon this OSC for robustness.
            p->in_osc   = false;
            p->len      = 0;
            p->overflow = false;
            continue;
        }
        if (b == 0x07) {                     // BEL terminator
            if (finalize(p, hit, i + 1)) { *pos = i + 1; return true; }
            continue;
        }
        if (b == 0x1b) {                     // possible ST start
            p->esc = true;
            continue;
        }
        if (p->len < (int)sizeof(p->buf) - 1)
            p->buf[p->len++] = (char)b;
        else
            p->overflow = true;
    }

    *pos = len;
    return false;
}
