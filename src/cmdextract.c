#include "cmdextract.h"

#include <ctype.h>
#include <string.h>

bool command_extract(const char *text, char *out, int out_size)
{
    if (out && out_size > 0)
        out[0] = '\0';
    if (!text || !out || out_size <= 0)
        return false;

    const char *open = strstr(text, "```");
    if (!open)
        return false;
    open += 3;

    const char *close = strstr(open, "```");
    if (!close)
        return false;

    // If a language tag / newline precedes the content, start after it.
    const char *nl = memchr(open, '\n', (size_t)(close - open));
    const char *content_start = (nl && nl < close) ? nl + 1 : open;
    const char *content_end = close;

    while (content_start < content_end && isspace((unsigned char)*content_start))
        content_start++;
    while (content_end > content_start && isspace((unsigned char)content_end[-1]))
        content_end--;

    size_t clen = (size_t)(content_end - content_start);
    if (clen == 0)
        return false;

    // SAFETY: refuse multi-line blocks (see header).
    if (memchr(content_start, '\n', clen))
        return false;

    if (clen >= (size_t)out_size)
        clen = (size_t)out_size - 1;
    memcpy(out, content_start, clen);
    out[clen] = '\0';
    return true;
}
