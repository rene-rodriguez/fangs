#include "sse.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

struct SseParser {
    char  *line;       // accumulates the current (possibly partial) SSE line
    size_t line_len;
    size_t line_cap;
    bool   ndjson;      // true: Ollama native (bare JSON per line), false: SSE
};

static void buf_append(char **buf, size_t *len, size_t *cap,
                       const char *s, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t c = *cap ? *cap : 256;
        while (c < *len + n + 1)
            c *= 2;
        *buf = realloc(*buf, c);
        *cap = c;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

SseParser *sse_parser_new(void)
{
    SseParser *p = calloc(1, sizeof(*p));
    return p;
}

SseParser *sse_parser_new_ndjson(void)
{
    SseParser *p = calloc(1, sizeof(*p));
    if (p)
        p->ndjson = true;
    return p;
}

void sse_parser_free(SseParser *p)
{
    if (!p)
        return;
    free(p->line);
    free(p);
}

// OpenAI-compatible chunk: choices[0].delta.{reasoning_content,content}.
static void handle_openai(cJSON *root, SseDeltaFn fn, void *userdata)
{
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    cJSON *c0 = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(c0, "delta");
    if (!delta)
        return;
    cJSON *r = cJSON_GetObjectItemCaseSensitive(delta, "reasoning_content");
    cJSON *c = cJSON_GetObjectItemCaseSensitive(delta, "content");
    if (fn && cJSON_IsString(r) && r->valuestring && r->valuestring[0])
        fn(userdata, r->valuestring, true);
    if (fn && cJSON_IsString(c) && c->valuestring && c->valuestring[0])
        fn(userdata, c->valuestring, false);
}

// Anthropic Messages stream: each event is `{"type":...}`. Only
// content_block_delta carries text; delta.type distinguishes the answer
// (text_delta → delta.text) from reasoning (thinking_delta → delta.thinking).
static void handle_anthropic(cJSON *root, const char *type,
                             SseDeltaFn fn, void *userdata)
{
    if (strcmp(type, "content_block_delta") != 0)
        return;
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
    cJSON *dtype = cJSON_GetObjectItemCaseSensitive(delta, "type");
    if (!fn || !cJSON_IsString(dtype))
        return;
    if (strcmp(dtype->valuestring, "thinking_delta") == 0) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(delta, "thinking");
        if (cJSON_IsString(t) && t->valuestring && t->valuestring[0])
            fn(userdata, t->valuestring, true);
    } else if (strcmp(dtype->valuestring, "text_delta") == 0) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(delta, "text");
        if (cJSON_IsString(t) && t->valuestring && t->valuestring[0])
            fn(userdata, t->valuestring, false);
    }
}

// Ollama native /api/chat line: {"message":{"content":...,"thinking":...},
// "done":bool,...}. The final line (done:true) carries stats and has no
// `message` key of interest — handled as a harmless no-op here. Overall
// stream completion is driven by HTTP transfer completion in ai_http.c's
// worker(), not by this "done" flag.
static void handle_ollama_native(cJSON *root, SseDeltaFn fn, void *userdata)
{
    cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!message)
        return;
    cJSON *thinking = cJSON_GetObjectItemCaseSensitive(message, "thinking");
    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
    if (fn && cJSON_IsString(thinking) && thinking->valuestring && thinking->valuestring[0])
        fn(userdata, thinking->valuestring, true);
    if (fn && cJSON_IsString(content) && content->valuestring && content->valuestring[0])
        fn(userdata, content->valuestring, false);
}

// Parse one complete NDJSON line (the whole line is one JSON object).
static void handle_ndjson_line(const char *json, SseDeltaFn fn, void *userdata)
{
    if (!json[0])
        return;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return;
    handle_ollama_native(root, fn, userdata);
    cJSON_Delete(root);
}

// Parse one `data:` payload (the text after "data: ") and emit its deltas.
// The two wire formats are told apart by shape: Anthropic events carry a
// top-level string `type`; OpenAI-compatible chunks carry a `choices` array.
static void handle_data(const char *json, SseDeltaFn fn, void *userdata)
{
    if (strcmp(json, "[DONE]") == 0)
        return;

    cJSON *root = cJSON_Parse(json);
    if (!root)
        return;

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (cJSON_IsString(type) && type->valuestring)
        handle_anthropic(root, type->valuestring, fn, userdata);
    else
        handle_openai(root, fn, userdata);

    cJSON_Delete(root);
}

void sse_parser_feed(SseParser *p, const char *bytes, size_t n,
                     SseDeltaFn fn, void *userdata)
{
    if (!p || !bytes || n == 0)
        return;

    buf_append(&p->line, &p->line_len, &p->line_cap, bytes, n);

    char *start = p->line;
    char *nl;
    while ((nl = memchr(start, '\n', (p->line + p->line_len) - start)) != NULL) {
        *nl = '\0';
        size_t ll = strlen(start);
        if (ll && start[ll - 1] == '\r')
            start[ll - 1] = '\0';
        if (p->ndjson)
            handle_ndjson_line(start, fn, userdata);
        else if (strncmp(start, "data: ", 6) == 0)
            handle_data(start + 6, fn, userdata);
        start = nl + 1;
    }

    // Keep the unparsed remainder for the next feed().
    size_t rem = (p->line + p->line_len) - start;
    memmove(p->line, start, rem);
    p->line_len = rem;
    if (p->line)
        p->line[rem] = '\0';
}
