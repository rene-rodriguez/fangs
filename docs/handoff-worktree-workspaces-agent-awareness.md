# Worktree Workspaces And Agent Awareness Handoff

> **Primary docs:** `docs/worktree-workspaces-agent-awareness-spec.md` and `docs/worktree-workspaces-agent-awareness-plan.md`
> **Date:** 2026-07-06

## Current Context

The repo already has workspace rail work in progress:

- `src/workspace_status.{c,h}` tracks unread attention for output, command completions, child exits, and BEL / OSC notifications.
- `src/cmdblocks.{c,h}` and `src/cmdblocks_osc.{c,h}` already expose `cmdblocks_notify_seq()`, `cmdblocks_notify_text()`, and title detection.
- `src/ui_workspace_rail_model.{c,h}` builds tab/pane rail rows and hit targets.
- `src/ui_workspace_rail.c` renders the rail and the `+` button.
- `src/main.c` owns tab creation through `app_add_tab()`, drains PTY output for all tabs, and currently passes a focused boolean into the workspace status model.

The working tree was dirty when this handoff was written, with existing changes in the workspace rail, command-block OSC, action registry, main loop, and tests. Do not reset or overwrite that work. Build on it.

## Implementation Order

Follow `docs/worktree-workspaces-agent-awareness-plan.md` in order:

1. Add `workspace_worktree` helper and tests.
2. Add `New Worktree Workspace` action and host wiring.
3. Add Option/Alt-click on the rail `+`.
4. Add `desktop_notify` helper and tests.
5. Gate attention focus on `IsWindowFocused()` and fire macOS notifications for unfocused rings.
6. Add recent-output working state to `workspace_status`.
7. Surface working state in the rail model and renderer.
8. Run full CMake/CTest plus manual smoke.

## Key Design Constraints

- `git worktree add` runs once in the action handler, not in render, model building, layout, or hover handling.
- Use argv-based `fork`/`execvp` for git and osascript. Do not use `system()` with concatenated shell strings.
- Keep `Cmd+T` and normal rail `+` behavior as same-directory workspace creation.
- The new worktree action should fail visibly outside git repos; it should not silently fall back to a same-directory tab.
- Desktop notifications only fire for BEL / OSC notification events while Fangs is unfocused.
- Focused-pane unread suppression depends on both active pane and window focus.
- Working state is not unread state. It is a recent-output heuristic and expires after about 2 seconds.

## Verification Commands

Use the focused test commands after each task, then finish with:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Manual smoke should cover:

- Worktree workspace creation from the palette.
- Worktree workspace creation from Option/Alt-click on rail `+`.
- Independent dirty diffs between original checkout and created worktree.
- Unfocused active-pane BEL creates unread + macOS notification.
- Streaming output shows a working marker that disappears after silence.

## Handoff Note

Start with the worktree helper. It is the riskiest part because it touches the filesystem and git state. Keep it well tested and make `main.c` call a single helper that either returns a completed worktree path/branch or a displayable error. After that, the notification and working-marker changes are mostly controlled extensions of the existing workspace status and rail model.
