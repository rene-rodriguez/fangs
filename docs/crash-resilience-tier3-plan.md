# Crash-Resilience Tier 3 Implementation Plan — Detached Session Server

> **Status: plan only. Not started.** Companion to
> `docs/crash-resilience-tier3-spec.md` (read that first for the architecture,
> the PTY-only/byte-replay decision, and the wire protocol). This is the
> task-by-task build sequence, in the same shape as `docs/workspace-rail-plan.md`
> and `docs/workspace-ops-plan.md`: each phase is independently shippable and
> testable, and the ordering is chosen so the risky, GUI-coupled work comes
> last, after the pure headless core is proven.

## Guiding constraints

- **Never regress Tier 0.** Every phase keeps `session_server = false` behaving
  byte-for-byte like today's monolith. The daemon path is additive and gated.
- **Headless-first.** The daemon, the wire framing, and the ring buffer are pure
  and testable without the GUI. Build and prove them before touching `main.c`.
- **Reuse, don't fork.** The `forkpty` code (`src/pty.c`), the JSON vocabulary
  (`src/remote_proto.*`), the vendored cJSON, and the session-store JSON
  (`src/workspace_session_store.*`) are lifted/extended, not rewritten.
- **Build via `./scripts/macos-build.sh`**, never plain `cmake`/`ninja` (it pins
  Zig 0.15.2; a stray system Zig 0.16.0 breaks the libghostty-vt step). New
  CTest targets are added the usual way (`add_executable` + `add_test`).

## File Map

New:

- `src/session_wire.h` / `.c` — pure frame encode/decode + the bounded output
  ring with resync anchor. No sockets, no pty. Client and daemon both link it.
- `src/session_backend.h` / `.c` — client-side seam: a `SessionBackend` that a
  `Session` reads/writes through, with a **local** (in-process `forkpty`, today)
  and a **remote** (daemon socket) implementation.
- `src/daemon/fangsd_main.c` — the `fangsd` binary entry point (accept loop,
  event loop).
- `src/daemon/fangsd_session.h` / `.c` — one daemon session: pty master +
  child pid + output ring + attached-client set.
- `src/daemon/fangsd_control.h` / `.c` — control-verb dispatch (create/list/
  attach/detach/resize/kill) built on `remote_proto`.
- `tests/session_wire_tests.c`, `tests/fangsd_session_tests.c`,
  `tests/fangsd_integration_tests.c`.

Modified:

- `src/pty.c` / `.h` — extract the child-side exec into a small reusable
  `pty_exec_shell()` so both the in-process path and the daemon share it.
- `src/session.c` / `.h` — route I/O through `SessionBackend` instead of a bare
  `pty_fd`.
- `src/config.c` / `.h`, `docs/config.example` — the three `session_server*`
  keys.
- `src/workspace_session_store.c` / `.h` — add `daemon_session_id` per tab.
- `src/main.c` — daemon auto-spawn, attach-on-launch, orphan adoption, fallback.
- `src/crash_log.c` — reused as-is by the daemon (install at `fangsd` startup).
- `CMakeLists.txt` — the `fangsd` executable, the new sources on `fangs`, the
  new test targets, and `util` linkage on Linux for the daemon (forkpty).

---

## Phase 0: Client-side backend seam (no daemon yet)

**Goal:** introduce `SessionBackend` so `Session` no longer reads a bare pty fd
directly, without changing any behavior. This is the keystone refactor that
makes every later phase a drop-in.

- Define `SessionBackend` (`src/session_backend.h`): an opaque handle plus the
  operations `Session` actually needs — `backend_read(sink)`, `backend_write`,
  `backend_resize`, `backend_child_alive`, `backend_pid`, `backend_close`.
- Implement `session_backend_local`: wraps exactly today's `pty_spawn` /
  `pty_read` / `pty_write` / `pty_set_winsize`. Pure move of existing logic.
- Change `struct Session` (`src/session.c:16`) to hold a `SessionBackend *`
  instead of `int pty_fd` (keep a `session_pty_fd()` accessor returning the
  local backend's fd for now, so `main.c`'s poll loop is untouched).
