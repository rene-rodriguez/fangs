# Phase 5 Handoff — Inline AI Generation (`Ctrl+Space`)

> **Audience:** whoever executes Phase 5 (you, or a fresh agent session).
> **Prereq:** Phases 1–4 are done — terminal core, config/settings, split sidebar, and
> streaming AI all build/run/test clean. This is the **last feature phase**; it's also the
> smallest, because it reuses the Phase 4 AI plumbing almost wholesale.
> **Date:** 2026-06-21.

---

## 1. Where Phase 4 left things (everything you reuse)

Phase 5 adds a second, smaller AI input surface. Almost all the machinery already exists:

```
src/
  ai_provider.h         # the AI seam — ai_stream_start/poll/error/cancel/free (REUSE as-is)
  ai_http.c             # worker pthread + libcurl + SSE (REUSE as-is)
  context.{c,h}         # context_build(te, lines, bytes) — redacted recent output (REUSE)
  cmdextract.{c,h}      # command_extract() — pull a single-line command from a fence (REUSE)
  pty.{c,h}             # pty_write(fd, buf, len) — the staged-injection primitive (REUSE)
  main.c                # has resolve_api_key(), the per-frame poll/drain pattern, and the
                        #   shortcut-interception pattern (Ctrl+, , Cmd+B) to copy
```

What's genuinely new in Phase 5 is just: a floating one-line input, a strict "command-only"
system prompt, and a sanitiser for the model's reply. **You are not touching the network,
threading, or redaction code** — only adding a small UI + prompt on top of the existing seam.

**Build & iterate:** `cmake --build build`; `ctest --test-dir build --output-on-failure`;
`bash scripts/macos-build.sh` for a clean build. Run: `./build/fangs`.

---

## 2. Phase 5 goal & exit criterion

`Ctrl+Space` opens a small floating prompt over the terminal. You type a request in natural
language; the model returns a single shell command, which is **staged at the shell prompt** (typed
in for you, no Enter). You review and run it yourself.

**Exit test:**
1. At a shell prompt, press `Ctrl+Space`; a floating input appears and captures keystrokes (they do
   **not** go to the shell).
2. Type "undo the last git commit" and press Enter; a brief "thinking…" state shows, then the
   floating input closes and `git reset --soft HEAD~1` (or equivalent) appears **at the shell
   prompt, not executed**.
3. Press Enter yourself to run it (or edit it first). `Esc` at any point cancels with nothing injected.
4. No key configured → a clear inline message, no hang/crash.

---

## 3. The spine — a state machine over the existing seam

It's the same intercept → capture → stream → inject loop the sidebar uses, with a different surface
and a one-command prompt. States: `INLINE_IDLE → INLINE_INPUT → INLINE_WAITING → (inject) → IDLE`.

```
Ctrl+Space (IDLE)      -> open floating input, capture focus            -> INPUT
type + Enter (INPUT)   -> ai_stream_start(command-only prompt)          -> WAITING
poll done (WAITING)    -> sanitise reply -> pty_write(cmd, no '\n')      -> IDLE  (input closes)
Esc (INPUT|WAITING)    -> cancel stream if any, inject nothing          -> IDLE
```

