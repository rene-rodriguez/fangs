// ai_http — OpenAI-compatible streaming client behind ai_provider.h.
//
// Promoted from spike/ai_stream/stream_window.c (proven live against an OpenAI-compatible endpoint).
// A worker pthread runs curl_easy_perform; incoming bytes go through the SSE
// parser; each delta is appended (under lock) to reason/answer buffers. The host
// drains via ai_stream_poll(). See ai_provider.h for the threading contract.
#include "ai_provider.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "cJSON.h"
#include "sse.h"

struct AiStream {
    pthread_mutex_t mu;

    char  *reason; size_t reason_len, reason_cap, reason_read;  // delta.reasoning_content
    char  *answer; size_t answer_len, answer_cap, answer_read;  // delta.content

    volatile int done;
    volatile int cancel;
    long http;
    int  curl_rc;          // CURLcode (0 == CURLE_OK)
    char err[256];

    // owned request data (freed by ai_stream_free)
    char *url;
    char *body;
    char *auth_header;
    char *version_header;  // Anthropic only ("anthropic-version: ..."), else NULL
    long  timeout;

    pthread_t thread;
    int       thread_started;
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

void ai_global_init(void)    { curl_global_init(CURL_GLOBAL_DEFAULT); }
void ai_global_cleanup(void) { curl_global_cleanup(); }

// --- worker side -------------------------------------------------------------
typedef struct { AiStream *s; SseParser *parser; } WorkerCtx;

// Runs on the worker thread (via the SSE parser). Appends under lock.
static void on_delta(void *ud, const char *text, bool is_reasoning)
{
    AiStream *s = ud;
    pthread_mutex_lock(&s->mu);
    if (is_reasoning)
        buf_append(&s->reason, &s->reason_len, &s->reason_cap, text, strlen(text));
    else
        buf_append(&s->answer, &s->answer_len, &s->answer_cap, text, strlen(text));
    pthread_mutex_unlock(&s->mu);
}

static size_t on_chunk(char *ptr, size_t size, size_t nmemb, void *ud)
{
    WorkerCtx *w = ud;
    if (w->s->cancel)
        return 0;                       // abort transfer
    size_t n = size * nmemb;
    sse_parser_feed(w->parser, ptr, n, on_delta, w->s);
    return n;
}

static int on_progress(void *ud, curl_off_t dt, curl_off_t dn,
                       curl_off_t ut, curl_off_t un)
{
    (void)dt; (void)dn; (void)ut; (void)un;
    AiStream *s = ud;
    return s->cancel ? 1 : 0;           // non-zero aborts a stalled transfer
}

static void *worker(void *arg)
{
    AiStream *s = arg;

    CURL *curl = curl_easy_init();
    if (!curl) {
        pthread_mutex_lock(&s->mu);
        snprintf(s->err, sizeof(s->err), "curl init failed");
        s->curl_rc = -1;
        s->done = 1;
        pthread_mutex_unlock(&s->mu);
        return NULL;
    }

    struct curl_slist *h = NULL;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: text/event-stream");
    h = curl_slist_append(h, s->auth_header);
    if (s->version_header)
        h = curl_slist_append(h, s->version_header);

    SseParser *parser = sse_parser_new();
    WorkerCtx ctx = { s, parser };

    curl_easy_setopt(curl, CURLOPT_URL, s->url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, s->body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, s);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, s->timeout);

    CURLcode rc = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);

    pthread_mutex_lock(&s->mu);
    s->curl_rc = (int)rc;
    s->http = http;
    if (rc != CURLE_OK && !s->cancel)
        snprintf(s->err, sizeof(s->err), "network error: %s", curl_easy_strerror(rc));
    else if (http == 401 || http == 403)
        snprintf(s->err, sizeof(s->err),
                 "HTTP %ld from %s — key rejected (does the endpoint/model match your key?)",
                 http, s->url);
    else if (http != 200 && http != 0)
        snprintf(s->err, sizeof(s->err), "HTTP %ld from %s", http, s->url);
    s->done = 1;
    pthread_mutex_unlock(&s->mu);

    sse_parser_free(parser);
    curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return NULL;
}

// --- request building --------------------------------------------------------
static bool is_anthropic(const AiConfig *cfg)
{
    return cfg->provider && strcmp(cfg->provider, "anthropic") == 0;
}

// OpenAI-compatible chat-completions body: system is just another message.
static char *build_body_openai(const AiConfig *cfg, const AiMessage *msgs, int n)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", cfg->model ? cfg->model : "");
    cJSON_AddBoolToObject(req, "stream", cfg->stream ? 1 : 0);
    if (cfg->max_tokens > 0)
        cJSON_AddNumberToObject(req, "max_tokens", cfg->max_tokens);
    cJSON *arr = cJSON_AddArrayToObject(req, "messages");
    for (int i = 0; i < n; i++) {
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", msgs[i].role ? msgs[i].role : "user");
        cJSON_AddStringToObject(m, "content", msgs[i].content ? msgs[i].content : "");
        cJSON_AddItemToArray(arr, m);
    }
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    return body;
}

