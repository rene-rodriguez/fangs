# Phase 3 Handoff — Split Layout & the Chat Sidebar (UI only)

> **Audience:** whoever executes Phase 3 (you, or a fresh agent session).
> **Prereq:** Phase 2 is done — config + `Ctrl+,` settings modal work, tests pass, builds
> warning-clean. This doc is self-contained; `docs/plan.md` §6.B/§7 has the broader picture.
> **Date:** 2026-06-21.

---

## 1. Where Phase 2 left things

```
nova-terminal/
├── CMakeLists.txt          # nova-terminal target + config_tests (ctest)
├── src/
│   ├── main.c              # window, font, input handlers, render, effects, frame loop
│   ├── pty.{c,h}           # forkpty plumbing (engine-agnostic sink callback)
│   ├── term_engine.{c,h}   # THE SEAM: libghostty-vt handles + dump_text()
│   ├── config.{c,h}        # INI ↔ AppConfig (0600, env-key precedence)
│   ├── ui_settings.{c,h}   # Ctrl+, RayGUI modal (defines RAYGUI_IMPLEMENTATION)
│   └── raygui.h            # vendored single-header (impl lives in ui_settings.c ONLY)
└── tests/config_tests.c    # 4 cases, wired into ctest
```

**Build & iterate**
- First/clean build (macOS): `bash scripts/macos-build.sh` → `build/nova-terminal`.
- **Iterating on `src/` changes: `cmake --build build`** — incremental, clang-only, ~seconds,
  does *not* rebuild libghostty-vt and does *not* need the Zig shim. Use this constantly.
- Tests: `ctest --test-dir build --output-on-failure`.
- Run: `./build/nova-terminal`.

**Two things from Phase 2 you'll build on directly**
- The **shortcut-interception pattern** (`src/main.c` ~line 1362): check the modifier+key,
  call a `*_toggle()`, set a `*_consumed` flag, drain `GetCharPressed()`, and gate
  `handle_input` on it. Phase 3's sidebar toggle copies this verbatim.
- The **draft-then-commit modal pattern** in `ui_settings.c`. The sidebar is *not* modal
  (see the focus model below) but the RayGUI mechanics (panel, text box edit-mode bool,
  buttons) are the same toolkit.

---

## 2. Phase 3 goal & exit criterion

