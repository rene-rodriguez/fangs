# Phase 2 Handoff — Configuration & the GUI Settings Modal

> **Audience:** whoever executes Phase 2 (you, or a fresh agent session).
> **Prereq:** Phase 1 is done — a working terminal builds + runs. This doc is
> self-contained; read `docs/spec.md` §5 (config) + §12 for the broader picture.
> **Date:** 2026-06-20.

---

## 1. Where Phase 1 left things

```
nova-terminal/
├── CMakeLists.txt          # canonical build (raylib 5.5 + ghostty ae52f97 via FetchContent)
├── assets/JetBrainsMono-Regular.ttf
├── cmake/bin2header.cmake  # embeds the font as a C header
├── scripts/macos-build.sh  # macOS build (hybrid-SDK + xcrun shim workaround)
├── src/
│   ├── main.c              # window, font, input handlers, render, effects, frame loop
│   ├── pty.{c,h}           # forkpty plumbing (engine-agnostic, sink callback)
│   └── term_engine.{c,h}   # THE SEAM: owns all libghostty-vt handles + dump_text()
└── vendor/                 # gitignored: zig 0.15.2, ghostling (reference), shims, hybrid SDK
```

**Build & iterate**
- First/clean build (macOS): `bash scripts/macos-build.sh` → `build/nova-terminal`.
- **Iterating on `src/` changes: `cmake --build build`** — incremental, ~seconds, does *not*
  rebuild libghostty-vt and does *not* need the Zig shim (only clang runs). Use this constantly.
- Run: `./build/nova-terminal`.
- Linux/CachyOS (unverified, expected clean): `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build` with Zig 0.15.2 on PATH.

**The seam contract you'll build on** (`src/term_engine.h`): the engine owns the
handles; the host borrows them. You will NOT touch libghostty-vt in Phase 2 — config
and UI are pure host concerns.

---

## 2. Phase 2 goal & exit criterion

A `~/.config/nova-terminal/config` INI is the source of truth for terminal + AI settings;
`Ctrl+,` opens a RayGUI modal to edit them; **Save** writes the file back and hot-reloads
live (no restart).

