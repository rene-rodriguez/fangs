# Phase 4 Handoff — Context Extraction & Streaming AI

> **Audience:** whoever executes Phase 4 (you, or a fresh agent session).
> **Prereq:** Phases 1–3 are done — terminal core, config + settings modal, split layout +
> chat sidebar all build/run/test clean. This doc is self-contained; `docs/plan.md` §6.B and §7
> have the broader picture.
> **Date:** 2026-06-21.

This is the phase where the sidebar starts talking to a model. It's the biggest one (networking +
a worker thread + streaming UI + command injection), but **the two hard risks are already retired**:
the streaming-in-C spike (`spike/ai_stream/`) works end-to-end against a live endpoint, and the
sidebar already has a `ui_sidebar_push(MSG_ASSISTANT, …)` surface to stream into.

---

## 1. Where Phase 3 left things (assets you build on)

```
src/
  main.c               # frame loop; submit handler at the sidebar draw call (~1551)
  term_engine.{c,h}    # SEAM — and term_engine_dump_text() already returns a plain-text
                       #   dump of screen+scrollback (THE context source; built in Phase 1)
  pty.{c,h}            # pty_write(fd, buf, len) — already the injection primitive for Run buttons
  ui_sidebar.{c,h}     # chat panel; ui_sidebar_push(role,text); MSG_USER/ASSISTANT/SYSTEM
  config.{c,h}         # AppConfig already has provider/endpoint/model/api_key/stream/max_tokens
  layout.{c,h}, ui_sidebar_model.{c,h}, ui_settings.{c,h}, config.{c,h}  # + their tests
spike/ai_stream/
  stream_window.c      # ⭐ THE TEMPLATE: worker pthread + mutex buffer + SSE/cJSON parse,
                       #   splits delta.reasoning_content vs delta.content. Promote this.
  cJSON.{c,h}          # vendored JSON parser — move into src/ (or vendor/) for Phase 4
```

**Build & iterate:** `cmake --build build` (incremental, clang-only); `bash scripts/macos-build.sh`
for a clean/reconfigure build; `ctest --test-dir build --output-on-failure`. Run: `./build/nova-terminal`.

**Two things already wired that Phase 4 plugs into:**
- The sidebar's **submit** path (`main.c` ~1555): `ui_sidebar_draw(...)` returns true with the typed
  prompt; today main.c echoes it. Phase 4 replaces that echo with "build context → start stream".
- `ui_sidebar_push(MSG_ASSISTANT, …)` exists — but it *appends a new message*. Streaming needs to
  *append to the current assistant message* (see §4e: add a tiny streaming API).

---

## 2. Phase 4 goal & exit criterion

Typing a question in the sidebar captures recent terminal output as context, sends it to the
configured model, and streams a context-aware answer back token-by-token. Fenced shell commands in
the answer get a **Run** button that stages the command at the shell prompt (never auto-runs).

**Exit test:**
1. Run a command that errors (e.g. `cat /nonexistent`). Ask the sidebar "what went wrong?".
2. The answer **streams in** (tokens appear progressively, UI stays responsive at 60fps), and it
   **references the actual on-screen error** (proving context extraction works).
3. If the answer contains a fenced command, a **Run** button appears; clicking it places the command
   at the shell prompt **without** pressing Enter.
4. With no key configured, the sidebar shows a clear error instead of hanging or crashing.

---

## 3. The architecture spine (read this first)

raylib is single-threaded immediate-mode; libcurl's streaming blocks. **The hardest correctness rule
of this phase:**

> **The network runs on a worker pthread that touches ONLY a mutex-guarded buffer. The raylib main
> loop drains that buffer each frame. No raylib/UI function is ever called off the main thread.**

`spike/ai_stream/stream_window.c` already does exactly this — `Shared{mu, reason[], answer[], done}`,
a `worker()` thread running `curl_easy_perform` with `on_chunk` → SSE line split → cJSON →
append-under-lock, and a main loop that locks, copies new bytes, unlocks. **Promote that structure
verbatim**; the only change is exposing it as a poll API instead of `stream_window`'s inline `main`.