- `session_feed_pty_stats` (`src/session.c:113`) reads via `backend_read`.

**Ship/verify:** zero behavior change. Full existing `ctest` green. This phase
alone is a safe, mergeable refactor.

---

## Phase 1: Wire framing + ring buffer (pure, headless)

**Goal:** the `session_wire` module — the shared transport primitive — fully
built and unit-tested with no sockets involved.

- `session_wire_frame(type, sid, payload, len)` → serialized bytes;
  `session_wire_parse(buf, len, *consumed, *out)` handling partial/split reads
  (returns "need more bytes" without consuming). Big-endian 9-byte header per
  spec.
- `WireRing`: fixed-cap byte ring with `ring_append(bytes)`, `ring_snapshot()`
  (for replay), and resync-anchor tracking — scan appended bytes for `ESC[2J`,
  `ESC[3J`, `ESC[?1049h`, `ESC[?1049l`; on overflow never drop below the current
  anchor.

**Tests (`tests/session_wire_tests.c`):** frame round-trip; header split across
two reads; payload split; oversized `length` rejected; ring overflow drops
oldest; anchor preserved across overflow; replay reproduces the expected tail.
All pure.

**Ship/verify:** new test target green; nothing else references it yet.

---

## Phase 2: Daemon core (headless, no GUI)

**Goal:** a runnable `fangsd` that owns PTYs and serves the socket protocol,
provable entirely from a test harness.

- Extract `pty_exec_shell(shell, cwd, tmux_wrap)` from `pty_spawn`'s child branch
  (`src/pty.c:77-124`) so the daemon reuses the exact login-shell / `$SHELL`
  resolution logic.
- `fangsd_session`: `forkpty` (reuse), non-blocking master, an owned `WireRing`,
  a set of attached client fds. `feed()` drains the master into the ring and
  fans out PTY_DATA frames to attached clients (non-blocking; ring absorbs
  backpressure).
- `fangsd_control`: parse a CONTROL frame via `remote_proto` (extended with
  `create`/`attach`/`detach`/`kill`), mutate session state, reply.
- `fangsd_main`: single-threaded `poll()`/`select()` event loop over {listen fd,
  client fds, session master fds}. Accept multiple clients. Install
  `crash_log_install()` at startup (daemon crash telemetry).

**Tests (`tests/fangsd_session_tests.c`, `tests/fangsd_integration_tests.c`):**
in-test fork the daemon on a temp socket; create a session, write `echo`, read
it back; detach + reconnect and assert the replay tail matches; `kill` and
assert the tombstone/exit status; drive a slow reader and assert live tail still
arrives. Fork/exec/socket patterns from `tests/pty_tmux_wrap_tests.c`.

**Ship/verify:** `fangsd` works as a standalone tool (a tiny built-in `--selftest`
or the CLI harness), independent of the GUI. This is the largest phase; land it
behind its own review.

---

## Phase 3: Daemon lifecycle & discovery

**Goal:** the daemon behaves like a well-mannered background service.

- Stable socket at `<app_dir>/fangsd.sock`, 0600 (contrast the pid-scoped
  `remote-<pid>.sock`); refuse to double-bind (existing daemon wins).
