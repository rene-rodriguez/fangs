# Crash-Resilience Tier 3 Spec — Detached Session Server

> **Status: spec + plan only. Nothing here is built.** This is the dedicated
> planning session that `docs/crash-resilience-plan.md` said Tier 3 deserved,
> gated on Tier 1 telemetry (now shipping) or a real incident actually showing
> the multi-week investment is warranted. Read `docs/crash-resilience-plan.md`
> first for the root cause and the cheaper tiers (crash telemetry, opt-in
> `tmux_wrap`) that already ship. Build sequence lives in
> `docs/crash-resilience-tier3-plan.md`.

## Summary

Today Fangs is a monolith: the GUI process calls `forkpty()` itself
(`src/pty.c:72`, reached via `session_create` → `pty_spawn`, `src/session.c:62`)
and owns the pty master fd, the VT engine, and the render loop all in one
address space. When that process dies — crash *or* clean quit — the kernel
SIGHUPs every shell's foreground process group and kills whatever is running,
including a long agent task. Workspace *layout* survives
(`persist_session_if_dirty`, `src/main.c:3040`; restored from `session.json`
via `workspace_session_store.c`), but the running processes and their
scrollback do not.

Tier 3 fixes the root cause the way tmux/screen do: move the `forkpty()` out
of the GUI and into a small, long-lived background daemon (**`fangsd`**) that
owns the PTYs and outlives any GUI process. The GUI becomes a **client** that
attaches to live sessions over a Unix socket, streams their output in, and
reconnects to them on relaunch. A GUI crash then kills only the client; the
shell/agent keeps running in the daemon and is reattached automatically next
launch.

This is deliberately the tmux architecture, scoped to Fangs' actual seams. The
central design bet is that the daemon owns **raw pty byte streams, not screen
state** — libghostty-vt stays entirely client-side, the existing
`term_engine_write(bytes)` path (`src/term_engine.h:33`) is reused unchanged,
and reattach works by replaying a bounded byte ring into a fresh engine. That
keeps the daemon tiny (no Zig, no GPU, no VT engine) — which is the whole point,
since a tiny daemon is a tiny crash surface.

## Goals

1. A GUI crash or restart must not kill the shells/agents running in its panes.
   After relaunch, the user's panes reconnect to the still-running sessions with
   their scrollback and live output intact.
2. The daemon is minimal and robust: no rendering, no VT engine, no GPU, no Zig
   toolchain dependency. Its only jobs are owning PTYs, buffering their output,
   and relaying I/O over a socket.
3. Graceful degradation: if the daemon can't start or the socket path fails,
   Fangs falls back to today's in-process `forkpty()` and behaves exactly as it
   does now. Tier 3 is never *worse* than Tier 0.
4. Opt-in and reversible during rollout, gated behind a single config flag
   (`session_server`, default off), exactly like `tmux_wrap` (`src/config.h:27`).
5. The hard logic (ring buffer, wire framing, replay, lifecycle) is pure and
   headless-testable — same discipline as `remote_proto`, `pane`, and
   `workspace_rail_model` (all pure, all unit-tested), so most of Tier 3 is
   verifiable without driving the GUI.

## Non-Goals

- **Not a server-side screen model.** The daemon never runs libghostty-vt and
  never tracks cell grids (see "Core decision" below). Reattach is byte replay,
  not a grid snapshot.
- **Not networked.** Unix domain socket only, 0600, user-owned. No TCP, ever.
  No multi-user, no remote-host sessions.
- **Not multi-client-input in v1.** Two GUIs may *view* one session (output
  fans out for free); merging *input* from two attached clients is deferred
  (see Open Questions).
- **Not a Windows story.** `forkpty()` and the daemon model are POSIX;
  macOS + Linux only, matching Fangs' existing targets.
- **Not replacing `tmux_wrap`.** Tier 2 stays as a zero-dependency fallback for
  users who don't enable the server. When `session_server` is on, `tmux_wrap`
  is ignored (the daemon already provides survival).

## Core decision: PTY-only daemon, byte-replay reattach

The one architectural fork in the road is **where the VT engine lives**.

**Option A — PTY-only daemon (chosen).** The daemon owns `forkpty()` + the pty
master fd + a bounded **raw output ring buffer** per session. The VT engine
(`term_engine` / libghostty-vt) stays in the GUI client. On attach the daemon
replays the ring as raw bytes; the client feeds them into a fresh `term_engine`
exactly as it feeds a local pty today, reconstructing the screen. Live bytes
follow.

