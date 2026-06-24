// Pull a runnable command out of an assistant message's fenced code block, for
// the sidebar "Run" button. Pure + testable.
//
// SAFETY: only SINGLE-LINE commands are returned. The Run button stages the
// command at the shell prompt without a trailing newline so the user must press
// Enter themselves; a multi-line block injected the same way would execute every
// line but the last, so we refuse to extract those.
#ifndef FANGS_CMDEXTRACT_H
#define FANGS_CMDEXTRACT_H

#include <stdbool.h>

// If `text` contains a fenced ```...``` block whose trimmed content is a single
// line, copy it into `out` (NUL-terminated) and return true. Otherwise false.
bool command_extract(const char *text, char *out, int out_size);

#endif // FANGS_CMDEXTRACT_H
