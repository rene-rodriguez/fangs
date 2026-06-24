#include "inline_cmd.h"

#include <string.h>

static bool is_fence(const char *s, const char *e)
{
    return (e - s) >= 3 && s[0] == '`' && s[1] == '`' && s[2] == '`';
}

bool inline_sanitize_command(const char *reply, char *out, int out_size)
{
    if (out && out_size > 0)
        out[0] = '\0';
    if (!reply || !out || out_size <= 0)
        return false;

    const char *p = reply;
    while (*p) {
        while (*p == '\n' || *p == '\r')   // skip blank separators
            p++;
        if (!*p)
            break;

        const char *le = p;
        while (*le && *le != '\n' && *le != '\r')
            le++;

        const char *s = p;
        const char *e = le;
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;

        if (e == s || is_fence(s, e)) {    // skip empty / ``` fence lines
            p = le;
            continue;
        }

        while (s < e && *s == '`') s++;    // strip surrounding backticks
        while (e > s && e[-1] == '`') e--;

        // strip a leading shell prompt marker: "$ ", "> ", "# "
        while (s < e && (*s == '$' || *s == '>' || *s == '#')
               && s + 1 < e && s[1] == ' ') {
            s += 2;
            while (s < e && *s == ' ') s++;
        }

        size_t n = (size_t)(e - s);
        if (n == 0) {
            p = le;
            continue;
        }
        if (n >= (size_t)out_size)
            n = (size_t)out_size - 1;
        memcpy(out, s, n);
        out[n] = '\0';
        return true;
    }
    return false;
}
