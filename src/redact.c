#include "redact.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define REDACTED "<redacted>"

// --- growable byte buffer ----------------------------------------------------
typedef struct { char *buf; size_t len, cap; } Buf;

static void buf_append(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->len + n + 1)
            c *= 2;
        b->buf = realloc(b->buf, c);
        b->cap = c;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void buf_str(Buf *b, const char *s) { buf_append(b, s, strlen(s)); }

// --- helpers -----------------------------------------------------------------
static bool ci_contains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0)
        return true;
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i]
               && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return true;
    }
    return false;
}

static bool ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool has_prefix(const char *s, const char *pre)
{
    return strncmp(s, pre, strlen(pre)) == 0;
}

// Does the key of a `key=value` pair name a secret?
static bool key_is_sensitive(const char *key)
{
    return ci_contains(key, "secret")
        || ci_contains(key, "password")
        || ci_contains(key, "passwd")
        || ci_contains(key, "token")
        || ci_contains(key, "api_key")
        || ci_contains(key, "apikey")
        || ci_contains(key, "api-key")
        || ci_contains(key, "access_key")
        || ci_contains(key, "secret_key")
        || ci_contains(key, "auth");
}

// Does a bare token look like a known secret format?
static bool looks_like_secret(const char *t)
{
    size_t n = strlen(t);
    if (has_prefix(t, "sk-") && n >= 12) return true;
    if (has_prefix(t, "sk_live_") || has_prefix(t, "sk_test_")) return true;
    if (has_prefix(t, "pk_live_") || has_prefix(t, "rk_live_")) return true;
    if (has_prefix(t, "fw_") && n >= 12) return true;
    if (has_prefix(t, "ghp_") || has_prefix(t, "gho_") || has_prefix(t, "ghu_")
        || has_prefix(t, "ghs_") || has_prefix(t, "ghr_")) return true;
    if (has_prefix(t, "github_pat_")) return true;
    if (has_prefix(t, "xoxb-") || has_prefix(t, "xoxp-") || has_prefix(t, "xoxa-")
        || has_prefix(t, "xoxr-") || has_prefix(t, "xoxs-")) return true;
    if ((has_prefix(t, "AKIA") || has_prefix(t, "ASIA")) && n >= 20) return true;
    return false;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

char *redact_secrets(const char *text)
{
    if (!text)
        return NULL;

    Buf out = {0};
    buf_str(&out, "");   // ensure non-NULL even for empty input

    bool redact_next = false;   // set after a "Bearer" token
    const char *p = text;
    while (*p) {
        if (is_space(*p)) {
            buf_append(&out, p, 1);
            p++;
            continue;
        }

        const char *tstart = p;
        while (*p && !is_space(*p))
            p++;
        size_t tlen = (size_t)(p - tstart);

        char tok[1024];
        size_t copylen = tlen < sizeof(tok) - 1 ? tlen : sizeof(tok) - 1;
        memcpy(tok, tstart, copylen);
        tok[copylen] = '\0';

        if (redact_next) {
            buf_str(&out, REDACTED);
            redact_next = false;
            continue;
        }

        if (ci_equal(tok, "bearer")) {
            buf_str(&out, tok);       // keep the literal "Bearer"
            redact_next = true;
            continue;
        }

        // key=value / key:value inside a single token?
        char *delim = strpbrk(tok, "=:");
        if (delim && delim != tok) {
            char key[256];
            size_t kl = (size_t)(delim - tok);
            if (kl >= sizeof(key))
                kl = sizeof(key) - 1;
            memcpy(key, tok, kl);
            key[kl] = '\0';
            if (key_is_sensitive(key)) {
                buf_append(&out, tok, (size_t)(delim - tok) + 1);  // key + delimiter
                if (delim[1] != '\0')
                    buf_str(&out, REDACTED);
                continue;
            }
        }

        if (looks_like_secret(tok))
            buf_str(&out, REDACTED);
        else
            buf_str(&out, tok);
    }

    return out.buf;
}

char *text_tail(const char *text, int max_lines, int max_bytes)
{
    if (!text)
        return NULL;

    size_t len = strlen(text);
    const char *start = text;

    if (max_lines > 0 && len > 0) {
        size_t i = len;
        if (i > 0 && text[i - 1] == '\n')   // ignore a single trailing newline
            i--;
        size_t count = 0;
        while (i > 0) {
            if (text[i - 1] == '\n') {
                count++;
                if (count >= (size_t)max_lines) {
                    start = text + i;
                    break;
                }
            }
            i--;
        }
    }

    if (max_bytes > 0) {
        size_t tail_len = strlen(start);
        if (tail_len > (size_t)max_bytes) {
            const char *cut = start + (tail_len - (size_t)max_bytes);
            const char *nl = strchr(cut, '\n');
            start = nl ? nl + 1 : cut;
        }
    }

    char *out = strdup(start);
    return out;
}
