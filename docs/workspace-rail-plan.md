# Workspace Rail Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Workspace Rail slice: left vertical tabs and panes with cwd, git branch, click focus, and notification rings.

**Architecture:** Add pure workspace info, status, and rail model modules; extend layout with an optional left rail; keep Raylib drawing isolated in `ui_workspace_rail.c`; keep `main.c` as the orchestration layer. Detect background activity from session feed stats and command-block completion sequence, not from expensive per-frame output formatting.

**Tech Stack:** C11, Raylib, existing Fangs `Layout`, `PaneNode`, `Session`, `CmdBlocks`, CTest via CMake.

---

## File Map

- Create: `src/workspace_info.h`
- Create: `src/workspace_info.c`
- Create: `tests/workspace_info_tests.c`
- Create: `src/workspace_status.h`
- Create: `src/workspace_status.c`
- Create: `tests/workspace_status_tests.c`
- Create: `src/ui_workspace_rail_model.h`
- Create: `src/ui_workspace_rail_model.c`
- Create: `tests/ui_workspace_rail_model_tests.c`
- Create: `src/ui_workspace_rail.h`
- Create: `src/ui_workspace_rail.c`
- Modify: `src/layout.h`
- Modify: `src/layout.c`
- Modify: `tests/layout_tests.c`
- Modify: `src/cmdblocks.h`
- Modify: `src/cmdblocks.c`
- Modify: `src/session.h`
- Modify: `src/session.c`
- Modify: `src/config.h`
- Modify: `src/config.c`
- Modify: `src/action_registry.h`
- Modify: `src/action_registry.c`
- Modify: `src/main.c`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `docs/config.example`

## Task 1: CWD And Git Branch Helpers

**Files:**
- Create: `src/workspace_info.h`
- Create: `src/workspace_info.c`
- Create: `tests/workspace_info_tests.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Create `tests/workspace_info_tests.c`:

```c
#include "workspace_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected '%s', got '%s'\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        failures++; \
    } \
} while (0)

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void write_file(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(body, f);
    fclose(f);
}

static void test_cwd_label(void)
{
    char out[128];
    workspace_cwd_label("/Users/rene/src/fangs", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "fangs");
    workspace_cwd_label("/Users/rene", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "~");
    workspace_cwd_label("/", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "/");
}

static void test_git_branch_directory(void)
{
    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-info-%ld", (long)getpid());
    mkdir(root, 0700);
    char git[512], sub[512], head[512];
    snprintf(git, sizeof(git), "%s/.git", root);
    snprintf(sub, sizeof(sub), "%s/src", root);
    snprintf(head, sizeof(head), "%s/HEAD", git);
    mkdir(git, 0700);
    mkdir(sub, 0700);
    write_file(head, "ref: refs/heads/main\n");

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(sub, out, (int)sizeof(out)));
    EXPECT_STR(out, "main");
}

static void test_git_detached_head(void)
{
    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-detached-%ld", (long)getpid());
    mkdir(root, 0700);
    char git[512], head[512];
    snprintf(git, sizeof(git), "%s/.git", root);
    snprintf(head, sizeof(head), "%s/HEAD", git);
    mkdir(git, 0700);
    write_file(head, "0123456789abcdef0123456789abcdef01234567\n");

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(root, out, (int)sizeof(out)));
    EXPECT_STR(out, "0123456");
}

static void test_git_file_pointer(void)
{
    char root[512], real_git[512], head[512], dotgit[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-file-%ld", (long)getpid());
    snprintf(real_git, sizeof(real_git), "/tmp/fangs-workspace-real-git-%ld", (long)getpid());
    mkdir(root, 0700);
    mkdir(real_git, 0700);
    snprintf(head, sizeof(head), "%s/HEAD", real_git);
    snprintf(dotgit, sizeof(dotgit), "%s/.git", root);
    write_file(head, "ref: refs/heads/worktree\n");
    char body[1024];
    snprintf(body, sizeof(body), "gitdir: %s\n", real_git);
    write_file(dotgit, body);

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(root, out, (int)sizeof(out)));
    EXPECT_STR(out, "worktree");
}

