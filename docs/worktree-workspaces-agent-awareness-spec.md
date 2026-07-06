# Worktree Workspaces And Agent Awareness Spec

> **Feature:** isolated git worktree workspaces, desktop notifications, and per-workspace working indicators
> **Date:** 2026-07-06
> **Status:** Ready for implementation planning
> **Companion plan:** `docs/worktree-workspaces-agent-awareness-plan.md`

## Summary

Fangs already has the workspace rail, per-pane attention dots, workspace rename, and BEL / OSC 9 / OSC 777 notification parsing in progress. This spec adds the next layer needed for running several coding agents at once:

1. A first-class **New Worktree Workspace** action that creates a git worktree once, at action time, then opens a new Fangs workspace rooted there.
2. **Desktop notifications** for agent rings while the Fangs window is not focused, plus focus-aware unread logic so the active pane can still become unread while the user is in another app.
3. A lightweight **working / idle marker** based on recent PTY output bytes, separate from unread attention.

The goal is cmux-style multi-agent ergonomics without adding persistence, background daemons, or new dependencies.

## Goals

- Add a command palette action named `New Worktree Workspace`.
- Add Option/Alt-click behavior on the rail `+` button to create a worktree workspace. A normal click still opens the existing same-directory workspace.
- Create git worktrees under `<repo-root>/.worktrees/<name>` from the current repository.
- Create the git worktree exactly once during the action handler, never during render, layout, rail model building, or hover handling.
- Open the new workspace with its shell cwd set to the created worktree path.
- Auto-fill the new workspace name from the generated branch/worktree name so the rail row is immediately meaningful.
- Keep existing `Cmd+T` / `Ctrl+Shift+T` behavior as a same-directory workspace action.
- When a pane emits BEL / OSC 9 / OSC 777 and the Fangs window is unfocused, fire a macOS desktop notification using a forked `osascript`.
- Treat the active pane as background for unread purposes while the Fangs window is not focused.
- Show a subtle working marker for rows with output bytes in the last 2 seconds.

## Non-Goals

- Do not add persistent workspace restore.
- Do not add a branch-name prompt in the first implementation.
- Do not move existing tabs into worktrees.
- Do not run `git worktree add` from any immediate-mode UI render path.
- Do not add notification dependencies or cross-platform notification frameworks.
- Do not notify for ordinary background output; desktop notifications are only for explicit agent rings.
- Do not infer process running state from shell jobs, process groups, or command-block lifecycle. The first working indicator is output-byte based only.

## UX

### New Worktree Workspace

Entry points:

- Command palette: `New Worktree Workspace`.
- Rail `+` button: Option-click on macOS, Alt-click on Linux.

Normal `+` click and `Cmd+T` keep the current behavior: open a new workspace in the focused pane's cwd.

On success:

- Fangs switches to the new workspace.
- The shell starts in `<repo-root>/.worktrees/<name>`.
- The rail label is set to `<name>` through the existing `Tab.name` field. The branch line should also show the same branch once `workspace_git_branch()` resolves the cwd.
- A short toast may say `Created worktree <name>`.

On failure:

- Fangs does not create a tab.
- Fangs shows a toast with a concise error, for example:
  - `Not inside a git repository`
  - `Could not create worktree: branch already exists`
  - `Could not create worktree: git exited 128`

### Name And Branch Generation

The first implementation generates a unique branch and path; it does not prompt.

Inputs:

- Current cwd: `session_cwd(active_session)`.
- Repo root: `git -C <cwd> rev-parse --show-toplevel`.
- Current branch: `git -C <cwd> branch --show-current`.

Rules:

- If the current branch is `feature/agent-ui`, the base name is `feature-agent-ui-agent`.
- If the current branch is `main`, the base name is `main-agent`.
- If the current branch is detached or empty, the base name is `worktree-agent`.
- Candidate names use `[A-Za-z0-9._-]`; every other byte becomes `-`; repeated `-` runs collapse; leading and trailing separators are trimmed.
- If `<repo-root>/.worktrees/<candidate>` exists or `refs/heads/<candidate>` exists, try `candidate-2`, then `candidate-3`, and so on.
- Cap candidates at 64 display bytes before the numeric suffix.

