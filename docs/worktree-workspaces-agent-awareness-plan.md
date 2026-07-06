# Worktree Workspaces And Agent Awareness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add isolated git worktree workspaces, macOS notifications for unfocused agent rings, and per-workspace working indicators.

**Architecture:** Keep render/layout pure and action-time side effects in host handlers. Add focused helper modules for git worktree creation and desktop notifications, extend `workspace_status` with recent-output timestamps, and pass a `working` boolean through the rail model into the renderer.

**Tech Stack:** C11, Raylib, existing Fangs `App`/`Tab`/`Session`/`WorkspaceStatus`, git CLI via `fork`/`execvp`, macOS `osascript`, CMake/CTest.

---

## File Map

- Create: `src/workspace_worktree.h`
- Create: `src/workspace_worktree.c`
- Create: `tests/workspace_worktree_tests.c`
- Create: `src/desktop_notify.h`
- Create: `src/desktop_notify.c`
- Create: `tests/desktop_notify_tests.c`
- Modify: `src/action_registry.h`
- Modify: `src/action_registry.c`
- Modify: `tests/action_registry_tests.c`
- Modify: `src/workspace_status.h`
- Modify: `src/workspace_status.c`
- Modify: `tests/workspace_status_tests.c`
- Modify: `src/ui_workspace_rail_model.h`
- Modify: `src/ui_workspace_rail_model.c`
- Modify: `tests/ui_workspace_rail_model_tests.c`
- Modify: `src/ui_workspace_rail.c`
- Modify: `src/main.c`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify if useful during implementation: `docs/workspace-rail-spec.md`

## Task 1: Add Worktree Creation Helper

**Files:**

- Create: `src/workspace_worktree.h`
- Create: `src/workspace_worktree.c`
- Create: `tests/workspace_worktree_tests.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Add `tests/workspace_worktree_tests.c` with these cases:

```c
#include "workspace_worktree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_FALSE(expr) do { if ((expr)) { fprintf(stderr, "FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static int run_git(const char *repo, const char *a, const char *b, const char *c, const char *d)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (repo)
            execlp("git", "git", "-C", repo, a, b, c, d, (char *)NULL);
        else
            execlp("git", "git", a, b, c, d, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

static void test_sanitize_name(void)
{
    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_sanitize_name("feature/agent ui", out, sizeof(out)));
    EXPECT_STR(out, "feature-agent-ui");
    EXPECT_TRUE(workspace_worktree_sanitize_name("---main---", out, sizeof(out)));
    EXPECT_STR(out, "main");
    EXPECT_FALSE(workspace_worktree_sanitize_name("///", out, sizeof(out)));
}

static void test_candidate_generation(void)
{
    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_candidate("main", 0, out, sizeof(out)));
    EXPECT_STR(out, "main-agent");
    EXPECT_TRUE(workspace_worktree_candidate("feature/agent-ui", 2, out, sizeof(out)));
    EXPECT_STR(out, "feature-agent-ui-agent-2");
    EXPECT_TRUE(workspace_worktree_candidate("", 0, out, sizeof(out)));
    EXPECT_STR(out, "worktree-agent");
}

static void test_create_rejects_non_repo(void)
{
    WorkspaceWorktreeResult r;
    EXPECT_FALSE(workspace_worktree_create("/tmp", &r));
    EXPECT_TRUE(r.error[0] != '\0');
}

static void test_create_local_worktree(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL, NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult r;
    EXPECT_TRUE(workspace_worktree_create(root, &r));
    EXPECT_STR(r.branch, "main-agent");
    EXPECT_TRUE(strstr(r.path, "/.worktrees/main-agent") != NULL);

    char dotgit[1024];
    snprintf(dotgit, sizeof(dotgit), "%s/.git", r.path);
    struct stat st;
    EXPECT_TRUE(stat(dotgit, &st) == 0);
}

int main(void)
{
    test_sanitize_name();
    test_candidate_generation();
    test_create_rejects_non_repo();
    test_create_local_worktree();
    return failures ? 1 : 0;
}
```

Add a CMake target for `workspace_worktree_tests`.

- [ ] **Step 2: Run the focused test and confirm RED**

Run:

```bash
cmake -S . -B build
cmake --build build --target workspace_worktree_tests
ctest --test-dir build -R workspace_worktree_tests --output-on-failure
```

Expected before implementation: compile fails because `workspace_worktree.h` does not exist.

- [ ] **Step 3: Implement `workspace_worktree.h`**

Expose:

```c
#ifndef FANGS_WORKSPACE_WORKTREE_H
#define FANGS_WORKSPACE_WORKTREE_H

