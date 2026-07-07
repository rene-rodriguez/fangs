# Workspace Ops Spec — Ports, Remote Control, Rail Ergonomics

> **Feature:** Port awareness (7), socket API + CLI (8), rail row ergonomics (9)
> **Date:** 2026-07-06
> **Status:** Ready for implementation planning
> **Companion plan:** `docs/workspace-ops-plan.md`
> **Prior slices:** `docs/workspace-rail-spec.md`, `docs/worktree-workspaces-agent-awareness-spec.md`

## Summary

The rail now answers "who needs me?" (attention rings, desktop notifications,
jump-to-unread) and "who is busy?" (working dots), and worktree workspaces give
each agent an isolated checkout. Three gaps remain for running many Claude Code
agents comfortably:

1. **Ports (7):** when an agent starts a dev server you can't tell which
   workspace owns `localhost:5173` without hunting. Show high-confidence ports
   on rail rows; click to open in the browser.
2. **Remote control (8):** there is no way for an agent (or a script) to drive
   fangs — create worktree workspaces, list their status, send input, read
   screens. A Unix-socket JSON API plus a `fangs ctl` CLI unlocks the
   orchestrator pattern: one Claude Code instance spawning and supervising the
   others.
3. **Rail ergonomics (9):** everything on a row today is a single left-click.
   Add a right-click context menu, safe middle-click close, drag-to-reorder,
   and a notification history popover.

Each feature is an independent slice; nothing in one blocks the others.

## Goals

- Parse dev-server announcements from PTY output into per-pane port claims,
  purely and chunk-safely; render them as clickable chips on rail rows.
- Expose a local JSON-over-Unix-socket API (`list`, `new`, `focus`, `rename`,
  `send`, `read`, `ring`) with a `fangs ctl` CLI front-end, gated by config,
  off by default.
- Right-click context menu on rail rows (Rename, New Worktree Here, Close).
- Middle-click close with a two-click arm/confirm (agents may be running).
- Drag a workspace row to reorder tabs.
- Notification history popover (last 32 attention events, click to jump).
- Keep every new decision-making surface a pure, tested model; raylib layers
  only paint; main.c only orchestrates.

## Non-Goals

- No Linux port verification in this slice (parser works everywhere; the
  optional libproc verifier is macOS-only and explicitly a stretch rung).
- No TCP listener, no auth tokens — the socket is filesystem-permission-gated
  local IPC only, like tmux/kitty remote control.
- No multi-client concurrency on the socket (serve one client at a time).
- No pane-level rename, no drag of pane rows, no drag between windows.
- No hover tooltips (the font atlas / interaction model stays as-is).
- No auto-launching browsers on port detection — click only.

---

## Feature 7: Ports

### UX

- A rail row's secondary line currently shows `branch` (or the attention
  text). Up to **3 port chips** render right-aligned on the secondary line,
  e.g. `:3000` `:4000` `:6006` (a monorepo `npm run dev` may announce app,
  API, and Storybook from one pane). Chips are fixed-width (fits `:65535`),
  so the pure layout can compute their rects without font measurement; the
  secondary text truncates earlier when chips are present.
- Chip colors: dim chrome (subtitle-tinted background); hover brightens.
- Left-click a chip opens `http://localhost:<port>` via the existing URL
  opener (`open` / `xdg-open` path already used for Ctrl/Cmd+click on URLs).
- Tab rows aggregate: a tab shows the focused pane's ports (falling back to
  the first leaf), same as labels do. Pane rows show their own.
- Compact rail shows no chips.

### Detection (Rung A — output parser, all platforms)

New pure module `workspace_ports.{c,h}`:

- `WorkspacePortScanner` — chunk-safe scanner state fed with raw PTY bytes,
  mirroring `CbParser`'s design (a match can straddle feed chunks; keep a
  small carry buffer, no allocation, no syscalls).
- Recognized patterns, case-insensitive on the host part, high confidence
  only:
  - `localhost:<port>`
  - `127.0.0.1:<port>`
  - `0.0.0.0:<port>`
  - `[::1]:<port>`
- `<port>` is 1–65535 with a non-digit boundary after it. Anything else
  (bare `:3000`, IPs like `10.x`, `host.docker.internal`) is ignored — false
  positives are worse than misses.
- Per scanner: up to `WORKSPACE_PORTS_MAX` (6) distinct ports, LRU-replaced,
  deduped. ANSI/OSC bytes pass through the scanner unmodified (it only
  observes, like the OSC scanner).
- Each `Session` owns one scanner (exactly like it owns a `CmdBlocks`);
  `session_feed_pty` feeds it the same bytes. Accessor:
  `session_ports(const Session *s, int *out, int max)`.

### Lifetime rules

- Ports are claims about the **current foreground command**. Clear a pane's
  ports when:
  - a new OSC 133 `D` completion arrives (the server exited back to the
    prompt) — main.c already polls `cmdblocks_completion_seq` per pane, so
    the clear hooks into that existing loop;
  - the child exits or the session respawns.
- No wall-clock expiry in v1 — a long-running server keeps its chip.

