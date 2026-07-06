#include "cmdblocks_osc.h"

#include <stdio.h>
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

// Copy payload text into a hit, trimming to CB_HIT_TEXT_MAX.
static void hit_set_text(CbHit *hit, const char *s)
{
    size_t n = strlen(s);
    if (n >= sizeof(hit->text)) n = sizeof(hit->text) - 1;
    memcpy(hit->text, s, n);
    hit->text[n] = '\0';
}

// Interpret the accumulated OSC payload. Returns true (and fills *hit) for a
// recognized mark. Always clears the OSC state for the next sequence.
static bool finalize(CbParser *p, CbHit *hit, size_t end)
{
    bool matched = false;

    hit->text[0] = '\0';

    p->buf[p->len] = '\0';

    // OSC 133 semantic marks. Payloads are tiny, so an overflowed sequence
    // was never a real mark — drop it.
    if (!p->overflow && p->len >= 5 && strncmp(p->buf, "133;", 4) == 0) {
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
    // OSC 9 desktop notification (iTerm2/Ghostty; Claude Code's notify
    // channel). "9;4;…" is ConEmu progress, not a notification — skip it.
    // Overflowed text is emitted truncated: the message still matters.
    else if (p->len >= 2 && strncmp(p->buf, "9;", 2) == 0) {
        if (strncmp(p->buf + 2, "4;", 2) != 0) {
            hit->mark = CB_MARK_NOTIFY;
            hit->code = -1;
            hit->end  = end;
            hit_set_text(hit, p->buf + 2);
            matched = true;
        }
    }
    // OSC 777;notify;<title>;<body> (libnotify convention).
    else if (p->len >= 11 && strncmp(p->buf, "777;notify;", 11) == 0) {
        hit->mark = CB_MARK_NOTIFY;
        hit->code = -1;
        hit->end  = end;
        char *title = p->buf + 11;
        char *semi = strchr(title, ';');
        if (semi && semi[1] != '\0') {
            *semi = '\0';
            char joined[CB_HIT_TEXT_MAX];
            snprintf(joined, sizeof(joined), "%s: %s", title, semi + 1);
            hit_set_text(hit, joined);
        } else {
            if (semi) *semi = '\0';
            hit_set_text(hit, title);
        }
        matched = true;
    }
    // OSC 0/2 window title.
    else if (p->len >= 2 && (p->buf[0] == '0' || p->buf[0] == '2')
             && p->buf[1] == ';') {
        hit->mark = CB_MARK_TITLE;
        hit->code = -1;
        hit->end  = end;
        hit_set_text(hit, p->buf + 2);
        matched = true;
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
            } else if (b == 0x07) {          // bare BEL — attention ping
                hit->mark    = CB_MARK_BELL;
                hit->code    = -1;
                hit->end     = i + 1;
                hit->text[0] = '\0';
                *pos = i + 1;
                return true;
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