**Option B — VT-in-daemon (rejected for v1).** The daemon owns the pty *and* the
screen grid; reattach sends a cell-grid snapshot. This is literally what tmux
does, and reattach is cheaper (send current grid, not full history). But it drags
libghostty-vt (and the Zig 0.15.2 toolchain, `./scripts/macos-build.sh`) into the
daemon, and it rewrites the client's render path from "borrow ghostty handles"
(`src/main.c:6076`) to "render a received grid." That breaks the `term_engine`
seam badly for a benefit (cheaper reattach) that a generously-sized ring makes
marginal.

**Chosen: Option A.** Rationale:

- **Minimal daemon.** No VT engine ⇒ no Zig, no GPU, ~a few hundred lines of pty
  + socket + ring plumbing. Smallest possible crash surface, which is the entire
  justification for the rewrite.
- **Reuses the existing client path.** `session_feed_pty_stats` (`src/session.c:113`)
  already does "read bytes → `cmdblocks_feed` → engine." Under Option A the only
  change is *where the bytes come from* (a framed socket instead of a pty fd).
- **The replay buffer and the backpressure buffer are the same buffer.** A slow
  or detached client is handled by the same bounded ring that powers reattach —
  one mechanism, two payoffs (see "Backpressure").

**The honest caveat.** Byte replay is correct only if replay starts at a clean
point in the escape-sequence stream. Two mitigations, both in v1:

1. **Resync anchor.** The daemon tracks the byte offset of the most recent
   full-screen clear it saw in the output stream (`ED 2`/`ED 3` — `ESC[2J` /
   `ESC[3J`, and alt-screen enter/exit `ESC[?1049h/l`). The ring never drops
   bytes *before* the current anchor without advancing the anchor to the next
   one, so replay always begins at a screen-clear boundary and never mid-CSI.
   This is a small scan over the output bytes, the same tier of work as
   `workspace_ports_feed` (`src/session.c:123`) already does per read.
2. **SIGWINCH redraw backstop.** Immediately after replay the client sends its
   real size via `resize`; full-screen TUIs (including coding agents) repaint on
   SIGWINCH regardless, so any residual primary-screen imperfection is corrected
   on the first frame. This is exactly the `dtach -r winch` strategy.

If, after Tier 1 data and real use, byte replay proves inadequate (e.g. a
heavily-scrolled primary screen resyncs poorly), Option B remains a future
refinement — but it is explicitly out of scope here.

## Process & lifecycle model

Mirrors tmux: **one daemon per user**, many sessions, clients come and go.

- **Identity & discovery.** The daemon listens on a *stable* path
  `<app_dir>/fangsd.sock` (`config_default_app_dir()`, `src/config.h:51`) —
  contrast the current remote API's *pid-scoped* `remote-<pid>.sock`
  (`src/remote_api.c:55`), which deliberately can't be found across restarts.
  Tier 3 needs the opposite: a new GUI must find the *old* daemon. The socket is
  `chmod 0600` and user-owned (same guard as `remote_api.c:268`).
- **Auto-spawn.** When the GUI starts with `session_server` on and no daemon
  answers `<app_dir>/fangsd.sock`, the GUI spawns one: `fork()` → `setsid()` →
  `fork()` again (so the daemon is not a session leader and is reparented to
  init, never re-acquiring a controlling terminal) → `chdir("/")` → close/replace
  std fds with a log file → `execv` the `fangsd` binary. This is standard
  double-fork daemonization; the daemon is **not** a child of the GUI and does
  not die with it. Same pattern tmux uses to start its server on first command.
- **Session lifetime.**
  - Client detaches (or its GUI dies): sessions keep running. *This is the win.*
  - Shell in a session exits: the daemon reaps it (`waitpid`, cf.
    `session_reap`, `src/session.c:248`), marks the session dead, keeps a short
    **tombstone** carrying the exit status so an attached/next client can show
    "exited (code N)" instead of the pane silently vanishing, then frees it once
    seen or after a grace period.
  - Idle daemon (zero live sessions, zero tombstones, no client) exits after a
    short grace period, so nothing lingers. tmux exits when its last session
    closes; same here.
  - Daemon crashes: its sessions die (nothing owns the PTYs anymore). Accepted:
    the daemon is tiny and stable *by construction*, and Tier 1 crash telemetry
    (`crash_log_install`, `src/crash_log.c`) is extended to it so daemon crashes
    are also data, not guesses.

## Wire protocol

