// SSE / OpenAI-streaming delta parser — the pure, network-free core of the AI
// transport. ai_http.c feeds raw libcurl bytes in; this splits them into lines,
// strips the "data: " prefix, parses each JSON chunk with cJSON, and surfaces
// each token via a callback. Kept separate from ai_http.c so it can be unit
// tested with canned SSE bytes (no sockets).
//
// Handles: partial lines across feed() boundaries, CRLF, the trailing
// "data: [DONE]" sentinel, and BOTH delta.reasoning_content (thinking) and
// delta.content (answer) — reasoning models stream the former first.
//
// Also handles Ollama's native /api/chat stream, which is a different wire
// dialect (NDJSON: each line IS a full JSON object, no "data: " prefix, no
// blank-line event framing) via sse_parser_new_ndjson() — same line-buffering,
// different per-line dispatch.
#ifndef FANGS_SSE_H
#define FANGS_SSE_H

#include <stddef.h>
#include <stdbool.h>

// Called once per extracted token. `text` is NUL-terminated; `is_reasoning`
// marks delta.reasoning_content vs delta.content.
typedef void (*SseDeltaFn)(void *userdata, const char *text, bool is_reasoning);

typedef struct SseParser SseParser;

SseParser *sse_parser_new(void);

// Construct a parser in NDJSON mode: each line is a complete JSON object
// (Ollama's native /api/chat stream shape) rather than an SSE "data: " event.
// Reuses the same line-buffering as sse_parser_new(); only the per-line JSON
// dispatch differs. Free with sse_parser_free() as usual.
SseParser *sse_parser_new_ndjson(void);

void       sse_parser_free(SseParser *p);

// Feed a chunk of raw bytes (any size, arbitrary boundaries). Invokes `fn` for
// each complete delta found. Incomplete trailing data is buffered until the
// next call.
void sse_parser_feed(SseParser *p, const char *bytes, size_t n,
                     SseDeltaFn fn, void *userdata);

#endif // FANGS_SSE_H
