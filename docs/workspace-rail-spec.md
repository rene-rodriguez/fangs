# Workspace Rail Spec

> **Feature:** Vertical tabs plus notification rings
> **Date:** 2026-07-06
> **Status:** Ready for implementation planning
> **Companion plan:** `docs/workspace-rail-plan.md`

## Summary

Build a left-side workspace rail that makes tabs, panes, project context, and background activity visible without opening a modal. The rail should answer three questions at a glance:

1. Which tab and pane am I in?
2. What project directory and git branch does each useful target represent?
3. Which background target needs attention?

This is the next high-value feature because Fangs already supports tabs, splits, command blocks, AI, and runbooks. The missing layer is workspace awareness. A rail gives those existing features a persistent control surface and creates the status model needed for later automation, browser splits, and socket APIs.

## Goals

- Add a left vertical rail with rows for tabs and panes.
- Show each row's working directory label from `session_cwd()`.
- Show a git branch when the active directory is inside a git worktree.
- Show notification rings for background output and failed background commands.
- Show one concise notification text line for the highest-priority unread event.
- Let users click a tab or pane row to switch focus.
- Preserve terminal behavior: the rail never forwards its clicks to the PTY, and it never overlaps terminal rendering.
- Keep the first slice testable with pure models for git labels, layout allocation, and attention state.

## Non-Goals

- Do not build the in-app browser in this slice.
- Do not add the CLI or socket API in this slice.
- Do not implement reliable port discovery yet. Ports need a separate provider that can inspect processes or parse structured app output without blocking the render loop.
- Do not persist workspaces or restore sessions.
- Do not add drag-reorder, rename, or close buttons in the first build.
- Do not auto-send AI prompts from rail notifications.

## UX

### Placement

The rail lives on the left edge. The AI sidebar remains on the right. The terminal grid occupies the rectangle between them.

Recommended defaults:

- Full rail width: `260` logical px.
- Compact rail width: `56` logical px.
- Minimum terminal width: keep the existing `320` logical px floor.
- Full rail appears when window width can fit full rail plus terminal plus any visible AI sidebar.
- Compact rail appears when the full rail cannot fit but `56 + min_terminal_w` can fit.
- Rail hides when the window cannot preserve the minimum terminal width.

The rail should use the existing derived UI theme colors from `ui_theme.{c,h}`. It should feel like operational chrome, not a separate panel: dark or light background derived from the terminal theme, thin separators, stable row heights, and no nested cards.

### Sections

The rail has three stacked regions:

- **Notification line:** one row near the top. Shows the most severe unread event, for example `T2 P1 failed: exit 1` or `T3 new output`.
- **Tabs:** one row per tab, up to `FANGS_MAX_TABS`. Each row shows tab number, project label, branch, and ring state.
- **Panes:** rows for the active tab's leaf panes. Each row shows pane number, project label, branch, and ring state.

In compact mode, the rail shows row numbers and rings only. Tooltips can come later; the first slice should avoid hover-only critical information.

### Row Labels

Use compact labels so text fits:

- `workspace_cwd_label("/Users/rene/src/fangs", "/Users/rene", out)` returns `fangs`.
- `workspace_cwd_label("/Users/rene", "/Users/rene", out)` returns `~`.
- `workspace_cwd_label("/tmp/build", "/Users/rene", out)` returns `build`.
- If branch exists, render `label branch`, for example `fangs main`.
- Truncate from the middle or end at draw time if the measured text exceeds the row width.

### Git Branch

Resolve the branch without shelling out to `git`:

1. Walk upward from `cwd` until `.git` exists.
2. If `.git` is a directory, read `.git/HEAD`.
3. If `.git` is a file, parse `gitdir: <path>` and read that target's `HEAD`.
4. If `HEAD` starts with `ref: refs/heads/`, show the branch name after that prefix.
5. If `HEAD` contains a detached commit hash, show the first seven characters.
6. If anything fails, omit branch text.

This keeps the render loop non-blocking and avoids depending on a user PATH.

### Notification Rings

Each tab and pane row has a small ring or dot on the leading edge.

Levels:

- `none`: no unread activity.
- `info`: background pane produced output.
- `warn`: background command finished with a non-zero exit code.
- `error`: background session exited.

Rules:

- Activity in the focused pane does not create unread attention.
- New output in any background pane marks that pane `info`.
- If command blocks report a new completion with `exit_code != 0` in a background pane, mark that pane `warn` and set text `command failed: exit N`.
- If a child exits in a background pane, mark that pane `error` and set text `process exited: N`.
- The tab ring shows the highest level among its panes.
- Focusing a pane clears that pane's unread status.
- Switching to a tab does not clear all panes by itself. Only the focused pane clears. This prevents hidden split panes from losing attention when the user lands on the tab.

Command failure detection should come from a cheap command-block status API, not from formatting output every frame. Add a completion sequence to `CmdBlocks`, increment it when OSC 133 `D;<code>` arrives, and let the workspace status model compare that sequence with its last-seen value for each pane.

Background-output detection should come from `session_feed_pty()` reporting bytes read. If a non-focused session reads bytes this frame, the main loop reports an output event to the status model.

### Click Behavior

- Clicking a tab row switches to that tab and syncs active runtime handles.
- Clicking a pane row focuses that pane inside the active tab and syncs active runtime handles.
- Clicking the rail consumes the click. It should not start terminal selection, scrollbar drag, command-block hover actions, or URL clicks.
- Keyboard shortcuts for existing tab and pane actions remain unchanged.
- Add a command palette action named `Toggle Workspace Rail`. The first slice does not need a global shortcut; users can toggle from `Cmd+P` / `Ctrl+Shift+P`.

