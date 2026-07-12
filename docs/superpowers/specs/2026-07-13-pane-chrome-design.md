# Modern Pane Chrome — Design Spec

Date: 2026-07-13
Status: Draft — pending user approval

## Goal

Make Fangs' terminal area feel like a modern terminal (Ghostty/Warp/cmux mix) by adding refined pane chrome: rounded pane cards, a minimal title/status bar, softer split gutters, an improved block cursor, and a cleaner scrollbar. The work stays inside the existing immediate-mode Raylib render loop and keeps the current layout/split system intact.

## Scope

### In scope

1. **Rounded pane frame with shadow**
   - Each leaf pane renders inside a rounded rectangle (`DrawRectangleRounded`).
   - Subtle drop shadow drawn behind the pane (dark fill, low alpha, offset 2 px × scale) to create depth against the app background.
   - 1 px border around the pane in `panel_border`; the focused pane uses `accent` instead.
   - Terminal content is scissored to the inner rounded rect so no sharp corners show.

2. **Pane title/status bar**
   - A thin header strip at the top of every pane: 24 px × scale tall.
   - Background is `panel_bg` slightly lifted toward `panel_border`.
   - Content (left to right, 8 px × scale padding):
     - Small status dot: green if process running, amber if idle/unseen output, red if last exit non-zero.
     - Session name or `cwd` basename (clipped with ellipsis).
     - Branch name (if available and there is room), dimmed.
   - The bar is only drawn when there is enough vertical space (pane height ≥ 48 px × scale); otherwise it is hidden so tiny panes stay readable.

3. **Soft split gutters**
   - Increase the inter-pane gap from 2 px to 6 px × scale.
   - The gap is filled with `panel_bg` (the same color as the app background behind panes), making splits read as negative space rather than hard lines.
   - On hover inside a gutter, draw a 2 px `accent` drag-handle line to hint that the split is resizable.

4. **Block cursor polish**
   - Add a 1 px outline to the block cursor in `accent` when focused.
   - Cursor alpha stays theme-driven but defaults to a slightly more visible value.
   - Bar/underline cursor styles keep the existing shape but get the same outline.

5. **Scrollbar refresh**
   - Rounded thumb (`DrawRectangleRounded`).
   - Thumb fades in on scroll activity and fades out after ~1.5 s of no scroll, driven by `frame_dt_sec`.
   - Width reduced from 6 px to 5 px × scale; margin from 2 px to 4 px × scale.

6. **Command-block card chrome**
   - The action bar inside `cmdblocks_draw` gets a 1 px rounded border and a very subtle inset shadow so each command block feels like a card sitting on top of the terminal.

### Out of scope

- Adding a tab strip or top bar.
- Introducing an icon font or image assets.
- Changing how splits are created, resized, or focused.
- Re-theming the workspace rail or modals (already handled in previous phases).

## Visual references

- **Ghostty**: pane frames with rounded corners, thin borders, and soft gutters.
- **Warp**: block-based command cards floating above terminal content.
- **cmux**: minimal per-pane status bar showing cwd/branch and process state.

## Architecture

The changes are localized to the render loop in `src/main.c` and a small derived-theme update in `src/ui_theme.c`:

1. `layout.c` gets a configurable `gap` so pane spacing is no longer hardcoded to 2 px.
2. `ui_theme.c` derives a few new semantic tokens (`pane_header_bg`, `pane_header_text`, `pane_status_running`, `pane_status_idle`, `pane_status_error`, `shadow`, `gutter_hover`) so colors stay theme-aware.
3. `main.c` wraps each pane render in a helper (`draw_pane_chrome`) that:
   - draws shadow,
   - draws rounded frame + border,
   - draws the title bar,
   - sets the scissor rect to the inner rounded rect,
   - calls `render_terminal`,
   - draws the scrollbar,
   - draws the focused border if needed.
4. A new gutter render pass walks the pane tree and draws hover hints for internal split nodes.
5. `cmdblocks_draw` uses the new `panel_bg`/`panel_border` tokens for its action bar.

## Components

### 1. Layout gap (`src/layout.{c,h}`)

Current: `const int gap = 2;` hardcoded inside `compute_panes_rec`.

Changes:
- Add a `pane_gap` parameter to `layout_compute_panes` (default callers can pass 6 × scale or 0 for no change).
- Keep the old behavior for tests and any callers that don't care by adding a wrapper `layout_compute_panes_with_gap`.

