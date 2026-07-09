# Workspace Ops Implementation Plan — Ports, Remote Control, Rail Ergonomics

> **Status (2026-07-08): Tasks 1–11 shipped.** Ports (`21e6d26`, `2d47167`,
> `c39242f`), remote control (`e7343db`), and rail ergonomics — menu component
> (`802c298`) + context menu/armed close/drag/history (`8df165e`), hardened in
> a bug-fix pass (`d6c283d`). Task 12 (libproc verification) was skipped as
> the stretch task it was scoped to be. See Task 13 for closeout status.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show high-confidence dev-server ports on rail rows, add a Unix-socket JSON API + `fangs ctl` CLI for agent orchestration, and add rail row ergonomics (context menu, armed middle-click close, drag reorder, notification history).

**Architecture:** Same discipline as the previous two slices — every new decision surface is a pure, tested model (`workspace_ports`, `remote_proto`, `ui_menu` model, rail-model extensions); raylib layers only paint; blocking I/O lives on worker pthreads mirroring `ai_http.c`; `main.c` only orchestrates via small host handlers. Read `docs/workspace-ops-spec.md` first; it is the contract.

**Tech Stack:** C11, Raylib, cJSON (vendored in `src/`), pthreads, Unix domain sockets, existing `App`/`Tab`/`Session`/`CmdBlocks`/`WorkspaceStatus`/`workspace_worktree`, CMake/CTest.

**Build note:** Always build with `scripts/macos-build.sh` (pins vendored Zig 0.15.2). A bare `cmake` reconfigure resolves Zig from PATH and fails on the ghostty external build. Tests: `ctest --test-dir build --output-on-failure`.

---

## File Map

- Create: `src/workspace_ports.{h,c}`, `tests/workspace_ports_tests.c`
- Create: `src/remote_proto.{h,c}`, `tests/remote_proto_tests.c`
- Create: `src/remote_api.{h,c}`
- Create: `src/ui_menu.{h,c}`, `tests/ui_menu_tests.c`
- Modify: `src/session.{h,c}` (own a port scanner)
- Modify: `src/config.{h,c}`, `docs/config.example`, `tests/config_tests.c`
- Modify: `src/workspace_status.{h,c}`, `tests/workspace_status_tests.c`
- Modify: `src/ui_workspace_rail_model.{h,c}`, `tests/ui_workspace_rail_model_tests.c`
- Modify: `src/ui_workspace_rail.c`
- Modify: `src/main.c` (host handlers, ctl mode, guards)
- Modify: `CMakeLists.txt`, `README.md`
- Optional (Task 12): `src/workspace_ports_verify.{h,c}`

Tasks 1–3 (ports), 4–7 (remote), 8–11 (ergonomics) are independent groups;
each group is a shippable commit series. Task 12 is a stretch.

---

## Task 1: Port scanner (pure)

**Files:** `src/workspace_ports.{h,c}`, `tests/workspace_ports_tests.c`, `CMakeLists.txt`

- [x] **Step 1: Write failing tests** (register the test binary in CMake the same way `workspace_status_tests` is registered)

API under test:

```c
#define WORKSPACE_PORTS_MAX 6

typedef struct {
    int  ports[WORKSPACE_PORTS_MAX];
    uint64_t last_seen[WORKSPACE_PORTS_MAX]; // feed sequence, for LRU
    int  count;
    char carry[32];      // tail of the previous chunk (match may straddle)
    int  carry_len;
    uint64_t feed_seq;
} WorkspacePortScanner;

void workspace_ports_reset(WorkspacePortScanner *sc);
void workspace_ports_feed(WorkspacePortScanner *sc, const uint8_t *data, size_t len);
int  workspace_ports_get(const WorkspacePortScanner *sc, int *out, int max); // ascending, returns count
void workspace_ports_clear(WorkspacePortScanner *sc);
```