### Config

Add one config key:

```ini
[ui]
workspace_rail = true
```

Default to `true`. The rail may still compact or hide on narrow windows to preserve the terminal minimum width. Settings UI can expose this later; the first slice only needs config load/save and command palette toggle.

## Architecture

Add small focused modules instead of expanding `main.c` further.

### `src/workspace_info.{c,h}`

Pure-ish filesystem helpers:

- `workspace_cwd_label(const char *cwd, const char *home, char *out, int out_size)`
- `workspace_git_branch(const char *cwd, char *out, int out_size)`

This module does no Raylib work and has dedicated tests with temporary directories.

### `src/workspace_status.{c,h}`

Pure attention model:

- Tracks attention per pane ID.
- Records output events, command completions, child exits, and focus clears.
- Computes tab-level highest attention.
- Exposes the highest-priority notification text.

Use a stable pane key derived from the session pointer in `main.c` for the first implementation. The model itself should accept a `uint64_t pane_id` so tests can use fixed numeric IDs.

### `src/ui_workspace_rail_model.{c,h}`

Pure presentation model:

- Builds tab and pane row descriptors from app/session snapshots.
- Applies compact/full row text rules.
- Keeps click-hit targets separate from rendering.

### `src/ui_workspace_rail.{c,h}`

Raylib rendering and mouse hit handling:

- Draws the left rail inside `Layout.rail`.
- Returns an action such as `switch tab 2` or `focus pane 1`.
- Uses `Font`, `UiTheme`, and row data from `ui_workspace_rail_model`.

### `src/layout.{c,h}`

Extend layout with rail allocation while preserving the existing `layout_compute()` behavior for compatibility. Add a new function:

```c
Layout layout_compute_with_rail(int window_w, int window_h,
                                bool rail_enabled, int rail_width, int rail_compact_width,
                                bool sidebar_visible, int sidebar_width,
                                int pad, int min_terminal_w);
```

Add fields to `Layout`:

```c
Rect rail;
bool rail_visible;
bool rail_compact;
```

`layout_compute()` can call `layout_compute_with_rail()` with `rail_enabled = false`.

### `src/cmdblocks.{c,h}`

Add a lightweight completion API:

```c
unsigned long cmdblocks_completion_seq(const CmdBlocks *cb);
int cmdblocks_latest_exit_code(const CmdBlocks *cb);
```

Increment the sequence when OSC 133 `D` arrives. The workspace status model uses this to detect a new command completion without allocating `CmdBlockAction`.

### `src/session.{c,h}`

Add feed stats:

```c
typedef struct {
    size_t bytes_read;
    bool eof;
    bool error;
} SessionFeedStats;

SessionFeedStats session_feed_pty_stats(Session *s);
```

Keep `session_feed_pty(Session *s)` as a wrapper for existing callers.

### `src/main.c`

Main remains the orchestrator:

- Allocate the rail rectangle through `layout_compute_with_rail()`.
- Feed all sessions, collect `SessionFeedStats`, and report background output events.
- Compare each pane's command-block sequence and report failed background command events.
- Clear attention when a pane receives focus.
- Build a rail view model each frame.
- Draw the rail before terminal panes.
- Apply rail click actions before terminal mouse handling.
- Add the command palette action to toggle `cfg.workspace_rail`.

## Error Handling

- Missing or unreadable `.git/HEAD`: render no branch.
- Long path or branch: truncate for display, never resize the row.
- Session pointer missing during tab close: drop its status entry during pruning.
- Child exit in active pane: existing exit banner remains primary; rail does not need to duplicate active-pane attention.
- Window too narrow: compact or hide rail to preserve terminal usability.

## Tests

Add or extend:

- `workspace_info_tests`: cwd labels, normal branch, detached HEAD, `.git` file worktree pointer, no repo.
- `workspace_status_tests`: background output marks info, failed background command marks warn, active events do not mark unread, focus clears one pane, tab aggregate picks highest level.
- `layout_tests`: rail hidden/full/compact, rail plus sidebar preserves min terminal width, terminal x starts after rail.
- `ui_workspace_rail_model_tests`: row ordering, active markers, notification text selection, compact mode text suppression.
- Existing full suite: `ctest --test-dir build --output-on-failure`.

## Acceptance

- Rail appears on the left on a normal desktop-width window.
- Creating tabs and splits updates rail rows without restart.
- Clicking a tab row switches tabs.
- Clicking a pane row focuses that pane.
- Background output creates an unread info ring.
- A failed command in a background pane creates a warn ring and notification text.
- Focusing the affected pane clears its ring.
- Branch and cwd labels render for normal git repositories without invoking `git`.
- Narrow windows compact or hide the rail and keep the terminal usable.
- AI sidebar still opens on the right and the terminal grid reflows between rail and sidebar.
- The command palette can toggle the rail.

## Deferred Slice: Ports

Ports belong in a second pass. The UI can reserve the concept, but the first implementation should not show guessed ports.

Recommended next design:

- Add `workspace_ports.{c,h}` with a provider interface.
- macOS/Linux provider can use process inspection, not a blocking shell command in the render loop.
- A parser can also detect common server lines such as `localhost:5173` from command output.
- The rail can show ports only when the provider reports confidence and owning pane.