### 2. Derived theme tokens (`src/ui_theme.{c,h}`)

Add to `UiTheme`:
```c
UiColor pane_header_bg;       // title bar background
UiColor pane_header_text;     // session/cwd text
UiColor pane_header_detail;   // branch / dim detail
UiColor pane_status_running;  // green dot
UiColor pane_status_idle;     // amber dot
UiColor pane_status_error;    // red dot
UiColor shadow;               // drop shadow fill
UiColor gutter_hover;         // resize hint line
```

Derived as blends of `bg`/`fg`/`ansi` for both dark and light paths.

### 3. Pane chrome helper (`src/main.c`)

A new static function:
```c
static void draw_pane_chrome_and_content(PaneNode *leaf, int px, int py, int pw, int ph,
                                         bool focused, float scale, double dt,
                                         ... terminal render args ...);
```

Responsibilities:
- Compute inner rect: `ix = px + 1, iy = py + header_h + 1, iw = pw - 2, ih = ph - header_h - 2`.
- If `header_h > 0`, draw the header strip.
- Draw shadow behind `px,py,pw,ph` using `shadow` color.
- Draw rounded frame (`DrawRectangleRounded`) with border color; focused uses `accent` at 220 alpha.
- Begin scissor on the inner rounded rectangle.
- Call `render_terminal` with `origin_x = ix`, `origin_y = iy`, and `term_area_w = pw`.
- Draw scrollbar using the new rounded/fading style.
- End scissor.

### 4. Title bar content

Status dot mapping:
- `pane_status_running` if session has a running foreground process.
- `pane_status_error` if the last recorded process exited non-zero and the pane hasn't been focused since.
- `pane_status_idle` otherwise (idle with unseen output) — fallback to amber when unseen output exists.

Text content:
- Primary: basename of the pane's `cwd` (via `session_cwd` or equivalent accessor); no leaf `name` field exists in `PaneNode`.
- Detail: `cached_git_branch` for the pane if non-empty and width allows.

### 5. Gutter hover hints

After all panes are rendered, derive internal split edges from adjacent leaf rects in the collector (shared vertical/horizontal edges with a 6 px gap between them). For each edge, if the mouse is within 4 px × scale of the edge midpoint, draw a short `accent` line segment centered on that edge.

### 6. Scrollbar animation state

Global `float g_scrollbar_alpha[64]` indexed by pane, or simpler: store `uint64_t g_scrollbar_last_scroll_ms[64]` and compute fade each frame.
- On any scroll event or when `scrollbar->offset` changes, set `last_scroll_ms = now_ms`.
- Alpha = `clamp((now_ms - last_scroll_ms) / 1500.0, 0, 1)` inverted (1 → 0).

### 7. Cursor outline

In `render_terminal`, after drawing the block/bar/underline cursor, draw a 1 px outline using `accent` when focused.

### 8. Command block chrome

In `cmdblocks_draw`, replace the flat action-bar background with a rounded rectangle using `panel_bg` and a 1 px border in `panel_border`. Add a subtle bottom shadow by drawing a darker rounded rect offset 1 px down behind it.

## Data flow

1. `main.c` computes layout with the new gap (6 px × scale).
2. For each leaf, it calls `draw_pane_chrome_and_content`, which draws chrome, sets scissor, renders terminal content, draws scrollbar, and ends scissor.
3. A second pass draws gutter hover hints.
4. `cmdblocks_draw` uses the new tokens for its floating action bar.

## Validation

- `bash scripts/macos-build.sh` succeeds.
- `ctest --test-dir build --output-on-failure` passes all 36 tests.
- `./build/fangs` launches and shows rounded pane frames, title bars, and softer gutters.
- Headless smoke tests write updated screenshots if the smoke harness already captures pane output.
- Split resize still works; focus border highlights the active pane; cursor has outline in focused pane.

## Risks / notes

- Rounded scissor rectangles are not natively supported by Raylib; we will use a rectangular scissor slightly inset so the rounded corners don't clip content awkwardly. The frame border already defines the rounded edge.
- Title bars consume vertical space, so grid row counts for each pane decrease by `header_h / cell_height`. We must recalculate `lterm_rows` from the inner height, not the full pane height.
- Multiple-pane layouts with small heights must hide the title bar to avoid unreadable terminals.
- The drop shadow adds overdraw; performance impact should be negligible on modern hardware but worth checking with many split panes.
