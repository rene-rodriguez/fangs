# Modern Pane Chrome — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add rounded pane frames, title bars, soft gutters, cursor outlines, and a refined scrollbar to the terminal area.

**Architecture:** Extend the immediate-mode render loop in `main.c` with a pane-chrome wrapper, derive new semantic colors in `ui_theme.c`, make pane spacing configurable in `layout.c`, and polish the cursor/scrollbar/command-block visuals in place.

**Tech Stack:** C, Raylib, libghostty-vt, CMake, custom layout/theming modules.

---

### Task 1: Make pane spacing configurable in layout

**Files:**
- Modify: `src/layout.h`
- Modify: `src/layout.c`
- Test: `tests/layout_tests.c`

**Context:** `layout.c` currently hardcodes a 2-pixel gap between panes. The spec needs a 6 px × scale gap.

- [ ] **Step 1: Add `pane_gap` parameter to the public API**

In `src/layout.h`, change:
```c
void layout_compute_panes(const PaneNode *root,
                          int term_x, int term_y, int term_w, int term_h,
                          PaneRectFn cb, void *user);
```
to:
```c
void layout_compute_panes(const PaneNode *root,
                          int term_x, int term_y, int term_w, int term_h,
                          int pane_gap,
                          PaneRectFn cb, void *user);
```

- [ ] **Step 2: Use the parameter instead of the hardcoded gap**

In `src/layout.c`, replace `const int gap = 2;` with the `pane_gap` parameter inside `compute_panes_rec`. Pass it through recursive calls.

The function signature becomes:
```c
static void compute_panes_rec(const PaneNode *n,
                              int x, int y, int w, int h,
                              int pane_gap,
                              PaneRectFn cb, void *user)
```

Update all arithmetic that used `gap` to use `pane_gap`.

- [ ] **Step 3: Update all callers of `layout_compute_panes`**

Find every call site with `grep -n "layout_compute_panes" src/main.c src/pane.c`.

For each call in `src/main.c`, pass the computed gap value. The gap is `6.0f * scale` clamped to an int ≥ 0 and ≤ 32. Use `int pane_gap = (int)(6.0f * scale);` near where `pad` is computed.

`src/pane.c` uses `layout_compute_panes` for directional focus moves (via a callback). Pass `0` there to preserve the existing behavior, or pass `pane_gap` if the caller stores it. The simplest path: pass `0` to keep focus-move math unchanged.

- [ ] **Step 4: Add a layout test for non-zero gap**

In `tests/layout_tests.c`, add:
```c
static void test_pane_gap_reduces_child_size(void)
{
    // Simple HSPLIT root at 50% with a 6 px gap.
    // We need a small pane tree. Create two leaves and a split node.
    Session s1 = {0}, s2 = {0};  // dummy sessions, only addresses matter
    PaneNode left = { .kind = PANE_LEAF, .leaf.session = &s1 };
    PaneNode right = { .kind = PANE_LEAF, .leaf.session = &s2 };
    PaneNode root = { .kind = PANE_HSPLIT, .split = { .left = &left, .right = &right, .ratio = 0.5f } };

    int out_x[2], out_y[2], out_w[2], out_h[2];
    int n = 0;
    layout_compute_panes(&root, 0, 0, 100, 100, 6,
                         [](const PaneNode *n, int x, int y, int w, int h, void *user) {
                             int *idx = (int *)user;
                             out_x[*idx] = x; out_y[*idx] = y;
                             out_w[*idx] = w; out_h[*idx] = h;
                             (*idx)++;
                         }, &n);

    // total width = left_w + 6 + right_w = 100
    EXPECT_INT(out_w[0] + out_w[1] + 6, 100);
    EXPECT_INT(out_x[1] - (out_x[0] + out_w[0]), 6);
}
```

Since the test file is C, not C++, replace the lambda with a static function:
```c
static int gap_out_x[2], gap_out_y[2], gap_out_w[2], gap_out_h[2];
static int gap_n = 0;

static void gap_collect_cb(const PaneNode *n, int x, int y, int w, int h, void *user)
{
    (void)user;
    gap_out_x[gap_n] = x; gap_out_y[gap_n] = y;
    gap_out_w[gap_n] = w; gap_out_h[gap_n] = h;
    gap_n++;
}

static void test_pane_gap_reduces_child_size(void)
{
    Session s1 = {0}, s2 = {0};
    PaneNode left = { .kind = PANE_LEAF, .leaf.session = &s1 };
    PaneNode right = { .kind = PANE_LEAF, .leaf.session = &s2 };
    PaneNode root = { .kind = PANE_HSPLIT, .split = { .left = &left, .right = &right, .ratio = 0.5f } };

    gap_n = 0;
    layout_compute_panes(&root, 0, 0, 100, 100, 6, gap_collect_cb, NULL);

    EXPECT_INT(gap_out_w[0] + gap_out_w[1] + 6, 100);
    EXPECT_INT(gap_out_x[1] - (gap_out_x[0] + gap_out_w[0]), 6);
}
```