static void test_no_repo(void)
{
    char out[128] = "unchanged";
    EXPECT_TRUE(!workspace_git_branch("/tmp", out, (int)sizeof(out)));
    EXPECT_STR(out, "");
}

int main(void)
{
    test_cwd_label();
    test_git_branch_directory();
    test_git_detached_head();
    test_git_file_pointer();
    test_no_repo();
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Add test target and run it to see it fail**

Modify `CMakeLists.txt`:

```cmake
add_executable(workspace_info_tests
  tests/workspace_info_tests.c
  src/workspace_info.c)
target_compile_features(workspace_info_tests PRIVATE c_std_11)
target_include_directories(workspace_info_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME workspace_info_tests COMMAND workspace_info_tests)
```

Run:

```bash
cmake --build build --target workspace_info_tests
ctest --test-dir build -R workspace_info_tests --output-on-failure
```

Expected before implementation: compile fails because `workspace_info.h` or functions do not exist.

- [ ] **Step 3: Implement helper API**

Create `src/workspace_info.h`:

```c
#ifndef FANGS_WORKSPACE_INFO_H
#define FANGS_WORKSPACE_INFO_H

#include <stdbool.h>

void workspace_cwd_label(const char *cwd, const char *home, char *out, int out_size);
bool workspace_git_branch(const char *cwd, char *out, int out_size);

#endif
```

Implement `src/workspace_info.c` with these rules:

- `workspace_cwd_label`: returns `~` for home, `/` for root, basename for other paths, and `""` for empty input.
- `workspace_git_branch`: walks upward from `cwd`; supports `.git` directory and `.git` file with `gitdir:`; reads `HEAD`; returns branch name or seven-character detached hash; writes `""` and returns `false` when no branch exists.
- No `system()`, `popen()`, or `git` subprocess.

- [ ] **Step 4: Verify task**

Run:

```bash
cmake --build build --target workspace_info_tests
ctest --test-dir build -R workspace_info_tests --output-on-failure
```

Expected: `workspace_info_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/workspace_info.h src/workspace_info.c tests/workspace_info_tests.c CMakeLists.txt
git commit -m "feat: add workspace info helpers"
```

## Task 2: Workspace Attention Model

**Files:**
- Create: `src/workspace_status.h`
- Create: `src/workspace_status.c`
- Create: `tests/workspace_status_tests.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Create tests that cover these exact behaviors:

```c
#include "workspace_status.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_INT(actual, expected) do { int a=(actual), e=(expected); if (a != e) { fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); failures++; } } while (0)

static void test_background_output_marks_info(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 12);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_INFO);
}

static void test_active_output_does_not_mark_unread(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, true, 12);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
}

static void test_failed_background_command_marks_warn(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_command(&st, 10, false, 2);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    const char *text = workspace_status_text(&st, 10);
    EXPECT_TRUE(strstr(text, "exit 2") != NULL);
}

static void test_focus_clears_one_pane(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_output(&st, 20, false, 1);
    workspace_status_clear(&st, 10);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
    EXPECT_INT(workspace_status_level(&st, 20), WORKSPACE_ATTENTION_INFO);
}

static void test_tab_aggregate_and_notification(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    uint64_t panes[] = { 10, 20 };
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_command(&st, 20, false, 7);
    EXPECT_INT(workspace_status_highest(&st, panes, 2), WORKSPACE_ATTENTION_WARN);
    char out[128];
    workspace_status_notification(&st, panes, 2, out, (int)sizeof(out));
    EXPECT_TRUE(strstr(out, "exit 7") != NULL);
}

int main(void)
{
    test_background_output_marks_info();
    test_active_output_does_not_mark_unread();
    test_failed_background_command_marks_warn();
    test_focus_clears_one_pane();
    test_tab_aggregate_and_notification();
    return failures ? 1 : 0;
}
```

- [ ] **Step 2: Add test target and run it to see it fail**

Add:

```cmake
add_executable(workspace_status_tests
  tests/workspace_status_tests.c
  src/workspace_status.c)
