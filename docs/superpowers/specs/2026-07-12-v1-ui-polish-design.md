# v1 UI Polish Design

## Goal
Make Fangs feel like a finished v1 application by improving the perceived quality of in-app feedback (toasts, notification rings) and the settings experience, without adding large new subsystems or new asset dependencies.

## Scope
- **Toasts:** richer visual styling, level icons, enter/exit animation, auto-dismiss progress.
- **Notification rings:** persistent unread accent plus a brief pulse when a new ring arrives.
- **Settings modal:** reorganize into vertical tab sections (General / AI / Advanced), improve spacing/typography, and fix focus/cursor consistency.
- **OS notification bridge:** when Fangs is not focused, also fire a native desktop notification on agent rings (already implemented for macOS; Linux falls back to `notify-send`-style path if present).
- **Out of scope for this round:** redesigning the command palette, adding a full notification center popover, adding a packaged icon font, or redesigning the rail row structure beyond the ring pulse.

## Decisions from brainstorming
1. Settings tabs: **vertical sidebar** on the left of the modal.
2. Toast placement: **top-right**, stacked downward with newest at top.
3. Notification ring: **one-shot scale + glow pulse** on new rings, then persistent unread state.
4. Icons: **primitive Raylib shapes only** (no new asset dependency).
5. OS notifications: **yes**, wire rings to the existing `desktop_notify_agent_ring()` path when app is not focused.

## Architecture
The changes stay inside the existing immediate-mode Raylib render loop. State for animations (toast age, ring pulse) is kept in small, self-contained structs in the UI modules, driven by `frame_dt_sec` from `main.c` (per the existing frame-pacing fix — never use `GetFrameTime()` for animation). The settings modal keeps its single-file implementation but splits its single scrollable form into sections selected by a vertical sidebar; the existing INI save/apply path is unchanged.

## Components

### 1. Toast system (`src/ui_toast.{c,h}`)
Current state: ring buffer of `(level, msg, ttl, max_ttl)`; drawing is inline in `main.c` as a flat rectangle with text.

Changes:
- Add a `birth` field so each toast can compute normalized age for animation.
- Provide `toast_tick(dt)` (already exists) and a new `toast_draw(font, scale, dt)` that renders the stack.
- Visual design per toast:
  - Rounded rectangle (`DrawRectangleRounded`) with level-colored 4 px left accent.
  - Small primitive icon on the left: info = "i" circle, warn = triangle with "!", error = circle with "×".
  - Message text to the right of the icon, vertically centered.
  - Subtle drop shadow below/behind (dark fill, low alpha).
  - Enter animation: first ~150 ms scales from 0.95 to 1.0 and fades alpha 0 → 1.
  - Exit animation: last ~250 ms slides right and fades out.
  - Progress bar: 2 px level-colored bar along the bottom edge that shrinks as TTL runs down.
- Stacking: top-right origin, newest at top, 8 px gap, 12 px margin from screen edges.
- Max width: 360 px at 1× scale, scaled by HiDPI `scale`.
- Keep existing `toast_push`, `toast_clear`, `toast_count`, `toast_get` API so callers in `main.c` don't change.

### 2. Notification ring pulse (`src/ui_workspace_rail.{c,h}` and model)
Current state: rail rows show an attention dot color (`attention_color`) and a notification strip; the bell button shows a numeric badge.

Changes:
- Add `float ring_pulse` to `WorkspaceRailView` (or to a transient render-time field) that starts at 1.0 when `workspace_status_note_notify()` is called and decays to 0.0 over ~700 ms.
- The pulse is driven by `frame_dt_sec` inside `ui_workspace_rail_draw` if `view->ring_pulse > 0`; it only affects the row that currently has the most-urgent unread pane (or the workspace row that matches `view->notification_pane`).
- Visual effect: scale the attention dot radius by `1.0 + 0.35 * pulse` and draw a low-alpha glow ring (`with_alpha(sev, 80 * pulse)`).
- Persistent unread state: if a row has unread attention, the dot stays filled and slightly brighter than before; remove the old subtle alpha so it actually reads.
- Wire the ring event so that if the Fangs window is not focused, it calls `desktop_notify_agent_ring()` with the workspace label and message text. This is a single call site in `main.c` near where `workspace_status_note_notify()` is already handled.

### 3. Settings modal redesign (`src/ui_settings.{c,h}`)
Current state: one long vertical form inside a `GuiPanel` modal.