Register the test in `main()`.

- [ ] **Step 5: Run layout tests**

Run:
```bash
cd build && cmake --build . && ./tests/layout_tests
```
Expected: `0 layout test failure(s)`.

- [ ] **Step 6: Commit**

```bash
git add src/layout.h src/layout.c tests/layout_tests.c
git commit -m "layout: make inter-pane gap configurable"
```

---

### Task 2: Add new semantic UI theme tokens

**Files:**
- Modify: `src/ui_theme.h`
- Modify: `src/ui_theme.c`
- Test: `tests/ui_theme_tests.c`

**Context:** The pane chrome needs theme-derived colors for header bars, status dots, shadows, and gutter hover hints.

- [ ] **Step 1: Extend `UiTheme` struct**

In `src/ui_theme.h`, add these fields before the closing brace of `UiTheme`:
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

- [ ] **Step 2: Derive the new tokens in `ui_theme_derive`**

In `src/ui_theme.c`, inside the dark-theme branch, add before `// Update the global.`:
```c
    // Pane chrome tokens.
    u.pane_header_bg    = blend_cc(u.panel_bg, fg, 30);
    u.pane_header_text  = blend_cc(fg, bg, 30);
    u.pane_header_detail= blend_cc(fg, bg, 90);
    u.pane_status_running = tc2uic(t->ansi[2], 220);  // green
    u.pane_status_idle    = tc2uic(t->ansi[3], 220);  // yellow/amber
    u.pane_status_error   = tc2uic(t->ansi[1], 220);  // red
    u.shadow            = (UiColor){ 0, 0, 0, 55 };
    u.gutter_hover      = u.accent;
```

In the light-theme branch, add the equivalent:
```c
    // Pane chrome tokens.
    u.pane_header_bg    = blend_cc(u.panel_bg, fg, 30);
    u.pane_header_text  = blend_cc(fg, bg, 40);
    u.pane_header_detail= blend_cc(fg, bg, 110);
    u.pane_status_running = tc2uic(t->ansi[2], 230);  // green
    u.pane_status_idle    = tc2uic(t->ansi[3], 230);  // yellow/amber
    u.pane_status_error   = tc2uic(t->ansi[1], 230);  // red
    u.shadow            = (UiColor){ 0, 0, 0, 40 };
    u.gutter_hover      = u.accent;
```

- [ ] **Step 3: Add tests that the new tokens exist and differ from bg**

In `tests/ui_theme_tests.c`, inside `test_dark_theme_contrast`, add:
```c
    EXPECT(memcmp(&u.pane_header_bg, &u.panel_bg, sizeof(UiColor)) != 0);
    EXPECT(rgb_dist(&u.pane_status_running, &u.panel_bg) > 0.20f);
    EXPECT(rgb_dist(&u.pane_status_error,   &u.panel_bg) > 0.20f);
```

Inside `test_light_theme_contrast`, add:
```c
    EXPECT(memcmp(&u.pane_header_bg, &u.panel_bg, sizeof(UiColor)) != 0);
    EXPECT(rgb_dist(&u.pane_status_running, &u.panel_bg) > 0.20f);
    EXPECT(rgb_dist(&u.pane_status_error,   &u.panel_bg) > 0.20f);
```

- [ ] **Step 4: Run ui_theme tests**

Run:
```bash
cd build && cmake --build . && ./tests/ui_theme_tests
```
Expected: `0 ui_theme test failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add src/ui_theme.h src/ui_theme.c tests/ui_theme_tests.c
git commit -m "ui_theme: derive pane chrome semantic tokens"
```

---

### Task 3: Add a pane chrome helper and integrate it

**Files:**
- Modify: `src/main.c`

