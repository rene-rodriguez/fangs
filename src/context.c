#include "context.h"

#include <stdlib.h>

#include "term_engine.h"
#include "redact.h"

char *context_build(TermEngine *te, int max_lines, int max_bytes)
{
    if (!te)
        return NULL;

    char *dump = term_engine_dump_text(te);   // full screen + scrollback
    if (!dump)
        return NULL;

    char *tail = text_tail(dump, max_lines, max_bytes);
    free(dump);
    if (!tail)
        return NULL;

    char *redacted = redact_secrets(tail);     // SECURITY: scrub before send
    free(tail);
    return redacted;
}