Command:

```bash
git -C <repo-root> worktree add -b <candidate> <repo-root>/.worktrees/<candidate> HEAD
```

The app must execute this through `fork`/`execvp` or equivalent argv-based process APIs, not through `system()` or shell string concatenation.

After successful creation, append `.worktrees/` to `<repo-root>/.git/info/exclude` if it is not already present. This is best-effort; failure to update the exclude file should not fail the workspace action.

### Desktop Notifications

When the BEL / OSC notification sequence advances for a pane and `IsWindowFocused()` is false, Fangs should fire a macOS notification:

```applescript
display notification "<message>" with title "Fangs" subtitle "<workspace>"
```

Implementation constraints:

- macOS only: compile the notification implementation under `__APPLE__`.
- Other platforms compile a no-op function that returns `false`.
- Use forked `osascript -e <script>`.
- Escape AppleScript string content for backslash and double quote.
- Do not block the render loop waiting for the notification process beyond the minimal double-fork parent reap.
- If `osascript` is missing or fails, ignore it; the unread dot still records the event.

Notification content:

- Message: the OSC text, or `needs attention` for bare BEL.
- Subtitle: prefer the workspace custom name, then OSC title, then cwd label, then `shell`.

### Window-Focus-Aware Unread

Current attention logic receives a `focused` boolean and suppresses unread state for focused panes. That boolean must become an effective focus flag:

```c
bool window_focused = IsWindowFocused();
bool active_pane = (ti == app.active && leaves[i] == active_tab->focused);
bool effective_focused = window_focused && active_pane;
```

Use `effective_focused` for output, command completion, child exit, and notify events. This means:

- Active pane output while Fangs is focused stays read.
- Active pane BEL / OSC notification while Fangs is focused stays read.
- Active pane BEL / OSC notification while Fangs is unfocused becomes unread and fires a desktop notification.
- Hidden/background pane events behave as they do today, with desktop notification only if Fangs is unfocused.

When the window transitions from unfocused to focused, clear attention on the currently focused pane because it is visible again. Hidden panes keep their unread status until the user focuses or jumps to them.

### Working / Idle Marker

The working marker is independent from unread attention:

- Any `session_feed_pty_stats()` call with `bytes_read > 0` records `last_output_ms` for that pane.
- A pane is working when `now_ms - last_output_ms <= 2000`.
- A tab/workspace is working when any leaf pane in that tab is working.
- Working state does not set notification text.
- Working state does not clear when focused.
- Working state expires naturally after silence.

Rail rendering:

- Full rail: draw a small dim pulsing dot on the trailing side of the row. If an attention dot is also present, reserve room for both markers.
- Compact rail: keep attention in the top-right corner and draw working in the bottom-right corner.
- Use existing theme colors with low alpha; it should read as activity, not as an alert.

## Architecture

### `src/workspace_worktree.{c,h}`

New module responsible for git worktree creation. This module performs action-time subprocess work and exposes pure helpers for tests.

API:

```c
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
```

Responsibilities:

- Resolve the repository root and branch with `git`.
- Generate a unique candidate branch/path.
- Create `.worktrees` if needed.
- Run `git worktree add -b <candidate> <path> HEAD`.
- Best-effort append `.worktrees/` to `.git/info/exclude`.
- Return enough data for `main.c` to open the new workspace and set its display name.

### `src/desktop_notify.{c,h}`

New notification helper.

API:

```c
bool desktop_notify_escape_applescript(const char *input, char *out, int out_size);
bool desktop_notify_agent_ring(const char *workspace, const char *message);
```

Responsibilities:

- Build a safe AppleScript snippet.
- On macOS, double-fork and `execlp("osascript", "osascript", "-e", script, NULL)`.
- On non-macOS, return `false` without side effects.

### `src/workspace_status.{c,h}`

Extend the existing model rather than creating a second status model.

Additions:

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

Store `last_output_ms` alongside existing pane entries. Keep `workspace_status_note_output()` as a compatibility wrapper for existing tests and callers, but switch `main.c` to the timestamped API.

