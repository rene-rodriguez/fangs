// ai_provider — THE AI SEAM.
//
// The single boundary between the host and "talking to a model". Today it's an
// OpenAI-compatible streaming HTTP client (ai_http.c); swapping providers or
// transports (Anthropic-native, a local model, a mock) means changing only the
// implementation behind this header, never the host.
//
// Threading contract (critical): a request runs on a worker thread that touches
// ONLY this stream's mutex-guarded buffers. The host calls ai_stream_poll()
// from the main (UI) thread each frame to drain new tokens. No callback ever
// fires on the worker thread — so the host never touches raylib off-thread.
#ifndef FANGS_AI_PROVIDER_H
#define FANGS_AI_PROVIDER_H

#include <stdbool.h>

typedef struct {
    const char *role;     // "system" | "user" | "assistant"
    const char *content;
} AiMessage;

typedef struct {
    const char *provider;   // "openai" (default/compatible) | "anthropic" (native)
    const char *endpoint;   // full chat-completions / messages URL
    const char *model;
    const char *api_key;    // already resolved by the host (env wins over config)
    int         max_tokens;
    bool        stream;     // Phase 4 assumes true (SSE)
} AiConfig;

typedef struct AiStream AiStream;

// Process-global curl init/cleanup. Call once at startup / shutdown.
void ai_global_init(void);
void ai_global_cleanup(void);

// Start a streaming request on a worker thread. Non-blocking. Copies what it
// needs from cfg/msgs (caller may free them after). Returns NULL if there's no
// API key or setup fails.
AiStream *ai_stream_start(const AiConfig *cfg, const AiMessage *msgs, int n_msgs);

// Drain newly-arrived text into `out` (NUL-terminated). Call each frame from the
// main thread. Returns bytes copied (0 if nothing new). *is_reasoning marks
// thinking vs answer. *done is set true only once the stream finished AND all
// buffered text has been drained; *ok reflects success (HTTP 200, no curl error).
int ai_stream_poll(AiStream *s, char *out, int out_size,
                   bool *is_reasoning, bool *done, bool *ok);

// Human-readable error once *done && !*ok (e.g. "HTTP 401", a curl message).
const char *ai_stream_error(AiStream *s);

// Ask the worker to abort (safe to call mid-stream). ai_stream_free joins it.
void ai_stream_cancel(AiStream *s);
void ai_stream_free(AiStream *s);

#endif // FANGS_AI_PROVIDER_H
