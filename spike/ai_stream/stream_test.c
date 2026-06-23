// =============================================================================
// Nova Terminal Phase 0b spike — headless streaming AI in C (OpenAI-compatible)
// =============================================================================
// Proves the risky part of paths A/B: libcurl streaming via CURLOPT_WRITEFUNCTION
// + SSE line parsing + cJSON delta extraction, token-by-token, in plain C.
// This is the seed of the real src/ai_http.c. No threads/UI yet (see the Raylib
// variant next) — here we just confirm tokens arrive incrementally.
//
//   Endpoint: any OpenAI-compatible /chat/completions (set ENDPOINT below)
//   Model:    a model your endpoint serves (set MODEL below)
//   Key:      env NOVA_API_KEY  (never hardcoded, never logged)
//
// Build: clang -O2 stream_test.c cJSON.c -lcurl -o stream_test
// Run:   NOVA_API_KEY=... ./stream_test
// =============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

#define ENDPOINT "https://api.openai.com/v1/chat/completions"  // any OpenAI-compatible endpoint
#define MODEL    "gpt-4o-mini"                                  // any model the endpoint serves
#define PROMPT   "Write a short haiku about a ghost living inside a terminal."

// Streaming state threaded through the libcurl write callback.
typedef struct {
    char  *line;  size_t line_len, line_cap;   // partial-line accumulator (chunks split mid-line)
    char  *raw;   size_t raw_len,  raw_cap;     // full raw body (for error diagnostics)
    size_t tokens;                              // count of deltas emitted (reasoning + content)
    int    phase;                               // 0=none, 1=reasoning_content, 2=content
} Stream;

static void grow(char **buf, size_t *cap, size_t need) {
    if (*cap >= need) return;
    size_t c = *cap ? *cap : 256;
    while (c < need) c *= 2;
    *buf = realloc(*buf, c);
    *cap = c;
}

// Parse one SSE payload (the text after "data: ") and print any delta.
// Reasoning models stream chain-of-thought in
// delta.reasoning_content BEFORE the answer arrives in delta.content — the real
// sidebar will show these as separate "thinking" vs "answer" regions.
static void handle_data(Stream *s, const char *json) {
    if (strcmp(json, "[DONE]") == 0) return;
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    cJSON *c0 = cJSON_GetArrayItem(choices, 0);
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(c0, "delta");
    if (delta) {
        cJSON *reasoning = cJSON_GetObjectItemCaseSensitive(delta, "reasoning_content");
        cJSON *content   = cJSON_GetObjectItemCaseSensitive(delta, "content");
        if (cJSON_IsString(reasoning) && reasoning->valuestring && *reasoning->valuestring) {
            if (s->phase != 1) { fputs("\n\033[2m[reasoning] \033[0m", stdout); s->phase = 1; }
            fputs(reasoning->valuestring, stdout); fflush(stdout); s->tokens++;
        }
        if (cJSON_IsString(content) && content->valuestring && *content->valuestring) {
            if (s->phase != 2) { fputs("\n\n\033[1m[answer]\033[0m\n", stdout); s->phase = 2; }
            fputs(content->valuestring, stdout); fflush(stdout); s->tokens++;
        }
    }
    cJSON_Delete(root);
}

static size_t on_chunk(char *ptr, size_t size, size_t nmemb, void *userdata) {
    Stream *s = userdata;
    size_t n = size * nmemb;

    // keep a full copy for diagnostics
    grow(&s->raw, &s->raw_cap, s->raw_len + n + 1);
    memcpy(s->raw + s->raw_len, ptr, n); s->raw_len += n; s->raw[s->raw_len] = 0;

    // append to the partial-line buffer, then process complete lines
    grow(&s->line, &s->line_cap, s->line_len + n + 1);
    memcpy(s->line + s->line_len, ptr, n); s->line_len += n; s->line[s->line_len] = 0;

    char *start = s->line, *nl;
    while ((nl = memchr(start, '\n', (s->line + s->line_len) - start)) != NULL) {
        *nl = 0;
        size_t ll = strlen(start);
        if (ll && start[ll - 1] == '\r') start[ll - 1] = 0;   // strip CR
        if (strncmp(start, "data: ", 6) == 0) handle_data(s, start + 6);
        start = nl + 1;
    }
    // shift any partial remainder back to the front
    size_t rem = (s->line + s->line_len) - start;
    memmove(s->line, start, rem);
    s->line_len = rem; s->line[rem] = 0;
    return n;
}

int main(void) {
    const char *key = getenv("NOVA_API_KEY");
    if (!key || !*key) { fprintf(stderr, "ERROR: NOVA_API_KEY not set\n"); return 2; }

    // Build the request body with cJSON (correct escaping, no string-concat).
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", MODEL);
    cJSON_AddBoolToObject(req, "stream", 1);
    cJSON_AddNumberToObject(req, "max_tokens", 256);
    cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", PROMPT);
    cJSON_AddItemToArray(msgs, m);
    char *body = cJSON_PrintUnformatted(req);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();

    size_t alen = strlen(key) + 32;
    char *auth = malloc(alen);
    snprintf(auth, alen, "Authorization: Bearer %s", key);
    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    hdrs = curl_slist_append(hdrs, "Accept: text/event-stream");
    hdrs = curl_slist_append(hdrs, auth);

    Stream s; memset(&s, 0, sizeof s);

    curl_easy_setopt(curl, CURLOPT_URL, ENDPOINT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    fprintf(stderr, "--- streaming from %s ---\n", MODEL);
    CURLcode rc = curl_easy_perform(curl);
    fputc('\n', stdout);

    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    fprintf(stderr, "--- done: HTTP %ld, curl=%s, %zu deltas (reasoning+content) ---\n",
            http, curl_easy_strerror(rc), s.tokens);
    if (s.tokens == 0)   // surface the error body when nothing streamed
        fprintf(stderr, "raw response:\n%.*s\n", (int)(s.raw_len > 1500 ? 1500 : s.raw_len), s.raw ? s.raw : "");

    free(s.line); free(s.raw); free(body); free(auth);
    cJSON_Delete(req);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return (rc == CURLE_OK && s.tokens > 0) ? 0 : 1;
}