// Anthropic-native /v1/messages body: system prompts are hoisted to a top-level
// `system` string; only user/assistant turns go in `messages`; max_tokens is
// required (the Messages API rejects a request without it).
static char *build_body_anthropic(const AiConfig *cfg, const AiMessage *msgs, int n)
{
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", cfg->model ? cfg->model : "");
    cJSON_AddBoolToObject(req, "stream", cfg->stream ? 1 : 0);
    cJSON_AddNumberToObject(req, "max_tokens", cfg->max_tokens > 0 ? cfg->max_tokens : 1024);

    // Concatenate every system-role message into the top-level `system` field.
    char *sys_prompt = NULL;
    size_t sys_len = 0, sys_cap = 0;
    cJSON *arr = cJSON_AddArrayToObject(req, "messages");
    for (int i = 0; i < n; i++) {
        const char *role = msgs[i].role ? msgs[i].role : "user";
        const char *content = msgs[i].content ? msgs[i].content : "";
        if (strcmp(role, "system") == 0) {
            if (sys_len)
                buf_append(&sys_prompt, &sys_len, &sys_cap, "\n\n", 2);
            buf_append(&sys_prompt, &sys_len, &sys_cap, content, strlen(content));
            continue;
        }
        cJSON *m = cJSON_CreateObject();
        cJSON_AddStringToObject(m, "role", role);
        cJSON_AddStringToObject(m, "content", content);
        cJSON_AddItemToArray(arr, m);
    }
    if (sys_prompt)
        cJSON_AddStringToObject(req, "system", sys_prompt);

    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    free(sys_prompt);
    return body;
}

static char *build_body(const AiConfig *cfg, const AiMessage *msgs, int n)
{
    return is_anthropic(cfg) ? build_body_anthropic(cfg, msgs, n)
                             : build_body_openai(cfg, msgs, n);
}

// --- public API --------------------------------------------------------------
AiStream *ai_stream_start(const AiConfig *cfg, const AiMessage *msgs, int n_msgs)
{
    if (!cfg || !cfg->endpoint || !cfg->endpoint[0])
        return NULL;
    if (!cfg->api_key || !cfg->api_key[0])
        return NULL;                    // no key — host shows a message

    AiStream *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    pthread_mutex_init(&s->mu, NULL);

    s->url = strdup(cfg->endpoint);
    s->body = build_body(cfg, msgs, n_msgs);
    s->timeout = 90L;

    // Anthropic authenticates with `x-api-key` + a required API-version header;
    // OpenAI-compatible endpoints use a bearer token.
    size_t alen = strlen(cfg->api_key) + 32;
    s->auth_header = malloc(alen);
    if (is_anthropic(cfg)) {
        snprintf(s->auth_header, alen, "x-api-key: %s", cfg->api_key);
        s->version_header = strdup("anthropic-version: 2023-06-01");
    } else {
        snprintf(s->auth_header, alen, "Authorization: Bearer %s", cfg->api_key);
    }

    if (!s->url || !s->body || !s->auth_header) {
        ai_stream_free(s);
        return NULL;
    }

    if (pthread_create(&s->thread, NULL, worker, s) != 0) {
        ai_stream_free(s);
        return NULL;
    }
    s->thread_started = 1;
    return s;
}

int ai_stream_poll(AiStream *s, char *out, int out_size,
                   bool *is_reasoning, bool *done, bool *ok)
{
    if (!s || !out || out_size <= 0)
        return 0;

    int copied = 0;
    pthread_mutex_lock(&s->mu);

    if (s->reason_read < s->reason_len) {
        size_t avail = s->reason_len - s->reason_read;
        size_t n = avail < (size_t)(out_size - 1) ? avail : (size_t)(out_size - 1);
        memcpy(out, s->reason + s->reason_read, n);
        out[n] = '\0';
        s->reason_read += n;
        copied = (int)n;
        if (is_reasoning) *is_reasoning = true;
    } else if (s->answer_read < s->answer_len) {
        size_t avail = s->answer_len - s->answer_read;
        size_t n = avail < (size_t)(out_size - 1) ? avail : (size_t)(out_size - 1);
        memcpy(out, s->answer + s->answer_read, n);
        out[n] = '\0';
        s->answer_read += n;
        copied = (int)n;
        if (is_reasoning) *is_reasoning = false;
    } else {
        out[0] = '\0';
    }

    bool drained = s->reason_read >= s->reason_len
                && s->answer_read >= s->answer_len;
    if (done) *done = s->done && drained;
    if (ok)   *ok = (s->curl_rc == 0 && s->http == 200);

    pthread_mutex_unlock(&s->mu);
    return copied;
}

const char *ai_stream_error(AiStream *s)
{
    if (!s)
        return "no stream";
    return s->err[0] ? s->err : "request failed";
}

void ai_stream_cancel(AiStream *s)
{
    if (s)
        s->cancel = 1;
}

void ai_stream_free(AiStream *s)
{
    if (!s)
        return;
    if (s->thread_started) {
        s->cancel = 1;
        pthread_join(s->thread, NULL);
    }
    pthread_mutex_destroy(&s->mu);
    free(s->reason);
    free(s->answer);
    free(s->url);
    free(s->body);
    free(s->auth_header);
    free(s->version_header);
    free(s);
}