- Auto-spawn helper (client-side, `src/main.c`): double-fork + `setsid` +
  `chdir("/")` + std-fd redirect to a daemon log + `execv fangsd`; bounded retry
  waiting for the socket (cf. `remote_api_start`'s 500×2ms wait).
- Idle self-exit after `session_server_idle_exit_seconds`; dead-session
  tombstones with exit status, freed once observed or after grace.
- Config keys (`session_server`, `session_server_scrollback_bytes`,
  `session_server_idle_exit_seconds`) through `config.c/h` + `docs/config.example`
  following the `tmux_wrap` 4-step pattern; unit tests in `tests/config_tests.c`.

**Ship/verify:** start GUI → daemon appears; quit all clients → daemon lingers
then exits; second GUI finds the running daemon.

---

## Phase 4: Client remote backend + fallback

**Goal:** the GUI actually uses the daemon when `session_server` is on.

- `session_backend_remote`: connects to `fangsd.sock`, sends `create`, then
  translates `backend_read` ← PTY_DATA frames, `backend_write`/`resize` →
  frames. Needs a small client-side receive buffer feeding `session_wire_parse`.
- Integrate framed-socket readiness into `main.c`'s existing per-frame poll: the
  remote backend exposes a pollable fd so the frame loop drains it the same way
  it drains local pty fds today (`session_feed_pty_stats` at `src/main.c:4879`).
- **Fallback:** if the daemon is unreachable or `create` fails, transparently
  fall back to `session_backend_local` — same graceful-degradation shape as
  `tmux_wrap` falling through to a plain shell (`src/pty.c:119`). A toast notes
  the server is unavailable.

**Ship/verify:** with `session_server = true`, everyday use is indistinguishable
from Tier 0 — until you kill the GUI. With it false, the local path is untouched.

---

## Phase 5: Reattach + persistence

**Goal:** the payoff — panes reconnect to live sessions across GUI restarts.

- Extend `WorkspaceSessionTab` (`src/workspace_session_store.h:14`) with
  `daemon_session_id`; write/read it in `workspace_session_store.c` (absent in
  old files ⇒ 0 ⇒ "create fresh"). Round-trip test.
- On launch (`session_server` on): `list` the daemon; for each restored tab,
  `attach` if its id is alive, else `create`. Feed replay + live bytes into the
  fresh `term_engine` (unchanged path).
- Orphan handling: daemon sessions not referenced by any restored tab → a toast
  + a command-palette entry ("Adopt N detached sessions") that opens them as new
  tabs. No modal dialog.
- "Reconnected" toast on successful reattach.

**Ship/verify:** acceptance tests 1–2 from the spec (kill -9 the GUI mid-`sleep`,
relaunch, process still running with scrollback; same for clean quit).

---

## Phase 6: Hardening & rollout

**Goal:** make it trustworthy enough to consider flipping the default.

- Multi-attach policy (output mirrors; input = most-recent attacher per spec
  Open Questions).
- Backpressure/ring tuning against a real agent workload; verify resync-anchor
  correctness on heavily-scrolled primary screens.
- Dead-session UX: show "exited (code N)" from tombstones instead of a pane
  vanishing.
- Daemon crash telemetry validated (`crash.log` gains daemon entries on a forced
  crash) — acceptance test 6.
- Docs: a `docs/handoff-*.md` note, README section on the session server and its
  trust model, and update `docs/crash-resilience-plan.md` to mark Tier 3 done.
- Decide, from Tier 1 + Tier 3 real-use data, whether `session_server` graduates
  from default-off to default-on.

---

## Verification (whole feature)

1. `./scripts/macos-build.sh`, then `ctest` from `build/` — all suites green,
   including the new `session_wire`, `fangsd_session`, and integration targets.
2. Manual acceptance 1–6 from `docs/crash-resilience-tier3-spec.md`.
3. Regression gate: with `session_server = false`, the entire pre-Tier-3 test
   suite passes unchanged and behavior is identical to today.
4. Linux parity check (`docs/linux-verification.md` conventions): daemon builds
   with `util` linkage, forkpty + socket paths work, reattach survives a GUI
   `kill -9`.

## Risk notes

- **Biggest risk is Phase 2 correctness** (pty fan-out + backpressure + resync
  anchor). It is also the most isolated and headless-testable phase — lean on
  the integration harness hard before Phase 4 couples it to the GUI.
- **Byte-replay fidelity** is the standing design bet (spec Core Decision). If it
  disappoints, the fallback is Option B (server-side screen model), which is a
  *new* planning effort, not a patch — do not slide into it mid-build.
- **Scope discipline:** networked sessions, multi-user, Windows, and shared
  input are all explicitly out of scope. Keep them out.