### Rung B (stretch, macOS-only): libproc verification

Optional `workspace_ports_verify.{c,h}`: a worker pthread (mirroring
`ai_http.c`'s mutex+thread pattern) samples every ~2 s: for each session's
child pid, walk descendant pids, list their sockets via
`proc_pidinfo(PROC_PIDLISTFDS)` + `proc_pidfdinfo(PROX_FDTYPE_SOCKET)`, and
publish LISTEN tcp ports under a mutex. Main thread merges: parsed ports that
verify show solid; parsed ports that don't verify are dropped after 10 s;
verified-but-unparsed ports are added. **Do this rung only if the parser rung
lands cleanly with time to spare; ship parser-only otherwise.**

---

## Feature 8: Remote control — socket API + `fangs ctl`

### Threat model / gating

`send` and `new --run` type bytes into your shell — that is arbitrary command
execution by design, same trust model as `tmux send-keys` or kitty remote
control. Therefore:

- Config gates, both default **false**:

```ini
[remote]
remote_api = false        # enables the socket + read-only/benign commands
remote_api_send = false   # additionally enables send and new --run
```

- Socket dir `~/.config/fangs/` (0700), socket file mode 0600.
- Socket path: `remote-<pid>.sock`, plus a `remote.sock` symlink pointing at
  the most recently started instance. Both unlinked on clean exit; a stale
  symlink is re-pointed on startup.
- Never a TCP listener. No option to change that.

### Protocol

JSON Lines over the Unix socket: one request object per line in, one response
object per line out. cJSON (already vendored in `src/`) handles both
directions.

Request: `{"id": 1, "cmd": "list"}` — `id` is echoed in the response.
Response: `{"id": 1, "ok": true, ...}` or `{"id": 1, "ok": false, "error": "…"}`.

Commands (v1):

| cmd | args | effect / reply payload |
|---|---|---|
| `list` | — | `workspaces`: array of `{index, name, label, title, branch, cwd, active, working, attention, ports, panes}` |
| `new` | `cwd?`, `worktree?` (bool), `name?`, `run?` | create workspace (worktree via `workspace_worktree_create`); optional `run` types `<cmd>\n` into the new shell (**requires remote_api_send**); replies `{index, cwd, branch}` |
| `focus` | `index`, `pane?` | switch tab / focus pane |
| `rename` | `index`, `name` | set `Tab.name` ("" resets to auto) |
| `send` | `index`, `text` | write text to the tab's focused pane PTY (**requires remote_api_send**; no implicit newline — callers append `\n` themselves) |
| `read` | `index`, `lines?` | plain-text screen dump of the tab's focused pane via `term_engine_dump_text()`, last `lines` lines (default 200), reply `{text}` |
| `ring` | `index`, `message?` | mark attention on that tab's focused pane (for agent hooks / scripts) |

Errors: unknown cmd, bad index, `remote_api_send` required, tab limit reached
(`FANGS_MAX_TABS` is 9), worktree failure (propagate
`WorkspaceWorktreeResult.error`).

### Architecture

- `remote_proto.{c,h}` — **pure**: parse a request line into a
  `RemoteRequest` struct; build response JSON strings from plain C structs.
  All protocol tests live here, no sockets or threads needed.
- `remote_api.{c,h}` — the socket owner: worker pthread accepts one client at
  a time, reads lines, pushes `RemoteRequest`s onto a mutex-guarded inbox;
  main loop calls `remote_api_poll()` once per frame, executes at most a few
  requests against `App` state (single-threaded, same as every other host
  action), and posts replies to an outbox the thread flushes. Mirrors the
  `ai_http.c` mutex/thread pattern.
- `main.c` — a `remote_execute(const RemoteRequest*, RemoteReply*)` host
  handler next to `apply_palette_action`, reusing the existing helpers
  (`app_add_tab_named`, `workspace_worktree_create`, `app_switch_tab`,
  `pty_write`, `term_engine_dump_text`, `workspace_status_note_notify`).
- CLI mode in `main()`: `fangs ctl <cmd> [args…]` connects to
  `~/.config/fangs/remote.sock` (or `--socket PATH`), builds the JSON request
  from argv, prints the response, exits non-zero on `ok:false` or connect
  failure. No window, no raylib init. Examples:

```sh
fangs ctl list
fangs ctl new --worktree --name fix-auth --run "claude"
fangs ctl send 2 $'run the tests\n'
fangs ctl read 2 --lines 80
fangs ctl ring 2 "review me"
```

### Orchestrator recipe (README)

Document the pattern explicitly: enable both gates, run a Claude Code
orchestrator in workspace 1, and give it `fangs ctl` — it can then spawn
worktree workspaces for sub-agents, poll `list` for `working`/`attention`,
`read` their screens, and `send` follow-ups. This is the cmux orchestration
story on fangs' own socket.

---

## Feature 9: Rail row ergonomics

### Context menu (right-click)

- New generic component `ui_menu.{c,h}`: a popover list with a **pure model**
  (items, anchor point, computed rects, hit test — same discipline as
  `ui_workspace_rail_model`) and a raylib draw layer. It is reused by the
  notification history below.
- Right-click a workspace row → `Rename…`, `New Worktree Here`,
  `Close Workspace`. Right-click a pane row → `Focus`, `Close Pane`.
- Menu opens anchored at the click, clamped to the window; Esc, click-away,
  or any action closes it. While open it captures clicks/keys (add
  `ui_menu_active()` to the same input guards that gate the other modals).
- `Rename…` opens the existing rename prompt for that tab (not just the
  active one — `ui_rename_prompt_open(index, name)` already takes an index).
- `New Worktree Here` runs the existing worktree action but seeded from the
  clicked tab's cwd.
- `Close Workspace` / `Close Pane` need host generalizations:
  `app_close_tab(int idx)` and close-pane-by-id (today only
  `app_close_active` exists). Closing kills live children (sessions own
  them) — that is what the confirm-styling below is for.

### Middle-click close (armed)

- Middle-click a workspace row once: the row enters an **armed** state for
  2 s — its secondary line shows `click again to close` in the warn color.
- A second middle-click within the window closes it; anything else (timeout,
  other clicks, keyboard) disarms. No silent kills of a workspace that might
  be running an agent.
- Armed state lives in main (`pane_id` + deadline), passes into the view as a
  per-row flag; the model stays pure.

### Drag-to-reorder workspaces

- Left-press on a workspace row + vertical drag past 6 px enters drag mode
  (suppresses the click action on release).
- While dragging, the rail draws an insertion line between rows at the drop
  slot; pure helper `workspace_rail_drop_index(view, my)` returns the slot.
- On release, reorder `app.tabs` by struct move (Tab structs are
  self-contained — sessions, names travel; the pane-id/status maps key on
  session pointers and are unaffected), update `app.active` to follow the
  moved selection. `Cmd+1..9` targets follow the new order (numbers are
  positional, exactly like cmux/browser tabs).

### Notification history

- `workspace_status` gains a ring buffer of the last
  `WORKSPACE_STATUS_EVENT_MAX` (32) attention events `{pane_id, level,
  text[96], at_ms}`, appended by `note_notify`, failing `note_command`, and
  `note_child_exit` (not raw output — too noisy).
- The rail header gains a **bell button** left of `+` showing the count of
  events newer than the last time the popover was opened; hidden at zero.
- Clicking it opens a `ui_menu` popover listing events newest-first as
  `<workspace label>: <text>` tinted by level, plus a trailing
  `Clear history` item. Clicking an event jumps to its pane via the existing
  `g_jump_request` path (a pruned/dead pane id is a no-op with a toast).

---

## Config

```ini
[remote]
remote_api = false
remote_api_send = false
```

No config for ports or ergonomics (always on; ports are passive).

## Error handling

- Port parser: on any ambiguity, parse nothing. Overlong numbers, missing
  boundaries, unknown hosts → skip.
- Remote: every failure is a structured `ok:false` reply; a wedged/closed
  client never blocks the render loop (worker thread owns all blocking I/O;
  inbox/outbox are bounded — drop the connection on overflow).
- `fangs ctl` with no running instance: clear message + exit 2.
- Menu targeting a tab that closed between open and click: validate index
  against `app.n_tabs` at execute time, no-op with a toast.
- Reorder during an active drag when a tab closes: cancel the drag.

## Tests

- `workspace_ports_tests`: each pattern, boundary/port-range rejection,
  chunk-split matches, dedupe, LRU cap, clear-on-completion semantics.
- `remote_proto_tests`: request parsing (every cmd, missing/extra fields,
  bad JSON, oversize line), response building (escaping, error shapes),
  gating matrix (`send`/`new --run` refused without `remote_api_send`).
- `ui_menu_tests` (pure model): layout rects, clamping to window bounds,
  hit test, Esc/away close semantics.
- `ui_workspace_rail_model_tests`: port chip rects + `OPEN_PORT` hit action;
  bell button rect + `HISTORY` hit action; armed-close row flag;
  `workspace_rail_drop_index` slots (top/middle/bottom, empty rail).
- `workspace_status_tests`: event ring append/order/wrap, clear, and that
  raw output does not append events.
- Existing suites stay green: `ctest --test-dir build --output-on-failure`.

## Acceptance

- `npx vite` in a workspace shows a `:5173` chip within a frame of the
  announcement; clicking it opens the browser; Ctrl+C on the server clears
  the chip at the next prompt. A single command announcing several servers
  (monorepo dev script: app + API + Storybook) shows up to three chips on
  that row.
- With `remote_api = true`: `fangs ctl list` reports every workspace with
  name/branch/working/attention/ports; `fangs ctl new --worktree --run claude`
  (with send enabled) creates an isolated agent workspace from the CLI;
  `fangs ctl read 1` dumps a screen. With gates off, the socket does not
  exist / send is refused.
- Right-click any workspace row → rename/worktree/close all work; middle
  click arms then closes; rows drag-reorder with a visible insertion line;
  the bell popover lists recent rings and clicking one jumps to that pane.
- All prior behavior intact: clicks, attention, desktop notifications,
  worktree +, rename, shortcuts.
