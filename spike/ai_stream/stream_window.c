// =============================================================================
// Fangs Phase 0b spike — streaming AI into a Raylib window (worker thread)
// =============================================================================
// Proves the architecture's concurrency/UI model from the spec:
//   - a libcurl request runs on a WORKER pthread (CURLOPT_WRITEFUNCTION stream)
//   - deltas are pushed into a MUTEX-GUARDED buffer
//   - the main thread (Raylib) redraws the growing buffer every frame
// Immediate-mode rendering = streaming is just "redraw what's accumulated".
// Reasoning models split delta.reasoning_content (thinking) from delta.content
// (answer); we keep them in two buffers and draw them as distinct regions.
//
// Build (links the raylib already built by ghostling):
//   clang -O2 -Wall stream_window.c cJSON.c \
//     -I ../../vendor/ghostling/build/_deps/raylib-src/src \
//     ../../vendor/ghostling/build/_deps/raylib-build/raylib/libraylib.a \
//     -lcurl -framework IOKit -framework Cocoa -framework OpenGL -o stream_window
// Run: FANGS_API_KEY=... ./stream_window
// =============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include "raylib.h"
#include "cJSON.h"

#define ENDPOINT "https://api.openai.com/v1/chat/completions"  // any OpenAI-compatible endpoint
#define MODEL    "gpt-4o-mini"                                  // any model the endpoint serves
#define PROMPT   "Explain what a pseudo-terminal (PTY) is in 3 short sentences."

// ---- shared state between worker (writer) and main thread (reader) ----------
typedef struct {
    pthread_mutex_t mu;
    char  *reason; size_t reason_len, reason_cap;   // delta.reasoning_content
    char  *answer; size_t answer_len, answer_cap;   // delta.content
    volatile int done;     // worker finished
    volatile int cancel;   // main asked worker to stop (window closed)
    long http; const char *curlerr;
} Shared;

static void append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1 > *cap) { size_t c = *cap ? *cap : 256; while (c < *len+n+1) c*=2; *buf=realloc(*buf,c); *cap=c; }
    memcpy(*buf + *len, s, n); *len += n; (*buf)[*len] = 0;
}

// ---- SSE parsing (worker side) ----------------------------------------------
typedef struct { Shared *sh; char *line; size_t line_len, line_cap; } Parser;

static void handle_data(Parser *p, const char *json) {
    if (strcmp(json, "[DONE]") == 0) return;
    cJSON *root = cJSON_Parse(json);
    if (!root) return;
    cJSON *c0 = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "choices"), 0);
    cJSON *delta = cJSON_GetObjectItemCaseSensitive(c0, "delta");
    if (delta) {
        cJSON *r = cJSON_GetObjectItemCaseSensitive(delta, "reasoning_content");
        cJSON *c = cJSON_GetObjectItemCaseSensitive(delta, "content");
        pthread_mutex_lock(&p->sh->mu);
        if (cJSON_IsString(r) && r->valuestring)
            append(&p->sh->reason, &p->sh->reason_len, &p->sh->reason_cap, r->valuestring, strlen(r->valuestring));
        if (cJSON_IsString(c) && c->valuestring)
            append(&p->sh->answer, &p->sh->answer_len, &p->sh->answer_cap, c->valuestring, strlen(c->valuestring));
        pthread_mutex_unlock(&p->sh->mu);
    }
    cJSON_Delete(root);
}

static size_t on_chunk(char *ptr, size_t size, size_t nmemb, void *ud) {
    Parser *p = ud;
    if (p->sh->cancel) return 0;                 // abort transfer if window closed
    size_t n = size * nmemb;
    append(&p->line, &p->line_len, &p->line_cap, ptr, n);
    char *start = p->line, *nl;
    while ((nl = memchr(start, '\n', (p->line + p->line_len) - start)) != NULL) {
        *nl = 0; size_t ll = strlen(start);
        if (ll && start[ll-1]=='\r') start[ll-1]=0;
        if (strncmp(start, "data: ", 6) == 0) handle_data(p, start + 6);
        start = nl + 1;
    }
    size_t rem = (p->line + p->line_len) - start;
    memmove(p->line, start, rem); p->line_len = rem; p->line[rem]=0;
    return n;
}

