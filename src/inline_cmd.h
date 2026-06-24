// Turn a model reply into a single shell command to stage at the prompt, for
// the inline Ctrl+Space surface. Pure + testable.
//
// The inline system prompt asks for a bare one-line command, but models stray:
// they wrap it in a ```fence```, add a "$ " prompt marker, or prepend prose.
// This pulls out the one command line. SAFETY: single line only — a staged,
// newline-free injection must never carry embedded newlines (see Phase 4/5).
#ifndef FANGS_INLINE_CMD_H
#define FANGS_INLINE_CMD_H

#include <stdbool.h>

// Extract a single command line from `reply` into `out` (NUL-terminated).
// Skips blank/fence lines, strips surrounding backticks and a leading
// "$ "/"> "/"# " prompt marker. Returns false if nothing usable is found.
bool inline_sanitize_command(const char *reply, char *out, int out_size);

#endif // FANGS_INLINE_CMD_H