One Unix socket, one uniform binary framing, two logical payload kinds. JSON
(the existing `remote_proto` vocabulary) rides inside control frames; raw pty
bytes ride inside data frames. Line-delimited JSON alone (today's transport,
`src/remote_api.c:180`) is unfit for streaming pty I/O — it would base64-bloat
every keystroke and every screen update.

**Frame header (fixed 9 bytes, big-endian):**

```
[u8  type][u32 session_id][u32 length][ ...length bytes of payload... ]
```

| type | name         | direction | payload                                        |
|------|--------------|-----------|------------------------------------------------|
| 0x01 | CONTROL      | both      | one JSON object (extends `remote_proto`)       |
| 0x02 | PTY_DATA     | both      | raw pty bytes (server→client = output; client→server = input) |
| 0x03 | EVENT        | server→client | async JSON (session exited, session added, replay-complete) |

`session_id == 0` on a CONTROL frame addresses the daemon itself (create, list,
attach). Data/input frames always carry a real session id.

**Control verbs** (JSON `cmd`, building directly on the enum in
`src/remote_proto.h:9`):

- `create` `{cwd, name, cols, rows, cell_w, cell_h}` → `{session_id}`. The
  daemon `forkpty`s (the code lifts from `pty_spawn`, `src/pty.c:60`).
- `list` → `[{session_id, name, cwd, cols, rows, alive, exit_status, title}]`.
  Reuses/extends the existing `list` response shape.
- `attach` `{session_id, cols, rows}` → daemon streams the replay ring as
  PTY_DATA frames, then an EVENT `replay-complete`, then live output. Sends a
  `resize` on the pty so the child SIGWINCH-repaints.
- `detach` `{session_id}` → client stops receiving; session keeps running.
- `resize` `{session_id, cols, rows, cell_w, cell_h}` → `pty_set_winsize`
  (`src/pty.c:176`).
- `kill` `{session_id}` → SIGHUP/terminate the session's process group and reap.

The existing `send`/`read`/`ring`/`focus`/`rename` verbs and the whole
`fangs ctl` CLI (`src/main.c:3914`) are a **client-level** concern (they act on
tabs/panes, which the daemon knows nothing about) and stay where they are — they
talk to the GUI's own control socket as today. The daemon protocol is strictly
about session I/O and lifecycle. `read` is discussed under Open Questions.

**Backpressure.** The daemon must never block its pty read loop on a slow or
detached socket. Per session it keeps the bounded output ring (the same one used
for replay); it writes to client sockets non-blocking. If a client can't keep
up, bytes accumulate in the ring (bounded by the configured scrollback budget);
on overflow the oldest bytes are dropped **but never below the current resync
anchor**, so live state is always intact and only deep scrollback is lost. A
detached session is just the degenerate case: ring fills to cap, oldest drops,
newest always available for the next attach.

## Reattachment protocol (GUI launch)

1. GUI resolves `<app_dir>/fangsd.sock`; if unanswered and `session_server` on,
   auto-spawns the daemon (above) and retries with a short bounded wait (cf. the
   500×2ms bind-wait in `remote_api_start`, `src/remote_api.c:76`).
