# Workspace Ops Handoff

> **Feature:** Ports on rail rows, remote-control socket + `fangs ctl`, rail row ergonomics
> **Primary docs:** `docs/workspace-ops-spec.md` and `docs/workspace-ops-plan.md`
> **Date:** 2026-07-06

## Current State

Working tree is clean at `e40b7c1`. The rail is now a working agent cockpit:

- `ui_workspace_rail_model` owns geometry (`workspace_rail_layout`) and click
  resolution (`workspace_rail_hit`); main.c and the painter share one laid-out
  view — never add separate row math in main.c.
- Attention pipeline: `cmdblocks_osc` reports BEL / OSC 9 / OSC 777 / OSC 0-2;
  `workspace_status` holds per-pane attention, working timestamps
  (`WORKSPACE_STATUS_WORKING_MS`), and jump targets; unfocused rings fire
  macOS notifications via `desktop_notify`.
- Worktree workspaces exist (`workspace_worktree`, Option/Alt-click on `+`,
  `FANGS_ACTION_NEW_WORKTREE_WORKSPACE`, `app_add_tab_named`).
- Rename exists (`ui_rename_prompt`, `Tab.name`, Cmd+Shift+R); label
  precedence is name > OSC title > cwd label, ASCII-sanitized (the font atlas
  is basic-Latin only — no unicode glyphs anywhere in the rail).
- 29 CTest suites pass.

Useful primitives already in the tree: `term_engine_dump_text()` (screen text
for `read`), vendored `cJSON` (protocol), `ai_http.c` (the pthread+mutex
worker pattern to copy), `redact.c`, toast/palette/prompt UI patterns.

## Recommended Scope

Implement the three feature groups in `docs/workspace-ops-plan.md`, in order:

1. **Ports** (Tasks 1–3): pure output scanner per session → clickable chips.
2. **Remote control** (Tasks 4–7): `remote_proto` (pure) → socket thread →
   host executor → `fangs ctl`. Config-gated, off by default.
3. **Ergonomics** (Tasks 8–11): generic `ui_menu` → context menu + armed
   middle-click close → drag reorder → notification history.

Task 12 (libproc port verification) is a stretch — skip unless everything
else is green with time left. The groups are independent; if you must cut,
cut from the back of each group, not the front.

## Important Constraints

- **Never block the render loop.** No `git`, no `lsof`, no socket reads on
  the main thread. One-shot action-time forks (worktree pattern) and worker
  pthreads (ai_http pattern) are the two approved shapes.
- **Pure models first.** Port scanning, protocol parse/build, menu layout,
  drop-index math, and event ring are all testable without raylib — write
  the failing tests before the implementation, repo style.
- **Every new modal/popover must be added to every input-guard chain** in
  main.c that currently lists `ui_rename_prompt_active()` (both the `&& !…`
  and `|| …` forms). Missing one leaks keystrokes to the shell.
- **ASCII only in rail text** (atlas has no other glyphs): badges and `:5173`
  chips, no bell/unicode glyphs.
- `send` / `new --run` are keystroke injection by design — keep them behind
  `remote_api_send`, keep the socket 0600, never TCP.
- Build with `scripts/macos-build.sh` (pinned Zig 0.15.2). A bare cmake
  reconfigure picks PATH Zig and breaks the ghostty build.
- Tab count is capped at `FANGS_MAX_TABS` (9); pane ids are session pointers;
  tabs reorder/compact by struct copy — safe to `memmove`.

## Likely Touch Points

- `src/session.{c,h}`: own the port scanner (mirror CmdBlocks ownership).
- `src/main.c`: host executor for remote commands, `ctl` argv mode before
  raylib init, context-menu/drag/armed-close handling in the pre-draw click
  section, guard-chain extensions.
- `src/ui_workspace_rail_model.{c,h}` + tests: port chips, bell badge, drag
  slots, armed-close row text, new action types.
- `src/config.{c,h}` + `docs/config.example`: `[remote]` gates.
- `CMakeLists.txt`: three new test binaries + new sources.
- `README.md`: ports, remote control (with the orchestrator recipe and the
  security warning), ergonomics.

## Verification

```bash
scripts/macos-build.sh
ctest --test-dir build --output-on-failure
git diff --check
```

Visual smoke: `FANGS_PHASE3_SMOKE_REPORT=/tmp/x.txt \
FANGS_PHASE3_SMOKE_SCREENSHOT=/tmp/x.png ./build/fangs` (2 frames, saves PNG).

Manual acceptance sweep: the checklist at the bottom of
`docs/workspace-ops-spec.md` (dev-server chip appears/clears, `fangs ctl`
round-trips, menu/middle-click/drag/history behaviors, gates actually gate).

## Commit Shape

1. `feat: show dev-server ports on workspace rail rows`
2. `feat: add remote control socket API and fangs ctl`
3. `feat: add rail context menu and armed middle-click close`
4. `feat: drag to reorder workspaces in the rail`
5. `feat: add notification history popover`
6. (stretch) `feat: verify rail ports via libproc on macOS`

Keep docs-only changes folded into the commit that implements them.