So the seam is a **poll**, not a callback (callbacks would fire on the worker thread):
```c
// ai_provider.h — the AI seam (swap providers/transports here, never in the host)
typedef struct AiStream AiStream;   // opaque; owns the worker thread + buffer

typedef struct { const char *role; const char *content; } AiMessage;   // role: "system"|"user"|"assistant"

typedef struct {
    const char *endpoint;     // cfg.endpoint (OpenAI-compatible /chat/completions)
    const char *model;        // cfg.model
    const char *api_key;      // RESOLVED: getenv("NOVA_API_KEY") ?: cfg.api_key
    int         max_tokens;   // cfg.max_tokens
    bool        stream;       // cfg.stream (true)
} AiConfig;

// Non-blocking: spawns the worker, returns immediately. NULL on setup failure
// (e.g. empty key). Copies what it needs from args; caller may free them after.
AiStream *ai_stream_start(const AiConfig *cfg, const AiMessage *msgs, int n_msgs);

// Call once per frame from the MAIN thread. Copies any newly-arrived text into
// out (NUL-terminated). *is_reasoning marks whether this delta is thinking vs answer.
// Returns bytes copied (0 if nothing new). Sets *done when the stream finished and
// *ok to the success flag once done.
int  ai_stream_poll(AiStream *s, char *out, int out_size,
                    bool *is_reasoning, bool *done, bool *ok);

const char *ai_stream_error(AiStream *s);   // human-readable error once done && !ok
void ai_stream_cancel(AiStream *s);         // signal worker to stop (set a flag curl checks)
void ai_stream_free(AiStream *s);           // joins the thread, frees buffers — MUST join
```
Implementation note: keep **two** buffers in `Shared` (reasoning + answer) and a read cursor for
each, so `poll` can report which region grew. Simplest poll contract: drain reasoning first (with
`*is_reasoning=true`) until its cursor catches up, then answer.

---

## 4. Work breakdown

### 4a. Build: vendor cJSON + add libcurl
- Move `spike/ai_stream/cJSON.{c,h}` → `src/` (or `vendor/cjson/`). Add `src/cJSON.c` to the
  `nova-terminal` target.
- libcurl: `find_package(CURL REQUIRED)` then `target_link_libraries(nova-terminal CURL::libcurl)`.
  macOS ships libcurl in the SDK; CachyOS has it (`pacman -S curl`). pthreads is libc on both
  (add `Threads::Threads` via `find_package(Threads)` to be safe).
- Add `src/ai_http.c`, `src/context.c`, `src/sse.c` (see below) to the executable.

### 4b. `src/ai_provider.h` + `src/ai_http.c` — the seam + the worker
Lift `Shared`/`Parser`/`on_chunk`/`worker` from `stream_window.c`. Changes from the spike:
- Build the JSON request body from `AiMessage[]` + `AiConfig` (not a hardcoded `PROMPT`). Use cJSON
  to serialize `{model, max_tokens, stream:true, messages:[...]}`.
- `Authorization: Bearer <key>` header (key from `AiConfig.api_key`, already resolved by the host).
- Add a `volatile int cancel` flag in `Shared`; check it in a `CURLOPT_XFERINFOFUNCTION` progress
  callback and return non-zero to abort (clean cancel).
- Expose `ai_stream_start/poll/error/cancel/free` per §3. `ai_stream_free` **must** `pthread_join`.
- `curl_global_init(CURL_GLOBAL_DEFAULT)` once at startup (main), `curl_global_cleanup()` at exit —
  not per-request.

### 4c. `src/sse.{c,h}` — the pure parser (testable)
The SSE/JSON delta logic is the part most worth unit-testing (no network needed). Factor it out:
```c
// Feed raw bytes (as they arrive); invokes on_delta for each parsed token.
// Handles partial lines across chunk boundaries, "data: " prefix, "[DONE]",
// and both delta.reasoning_content and delta.content.
typedef void (*SseDeltaFn)(void *ud, const char *text, bool is_reasoning);
typedef struct SseParser SseParser;
SseParser *sse_parser_new(void);
void sse_parser_feed(SseParser *p, const char *bytes, size_t n, SseDeltaFn fn, void *ud);
void sse_parser_free(SseParser *p);
```
`ai_http.c`'s `on_chunk` becomes a thin wrapper that feeds bytes to the parser and appends deltas to
`Shared` under the lock. `tests/sse_tests.c`: feed canned SSE (including a token split across two
`feed` calls, a reasoning-then-content sequence, and `[DONE]`) and assert the extracted deltas.

