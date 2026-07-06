# Workspace Rail Handoff

> **Feature:** Vertical tabs plus notification rings
> **Primary docs:** `docs/workspace-rail-spec.md` and `docs/workspace-rail-plan.md`
> **Date:** 2026-07-06

## Current State

Fangs already has the foundations this feature needs:

- Tabs and splits live in `src/main.c` through `App`, `Tab`, and `PaneNode`.
- `src/pane.{c,h}` provides pure split-tree operations and leaf collection.
- Each pane leaf owns a `Session`.
- `session_cwd()` exposes the current working directory from OSC 7 with a cached fallback.
- `src/layout.{c,h}` already isolates terminal and AI sidebar rectangles.
- `src/cmdblocks.{c,h}` already sees OSC 133 command completion and exit code.
- `src/ui_toast.{c,h}` handles transient toasts, but this feature needs persistent per-pane attention rather than short-lived toast state.

The working tree was clean before these docs were added.

## Recommended Scope

Implement **Workspace Rail v1**:

- Left rail for tabs and active-tab panes.
- CWD basename and git branch labels.
- Click-to-switch for tab rows and click-to-focus for pane rows.
- Notification rings for background output, failed background commands, and background child exits.
- Command palette action to toggle the rail.

Do not implement ports in v1. Reliable port support needs a provider that can inspect processes or parse structured output without blocking the render loop.

## Implementation Order

Follow `docs/workspace-rail-plan.md` in order:

1. `workspace_info`: cwd labels and git branch discovery.
2. `workspace_status`: pure attention state.
3. Rail-aware layout.
4. Cheap event sources from `CmdBlocks` and `Session`.
5. Pure rail presentation model.
6. Raylib rail UI plus `main.c` integration.
7. README/config docs and full verification.

This order keeps the risky UI work until after pure behavior has tests.

## Important Constraints

- Keep planning docs for this feature directly under `docs/`.
- Do not shell out to `git` in the render loop.
- Do not infer ports from random output in v1.
- Do not clear every pane in a tab when switching tabs. Clear only the pane that receives focus.
- Do not forward rail clicks to terminal selection, URL click handling, scrollbars, or command-block hover buttons.
- Do not resize terminal content by overlaying the rail. Allocate a real layout rectangle and reflow the terminal grid.

## Likely Touch Points

- `src/main.c`: app orchestration, event collection, focus switching, drawing.
- `src/layout.{c,h}` and `tests/layout_tests.c`: left rail rect.
- `src/session.{c,h}`: feed stats.
- `src/cmdblocks.{c,h}`: completion sequence and latest exit code.
- `src/config.{c,h}` and `docs/config.example`: `workspace_rail = true`.
- `src/action_registry.{c,h}`: palette action.
- `CMakeLists.txt`: new modules and tests.
- `README.md`: feature and config docs.

## Verification

Use the normal repo checks:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

Manual smoke:

1. Launch `./build/fangs`.
2. Create a second tab.
3. Split a pane.
4. Confirm the rail shows tab and pane rows.
5. Click rows and verify focus changes.
6. Run a command that produces output in a background pane and verify an info ring appears.
7. Run `false` or another failing command in a background pane with shell integration enabled and verify a warn ring appears.
8. Focus that pane and verify the ring clears.
9. Open the AI sidebar and confirm terminal content reflows between rail and sidebar.

## Commit Shape

Recommended commits:

1. `feat: add workspace info helpers`
2. `feat: add workspace attention model`
3. `feat: add workspace rail layout`
4. `feat: expose workspace activity events`
5. `feat: add workspace rail model`
6. `feat: add workspace rail UI`
7. `docs: document workspace rail`

Keep docs and implementation commits separate if you need to hand off again before UI integration.