Test cases (style of `tests/workspace_status_tests.c`):
- `"  Local:   http://localhost:5173/"` → `{5173}`
- `"listening on 127.0.0.1:8080"` → `{8080}`; `"0.0.0.0:3000"` → `{3000}`; `"[::1]:9229"` → `{9229}`
- boundary: `"localhost:5173abc"` → no match (port must end at a non-digit or end-of-feed-flush); `"localhost:0"`, `"localhost:70000"`, `"localhost:"` → no match
- unknown hosts rejected: `"10.0.0.5:443"`, `"host.docker.internal:4000"` → empty
- case-insensitive host: `"LocalHost:4321"` → `{4321}`
- chunk split: feed `"localhost:51"` then `"73 ready"` → `{5173}`; also split inside the host: `"local"` + `"host:5173"` → `{5173}`
- dedupe: same port announced twice → count 1
- LRU cap: announce 7 distinct ports → count 6, oldest evicted
- `workspace_ports_clear` empties; `workspace_ports_get` returns ascending order

- [x] **Step 2: Implement** — a byte-at-a-time matcher against the four host literals with a rolling window (`carry`), then digit accumulation with range check and boundary requirement. No allocation, no syscalls, ANSI bytes just fail the match and pass through. Mirror `cmdblocks_osc.c`'s chunk-safety comments.
- [x] **Step 3: Build via `scripts/macos-build.sh`, run the new test binary, then the full suite.**

## Task 2: Sessions own a scanner; lifetime wiring

**Files:** `src/session.{h,c}`, `src/main.c`

- [x] **Step 1:** `Session` owns a `WorkspacePortScanner` exactly like it owns `CmdBlocks`: feed it in the PTY read path (`session_feed_pty*`), reset it in `session_respawn`. Accessor: `int session_ports(const Session *s, int *out, int max);` plus `void session_ports_clear(Session *s);`.
- [x] **Step 2:** In main.c's per-pane event loop (the block that polls `cmdblocks_completion_seq` — search for `pane_update_completion_seq`), when a **new completion** is detected, also call `session_ports_clear(ss)` — the foreground command exited, its port claims are stale. Child exit / respawn also clears (Step 1).
- [x] **Step 3:** Full build + suite; manual smoke: run `python3 -m http.server 8000`, confirm via debugger/log or defer visual check to Task 3.

## Task 3: Port chips in the rail

**Files:** `src/ui_workspace_rail_model.{h,c}`, `tests/ui_workspace_rail_model_tests.c`, `src/ui_workspace_rail.c`, `src/main.c`

- [x] **Step 1: Failing model tests**
  - `WorkspaceRailInput` gains `const int *ports; int port_count;`. `WorkspaceRailRow` gains `int ports[3]; int port_count;` (build copies at most 3, ascending) and chip rects `int port_x[3], port_y, port_w, port_h;` assigned by `workspace_rail_layout` (fixed chip width, right-aligned on the secondary line, only in full mode).
  - New action `WORKSPACE_RAIL_ACTION_OPEN_PORT` (`index` = row, reuse `pane_id`… add `int port;` to `WorkspaceRailAction`). `workspace_rail_hit` returns it for clicks inside chip rects (chips hit-test **before** the row-switch/focus fallthrough).
  - Tests: chips absent in compact mode; a three-port row's chip rects don't overlap each other or the attention dot; hit inside each chip → OPEN_PORT with the right port; hit elsewhere on the row still switches/focuses.
