// AI context capture: the recent terminal screen + scrollback, trimmed to a
// budget and run through the secret redactor, ready to hand to the model.
//
// Forward-declares TermEngine (instead of including term_engine.h) so callers
// don't transitively pull in ghostty headers.
#ifndef NOVA_CONTEXT_H
#define NOVA_CONTEXT_H

typedef struct TermEngine TermEngine;

// Capture the last `max_lines` lines of terminal text (capped at `max_bytes`),
// redacted. Returns malloc'd NUL-terminated UTF-8; caller frees. NULL on failure.
char *context_build(TermEngine *te, int max_lines, int max_bytes);

#endif // NOVA_CONTEXT_H