### 4d. `src/context.{c,h}` — capture + **mandatory redaction**
```c
// Build the context block to send: last max_lines of terminal text, capped at
// max_bytes, with secrets scrubbed. Returns malloc'd NUL-terminated text (caller frees).
char *context_build(TermEngine *te, int max_lines, int max_bytes);

// Pure + testable: redact secrets in-place (used by context_build).
void context_redact(char *text);
```
- Source: `term_engine_dump_text(te)` (already returns screen+scrollback as plain text). Take the
  **last** `max_lines` lines (the recent stuff), enforce a hard `max_bytes` budget (token-cost
  control — default e.g. 8 KB / ~120 lines, make it config later).
- **Redaction is not optional** — we're shipping terminal contents to a third party. Scrub at least:
  `sk-…`, `fw_…`, `AKIA[0-9A-Z]{16}`, `ghp_…`/`github_pat_…`, `Bearer <token>`, lines matching
  `(?i)(api[_-]?key|secret|password|token)\s*[:=]\s*\S+`, and `export FOO=…`-style secret assignments.
  Replace the secret with `«redacted»`. `tests/context_tests.c` asserts each pattern is scrubbed and
  that ordinary output survives.

### 4e. Sidebar: streaming append + reasoning region
`ui_sidebar_push` appends whole messages; streaming needs to grow the *current* assistant message.
Add to `ui_sidebar.{c,h}`:
```c
void ui_sidebar_begin_assistant(void);            // push an empty MSG_ASSISTANT message, remember its index
void ui_sidebar_append_assistant(const char *delta, bool is_reasoning);  // append to that message
void ui_sidebar_end_assistant(void);              // mark complete (stops the "typing" indicator)
bool ui_sidebar_is_streaming(void);
```
- **Reasoning vs answer:** the spike proved models stream `reasoning_content` (thinking) before
  `content` (answer). Render thinking dimmed/italic in a collapsible or clearly-secondary block above
  the answer. Simplest MVP: store `char reasoning[]` + `char answer[]` per assistant message; draw
  reasoning in grey, answer in normal. Don't drop reasoning silently — users find it useful.
- While streaming, gate a second submit (one request in flight in Phase 4) — disable Send / show a
  "Stop" button wired to `ai_stream_cancel`.

### 4f. `main.c` integration (lifecycle is the tricky part)
Hold one `AiStream *active_stream` in `main()`. At the submit site (replace the current echo):
```c
if (ui_sidebar_draw(lo.sidebar, submitted, sizeof submitted)) {
    ui_sidebar_push(MSG_USER, submitted);
    char *ctx = context_build(te, 120, 8192);
    AiMessage msgs[] = {
        {"system", "You are a terminal assistant. Use the provided recent terminal output to answer. "
                   "If you propose a command, put ONLY the command in a single fenced ```sh block."},
        {"user",   /* ctx + "\n\n---\n\n" + submitted, assembled into one buffer */ },
    };
    AiConfig aic = { cfg.endpoint, cfg.model, resolve_key(&cfg), cfg.max_tokens, cfg.stream };
    active_stream = ai_stream_start(&aic, msgs, 2);
    if (!active_stream) ui_sidebar_push(MSG_SYSTEM, "No API key — set NOVA_API_KEY or add one in Ctrl+,");
    else ui_sidebar_begin_assistant();
    free(ctx);
}
```
Then **each frame**, before drawing the sidebar, drain the stream:
```c
if (active_stream) {
    char buf[2048]; bool is_reason=false, done=false, ok=false;
    while (ai_stream_poll(active_stream, buf, sizeof buf, &is_reason, &done, &ok) > 0)
        ui_sidebar_append_assistant(buf, is_reason);
    if (done) {
        ui_sidebar_end_assistant();
        if (!ok) ui_sidebar_push(MSG_SYSTEM, ai_stream_error(active_stream));
        ai_stream_free(active_stream);     // joins the worker
        active_stream = NULL;
    }
}
```
- `resolve_key(&cfg)`: `const char *k = getenv("NOVA_API_KEY"); return (k && *k) ? k : cfg.api_key;`
- **Cleanup:** in the `cleanup:` path, `if (active_stream) { ai_stream_cancel(active_stream);
  ai_stream_free(active_stream); }` so closing the window mid-stream joins the thread (no leak/crash).

### 4g. Run buttons — fenced command → staged injection
- Parse the assistant answer for ```` ``` ```` fenced blocks (optionally only `sh`/`bash`/`shell`
  fences). For each, the sidebar draws a **Run** button next to the rendered block.