target_compile_features(workspace_status_tests PRIVATE c_std_11)
target_include_directories(workspace_status_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME workspace_status_tests COMMAND workspace_status_tests)
```

Run:

```bash
cmake --build build --target workspace_status_tests
ctest --test-dir build -R workspace_status_tests --output-on-failure
```

Expected before implementation: compile fails.

- [ ] **Step 3: Implement status API**

Create `src/workspace_status.h`:

```c
#ifndef FANGS_WORKSPACE_STATUS_H
#define FANGS_WORKSPACE_STATUS_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    WORKSPACE_ATTENTION_NONE = 0,
    WORKSPACE_ATTENTION_INFO = 1,
    WORKSPACE_ATTENTION_WARN = 2,
    WORKSPACE_ATTENTION_ERROR = 3,
} WorkspaceAttention;

typedef struct {
    uint64_t pane_id;
    WorkspaceAttention level;
    char text[96];
} WorkspaceStatusEntry;

typedef struct {
    WorkspaceStatusEntry entries[128];
    int count;
} WorkspaceStatus;

void workspace_status_init(WorkspaceStatus *st);
void workspace_status_prune(WorkspaceStatus *st, const uint64_t *live_ids, int live_count);
void workspace_status_note_output(WorkspaceStatus *st, uint64_t pane_id, int focused, size_t bytes);
void workspace_status_note_command(WorkspaceStatus *st, uint64_t pane_id, int focused, int exit_code);
void workspace_status_note_exit(WorkspaceStatus *st, uint64_t pane_id, int focused, int exit_code);
void workspace_status_clear(WorkspaceStatus *st, uint64_t pane_id);
WorkspaceAttention workspace_status_level(const WorkspaceStatus *st, uint64_t pane_id);
const char *workspace_status_text(const WorkspaceStatus *st, uint64_t pane_id);
WorkspaceAttention workspace_status_highest(const WorkspaceStatus *st, const uint64_t *pane_ids, int count);
void workspace_status_notification(const WorkspaceStatus *st, const uint64_t *pane_ids, int count,
                                   char *out, int out_size);

