// cmdblocks_osc — pure OSC-133 semantic-prompt scanner (no raylib, no engine).
//
// Command blocks are driven entirely by the OSC 133 marks the user's shell
// emits (see docs/shell-integration.md). This unit is the byte-level half:
// a chunk-safe scanner that pulls the marks we care about out of the raw PTY
// stream without modifying it. The host passes every byte through to the VT
// engine unchanged; this scanner only observes.
//
// Kept dependency-free on purpose so it can be unit-tested in isolation
// (mirrors inline_cmd.c vs ui_inline.c).
#ifndef FANGS_CMDBLOCKS_OSC_H
#define FANGS_CMDBLOCKS_OSC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// The four OSC 133 semantic marks. We act on PROMPT (block top) and DONE
// (exit code); CMD/EXEC are recognized but reserved for future use.
typedef enum {
    CB_MARK_NONE = 0,
    CB_MARK_PROMPT,   // OSC 133;A      — prompt start (= top of a command block)
    CB_MARK_CMD,      // OSC 133;B      — prompt end / typed-command start
    CB_MARK_EXEC,     // OSC 133;C      — command began executing
    CB_MARK_DONE,     // OSC 133;D;<n>  — command finished (n = exit code)
} CbMark;

typedef struct {
    CbMark mark;
    int    code;   // exit code for CB_MARK_DONE; -1 when absent/unknown
    size_t end;    // index in the current chunk just past the OSC terminator
} CbHit;

// Persistent scanner state across feed chunks. Zero-initialize, or call
// cb_parser_reset(). A single OSC sequence may straddle several chunks.
typedef struct {
    bool in_osc;     // currently inside an OSC string (after ESC ])
    bool esc;        // previous byte was ESC (top level: detect "]"; in OSC: detect ST "\")
    bool overflow;   // current OSC outgrew buf — ignore it
    int  len;        // bytes accumulated in buf
    char buf[48];    // OSC payload after "ESC ]" (only "133;…" interests us)
} CbParser;

void cb_parser_reset(CbParser *p);

// Scan data[*pos .. len) for the next OSC-133 mark. On a completed mark, fills
// *hit (hit->end = index just past the terminator), sets *pos = hit->end and
// returns true. When the chunk is exhausted with no further mark, returns
// false and sets *pos = len. Non-OSC bytes are simply skipped — the caller is
// responsible for forwarding the full byte stream to the VT engine unchanged.
bool cb_parse_next(CbParser *p, const uint8_t *data, size_t len,
                   size_t *pos, CbHit *hit);

#endif // FANGS_CMDBLOCKS_OSC_H
