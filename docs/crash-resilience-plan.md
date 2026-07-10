# Crash / agent-process resilience — options

> **Status:** Tier 1 (crash telemetry) and Tier 2 (opt-in tmux wrapping) are both
> implemented. Tier 3 remains an option, not built. Written 2026-07-09 alongside the
> rail diff-review popover, after flagging that a Fangs crash today kills whatever's
> running inside it (including a long-running coding agent task). Read this before
> picking a further tier to act on.

## Root cause, confirmed in code

`src/pty.c` spawns the shell via `forkpty()` (`openpty` + `fork` + `login_tty()`), which
makes the shell a new session leader with the pty slave as its controlling terminal.
This is standard POSIX terminal behavior, not a Fangs bug: when the process holding the
pty master exits for *any* reason — clean quit or crash — the kernel delivers `SIGHUP`
to the slave's foreground process group, normally killing the shell and whatever's in
the foreground (a running agent, mid-task).

Every terminal emulator that isn't `tmux`/`screen` behaves this way. Those two survive
because a persistent server process — not the terminal window you're looking at —
actually owns the pty.

## Already mitigated today

Workspace *layout* (open tabs/panes, names, cwd, color tags) is flushed to disk on every
dirty frame (`persist_session_if_dirty`, `src/main.c`, called each frame the tab list
changed — not batched to clean-exit only) and restored on next launch
(`workspace_session_store.c`). A crash costs you the running processes and conversation
state, not the shape of your workspace — that part is already solved.

## Options, cheapest to most complete

- **Tier 1 — crash telemetry. DONE.** `src/crash_log.c` installs handlers for
  `SIGSEGV`/`SIGABRT`/`SIGBUS`/`SIGILL`/`SIGFPE` (via `crash_log_install()`, called from
  `main()` before config load) that append a timestamped marker plus a
  `backtrace()`/`backtrace_symbols_fd()` dump to `~/.config/fangs/crash.log`, then let
  the signal re-raise with its default disposition (`SA_RESETHAND`) so the process still
  terminates exactly as it would have without this handler. This turns "does Fangs
  actually crash in daily use" from a guess into data. Check `~/.config/fangs/crash.log`
  after any unexpected quit.

- **Tier 2 — opt-in tmux wrapping. DONE.** The manual workaround (run agent tasks inside
  `tmux`/`screen` yourself) still works with zero Fangs code and always will. On top of
  that, `pty_spawn()` (`src/pty.c`) now has a `pty_set_tmux_wrap(bool)` switch, driven by
  a new `tmux_wrap` config key (`[terminal]` section, off by default — see
  `docs/config.example`). When enabled, every new shell is launched via
  `tmux new-session -A -s fangs-<cwd-basename>-<pid>` instead of directly; if `tmux`
  isn't on `PATH`, the `execlp` fails and it falls straight through to the plain shell,
  so there's no hard dependency. A Fangs crash still SIGHUPs the pty's foreground
  process (the tmux *client*), but the shell/agent keeps running in tmux's own *server*
  — reattach after relaunching Fangs with `tmux ls` / `tmux attach -t <name>`. No
  automatic reattachment is wired up; this only guarantees survival, not a seamless
  reconnect (that's Tier 3's job). Tested in `tests/pty_tmux_wrap_tests.c` (skips its
  tmux-specific assertions if `tmux` isn't installed) and `tests/config_tests.c`.

- **Tier 3 — the real fix (large — a genuine architecture project, not polish).** Split
  Fangs into a thin GUI client plus a persistent background server that owns the PTYs
  (the `forkpty` call moves server-side) and survives GUI crashes or restarts, with the
  GUI reattaching to live sessions over IPC on relaunch. The existing `fangs ctl`
  remote-control socket is a relevant foundation for that IPC layer, but this still
  needs: a session-reattachment protocol, orphan/lifecycle detection for the server
  process, and new UI for "reconnect to running session" vs. "start fresh." This is the
  tmux-equivalent rewrite. It deserves its own dedicated planning session — task-by-task,
  the way `docs/workspace-rail-plan.md` was — gated on Tier 1 actually showing it's
  warranted, not something to scope line-by-line here.

## Recommendation

Tiers 1 and 2 are done and shipping now. Treat Tier 3 as a separate future conversation,
started only once Tier 1's data (or a real incident) says it's worth the multi-week
investment.