#include <stdbool.h>

#define WORKTREE_NAME_MAX 96
#define WORKTREE_PATH_MAX 4096
#define WORKTREE_ERROR_MAX 256

typedef struct {
    char repo_root[WORKTREE_PATH_MAX];
    char branch[WORKTREE_NAME_MAX];
    char path[WORKTREE_PATH_MAX];
    char error[WORKTREE_ERROR_MAX];
} WorkspaceWorktreeResult;

bool workspace_worktree_sanitize_name(const char *input, char *out, int out_size);
bool workspace_worktree_candidate(const char *current_branch, int suffix, char *out, int out_size);
bool workspace_worktree_create(const char *cwd, WorkspaceWorktreeResult *out);
bool workspace_worktree_remove_created(const WorkspaceWorktreeResult *created);

#endif
```

- [ ] **Step 4: Implement `workspace_worktree.c`**

Implementation requirements:

- Use `fork`/`execvp` with argv arrays for every git invocation.
- Capture stdout for:
  - `git -C <cwd> rev-parse --show-toplevel`
  - `git -C <cwd> branch --show-current`
- Use exit status only for:
  - `git -C <repo-root> show-ref --verify --quiet refs/heads/<candidate>`
  - `git -C <repo-root> worktree add -b <candidate> <path> HEAD`
  - `git -C <repo-root> worktree remove --force <path>`
- Create `<repo-root>/.worktrees` with `mkdir(worktrees_path, 0775)` when absent.
- Candidate uniqueness checks both existing path and branch ref.
- Best-effort append `.worktrees/` to `<repo-root>/.git/info/exclude` if the line is absent.
- Return `false` with `out->error` populated on every failure path.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
cmake --build build --target workspace_worktree_tests
ctest --test-dir build -R workspace_worktree_tests --output-on-failure
```

Expected: `workspace_worktree_tests` passes.

- [ ] **Step 6: Commit**

```bash
git add src/workspace_worktree.h src/workspace_worktree.c tests/workspace_worktree_tests.c CMakeLists.txt
git commit -m "feat: add git worktree workspace helper"
```

## Task 2: Add Worktree Workspace Action

**Files:**

- Modify: `src/action_registry.h`
- Modify: `src/action_registry.c`
- Modify: `tests/action_registry_tests.c`
- Modify: `src/main.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing action registry test**

Extend `tests/action_registry_tests.c`:

```c
static void test_registry_exposes_worktree_workspace_action(void)
{
    const FangsAction *action = action_registry_find(FANGS_ACTION_NEW_WORKTREE_WORKSPACE);
    EXPECT_TRUE(action != NULL);
    EXPECT_STR_EQ(action->name, "workspace.new_worktree");
    EXPECT_STR_EQ(action->label, "New Worktree Workspace");
}
```

Call it from `main()`.

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target action_registry_tests
ctest --test-dir build -R action_registry_tests --output-on-failure
```

Expected before implementation: compile fails because `FANGS_ACTION_NEW_WORKTREE_WORKSPACE` is undefined.

- [ ] **Step 3: Add registry action**

Add enum value after `FANGS_ACTION_NEW_TAB`:

```c
FANGS_ACTION_NEW_WORKTREE_WORKSPACE,
```

Add registry entry:

```c
{
    FANGS_ACTION_NEW_WORKTREE_WORKSPACE,
    "workspace.new_worktree",
    "New Worktree Workspace",
    "Create a git worktree and open a workspace there",
    "",
},
```

- [ ] **Step 4: Add an optional-name tab creation path**

In `src/main.c`, refactor `app_add_tab()` through an internal helper:

```c
static Session *app_add_tab_named(uint16_t cols, uint16_t rows,
                                  int cell_w, int cell_h,
                                  int max_scrollback, const char *cwd,
                                  const char *name,
                                  bool kitty_images,
                                  int kitty_image_storage_mb,
                                  TermEngine **te, int *pty_fd,
                                  pid_t *child, bool *child_exited)
```