static void *worker(void *arg) {
    Shared *sh = arg;
    const char *key = getenv("FANGS_API_KEY");

    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "model", MODEL);
    cJSON_AddBoolToObject(req, "stream", 1);
    cJSON_AddNumberToObject(req, "max_tokens", 512);
    cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "user");
    cJSON_AddStringToObject(m, "content", PROMPT);
    cJSON_AddItemToArray(msgs, m);
    char *body = cJSON_PrintUnformatted(req);

    CURL *curl = curl_easy_init();
    size_t alen = strlen(key) + 32; char *auth = malloc(alen);
    snprintf(auth, alen, "Authorization: Bearer %s", key);
    struct curl_slist *h = NULL;
    h = curl_slist_append(h, "Content-Type: application/json");
    h = curl_slist_append(h, "Accept: text/event-stream");
    h = curl_slist_append(h, auth);

    Parser p; memset(&p, 0, sizeof p); p.sh = sh;
    curl_easy_setopt(curl, CURLOPT_URL, ENDPOINT);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &p);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &sh->http);
    sh->curlerr = curl_easy_strerror(rc);
    sh->done = 1;

    free(p.line); free(auth); free(body); cJSON_Delete(req);
    curl_slist_free_all(h); curl_easy_cleanup(curl);
    return NULL;
}

// ---- naive word-wrap drawing (handles '\n' + pixel width) --------------------
static int draw_wrapped(const char *text, int x, int y, int fs, int maxw, Color col) {
    int lineH = fs + 6, cy = y;
    char line[4096]; size_t ll = 0; line[0] = 0;
    const char *p = text;
    while (*p) {
        if (*p == '\n') { DrawText(line, x, cy, fs, col); cy += lineH; ll = 0; line[0]=0; p++; continue; }
        char word[1024]; size_t wl = 0;
        while (*p && *p!=' ' && *p!='\n' && wl < sizeof(word)-1) word[wl++] = *p++;
        word[wl] = 0;
        char tent[5120];
        snprintf(tent, sizeof tent, "%s%s%s", line, ll ? " " : "", word);
        if (ll && MeasureText(tent, fs) > maxw) {
            DrawText(line, x, cy, fs, col); cy += lineH;
            snprintf(line, sizeof line, "%s", word);
        } else snprintf(line, sizeof line, "%s", tent);
        ll = strlen(line);
        while (*p == ' ') p++;
    }
    if (ll) { DrawText(line, x, cy, fs, col); cy += lineH; }
    return cy;
}

int main(void) {
    if (!getenv("FANGS_API_KEY")) { fprintf(stderr, "ERROR: FANGS_API_KEY not set\n"); return 2; }

    Shared sh; memset(&sh, 0, sizeof sh);
    pthread_mutex_init(&sh.mu, NULL);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    pthread_t th;
    pthread_create(&th, NULL, worker, &sh);   // network runs off the UI thread

    const int W = 920, H = 680;
    InitWindow(W, H, "Fangs — Phase 0b: streaming AI (worker thread + Raylib)");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        // snapshot shared text under lock, then draw outside the lock
        pthread_mutex_lock(&sh.mu);
        char *r = sh.reason ? strdup(sh.reason) : strdup("");
        char *a = sh.answer ? strdup(sh.answer) : strdup("");
        int done = sh.done; long http = sh.http;
        pthread_mutex_unlock(&sh.mu);

        BeginDrawing();
        ClearBackground((Color){18, 18, 22, 255});
        DrawText("THINKING", 20, 16, 18, (Color){120, 120, 140, 255});
        int y = draw_wrapped(r, 20, 42, 18, W - 40, (Color){150, 150, 165, 255});
        y += 10;
        DrawText("ANSWER", 20, y, 20, (Color){90, 200, 140, 255});
        draw_wrapped(a, 20, y + 28, 20, W - 40, RAYWHITE);

        const char *status = done ? TextFormat("done · HTTP %ld · close window to exit", http)
                                  : "streaming…";
        DrawText(status, 20, H - 28, 16, (Color){200, 180, 80, 255});
        EndDrawing();
        free(r); free(a);
    }

    sh.cancel = 1;                 // tell worker to abort if still streaming
    pthread_join(th, NULL);
    CloseWindow();
    curl_global_cleanup();
    pthread_mutex_destroy(&sh.mu);
    printf("final: HTTP %ld, curl=%s\n", sh.http, sh.curlerr ? sh.curlerr : "?");
    return 0;
}
