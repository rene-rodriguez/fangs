# Fangs — Technical Specification

> **Status:** Draft v1 · **Last updated:** 2026-06-20
> **Companion:** [`docs/plan.md`](./plan.md) (roadmap & rationale)
> **Engine:** Path A — `libghostty-vt` + Raylib (C). Decision recorded in plan §3.

This document specifies *how* Fangs is built: module contracts, data flows, the config
format, the AI/networking model, concurrency, and per-milestone acceptance criteria. It is
written to be executable as a build plan, not just descriptive.

---

## 1. Scope

### In scope (v1)
- A daily-drivable GPU-accelerated terminal for **CachyOS/Linux and macOS** (both first-class targets). macOS builds via a documented toolchain workaround (`scripts/macos-build.sh`, §4.1) — verified building on macOS 26.5 arm64. **CachyOS x86-64 verified building (2026-06-22):** clean build + all 8 ctest suites pass with Zig 0.15.2 (`pacman` ships 0.16.0, which won't build the pin); GUI window open pending a graphical session.
- BYOK AI sidebar chat with terminal-context awareness and a "Run" action.
- Inline natural-language → command generation (`Ctrl+Space`), staged at the prompt.
- `ini` dotfile config + RayGUI settings modal with hot reload.

### Out of scope (v1)
- Windows (the engine supports it; we don't target it yet).
- Tabs, splits/panes beyond the single terminal + sidebar split.
- Multi-turn agentic tool-use, file editing, or command auto-execution.
- Sixel/kitty-graphics-dependent features (the engine supports them; we don't build product on them in v1).
- Telemetry, accounts, sync — by design, never.

### Non-negotiable invariants
- **The PTY byte stream is never altered to fake UI.** All AI UI is a Raylib overlay.
- **No command is executed without an explicit user keystroke.** Injection stages text; the user presses Enter.
- **Secrets never leave the machine except to the user-configured endpoint.**

---

## 2. Glossary

- **PTY** — pseudo-terminal; master fd we read/write, slave drives the child shell.
- **libghostty-vt** — Ghostty's extracted, zero-dependency VT engine (Zig lib, C ABI). Parses VT sequences, holds terminal state, exposes a render-state snapshot and a formatter.
- **Render state** — an immutable per-frame snapshot of the grid produced by the engine for renderers to iterate.
- **Formatter** — engine API that serializes terminal content to plain text / VT / HTML.
- **Seam** — a narrow internal interface (`term_engine.h`, `ai_provider.h`) that isolates a swappable dependency.

---

## 3. System Architecture

```
                         ┌─────────────────────────────────────────────┐
                         │                 main.c                       │
                         │   event loop · frame orchestration · input   │
                         │   router (PTY vs inline vs sidebar focus)    │
                         └───┬───────────────┬───────────────┬──────────┘
                             │               │               │
              ┌──────────────▼──┐   ┌─────────▼────────┐  ┌───▼─────────────┐
              │  term_engine.*  │   │   render.c        │  │  ui_*.c (RayGUI)│
              │  (SEAM)         │   │  Raylib draw:     │  │  sidebar /       │
              │  wraps          │   │  left grid +      │  │  settings /      │
              │  libghostty-vt  │   │  split layout     │  │  inline prompt   │
              └───┬─────────┬───┘   └──────────────────┘  └───┬─────────────┘
                  │         │                                  │
            ┌─────▼───┐ ┌───▼────────┐                  ┌──────▼───────┐
            │ pty.c   │ │ context.c  │                  │ ai_provider.*│
            │ forkpty │ │ formatter  │◄─────────────────┤   (SEAM)     │
            │  R/W    │ │ -> text    │   context text    │  ai_http.c   │
            └────┬────┘ └────────────┘                  │  curl+SSE+   │
                 │                                       │  cJSON +     │
            child shell                                  │  worker thr. │
                                                         └──────┬───────┘
                                                                │ HTTPS (stream)
                                                          provider endpoint
```

**Frame loop (per the ghostling reference, extended):**
1. Handle resize → `term_engine_resize()` + `ioctl(TIOCSWINSZ)`.
2. Drain PTY (`pty_read`) → `term_engine_write()` (feed bytes to the parser).
3. Reap child (`waitpid`, non-blocking).
4. Route input: if `inline_mode` → inline box; else if sidebar focused → sidebar; else encode keys → PTY.
5. Pull any streamed AI tokens from the worker's ring buffer into UI state.
6. `term_engine_update_render_state()` → snapshot.
7. `BeginDrawing()` → draw grid (left) → draw sidebar/modal/inline overlays → `EndDrawing()`.

---

## 4. Engine Integration (`term_engine`)

### 4.1 Dependency pinning (resolved in Phase 0a — 2026-06-20)
Exact pins, verified by actually cloning and configuring the build:
- **Ghostling bootstrap:** `ghostty-org/ghostling` @ `f9034e43a50a2f3a8101e35497f486090c1ddd6e`.
- **libghostty-vt:** pulled by ghostling's CMake `FetchContent` from `ghostty-org/ghostty` @ `ae52f97dcac558735cfa916ea3965f247e5c6e9e`; CMake delegates to `zig build -Demit-lib-vt`. **This ghostty SHA is the libghostty-vt revision to pin.**
- **Raylib:** `5.5` (CMake `find_package`, else `FetchContent` from the GitHub tag).
- **Zig:** exactly **0.15.2** (ghostling's `flake.nix` pins it). **Homebrew ships 0.16.0, which is incompatible** — install the 0.15.2 tarball from ziglang.org and put it first on PATH.
- Upgrades are manual: bump the ghostty SHA → rebuild → smoke-test → commit.

> **macOS 26.5 / Xcode 26 toolchain note (R3 — worked around & VERIFIED BUILDING):** Zig 0.15.2's self-hosted Mach-O linker cannot parse the macOS 26.5 SDK's `libSystem.tbd`, so a naive `zig build lib-vt` fails (`undefined symbol: __availability_version_check`, `_fork`, …). **Workaround, automated in `scripts/macos-build.sh`:** build a *hybrid SDK* — the real macOS SDK (headers + frameworks, so ghostty's `findNative` succeeds) with `usr/lib/libSystem.tbd` swapped for Zig's *bundled, parseable* one — and feed it to Zig via an `xcrun` shim on PATH (so both `findNative` and the linker's `-syslibroot` resolve to it), plus a project-local `ZIG_GLOBAL_CACHE_DIR` to avoid SDK-detection cache poisoning (the gotcha that initially looked like a hard block). clang/Apple-ld are unaffected; the binary links the real `/usr/lib/libSystem.B.dylib` at runtime. **Verified: a cold libghostty-vt build + full ghostling link succeeds on macOS 26.5 arm64 (~1.5 min) → 1.2 MB Mach-O.** Delete the shim once upstream Zig handles the macOS 26 SDK. Linux/CachyOS never hits this. Tracked in `plan.md` §7.

### 4.2 The seam (`term_engine.h`)
A deliberately small surface so libghostty-vt — or a future libvterm / Rust core — sits behind one interface:

```c
typedef struct TermEngine TermEngine;

TermEngine *term_engine_new(int cols, int rows, int cell_w, int cell_h);
void  term_engine_free(TermEngine *);

void  term_engine_write(TermEngine *, const uint8_t *bytes, size_t n);   // PTY output -> parser
void  term_engine_resize(TermEngine *, int cols, int rows, int cw, int ch);

// per-frame snapshot for the renderer
void  term_engine_update_render_state(TermEngine *);
void  term_engine_for_each_cell(TermEngine *, cell_cb cb, void *user);   // wraps row/cell iterators

// AI context extraction
// Returns a malloc'd UTF-8 string of the last `lines` rows (visible + scrollback).
char *term_engine_dump_text(TermEngine *, int lines);

// input encoding (delegates to ghostty key/mouse encoders)
size_t term_engine_encode_key(TermEngine *, const KeyEvent *, uint8_t *out, size_t cap);
```

### 4.3 Mapping to the real libghostty-vt C API
These are the actual symbols used by the ghostling reference (`include/ghostty/vt/*`); our seam delegates to them:

- **Lifecycle:** `ghostty_terminal_new`, `ghostty_terminal_resize`, `ghostty_terminal_vt_write`, `ghostty_terminal_set`, `ghostty_terminal_get`, `ghostty_terminal_free`.
- **Input encoding:** `ghostty_key_encoder_new`, `ghostty_key_event_new`, `ghostty_key_encoder_setopt_from_terminal`, `ghostty_key_encoder_encode`; mouse equivalents `ghostty_mouse_encoder_*`.
- **Render state:** `ghostty_render_state_new`, `ghostty_render_state_update`, `ghostty_render_state_get`, `ghostty_render_state_row_iterator_new` / `_next`, `ghostty_render_state_row_cells_next` / `_get`, `ghostty_render_state_colors_get`.
- **Context dump:** `<ghostty/vt/formatter.h>` (plain-text / HTML output). **⚠ Confirm exact `ghostty_formatter_*` signatures against the pinned header in Phase 0** — if they differ or are absent at our pin, `term_engine_dump_text` falls back to render-state row/cell iteration (already proven by ghostling).
- **Effects:** `ghostty_focus_encode`, `ghostty_terminal_mode_get`.

### 4.4 PTY (`pty.c`)
Lifted from ghostling, kept behind a small API:
- `forkpty(&pty_fd, NULL, NULL, &ws)` — combines openpty + fork + login_tty.
- Parent: master fd set non-blocking via `fcntl(.. O_NONBLOCK)`.
- Child: `TERM=xterm-256color`, exec `$SHELL` → passwd entry → `/bin/sh` fallback.
- Resize: `ioctl(pty_fd, TIOCSWINSZ, &new_ws)` whenever the grid dimensions change.
- Read: drained each frame into a buffer, forwarded to `term_engine_write`.
- Write/inject: `write(pty_fd, bytes, len)` — used both for encoded keystrokes and for AI command injection (§8).

---

## 5. Configuration (`config.c`)

### 5.1 Location & precedence
- File: `~/.config/fangs/config` (INI). **Created by the app on first run if absent.** Not in the repo; user-owned.
- **API key precedence:** `FANGS_API_KEY` env var **wins**; the `[ai] api_key` file field is a fallback. This keeps the key out of the file by default and avoids accidental commits. The settings modal writes to the file only if the user explicitly enters a key there.

### 5.2 Schema (full example)
```ini
[terminal]
font_family = JetBrainsMono Nerd Font
font_size   = 14
theme       = dark            ; dark | light | <named>
scrollback  = 10000

[ai]
provider    = openai          ; openai | anthropic | ollama | custom (all OpenAI-compatible unless anthropic)
endpoint    = https://api.openai.com/v1/chat/completions
model       = gpt-4o-mini
api_key     =                 ; leave blank; prefer FANGS_API_KEY env var
stream      = true
max_tokens  = 1024
temperature = 0.2

[context]
capture_lines = 100           ; scrollback lines fed to the model
redact        = true          ; scrub secrets before sending (§7.3)

[prompts]
sidebar_system = You are a terminal assistant. Be concise.
inline_system  = Return ONLY the raw shell command. No prose, no markdown, no code fences.
```

### 5.3 Behavior
- Parsed into a single `AppConfig` struct at startup; a lightweight INI parser (vendored or ~150 LOC).
- `Ctrl+,` opens the RayGUI modal bound to `AppConfig` fields.
- **Hot reload:** "Save" writes the struct back to the INI and re-applies in place — font/theme via the renderer, AI fields by swapping the provider config. No restart. The modal indicates that the key field is written to disk (not git-ignored — it's outside the repo, in `~/.config`).

---

## 6. AI Provider & Networking (`ai_provider` / `ai_http.c`)

### 6.1 The seam (`ai_provider.h`)
```c
typedef struct { const char *role; const char *content; } AiMessage;     // role: system|user|assistant

// on_token is invoked from the worker thread for each streamed delta.
// on_done fires once at end (ok flag + optional error string).
typedef void (*AiTokenCb)(const char *delta, void *user);
typedef void (*AiDoneCb)(bool ok, const char *err, void *user);

typedef struct AiRequest AiRequest;
AiRequest *ai_send(const AiConfig *cfg,
                   const AiMessage *msgs, size_t n,
                   AiTokenCb on_token, AiDoneCb on_done, void *user);
void ai_cancel(AiRequest *);   // user hit Esc / closed sidebar
```

### 6.2 Threading model (the crux of "streaming in C")
- `ai_send` spawns **one detached worker `pthread`** per request.
- The worker runs a blocking `curl_easy_perform` with `CURLOPT_WRITEFUNCTION` set to our chunk handler.
- The chunk handler runs the **SSE line splitter** (§6.3), extracts deltas via cJSON, and pushes each delta string into a **mutex-guarded ring buffer** owned by the request.
- The main loop, each frame, drains the ring buffer (`on_token` equivalents) into UI state and redraws. **Immediate-mode rendering means streaming is just "redraw the growing buffer."**
- Cancellation: `ai_cancel` sets an atomic flag the write-callback checks; returning non-write-size aborts the transfer.
- No token is ever touched by two threads at once; the ring buffer's mutex is the only shared-state lock.

### 6.3 SSE parsing (OpenAI-compatible)
~40 lines, no library:
1. Append incoming chunk to a line-accumulator (chunks split mid-line).
2. For each complete `\n`-terminated line:
   - Ignore blanks and lines not starting with `data: `.
   - On `data: [DONE]` → signal completion.
   - Else parse the JSON after `data: ` with cJSON, read `choices[0].delta.content`, push to ring buffer.

### 6.4 Provider formats
- **OpenAI-compatible** (OpenAI, Ollama, and most "custom" endpoints): `POST {endpoint}` with `{model, messages, stream, max_tokens, temperature}`; `Authorization: Bearer <key>`. This is the baseline path.
- **Anthropic-native** (✅ shipped 2026-06-22): different envelope (`POST /v1/messages`, `x-api-key` + `anthropic-version: 2023-06-01`, top-level `system` string, required `max_tokens`, `content_block_delta` event shape with `text_delta`/`thinking_delta`). Selected via `cfg.provider == "anthropic"`; `ai_http.c` builds the body + headers and `sse.c` auto-detects the wire format by JSON shape (top-level string `type` → Anthropic; `choices` array → OpenAI). Both live behind the same `ai_provider` seam.
- JSON request bodies are **built with cJSON**, never string-concatenated (escaping correctness).
- **Reasoning models (verified with a hosted OpenAI-compatible reasoning model, Phase 0b):** deltas may carry `choices[0].delta.reasoning_content` (chain-of-thought) *before* `choices[0].delta.content` (the answer). The parser must read both; the sidebar renders "thinking" (dim/collapsible) distinctly from the answer. `ai_provider.h`'s token callback should tag each delta with its kind (`reasoning` vs `content`).

---

## 7. Context Extraction (`context.c`)

### 7.1 Source
- Primary: `term_engine_dump_text(engine, cfg.capture_lines)` → the formatter API serializes the last N rows (visible + scrollback) to plain UTF-8.
- Fallback: render-state row/cell iteration assembling text manually (proven path).

### 7.2 Assembly
The sidebar request messages are:
```
[ {system: prompts.sidebar_system},
  {user: "<recent terminal output>\n```\n" + dump + "\n```\n\n" + user_question} ]
```
Working directory and last exit status are included when cheaply available (shell-integration OSC if present; otherwise omitted — no shell hacks required for v1).

### 7.3 Redaction (when `context.redact = true`)
Before send, a regex pass scrubs obvious secrets from the dumped text: `sk-…`, `ghp_…`, AWS-key patterns, `Bearer <token>`, `PASSWORD=…`, and lines matching `*_KEY=`/`*_TOKEN=`/`*_SECRET=`. Best-effort, documented as such — not a guarantee.

### 7.4 Run action
When a streamed assistant message contains a fenced block, the sidebar renders a **Run** button. Clicking it calls `pty_write(pty_fd, command, len)` — the command appears at the live prompt, **unexecuted**. The user reviews and presses Enter. (Same injection primitive as §8.)

---

## 8. Inline Command Generation (`ui_inline.c`)

State machine — no interception of the real shell's stdin:

```
NORMAL ──Ctrl+Space──► INLINE_INPUT ──Enter──► AWAITING_AI ──token stream──► INJECTED ──► NORMAL
   ▲                        │  Esc                    │ error/cancel              │
   └────────────────────────┴─────────────────────────┴──────────────────────────┘
```

- **INLINE_INPUT:** `inline_mode = true`; a floating RayGUI text box is drawn near the cursor; the input router sends keys to the box, not the PTY.
- **AWAITING_AI:** the typed prompt + `prompts.inline_system` ("return ONLY the raw command") go through `ai_provider`. A spinner shows in the box.
- **INJECTED:** the (whitespace-trimmed, single-line) response is `write()`-en to the PTY fd as simulated keystrokes. It rests at the prompt. **We never send `\n`.** The user presses Enter.
- **Esc** at any point aborts (`ai_cancel` if in flight) and returns to NORMAL.

Guardrails: strip surrounding code fences/backticks if the model adds them; reject multi-line responses by taking only the first line (configurable later); never inject control characters beyond the command text.

---

## 9. Rendering & Input (`render.c`, input router in `main.c`)

- **Split layout:** terminal grid occupies the left ~75%; sidebar the right ~25%. The grid's cols/rows are computed from the *left region* width, not the full window — resize recalculates and calls `term_engine_resize` + `TIOCSWINSZ`.
- **Cell drawing:** per render-state row → per cell: draw background `DrawRectangle` if set, then `DrawTextEx` for the grapheme (bold = redraw offset; italic = shear/offset), using the configured Nerd Font atlas.
- **Input routing precedence each frame:** `inline_mode` box → focused sidebar input → terminal. Terminal keys are encoded via `term_engine_encode_key` (ghostty's encoder) and written to the PTY.
- **Known limitation (upstream):** Raylib's input system breaks some Kitty-keyboard-protocol inputs. Accepted for v1; documented; revisited only if it blocks real usage.

---

## 10. Security & Privacy

- **Local-first:** the only outbound network call is to the user-configured AI endpoint. No analytics, no phone-home, no account.
- **Key handling:** prefer `FANGS_API_KEY` env var; if stored in the INI, the file is `~/.config/fangs/config` (user-owned, mode `0600` enforced by the app on write). Never logged, never sent anywhere but the provider.
- **Context leaves the machine:** the sidebar deliberately sends terminal output to the provider. This is surfaced in the UI; `context.redact` mitigates obvious secrets; capture size is user-controlled.
- **No auto-exec:** injection stages text only (§7.4, §8).

---

## 11. Build & Run (Arch / CachyOS)

> Per the team's run-instruction convention: these are copy-pasteable, with the *why* and the
> committed/ignored status noted. Exact submodule/raylib fetch mechanics are **confirmed in Phase 0a**.

**System dependencies** (Zig is the non-obvious one — `libghostty-vt` builds with it):
```bash
sudo pacman -S --needed cmake ninja base-devel git curl   # Zig handled separately, see below
```
- `zig` must be **0.15.2** (`zig version`) — the pinned ghostty commit builds against 0.15.x. **Do not
  `pacman -S zig`:** Arch/CachyOS currently ships **0.16.0**, which won't build the pin. Download
  0.15.2 from [ziglang.org/download](https://ziglang.org/download/) (or `zigup`) and put it first on
  `PATH`. *Why:* libghostty-vt compiles via Zig at build time. (Verified on CachyOS x86-64, 2026-06-22.)

**Configure & build** (Raylib + libghostty-vt are pulled by the build; libghostty-vt is a pinned submodule):
```bash
git submodule update --init --recursive    # fetches pinned libghostty-vt
cmake -B build -G Ninja
cmake --build build
```

**Run:**
```bash
./build/fangs
```

**Config file** (created on first run; *not* in the repo — lives in your home, not git-ignored because it's outside the tree):
```bash
$EDITOR ~/.config/fangs/config
export FANGS_API_KEY=sk-...      # preferred over putting the key in the file
```
- Changing the config takes effect on **Save in the modal** (hot reload) or next launch for file edits. No rebuild needed for config changes; a rebuild is only needed after C source changes.

---

## 12. Milestones & Acceptance Criteria

| Phase | Deliverable | Acceptance test |
|---|---|---|
| **0a** | Pinned engine builds | `./build/fangs` opens a terminal window from a pinned `libghostty-vt` SHA on CachyOS |
| **0b** | Streaming AI spike | Tokens from one hardcoded prompt visibly stream into a Raylib window via worker thread + ring buffer |
| **1** ✅ | Daily-drivable terminal | **DONE** — `src/{main,pty,term_engine}.c` + root `CMakeLists.txt`; builds → `build/fangs`, runs clean (ghostty-vt + raylib/Cocoa/OpenGL). Engine behind the `term_engine` seam. Handoff: `docs/handoff-phase2.md` |
| **2** | Config + modal | Edit font/provider/model in file *and* in `Ctrl+,` modal; both hot-reload live |
| **3** | Split + sidebar UI | Chat panel renders beside a working terminal; scroll + input box functional (unwired) |
| **4** | Context-aware chat | Trigger an error on screen, ask the sidebar, get a streamed answer that references it; Run button stages a command |
| **5** | Inline generation | `Ctrl+Space` → "undo last git commit" → correct command staged at prompt, awaiting your Enter |

---

## 13. Risk Register

| ID | Risk | L | I | Mitigation | Owner phase |
|---|---|---|---|---|---|
| R1 | libghostty-vt API break on upgrade | H | M | Pin SHA; `term_engine` seam; deliberate upgrades | 0a, ongoing |
| R2 | Streaming/SSE/JSON in C friction (**retired** — proven 0b) | — | — | Validated vs an OpenAI-compatible endpoint; quarantined in `ai_http.c`; cJSON; worker+mutex buffer | 0b ✓ |
| R3 | Zig 0.15.2 ↔ macOS 26.5 SDK linker incompat (**realized → worked around**) | Low (resid.) | Low | `scripts/macos-build.sh` (hybrid SDK + `xcrun` shim); verified building. Drop shim when upstream Zig supports the macOS 26 SDK. | 0a ✓ |
| R4 | Formatter API differs at our pin | M | L | Confirm signatures in 0; render-state fallback | 0, 4 |
| R5 | Raylib kitty-keyboard gaps (upstream) | M | L | Accept for v1; document | 1 |
| R6 | Context leaks secrets to provider | M | M | Redaction pass; capture-size control; UI disclosure | 4 |

---

## 14. Open Questions

- ~~Exact pinned `libghostty-vt` commit SHA.~~ **Resolved:** ghostty @ `ae52f97dcac558735cfa916ea3965f247e5c6e9e` (§4.1).
- ~~How Raylib + libghostty-vt are fetched.~~ **Resolved:** CMake `FetchContent` (raylib 5.5 tag; ghostty repo at the pinned SHA, `zig build -Demit-lib-vt`).
- ~~macOS-native build blocked~~ **Resolved (workaround):** builds via `scripts/macos-build.sh` (hybrid SDK + `xcrun` shim, §4.1), verified on macOS 26.5 arm64. Long-term: drop the shim when upstream Zig handles the new SDK.
- Confirmed `ghostty_formatter_*` signatures vs. render-state fallback (needs a successful build to introspect the pinned header).
- ~~Anthropic-native messages support in v1, or OpenAI-compatible only first.~~ **Resolved (2026-06-22):** both ship — Anthropic-native `/v1/messages` and OpenAI-compatible, selected by the provider toggle.
- ~~Packaging target: AUR `PKGBUILD` (CachyOS) + `.app`/Homebrew (macOS) vs. `cmake --install`.~~ **Resolved (2026-06-22):** AUR `PKGBUILD` shipped + verified (`packaging/aur/`, `fangs-git`); macOS `.app`/Homebrew still TODO.