Behavior:

- Same as existing `app_add_tab()`.
- Set `tab->name` to `name` when `name && name[0]`, else `""`.
- Keep existing `app_add_tab()` as a wrapper passing `NULL` for name.

- [ ] **Step 5: Add worktree host helper**

Add a helper near other host action helpers:

```c
static bool open_worktree_workspace_from_current(
    TermEngine **te, int *pty_fd, pid_t *child, bool *child_exited,
    GhosttyTerminal *terminal,
    GhosttyRenderState *render_state,
    GhosttyRenderStateRowIterator *row_iter,
    GhosttyRenderStateRowCells *row_cells,
    GhosttyKittyGraphicsPlacementIterator *placement_iter,
    GhosttyKeyEncoder *key_encoder,
    GhosttyKeyEvent *key_event,
    GhosttyMouseEncoder *mouse_encoder,
    GhosttyMouseEvent *mouse_event,
    AppConfig *cfg,
    int cell_width, int cell_height,
    uint16_t term_cols, uint16_t term_rows,
    int *prev_term_area_w)
```

Required behavior:

- Call `sync_runtime_for_action(te, pty_fd, child, child_exited, terminal, render_state, row_iter, row_cells, placement_iter, key_encoder, key_event, mouse_encoder, mouse_event)`.
- If `app.n_tabs >= FANGS_MAX_TABS`, show a toast and return `false`.
- Call `workspace_worktree_create(session_cwd(cur), &created)`.
- On failure, `toast_show(created.error)` and return `false`.
- Call `app_add_tab_named(term_cols, term_rows, cell_width, cell_height, cfg->scrollback, created.path, created.branch, cfg->kitty_images, cfg->kitty_image_storage_mb, te, pty_fd, child, child_exited)`.
- If tab creation fails, call `workspace_worktree_remove_created(&created)`, show a toast, and return `false`.
- Sync runtime handles and set `prev_term_area_w = -1`.
- Show a success toast.

- [ ] **Step 6: Wire palette action**

In `execute_host_action()`, handle:

```c
case FANGS_ACTION_NEW_WORKTREE_WORKSPACE:
    open_worktree_workspace_from_current(
        te, pty_fd, child, child_exited,
        terminal, render_state, row_iter, row_cells, placement_iter,
        key_encoder, key_event, mouse_encoder, mouse_event,
        cfg, cell_width, cell_height, term_cols, term_rows,
        prev_term_area_w);
    break;
```

- [ ] **Step 7: Verify action tests**

Run:

```bash
cmake --build build --target action_registry_tests
ctest --test-dir build -R action_registry_tests --output-on-failure
```

Expected: `action_registry_tests` passes.

- [ ] **Step 8: Commit**

```bash
git add src/action_registry.h src/action_registry.c tests/action_registry_tests.c src/main.c CMakeLists.txt
git commit -m "feat: add worktree workspace action"
```

## Task 3: Add Option/Alt-Click Rail Shortcut

**Files:**

- Modify: `src/main.c`
- Modify: `README.md`

- [ ] **Step 1: Add click behavior**

In the existing `WORKSPACE_RAIL_ACTION_NEW_TAB` branch, compute:

```c
bool alternate_new_workspace =
    IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
```

If true, call `open_worktree_workspace_from_current()` with the local runtime pointers from the rail click block; otherwise keep the existing `app_add_tab()` behavior.

- [ ] **Step 2: Preserve normal `Cmd+T` behavior**

Do not change the `KEY_T` handler. It should keep calling `app_add_tab` with the focused session cwd.

- [ ] **Step 3: Update README**

Update the workspace rail text and shortcut table:

- `+` opens a same-directory workspace.
- Option/Alt-click `+` creates a git worktree workspace.
- Command palette exposes `New Worktree Workspace`.

- [ ] **Step 4: Manual smoke**

Run Fangs from a git repo:

```bash
cmake --build build --target fangs
./build/fangs
```

In the app, Option/Alt-click `+`. In the new workspace shell:

```bash
pwd
git branch --show-current
```

Expected: `pwd` is under `.worktrees/` and the branch matches the rail label.

- [ ] **Step 5: Commit**

```bash
git add src/main.c README.md
git commit -m "feat: add worktree rail shortcut"
```