- The host owns the PTY, so surface the request and inject in `main.c` (mirror the submit pattern):
  `ui_sidebar_draw` returns/exposes a "run requested" command → `main.c` does
  `pty_write(pty_fd, cmd, strlen(cmd))` **with no trailing `\n`**. It lands at the prompt; the user
  reviews and presses Enter. Factor command extraction into a pure `command_extract()` + tests.
- **Never** append `\n`. **Never** auto-run. This is the single most important safety rule in the app.

---

## 5. Gotchas (read before coding)

- **No UI calls off the worker thread.** The worker only locks the mutex and appends bytes. Every
  `DrawX`/`Gui*`/`ui_sidebar_*` call stays on the main thread. Violating this = intermittent crashes.
- **`ai_stream_free` must `pthread_join`.** Don't detach; you need a clean join on done *and* on
  window-close-mid-stream (cancel then free).
- **Partial UTF-8 / partial JSON across chunks.** `CURLOPT_WRITEFUNCTION` hands you arbitrary byte
  boundaries — a multibyte char or a `data:` line can split across two callbacks. The SSE parser must
  buffer an incomplete trailing line and resume next feed (the spike's `Parser{line, line_len}` does
  this — keep it).
- **Redaction is mandatory and happens before the bytes leave the process.** Put it inside
  `context_build`, not at the call site, so there's no path that sends un-redacted text.
- **API key: never log it, never put it in the request echo, never write it to the report.** It comes
  from env (preferred) or the `0600` config. The secret-scan before any commit still applies.
- **reasoning_content is real** (reasoning / o-style models): handle `delta.reasoning_content` *and*
  `delta.content`. A model that only sends `content` must still work (reasoning region stays empty).
- **`curl_global_init` once**, at process start, not per request (it's not thread-safe to call late).
- **Wrapped-text + newlines:** `ui_sidebar.c`'s `wrapped_text` currently treats `\n` as a normal char.
  Assistant answers have real newlines — teach the wrapper to break on `\n` (split into paragraphs
  first, then word-wrap each) or code blocks will render as one long line.
- **One request in flight (Phase 4).** Disable Send while streaming (offer Stop). Multiple concurrent
  streams + the single `active_stream` slot = a leak. Multi-turn/queueing is a later refinement.
- **Provider shape:** OpenAI-compatible `/chat/completions` is the baseline (works for OpenAI,
  Ollama, and most endpoints). Anthropic-native (`/v1/messages`, `x-api-key`, different SSE event shape)
  is a *different* code path — gate it behind `cfg.provider` in `ai_http.c` if/when needed; don't
  assume one wire format. (Proven against an OpenAI-compatible endpoint.)

---

## 6. Acceptance checklist
- [ ] `cat /nonexistent` then ask "what went wrong?" → answer **streams** and names the real error.
- [ ] UI stays at 60fps while tokens arrive (worker thread, not blocking the loop).
- [ ] Reasoning (if the model sends it) shows dimmed/separate; the answer shows normally.
- [ ] A fenced command renders a **Run** button; clicking stages it at the prompt with **no** newline.
- [ ] No key set → sidebar shows a clear message; no hang, no crash.
- [ ] Closing the window mid-stream exits cleanly (worker joined, no leak/segfault).
- [ ] Context sent is redacted: put a fake `export API_KEY=sk-abc123…` on screen, ask a question,
      confirm (via a debug dump or a breakpoint) the outgoing body shows `«redacted»`.
- [ ] `ctest` green (config, layout, sidebar-model, **+ sse, context, command-extract**);
      `cmake --build build` warning-clean.

## 7. After Phase 4
Phase 5 = inline `Ctrl+Space` generation (`src/ui_inline.c`): intercept `Ctrl+Space`, open a floating
input near the cursor, capture focus, send via the **same `ai_provider` seam** with a strict
"return only the raw command, no prose, no fences" system prompt, then `pty_write` the result staged
at the prompt — the user presses Enter. Most of the plumbing (provider seam, key resolution, staged
injection) is reused from Phase 4. See `plan.md` §6.C and §7.