Split the window into a left terminal region and a right chat panel. The panel is **UI only** —
no AI yet (that's Phase 4). It renders a scrollable message history and an input box; submitting
appends the typed text to the history, clearly labelled as not-yet-wired.

**Exit test (all four):**
1. Toggle the sidebar on/off with the chord; the terminal **re-flows** to the narrower/wider
   width each time (run `htop` or `vim` and watch it reflow — no garbage, no overdraw into the panel).
2. With the sidebar **visible but not focused**, the terminal still types normally.
3. Click the sidebar input, type a prompt, press Enter → it appears in the history above a
   system line like `(AI not wired yet — Phase 4)`. Keys did **not** leak to the shell.
4. Resize the window and hot-reload font size (`Ctrl+,` → change size → Save): the split, the
   grid, and the panel all stay consistent.

---

## 3. The one architectural idea (read this first)

The terminal today reads `GetScreenWidth()/GetScreenHeight()` directly in four functions. Phase 3
puts a chat panel on the **right**, so:

> **The terminal stays anchored top-left at `pad`. Only its available *width* shrinks.**
> Height stays the full window height. The terminal origin never moves.

That collapses the whole phase into: **compute one number — `term_area_w` (window width minus the
sidebar) — and thread it into the four terminal-path functions, replacing their `GetScreenWidth()`
reads.** The `GetScreenHeight()` reads stay as-is. No general-rectangle rewrite needed.

The four call sites (verified against current `src/main.c`):

| Function | Line(s) of `GetScreenWidth()` | Change |
|---|---|---|
| `compute_terminal_grid` (45) | 48 | cols = `(term_area_w - 2*pad)/cell_w`; height read stays |
| `handle_mouse` (244) | 255 | map clicks against `term_area_w`; also early-return if `GetMouseX() >= term_area_w` |
| `handle_scrollbar` (493) | 509 | scrollbar geometry against `term_area_w` (its right edge) |
| `render_terminal` (719) | 910 | draw the scrollbar at the right edge of `term_area_w`, not the window |

Add an `int term_area_w` parameter to each; pass it from the frame loop. (Height params can stay
implicit via `GetScreenHeight()` — only width changes.)

---

## 4. Work breakdown

### 4a. `src/layout.{c,h}` — a pure, testable layout function
Keep the geometry in one **arithmetic-only** function (no raylib calls) so it unit-tests cleanly,
exactly like `config.c`:
```c
// layout.h
typedef struct { int x, y, w, h; } Rect;     // plain ints → test links nothing from raylib
typedef struct {
    Rect terminal;        // grid draws here (anchored top-left)
    Rect sidebar;         // chat panel; w == 0 when hidden
    bool sidebar_visible;
} Layout;

// Pure: window size + sidebar state → the two rects. Clamps the sidebar so the
// terminal keeps at least min_terminal_w px; hides the sidebar entirely if the
// window is too narrow to honor that.
Layout layout_compute(int window_w, int window_h, bool sidebar_visible,
                      int sidebar_width, int pad, int min_terminal_w);
```
Recommended defaults: `sidebar_width = 380`, `min_terminal_w = 320`. (Plan says "~75/25"; a fixed
panel width gives a steadier reading column than a ratio — but a `clamp(0.30*window_w, 320, 480)`
ratio is fine too. Pick one; it's a one-liner to change.)

`term_area_w` for §3 is just `layout.terminal.w`.

### 4b. Thread `term_area_w` through the terminal path
Mechanical: add the param to the four functions in §3, replace their `GetScreenWidth()` with it,
and pass `layout.terminal.w` at the call sites in `main()`. Update `apply_config` (it calls
`compute_terminal_grid` at ~1139) and the resize handler (~1296) to pass `term_area_w` too — both
must recompute the grid from the *terminal area*, not the window, or the font-reload/resize paths
will fight the split.

### 4c. `src/ui_sidebar.{c,h}` — the chat panel
```c
typedef enum { MSG_USER, MSG_ASSISTANT, MSG_SYSTEM } MsgRole;

void  ui_sidebar_toggle(void);
bool  ui_sidebar_visible(void);
bool  ui_sidebar_focused(void);          // input box currently capturing keys
void  ui_sidebar_focus(bool on);         // host sets focus on toggle-open / click-in-terminal

// Draw the panel in `bounds`. If the user submits this frame, returns true and
// copies the entered text into out_prompt (caller-owned buffer); clears the input.
bool  ui_sidebar_draw(Rect bounds, char *out_prompt, int out_prompt_size);

// Append a message to the scrollback history (host calls this; Phase 4 also calls
// it as assistant tokens stream in).
void  ui_sidebar_push(MsgRole role, const char *text);
```
Internals (keep it modest for Phase 3):
- **History store:** a growable array (or fixed cap ~256) of `{role, char text[1024]}`. On submit,
  `ui_sidebar_push(MSG_USER, prompt)` then `ui_sidebar_push(MSG_SYSTEM, "(AI not wired yet — Phase 4)")`.
- **History render:** `BeginScissorMode` over the history region; draw each message word-wrapped to
  the panel width (a small wrap helper: walk words, `MeasureTextEx`, break when the line would
  overflow), accumulating `y`. Offset all `y` by a `scroll_offset` driven by `GetMouseWheelMove()`
  when the mouse is over the panel; clamp so you can't scroll past the ends. Colour user vs system
  lines differently. End scissor.
- **Input row:** a `GuiTextBox` at the bottom + a `GuiButton("Send")`. Submit on Send **or** Enter
  while focused. ⚠️ Verify `GuiTextBox`'s return/Enter semantics against the vendored `raygui.h`
  (same edit-mode-bool caution as Phase 2) — if Enter isn't reported, detect `IsKeyPressed(KEY_ENTER)`
  while focused instead.
- **Do NOT** `#define RAYGUI_IMPLEMENTATION` here — it's already defined in `ui_settings.c`.
  Just `#include "raylib.h"` then `#include "raygui.h"`. (Two implementations → duplicate-symbol
  link error.)

### 4d. Integrate in `src/main.c`
1. **Per-frame layout** (top of the loop, after the resize handler updates window size):
   ```c
   Layout lo = layout_compute(GetScreenWidth(), GetScreenHeight(),
                              ui_sidebar_visible(), 380, pad, 320);
   int term_area_w = lo.terminal.w;
   ```
2. **Toggle chord** — copy the Phase 2 interception (before any input forwarding). Use a chord that
   does **not** collide with terminal control codes (a bare `Ctrl+letter` is a real keystroke apps
   need). Recommended: **`Cmd+B` on macOS / `Ctrl+Shift+B` on Linux**:
   ```c
   bool sidebar_chord = IsKeyPressed(KEY_B) &&
       ((IsKeyDown(KEY_LEFT_SUPER)||IsKeyDown(KEY_RIGHT_SUPER)) ||
        ((IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)) &&
         (IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT))));
   if (sidebar_chord) { ui_sidebar_toggle(); ui_sidebar_focus(ui_sidebar_visible()); /*drain GetCharPressed()*/ }
   ```
   Opening focuses the input so you can type immediately; closing drops focus.
3. **Focus model — the subtle part.** Visibility ≠ blocking. The sidebar gates PTY input **only
   when focused**:
   ```c
   bool sidebar_capturing = ui_sidebar_visible() && ui_sidebar_focused();
   if (!child_exited && !ui_settings_open() && !sidebar_capturing && !sidebar_chord && !settings_shortcut_consumed) {
       handle_input(pty_fd, key_encoder, key_event, terminal);
       if (!scrollbar_consumed && GetMouseX() < term_area_w)   // don't forward clicks in the panel
           handle_mouse(pty_fd, mouse_encoder, mouse_event, terminal, cell_width, cell_height, pad, term_area_w);
   }
   ```
   - A click inside the terminal area (`GetMouseX() < term_area_w`) should call `ui_sidebar_focus(false)`
     (return focus to the terminal). A click in the input box focuses it (RayGUI edit-mode + the host
     setting `ui_sidebar_focus(true)`).
   - `Esc` while the sidebar is focused → `ui_sidebar_focus(false)` (back to terminal). This doesn't
     clash with terminal `Esc` because the terminal isn't capturing keys while the sidebar is focused.
4. **Draw order** inside `BeginDrawing()`:
   ```c
   ClearBackground(win_bg);
   BeginScissorMode(lo.terminal.x, lo.terminal.y, lo.terminal.w, lo.terminal.h);
   render_terminal(..., term_area_w, ...);          // scissor stops any bleed during reflow
   EndScissorMode();
   if (ui_sidebar_visible()) {
       // divider + panel bg, then the panel
       DrawLine(lo.sidebar.x, 0, lo.sidebar.x, lo.sidebar.h, (Color){60,60,60,255});
       char submitted[1024];
       if (ui_sidebar_draw(lo.sidebar, submitted, sizeof submitted)) {
           ui_sidebar_push(MSG_USER, submitted);
           ui_sidebar_push(MSG_SYSTEM, "(AI not wired yet — Phase 4)");
       }
   }
   // ... exit banner ...
   if (ui_settings_open()) ui_settings_draw(&cfg, &saved);   // modal still on top of everything
   ```
   The settings modal stays the top layer and remains fully blocking (unchanged).

### 4e. Persist sidebar state (optional, low cost)
The config infra exists, so it's cheap to round-trip the panel preference. Add to `AppConfig` a
`[ui]` section: `bool sidebar_visible; int sidebar_width;`. Extend `apply_key_value`,
`config_defaults`, and `config_save` (mirror the existing fields — and add a `config_tests` case,
matching the discipline already there). If you'd rather keep Phase 3 tight, skip this and default
the sidebar to hidden at startup.

### 4f. Tests
- `tests/layout_tests.c` + a `layout_tests` ctest target (mirror `config_tests` in `CMakeLists.txt`).
  Because `layout_compute` is pure arithmetic, the test links only `src/layout.c` — no raylib.
  Cover: hidden → terminal == full window; visible → `terminal.w + sidebar.w (+divider) == window_w`;
  narrow window clamps/hides the sidebar (terminal keeps `min_terminal_w`); font/grid math is
  unaffected by the split (cols from `terminal.w`, not window width).
- Add `src/ui_sidebar.c` and `src/layout.c` to `add_executable(nova-terminal ...)`.

---

## 5. Gotchas (read before coding)

- **`RAYGUI_IMPLEMENTATION` is already defined** (in `ui_settings.c`). Defining it again in
  `ui_sidebar.c` → duplicate symbols at link. Include the header without the macro.
- **Sidebar visible ≠ input blocked.** The whole point of the focus model: with the panel open you
  must still be able to type in the shell. Gate PTY input on `ui_sidebar_focused()`, not
  `ui_sidebar_visible()`. (Contrast the Phase 2 modal, which blocks on *open*.)
- **Mouse clicks in the panel must not reach the terminal.** Gate `handle_mouse` on
  `GetMouseX() < term_area_w`, and have `handle_scrollbar` (line 493) use `term_area_w` for its
  hit region too — otherwise a click near the panel's left edge registers as a scrollbar drag.
- **Scissor the terminal.** Without `BeginScissorMode` over `lo.terminal`, a mid-resize frame (or a
  wide glyph at the boundary) can overdraw into the panel. Cheap insurance.
- **Recompute grid in three places, consistently:** the resize handler (~1296), `apply_config`
  (~1139), and the per-frame layout must all derive cols/rows from `term_area_w`. Miss one and the
  toggle/resize/font-reload paths disagree about the grid width.
- **Don't reflow on every frame.** Only call `term_engine_resize` + `pty_set_winsize` when
  `term_area_w` (or rows) actually changes — i.e. on toggle and on window resize. Resizing the PTY
  every frame spams `SIGWINCH` and thrashes TUIs. Track a `prev_term_area_w` like the existing
  `prev_width/prev_height`.
- **Word-wrap is manual** in raylib — there's no wrapped-text primitive. Keep the wrap helper
  simple (break on spaces; hard-break tokens longer than the panel). Good enough for Phase 3.
- **DPI:** reuse the existing `GetWindowScaleDPI()` pattern for any new text sizing; don't hand-roll
  pixel sizes or you'll reintroduce blur / mismatch the terminal font.

---

## 6. Acceptance checklist
- [ ] Chord toggles the sidebar; terminal reflows to the new width (verify with `htop`/`vim`).
- [ ] Sidebar visible + unfocused → typing still goes to the shell.
- [ ] Click input, type, Enter → message shows in history with the "(AI not wired — Phase 4)" line;
      nothing leaked to the shell.
- [ ] Clicking back in the terminal returns focus (typing hits the shell again); `Esc` in the input
      also returns focus without closing the terminal.
- [ ] History scrolls with the mouse wheel and clips to the panel (no overdraw into the terminal).
- [ ] Window resize + `Ctrl+,` font-size change keep the split, grid, and panel consistent.
- [ ] `ctest` green (config + layout); `cmake --build build` warning-clean; `vim`/`htop` run fine.

## 7. After Phase 3
Phase 4 wires the AI: promote `spike/ai_stream/` into `src/ai_http.c` behind a new
`src/ai_provider.h` seam (`send(messages, on_token)`), add `src/context.c` to dump on-screen text
via `term_engine_dump_text()` (already built) with a redaction pass, and stream assistant tokens
into the sidebar via `ui_sidebar_push(MSG_ASSISTANT, ...)` — which is exactly why the panel's push
API exists now. Responses containing a fenced command get a **Run** button that injects bytes into
the PTY (staged at the prompt, never auto-executed). See `plan.md` §6.B and §7.