2. GUI sends `list`.
3. GUI loads `session.json`. Tier 3 extends `WorkspaceSessionTab`
   (`src/workspace_session_store.h:14`) with a `daemon_session_id` per tab
   (0/absent in old files → "no server session, create fresh"). For each
   restored tab:
   - if its `daemon_session_id` is alive in the daemon's `list` → `attach`;
   - else → `create` a fresh session (today's behavior) and record the new id.
4. On `attach`, the client feeds replayed + live PTY_DATA into a fresh
   `term_engine` — the same `cmdblocks_feed` + engine path as `session_feed_pty_stats`.
5. **Orphans** (live daemon sessions not referenced by any restored tab — e.g.
   the user closed the GUI window but the agent kept running) are surfaced, not
   force-adopted: a toast ("2 detached sessions running") and a command-palette
   entry to adopt them into new tabs. Mirrors `tmux ls` showing unattached
   sessions.

**New UI, minimal:** a "reconnected" toast on successful reattach, and the
orphan-adoption palette entry. No modal "reconnect vs fresh" dialog in v1 — the
`daemon_session_id` match makes the decision automatically.

## Security & trust model

- Unix socket, 0600, user app dir — identical guard to the current remote API
  (`src/remote_api.c:268`), no network surface.
- The daemon relays arbitrary input to real shells, so it is inherently a
  "send"-capable surface — the same threat the workspace-ops spec already flags
  for `remote_api_send` ("arbitrary command execution, same trust model as tmux
  send-keys"). The difference: the daemon *is* the terminal backend, so it's
  effectively always on when `session_server` is enabled. That's why Tier 3 is
  gated behind its own default-off config flag during rollout and documented as
  an explicit trust decision, not silently switched on.
- The daemon never writes anything but its own log and the pty streams; it holds
  no secrets, reads no config beyond socket path + ring size, and takes no
  instruction from pty *content* (output is data, never commands).

## Failure modes

| Event | Behavior |
|-------|----------|
| Daemon won't start / bind fails | GUI falls back to in-process `forkpty()` (Tier 0). A toast notes the server is unavailable. Never worse than today. |
| Daemon dies, GUI alive | Client sees socket EOF, marks affected sessions dead (tombstone-style), offers a "restart session server" action. Those sessions' processes are lost (rare — tiny daemon). |
| **GUI dies, daemon alive** | **Sessions survive; next GUI reattaches with scrollback + live output.** The whole point. |
| Client can't keep up | Server ring-buffers (bounded); deep scrollback may truncate; live tail always delivered. No corruption of current screen (resync anchor). |
| `session.json` references a dead/absent session id | Create fresh, exactly like today. |
| Two GUIs attach one session | v1: output mirrors to both; input policy = last-attached-wins or read-only-second (see Open Questions). |
| `session_server` on but `tmux_wrap` also on | Daemon wins; `tmux_wrap` ignored (documented). |

## Config

New `[terminal]` (or new `[session]`) keys, following the `tmux_wrap` pattern
(`src/config.h:27`, `src/config.c` parse/default/save, `docs/config.example`):

- `session_server = false` — master switch for Tier 3. Off ⇒ today's monolith.
- `session_server_scrollback_bytes = 1048576` — per-session daemon ring cap
  (the replay + backpressure budget). Distinct from the VT engine's line-based
  `scrollback`; this is a raw-byte bound on the daemon side.
- `session_server_idle_exit_seconds = 30` — how long an empty daemon lingers
  before self-exit (0 = exit immediately when the last session closes).

## Tests

Because the daemon is headless and the framing/ring/replay logic is pure, most
of Tier 3 is testable without the GUI (repo convention: small per-module CTest
executables, cf. `tests/pty_tmux_wrap_tests.c`, `tests/config_tests.c`):

- **Wire framing** (`session_wire`): frame/deframe round-trip, partial reads,
  split-across-buffer headers, oversized-length rejection. Pure.
- **Ring buffer**: append past cap drops oldest, resync anchor never dropped,
  replay reproduces expected byte tail. Pure.
- **Daemon integration** (fork the daemon in-test, connect over a temp socket):
  create → send input → read output; detach → reconnect → assert replay matches;
  kill → assert tombstone + exit status; idle → assert self-exit. Mirrors the
  fork/exec/socket patterns already in `tests/pty_tmux_wrap_tests.c` and
  `tests/workspace_git_status_tests.c`.
- **Reattach mapping**: `session.json` with a live id attaches; with a stale id
  creates fresh. Extends `tests/config`/session-store tests.
- **Fallback**: with the daemon unreachable, `session_create` still yields a
  working in-process session (Tier 0 path unbroken).

## Acceptance

1. `session_server = true`, start Fangs, launch a long-running command
   (`sleep 300 &` / an agent), `kill -9` the GUI, relaunch → the pane
   reconnects and the process is still running with its scrollback.
2. Same with a clean quit + relaunch.
3. `session_server = false` → behavior byte-for-byte identical to today; full
   existing test suite still green.
4. Daemon binary killed while GUI runs → GUI degrades gracefully, no hang.
5. Daemon unavailable at launch → GUI falls back to in-process sessions.
6. `crash.log` gains daemon entries when the daemon is made to crash on purpose.

## Open questions

- **Multi-client input.** v1 mirrors output; do we allow two GUIs to *type* into
  one session (tmux does, chaotically) or restrict input to one attacher?
  Recommendation: input to the most-recently-attached client only; revisit if
  users want true shared control.
- **`fangs ctl read` without an attached client.** Under Option A the daemon
  holds only raw bytes, not rendered text, so a client must be attached (with a
  live `term_engine`) to answer `read`/AI-context. Options: keep `read`
  client-only; or have the daemon keep a tiny text-tail. Recommendation:
  client-only for v1.
- **Ring vs Option B.** Revisit server-side screen state only if byte replay
  correctness proves inadequate in real use.
- **One daemon per user vs per window.** Recommendation: per user (tmux model);
  per-window loses the cross-restart survival benefit for multi-window users.