#endif
```

Implementation rules:

- Active events do not create unread entries.
- `bytes == 0` does nothing.
- `exit_code == 0` does nothing for command completion.
- A higher level replaces a lower level.
- A lower level does not overwrite a higher level.
- `workspace_status_notification` returns the text for the highest-level pane in input order.

- [ ] **Step 4: Verify task**

Run:

```bash
cmake --build build --target workspace_status_tests
ctest --test-dir build -R workspace_status_tests --output-on-failure
```

Expected: `workspace_status_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/workspace_status.h src/workspace_status.c tests/workspace_status_tests.c CMakeLists.txt
git commit -m "feat: add workspace attention model"
```

## Task 3: Rail-Aware Layout

**Files:**
- Modify: `src/layout.h`
- Modify: `src/layout.c`
- Modify: `tests/layout_tests.c`

- [ ] **Step 1: Add failing layout tests**

Extend `tests/layout_tests.c` with:

```c
static void test_full_rail_sits_left_of_terminal(void)
{
    Layout lo = layout_compute_with_rail(1400, 800, true, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(!lo.rail_compact);
    EXPECT_INT(lo.rail.x, 0);
    EXPECT_INT(lo.rail.w, 260);
    EXPECT_INT(lo.terminal.x, 260);
    EXPECT_INT(lo.terminal.w, 1140);
}

static void test_rail_and_sidebar_preserve_terminal_width(void)
{
    Layout lo = layout_compute_with_rail(1000, 800, true, 260, 56, true, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_INT(lo.rail.w, 260);
    EXPECT_TRUE(lo.sidebar_visible);
    EXPECT_INT(lo.terminal.w, 360);
    EXPECT_INT(lo.terminal.x, 260);
    EXPECT_INT(lo.sidebar.x, 620);
}

static void test_rail_compacts_when_full_width_will_not_fit(void)
{
    Layout lo = layout_compute_with_rail(700, 600, true, 260, 56, true, 380, 4, 320);
    EXPECT_TRUE(lo.rail_visible);
    EXPECT_TRUE(lo.rail_compact);
    EXPECT_INT(lo.rail.w, 56);
    EXPECT_TRUE(lo.terminal.w >= 320);
}

static void test_rail_hides_when_too_narrow(void)
{
    Layout lo = layout_compute_with_rail(340, 600, true, 260, 56, false, 380, 4, 320);
    EXPECT_TRUE(!lo.rail_visible);
    EXPECT_INT(lo.rail.w, 0);
    EXPECT_INT(lo.terminal.x, 0);
    EXPECT_INT(lo.terminal.w, 340);
}
```

Call them from `main()`.

- [ ] **Step 2: Run tests to see them fail**

```bash
cmake --build build --target layout_tests
ctest --test-dir build -R layout_tests --output-on-failure
```

Expected before implementation: compile fails because `layout_compute_with_rail` and fields do not exist.

- [ ] **Step 3: Implement layout extension**

Modify `Layout` in `src/layout.h`:

```c
typedef struct {
    Rect terminal;
    Rect sidebar;
    Rect rail;
    bool sidebar_visible;
    bool rail_visible;
    bool rail_compact;
} Layout;
```

Add:

```c
Layout layout_compute_with_rail(int window_w, int window_h,
                                bool rail_enabled, int rail_width, int rail_compact_width,
                                bool sidebar_visible, int sidebar_width,
                                int pad, int min_terminal_w);
```

Implementation rule:

1. Choose full rail if `window_w - rail_width >= min_terminal_w`.
2. Else choose compact rail if `window_w - rail_compact_width >= min_terminal_w`.
3. Else hide rail.
4. Allocate sidebar from the remaining width using the existing clamp behavior.
5. Set terminal `x` to the rail width and terminal `w` to remaining width minus sidebar width.
6. Keep `layout_compute()` behavior identical by calling the new function with `rail_enabled = false`.

- [ ] **Step 4: Verify task**

```bash
cmake --build build --target layout_tests
ctest --test-dir build -R layout_tests --output-on-failure
```

Expected: `layout_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/layout.h src/layout.c tests/layout_tests.c
git commit -m "feat: add workspace rail layout"
```

## Task 4: Command And Session Event Sources

**Files:**
- Modify: `src/cmdblocks.h`
- Modify: `src/cmdblocks.c`
- Modify: `src/session.h`
- Modify: `src/session.c`
- Add focused tests where possible by extending existing command-block parser tests only if an exported pure test can observe sequence behavior.

- [ ] **Step 1: Add command-block completion API**

Add to `src/cmdblocks.h`:

```c
unsigned long cmdblocks_completion_seq(const CmdBlocks *cb);
int cmdblocks_latest_exit_code(const CmdBlocks *cb);
```

In `struct CmdBlocks`, add:

```c
unsigned long completion_seq;
int latest_exit_code;
```

In `cmdblocks_feed`, when `hit.mark == CB_MARK_DONE`, set:

```c
cb->latest_exit_code = hit.code;
cb->completion_seq++;
```

Return `0` and `-1` from accessors when `cb == NULL`.

- [ ] **Step 2: Add session feed stats API**

Add to `src/session.h`:

```c
typedef struct {
    size_t bytes_read;
    bool eof;
    bool error;
} SessionFeedStats;

SessionFeedStats session_feed_pty_stats(Session *s);
```

Implement `session_feed_pty_stats()` in `src/session.c` by using the same read loop as `session_feed_pty`, accumulating `bytes_read`. Keep existing behavior for EOF and errors. Change `session_feed_pty()` to:

```c
void session_feed_pty(Session *s)
{
    (void)session_feed_pty_stats(s);
}
```

- [ ] **Step 3: Verify build**

```bash
cmake --build build
ctest --test-dir build -R "cmdblocks_osc_tests|pane_tests" --output-on-failure
```

Expected: build succeeds and selected tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/cmdblocks.h src/cmdblocks.c src/session.h src/session.c
git commit -m "feat: expose workspace activity events"
```

## Task 5: Rail Presentation Model

**Files:**
- Create: `src/ui_workspace_rail_model.h`
- Create: `src/ui_workspace_rail_model.c`
- Create: `tests/ui_workspace_rail_model_tests.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Tests should verify row ordering, active markers, compact labels, and notification text. Use fixed arrays instead of `App` to keep the model pure.

Core header shape:

```c
typedef enum {
    WORKSPACE_RAIL_ACTION_NONE = 0,
    WORKSPACE_RAIL_ACTION_SWITCH_TAB,
    WORKSPACE_RAIL_ACTION_FOCUS_PANE,
} WorkspaceRailActionType;

typedef struct {
    uint64_t id;
    int index;
    int active;
    WorkspaceAttention attention;
    char label[64];
    char branch[64];
    char text[128];
} WorkspaceRailRow;

typedef struct {
    WorkspaceRailRow tabs[9];
    int tab_count;
    WorkspaceRailRow panes[64];
    int pane_count;
    char notification[128];
    int compact;
} WorkspaceRailView;
```

Write tests that call a model builder with two tabs and two panes, then assert:

- active tab row has `active == 1`
- pane rows preserve input order
- compact mode keeps labels empty or numeric-only
- notification contains failed command text from `WorkspaceStatus`

- [ ] **Step 2: Add test target and run it to see it fail**

Add:

```cmake
add_executable(ui_workspace_rail_model_tests
  tests/ui_workspace_rail_model_tests.c
  src/ui_workspace_rail_model.c
  src/workspace_status.c)
target_compile_features(ui_workspace_rail_model_tests PRIVATE c_std_11)
target_include_directories(ui_workspace_rail_model_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
add_test(NAME ui_workspace_rail_model_tests COMMAND ui_workspace_rail_model_tests)
```

Run:

```bash
cmake --build build --target ui_workspace_rail_model_tests
ctest --test-dir build -R ui_workspace_rail_model_tests --output-on-failure
```

Expected before implementation: compile fails.

- [ ] **Step 3: Implement the model**

Keep this file free of Raylib. It should copy already-computed cwd labels and branches into rows, attach attention levels from `WorkspaceStatus`, and compute one notification string from visible pane IDs.

- [ ] **Step 4: Verify task**

```bash
cmake --build build --target ui_workspace_rail_model_tests
ctest --test-dir build -R ui_workspace_rail_model_tests --output-on-failure
```

Expected: `ui_workspace_rail_model_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/ui_workspace_rail_model.h src/ui_workspace_rail_model.c tests/ui_workspace_rail_model_tests.c CMakeLists.txt
git commit -m "feat: add workspace rail model"
```

## Task 6: Rail Rendering And Main Integration

**Files:**
- Create: `src/ui_workspace_rail.h`
- Create: `src/ui_workspace_rail.c`
- Modify: `src/main.c`
- Modify: `src/config.h`
- Modify: `src/config.c`
- Modify: `src/action_registry.h`
- Modify: `src/action_registry.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Wire config and action registry**

Add to `AppConfig`:

```c
bool workspace_rail;
```

Defaults: `true`. Save/load under `[ui]`:

```ini
[ui]
workspace_rail = true
```

Add action:

```c
FANGS_ACTION_TOGGLE_WORKSPACE_RAIL,
```

Registry row:

```c
{
    FANGS_ACTION_TOGGLE_WORKSPACE_RAIL,
    "workspace.toggle_rail",
    "Toggle Workspace Rail",
    "Show or hide vertical tabs, panes, and notifications",
    "",
},
```

- [ ] **Step 2: Add renderer API**

Create `src/ui_workspace_rail.h`:

```c
#ifndef FANGS_UI_WORKSPACE_RAIL_H
#define FANGS_UI_WORKSPACE_RAIL_H

#include "layout.h"
#include "raylib.h"
#include "ui_workspace_rail_model.h"

typedef struct {
    WorkspaceRailActionType type;
    int index;
} WorkspaceRailAction;

WorkspaceRailAction ui_workspace_rail_draw(Font font, Rect bounds,
                                           const WorkspaceRailView *view,
                                           int mouse_x, int mouse_y,
                                           int mouse_pressed);

#endif
```

Draw rules:

- Fill `bounds` with `g_ui_theme.panel_bg`.
- Draw a right separator with `g_ui_theme.sidebar_separator`.
- Use row height `32` in full mode and `36` in compact mode.
- Draw active rows with a subtle `selection` fill.
- Draw attention rings as small filled circles: info uses accent, warn uses inline error, error uses a stronger red derived from inline error.
- Use `MeasureTextEx` to clip or truncate row text before drawing.
- Return switch/focus action when mouse press lands on a tab or pane row.

- [ ] **Step 3: Integrate layout and drawing in `main.c`**

Add includes:

```c
#include "workspace_info.h"
#include "workspace_status.h"
#include "ui_workspace_rail.h"
#include "ui_workspace_rail_model.h"
```

Add process lifetime state near other app-level state:

```c
WorkspaceStatus workspace_status;
workspace_status_init(&workspace_status);
unsigned long pane_seen_completion[128];
```

Use `layout_compute_with_rail()` where `layout_compute()` currently runs:

```c
lo = layout_compute_with_rail(w, h, cfg.workspace_rail,
                              260, 56,
                              ui_sidebar_visible(), sidebar_width,
                              pad, min_terminal_w);
term_area_w = lo.terminal.w;
```

When feeding each session, use `session_feed_pty_stats(ss)`. If `bytes_read > 0` and the leaf is not focused, call `workspace_status_note_output(...)`.

For each leaf, read:

```c
CmdBlocks *cb = (CmdBlocks *)session_cmdblocks(ss);
unsigned long seq = cmdblocks_completion_seq(cb);
int code = cmdblocks_latest_exit_code(cb);
```

If `seq` differs from the stored last-seen sequence, update last-seen and call `workspace_status_note_command(...)`.

When focus changes through click, keyboard, tab switch, split creation, or close fallback, call:

```c
workspace_status_clear(&workspace_status, pane_id_for_session(focused_session));
```

Before terminal mouse handling, draw rail and apply click action. If rail consumes a click, skip terminal mouse handling for that frame.

- [ ] **Step 4: Build and smoke manually**

```bash
cmake --build build
./build/fangs
```

Manual checks:

- Rail appears left.
- New tabs add tab rows.
- Splits add pane rows.
- Clicking rail rows switches focus.
- AI sidebar still appears on the right.
- Terminal text starts after the rail, with no overlap.

- [ ] **Step 5: Commit**

```bash
git add src/ui_workspace_rail.h src/ui_workspace_rail.c src/main.c src/config.h src/config.c src/action_registry.h src/action_registry.c CMakeLists.txt
git commit -m "feat: add workspace rail UI"
```

## Task 7: Docs, Full Verification, And Handoff Update

**Files:**
- Modify: `README.md`
- Modify: `docs/config.example`
- Modify: `docs/handoff-workspace-rail.md`

- [ ] **Step 1: Document user-facing behavior**

Update README key feature list and configuration section:

```markdown
- **Workspace rail** — vertical tabs and panes with project labels, git branches, and notification rings.
```

Add config example:

```ini
[ui]
workspace_rail = true
```

- [ ] **Step 2: Run full verification**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Expected:

- Build exits `0`.
- CTest reports all tests passed.
- `git diff --check` produces no output.

- [ ] **Step 3: Commit docs**

```bash
git add README.md docs/config.example docs/handoff-workspace-rail.md
git commit -m "docs: document workspace rail"
```

## Final Acceptance Checklist

- [ ] Rail appears on the left and does not overlap terminal content.
- [ ] Rail compacts or hides on narrow windows.
- [ ] Tab rows reflect `app.tabs`.
- [ ] Pane rows reflect active tab leaves.
- [ ] CWD labels come from `session_cwd()`.
- [ ] Git branches resolve without invoking `git`.
- [ ] Rail clicks focus tabs and panes.
- [ ] Background output marks info attention.
- [ ] Background failed command marks warn attention.
- [ ] Focusing a pane clears its attention.
- [ ] Command palette toggles the rail.
- [ ] `cmake --build build` passes.
- [ ] `ctest --test-dir build --output-on-failure` passes.
- [ ] `git diff --check` is clean.