## Task 4: Add Desktop Notification Helper

**Files:**

- Create: `src/desktop_notify.h`
- Create: `src/desktop_notify.c`
- Create: `tests/desktop_notify_tests.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests**

Create tests for escaping:

```c
#include "desktop_notify.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static void test_applescript_escape(void)
{
    char out[256];
    EXPECT_TRUE(desktop_notify_escape_applescript("say \"hi\" \\ done", out, sizeof(out)));
    EXPECT_STR(out, "say \\\"hi\\\" \\\\ done");
}

static void test_empty_message_is_allowed(void)
{
    char out[8];
    EXPECT_TRUE(desktop_notify_escape_applescript("", out, sizeof(out)));
    EXPECT_STR(out, "");
}

int main(void)
{
    test_applescript_escape();
    test_empty_message_is_allowed();
    return failures ? 1 : 0;
}
```

Add a CMake target for `desktop_notify_tests`.

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target desktop_notify_tests
ctest --test-dir build -R desktop_notify_tests --output-on-failure
```

Expected before implementation: compile fails because `desktop_notify.h` does not exist.

- [ ] **Step 3: Implement helper**

Expose:

```c
bool desktop_notify_escape_applescript(const char *input, char *out, int out_size);
bool desktop_notify_agent_ring(const char *workspace, const char *message);
```

Implementation:

- Escaper copies bytes, prefixing `\` and `"` with `\`.
- Return `false` if output would truncate.
- `desktop_notify_agent_ring()` defaults empty `message` to `needs attention`.
- Under `__APPLE__`, build `display notification "<escaped-message>" with title "Fangs" subtitle "<escaped-workspace>"`, then double-fork and `execlp("osascript", "osascript", "-e", script, NULL)`.
- Outside `__APPLE__`, `(void)` inputs and return `false`.

- [ ] **Step 4: Verify helper tests**

Run:

```bash
cmake --build build --target desktop_notify_tests
ctest --test-dir build -R desktop_notify_tests --output-on-failure
```

Expected: `desktop_notify_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/desktop_notify.h src/desktop_notify.c tests/desktop_notify_tests.c CMakeLists.txt
git commit -m "feat: add desktop notification helper"
```

## Task 5: Make Attention Window-Focus Aware

**Files:**

- Modify: `src/main.c`
- Modify: `tests/workspace_status_tests.c`

- [ ] **Step 1: Add status model regression test**

Add this test to `tests/workspace_status_tests.c`:

```c
static void test_active_pane_can_be_marked_when_caller_says_not_focused(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_notify(&st, 10, false, "approve?");

    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    EXPECT_TRUE(strstr(workspace_status_text(&st, 10), "approve?") != NULL);
}
```

This confirms the model already supports the desired behavior when `main.c` passes `focused=false`.

- [ ] **Step 2: Run focused test**

Run:

```bash
cmake --build build --target workspace_status_tests
ctest --test-dir build -R workspace_status_tests --output-on-failure
```

Expected: the new test passes or compile fails only if local test helpers need adjustment.

- [ ] **Step 3: Gate focused flag in `main.c`**

Before the PTY drain pass, compute:

```c
bool window_focused_now = IsWindowFocused();
```

Inside the per-pane loop, replace:

```c
bool focused = (ti == app.active && leaves[i] == active_tab->focused);
```

with:

```c
bool active_pane = (ti == app.active && leaves[i] == active_tab->focused);
bool focused = window_focused_now && active_pane;
```

Pass `focused` into `workspace_status_note_output`, `workspace_status_note_command`, `workspace_status_note_child_exit`, and `workspace_status_note_notify`.

- [ ] **Step 4: Fire desktop notifications on unfocused ring events**

When `nprev != nseq`, after the `workspace_status_note_notify` call:

```c
if (!window_focused_now) {
    char workspace[128];
    workspace_notification_label(ss, workspace, sizeof(workspace));
    desktop_notify_agent_ring(workspace, cmdblocks_notify_text(cb));
}
```

Add a small helper in `main.c` that builds `workspace` from `cmdblocks_title(cb)`, `session_cwd(ss)` via `workspace_cwd_label`, then `shell`.

- [ ] **Step 5: Clear active pane on window focus gain**

In the existing `if (focused != prev_focused)` focus-reporting block, when `focused` is true, clear the currently focused pane:

```c
if (focused && app.n_tabs > 0 && app.active >= 0) {
    Tab *tab = &app.tabs[app.active];
    if (tab->focused && tab->focused->kind == PANE_LEAF) {
        uint64_t id = pane_id_for_session(tab->focused->leaf.session);
        workspace_status_clear(&g_workspace_status, id);
        g_last_focused_pane_id = id;
    }
}
```

- [ ] **Step 6: Verify tests**

Run:

```bash
cmake --build build --target workspace_status_tests desktop_notify_tests
ctest --test-dir build -R "workspace_status_tests|desktop_notify_tests" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 7: Manual smoke**

In Fangs, run:

```bash
printf '\a'
```

Cases:

- Fangs focused, active pane: no unread dot and no macOS notification.
- Fangs unfocused, active pane: unread dot and macOS notification.
- Fangs focused, background pane: unread dot and no macOS notification.

- [ ] **Step 8: Commit**

```bash
git add src/main.c tests/workspace_status_tests.c
git commit -m "feat: notify when unfocused agent rings"
```

## Task 6: Add Working State To Workspace Status

**Files:**

- Modify: `src/workspace_status.h`
- Modify: `src/workspace_status.c`
- Modify: `tests/workspace_status_tests.c`

- [ ] **Step 1: Write failing tests**

Add tests:

```c
static void test_output_marks_working_without_unread_when_focused(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_output_at(&st, 10, true, 42, 1000);

    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
    EXPECT_TRUE(workspace_status_is_working_at(&st, 10, 1500));
}

static void test_working_expires_after_silence(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_output_at(&st, 10, false, 42, 1000);

    EXPECT_TRUE(workspace_status_is_working_at(&st, 10, 2999));
    EXPECT_TRUE(!workspace_status_is_working_at(&st, 10, 3001));
}
```

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target workspace_status_tests
ctest --test-dir build -R workspace_status_tests --output-on-failure
```

Expected before implementation: compile fails because timestamped functions do not exist.

- [ ] **Step 3: Extend model**

In `WorkspaceStatus`, add:

```c
uint64_t last_output_ms[WORKSPACE_STATUS_MAX_PANES];
```

Add API:

```c
#define WORKSPACE_STATUS_WORKING_MS 2000
void workspace_status_note_output_at(WorkspaceStatus *st, uint64_t pane_id,
                                     bool focused, size_t bytes_read,
                                     uint64_t now_ms);
bool workspace_status_is_working_at(const WorkspaceStatus *st, uint64_t pane_id,
                                    uint64_t now_ms);
bool workspace_status_any_working_at(const WorkspaceStatus *st,
                                     const uint64_t *pane_ids, int n,
                                     uint64_t now_ms);
```

Implementation:

- `note_output_at` updates `last_output_ms[idx] = now_ms` whenever `bytes_read > 0`.
- It then applies the existing unread logic only when `focused == false`.
- Existing `workspace_status_note_output()` calls `workspace_status_note_output_at(st, pane_id, focused, bytes_read, 0)`.
- `is_working_at` returns true only if the pane exists, has recorded output, and `now_ms >= last_output_ms && now_ms - last_output_ms <= 2000`.
- Copy/prune/remove paths preserve or shift `last_output_ms`.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
cmake --build build --target workspace_status_tests
ctest --test-dir build -R workspace_status_tests --output-on-failure
```

Expected: `workspace_status_tests` passes.

- [ ] **Step 5: Commit**

```bash
git add src/workspace_status.h src/workspace_status.c tests/workspace_status_tests.c
git commit -m "feat: track recent workspace output"
```

## Task 7: Surface Working State In The Rail

**Files:**

- Modify: `src/ui_workspace_rail_model.h`
- Modify: `src/ui_workspace_rail_model.c`
- Modify: `tests/ui_workspace_rail_model_tests.c`
- Modify: `src/ui_workspace_rail.c`
- Modify: `src/main.c`

- [ ] **Step 1: Write failing rail model test**

Add:

```c
static void test_working_flag_propagates_to_rows(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "agent", .branch = "main", .active = 1, .working = 1 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 2, .label = "agent", .branch = "main", .active = 1, .working = 1 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 1, &st, 0);

    EXPECT_INT(view.tabs[0].working, 1);
    EXPECT_INT(view.panes[0].working, 1);
}
```

- [ ] **Step 2: Run and confirm RED**

Run:

```bash
cmake --build build --target ui_workspace_rail_model_tests
ctest --test-dir build -R ui_workspace_rail_model_tests --output-on-failure
```

Expected before implementation: compile fails because `working` fields do not exist.

- [ ] **Step 3: Extend rail model types**

Add `int working;` to `WorkspaceRailRow` and `WorkspaceRailInput`.

In `workspace_rail_build()`, copy:

```c
row->working = in->working ? 1 : 0;
```

- [ ] **Step 4: Compute working state in main**

Change `collect_rail_inputs()` to accept `uint64_t now_ms`.

For each tab:

- Collect leaf pane IDs.
- Set `ri->tabs[i].working = workspace_status_any_working_at(&g_workspace_status, ids, n, now_ms) ? 1 : 0;`

For each visible pane:

- Set `ri->panes[i].working = workspace_status_is_working_at(&g_workspace_status, pane_id, now_ms) ? 1 : 0;`

In the PTY drain pass:

- Compute `now_ms` once per frame from `clock_gettime(CLOCK_MONOTONIC, &ts)`.
- Replace `workspace_status_note_output(&g_workspace_status, pane_id, focused, stats.bytes_read)` with `workspace_status_note_output_at(&g_workspace_status, pane_id, focused, stats.bytes_read, now_ms)`.
- Pass `now_ms` into both rail input collection calls.

- [ ] **Step 5: Render working marker**

In `ui_workspace_rail.c`:

- Add `working_color()` using `g_ui_theme.subtitle` or `g_ui_theme.accent` with alpha around 120.
- In compact mode, draw working at bottom-right (`x + w - 10`, `row->y + row->h - 10`) with radius 3.
- In full mode, reserve trailing space for a working dot before the attention dot. Draw radius 3 with low alpha.
- Use `GetTime()` only for the alpha pulse; do not feed model state from render.

- [ ] **Step 6: Verify rail tests**

Run:

```bash
cmake --build build --target ui_workspace_rail_model_tests workspace_status_tests
ctest --test-dir build -R "ui_workspace_rail_model_tests|workspace_status_tests" --output-on-failure
```

Expected: both test suites pass.

- [ ] **Step 7: Manual smoke**

In one workspace, run:

```bash
while true; do date; sleep 1; done
```

Expected:

- Working marker appears on that pane/workspace.
- Marker disappears about 2 seconds after `Ctrl+C`.
- A pane with no recent output shows no working marker.

- [ ] **Step 8: Commit**

```bash
git add src/workspace_status.h src/workspace_status.c tests/workspace_status_tests.c src/ui_workspace_rail_model.h src/ui_workspace_rail_model.c tests/ui_workspace_rail_model_tests.c src/ui_workspace_rail.c src/main.c
git commit -m "feat: show workspace working indicators"
```

## Task 8: Final Integration Verification

**Files:**

- Modify: `README.md`
- Modify if useful: `docs/workspace-rail-spec.md`

- [ ] **Step 1: Build all tests**

Run:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: build succeeds and all tests pass.

- [ ] **Step 2: Manual full workflow**

Run:

```bash
./build/fangs
```

Verify:

- `Cmd+T` opens a same-directory workspace.
- Palette `New Worktree Workspace` opens a `.worktrees/<name>` workspace.
- Option/Alt-click rail `+` opens a `.worktrees/<name>` workspace.
- Original checkout and worktree can have independent dirty diffs.
- BEL / OSC notifications while Fangs is unfocused create a macOS notification.
- Working markers appear for streaming output and expire after silence.

- [ ] **Step 3: Check dirty diff**

Run:

```bash
git status --short
git diff -- src tests CMakeLists.txt README.md
```

Expected: only intended files changed.

- [ ] **Step 4: Commit docs/readme finalization**

```bash
git add README.md docs/worktree-workspaces-agent-awareness-spec.md docs/worktree-workspaces-agent-awareness-plan.md docs/handoff-worktree-workspaces-agent-awareness.md
git commit -m "docs: plan worktree workspace awareness"
```

Use this commit only if the planning docs are part of the implementation branch. If the docs were committed before implementation, skip this step.