Use a **separate** `AiStream *inline_stream` (distinct from the sidebar's `active_stream`) so the two
features never trample one stream handle.

---

## 4. Work breakdown

### 4a. `src/ui_inline.{c,h}` — the floating prompt
```c
typedef enum { INLINE_IDLE, INLINE_INPUT, INLINE_WAITING } InlineState;

void        ui_inline_open(int cursor_px, int cursor_py);  // Ctrl+Space: enter INPUT near cursor
bool        ui_inline_active(void);                        // true in INPUT or WAITING (gate PTY)
InlineState ui_inline_state(void);
const char *ui_inline_take_prompt(void);  // non-NULL once on the frame Enter is pressed (INPUT->WAITING)
void        ui_inline_set_waiting(const char *status);     // host calls after starting the stream
void        ui_inline_cancel(void);                        // Esc / done: back to IDLE
void        ui_inline_draw(void);                          // draw the floating box (top layer-ish)
```
- A single `GuiTextBox`-style line in a small rounded panel near the cursor, with a hint label
  ("Ask for a command — Enter to stage, Esc to cancel"). In `INLINE_WAITING`, show a spinner / the
  status text instead of accepting input.
- Like the sidebar, capture Enter via `IsKeyPressed(KEY_ENTER)` before/around the text box and clear
  the buffer on submit. `Esc` always cancels.

### 4b. Cursor anchoring (where the box appears)
The box should appear near the terminal cursor. The host already has `cell_width/height`, `pad`, and
the terminal handle. Get the cursor cell from the engine (add a tiny accessor to `term_engine` if
needed, e.g. `term_engine_cursor(te, &row, &col)` over libghostty-vt's cursor query) and compute
`cursor_px = pad + col*cell_width`, `cursor_py = pad + (row+1)*cell_height`. **If exposing the cursor
is fiddly, ship v1 with the box anchored at a fixed spot** (e.g. bottom-left of the terminal area)
and treat cursor-anchoring as a refinement — don't block the phase on it.

### 4c. The strict prompt + the sanitiser
System prompt (command-ONLY — this is the whole trick):
```
You translate the user's request into a single shell command for their shell.
Output ONLY the command on one line. No explanation, no markdown, no backticks.
Recent terminal output is provided for context. If unsure, output the closest single command.
```
Send `context_build(te, 40, 4096)` (a smaller budget than the sidebar) as a context message so
"undo the last git commit" knows it's a git repo. Then **sanitise** the reply before injecting —
models sometimes wrap it in a fence or add prose anyway:
```c
// src/inline_cmd.{c,h} — pure + testable
// Strip code fences/backticks/prose; return the single command line. If the reply
// contains a fenced block, prefer command_extract(); else take the first non-empty,
// non-prose line and trim a leading "$ "/"> " prompt.
bool inline_sanitize_command(const char *reply, char *out, int out_size);
```
`tests/inline_cmd_tests.c`: fenced reply → bare command; reply with a leading `$ ` → stripped;
multi-line prose + command → the command line; empty → false. (Mirror `cmdextract_tests`.)

### 4d. `main.c` integration (reuses Phase 4 patterns)
1. **Intercept `Ctrl+Space`** before PTY input, beside the existing `Ctrl+,` / `Cmd+B` blocks:
   ```c
   if (IsKeyPressed(KEY_SPACE) &&
       (IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL))) {
       ui_inline_open(cursor_px, cursor_py);
       /* drain GetCharPressed() */
   }
   ```
   (`Ctrl+Space` is a real terminal key — NUL — so consuming it here is correct for the AI surface;
   document that Fangs reserves it. If a user needs literal `Ctrl+Space`, that's a known tradeoff.)
2. **Gate PTY input** while inline is active — extend the existing gate:
   `... && !ui_inline_active()` alongside the sidebar/modal conditions.
3. **On submit** (`ui_inline_take_prompt()` returns non-NULL): start a stream with the command-only
   prompt and `inline_stream = ai_stream_start(...)`, then `ui_inline_set_waiting("thinking…")`.
   Reuse `resolve_api_key(&cfg)`; if no key, show the inline no-key message and cancel.
4. **Drain each frame** (mirror the sidebar drain): accumulate `inline_stream` deltas (answer region
   only — ignore reasoning for inline) into a buffer. On `done`: `inline_sanitize_command()` →
   `pty_write(pty_fd, cmd, strlen(cmd))` **with NO newline** → `ui_inline_cancel()` (close) →
   free/join the stream. On `!ok`: show the error briefly, don't inject.
5. **Draw** `ui_inline_draw()` near the end of the frame, after the sidebar, before the settings
   modal (modal stays the top layer).
6. **Cleanup:** free/join `inline_stream` in the `cleanup:` path, exactly like `active_stream`.

---

## 5. Gotchas (read before coding)

- **Never inject a newline.** Same rule as the Phase 4 Run button: stage the command, let the user
  press Enter. `inline_sanitize_command` must also collapse any trailing newline the model emitted.
- **Single-line only.** If the model returns a multi-line script, take the first command line (or
  refuse and show "couldn't produce a single command") — injecting embedded newlines would execute
  intermediate lines. `command_extract` already enforces this for fenced replies; keep the same bar.
- **Gate input the moment the box opens.** While `ui_inline_active()`, do not forward keys to the
  PTY, or the request text leaks into the shell. Drain `GetCharPressed()` on the open frame so the
  triggering keystroke doesn't seed the box.
- **Two streams, two handles.** `inline_stream` is separate from the sidebar's `active_stream`.
  Don't share; free/join each independently. Opening inline while the sidebar streams is fine (two
  worker threads), but you may choose to disallow it for simplicity.
- **Worker-thread rule still holds.** All `ui_inline_*` calls happen on the main thread; the worker
  only fills its mutex buffer (unchanged from Phase 4).
- **Reasoning models:** for inline you only want the final command, so drain only the answer region
  and ignore `is_reasoning` deltas (or strip them in the sanitiser).
- **No key / error:** show it in the floating box and close cleanly — never leave the user stuck in a
  captured-input state with no feedback.

## 6. Acceptance checklist
- [ ] `Ctrl+Space` opens the floating input; keystrokes are captured, not sent to the shell.
- [ ] A natural-language request stages a single command at the prompt with **no** trailing newline.
- [ ] `Esc` cancels from both INPUT and WAITING with nothing injected; focus returns to the terminal.
- [ ] Multi-line model output never injects newlines (first command only, or a clean refusal).
- [ ] No key configured → clear inline message, no hang/crash; closing mid-request joins the worker.
- [ ] `ctest` green (existing 6 + `inline_cmd_tests`); `cmake --build build` warning-clean; `vim`/`htop` fine.

## 7. After Phase 5 — toward v1
All five feature phases done = the anti-Warp MVP. Remaining polish for a v1 release (no longer
risk-bearing, so no handoff docs needed — pick by appetite):
- **CachyOS/Linux verification** on real hardware (expected clean; the one untested target).
- **Theming:** wire `cfg.theme` to the full terminal palette (today it only sets the window bg).
- **Multi-turn chat:** send prior sidebar messages as history instead of a single user turn.
- **Anthropic-native provider** path in `ai_http.c` (`/v1/messages`, `x-api-key`, different SSE shape).
- **Packaging:** AUR `PKGBUILD` / `cmake --install`; app bundle on macOS.
- **Sidebar font:** use the embedded JetBrains Mono instead of `GetFontDefault()` for crisper text.
See `plan.md` §8 (risk register) and §9 (open questions).
