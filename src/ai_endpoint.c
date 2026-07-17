#include "ai_endpoint.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool provider_is(const char *provider, const char *expected)
{
    return provider && strcmp(provider, expected) == 0;
}

static bool ends_with(const char *value, const char *suffix)
{
    size_t value_len = strlen(value);
    size_t suffix_len = strlen(suffix);
    return value_len >= suffix_len
        && strcmp(value + value_len - suffix_len, suffix) == 0;
}

static bool has_url_path(const char *url)
{
    const char *authority = strstr(url, "://");
    const char *start = authority ? authority + 3 : url;
    return strchr(start, '/') != NULL;
}

bool ai_endpoint_resolve(const char *provider, const char *configured,
                         char *out, size_t out_size)
{
    if (!configured || !out || out_size == 0)
        return false;

    while (*configured && isspace((unsigned char)*configured))
        configured++;

    size_t input_len = strlen(configured);
    while (input_len > 0 && isspace((unsigned char)configured[input_len - 1]))
        input_len--;
    if (input_len == 0 || input_len >= 512)
        return false;

    size_t suffix_at = input_len;
    for (size_t i = 0; i < input_len; i++) {
        if (configured[i] == '?' || configured[i] == '#') {
            suffix_at = i;
            break;
        }
    }

    size_t base_len = suffix_at;
    while (base_len > 0 && configured[base_len - 1] == '/')
        base_len--;
    if (base_len == 0 || base_len >= 512)
        return false;

    char base[512];
    memcpy(base, configured, base_len);
    base[base_len] = '\0';

    const char *route = "";
    if (provider_is(provider, "anthropic")) {
        if (!ends_with(base, "/messages"))
            route = ends_with(base, "/v1") || has_url_path(base)
                ? "/messages" : "/v1/messages";
    } else if (provider_is(provider, "ollama")) {
        if (!ends_with(base, "/api/chat"))
            route = ends_with(base, "/api") ? "/chat" : "/api/chat";
    } else if (!ends_with(base, "/chat/completions")) {
        route = has_url_path(base) ? "/chat/completions"
                                   : "/v1/chat/completions";
    }

    const char *suffix = configured + suffix_at;
    int written = snprintf(out, out_size, "%s%s%.*s", base, route,
                           (int)(input_len - suffix_at), suffix);
    return written >= 0 && (size_t)written < out_size;
}