**Context:** The existing render loop (around lines 6383–6464) calls `layout_compute_panes`, iterates leaf rects, scissor-draws `render_terminal`, and then draws a 1 px focus border. This task wraps that in a helper that draws shadow, rounded frame, title bar, and content.

- [ ] **Step 1: Add helper forward declaration near `render_terminal`**

Near the top of `src/main.c` where other static helpers are declared, add:
```c
static void draw_pane_chrome_and_content(PaneNode *leaf,
                                         int px, int py, int pw, int ph,
                                         bool focused, float scale, double dt,
                                         Font font, Font bold_font,
                                         int cell_width, int cell_height, int font_size, int pad,
                                         GhosttyTerminal terminal,
                                         GhosttyRenderState rs, GhosttyRenderStateRowIterator ri,
                                         GhosttyRenderStateRowCells rc,
                                         GhosttyKittyGraphicsPlacementIterator pi,
                                         KittyImageRenderer *kitty_renderer,
                                         AppConfig *cfg, uint64_t now_ms);
```

- [ ] **Step 2: Implement `draw_pane_chrome_and_content`**

Insert the implementation after `render_terminal` (after line ~2730). Use raylib drawing functions.

Key logic:
```c
static void draw_pane_chrome_and_content(PaneNode *leaf, int px, int py, int pw, int ph,
                                         bool focused, float scale, double dt,
                                         Font font, Font bold_font,
                                         int cell_width, int cell_height, int font_size, int pad,
                                         GhosttyTerminal terminal,
                                         GhosttyRenderState rs, GhosttyRenderStateRowIterator ri,
                                         GhosttyRenderStateRowCells rc,
                                         GhosttyKittyGraphicsPlacementIterator pi,
                                         KittyImageRenderer *kitty_renderer,
                                         AppConfig *cfg, uint64_t now_ms)
{
    (void)dt;
    if (pw < 4 || ph < 4) return;

    const int header_h = (ph >= (int)(48.0f * scale)) ? (int)(24.0f * scale) : 0;
    const float corner = 6.0f * scale;

    // Outer pane rect.
    Rectangle outer = { (float)px, (float)py, (float)pw, (float)ph };

    // Inner content rect (inside border and below header).
    int ix = px + 1;
    int iy = py + header_h + 1;
    int iw = pw - 2;
    int ih = ph - header_h - 2;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;

    // Shadow.
    int soff = (int)(2.0f * scale);
    if (soff < 1) soff = 1;
    Color shadow = UI2RAY(g_ui_theme.shadow);
    DrawRectangleRounded((Rectangle){ (float)(px + soff), (float)(py + soff),
                                      (float)pw, (float)ph },
                         corner / fminf(pw, ph), 8, shadow);

    // Frame background + border.
    DrawRectangleRounded(outer, corner / fminf(pw, ph), 8,
                         UI2RAY(g_ui_theme.panel_bg));
    Color border = focused ? UI2RAY((UiColor){ g_ui_theme.accent.r, g_ui_theme.accent.g, g_ui_theme.accent.b, 220 })
                           : UI2RAY(g_ui_theme.panel_border);
    DrawRectangleRoundedLines(outer, corner / fminf(pw, ph), 8, 1.0f, border);

    // Header bar.
    if (header_h > 0) {
        Rectangle header = { (float)px + 1.0f, (float)py + 1.0f,
                             (float)pw - 2.0f, (float)header_h };
        DrawRectangleRounded(header, corner / fminf(pw, ph), 8,
                             UI2RAY(g_ui_theme.pane_header_bg));

        Session *ss = leaf->leaf.session;
        const char *pcwd = session_cwd(ss);
        const char *label = pcwd ? pcwd : "";
        const char *slash = strrchr(label, '/');
        if (slash && slash[1]) label = slash + 1;

        char primary[128];
        snprintf(primary, sizeof(primary), "%s", label);

        float text_y = py + 1.0f + (header_h - font_size) / 2.0f;
        float text_x = px + 8.0f * scale;

        // Status dot.
        int status_r = (int)(3.5f * scale);
        if (status_r < 2) status_r = 2;
        Color dot = UI2RAY(g_ui_theme.pane_status_idle);
        int exit_st = session_exit_status(ss);
        if (exit_st >= 0) dot = UI2RAY(g_ui_theme.pane_status_error);
        // Running detection: use child_alive when available; idle fallback is fine for now.
        // If session_child_alive(ss) is true, set dot = pane_status_running.
        if (session_child_alive(ss)) dot = UI2RAY(g_ui_theme.pane_status_running);
        DrawCircle((int)(text_x + status_r), (int)(text_y + font_size / 2.0f), status_r, dot);
        text_x += status_r * 2.0f + 6.0f * scale;

        // Primary label.
        Vector2 prim_sz = MeasureTextEx(font, primary, (float)font_size, 0);
        if (prim_sz.x > 0 && text_x + prim_sz.x < px + pw - 8.0f * scale) {
            DrawTextEx(font, primary, (Vector2){ text_x, text_y }, (float)font_size, 0,
                       UI2RAY(g_ui_theme.pane_header_text));
            text_x += prim_sz.x + 8.0f * scale;
        }

        // Branch detail.
        char branch[64] = {0};
        cached_git_branch(pcwd ? pcwd : "", branch, sizeof(branch), now_ms);
        if (branch[0]) {
            char detail[128];
            snprintf(detail, sizeof(detail), "(%s)", branch);
            Vector2 det_sz = MeasureTextEx(font, detail, (float)font_size, 0);
            if (text_x + det_sz.x < px + pw - 8.0f * scale) {
                DrawTextEx(font, detail, (Vector2){ text_x, text_y }, (float)font_size, 0,
                           UI2RAY(g_ui_theme.pane_header_detail));
            }
        }
    }

    // Scissor to inner rect (rectangular; rounded frame hides corners).
    BeginScissorMode(ix, iy, iw, ih);

    // Recalculate rows from inner height for cmdblocks_draw later.
    int inner_rows = (ih - 2 * pad) / cell_height;
    if (inner_rows < 1) inner_rows = 1;

    render_terminal(rs, ri, rc, font, bold_font,
                    cell_width, cell_height, font_size, pad,
                    pw, lsb_ptr_for_terminal ? lsb_ptr_for_terminal : NULL,  // see note below
                    terminal, pi, kitty_renderer, ix, iy, cfg, now_ms);

    EndScissorMode();
}
```