### `src/ui_workspace_rail_model.{c,h}`

Extend row/input data by adding `int working` to both structs:

```c
typedef struct {
    /* existing WorkspaceRailRow fields */
    int working;
} WorkspaceRailRow;

typedef struct {
    /* existing WorkspaceRailInput fields */
    int working;
} WorkspaceRailInput;
```

The model only propagates the boolean. Main computes whether a tab or pane is working because tab working state is an aggregate over all leaf panes, while a tab's attention ID currently points at the highest-attention representative pane.

### `src/ui_workspace_rail.c`

Render working markers without changing hit targets.

### `src/action_registry.{c,h}`

Add:

```c
FANGS_ACTION_NEW_WORKTREE_WORKSPACE
```

Registry entry:

- name: `workspace.new_worktree`
- label: `New Worktree Workspace`
- detail: `Create a git worktree and open a workspace there`
- shortcut: empty for now

### `src/main.c`

Main remains orchestration:

- Keep `app_add_tab()` as the normal same-directory path.
- Add `app_add_tab_named()` with the same parameters as `app_add_tab()` plus `const char *name` immediately after `const char *cwd`.
- Add a host action handler for `FANGS_ACTION_NEW_WORKTREE_WORKSPACE`.
- Add a shared helper `open_worktree_workspace_from_current()` used by the palette action and Option/Alt-click.
- Compute `window_focused` once before the PTY drain pass and pass `effective_focused` into workspace status updates.
- On notification sequence changes, call `desktop_notify_agent_ring()` only when `window_focused == false`.
- On window focus gain, clear the active pane's unread attention.
- Compute `now_ms` once per frame and pass it to `workspace_status_note_output_at()` and `collect_rail_inputs(now_ms)`.

## Error Handling

- Check `app.n_tabs < FANGS_MAX_TABS` before creating a worktree. If no tab slot is available, show a toast and do not run git.
- If worktree creation succeeds but `session_create()` fails, best-effort remove the newly created worktree with:

```bash
git -C <repo-root> worktree remove --force <path>
```

Then show a toast. This cleanup must only run for the path just created by the failed action.

- If the cwd is not a git repository, do not fall back to normal tab creation. The explicit worktree action should fail visibly.
- If branch/path candidate generation exhausts suffixes, show a toast and do not create a tab.

## Testing

Unit tests:

- `workspace_worktree_tests`: name sanitization, candidate generation, actual local `git init` + `git worktree add` happy path, non-repo failure.
- `desktop_notify_tests`: AppleScript escaping and non-empty script construction. Do not require Notification Center permissions and do not run `osascript` in tests.
- `workspace_status_tests`: focused output records working without unread, working expires after 2 seconds, active-pane output while window-unfocused caller passes `focused=false` and marks unread.
- `ui_workspace_rail_model_tests`: pane working propagates to row; tab working can be provided independently from attention.
- `action_registry_tests`: new action is registered, unique, and described.

Manual verification:

1. Open Fangs in a git repository.
2. Option/Alt-click the rail `+`.
3. Confirm a new tab opens in `.worktrees/<name>` and `git branch --show-current` prints `<name>`.
4. Confirm `git status --short` in the original checkout is not polluted by edits in the worktree.
5. Trigger BEL or OSC 9 from a background pane while Fangs is focused; confirm unread dot appears and no macOS notification appears.
6. Switch to another app, trigger BEL or OSC 9 in the active pane; confirm unread dot appears and macOS notification fires.
7. Run a command that streams output; confirm the working marker appears within 2 seconds and disappears after silence.

## Acceptance Criteria

- `New Worktree Workspace` creates one git worktree at action time and opens a new workspace there.
- Normal new-workspace behavior remains unchanged.
- Worktree paths are under `.worktrees/` and are excluded from normal git status best-effort.
- Window-unfocused active-pane rings produce unread state and macOS notifications.
- Desktop notifications do not repeat every frame for the same ring event.
- Working markers reflect recent output bytes and expire after about 2 seconds of silence.
- All new unit tests and existing affected tests pass through CTest.