- [x] **Step 2: Implement model + draw.** Chips: rounded rect, subtitle-tinted fill, text `:%d` at FS_SUB, hover brightens (same hover pattern as the `+` button). Secondary-line text (`branch`/attention text) is clipped to end before the first chip.
- [x] **Step 3: Wire main.c** — `collect_rail_inputs` fills ports (tab rows: focused pane's scanner, fallback first leaf — same representative logic as titles; pane rows: own scanner). Handle `OPEN_PORT` in the rail click switch by building `http://localhost:<port>` and calling the existing URL opener (grep main.c for the `xdg-open`/`open` fork used by Ctrl/Cmd+click; extract a small helper if it is inline).
- [x] **Step 4: Full suite + visual smoke** (`FANGS_PHASE3_SMOKE_SCREENSHOT=…`), plus a live check: `python3 -m http.server 8000` in a split, confirm chip appears, click opens browser, Ctrl+C clears at next prompt.
- [x] **Commit:** `feat: show dev-server ports on workspace rail rows`

## Task 4: Remote protocol (pure)

**Files:** `src/remote_proto.{h,c}`, `tests/remote_proto_tests.c`, `CMakeLists.txt`

- [x] **Step 1: Failing tests** for the parse/build API:

```c
typedef enum { REMOTE_CMD_NONE, REMOTE_CMD_LIST, REMOTE_CMD_NEW, REMOTE_CMD_FOCUS,
               REMOTE_CMD_RENAME, REMOTE_CMD_SEND, REMOTE_CMD_READ, REMOTE_CMD_RING } RemoteCmd;

typedef struct {
    long id; RemoteCmd cmd;
    int index, pane, lines;          // -1 when absent
    bool worktree;
    char cwd[4096], name[64], run[512], text[4096], message[128];
} RemoteRequest;

bool remote_proto_parse(const char *line, RemoteRequest *out, char *err, int err_size);
// Builders return malloc'd JSON lines (caller frees):
char *remote_proto_error(long id, const char *msg);
char *remote_proto_ok(long id);                       // {"id":N,"ok":true}
char *remote_proto_ok_obj(long id, /* cJSON* */ void *fields); // takes ownership
```

Tests: every cmd parses with required/optional fields; unknown cmd, missing `cmd`, non-JSON, and >8 KiB lines produce errors; builders emit exact JSON including escaping of quotes/newlines in `read` text; `id` echoes through.

- [x] **Step 2: Implement with cJSON** (`src/cJSON.h` is vendored). Keep it protocol-only: no sockets, no threads, no App types.
- [x] **Step 3: Build + suite.**

## Task 5: Config gates

**Files:** `src/config.{h,c}`, `tests/config_tests.c`, `docs/config.example`

- [x] **Step 1:** Failing config tests: `remote_api` and `remote_api_send` parse under `[remote]`, default false, round-trip through `config_save`. Follow exactly how `workspace_rail` was added.
- [x] **Step 2:** Implement + document in `docs/config.example` with the one-line threat note from the spec.
- [x] **Step 3:** Build + suite.

## Task 6: Socket server + host executor

**Files:** `src/remote_api.{h,c}`, `src/main.c`, `CMakeLists.txt`

- [x] **Step 1: `remote_api` module.** API:

```c
bool remote_api_start(const char *socket_dir);   // spawns the worker thread
void remote_api_stop(void);
bool remote_api_poll(RemoteRequest *out);        // main thread, non-blocking
void remote_api_respond(char *json_line);        // takes ownership, thread writes it
const char *remote_api_socket_path(void);
```

Worker thread: create `remote-<pid>.sock` (dir 0700, sock 0600, `unlink` stale), repoint the `remote.sock` symlink, `listen`, accept **one client at a time**, read newline-delimited requests (cap 8 KiB/line, parse with `remote_proto_parse`, parse errors answered directly from the thread), push valid requests to a mutex-guarded inbox (bounded, 16 — overflow drops the client). Mirror the mutex discipline of `ai_http.c`. Unlink socket + symlink in `remote_api_stop` (call it in main's shutdown path).

- [x] **Step 2: Host executor in main.c.** `remote_execute(const RemoteRequest *rq)` placed next to `apply_palette_action`, returning a `remote_proto_*` JSON line:
  - `list`: walk `app.tabs` reusing `collect_rail_inputs`-style data (name/label/branch/title via the same representative-session logic, `working`/`attention` from `workspace_status`, ports from Task 2, `panes` = leaf count).
  - `new`: gate `FANGS_MAX_TABS`; `worktree:true` → `workspace_worktree_create` from the active tab's cwd (or `cwd` arg), then `app_add_tab_named`; `run` requires `remote_api_send` → write `run + "\n"` to the new session's PTY.
  - `focus`: `app_switch_tab` + optional pane focus by index; sync runtime + drain (copy the rail click handler's calls verbatim).
  - `rename`: bounds-check, copy into `Tab.name`.
  - `send`: requires `remote_api_send`; `pty_write` to the tab's focused pane.
  - `read`: `term_engine_dump_text()` on the tab's focused pane engine, trim to last `lines` lines.
  - `ring`: `workspace_status_note_notify(..., focused=false, message)`.
  - In the frame loop (near the jump-request resolution block): if `cfg.remote_api`, drain up to 4 requests per frame through `remote_execute` → `remote_api_respond`. Start/stop the server on config load/save transitions.
- [x] **Step 3: Build + suite.** Manual: enable gates, `echo '{"id":1,"cmd":"list"}' | nc -U ~/.config/fangs/remote.sock`.

## Task 7: `fangs ctl` CLI mode

**Files:** `src/main.c`, `README.md`

- [x] **Step 1:** In `main()` before raylib init: `argv[1] == "ctl"` → build a `RemoteRequest` JSON from argv (subcommands exactly as in the spec table; `--socket PATH` override; default `~/.config/fangs/remote.sock`), connect, send, print the reply line (for `read`, print the `text` field raw instead of JSON), exit 0 / 1 (`ok:false`) / 2 (connect failure, with a "is fangs running with remote_api=true?" hint). Keep argv→JSON building in a small pure-ish function so it can be unit-tested if time allows.
- [x] **Step 2:** README: remote-control section with the gating warning + the orchestrator recipe (spec has the copy). Keybindings table untouched.
- [x] **Step 3:** Full build + suite. Manual end-to-end: `fangs ctl new --worktree --name demo`, `fangs ctl list`, `fangs ctl read 1`.
- [x] **Commit:** `feat: add remote control socket API and fangs ctl` (Tasks 4–7; split 4/5 into a prep commit if diffs get large)

## Task 8: Generic menu component

**Files:** `src/ui_menu.{h,c}`, `tests/ui_menu_tests.c`, `CMakeLists.txt`

- [x] **Step 1: Failing pure-model tests.** Model mirrors the rail split: `ui_menu_model` logic embedded in one file is fine, but keep layout/hit free of raylib:

```c
#define UI_MENU_MAX_ITEMS 34
typedef struct { char label[112]; int tag; UiColor tint; bool separator; } UiMenuItem;
typedef struct {
    UiMenuItem items[UI_MENU_MAX_ITEMS]; int count;
    int x, y, w, h; int item_h;          // set by ui_menu_layout
    bool open;
} UiMenu;
void ui_menu_open(UiMenu *m, const UiMenuItem *items, int count, int anchor_x, int anchor_y);
void ui_menu_layout(UiMenu *m, int win_w, int win_h);   // clamps inside window
int  ui_menu_hit(const UiMenu *m, int mx, int my);      // item index, -1 outside
```

Tests: layout clamps at right/bottom edges; hit maps rows correctly; separators are skipped by hit; count caps.

- [x] **Step 2: Draw layer** (`ui_menu_draw(Font, const UiMenu*, int mx, int my)`): panel bg + border (reuse `inline_bg`/`panel_border` like the prompts), hover wash, per-item tint. Esc/click-away handling stays in the host.
- [x] **Step 3: Build + suite.**

## Task 9: Context menu + armed middle-click close

**Files:** `src/main.c`, `src/ui_workspace_rail_model.{h,c}` (+tests), `src/ui_workspace_rail.c`

- [x] **Step 1: Host generalizations.** Refactor `app_close_active` so `app_close_tab(int idx)` and `app_close_pane(uint64_t pane_id)` exist and `app_close_active` delegates. Existing tests stay green.
- [x] **Step 2: Context menu wiring.** Right-click (`MOUSE_BUTTON_RIGHT`) inside the rail resolves via `workspace_rail_hit`; on a tab/pane row open the `UiMenu` with the spec's items (tags encode action+index). Menu clicks execute before drawing, same pre-draw pattern as rail clicks. Add `ui_menu_active()`-style gating to every input-guard chain that lists `ui_rename_prompt_active()` (mechanical: extend both `&& !…` and `|| …` forms, exactly as the rename prompt did). `Rename…` → `ui_rename_prompt_open(idx, name)`; `New Worktree Here` → existing worktree handler seeded with that tab's representative cwd; closes → Step 1 helpers.
- [x] **Step 3: Armed close.** Middle-click on a workspace row arms `{tab_id, deadline_ms}` in main; view rows gain `int closing;` — model test: armed row's secondary line shows `click again to close` (build swaps it in, warn tint in draw). Second middle-click before deadline → `app_close_tab`. Any other input disarms.
- [x] **Step 4: Build + suite + manual check** (menu on both row kinds, close paths, Esc/away dismiss).
- [x] **Commit:** `feat: add rail context menu and armed middle-click close`

## Task 10: Drag-to-reorder workspaces

**Files:** `src/ui_workspace_rail_model.{h,c}` (+tests), `src/ui_workspace_rail.c`, `src/main.c`

- [x] **Step 1: Failing model tests** for `int workspace_rail_drop_index(const WorkspaceRailView *v, int my);` — slot 0 above first row, N between rows, tab_count at bottom; and for view fields `int drag_from, drag_slot;` (set by host, consumed by draw).
- [x] **Step 2: Host drag tracking.** On left-press over a tab row record candidate + press y; >6 px vertical movement enters drag (cancels the pending click); each frame update `drag_slot`; on release reorder `app.tabs` with struct moves (`memmove`), fix `app.active` to follow the dragged tab, cancel on tab-close/Esc. Draw: insertion line (accent, 2 px) at the slot; dragged row dimmed.
- [x] **Step 3: Build + suite + manual reorder check** (order persists, `Cmd+1..9` follows positions, attention dots follow their tabs).
- [x] **Commit:** `feat: drag to reorder workspaces in the rail`

## Task 11: Notification history

**Files:** `src/workspace_status.{h,c}` (+tests), `src/ui_workspace_rail_model.{h,c}` (+tests), `src/ui_workspace_rail.c`, `src/main.c`

- [x] **Step 1: Failing status tests.** Ring buffer `{pane_id, level, text[96], at_ms}` × 32: appended by `note_notify`, failing `note_command`, `note_child_exit`; **not** by `note_output`; newest-first read API `int workspace_status_events(const WorkspaceStatus*, WorkspaceStatusEvent *out, int max);` wrap-around order; `workspace_status_events_clear`.
- [x] **Step 2: Bell button.** Model: header bell rect left of `+` (`bell_x/y/w/h`), hidden when `unseen == 0` (host passes the unseen count into `workspace_rail_build`); hit → new action `WORKSPACE_RAIL_ACTION_HISTORY`. Draw: bell glyph is out (ASCII-only atlas) — draw a small badge with the count instead. Model tests for rect/hit/hidden-at-zero.
- [x] **Step 3: Popover.** `HISTORY` action opens a `UiMenu` of events (`<workspace label>: <text>`, level tint — resolve pane_id→tab label via the collect helpers; ASCII-sanitize with the model's rules) + trailing `Clear history`. Click event → `g_jump_request = pane_id` (dead ids no-op with a toast); Clear → `workspace_status_events_clear`. Opening records `last_seen` so the badge resets.
- [x] **Step 4: Build + suite + manual check.**
- [x] **Commit:** `feat: add notification history popover`

## Task 12 (stretch, only if 1–11 are green with time to spare): libproc port verification

**Files:** `src/workspace_ports_verify.{h,c}`, `src/main.c`, `CMakeLists.txt`

**Status: skipped.** Tasks 1–11 shipped with time to spare, but this was
never picked up — the output-parsing scanner from Task 1 has been sufficient
in practice. Revisit only if false-positive/negative port chips become an
actual complaint.

- [ ] Worker pthread sampling every ~2 s: for each session `session_child_pid`, enumerate descendants, list LISTEN tcp ports via `proc_pidinfo(PROC_PIDLISTFDS)` + `proc_pidfdinfo(PROX_FDTYPE_SOCKET)`; publish `{pane_id → ports}` under a mutex; `#ifdef __APPLE__` (no-op elsewhere). Main merges per spec: unverified parsed ports drop after 10 s; verified-unparsed ports append. Keep the merge logic pure and tested; only the sampler touches libproc.
- [ ] **Commit:** `feat: verify rail ports via libproc on macOS`

## Task 13: Final integration verification

- [x] `scripts/macos-build.sh`; `ctest --test-dir build --output-on-failure` (32/32 suites green as of `d6c283d`).
- [x] `git diff --check`.
- [x] Manual sweep of the acceptance list at the bottom of `docs/workspace-ops-spec.md` — verified at the code level during the `d6c283d` bug-fix pass (every menu action, the armed-close identity, drag insertion index, and history-popover jump path were traced end to end) plus a `FANGS_PHASE3_SMOKE_*` render check. Not re-driven through the live GUI interactively; do that first if a future regression is suspected here.
- [x] Update `README.md` feature bullets (ports, remote control, ergonomics) and `docs/config.example` if not already done in-task. `docs/config.example` already had the `[remote]` gates with the threat-note comment; `README.md`'s Features/Keybindings sections were missing the ports and rail-ergonomics bullets — added 2026-07-08.