**Note:** The helper needs the scrollbar pointer. Add it as a parameter: `GhosttyTerminalScrollbar *lsb_ptr`. So the real signature includes `GhosttyTerminalScrollbar *lsb_ptr` after `pad`.

- [ ] **Step 3: Replace the inline pane render loop in `main.c`**

Find the existing loop (lines ~6388–6464). Replace it with calls to the helper, passing `lsb_ptr` and `inner_rows`.

Before the loop, compute `int pane_gap = (int)(6.0f * scale);` and pass it to `layout_compute_panes`.

Inside the loop, after calling the helper, keep the `cmdblocks_draw` call for the focused pane. Use `inner_rows` from the helper (or recompute it inline) when calling `cmdblocks_draw`.

Remove the old `BeginScissorMode` / `render_terminal` / `EndScissorMode` / focus border code — the helper now owns all of it.

- [ ] **Step 4: Build and run the app**

Run:
```bash
bash scripts/macos-build.sh
```

Then launch:
```bash
./build/fangs
```

Expected: panes render with rounded corners and a header bar; focused pane has an accent border.

- [ ] **Step 5: Commit**

```bash
git add src/main.c
git commit -m "main: add pane chrome wrapper with rounded frame, shadow, and header bar"
```

---

### Task 4: Refine the block cursor

**Files:**
- Modify: `src/main.c`

**Context:** The cursor is drawn in `render_terminal` around lines 2658–2682. Add a 1 px accent outline when the window is focused.

- [ ] **Step 1: Add accent outline after the cursor body**

In `render_terminal`, after the `switch (vstyle)` block and still inside `if (cursor_visible && blink_on)`, add:
```c
                // Accent outline around the cursor shape.
                Color outline = UI2RAY(g_ui_theme.accent);
                switch (vstyle) {
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK:
                    default:
                        DrawRectangleLines(cur_x, cur_y, cell_width, cell_height, outline);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
                        DrawRectangle(cur_x, cur_y, 2, cell_height, outline);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
                        DrawRectangle(cur_x, cur_y + cell_height - 3, cell_width, 3, outline);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
                        DrawRectangleLines(cur_x, cur_y, cell_width, cell_height, outline);
                        break;
                }
```

- [ ] **Step 2: Build and visually verify**

Run:
```bash
bash scripts/macos-build.sh && ./build/fangs
```

Focus a terminal pane and type. The cursor should have a thin accent-colored outline.

- [ ] **Step 3: Commit**