**Exit test:** change `font_size` and `model` both (a) by editing the file then relaunching,
and (b) in the `Ctrl+,` modal with Save — both take effect. The AI fields are stored now
but not yet *used* (that's Phase 4); proving they round-trip file↔struct↔GUI is enough.

---

## 3. Work breakdown

### 3a. Vendor RayGUI (not present yet)
RayGUI is a single header, separate from raylib. Add it:
```bash
curl -fsSL -o src/raygui.h https://raw.githubusercontent.com/raysan5/raygui/master/src/raygui.h
```
In exactly one `.c` (use `src/ui_settings.c`), define the implementation before including:
```c
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
```
No CMake change needed beyond adding `src/ui_settings.c` to the `add_executable(nova-terminal ...)` list.

### 3b. `src/config.{c,h}` — the INI + AppConfig
Define `AppConfig` and load/save it. Schema is in `spec.md` §5.2; minimum for Phase 2:
```c
typedef struct {
    // [terminal]
    char  font_family[128];   // informational for now; v1 hot-reloads size only (see gotchas)
    int   font_size;          // default 16
    char  theme[32];          // "dark" | "light" (wire to bg/fg later)
    int   scrollback;         // default 1000  (note: term_engine hardcodes 1000 today)
    // [ai]  — stored, not used until Phase 4
    char  provider[32];
    char  endpoint[256];
    char  model[128];
    char  api_key[256];       // usually empty; env NOVA_API_KEY wins (see below)
    bool  stream;
    int   max_tokens;
} AppConfig;

void config_defaults(AppConfig *c);
bool config_load(AppConfig *c, const char *path);   // creates file w/ defaults if absent
bool config_save(const AppConfig *c, const char *path);
const char *config_default_path(void);  // "$HOME/.config/nova-terminal/config" (mkdir -p the dir)
```
- **API key precedence:** at the point of use (Phase 4), `getenv("NOVA_API_KEY")` wins;
  `c->api_key` is the fallback. In Phase 2 just store/round-trip it; if the env var is set,
  grey the field out in the modal and show "(from env)".
- INI parsing: a ~120-line hand-rolled parser is fine (`[section]` + `key = value`, `;`/`#`
  comments, trim). Don't pull a dependency. Write the file with `0600` perms (it can hold a key).

### 3c. `src/ui_settings.{c,h}` — the modal
```c
// Returns true while the modal is open (host should suppress PTY input meanwhile).
bool ui_settings_open(void);
void ui_settings_toggle(void);
// Draw + handle the modal for this frame. Sets *out_saved=true on the frame Save is clicked.
// On save, *cfg holds the edited values (host then persists + hot-reloads).
void ui_settings_draw(AppConfig *cfg, bool *out_saved);
```
Use RayGUI widgets: `GuiPanel`, `GuiLabel`, `GuiTextBox` (font_family, endpoint, model, api_key),
`GuiSpinner`/`GuiValueBox` (font_size, max_tokens), `GuiToggleGroup` (theme, provider),
`GuiCheckBox` (stream), `GuiButton` ("Save", "Cancel"). Draw a dimmed full-screen rect behind
the panel. `GuiTextBox` needs an "edit mode" bool per field (click to focus) — keep a small
static array of those.

### 3d. Integrate in `src/main.c`
1. After `te` is created and config loaded, keep an `AppConfig cfg;` in `main()`.
2. **Intercept `Ctrl+,` before PTY input.** In the loop, before `handle_input(...)`:
   ```c
   if (IsKeyPressed(KEY_COMMA) && (IsKeyDown(KEY_LEFT_SUPER)||IsKeyDown(KEY_LEFT_CONTROL)))
       ui_settings_toggle();
   ```
   (Pick Super on macOS / Ctrl on Linux, or accept both.)
3. **Gate PTY input while the modal is open** so typing edits fields, not the shell:
   ```c
   if (!child_exited && !ui_settings_open()) {
       handle_input(...); if (!scrollbar_consumed) handle_mouse(...);
   }
   ```
4. **Draw the modal** inside `BeginDrawing()`, after `render_terminal(...)`, before `EndDrawing()`:
   ```c
   if (ui_settings_open()) {
       bool saved = false;
       ui_settings_draw(&cfg, &saved);
       if (saved) { config_save(&cfg, config_default_path()); apply_config(&cfg, ...); }
   }
   ```

### 3e. Hot-reload (`apply_config`)
- **font_size** (the meaningful one): `UnloadFont(mono_font)` → reload from the embedded TTF at
  the new `font_size * dpi_scale`, re-`MeasureTextEx("M")` to recompute `cell_width/height`,
  then recompute `term_cols/rows`, call `term_engine_resize(te, ...)` + `pty_set_winsize(...)`.
  Factor the existing font-load + cell-measure code from `main()` into a helper so both startup
  and reload call it.
- **theme**: for v1, map "dark"/"light" to a window-bg fallback; full palette theming is a stretch.
- **AI fields**: nothing to apply yet (Phase 4 reads `cfg`).

---

## 4. Gotchas (read before coding)

- **`Ctrl+,` vs the comma key:** the comma is a normal terminal key. You MUST check the modifier
  and consume the event before `handle_input` forwards it to the PTY, or apps will receive a stray comma.
- **Modal must eat keyboard input:** while open, do not call `handle_input` (step 3c above), else
  keystrokes go to both the shell and the text boxes.
- **Font family is not trivial:** the font is *embedded* (`assets/…ttf` → C header). Honoring an
  arbitrary `font_family` means loading a TTF from a filesystem path at runtime (`LoadFontFromMemory`
  → `LoadFontEx(path,…)`). **Recommendation: Phase 2 hot-reloads `font_size` only**; treat
  `font_family` as stored-but-not-applied, and make loading from a path a clearly-scoped stretch goal.
- **`scrollback` is currently hardcoded to 1000** in `term_engine_create`. To make it configurable,
  add a param to `term_engine_create` (the seam already owns this — clean place to extend).
- **RayGUI + raylib 5.5:** master raygui targets raylib 5.x; if you hit an API mismatch, pin raygui
  to a 4.0 release tag instead of master.
- **DPI:** cell math divides `MeasureTextEx` by `GetWindowScaleDPI()`. Reuse the existing pattern;
  don't reintroduce blur by skipping it.

---

## 5. Acceptance checklist
- [ ] `~/.config/nova-terminal/config` is created with defaults on first run.
- [ ] Editing `font_size` in the file then relaunching changes the glyph size + grid.
- [ ] `Ctrl+,` opens the modal; typing edits fields and does NOT leak to the shell.
- [ ] Changing `font_size` + clicking Save resizes the live grid with no restart.
- [ ] Changing `model`/`endpoint` + Save round-trips to the file (verify by reopening).
- [ ] `cmake --build build` stays warning-clean; `./build/nova-terminal` runs `vim`/`htop` fine.

## 6. After Phase 2
Phase 3 = split layout + sidebar UI (75/25). Phase 4 = wire `ai_http.c` (the proven
`spike/ai_stream/` code) behind `ai_provider.h`, using `term_engine_dump_text()` for context
and `cfg` for provider/model/key. Phase 5 = inline `Ctrl+Space` generation. See `plan.md` §7.