Changes:
- Replace the single form with a vertical sidebar tab strip on the left:
  - Tabs: **General**, **AI**, **Advanced**.
  - Active tab is highlighted with the theme accent color.
  - Clicking a tab switches sections; `Esc` still closes the modal and cancels.
- Each section is a scrollable form area on the right:
  - **General:** Font family, Font size, Theme mode, Theme, Scrollback.
  - **AI:** Provider toggle group, Endpoint, Model, Max tokens, API key, Stream checkbox.
  - **Advanced:** Ollama knobs (num_ctx, num_gpu, num_thread, num_batch) only visible when Ollama is selected; otherwise show a disabled/placeholder message.
- Keep all existing controls (Raygui text boxes, spinners, checkboxes, dropdowns) but increase spacing and align labels consistently:
  - Section heading font size 18 px × scale.
  - Row label 14 px × scale, placed above each control.
  - 20 px × scale between rows, 28 px × scale between sections.
- Footer: Cancel / Save buttons pinned to the bottom-right of the modal.
- Tab switching resets any active text-edit focus via `clear_editing()`.
- Cursor feedback: every clickable control (tabs, toggles, dropdowns, buttons, checkboxes) sets `MOUSE_CURSOR_POINTING_HAND`; text boxes set `MOUSE_CURSOR_IBEAM`.
- Maintain the existing `ui_settings_open/toggle/draw` API and `out_saved` semantics.

### 4. OS notification gating (`src/main.c`)
- At the existing ring handler, check `IsWindowFocused()` (or a wrapper that returns true if the window has focus). If not focused, call `desktop_notify_agent_ring()` with the workspace label and the notification text.
- On Linux, `desktop_notify_agent_ring()` is currently an AppleScript fallback. Extend `desktop_notify.c` to try `notify-send` first when available before falling back.

## Data flow
1. User or agent triggers a toast → `toast_push()` adds to ring; `main.c` calls `toast_tick(frame_dt_sec)` each frame; `main.c` calls `toast_draw()` after the main UI so toasts paint on top.
2. Agent rings a workspace → `workspace_status_note_notify()` updates the rail model; `main.c` increments a transient pulse value in the rail view and, if unfocused, calls `desktop_notify_agent_ring()`.
3. User opens settings → modal draws sidebar; sidebar selection changes `settings_tab` enum; `ui_settings_draw` renders only that section; Save copies `draft` to `cfg`, closes the modal, and returns `out_saved=true`.

## Error handling / edge cases
- Toast ring full: keep existing overwrite-oldest behavior.
- Settings tab switch while a dropdown is open: close dropdown and clear editing.
- Window focus detection for notifications: if focus API is unreliable on a platform, default to sending the notification (better to over-notify than miss a ring).
- Linux `notify-send` missing: fall back to existing AppleScript/osascript path (no-op on Linux), which is acceptable because the in-app ring already happened.

## Testing
- Run `cmake --build build && ctest --test-dir build --output-on-failure` after any change; existing tests for config, palette model, etc. must pass.
- Visual smoke test: launch with `FANGS_PHASE3_SMOKE_SCREENSHOT=/tmp/x.png FANGS_PHASE3_SMOKE_REPORT=/tmp/x.txt ./build/fangs` and inspect the screenshot for toast placement, settings modal tabs, and rail row readability.
- Manual test cases:
  - Trigger a palette workflow to generate a toast; verify enter/exit animation and progress bar.
  - Run `fangs ctl ring <idx> "test"` from another terminal while Fangs is unfocused; verify native notification on macOS and rail pulse.
  - Open settings, switch tabs, verify only one section shows and Save/Cancel still work.

## Files to modify
- `src/ui_toast.h`
- `src/ui_toast.c`
- `src/ui_workspace_rail.h`
- `src/ui_workspace_rail.c`
- `src/ui_settings.h`
- `src/ui_settings.c`
- `src/desktop_notify.c`
- `src/main.c` (toast drawing call site, ring notification call site, pulse injection)

## Files unlikely to need changes
- Theme files (`theme.c`, `ui_theme.{c,h}`) — reuse existing `g_ui_theme` tokens.
- Config (`config.{c,h}`) — no new settings.
- Layout (`layout.{c,h}`) — modal bounds computed inside settings.

## Spec self-review
- **Placeholder scan:** no TBD/TODO sections.
- **Internal consistency:** animation uses `frame_dt_sec` per existing project memory.
- **Scope check:** this is one focused polish round; no new subsystems like a notification center.
- **Ambiguity check:** vertical tabs, top-right toasts, primitive icons, and OS notifications are all explicitly decided.