```bash
git add src/main.c
git commit -m "main: add accent outline to terminal cursor"
```

---

### Task 5: Refresh the scrollbar

**Files:**
- Modify: `src/main.c`

**Context:** The scrollbar is drawn at the end of `render_terminal`. It currently uses `DrawRectangle`, a 6 px width, and is always visible.

- [ ] **Step 1: Add scrollbar fade state tracking**

Add a global array near other globals in `src/main.c`:
```c
static uint64_t g_scrollbar_last_scroll_ms[64];
```

- [ ] **Step 2: Detect scroll activity**

At the start of the scrollbar-drawing block in `render_terminal`, after computing `thumb_y`, check if `scrollbar->offset` changed compared to a stored value. Since `render_terminal` is called per pane, we need an index. The simplest robust approach: store the last offset per pane via a hash of the terminal pointer, or add a parameter `int pane_index`. For now, pass `int pane_index` into `render_terminal` from the caller.

Add `int pane_index` to `render_terminal` signature. At the call site in `draw_pane_chrome_and_content`, pass the loop index.

At the top of the scrollbar block:
```c
    static int last_scrollbar_offset[64] = {0};
    if (pane_index >= 0 && pane_index < 64) {
        if (scrollbar->offset != last_scrollbar_offset[pane_index]) {
            g_scrollbar_last_scroll_ms[pane_index] = now_ms;
            last_scrollbar_offset[pane_index] = scrollbar->offset;
        }
    }
```

- [ ] **Step 3: Compute fade alpha**

After the last-scroll update:
```c
    uint64_t idle_ms = now_ms - g_scrollbar_last_scroll_ms[pane_index];
    float fade = 1.0f - fminf((float)idle_ms / 1500.0f, 1.0f);
    if (fade < 0.0f) fade = 0.0f;
```

- [ ] **Step 4: Draw rounded thumb with fade**

Replace the old `DrawRectangle` call with:
```c
    const int bar_width = (int)(5.0f * scale);
    const int bar_margin = (int)(4.0f * scale);
    int bar_x = origin_x + term_area_w - bar_width - bar_margin;

    Color thumb = UI2RAY(g_ui_theme.scrollbar);
    thumb.a = (unsigned char)(thumb.a * fade);
    if (thumb.a > 0) {
        float radius = (float)bar_width / 2.0f;
        DrawRectangleRounded((Rectangle){ (float)bar_x, (float)thumb_y,
                                          (float)bar_width, (float)thumb_height },
                             radius / fminf(bar_width, thumb_height), 8, thumb);
    }
```

- [ ] **Step 5: Build and verify**

Run:
```bash
bash scripts/macos-build.sh && ./build/fangs
```

Scroll a pane; the scrollbar thumb should fade in and out.

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -m "main: rounded fading scrollbar per pane"
```

---

### Task 6: Add gutter hover resize hints

**Files:**
- Modify: `src/main.c`

**Context:** With a 6 px gap, splits are now negative space. We want a small accent line to appear when the mouse hovers near a split edge.

- [ ] **Step 1: Detect shared edges between adjacent leaf rects**

After the pane render loop in `main.c` (where the old empty gutter loop lived), iterate over `collector.entries`. For each pair of leaves, check if they share a vertical edge (same `x + w` or same `x`) or a horizontal edge (same `y + h` or same `y`) with a gap of roughly `pane_gap` between them.

Add a helper:
```c
static void draw_gutter_hints(PaneRectEntry *entries, int count, int pane_gap,
                              float scale, int mouse_x, int mouse_y)
{
    const int hit_dist = (int)(4.0f * scale);
    const int handle_len = (int)(24.0f * scale);
    Color hint = UI2RAY(g_ui_theme.gutter_hover);

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            PaneRectEntry *a = &entries[i];
            PaneRectEntry *b = &entries[j];

            // Vertical split: a on the left, b on the right.
            if (a->y == b->y && a->h == b->h &&
                (b->x == a->x + a->w + pane_gap || a->x == b->x + b->w + pane_gap)) {
                int edge_x = (a->x < b->x) ? a->x + a->w + pane_gap / 2
                                           : b->x + b->w + pane_gap / 2;
                int mid_y = a->y + a->h / 2;
                if (abs(mouse_x - edge_x) <= hit_dist &&
                    mouse_y >= a->y && mouse_y <= a->y + a->h) {
                    DrawLine(edge_x, mid_y - handle_len / 2,
                             edge_x, mid_y + handle_len / 2, hint);
                }
            }

            // Horizontal split: a on top, b on bottom.
            if (a->x == b->x && a->w == b->w &&
                (b->y == a->y + a->h + pane_gap || a->y == b->y + b->h + pane_gap)) {
                int edge_y = (a->y < b->y) ? a->y + a->h + pane_gap / 2
                                           : b->y + b->h + pane_gap / 2;
                int mid_x = a->x + a->w / 2;
                if (abs(mouse_y - edge_y) <= hit_dist &&
                    mouse_x >= a->x && mouse_x <= a->x + a->w) {
                    DrawLine(mid_x - handle_len / 2, edge_y,
                             mid_x + handle_len / 2, edge_y, hint);
                }
            }
        }
    }
}
```

Call it after the pane loop:
```c
    draw_gutter_hints(pane_rects, collector.count, pane_gap, scale, GetMouseX(), GetMouseY());
