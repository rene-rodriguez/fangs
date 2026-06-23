// SSE / OpenAI-streaming delta parser — the pure, network-free core of the AI
// transport. ai_http.c feeds raw libcurl bytes in; this splits them into lines,
// strips the "data: " prefix, parses each JSON chunk with cJSON, and surfaces
// each token via a callback. Kept separate from ai_http.c so it can be unit
// tested with canned SSE bytes (no sockets).
//
// Handles: partial lines across feed() boundaries, CRLF, the trailing
// "data: [DONE]" sentinel, and BOTH delta.reasoning_content (thinking) and
// delta.content (answer) — reasoning models stream the former first.
#ifndef NOVA_SSE_H
#define NOVA_SSE_H

#include <stddef.h>
#include <stdbool.h>

// Called once per extracted token. `text` is NUL-terminated; `is_reasoning`
// marks delta.reasoning_content vs delta.content.
typedef void (*SseDeltaFn)(void *userdata, const char *text, bool is_reasoning);

typedef struct SseParser SseParser;

SseParser *sse_parser_new(void);
void       sse_parser_free(SseParser *p);

// Feed a chunk of raw bytes (any size, arbitrary boundaries). Invokes `fn` for
// each complete delta found. Incomplete trailing data is buffered until the
// next call.
void sse_parser_feed(SseParser *p, const char *bytes, size_t n,
                     SseDeltaFn fn, void *userdata);

#endif // NOVA_SSE_H