```

- [ ] **Step 2: Build and verify**

Run:
```bash
bash scripts/macos-build.sh && ./build/fangs
```

Split a tab into two panes and hover the gutter. An accent handle line should appear.

- [ ] **Step 3: Commit**

```bash
git add src/main.c
git commit -m "main: gutter hover resize hints"
```

---

### Task 7: Command-block card chrome

**Files:**
- Modify: `src/cmdblocks.c`

**Context:** The action bar in `cmdblocks_draw` currently draws flat rounded rectangles. We want a subtle card look.

- [ ] **Step 1: Wrap the action bar in a rounded bordered panel**

In `cmdblocks_draw`, locate where the action bar background is drawn. Before drawing the copy/AI buttons, draw a rounded card background, a 1 px border using the available Raylib rounded-border function (`DrawRectangleRoundedLinesEx` or `DrawRectangleRoundedLines`), and a subtle drop shadow. Use `g_ui_theme.panel_bg`, `g_ui_theme.panel_border`, and a low-alpha shadow derived from `g_ui_theme.shadow`. The card bounds may be derived from the existing button rectangles.

Use existing local variables for `bg_x/bg_y/bg_w/bg_h` if they exist, or compute a bounding box from the existing button rectangles.

- [ ] **Step 2: Build and verify**

Run:
```bash
bash scripts/macos-build.sh && ./build/fangs
```

Run a command in a pane. The "Copy" / "⚡ Ask AI" action bar should look like a bordered card.

- [ ] **Step 3: Commit**

```bash
git add src/cmdblocks.c
git commit -m "cmdblocks: card-style action bar chrome"
```

---

### Task 8: Full test and smoke validation

**Files:**
- Run: all tests + headless smoke

- [ ] **Step 1: Build and run the full test suite**

Run:
```bash
bash scripts/macos-build.sh
ctest --test-dir build --output-on-failure
```

Expected: 100% tests passed, 0 tests failed out of 36 (or current count).

- [ ] **Step 2: Run headless smoke if available**

Check if a smoke test exists:
```bash
grep -n "smoke" scripts/macos-build.sh CMakeLists.txt tests/*.sh 2>/dev/null | head -n 20
```

If there is a smoke target (e.g., `FANGS_SMOKE_SETTINGS=1 ./build/fangs`), run it and confirm it writes screenshots to `/tmp/` without crashing.

- [ ] **Step 3: Commit any test/smoke fixes**

If tests needed adjustment, commit them with a message like:
```bash
git add <files>
git commit -m "tests: adjust expectations for pane chrome changes"
```

---

### Task 9: Final integration commit (if needed)

- [ ] **Step 1: Review git log**

```bash
git log --oneline -10
```

If the per-task commits are clean, no extra commit is needed. Otherwise, verify the working tree is clean:
```bash
git status --short
```

- [ ] **Step 2: Push the branch**

```bash
git push origin main
```

(Only if working on `main`; if on a feature branch, push the feature branch.)

---

## Spec coverage self-review

| Spec requirement | Task |
| ---------------- | ---- |
| Rounded pane frame with shadow | Task 3 |
| Pane title/status bar | Task 3 |
| Soft split gutters (6 px gap) | Task 1, Task 6 |
| Block cursor outline | Task 4 |
| Rounded fading scrollbar | Task 5 |
| Command-block card chrome | Task 7 |
| Theme-derived new tokens | Task 2 |
| Validation (build + tests + smoke) | Task 8, Task 9 |

No placeholders remain.
