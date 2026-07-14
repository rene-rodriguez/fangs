#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>

#include "raylib.h"
#include "raygui.h"
#include <ghostty/vt.h>

// GLFW is statically linked in via raylib (PLATFORM_DESKTOP always builds it
// in), but raylib doesn't expose glfw3.h through its public target, and
// including it directly risks colliding with raylib.h's own identifiers.
// glfwWaitEventsTimeout() is the one function we need for event-driven frame
// pacing (see FANGS_IDLE_FPS/FANGS_ACTIVE_FPS below) and GLFW's C ABI is stable, so a bare
// extern is safer than pulling in the whole header. Event waiting is process-
// global in GLFW, not per-window, hence no GLFWwindow* parameter.
extern void glfwWaitEventsTimeout(double timeout_seconds);

#include "ai_block.h"
#include "ai_provider.h"
#include "cmdblocks.h"
#include "config.h"
#include "desktop_notify.h"
#include "context.h"
#include "inline_cmd.h"
#include "kitty_images.h"
#include "layout.h"
#include "pane.h"
#include "pty.h"
#include "redact.h"
#include "session.h"
#include "term_engine.h"
#include "theme.h"
#include "ui_inline.h"
#include "ui_palette.h"
#include "ui_sidebar.h"
#include "ui_sidebar_model.h"
#include "ui_settings.h"
#include "ui_theme.h"
#include "ui_toast.h"
#include "ui_effects.h"
#include "ui_workflow_prompt.h"
#include "ui_menu.h"
#include "ui_menu_draw.h"
#include "ui_rename_prompt.h"
#include "ui_broadcast_prompt.h"
#include "workspace_info.h"
#include "workspace_session_store.h"
#include "crash_log.h"
#include "workspace_git_status.h"
#include "workspace_status.h"
#include "workspace_worktree.h"
#include "ui_workspace_rail.h"
#include "ui_workspace_rail_model.h"
#include "remote_api.h"
#include "remote_proto.h"
#include "cJSON.h"

// Font embedded into the binary at compile time (CMake bin2header from
// assets/JetBrainsMono-Regular.ttf and JetBrainsMono-Bold.ttf).
#include "font_jetbrains_mono.h"
#include "font_jetbrains_mono_bold.h"

// App icon (CMake bin2header from assets/fangs-icon.png). Linux-only: macOS
// gets its icon from the .app bundle's Info.plist/.icns instead.
#include "icon_fangs.h"

// Font-zoom (Ctrl +/-/0) bounds. Default matches config_defaults().
#define FANGS_DEFAULT_FONT_SIZE 16
#define FANGS_MIN_FONT_SIZE     6
#define FANGS_MAX_FONT_SIZE     96
#define FANGS_MIN_WINDOW_W      320
#define FANGS_MIN_WINDOW_H      240
#define FANGS_MAX_WINDOW_DIM    10000

// Max tabs (Cmd+1..9 selects tab N; 0 reserved).
#define FANGS_MAX_TABS 9

// Idle-aware frame pacing: drop to a low FPS floor when nothing has
// happened for a while (no input, no PTY/AI output, no UI animation),
// and snap back to full FPS on any activity. See the idle-tracking block
// at the end of the main loop, right before EndDrawing().
#define FANGS_IDLE_TIMEOUT_MS 2000   // ms of no activity before throttling FPS
// Redraw-rate caps used by the event-driven pacing at the end of the main
// loop (glfwWaitEventsTimeout()). These bound staleness of PTY output/
// animations (cursor blink, toast fade) and idle CPU use — they are NOT what
// makes clicks reliable, since glfwWaitEventsTimeout() wakes on the actual
// input event rather than napping through a fixed window regardless of either
// value. (An earlier version of this fix instead raised FANGS_IDLE_FPS alone,
// on the theory that a smaller blind-sleep window would be safe enough — but
// SetTargetFPS()'s WaitTime() sleeps blindly no matter what it's set to, so a
// fast enough click could still land its press+release inside *any* fixed
// window and get coalesced away by GLFW's mouse-button callback, which just
// overwrites currentButtonState per event as it's processed. That's why
// SetTargetFPS is left uncapped entirely now — see InitWindow above.)
#define FANGS_IDLE_FPS         15
#define FANGS_ACTIVE_FPS       60

// Tab structure: owns a pane tree of terminal sessions.
typedef struct {
    PaneNode *root;
    PaneNode *focused;
    char name[64];   // user-set workspace name ("" = automatic rail label)
    int  color_tag;  // 0 = none, 1..WORKSPACE_RAIL_COLOR_TAG_COUNT = palette index
} Tab;

// App: the whole terminal window.
typedef struct {
    Tab tabs[FANGS_MAX_TABS];
    int n_tabs;
    int active;  // index into tabs[]
} App;

// Global App: owns all tabs and their pane trees of Sessions (§16.5).
static App app = {0};

// Pointer to the active session's CmdBlocks, kept in sync by
// sync_active_session(). Used by feed_engine() and the draw loop.
static CmdBlocks *g_cmdblocks = NULL;

// Workspace attention model for notification rings (workspace rail).
static WorkspaceStatus g_workspace_status = {0};
static bool g_workspace_status_inited = false;
static WorkspaceGitStatusSampler *g_git_status_sampler = NULL;

// Per-pane last-seen command-block completion and notification sequences for
// detecting new events in background panes.
static unsigned long g_pane_seen_completion[128] = {0};
static unsigned long g_pane_seen_notify[128] = {0};
static uint64_t g_pane_seen_ids[128] = {0};
static int g_pane_seen_count = 0;

// Last-focused pane ID for clearing attention on focus switch.
static uint64_t g_last_focused_pane_id = 0;

// Per-pane last-scrollbar-activity timestamp for fade-in/out.
// Array size matches the 64-entry PaneRectEntry buffers used elsewhere
// (e.g. resize_pane_leaves_to_fit's rects[64]).
static uint64_t g_scrollbar_last_scroll_ms[64];

// Stable slot lookup for scrollbar state so pane-id reuse does not alias
// faded activity timestamps. Falls back to modulo hashing only when full.
static uint64_t g_scrollbar_pane_ids[64];
static int      g_scrollbar_slot_count = 0;
static uint64_t g_scrollbar_last_offset[64];
static bool     g_scrollbar_initialized[64];

// Pending "jump to unread" target (set by Cmd+Shift+U or a click on the
// rail's notification strip; consumed before drawing).
static uint64_t g_jump_request = 0;

// Remote API server (NULL when disabled).
static RemoteApi *g_remote_api = NULL;

// Context menu / notification-history popover (reused).
static UiMenu g_rail_menu = {0};

// Armed-close state: pane_id and deadline (monotonic ms). 0 = not armed.
static uint64_t g_armed_pane_id = 0;
static uint64_t g_armed_deadline_ms = 0;

// Hover-preview dwell state: which rail row (tab/pane id) the mouse has been
// resting on and since when. The preview text itself is computed once (not
// every frame) after the dwell threshold passes — see the draw-time block
// near ui_workspace_rail_draw()'s call site.
static uint64_t g_hover_row_id = 0;
static uint64_t g_hover_since_ms = 0;
static char     g_hover_preview[256] = "";
#define FANGS_HOVER_DWELL_MS 500
// Below this many non-whitespace characters, the scrollback tail is just an
// idle prompt (e.g. "~" or a bare shell glyph) — not worth popping up a
// preview for, so the whole popup is skipped rather than shown near-empty.
#define FANGS_HOVER_PREVIEW_MIN_CHARS 12

// Set on any tab-list mutation (add/close/rename/reorder); the per-frame
// loop saves the session state and clears it when set, so writes happen at
// most once per frame and only when something actually changed.
static bool g_session_dirty = false;

// Drag-to-reorder state.
static int g_drag_from = -1;        // source tab index, -1 = not dragging
static int g_drag_slot = -1;        // insertion slot
static int g_drag_candidate = -1;   // tab index of the initial left-press
static int g_drag_press_y = 0;      // Y position of the initial left-press

// Rail resize-handle drag state: dragging the rail's right edge resizes it;
// releasing below WORKSPACE_RAIL_HIDE_THRESHOLD soft-collapses the rail to a
// thin WORKSPACE_RAIL_COLLAPSED_WIDTH sliver rather than hiding it outright,
// so the handle stays grabbable and the rail can always be dragged back open
// (mirrors VS Code/cmux's sidebar collapse gesture). The hard on/off toggle
// (Cmd+Shift+E / workspace.toggle_rail) is unaffected and clears this.
static bool g_rail_resizing = false;
static bool g_rail_collapsed = false;
static int  g_rail_drag_width = 0;  // live width (px) while dragging, pre-commit
static bool g_rail_was_collapsed_on_press = false; // rail state at gesture start
static int  g_rail_resize_press_x = 0;             // GetMouseX() at gesture start
#define WORKSPACE_RAIL_HIDE_THRESHOLD 120
#define WORKSPACE_RAIL_COLLAPSED_WIDTH 8
#define RAIL_RESIZE_HANDLE_PAD 4

// Animated (visual-only) rail width for the collapse/expand toggle, so it
// glides open/closed instead of snapping. Mirrors the drag-resize path below:
// the terminal grid/pty are only reflowed once the animation settles, since
// reflowing every intermediate frame makes wrapped lines "walk" the screen.
static float g_rail_width_anim = -1.0f; // -1 = uninitialized, snaps to target on first frame
#define RAIL_ANIM_SPEED 14.0f            // higher = snappier
#define RAIL_RESIZE_CLICK_SLOP_PX 6   // press->release delta below this = "just a click"

// Last-seen event count for the bell badge (set when history popover opens).
static int g_history_last_seen = 0;

// Rail context menu state: the target when the menu was opened.
static int  g_rail_context_tab = -1;    // tab index (for tab rows)
static uint64_t g_rail_context_pane = 0; // pane_id (for pane rows)
static bool g_rail_context_is_pane = false;

// Menu item tags for the rail context menu.
#define RAIL_MENU_RENAME      100
#define RAIL_MENU_WORKTREE    101
#define RAIL_MENU_CLOSE       102
#define RAIL_MENU_FOCUS       103
#define RAIL_MENU_HISTORY_CLEAR 104
#define RAIL_MENU_CLEANUP_CONFIRM 105
#define RAIL_MENU_PR_CREATE   106
#define RAIL_MENU_PR_DIFF     107
#define RAIL_MENU_COLOR       108   // opens the color-tag submenu
#define RAIL_MENU_DIFF        109   // opens the changed-files popover
// History event tags start at 200 (tag = 200 + event_index).
// Color-tag submenu tags start at 350 (tag = RAIL_MENU_COLOR_BASE + choice;
// choice 0 = "None", 1..WORKSPACE_RAIL_COLOR_TAG_COUNT = palette index).
// Reuses g_rail_context_tab to remember which tab the submenu applies to.
#define RAIL_MENU_COLOR_BASE  350

// History event cache: snapshot taken when the notification popover opens.
#define HISTORY_EVENT_CACHE 32
static WorkspaceStatusEvent g_history_event_cache[HISTORY_EVENT_CACHE];
static int g_history_event_count = 0;

// Attention Inbox: snapshot of currently-attention-needing tabs' pane ids,
// taken when the popover opens (tag = RAIL_MENU_INBOX_BASE + index).
#define RAIL_MENU_INBOX_BASE 300
static uint64_t g_inbox_pane_cache[WORKSPACE_RAIL_MAX_TABS];
static int g_inbox_pane_count = 0;

// Cross-workspace search results: one entry per tab with a match, snapshot
// taken when the results popover opens (tag = RAIL_MENU_SEARCH_BASE + index).
#define RAIL_MENU_SEARCH_BASE 400
static uint64_t g_search_result_pane_cache[WORKSPACE_RAIL_MAX_TABS];
static int g_search_result_count = 0;

// Diff-review popover: one entry per changed file, snapshot taken when the
// popover opens (tag = RAIL_MENU_DIFF_BASE + index). Clicking an entry copies
// its path to the clipboard rather than jumping anywhere, so this caches
// paths instead of pane ids.
#define RAIL_MENU_DIFF_BASE 500
#define RAIL_MENU_DIFF_PATH_CACHE_MAX 256
static char g_diff_result_path_cache[WORKSPACE_GIT_STATUS_MAX_FILES][RAIL_MENU_DIFF_PATH_CACHE_MAX];
static int g_diff_result_count = 0;

// Worktree cleanup: candidates snapshot taken when the confirm popover
// opens; acted on only if RAIL_MENU_CLEANUP_CONFIRM is selected.
static char g_cleanup_repo_root[WORKTREE_PATH_MAX];
static WorkspaceWorktreeCandidate g_cleanup_candidates[WORKTREE_CLEANUP_MAX];
static int g_cleanup_candidate_count = 0;

// PR/review handoff: repo root + base branch resolved when the rail context
// menu opens on a tab row; acted on if RAIL_MENU_PR_CREATE/PR_DIFF is chosen.
static char g_pr_repo_root[WORKTREE_PATH_MAX];
static char g_pr_base_branch[WORKTREE_NAME_MAX];

#define OPEN_WORKTREE_EXCLUDE_MAX (FANGS_MAX_TABS * WORKSPACE_RAIL_MAX_PANES)

static bool string_list_contains(const char *const *items, int count, const char *value)
{
    for (int i = 0; i < count; i++) {
        if (items[i] && value && strcmp(items[i], value) == 0)
            return true;
    }
    return false;
}

static int collect_open_session_cwds(const char **out, int max)
{
    if (!out || max <= 0)
        return 0;

    int count = 0;
    for (int ti = 0; ti < app.n_tabs && count < max; ti++) {
        Tab *tt = &app.tabs[ti];
        if (!tt->root)
            continue;

        PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
        int n_leaves = 0;
        pane_collect_leaves(tt->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n_leaves);
        for (int pi = 0; pi < n_leaves && count < max; pi++) {
            if (leaves[pi]->kind != PANE_LEAF)
                continue;
            const char *cwd = session_cwd(leaves[pi]->leaf.session);
            if (!cwd || !cwd[0] || string_list_contains(out, count, cwd))
                continue;
            out[count++] = cwd;
        }
    }
    return count;
}

// Forward declaration for scrollbar_state_prune.
static Session *session_for_pane_id(uint64_t target);

// Return the stable scrollbar-state slot for a pane. Panes keep the same slot
// for their lifetime; when the lookup table fills, we fall back to modulo
// hashing on pane_id so the arrays are still indexed within bounds.
static int scrollbar_slot_for_pane(uint64_t pane_id)
{
    for (int i = 0; i < g_scrollbar_slot_count; i++) {
        if (g_scrollbar_pane_ids[i] == pane_id)
            return i;
    }
    if (g_scrollbar_slot_count < 64) {
        int i = g_scrollbar_slot_count++;
        g_scrollbar_pane_ids[i] = pane_id;
        g_scrollbar_last_scroll_ms[i] = 0;
        g_scrollbar_last_offset[i] = 0;
        g_scrollbar_initialized[i] = false;
        return i;
    }
    return (int)(pane_id % 64);
}

// Produce a stable numeric pane ID from a session pointer.
static uint64_t pane_id_for_session(const Session *s)
{
    // Use the session pointer value directly — it's stable within a process.
    return (uint64_t)(uintptr_t)s;
}

// Resolve a pane_id back to its live Session, or NULL if it no longer
// exists (pane closed since the id was captured). Walks every tab's pane
// tree, mirroring the jump-to-pane resolution loop later in the frame.
static Session *session_for_pane_id(uint64_t target)
{
    if (target == 0)
        return NULL;
    for (int ti = 0; ti < app.n_tabs; ti++) {
        Tab *tt = &app.tabs[ti];
        if (!tt->root)
            continue;
        PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
        int n = 0;
        pane_collect_leaves(tt->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n);
        for (int i = 0; i < n; i++) {
            if (leaves[i]->kind != PANE_LEAF)
                continue;
            if (pane_id_for_session(leaves[i]->leaf.session) == target)
                return leaves[i]->leaf.session;
        }
    }
    return NULL;
}

// Remove scrollbar state slots whose pane has been closed. Compact the
// parallel arrays and update the slot count so pane reuse does not alias
// stale fade timestamps.
static void scrollbar_state_prune(void)
{
    int write = 0;
    for (int read = 0; read < g_scrollbar_slot_count; read++) {
        uint64_t pane_id = g_scrollbar_pane_ids[read];
        if (session_for_pane_id(pane_id) == NULL)
            continue;
        if (write != read) {
            g_scrollbar_pane_ids[write]       = g_scrollbar_pane_ids[read];
            g_scrollbar_last_scroll_ms[write] = g_scrollbar_last_scroll_ms[read];
            g_scrollbar_last_offset[write]    = g_scrollbar_last_offset[read];
            g_scrollbar_initialized[write]    = g_scrollbar_initialized[read];
        }
        write++;
    }
    g_scrollbar_slot_count = write;
}

// Find or create the seen-sequence slot for a pane. Returns -1 when full.
static int pane_seen_slot(const Session *s)
{
    uint64_t id = pane_id_for_session(s);
    for (int i = 0; i < g_pane_seen_count; i++) {
        if (g_pane_seen_ids[i] == id)
            return i;
    }
    if (g_pane_seen_count >= 128)
        return -1;
    g_pane_seen_ids[g_pane_seen_count] = id;
    g_pane_seen_completion[g_pane_seen_count] = 0;
    g_pane_seen_notify[g_pane_seen_count] = 0;
    return g_pane_seen_count++;
}

// Track a pane's last-seen command-block completion sequence.
// Returns the previous sequence (0 if unknown) and updates the stored value.
static unsigned long pane_update_completion_seq(const Session *s, unsigned long seq)
{
    int i = pane_seen_slot(s);
    if (i < 0) return 0;
    unsigned long prev = g_pane_seen_completion[i];
    g_pane_seen_completion[i] = seq;
    return prev;
}

// Same for the notification sequence (BEL / OSC 9 / OSC 777).
static unsigned long pane_update_notify_seq(const Session *s, unsigned long seq)
{
    int i = pane_seen_slot(s);
    if (i < 0) return 0;
    unsigned long prev = g_pane_seen_notify[i];
    g_pane_seen_notify[i] = seq;
    return prev;
}

static bool pane_id_list_contains(const uint64_t *ids, int count, uint64_t id)
{
    for (int i = 0; i < count; i++) {
        if (ids[i] == id) return true;
    }
    return false;
}

static void pane_prune_completion_seen(const uint64_t *live_ids, int live_count)
{
    int write_idx = 0;
    for (int i = 0; i < g_pane_seen_count; i++) {
        if (!pane_id_list_contains(live_ids, live_count, g_pane_seen_ids[i]))
            continue;
        if (write_idx != i) {
            g_pane_seen_ids[write_idx] = g_pane_seen_ids[i];
            g_pane_seen_completion[write_idx] = g_pane_seen_completion[i];
            g_pane_seen_notify[write_idx] = g_pane_seen_notify[i];
        }
        write_idx++;
    }
    g_pane_seen_count = write_idx;
}

static uint64_t tab_attention_id(Tab *tab)
{
    if (!tab || !tab->root)
        return 0;

    PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
    int n_leaves = 0;
    pane_collect_leaves(tab->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n_leaves);

    uint64_t best_id = 0;
    WorkspaceAttention best_level = WORKSPACE_ATTENTION_NONE;
    for (int i = 0; i < n_leaves; i++) {
        if (leaves[i]->kind != PANE_LEAF)
            continue;
        uint64_t id = pane_id_for_session(leaves[i]->leaf.session);
        if (best_id == 0)
            best_id = id;
        WorkspaceAttention level = workspace_status_level(&g_workspace_status, id);
        if (level > best_level) {
            best_level = level;
            best_id = id;
        }
    }
    return best_id;
}

// ---------------------------------------------------------------------------
// Workspace rail input snapshot (§ workspace-rail-spec)
// ---------------------------------------------------------------------------

// Stable string storage for rail inputs — the view builder only keeps
// pointers, so labels must outlive the collection loop.
typedef struct {
    WorkspaceRailInput tabs[WORKSPACE_RAIL_MAX_TABS];
    int tab_count;
    WorkspaceRailInput panes[WORKSPACE_RAIL_MAX_PANES];
    int pane_count;
    char tab_labels[WORKSPACE_RAIL_MAX_TABS][64];
    char tab_branches[WORKSPACE_RAIL_MAX_TABS][64];
    char pane_labels[WORKSPACE_RAIL_MAX_PANES][64];
    char pane_branches[WORKSPACE_RAIL_MAX_PANES][64];
    int  tab_ports[WORKSPACE_RAIL_MAX_TABS][3];
    int  tab_port_counts[WORKSPACE_RAIL_MAX_TABS];
    int  pane_ports[WORKSPACE_RAIL_MAX_PANES][3];
    int  pane_port_counts[WORKSPACE_RAIL_MAX_PANES];
} RailInputs;

static RailInputs g_rail_inputs;
static WorkspaceRailView g_rail_view;

// First leaf session of a tab, or NULL.
static Session *tab_first_leaf_session(Tab *tab)
{
    if (!tab || !tab->root)
        return NULL;
    PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
    int n = 0;
    pane_collect_leaves(tab->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n);
    for (int i = 0; i < n; i++) {
        if (leaves[i]->kind == PANE_LEAF)
            return leaves[i]->leaf.session;
    }
    return NULL;
}

// Stable identity for arming destructive actions on a tab: the first leaf
// pane id. tab_attention_id() shifts whenever attention levels change, which
// would break an armed close between the two confirmation clicks.
static uint64_t tab_stable_id(Tab *tab)
{
    Session *s = tab_first_leaf_session(tab);
    return s ? pane_id_for_session(s) : 0;
}

// True when the tab's pane tree contains the given pane id.
static bool tab_contains_pane(Tab *tab, uint64_t pane_id)
{
    if (!tab || !tab->root || pane_id == 0)
        return false;
    PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
    int n = 0;
    pane_collect_leaves(tab->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n);
    for (int i = 0; i < n; i++) {
        if (leaves[i]->kind == PANE_LEAF
            && pane_id_for_session(leaves[i]->leaf.session) == pane_id)
            return true;
    }
    return false;
}

// Snapshot tab and pane descriptors for the rail. A tab is represented by its
// focused pane (falling back to the first leaf): cwd label, git branch, and
// the OSC 0/2 window title so agent tabs read as what they're doing.
// workspace_git_branch() walks the filesystem (stat/fopen up to the repo
// root) and collect_rail_inputs() runs every frame the rail is visible, so
// without caching that walk re-runs 60+ times/sec for every tab and every
// pane in the active tab — for a value that only changes on checkout/cd.
// Throttled to the same cadence as the git-status dirty-count sampler
// (workspace_git_status.c) so both rail signals refresh in lockstep.
#define RAIL_BRANCH_CACHE_SIZE 96
typedef struct {
    char cwd[1024];
    char branch[64];
    uint64_t computed_ms;
    bool used;
} RailBranchCacheEntry;

static RailBranchCacheEntry g_rail_branch_cache[RAIL_BRANCH_CACHE_SIZE];

static bool cached_git_branch(const char *cwd, char *out, int out_size, uint64_t now_ms)
{
    if (!cwd || !*cwd || out_size <= 0) {
        if (out_size > 0) out[0] = '\0';
        return false;
    }

    int match = -1;
    int lru = 0;
    for (int i = 0; i < RAIL_BRANCH_CACHE_SIZE; i++) {
        RailBranchCacheEntry *e = &g_rail_branch_cache[i];
        if (!e->used) { lru = i; continue; }
        if (strcmp(e->cwd, cwd) == 0) { match = i; break; }
        if (!g_rail_branch_cache[lru].used
            || e->computed_ms < g_rail_branch_cache[lru].computed_ms)
            lru = i;
    }

    if (match >= 0) {
        RailBranchCacheEntry *e = &g_rail_branch_cache[match];
        if (now_ms - e->computed_ms < WORKSPACE_GIT_STATUS_INTERVAL_MS) {
            snprintf(out, out_size, "%s", e->branch);
            return e->branch[0] != '\0';
        }
        lru = match;
    }

    bool found = workspace_git_branch(cwd, out, out_size);
    RailBranchCacheEntry *slot = &g_rail_branch_cache[lru];
    snprintf(slot->cwd, sizeof(slot->cwd), "%s", cwd);
    snprintf(slot->branch, sizeof(slot->branch), "%s", found ? out : "");
    slot->computed_ms = now_ms;
    slot->used = true;
    return found;
}

static void collect_rail_inputs(uint64_t now_ms)
{
    RailInputs *ri = &g_rail_inputs;
    const char *home = getenv("HOME");
    ri->tab_count = 0;
    ri->pane_count = 0;

    for (int ti = 0; ti < app.n_tabs && ti < WORKSPACE_RAIL_MAX_TABS; ti++) {
        Tab *tt = &app.tabs[ti];
        int i = ri->tab_count;
        Session *rep = NULL;
        if (tt->focused && tt->focused->kind == PANE_LEAF)
            rep = tt->focused->leaf.session;
        if (!rep)
            rep = tab_first_leaf_session(tt);

        const char *cwd = rep ? session_cwd(rep) : "";
        ri->tab_labels[i][0] = '\0';
        ri->tab_branches[i][0] = '\0';
        workspace_cwd_label(cwd, home, ri->tab_labels[i],
                            (int)sizeof(ri->tab_labels[i]));
        if (cwd && cwd[0])
            cached_git_branch(cwd, ri->tab_branches[i],
                              (int)sizeof(ri->tab_branches[i]), now_ms);

        ri->tabs[i].id = tab_attention_id(tt);
        ri->tabs[i].color_tag = tt->color_tag;
        ri->tabs[i].label = ri->tab_labels[i];
        ri->tabs[i].branch = ri->tab_branches[i];
        ri->tabs[i].title = rep
            ? cmdblocks_title((CmdBlocks *)session_cmdblocks(rep)) : "";
        ri->tabs[i].name = tt->name;
        ri->tabs[i].active = (ti == app.active) ? 1 : 0;
        ri->tabs[i].git_changed_count = 0;

        // Armed-close state: match on the tab's stable id (the attention
        // representative shifts when attention changes mid-confirmation).
        uint64_t tab_close_id = tab_stable_id(tt);
        ri->tabs[i].closing = (g_armed_pane_id != 0
                               && g_armed_pane_id == tab_close_id
                               && g_armed_deadline_ms > now_ms) ? 1 : 0;

        // Compute working state: aggregate over all leaf panes in this tab.
        int working = 0;
        int idle_ms = -1;
        if (tt->root) {
            PaneNode *tleaves[WORKSPACE_RAIL_MAX_PANES];
            int tnl = 0;
            pane_collect_leaves(tt->root, tleaves, WORKSPACE_RAIL_MAX_PANES, &tnl);
            uint64_t tpids[WORKSPACE_RAIL_MAX_PANES];
            int ntp = 0;
            for (int pi = 0; pi < tnl && pi < WORKSPACE_RAIL_MAX_PANES; pi++) {
                if (tleaves[pi]->kind != PANE_LEAF) continue;
                tpids[ntp++] = pane_id_for_session(tleaves[pi]->leaf.session);
            }
            if (workspace_status_any_working_at(&g_workspace_status, tpids, ntp, now_ms))
                working = 1;
            // Idle duration = time since the MOST RECENT output across any
            // pane in this tab (i.e. based on the max last_output_ms).
            uint64_t most_recent = 0;
            for (int pi = 0; pi < ntp; pi++) {
                uint64_t lo_ms = workspace_status_last_output_ms(&g_workspace_status, tpids[pi]);
                if (lo_ms > most_recent) most_recent = lo_ms;
            }
            if (most_recent) idle_ms = (int)(now_ms - most_recent);
            ri->tabs[i].git_changed_count =
                workspace_git_status_sum_unique_for(g_git_status_sampler, tpids, ntp);
        }
        ri->tabs[i].working = working;
        ri->tabs[i].idle_ms = idle_ms;

        // Collect dev-server ports from the representative session.
        ri->tab_port_counts[i] = 0;
        if (rep) {
            int ports[3] = {0};
            int pn = session_ports(rep, ports, 3);
            for (int pi = 0; pi < pn && pi < 3; pi++) {
                ri->tab_ports[i][pi] = ports[pi];
                ri->tab_port_counts[i]++;
            }
        }
        ri->tabs[i].ports[0] = ri->tab_ports[i][0];
        ri->tabs[i].ports[1] = ri->tab_ports[i][1];
        ri->tabs[i].ports[2] = ri->tab_ports[i][2];
        ri->tabs[i].port_count = ri->tab_port_counts[i];
        ri->tab_count++;
    }

    Tab *atab = (app.n_tabs > 0 && app.active >= 0) ? &app.tabs[app.active] : NULL;
    if (atab && atab->root) {
        PaneNode *aleaves[WORKSPACE_RAIL_MAX_PANES];
        int anl = 0;
        pane_collect_leaves(atab->root, aleaves, WORKSPACE_RAIL_MAX_PANES, &anl);
        for (int pi = 0; pi < anl && ri->pane_count < WORKSPACE_RAIL_MAX_PANES; pi++) {
            if (aleaves[pi]->kind != PANE_LEAF)
                continue;
            int i = ri->pane_count;
            Session *ps = aleaves[pi]->leaf.session;
            const char *pcwd = session_cwd(ps);
            ri->pane_labels[i][0] = '\0';
            ri->pane_branches[i][0] = '\0';
            workspace_cwd_label(pcwd, home, ri->pane_labels[i],
                                (int)sizeof(ri->pane_labels[i]));
            if (pcwd && pcwd[0])
                cached_git_branch(pcwd, ri->pane_branches[i],
                                  (int)sizeof(ri->pane_branches[i]), now_ms);

            uint64_t pid = pane_id_for_session(ps);
            ri->panes[i].id = pid;
            ri->panes[i].color_tag = atab->color_tag;
            ri->panes[i].label = ri->pane_labels[i];
            ri->panes[i].branch = ri->pane_branches[i];
            ri->panes[i].title = cmdblocks_title((CmdBlocks *)session_cmdblocks(ps));
            ri->panes[i].name = NULL;   // panes are never renamed
            ri->panes[i].active = (aleaves[pi] == atab->focused) ? 1 : 0;
            ri->panes[i].working = workspace_status_is_working_at(&g_workspace_status, pid, now_ms) ? 1 : 0;
            {
                uint64_t plast = workspace_status_last_output_ms(&g_workspace_status, pid);
                ri->panes[i].idle_ms = plast ? (int)(now_ms - plast) : -1;
            }
            ri->panes[i].git_changed_count =
                workspace_git_status_count_for(g_git_status_sampler, pid);
            ri->panes[i].closing = 0;  // pane rows are not armed for close

            // Collect dev-server ports from this pane session.
            ri->pane_port_counts[i] = 0;
            if (ps) {
                int ports[3] = {0};
                int pn = session_ports(ps, ports, 3);
                for (int pi2 = 0; pi2 < pn && pi2 < 3; pi2++) {
                    ri->pane_ports[i][pi2] = ports[pi2];
                    ri->pane_port_counts[i]++;
                }
            }
            ri->panes[i].ports[0] = ri->pane_ports[i][0];
            ri->panes[i].ports[1] = ri->pane_ports[i][1];
            ri->panes[i].ports[2] = ri->pane_ports[i][2];
            ri->panes[i].port_count = ri->pane_port_counts[i];
            ri->pane_count++;
        }
    }
}

// Build + lay out the shared rail view (g_rail_view): snapshot inputs, inject
// host-side bell/drag state, assign geometry. Every rail consumer (click
// handlers, drag tracking, drawing) must go through this so hit targets can't
// drift from what is painted — and so host-owned fields like bell_unseen are
// never forgotten (workspace_rail_build memsets the view).
static void build_rail_view(const Layout *lo, uint64_t now_ms, int font_size)
{
    collect_rail_inputs(now_ms);
    workspace_rail_build(&g_rail_view,
                          g_rail_inputs.tabs, g_rail_inputs.tab_count,
                          g_rail_inputs.panes, g_rail_inputs.pane_count,
                          &g_workspace_status,
                          lo->rail_compact ? 1 : 0);
    g_rail_view.bell_unseen = workspace_status_unseen(&g_workspace_status,
                                                       g_history_last_seen);
    workspace_rail_layout(&g_rail_view, lo->rail.x, lo->rail.y,
                          lo->rail.w, lo->rail.h, font_size);
    g_rail_view.drag_from = g_drag_from;
    g_rail_view.drag_slot = g_drag_slot;
}

// ---------------------------------------------------------------------------
// Pane-rect collector for multi-pane rendering (§16.4)
// ---------------------------------------------------------------------------

typedef struct { PaneNode *leaf; int x, y, w, h; } PaneRectEntry;

typedef struct {
    PaneRectEntry *entries;
    int count, capacity;
} PaneRectCollector;

static void pane_rect_collect_cb(const PaneNode *n, int x, int y, int w, int h, void *user)
{
    PaneRectCollector *c = (PaneRectCollector *)user;
    if (c->count < c->capacity) {
        c->entries[c->count].leaf = (PaneNode *)n;
        c->entries[c->count].x = x;
        c->entries[c->count].y = y;
        c->entries[c->count].w = w;
        c->entries[c->count].h = h;
        c->count++;
    }
}

// ---------------------------------------------------------------------------
// Session handle mgmt
// ---------------------------------------------------------------------------

// Re-extract the active session's handles into the main-loop locals.
// Call on tab switch, pane split/close/focus change, and at startup.
// Returns the active Session*, or NULL if no tab/session exists.
// Also updates the module-global g_cmdblocks pointer.
static Session *sync_active_session(TermEngine **te_out,
                                     int *pty_fd_out, pid_t *child_out,
                                     bool *child_exited_out)
{
    if (app.n_tabs < 1 || app.active < 0 || app.active >= app.n_tabs) {
        g_cmdblocks = NULL;
        if (te_out) *te_out = NULL;
        if (pty_fd_out) *pty_fd_out = -1;
        if (child_out) *child_out = -1;
        if (child_exited_out) *child_exited_out = true;
        return NULL;
    }
    Tab *tab = &app.tabs[app.active];
    if (!tab->focused)
        tab->focused = pane_first_leaf(tab->root);
    PaneNode *leaf = tab->focused;
    if (!leaf || leaf->kind != PANE_LEAF) {
        leaf = pane_first_leaf(tab->root);
        tab->focused = leaf;
    }
    if (!leaf) {
        g_cmdblocks = NULL;
        if (te_out) *te_out = NULL;
        if (pty_fd_out) *pty_fd_out = -1;
        if (child_out) *child_out = -1;
        if (child_exited_out) *child_exited_out = true;
        return NULL;
    }
    Session *s = leaf->leaf.session;
    g_cmdblocks = (CmdBlocks *)session_cmdblocks(s);
    if (te_out)      *te_out      = (TermEngine *)session_engine(s);
    if (pty_fd_out)  *pty_fd_out  = session_pty_fd(s);
    if (child_out)   *child_out   = session_child_pid(s);
    if (child_exited_out) *child_exited_out = !session_child_alive(s);
    return s;
}

static Session *sync_active_runtime(TermEngine **te_out,
                                    int *pty_fd_out, pid_t *child_out,
                                    bool *child_exited_out,
                                    GhosttyTerminal *terminal_out,
                                    GhosttyRenderState *render_state_out,
                                    GhosttyRenderStateRowIterator *row_iter_out,
                                    GhosttyRenderStateRowCells *row_cells_out,
                                    GhosttyKittyGraphicsPlacementIterator *placement_iter_out,
                                    GhosttyKeyEncoder *key_encoder_out,
                                    GhosttyKeyEvent *key_event_out,
                                    GhosttyMouseEncoder *mouse_encoder_out,
                                    GhosttyMouseEvent *mouse_event_out)
{
    Session *s = sync_active_session(te_out, pty_fd_out, child_out, child_exited_out);
    TermEngine *te = te_out ? *te_out : NULL;
    if (!s || !te)
        return s;

    if (terminal_out)       *terminal_out       = term_engine_terminal(te);
    if (render_state_out)   *render_state_out   = term_engine_render_state(te);
    if (row_iter_out)       *row_iter_out       = term_engine_row_iter(te);
    if (row_cells_out)      *row_cells_out      = term_engine_row_cells(te);
    if (placement_iter_out) *placement_iter_out = term_engine_placement_iter(te);
    if (key_encoder_out)    *key_encoder_out    = term_engine_key_encoder(te);
    if (key_event_out)      *key_event_out      = term_engine_key_event(te);
    if (mouse_encoder_out)  *mouse_encoder_out  = term_engine_mouse_encoder(te);
    if (mouse_event_out)    *mouse_event_out    = term_engine_mouse_event(te);
    return s;
}

// Initialize the App with a single tab containing one leaf Session.
// `cwd` is NULL to inherit the process's launch directory (the default), or
// a restored workspace's cwd when session restore is placing the first tab.
// Returns the Session pointer for handle extraction, or NULL on failure.
static Session *app_init_first_tab(uint16_t cols, uint16_t rows,
                                    int cell_w, int cell_h,
                                    int max_scrollback,
                                    bool kitty_images,
                                    int kitty_image_storage_mb,
                                    const char *cwd)
{
    Session *s = session_create(cols, rows, cell_w, cell_h, max_scrollback, cwd,
                                kitty_images, kitty_image_storage_mb);
    if (!s) return NULL;

    Tab *tab = &app.tabs[0];
    tab->root    = pane_leaf(s);
    tab->focused = tab->root;
    tab->name[0] = '\0';
    app.n_tabs  = 1;
    app.active  = 0;
    return s;
}

// Switch to a different tab. Returns the new active Session*, or NULL.
static Session *app_switch_tab(int idx, TermEngine **te, int *pty_fd,
                                pid_t *child, bool *child_exited)
{
    if (idx < 0 || idx >= app.n_tabs || idx == app.active)
        return sync_active_session(te, pty_fd, child, child_exited);
    app.active = idx;
    return sync_active_session(te, pty_fd, child, child_exited);
}

// Add a new empty tab with an initial Session.
static Session *app_add_tab(uint16_t cols, uint16_t rows,
                             int cell_w, int cell_h,
                             int max_scrollback, const char *cwd,
                             bool kitty_images,
                             int kitty_image_storage_mb,
                             TermEngine **te, int *pty_fd,
                             pid_t *child, bool *child_exited)
{
    if (app.n_tabs >= FANGS_MAX_TABS) return NULL;

    Session *s = session_create(cols, rows, cell_w, cell_h, max_scrollback, cwd,
                                kitty_images, kitty_image_storage_mb);
    if (!s) return NULL;
    register_session_effects(s);
    update_session_effects(s, cols, rows, cell_w, cell_h);

    Tab *tab = &app.tabs[app.n_tabs];
    tab->root    = pane_leaf(s);
    tab->focused = tab->root;
    tab->name[0] = '\0';
    app.n_tabs++;
    app.active = app.n_tabs - 1;  // switch to the new tab
    g_session_dirty = true;
    return sync_active_session(te, pty_fd, child, child_exited);
}

// Like app_add_tab but sets tab->name immediately after creation.
static Session *app_add_tab_named(uint16_t cols, uint16_t rows,
                                  int cell_w, int cell_h,
                                  int max_scrollback, const char *cwd,
                                  const char *name,
                                  bool kitty_images,
                                  int kitty_image_storage_mb,
                                  TermEngine **te, int *pty_fd,
                                  pid_t *child, bool *child_exited)
{
    Session *s = app_add_tab(cols, rows, cell_w, cell_h, max_scrollback, cwd,
                             kitty_images, kitty_image_storage_mb,
                             te, pty_fd, child, child_exited);
    if (s && name && name[0]) {
        Tab *tab = &app.tabs[app.active];
        strncpy(tab->name, name, sizeof(tab->name) - 1);
        tab->name[sizeof(tab->name) - 1] = '\0';
    }
    return s;
}

// Close the active tab or focused pane in it.
static bool app_close_active(void)
{
    if (app.n_tabs <= 0) return false;

    Tab *tab = &app.tabs[app.active];
    if (!tab->root) {
        // Empty tab; remove it.
        for (int i = app.active; i < app.n_tabs - 1; i++)
            app.tabs[i] = app.tabs[i + 1];
        app.n_tabs--;
        if (app.active >= app.n_tabs) app.active = app.n_tabs - 1;
        if (app.active < 0) app.active = 0;
        return true;
    }

    // If the focused pane is the only leaf, close the whole tab.
    PaneNode *focused = tab->focused;
    if (pane_count_leaves(tab->root) <= 1 || !focused) {
        // Destroy the pane tree and clear the tab.
        pane_destroy(tab->root);
        tab->root = NULL;
        tab->focused = NULL;
        // Remove the tab from the array.
        for (int i = app.active; i < app.n_tabs - 1; i++)
            app.tabs[i] = app.tabs[i + 1];
        app.n_tabs--;
        if (app.active >= app.n_tabs) app.active = app.n_tabs - 1;
        if (app.active < 0) app.active = 0;
        return true;
    }

    // Close the focused pane in a multi-pane tab.
    PaneNode *new_focus = NULL;
    PaneNode *new_root = pane_close(tab->root, focused, &new_focus);
    tab->root = new_root;
    tab->focused = new_focus ? new_focus : pane_first_leaf(new_root);
    return true;
}

// Close a tab by index. Destroys its pane tree and shifts remaining tabs.
static void app_close_tab(int idx)
{
    if (idx < 0 || idx >= app.n_tabs) return;
    Tab *tab = &app.tabs[idx];
    if (tab->root) {
        pane_destroy(tab->root);
        tab->root = NULL;
        tab->focused = NULL;
    }
    int n = app.n_tabs - idx - 1;
    if (n > 0)
        memmove(&app.tabs[idx], &app.tabs[idx + 1], (size_t)n * sizeof(Tab));
    app.n_tabs--;
    // Keep the active selection pointing at the same tab after the shift;
    // closing the active tab itself falls through to the next one.
    if (idx < app.active) app.active--;
    if (app.active >= app.n_tabs) app.active = app.n_tabs - 1;
    if (app.active < 0) app.active = 0;
    g_session_dirty = true;
}

// Close a specific pane by its session's pane_id. If it's the last pane in its
// tab, the entire tab is removed.  Returns true if anything was closed.
static bool app_close_pane(uint64_t pane_id)
{
    for (int ti = 0; ti < app.n_tabs; ti++) {
        Tab *tt = &app.tabs[ti];
        if (!tt->root) continue;
        PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
        int nl = 0;
        pane_collect_leaves(tt->root, leaves, WORKSPACE_RAIL_MAX_PANES, &nl);
        for (int pi = 0; pi < nl; pi++) {
            if (leaves[pi]->kind != PANE_LEAF) continue;
            if (pane_id_for_session(leaves[pi]->leaf.session) != pane_id)
                continue;
            // Found it.
            if (nl <= 1) {
                // Last pane — close the whole tab.
                app_close_tab(ti);
            } else {
                PaneNode *new_focus = NULL;
                PaneNode *new_root = pane_close(tt->root, leaves[pi], &new_focus);
                tt->root = new_root;
                tt->focused = new_focus ? new_focus : pane_first_leaf(new_root);
            }
            return true;
        }
    }
    return false;
}

// Disarm armed-close state (called on any click outside the rail or timeout).
static void disarm_armed_close(void)
{
    g_armed_pane_id = 0;
    g_armed_deadline_ms = 0;
}

// Split the focused pane of the active tab.
static Session *app_split_focused(PaneKind dir, uint16_t cols, uint16_t rows,
                                   int cell_w, int cell_h,
                                   int max_scrollback, const char *cwd,
                                   bool kitty_images,
                                   int kitty_image_storage_mb,
                                   TermEngine **te, int *pty_fd,
                                   pid_t *child, bool *child_exited)
{
    if (app.n_tabs <= 0) return NULL;
    Tab *tab = &app.tabs[app.active];
    if (!tab->root || !tab->focused) return NULL;

    Session *new_s = session_create(cols, rows, cell_w, cell_h, max_scrollback, cwd,
                                    kitty_images, kitty_image_storage_mb);
    if (!new_s) return NULL;
    register_session_effects(new_s);
    update_session_effects(new_s, cols, rows, cell_w, cell_h);

    PaneNode *new_root = pane_split(tab->root, tab->focused, dir, new_s, 0.5f);
    tab->root = new_root;

    // Focus follows to the newly created pane (the leaf owning new_s).
    PaneNode *leaves[FANGS_MAX_TABS > 64 ? FANGS_MAX_TABS : 64];
    int nl = 0;
    pane_collect_leaves(new_root, leaves, (int)(sizeof(leaves)/sizeof(leaves[0])), &nl);
    tab->focused = pane_first_leaf(new_root);
    for (int i = 0; i < nl; i++) {
        if (leaves[i]->kind == PANE_LEAF && leaves[i]->leaf.session == new_s) {
            tab->focused = leaves[i];
            break;
        }
    }

    return sync_active_session(te, pty_fd, child, child_exited);
}

// Destroy all tabs and their pane-trees.
static void app_destroy_all(void)
{
    for (int i = 0; i < app.n_tabs; i++) {
        if (app.tabs[i].root) {
            pane_destroy(app.tabs[i].root);
            app.tabs[i].root = NULL;
            app.tabs[i].focused = NULL;
        }
    }
    app.n_tabs = 0;
    app.active = 0;
}

// Snap a raw scale factor to the nearest 0.25 step (1.49 -> 1.50, 1.62 -> 1.50).
static float snap_quarter(float v)
{
    return (float)((int)(v / 0.25f + 0.5f)) * 0.25f;
}

// Resolve the effective content scale used to rasterize the font at native
// pixel density, so font_size stays a *logical* size across machines.
//
// Resolution order:
//   1. FANGS_SCALE env — explicit override, always wins (e.g. FANGS_SCALE=1.5).
//   2. GetWindowScaleDPI() when it reports a real scale (macOS Retina = 2.0, and
//      Wayland/X11 setups whose GLFW does surface the content scale).
//   3. Monitor physical DPI fallback: GLFW's Wayland backend reports 1.0 even
//      under fractional scaling, so derive scale ~= DPI/96 from the monitor's
//      physical size and snap to a 0.25 step. Only departs from 1.0 for clearly
//      HiDPI panels (>= ~125 DPI) so normal ~96-DPI displays are never enlarged.
static Vector2 fangs_content_scale(void)
{
    const char *env = getenv("FANGS_SCALE");
    if (env && env[0] != '\0') {
        float v = (float)atof(env);
        if (v > 0.0f)
            return (Vector2){v, v};
    }

    Vector2 dpi = GetWindowScaleDPI();
    if (dpi.y > 1.01f)
        return dpi;

    int mon = GetCurrentMonitor();
    int res_w = GetMonitorWidth(mon), res_h = GetMonitorHeight(mon);
    int mm_w  = GetMonitorPhysicalWidth(mon), mm_h = GetMonitorPhysicalHeight(mon);
    if (mm_w > 0 && mm_h > 0 && res_w > 0 && res_h > 0) {
        float avg_dpi = 0.5f * (res_w * 25.4f / mm_w + res_h * 25.4f / mm_h);
        float raw = avg_dpi / 96.0f;
        if (raw >= 1.30f) {              // ~125+ DPI: genuinely HiDPI
            float snapped = snap_quarter(raw);
            if (snapped < 1.0f) snapped = 1.0f;
            if (snapped > 3.0f) snapped = 3.0f;
            return (Vector2){snapped, snapped};
        }
    }
    return (Vector2){1.0f, 1.0f};
}

// Codepoints baked into the terminal font atlas. Passing NULL/0 to
// LoadFontFromMemory only rasterizes the default 95 basic-Latin glyphs, so
// any other codepoint falls back to raylib's GLYPH_NOTFOUND '?' glyph at
// render time. TUI apps (tmux, vim, htop, ...) draw window borders with the
// Unicode box-drawing and block-element blocks; AI CLIs (claude, codex, ...)
// lean on smart quotes/dashes, chevron prompts, and braille spinners. All of
// these are fully covered by JetBrains Mono, so include them explicitly to
// keep those glyphs from silently degrading to '?'.
static const int *terminal_font_codepoints(int *out_count)
{
    // Basic Latin(95) + Latin-1 Supplement(96) + General Punctuation(112) +
    // Box Drawing(128) + Block Elements(32) + Geometric Shapes(96) +
    // Arrows(112) + Misc Technical(256) + Dingbats(192) + Braille(256).
    static int codepoints[95 + 96 + 112 + 128 + 32 + 96 + 112 + 256 + 192 + 256];
    static bool built = false;
    if (!built) {
        int n = 0;
        for (int cp = 0x20; cp <= 0x7E; cp++) codepoints[n++] = cp;     // Basic Latin
        for (int cp = 0xA0; cp <= 0xFF; cp++) codepoints[n++] = cp;     // Latin-1 Supplement (NBSP, accents, guillemets, degree, ...)
        for (int cp = 0x2000; cp <= 0x206F; cp++) codepoints[n++] = cp; // General Punctuation (smart quotes ' ' " ", en/em dash, ellipsis, bullet)
        for (int cp = 0x2500; cp <= 0x257F; cp++) codepoints[n++] = cp; // Box Drawing
        for (int cp = 0x2580; cp <= 0x259F; cp++) codepoints[n++] = cp; // Block Elements
        for (int cp = 0x25A0; cp <= 0x25FF; cp++) codepoints[n++] = cp; // Geometric Shapes (bullets/markers used by TUIs)
        for (int cp = 0x2190; cp <= 0x21FF; cp++) codepoints[n++] = cp; // Arrows
        for (int cp = 0x2300; cp <= 0x23FF; cp++) codepoints[n++] = cp; // Misc Technical (⏎, ⏵⏵ auto-accept indicator, ⌘, ...)
        for (int cp = 0x2700; cp <= 0x27BF; cp++) codepoints[n++] = cp; // Dingbats (❯ prompt chevron, ✓, ✗, ...)
        for (int cp = 0x2800; cp <= 0x28FF; cp++) codepoints[n++] = cp; // Braille Patterns (spinner animations)
        built = true;
    }
    *out_count = (int)(sizeof(codepoints) / sizeof(codepoints[0]));
    return codepoints;
}

static Font load_terminal_font(int font_size, int *cell_width, int *cell_height)
{
    Vector2 dpi_scale = fangs_content_scale();
    int font_size_px = (int)(font_size * dpi_scale.y);
    if (font_size_px < 1)
        font_size_px = 1;

    int cp_count = 0;
    const int *cps = terminal_font_codepoints(&cp_count);
    Font font = LoadFontFromMemory(".ttf", font_jetbrains_mono,
                         (int)sizeof(font_jetbrains_mono), font_size_px, (int *)cps, cp_count);
    if (font.texture.id == 0)
        return font;

    // The texture is rasterized at native pixel size; bilinear filtering keeps
    // fractional positioning from looking jagged without introducing blur.
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Vector2 glyph_size = MeasureTextEx(font, "M", font_size_px, 0);
    int measured_width  = (int)(glyph_size.x / dpi_scale.x);
    int measured_height = (int)(glyph_size.y / dpi_scale.y);

    *cell_width = measured_width < 1 ? 1 : measured_width;
    *cell_height = measured_height < 1 ? 1 : measured_height;
    return font;
}

static void compute_terminal_grid(int term_area_w, int pad,
                                  int cell_width, int cell_height,
                                  uint16_t *cols_out, uint16_t *rows_out)
{
    int scr_h = GetScreenHeight();
    int cols = (term_area_w - 2 * pad) / cell_width;
    int rows = (scr_h - 2 * pad) / cell_height;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    *cols_out = (uint16_t)cols;
    *rows_out = (uint16_t)rows;
}

// Per-pane variant of compute_terminal_grid: computes a grid sized to a
// specific pane's own pixel rect rather than the whole (possibly multi-pane)
// terminal area. Splits must each get their own grid — sizing every pane's
// PTY/VT engine to the full area's width/height while rendering it into a
// fraction of that space is what made split content look garbled (§16.4).
static void compute_pane_grid(int pane_w, int pane_h, int pad,
                              int cell_width, int cell_height,
                              uint16_t *cols_out, uint16_t *rows_out)
{
    int cols = (pane_w - 2 * pad) / cell_width;
    int rows = (pane_h - 2 * pad) / cell_height;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    *cols_out = (uint16_t)cols;
    *rows_out = (uint16_t)rows;
}

// Resize every leaf session in `tab` to its own pane rect (from
// layout_compute_panes over the given terminal-area bounds), rather than the
// single shared grid the whole area would imply. Writes the focused leaf's
// resulting cols/rows to *focused_cols_out/*focused_rows_out so callers can
// keep their "current session" grid vars (used for new-tab/split defaults,
// banners, cmdblocks row counts, ...) in sync with whichever pane has focus.
static void resize_pane_leaves_to_fit(Tab *tab,
                                      int term_x, int term_y,
                                      int term_w, int term_h,
                                      int pane_gap,
                                      int pad, int cell_width, int cell_height,
                                      uint16_t *focused_cols_out,
                                      uint16_t *focused_rows_out)
{
    if (!tab || !tab->root)
        return;

    PaneRectEntry rects[64];
    PaneRectCollector collector = { .entries = rects, .count = 0, .capacity = 64 };
    layout_compute_panes(tab->root, term_x, term_y, term_w, term_h,
                         pane_gap,
                         pane_rect_collect_cb, &collector);

    for (int i = 0; i < collector.count; i++) {
        PaneNode *leaf = collector.entries[i].leaf;
        int pw = collector.entries[i].w;
        int ph = collector.entries[i].h;
        Session *ss = leaf->leaf.session;
        if (!ss) continue;

        uint16_t cols, rows;
        compute_pane_grid(pw, ph, pad, cell_width, cell_height, &cols, &rows);

        TermEngine *ste = (TermEngine *)session_engine(ss);
        int spfd = session_pty_fd(ss);
        term_engine_resize(ste, cols, rows, cell_width, cell_height);
        update_session_effects(ss, cols, rows, cell_width, cell_height);
        pty_set_winsize(spfd, cols, rows, cell_width, cell_height);

        if (leaf == tab->focused) {
            if (focused_cols_out) *focused_cols_out = cols;
            if (focused_rows_out) *focused_rows_out = rows;
        }
    }
}

static bool export_screen_image(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    Image img = LoadImageFromScreen();
    if (!img.data)
        return false;

    bool ok = ExportImage(img, path);
    UnloadImage(img);
    return ok;
}

static int clamp_window_dimension(int value, int fallback, int min_value)
{
    if (value < min_value)
        return fallback;
    if (value > FANGS_MAX_WINDOW_DIM)
        return FANGS_MAX_WINDOW_DIM;
    return value;
}

static bool window_rect_visible_on_any_monitor(int x, int y, int w, int h)
{
    int count = GetMonitorCount();
    if (count < 1)
        return true;

    for (int i = 0; i < count; i++) {
        Vector2 mp = GetMonitorPosition(i);
        int mx = (int)mp.x;
        int my = (int)mp.y;
        int mw = GetMonitorWidth(i);
        int mh = GetMonitorHeight(i);
        bool intersects = x < mx + mw && x + w > mx
                       && y < my + mh && y + h > my;
        if (intersects)
            return true;
    }
    return false;
}

static void restore_window_position_if_visible(const AppConfig *cfg)
{
    if (!cfg || cfg->window_x < 0 || cfg->window_y < 0)
        return;
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    if (window_rect_visible_on_any_monitor(cfg->window_x, cfg->window_y, w, h))
        SetWindowPosition(cfg->window_x, cfg->window_y);
}

#ifndef __APPLE__
// Sets the taskbar/alt-tab icon on Linux (X11 and Wayland via GLFW). macOS
// skips this — it gets its icon from the .app bundle instead, and would
// otherwise briefly flash this one over the bundle's dock icon.
static void set_linux_window_icon(void)
{
    Image icon = LoadImageFromMemory(".png", icon_fangs_png, (int)sizeof(icon_fangs_png));
    if (icon.data == NULL)
        return;
    ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    SetWindowIcon(icon);
    UnloadImage(icon);
}
#endif

static bool save_window_geometry(AppConfig *cfg, const char *config_path)
{
    if (!cfg || !config_path)
        return false;
    Vector2 pos = GetWindowPosition();
    cfg->window_width = GetScreenWidth();
    cfg->window_height = GetScreenHeight();
    cfg->window_x = (int)pos.x;
    cfg->window_y = (int)pos.y;
    return config_save(cfg, config_path);
}

static bool write_phase3_smoke_report(const char *path,
                                      Layout lo,
                                      int term_area_w,
                                      uint16_t term_cols,
                                      uint16_t term_rows,
                                      bool screenshot_written)
{
    if (!path || path[0] == '\0')
        return false;

    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    bool pty_visible_unfocused = ui_sidebar_allows_pty_input(
        false, false, lo.sidebar_visible, false, false, false);
    bool pty_visible_focused = ui_sidebar_allows_pty_input(
        false, false, lo.sidebar_visible, true, false, false);

    fprintf(f,
        "phase3_smoke=ok\n"
        "window_w=%d\n"
        "window_h=%d\n"
        "sidebar_visible=%d\n"
        "sidebar_focused=%d\n"
        "layout_sidebar_visible=%d\n"
        "terminal_x=%d\n"
        "terminal_y=%d\n"
        "terminal_w=%d\n"
        "terminal_h=%d\n"
        "sidebar_x=%d\n"
        "sidebar_y=%d\n"
        "sidebar_w=%d\n"
        "sidebar_h=%d\n"
        "term_area_w=%d\n"
        "term_cols=%u\n"
        "term_rows=%u\n"
        "pty_allowed_visible_unfocused=%d\n"
        "pty_allowed_visible_focused=%d\n"
        "screenshot_written=%d\n",
        GetScreenWidth(),
        GetScreenHeight(),
        ui_sidebar_visible() ? 1 : 0,
        ui_sidebar_focused() ? 1 : 0,
        lo.sidebar_visible ? 1 : 0,
        lo.terminal.x,
        lo.terminal.y,
        lo.terminal.w,
        lo.terminal.h,
        lo.sidebar.x,
        lo.sidebar.y,
        lo.sidebar.w,
        lo.sidebar.h,
        term_area_w,
        (unsigned)term_cols,
        (unsigned)term_rows,
        pty_visible_unfocused ? 1 : 0,
        pty_visible_focused ? 1 : 0,
        screenshot_written ? 1 : 0);

    return fclose(f) == 0;
}


// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

// Map a raylib key constant to a GhosttyKey code.
// Returns GHOSTTY_KEY_UNIDENTIFIED for keys we don't handle.
static GhosttyKey raylib_key_to_ghostty(int rl_key)
{
    // Letters — raylib KEY_A..KEY_Z are contiguous, and so are
    // GHOSTTY_KEY_A..GHOSTTY_KEY_Z.
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return GHOSTTY_KEY_A + (rl_key - KEY_A);

    // Digits — raylib KEY_ZERO..KEY_NINE are contiguous.
    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return GHOSTTY_KEY_DIGIT_0 + (rl_key - KEY_ZERO);

    // Function keys — raylib KEY_F1..KEY_F12 are contiguous.
    if (rl_key >= KEY_F1 && rl_key <= KEY_F12)
        return GHOSTTY_KEY_F1 + (rl_key - KEY_F1);

    switch (rl_key) {
    case KEY_SPACE:       return GHOSTTY_KEY_SPACE;
    case KEY_ENTER:       return GHOSTTY_KEY_ENTER;
    case KEY_TAB:         return GHOSTTY_KEY_TAB;
    case KEY_BACKSPACE:   return GHOSTTY_KEY_BACKSPACE;
    case KEY_DELETE:      return GHOSTTY_KEY_DELETE;
    case KEY_ESCAPE:      return GHOSTTY_KEY_ESCAPE;
    case KEY_UP:          return GHOSTTY_KEY_ARROW_UP;
    case KEY_DOWN:        return GHOSTTY_KEY_ARROW_DOWN;
    case KEY_LEFT:        return GHOSTTY_KEY_ARROW_LEFT;
    case KEY_RIGHT:       return GHOSTTY_KEY_ARROW_RIGHT;
    case KEY_HOME:        return GHOSTTY_KEY_HOME;
    case KEY_END:         return GHOSTTY_KEY_END;
    case KEY_PAGE_UP:     return GHOSTTY_KEY_PAGE_UP;
    case KEY_PAGE_DOWN:   return GHOSTTY_KEY_PAGE_DOWN;
    case KEY_INSERT:      return GHOSTTY_KEY_INSERT;
    case KEY_MINUS:       return GHOSTTY_KEY_MINUS;
    case KEY_EQUAL:       return GHOSTTY_KEY_EQUAL;
    case KEY_LEFT_BRACKET:  return GHOSTTY_KEY_BRACKET_LEFT;
    case KEY_RIGHT_BRACKET: return GHOSTTY_KEY_BRACKET_RIGHT;
    case KEY_BACKSLASH:   return GHOSTTY_KEY_BACKSLASH;
    case KEY_SEMICOLON:   return GHOSTTY_KEY_SEMICOLON;
    case KEY_APOSTROPHE:  return GHOSTTY_KEY_QUOTE;
    case KEY_COMMA:       return GHOSTTY_KEY_COMMA;
    case KEY_PERIOD:      return GHOSTTY_KEY_PERIOD;
    case KEY_SLASH:       return GHOSTTY_KEY_SLASH;
    case KEY_GRAVE:       return GHOSTTY_KEY_BACKQUOTE;
    default:              return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

// Build a GhosttyMods bitmask from the current raylib modifier key state.
static GhosttyMods get_ghostty_mods(void)
{
    GhosttyMods mods = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
        mods |= GHOSTTY_MODS_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
        mods |= GHOSTTY_MODS_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT))
        mods |= GHOSTTY_MODS_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
        mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

// Return the unshifted Unicode codepoint for a raylib key, i.e. the
// character the key produces with no modifiers on a US layout.  The
// Kitty keyboard protocol requires this to identify keys.  Returns 0
// for keys that don't have a natural codepoint (arrows, F-keys, etc.).
static uint32_t raylib_key_unshifted_codepoint(int rl_key)
{
    if (rl_key >= KEY_A && rl_key <= KEY_Z)
        return 'a' + (uint32_t)(rl_key - KEY_A);
    if (rl_key >= KEY_ZERO && rl_key <= KEY_NINE)
        return '0' + (uint32_t)(rl_key - KEY_ZERO);

    switch (rl_key) {
    case KEY_SPACE:          return ' ';
    case KEY_MINUS:          return '-';
    case KEY_EQUAL:          return '=';
    case KEY_LEFT_BRACKET:   return '[';
    case KEY_RIGHT_BRACKET:  return ']';
    case KEY_BACKSLASH:      return '\\';
    case KEY_SEMICOLON:      return ';';
    case KEY_APOSTROPHE:     return '\'';
    case KEY_COMMA:          return ',';
    case KEY_PERIOD:         return '.';
    case KEY_SLASH:          return '/';
    case KEY_GRAVE:          return '`';
    default:                 return 0;
    }
}

// Encode a single Unicode codepoint into a UTF-8 byte buffer.
// Returns the number of bytes written (1–4).
// Invalid codepoints (> U+10FFFF) are replaced with U+FFFD.
static int utf8_encode(uint32_t cp, char out[4])
{
    // Unicode defines the maximum valid codepoint as U+10FFFF.
    // Codepoints above this value are invalid and should be replaced
    // with the Unicode replacement character U+FFFD.
    const uint32_t MAX_UNICODE = 0x10FFFF;
    const uint32_t REPLACEMENT_CHAR = 0xFFFD;

    if (cp > MAX_UNICODE) {
        cp = REPLACEMENT_CHAR;
    }

    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

// Map a raylib mouse button to a GhosttyMouseButton.
static GhosttyMouseButton raylib_mouse_to_ghostty(int rl_button)
{
    switch (rl_button) {
    case MOUSE_BUTTON_LEFT:    return GHOSTTY_MOUSE_BUTTON_LEFT;
    case MOUSE_BUTTON_RIGHT:   return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case MOUSE_BUTTON_MIDDLE:  return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    case MOUSE_BUTTON_SIDE:    return GHOSTTY_MOUSE_BUTTON_FOUR;
    case MOUSE_BUTTON_EXTRA:   return GHOSTTY_MOUSE_BUTTON_FIVE;
    case MOUSE_BUTTON_FORWARD: return GHOSTTY_MOUSE_BUTTON_SIX;
    case MOUSE_BUTTON_BACK:    return GHOSTTY_MOUSE_BUTTON_SEVEN;
    default:                   return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
}

// Encode a mouse event and write the resulting escape sequence to the pty.
// If the encoder produces no output (e.g. tracking is disabled), this is
// a no-op.
static void mouse_encode_and_write(int pty_fd, GhosttyMouseEncoder encoder,
                                   GhosttyMouseEvent event)
{
    char buf[128];
    size_t written = 0;
    GhosttyResult res = ghostty_mouse_encoder_encode(
        encoder, event, buf, sizeof(buf), &written);
    if (res == GHOSTTY_SUCCESS && written > 0)
        pty_write(pty_fd, buf, written);
}

// Poll raylib for mouse events and use the libghostty mouse encoder
// to produce the correct VT escape sequences, which are then written
// to the pty.  The encoder handles tracking mode (X10, normal, button,
// any-event) and output format (X10, UTF8, SGR, URxvt, SGR-Pixels)
// based on what the terminal application has requested.
static void handle_mouse(int pty_fd, GhosttyMouseEncoder encoder,
                         GhosttyMouseEvent event, GhosttyTerminal terminal,
                         int cell_width, int cell_height, int pad,
                         int term_origin_x, int term_origin_y,
                         int term_area_w, int term_area_h)
{
    int local_x = GetMouseX() - term_origin_x;
    int local_y = GetMouseY() - term_origin_y;
    if (local_x < 0 || local_x >= term_area_w
        || local_y < 0 || local_y >= term_area_h)
        return;

    // Sync encoder tracking mode and format from terminal state so
    // mode changes (e.g. applications enabling SGR mouse reporting)
    // are honoured automatically.
    ghostty_mouse_encoder_setopt_from_terminal(encoder, terminal);

    // Provide the encoder with the current terminal geometry so it
    // can convert pixel positions to cell coordinates.
    GhosttyMouseEncoderSize enc_size = {
        .size          = sizeof(GhosttyMouseEncoderSize),
        .screen_width  = (uint32_t)term_area_w,
        .screen_height = (uint32_t)term_area_h,
        .cell_width    = (uint32_t)cell_width,
        .cell_height   = (uint32_t)cell_height,
        .padding_top   = (uint32_t)pad,
        .padding_bottom = (uint32_t)pad,
        .padding_left  = (uint32_t)pad,
        .padding_right = (uint32_t)pad,
    };
    ghostty_mouse_encoder_setopt(encoder,
        GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &enc_size);

    // Track whether any button is currently held — the encoder uses
    // this to distinguish drags from plain motion.
    bool any_pressed = IsMouseButtonDown(MOUSE_BUTTON_LEFT)
                    || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
                    || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
    ghostty_mouse_encoder_setopt(encoder,
        GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &any_pressed);

    // Enable motion deduplication so the encoder suppresses redundant
    // motion events within the same cell.
    bool track_cell = true;
    ghostty_mouse_encoder_setopt(encoder,
        GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &track_cell);

    GhosttyMods mods = get_ghostty_mods();
    ghostty_mouse_event_set_mods(event, mods);
    ghostty_mouse_event_set_position(event,
        (GhosttyMousePosition){ .x = (float)local_x, .y = (float)local_y });

    // Check each mouse button for press/release events.
    static const int buttons[] = {
        MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE,
        MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA, MOUSE_BUTTON_FORWARD,
        MOUSE_BUTTON_BACK,
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        int rl_btn = buttons[i];
        GhosttyMouseButton gbtn = raylib_mouse_to_ghostty(rl_btn);
        if (gbtn == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
            continue;

        if (IsMouseButtonPressed(rl_btn)) {
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_PRESS);
            ghostty_mouse_event_set_button(event, gbtn);
            mouse_encode_and_write(pty_fd, encoder, event);
        } else if (IsMouseButtonReleased(rl_btn)) {
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_RELEASE);
            ghostty_mouse_event_set_button(event, gbtn);
            mouse_encode_and_write(pty_fd, encoder, event);
        }
    }

    // Mouse motion — send a motion event with whatever button is held
    // (or no button for pure motion in any-event tracking mode).
    Vector2 delta = GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f) {
        ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_MOTION);
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_LEFT);
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_RIGHT);
        else if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
            ghostty_mouse_event_set_button(event, GHOSTTY_MOUSE_BUTTON_MIDDLE);
        else
            ghostty_mouse_event_clear_button(event);
        mouse_encode_and_write(pty_fd, encoder, event);
    }

    // Scroll wheel handling.  When a mouse tracking mode is active the
    // wheel events are forwarded to the application as button 4/5
    // press+release pairs.  Otherwise we scroll the viewport through
    // the scrollback buffer so the user can review history.
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        // Check whether any mouse tracking mode is enabled.  If so,
        // the application wants to handle scroll events itself.
        bool mouse_tracking = false;
        ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);

        if (mouse_tracking) {
            // Forward to the application via the mouse encoder.
            GhosttyMouseButton scroll_btn = (wheel > 0.0f)
                ? GHOSTTY_MOUSE_BUTTON_FOUR
                : GHOSTTY_MOUSE_BUTTON_FIVE;
            ghostty_mouse_event_set_button(event, scroll_btn);
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_PRESS);
            mouse_encode_and_write(pty_fd, encoder, event);
            ghostty_mouse_event_set_action(event, GHOSTTY_MOUSE_ACTION_RELEASE);
            mouse_encode_and_write(pty_fd, encoder, event);
        } else {
            // Scroll the viewport through scrollback.  Scroll 3 rows
            // per wheel tick for a comfortable pace.  Delta is negative
            // to scroll up (into history), positive to scroll down.
            int delta = (wheel > 0.0f) ? -3 : 3;
            GhosttyTerminalScrollViewport sv = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = { .delta = delta },
            };
            ghostty_terminal_scroll_viewport(terminal, sv);
        }
    }
}

// Poll raylib for keyboard events and use the libghostty key encoder
// to produce the correct VT escape sequences, which are then written
// to the pty.  The encoder respects terminal modes (cursor key
// application mode, Kitty keyboard protocol, etc.) so we don't need
// to maintain our own escape-sequence tables.
static void handle_input(int pty_fd, GhosttyKeyEncoder encoder,
                         GhosttyKeyEvent event, GhosttyTerminal terminal)
{
    // Sync encoder options from the terminal so mode changes (e.g.
    // application cursor keys, Kitty keyboard protocol) are honoured.
    ghostty_key_encoder_setopt_from_terminal(encoder, terminal);

    // Drain printable characters from raylib's input queue.  We collect
    // them into a single UTF-8 buffer so the encoder can attach text
    // to the key event.
    char char_utf8[64];
    int char_utf8_len = 0;
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        char u8[4];
        int n = utf8_encode(ch, u8);
        if (char_utf8_len + n < (int)sizeof(char_utf8)) {
            memcpy(&char_utf8[char_utf8_len], u8, n);
            char_utf8_len += n;
        }
    }

    // All raylib keys we want to check for press/repeat events.
    // Letters and digits are handled via ranges; everything else is
    // enumerated explicitly.
    static const int special_keys[] = {
        KEY_SPACE, KEY_ENTER, KEY_TAB, KEY_BACKSPACE, KEY_DELETE,
        KEY_ESCAPE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_HOME, KEY_END, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_INSERT,
        KEY_MINUS, KEY_EQUAL, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET,
        KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA,
        KEY_PERIOD, KEY_SLASH, KEY_GRAVE,
        KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
        KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    };

    // Build the set of raylib keys to scan: letters + digits + specials.
    int keys_to_check[26 + 10 + sizeof(special_keys) / sizeof(special_keys[0])];
    int num_keys = 0;
    for (int k = KEY_A; k <= KEY_Z; k++)
        keys_to_check[num_keys++] = k;
    for (int k = KEY_ZERO; k <= KEY_NINE; k++)
        keys_to_check[num_keys++] = k;
    for (size_t i = 0; i < sizeof(special_keys) / sizeof(special_keys[0]); i++)
        keys_to_check[num_keys++] = special_keys[i];

    GhosttyMods mods = get_ghostty_mods();

    for (int i = 0; i < num_keys; i++) {
        int rl_key = keys_to_check[i];
        bool pressed  = IsKeyPressed(rl_key);
        bool repeated = IsKeyPressedRepeat(rl_key);
        bool released = IsKeyReleased(rl_key);
        if (!pressed && !repeated && !released)
            continue;

        GhosttyKey gkey = raylib_key_to_ghostty(rl_key);
        if (gkey == GHOSTTY_KEY_UNIDENTIFIED)
            continue;

        GhosttyKeyAction action = released  ? GHOSTTY_KEY_ACTION_RELEASE
                                : pressed   ? GHOSTTY_KEY_ACTION_PRESS
                                            : GHOSTTY_KEY_ACTION_REPEAT;

        ghostty_key_event_set_key(event, gkey);
        ghostty_key_event_set_action(event, action);
        ghostty_key_event_set_mods(event, mods);

        // The unshifted codepoint is the character the key produces
        // with no modifiers.  The Kitty protocol needs it to identify
        // keys independent of the current shift state.
        uint32_t ucp = raylib_key_unshifted_codepoint(rl_key);
        ghostty_key_event_set_unshifted_codepoint(event, ucp);

        // Consumed mods are modifiers the platform's text input
        // already accounted for when producing the UTF-8 text.
        // For printable keys, shift is consumed (it turns 'a' → 'A').
        // For non-printable keys nothing is consumed.
        GhosttyMods consumed = 0;
        if (ucp != 0 && (mods & GHOSTTY_MODS_SHIFT))
            consumed |= GHOSTTY_MODS_SHIFT;
        ghostty_key_event_set_consumed_mods(event, consumed);

        // Attach any UTF-8 text that raylib produced for this frame.
        // For unmodified printable keys this is the character itself;
        // for special keys or ctrl combos there's typically no text.
        // Release events never carry text.
        if (char_utf8_len > 0 && !released) {
            ghostty_key_event_set_utf8(event, char_utf8, (size_t)char_utf8_len);
            // Only attach the text to the first key event this frame
            // to avoid duplicating it.
            char_utf8_len = 0;
        } else {
            ghostty_key_event_set_utf8(event, NULL, 0);
        }

        char buf[128];
        size_t written = 0;
        GhosttyResult res = ghostty_key_encoder_encode(
            encoder, event, buf, sizeof(buf), &written);
        if (res == GHOSTTY_SUCCESS && written > 0) {
            pty_write(pty_fd, buf, written);
            // Text was consumed by the encoder — clear it so the
            // fallback below doesn't double-send.
            char_utf8_len = 0;
        }
    }

    // Fallback: on some platforms (e.g. VMs) the character event arrives
    // a frame after the key-press event.  If we collected UTF-8 text but
    // no key event consumed it, write it directly to the PTY so input
    // isn't silently dropped.
    if (char_utf8_len > 0)
        pty_write(pty_fd, char_utf8, char_utf8_len);
}

// Handle scrollbar drag-to-scroll.  When the user clicks in the
// scrollbar region we begin tracking; while held we map the mouse Y
// position directly to an absolute scroll offset so the thumb follows
// the cursor exactly.
//
// Returns true while a drag is in progress so the caller can skip
// normal mouse handling if desired.
static bool handle_scrollbar(GhosttyTerminal terminal,
                             GhosttyRenderState render_state,
                             bool *dragging,
                             int term_origin_x, int term_origin_y,
                             int term_area_w, int term_area_h,
                             float scale)
{
    // Query scrollbar geometry from the terminal.
    GhosttyTerminalScrollbar scrollbar = {0};
    if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                             &scrollbar) != GHOSTTY_SUCCESS)
        return false;

    // Nothing to drag when the viewport covers all content.
    if (scrollbar.total <= scrollbar.len) {
        *dragging = false;
        return false;
    }

    int bar_width = (int)(5.0f * scale);
    int bar_margin = (int)(4.0f * scale);
    int bar_left = term_origin_x + term_area_w - bar_width - bar_margin;
    int bar_right = term_origin_x + term_area_w - bar_margin;
    Vector2 mpos = GetMousePosition();

    // Padded drag hit region: a bit wider than the visual thumb so users can
    // grab the track without pixel-perfect accuracy. Visual thumb stays inside
    // bar_left..bar_right; only input handling uses the expanded bounds.
    int hit_left  = bar_left - (int)(8.0f * scale);
    int hit_right = bar_right + (int)(4.0f * scale);

    // Start a drag when the user clicks in the padded scrollbar region.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
        && mpos.x >= hit_left && mpos.x <= hit_right
        && mpos.y >= term_origin_y && mpos.y < term_origin_y + term_area_h) {
        *dragging = true;
    }

    if (*dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        // Map mouse Y directly to an absolute scroll offset.
        // Y=0 → top of scrollback (offset 0), Y=scr_h → bottom
        // (offset = total - len).
        uint64_t scrollable = scrollbar.total - scrollbar.len;
        double frac = (double)(mpos.y - term_origin_y) / (double)term_area_h;
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        int64_t target = (int64_t)(frac * (double)scrollable);

        intptr_t delta = (intptr_t)(target - (int64_t)scrollbar.offset);
        if (delta != 0) {
            GhosttyTerminalScrollViewport sv = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = { .delta = delta },
            };
            ghostty_terminal_scroll_viewport(terminal, sv);
            ghostty_render_state_update(render_state, terminal);
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        *dragging = false;

    return *dragging;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Render the current terminal screen using the RenderState API.
//
// For each row/cell we read the grapheme codepoints and the cell's style,
// resolve foreground/background colors via the palette, and draw each
// character individually with DrawTextEx.  This supports per-cell colors
// from SGR sequences (bold, 256-color, 24-bit RGB, etc.).
//
// cell_width and cell_height are the measured dimensions of a single
// monospace glyph at the current font size, in screen (logical) pixels.
// font_size is the logical font size (before DPI scaling).
// pad is the pixel margin between the window edges and the terminal grid.
//
// If scrollbar is non-NULL, a scrollbar indicator is drawn on the right
// edge of the terminal area.
// --- Host-side text selection + clipboard ----------------------------------
// The engine's selection API is snapshot-based; for full control of the
// highlight + copied text we track a linear viewport selection in the host and
// capture the selected cells' text during the render pass.
typedef struct { bool active, dragging; int sr, sc, er, ec; } Selection;
static Selection g_sel = {0};
static char g_sel_text[1 << 16];
static int  g_sel_len = 0;
static int  g_sel_row = -1;   // last row appended during capture

static void sel_ordered(const Selection *s, int *r0, int *c0, int *r1, int *c1)
{
    if (s->sr < s->er || (s->sr == s->er && s->sc <= s->ec)) {
        *r0 = s->sr; *c0 = s->sc; *r1 = s->er; *c1 = s->ec;
    } else {
        *r0 = s->er; *c0 = s->ec; *r1 = s->sr; *c1 = s->sc;
    }
}

static bool sel_contains(int row, int col)
{
    if (!g_sel.active) return false;
    int r0, c0, r1, c1; sel_ordered(&g_sel, &r0, &c0, &r1, &c1);
    if (row < r0 || row > r1) return false;
    if (r0 == r1) return col >= c0 && col <= c1;
    if (row == r0) return col >= c0;
    if (row == r1) return col <= c1;
    return true;
}

// Append a selected cell's text during the render pass; '\n' on row change.
static void sel_capture(int row, const char *s, int n)
{
    if (!g_sel.active) return;
    if (g_sel_row != -1 && row != g_sel_row && g_sel_len < (int)sizeof(g_sel_text) - 1)
        g_sel_text[g_sel_len++] = '\n';
    g_sel_row = row;
    if (g_sel_len + n < (int)sizeof(g_sel_text) - 1) {
        memcpy(g_sel_text + g_sel_len, s, (size_t)n);
        g_sel_len += n;
    }
    g_sel_text[g_sel_len] = '\0';
}

// Copy the captured selection to the clipboard, trimming trailing spaces/line.
static void sel_copy_to_clipboard(void)
{
    if (!g_sel.active || g_sel_len == 0) return;
    static char out[sizeof(g_sel_text)];
    int n = 0, line_start = 0;
    for (int i = 0; i <= g_sel_len; i++) {
        char c = (i < g_sel_len) ? g_sel_text[i] : '\n';
        if (c == '\n') {
            while (n > line_start && out[n - 1] == ' ') n--;
            if (i < g_sel_len) out[n++] = '\n';
            line_start = n;
        } else {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    if (n > 0) SetClipboardText(out);
}

// Paste clipboard text into the pty, bracketed-paste-encoded when the app
// enabled DECSET 2004 (so newlines don't auto-execute in a supporting shell).
static void do_paste(int pty_fd, GhosttyTerminal terminal)
{
    const char *clip = GetClipboardText();
    if (!clip || !clip[0]) return;
    size_t len = strlen(clip);

    bool bracketed = false;
    ghostty_terminal_mode_get(terminal, GHOSTTY_MODE_BRACKETED_PASTE, &bracketed);

    size_t need = 0;
    char *data = malloc(len + 1);
    if (!data) return;
    memcpy(data, clip, len + 1);
    ghostty_paste_encode(data, len, bracketed, NULL, 0, &need);   // query size

    char *buf = malloc(need > 0 ? need : len + 1);
    if (buf) {
        size_t written = 0;
        memcpy(data, clip, len + 1);   // refresh (encode may modify data)
        if (ghostty_paste_encode(data, len, bracketed, buf, need, &written) == GHOSTTY_SUCCESS)
            pty_write(pty_fd, buf, written);
        free(buf);
    }
    free(data);
}

// --- Clickable URLs --------------------------------------------------------
// The viewport text grid is captured each render so a Ctrl/Cmd+click can be
// hit-tested against URLs without re-walking the engine state.
#define UI_MAX_ROWS 300
#define UI_MAX_COLS 500
static char g_row_text[UI_MAX_ROWS][UI_MAX_COLS * 4 + 4];
static int  g_row_off[UI_MAX_ROWS][UI_MAX_COLS + 1];   // byte offset per column
static int  g_row_len[UI_MAX_ROWS];
static int  g_row_cols[UI_MAX_ROWS];
static int  g_rows_captured = 0;

static void row_capture(int row, int col, const char *s, int n)
{
    if (row < 0 || row >= UI_MAX_ROWS || col < 0 || col >= UI_MAX_COLS) return;
    if (col == 0) { g_row_len[row] = 0; g_row_cols[row] = 0; }
    g_row_off[row][col] = g_row_len[row];
    if (g_row_len[row] + n < UI_MAX_COLS * 4) {
        memcpy(&g_row_text[row][g_row_len[row]], s, (size_t)n);
        g_row_len[row] += n;
    }
    g_row_text[row][g_row_len[row]] = '\0';
    g_row_cols[row] = col + 1;
    if (row + 1 > g_rows_captured) g_rows_captured = row + 1;
}

static bool url_is_char(unsigned char c)
{
    return (c >= '!' && c <= '~') && c != '"' && c != '<' && c != '>'
        && c != '`' && c != '{' && c != '}' && c != '\\' && c != '|' && c != '^';
}

static bool url_starts(const char *s, int n)
{
    return (n >= 7 && strncmp(s, "http://", 7) == 0)
        || (n >= 8 && strncmp(s, "https://", 8) == 0)
        || (n >= 7 && strncmp(s, "file://", 7) == 0)
        || (n >= 6 && strncmp(s, "ftp://", 6) == 0);
}

// If (row,col) sits on a URL in the captured grid, copy it to out and return true.
static bool url_at(int row, int col, char *out, int out_size)
{
    if (row < 0 || row >= g_rows_captured || col < 0 || col >= g_row_cols[row])
        return false;
    const char *line = g_row_text[row];
    int len = g_row_len[row];
    int click = g_row_off[row][col];
    for (int i = 0; i < len; i++) {
        if (!url_starts(line + i, len - i)) continue;
        int j = i;
        while (j < len && url_is_char((unsigned char)line[j])) j++;
        while (j > i && strchr(".,;:!?)]}'", line[j - 1])) j--;   // trim trailing punctuation
        if (click >= i && click < j) {
            int m = j - i;
            if (m >= out_size) m = out_size - 1;
            memcpy(out, line + i, (size_t)m);
            out[m] = '\0';
            return true;
        }
        i = j;
    }
    return false;
}

static bool path_tok_char(unsigned char c)
{
    return (c >= '!' && c <= '~') && c != '"' && c != '\'' && c != '`'
        && c != '(' && c != ')' && c != '[' && c != ']' && c != '{' && c != '}'
        && c != '<' && c != '>' && c != '|' && c != '\\' && c != '^'
        && c != ',' && c != ';';
}

// If (row,col) sits on a file-path-like token — optionally suffixed with
// :line or :line:col, e.g. "src/main.c:1919" (this repo's own reference
// convention) — that resolves to a real file relative to `cwd`, copy the
// resolved absolute path to `out` and return true. Existence is checked via
// stat() so plain prose ("e.g.", "etc.") never lights up as clickable.
static bool file_ref_at(int row, int col, const char *cwd, char *out, int out_size)
{
    if (row < 0 || row >= g_rows_captured || col < 0 || col >= g_row_cols[row])
        return false;
    const char *line = g_row_text[row];
    int len = g_row_len[row];
    int click = g_row_off[row][col];
    int i = 0;
    while (i < len) {
        if (!path_tok_char((unsigned char)line[i])) { i++; continue; }
        int j = i;
        while (j < len && path_tok_char((unsigned char)line[j])) j++;
        int tok_end = j;
        while (tok_end > i && strchr(".,;:!?)]}'", line[tok_end - 1])) tok_end--;
        if (click >= i && click < tok_end) {
            char tok[1024];
            int m = tok_end - i;
            if (m >= (int)sizeof(tok)) m = (int)sizeof(tok) - 1;
            memcpy(tok, line + i, (size_t)m);
            tok[m] = '\0';

            // Peel up to two trailing :<digits> groups (line, then col).
            for (int k = 0; k < 2; k++) {
                char *colon = strrchr(tok, ':');
                if (!colon || !*(colon + 1)) break;
                bool all_digit = true;
                for (char *q = colon + 1; *q; q++)
                    if (!isdigit((unsigned char)*q)) { all_digit = false; break; }
                if (!all_digit) break;
                *colon = '\0';
            }

            bool has_slash = strchr(tok, '/') != NULL;
            bool has_home = tok[0] == '~';
            const char *dot = strrchr(tok, '.');
            bool has_ext = dot && dot != tok && *(dot + 1);
            if (tok[0] && (has_slash || has_home || has_ext)) {
                char resolved[2048];
                resolved[0] = '\0';
                if (tok[0] == '/') {
                    snprintf(resolved, sizeof(resolved), "%s", tok);
                } else if (tok[0] == '~') {
                    const char *home = getenv("HOME");
                    snprintf(resolved, sizeof(resolved), "%s%s", home ? home : "", tok + 1);
                } else if (cwd && cwd[0]) {
                    snprintf(resolved, sizeof(resolved), "%s/%s", cwd, tok);
                }
                struct stat st;
                if (resolved[0] && stat(resolved, &st) == 0) {
                    snprintf(out, (size_t)out_size, "%s", resolved);
                    return true;
                }
            }
        }
        i = j;
    }
    return false;
}

// Open a URL with the OS handler, double-forked + setsid so it detaches cleanly
// (no zombie, no controlling terminal). No shell, so the URL isn't interpreted.
static void open_url(const char *url)
{
#if defined(__APPLE__)
    const char *opener = "open";
#else
    const char *opener = "xdg-open";
#endif
    pid_t pid = fork();
    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 == 0) {
            setsid();
            execlp(opener, opener, url, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (pid > 0)
        waitpid(pid, NULL, 0);
}

// --- Find (Ctrl+F): highlight matches in the visible scrollback -------------
static bool g_search_active = false;
static char g_search_query[128] = "";

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int ci_find(const char *hay, int haylen, const char *needle, int needlen, int from)
{
    if (needlen == 0) return -1;
    for (int i = from; i + needlen <= haylen; i++) {
        int k = 0;
        while (k < needlen && lc(hay[i + k]) == lc(needle[k])) k++;
        if (k == needlen) return i;
    }
    return -1;
}

// Map a byte offset within a captured row back to its column.
static int col_of_byte(int row, int off)
{
    for (int c = 0; c < g_row_cols[row]; c++) {
        int start = g_row_off[row][c];
        int end = (c + 1 < g_row_cols[row]) ? g_row_off[row][c + 1] : g_row_len[row];
        if (off >= start && off < end) return c;
    }
    return g_row_cols[row] > 0 ? g_row_cols[row] - 1 : 0;
}

// One-shot, explicit (Enter-triggered — never per-frame/per-keystroke) scan
// of every open session's full screen+scrollback text for `query`, grouped
// one result per tab. Opens a results popover (mirrors the Attention Inbox
// pattern: tag = RAIL_MENU_SEARCH_BASE + index, pane id cached in parallel).
static void perform_cross_workspace_search(const char *query)
{
    if (!query || !query[0])
        return;
    int qlen = (int)strlen(query);
    const char *home = getenv("HOME");

    UiMenuItem sitems[WORKSPACE_RAIL_MAX_TABS];
    memset(sitems, 0, sizeof(sitems));
    g_search_result_count = 0;

    for (int ti = 0; ti < app.n_tabs && g_search_result_count < WORKSPACE_RAIL_MAX_TABS; ti++) {
        Tab *tt = &app.tabs[ti];
        if (!tt->root)
            continue;
        PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
        int n = 0;
        pane_collect_leaves(tt->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n);

        uint64_t match_pane_id = 0;
        char snippet[160] = "";
        for (int pi = 0; pi < n && match_pane_id == 0; pi++) {
            if (leaves[pi]->kind != PANE_LEAF)
                continue;
            Session *ss = leaves[pi]->leaf.session;
            if (!ss)
                continue;
            char *dump = term_engine_dump_text((TermEngine *)session_engine(ss));
            if (!dump)
                continue;
            int dlen = (int)strlen(dump);
            int pos = ci_find(dump, dlen, query, qlen, 0);
            if (pos >= 0) {
                // Extract the containing line and redact it — a match
                // snippet could sit right next to a secret in the output,
                // same security boundary as the AI context/hover preview.
                int ls = pos;
                while (ls > 0 && dump[ls - 1] != '\n') ls--;
                int le = pos;
                while (le < dlen && dump[le] != '\n') le++;
                int llen = le - ls;
                if (llen > 140) llen = 140;
                char raw_line[160];
                snprintf(raw_line, sizeof(raw_line), "%.*s", llen, dump + ls);
                char *redacted = redact_secrets(raw_line);
                snprintf(snippet, sizeof(snippet), "%s", redacted ? redacted : raw_line);
                free(redacted);
                match_pane_id = pane_id_for_session(ss);
            }
            free(dump);
        }

        if (match_pane_id != 0) {
            char label[64];
            if (tt->name[0]) {
                snprintf(label, sizeof(label), "%s", tt->name);
            } else {
                Session *rep = tt->focused ? tt->focused->leaf.session : tab_first_leaf_session(tt);
                const char *title = rep ? cmdblocks_title((CmdBlocks *)session_cmdblocks(rep)) : NULL;
                if (title && title[0])
                    snprintf(label, sizeof(label), "%s", title);
                else
                    workspace_cwd_label(rep ? session_cwd(rep) : "",
                                        home ? home : "", label, sizeof(label));
            }
            int idx = g_search_result_count;
            snprintf(sitems[idx].label, sizeof(sitems[idx].label), "%s: %s", label, snippet);
            sitems[idx].tag = RAIL_MENU_SEARCH_BASE + idx;
            sitems[idx].tint = UI_COLOR_TEXT;
            g_search_result_pane_cache[idx] = match_pane_id;
            g_search_result_count++;
        }
    }

    if (g_search_result_count == 0) {
        toast_push(TOAST_INFO, "No matches found");
        return;
    }

    int mx = GetScreenWidth() / 2;
    int my = GetScreenHeight() / 2;
    ui_menu_open(&g_rail_menu, sitems, g_search_result_count, mx, my);
    ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
}

// Show a popover listing the changed files in cwd's git worktree, so an
// agent's progress can be reviewed without switching to its pane. One-shot,
// foreground call — only invoked from an explicit menu/badge click, never
// per-frame, matching the same guardrail as perform_cross_workspace_search.
static void perform_view_changes(const char *cwd)
{
    if (!cwd || !cwd[0]) {
        toast_push(TOAST_INFO, "No changes to show.");
        return;
    }

    WorkspaceGitFileChange changes[WORKSPACE_GIT_STATUS_MAX_FILES];
    int total = 0;
    int n = workspace_git_status_list_changes(cwd, changes,
                                              WORKSPACE_GIT_STATUS_MAX_FILES, &total);
    if (n == 0) {
        toast_push(TOAST_INFO, "No changes to show.");
        return;
    }

    UiMenuItem ditems[WORKSPACE_GIT_STATUS_MAX_FILES + 1];
    memset(ditems, 0, sizeof(ditems));
    g_diff_result_count = 0;

    for (int i = 0; i < n; i++) {
        int idx = g_diff_result_count;
        char stat_suffix[24] = "";
        if (changes[i].insertions >= 0 || changes[i].deletions >= 0) {
            snprintf(stat_suffix, sizeof(stat_suffix), " (+%d/-%d)",
                    changes[i].insertions >= 0 ? changes[i].insertions : 0,
                    changes[i].deletions >= 0 ? changes[i].deletions : 0);
        }
        snprintf(ditems[idx].label, sizeof(ditems[idx].label), "%s %s%s",
                changes[i].status, changes[i].path, stat_suffix);
        ditems[idx].tag = RAIL_MENU_DIFF_BASE + idx;
        ditems[idx].tint = (changes[i].status[0] == 'D' || changes[i].status[1] == 'D')
            ? UI_COLOR_INLINE_ERROR : UI_COLOR_TEXT;
        ditems[idx].separator = false;
        snprintf(g_diff_result_path_cache[idx], sizeof(g_diff_result_path_cache[idx]),
                "%s", changes[i].path);
        g_diff_result_count++;
    }

    if (total > n) {
        int idx = g_diff_result_count++;
        snprintf(ditems[idx].label, sizeof(ditems[idx].label),
                "+%d more changed", total - n);
        ditems[idx].tag = -1; // informational only, not clickable
        ditems[idx].tint = UI_COLOR_SUBTITLE;
        ditems[idx].separator = false;
    }

    int mx = GetScreenWidth() / 2;
    int my = GetScreenHeight() / 2;
    ui_menu_open(&g_rail_menu, ditems, g_diff_result_count, mx, my);
    ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
}

// Highlight every visible occurrence of the query; returns the match count.
static int draw_search_highlights(int origin_x, int origin_y,
                                  int pad, int cell_width, int cell_height)
{
    int qlen = (int)strlen(g_search_query);
    if (qlen == 0) return 0;
    int total = 0;
    for (int r = 0; r < g_rows_captured; r++) {
        const char *line = g_row_text[r];
        int len = g_row_len[r];
        int from = 0, m;
        while ((m = ci_find(line, len, g_search_query, qlen, from)) >= 0) {
            int c0 = col_of_byte(r, m);
            int c1 = col_of_byte(r, m + qlen - 1);
            for (int c = c0; c <= c1; c++)
                DrawRectangle(origin_x + pad + c * cell_width,
                              origin_y + pad + r * cell_height,
                              cell_width, cell_height, UI2RAY(g_ui_theme.search_hit));
            total++;
            from = m + qlen;
        }
    }
    return total;
}

static void search_input(void)
{
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        int l = (int)strlen(g_search_query);
        if (ch >= 32 && ch < 127 && l < (int)sizeof(g_search_query) - 1) {
            g_search_query[l] = (char)ch;
            g_search_query[l + 1] = '\0';
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE)) {
        int l = (int)strlen(g_search_query);
        if (l > 0) g_search_query[l - 1] = '\0';
    }
}

static void draw_search_box(Font font, int term_origin_x, int term_area_w, int matches)
{
    int w = 300, h = 34;
    int x = term_origin_x + term_area_w - w - 16;
    if (x < term_origin_x + 8) x = term_origin_x + 8;
    int y = 12;
    DrawRectangle(x, y, w, h, UI2RAY(g_ui_theme.search_bg));
    DrawRectangleLines(x, y, w, h, UI2RAY(g_ui_theme.search_border));
    char label[200];
    snprintf(label, sizeof(label), "Find: %s", g_search_query);
    DrawTextEx(font, label, (Vector2){(float)x + 10, (float)y + 8}, 16.0f, 0,
               UI2RAY(g_ui_theme.search_text));
    char cnt[32];
    snprintf(cnt, sizeof(cnt), "%d", matches);
    Vector2 cs = MeasureTextEx(font, cnt, 14.0f, 0);
    DrawTextEx(font, cnt, (Vector2){(float)x + w - cs.x - 10, (float)y + 10}, 14.0f, 0,
               UI2RAY(g_ui_theme.search_count));
    if (g_search_query[0]) {
        DrawTextEx(font, "Enter: search all workspaces",
                  (Vector2){(float)x + 10, (float)(y + h + 4)}, 11.0f, 0,
                  UI2RAY(g_ui_theme.search_count));
    }
}

static void draw_pane_chrome_and_content(PaneNode *leaf,
                                         int px, int py, int pw, int ph,
                                         int header_h, bool focused, float scale,
                                         Font font, Font bold_font,
                                         int cell_width, int cell_height, int font_size, int pad,
                                         GhosttyTerminal terminal,
                                         GhosttyRenderState rs,
                                         GhosttyRenderStateRowIterator ri,
                                         GhosttyRenderStateRowCells rc,
                                         GhosttyKittyGraphicsPlacementIterator pi,
                                         KittyImageRenderer *kitty_renderer,
                                         GhosttyTerminalScrollbar *lsb_ptr,
                                         AppConfig *cfg, uint64_t now_ms,
                                         int *out_inner_rows,
                                         uint64_t pane_id);

static void render_terminal(GhosttyRenderState render_state,
                            GhosttyRenderStateRowIterator row_iter,
                            GhosttyRenderStateRowCells cells,
                            Font font, Font bold_font,
                            int cell_width, int cell_height,
                            int font_size,
                            int pad,
                            int term_area_w,
                            int term_area_h,
                            const GhosttyTerminalScrollbar *scrollbar,
                            GhosttyTerminal terminal,
                            GhosttyKittyGraphicsPlacementIterator placement_iter,
                            KittyImageRenderer *kitty_renderer,
                            int origin_x, int origin_y,
                            AppConfig *cfg,
                            uint64_t now_ms,
                            uint64_t pane_id,
                            float scale)
{
    // Grab colors (palette, default fg/bg) from the render state so we
    // can resolve palette-indexed cell colors.
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_colors_get(render_state, &colors) != GHOSTTY_SUCCESS)
        return;

    // Reset per-frame capture: selection text + the URL hit-test grid.
    g_rows_captured = 0;
    if (g_sel.active) { g_sel_len = 0; g_sel_row = -1; g_sel_text[0] = '\0'; }

    // Obtain the Kitty graphics storage from the terminal.  This is a
    // borrowed pointer valid until the next mutating terminal call.
    GhosttyKittyGraphics kitty_gfx = NULL;
    bool has_kitty = (ghostty_terminal_get(terminal,
        GHOSTTY_TERMINAL_DATA_KITTY_GRAPHICS, &kitty_gfx) == GHOSTTY_SUCCESS
        && kitty_gfx != NULL);

    // Populate the row iterator from the current render state snapshot.
    if (ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS)
        return;

    // --- Layer 1: images below cell backgrounds (z < INT32_MIN/2) ---
    if (has_kitty && placement_iter) {
        kitty_image_renderer_draw_layer(kitty_renderer, terminal, kitty_gfx, placement_iter,
                                        origin_x, origin_y,
                                        cell_width, cell_height, pad,
                                        GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_BG);
    }

    // Background/capture pass: draw cell backgrounds and selection, and build
    // the text/URL capture grids before below-text images are painted.
    int y = origin_y + pad;
    while (ghostty_render_state_row_iterator_next(row_iter)) {
        if (ghostty_render_state_row_get(row_iter,
                GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS)
            continue;

        int x = origin_x + pad;

        while (ghostty_render_state_row_cells_next(cells)) {
            int sel_row = (y - (origin_y + pad)) / cell_height;
            int sel_col = (x - (origin_x + pad)) / cell_width;
            bool cell_selected = sel_contains(sel_row, sel_col);

            uint32_t grapheme_len = 0;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);

            if (grapheme_len == 0) {
                GhosttyColorRgb bg = {0};
                if (ghostty_render_state_row_cells_get(cells,
                        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg) == GHOSTTY_SUCCESS) {
                    DrawRectangle(x, y, cell_width, cell_height,
                                  (Color){ bg.r, bg.g, bg.b, 255 });
                }

                if (cell_selected) {
                    DrawRectangle(x, y, cell_width, cell_height, UI2RAY(g_ui_theme.selection));
                    sel_capture(sel_row, " ", 1);
                }
                row_capture(sel_row, sel_col, " ", 1);

                x += cell_width;
                continue;
            }

            uint32_t codepoints[16];
            uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);

            char text[64];
            int pos = 0;
            for (uint32_t i = 0; i < len && pos < 60; i++) {
                char u8[4];
                int n = utf8_encode(codepoints[i], u8);
                memcpy(&text[pos], u8, n);
                pos += n;
            }
            text[pos] = '\0';

            GhosttyColorRgb fg = colors.foreground;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);

            GhosttyColorRgb bg_rgb = colors.background;
            bool has_bg = ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg_rgb) == GHOSTTY_SUCCESS;

            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            if (style.inverse) {
                GhosttyColorRgb tmp = fg;
                fg = bg_rgb;
                bg_rgb = tmp;
                has_bg = true;
            }

            if (has_bg)
                DrawRectangle(x, y, cell_width, cell_height,
                              (Color){ bg_rgb.r, bg_rgb.g, bg_rgb.b, 255 });

            if (cell_selected) {
                DrawRectangle(x, y, cell_width, cell_height, UI2RAY(g_ui_theme.selection));
                sel_capture(sel_row, text, pos);
            }
            row_capture(sel_row, sel_col, text, pos);

            x += cell_width;
        }

        y += cell_height;
    }

    // --- Layer 2: images below text (INT32_MIN/2 <= z < 0) ---
    if (has_kitty && placement_iter) {
        kitty_image_renderer_draw_layer(kitty_renderer, terminal, kitty_gfx, placement_iter,
                                        origin_x, origin_y,
                                        cell_width, cell_height, pad,
                                        GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_TEXT);
    }

    // Text/decorations pass.
    if (ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS)
        return;

    y = origin_y + pad;

    while (ghostty_render_state_row_iterator_next(row_iter)) {
        // Get the cells for this row (reuses the same cells handle).
        if (ghostty_render_state_row_get(row_iter,
                GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS)
            continue;

        int x = origin_x + pad;

        while (ghostty_render_state_row_cells_next(cells)) {
            int sel_row = (y - (origin_y + pad)) / cell_height;
            int sel_col = (x - (origin_x + pad)) / cell_width;
            bool cell_selected = sel_contains(sel_row, sel_col);

            // How many codepoints make up the grapheme? 0 = empty cell.
            uint32_t grapheme_len = 0;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);

            if (grapheme_len == 0) {
                x += cell_width;
                continue;
            }

            // Read the grapheme codepoints.
            uint32_t codepoints[16];
            uint32_t len = grapheme_len < 16 ? grapheme_len : 16;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, codepoints);

            // Build a UTF-8 string from the grapheme codepoints.
            char text[64];
            int pos = 0;
            for (uint32_t i = 0; i < len && pos < 60; i++) {
                char u8[4];
                int n = utf8_encode(codepoints[i], u8);
                memcpy(&text[pos], u8, n);
                pos += n;
            }
            text[pos] = '\0';

            // Resolve foreground and background colors using the new
            // per-cell color queries.  These flatten style colors,
            // content-tag colors, and palette lookups into a single RGB
            // value, returning INVALID_VALUE when the cell has no
            // explicit color (in which case we use the terminal default).
            GhosttyColorRgb fg = colors.foreground;
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);

            GhosttyColorRgb bg_rgb = colors.background;
            bool has_bg = ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg_rgb) == GHOSTTY_SUCCESS;

            // Read the style for flags (inverse, bold, italic) — color
            // resolution is handled above via the new API.
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            ghostty_render_state_row_cells_get(cells,
                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

            // Inverse (reverse video): swap foreground and background colors.
            if (style.inverse) {
                GhosttyColorRgb tmp = fg;
                fg = bg_rgb;
                bg_rgb = tmp;
                has_bg = true;
            }

            Color ray_fg = { fg.r, fg.g, fg.b, 255 };

            // Italic: apply a simple shear by shifting the top of the glyph
            // to the right.  The offset is proportional to font size so it
            // looks reasonable at any scale.
            int italic_offset = style.italic ? (font_size / 6) : 0;

            // Bold: use the real bold font face when style.bold is set.
            // JetBrains Mono Bold shares the regular advance width, so the
            // grid does not shift. (§E4)
            Font draw_font = (style.bold && bold_font.texture.id != 0) ? bold_font : font;
            DrawTextEx(draw_font, text, (Vector2){x + italic_offset, y}, font_size, 0, ray_fg);

            // Faint: overlay a semi-transparent bg-colored rectangle to
            // desaturate the glyph toward the background.
            if (style.faint) {
                Color fade_color = (Color){ bg_rgb.r, bg_rgb.g, bg_rgb.b, 140 };
                DrawRectangle(x, y, cell_width, cell_height, fade_color);
            }

            // Invisible: hide the text by drawing the background over it.
            if (style.invisible) {
                Color hide = (Color){ bg_rgb.r, bg_rgb.g, bg_rgb.b, 255 };
                DrawRectangle(x, y, cell_width, cell_height, hide);
            }

            // Underline: pick underline_color if set, else fg.
            Color deco_color = ray_fg;
            if (style.underline != GHOSTTY_SGR_UNDERLINE_NONE) {
                if (style.underline_color.tag == GHOSTTY_STYLE_COLOR_RGB)
                    deco_color = (Color){ style.underline_color.value.rgb.r,
                                          style.underline_color.value.rgb.g,
                                          style.underline_color.value.rgb.b, 255 };
                int uy = y + cell_height - 2;
                if (style.underline == GHOSTTY_SGR_UNDERLINE_DOUBLE)
                    DrawRectangle(x, uy - 2, cell_width, 1, deco_color);
                DrawRectangle(x, uy, cell_width, 1, deco_color);
            }

            // Strikethrough: a horizontal line through the middle of the cell.
            if (style.strikethrough)
                DrawRectangle(x, y + cell_height / 2, cell_width, 1, ray_fg);

            // Overline: a line at the top of the cell.
            if (style.overline)
                DrawRectangle(x, y, cell_width, 1, ray_fg);

            x += cell_width;
        }

        // Clear per-row dirty flag after rendering it.
        bool clean = false;
        ghostty_render_state_row_set(row_iter,
            GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY, &clean);

        y += cell_height;
    }

    // Draw the cursor with proper visual style, blink, and focus-loss
    // hollow rendering (§E4).  Reads CURSOR_VISUAL_STYLE, CURSOR_BLINKING,
    // and CURSOR_PASSWORD_INPUT from the engine (ghostty/vt/render.h).
    {
        bool cursor_visible = false;
        ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
        bool cursor_in_viewport = false;
        ghostty_render_state_get(render_state,
            GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_in_viewport);

        if (cursor_in_viewport) {
            uint16_t cx = 0, cy = 0;
            ghostty_render_state_get(render_state,
                GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
            ghostty_render_state_get(render_state,
                GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);

            GhosttyColorRgb cur_rgb = colors.foreground;
            if (colors.cursor_has_value)
                cur_rgb = colors.cursor;

            int cur_x = origin_x + pad + cx * cell_width;
            int cur_y = origin_y + pad + cy * cell_height;
            bool focused = IsWindowFocused();

            // Read visual style and blink from the engine.
            int  vstyle = (int)GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
            uint32_t vstyle_u32 = (uint32_t)GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
            if (ghostty_render_state_get(render_state,
                    GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &vstyle_u32) == GHOSTTY_SUCCESS)
                vstyle = (int)vstyle_u32;
            bool blinking = false;
            uint32_t blink_u32 = 0;
            if (ghostty_render_state_get(render_state,
                    GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING, &blink_u32) == GHOSTTY_SUCCESS)
                blinking = (blink_u32 != 0);

            // Determine blink phase (~500 ms on/off, wall-clock based so it
            // stays correct even while the idle-frame-rate throttle is
            // holding the render loop at a lower FPS).
            bool blink_on = true;
            if (blinking && cfg->cursor_blink && focused)
                blink_on = ((now_ms / 500) % 2) == 0;

            // Window unfocused → always hollow outline.
            if (!focused) {
                Color hollow = { cur_rgb.r, cur_rgb.g, cur_rgb.b, 200 };
                DrawRectangleLines(cur_x, cur_y, cell_width, cell_height, hollow);
            } else if (cursor_visible && blink_on) {
                Color cur_color = { cur_rgb.r, cur_rgb.g, cur_rgb.b, g_ui_theme.cursor_alpha };

                switch (vstyle) {
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK:
                    default:
                        DrawRectangle(cur_x, cur_y, cell_width, cell_height, cur_color);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
                        DrawRectangle(cur_x, cur_y, 2, cell_height, cur_color);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
                        DrawRectangle(cur_x, cur_y + cell_height - 3, cell_width, 3, cur_color);
                        break;
                    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
                        DrawRectangleLines(cur_x, cur_y, cell_width, cell_height, cur_color);
                        break;
                }

                // Accent outline around the focused block cursor.
                Color outline = UI2RAY(g_ui_theme.accent);
                if (vstyle == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK ||
                    vstyle == GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW) {
                    DrawRectangleLines(cur_x, cur_y, cell_width, cell_height, outline);
                }
            }
        }
    }

    // --- Layer 3: images above text (z >= 0) ---
    if (has_kitty && placement_iter) {
        kitty_image_renderer_draw_layer(kitty_renderer, terminal, kitty_gfx, placement_iter,
                                        origin_x, origin_y,
                                        cell_width, cell_height, pad,
                                        GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT);
    }

    // Draw the scrollbar when there is scrollback content to scroll through.
    // The scrollbar thumb spans the pane height (or the full screen height
    // for a single-pane layout).
    if (scrollbar && scrollbar->total > scrollbar->len) {
        // The caller now passes the inner content height so the thumb
        // geometry matches the actual scrolled area in all pane layouts.
        int scr_h = term_area_h;

        int slot = scrollbar_slot_for_pane(pane_id);

        if (!g_scrollbar_initialized[slot]) {
            g_scrollbar_initialized[slot] = true;
            g_scrollbar_last_scroll_ms[slot] = now_ms - 1500;
        }
        if (scrollbar->offset != g_scrollbar_last_offset[slot]) {
            g_scrollbar_last_offset[slot] = scrollbar->offset;
            g_scrollbar_last_scroll_ms[slot] = now_ms;
        }

        uint64_t last_scroll_ms = g_scrollbar_last_scroll_ms[slot];
        float elapsed = (float)(now_ms - last_scroll_ms) / 1500.0f;
        if (elapsed < 0.0f) elapsed = 0.0f;
        if (elapsed > 1.0f) elapsed = 1.0f;
        float fade = 1.0f - elapsed;

        int bar_width = (int)(5.0f * scale);
        int bar_margin = (int)(4.0f * scale);
        int bar_x = origin_x + term_area_w - bar_width - bar_margin;

        double visible_frac = (double)scrollbar->len / (double)scrollbar->total;
        int thumb_height = (int)(scr_h * visible_frac);
        if (thumb_height < 10) thumb_height = 10;

        double scroll_frac = (scrollbar->total > scrollbar->len)
            ? (double)scrollbar->offset / (double)(scrollbar->total - scrollbar->len)
            : 1.0;
        int thumb_y = origin_y + (int)(scroll_frac * (scr_h - thumb_height));

        float radius = bar_width / 2.0f;
        Color thumb_color = UI2RAY(g_ui_theme.scrollbar);
        int alpha = (int)(thumb_color.a * fade);
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;
        thumb_color.a = (unsigned char)alpha;

        if (thumb_color.a > 0) {
            float roundness = radius / fminf((float)bar_width, (float)thumb_height);
            if (roundness > 0.5f) roundness = 0.5f;
            DrawRectangleRounded((Rectangle){(float)bar_x, (float)thumb_y,
                                             (float)bar_width, (float)thumb_height},
                                 roundness, 8, thumb_color);
        }
    }

    // Reset global dirty state so the next update reports changes accurately.
    GhosttyRenderStateDirty clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set(render_state,
        GHOSTTY_RENDER_STATE_OPTION_DIRTY, &clean_state);
}

static void draw_pane_chrome_and_content(PaneNode *leaf,
                                         int px, int py, int pw, int ph,
                                         int header_h, bool focused, float scale,
                                         Font font, Font bold_font,
                                         int cell_width, int cell_height, int font_size, int pad,
                                         GhosttyTerminal terminal,
                                         GhosttyRenderState rs,
                                         GhosttyRenderStateRowIterator ri,
                                         GhosttyRenderStateRowCells rc,
                                         GhosttyKittyGraphicsPlacementIterator pi,
                                         KittyImageRenderer *kitty_renderer,
                                         GhosttyTerminalScrollbar *lsb_ptr,
                                         AppConfig *cfg, uint64_t now_ms,
                                         int *out_inner_rows,
                                         uint64_t pane_id)
{
    if (pw < 4 || ph < 4) return;

    if (header_h < 0) header_h = 0;
    const float corner = 6.0f * scale;

    Rectangle outer = { (float)px, (float)py, (float)pw, (float)ph };

    int ix = px + 1;
    int iy = py + header_h + 1;
    int iw = pw - 2;
    int ih = ph - header_h - 2;
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;

    int inner_rows = (ih - 2 * pad) / cell_height;
    if (inner_rows < 1) inner_rows = 1;
    if (out_inner_rows) *out_inner_rows = inner_rows;

    // Drop shadow.
    int soff = (int)(2.0f * scale);
    if (soff < 1) soff = 1;
    Color shadow = UI2RAY(g_ui_theme.shadow);
    DrawRectangleRounded((Rectangle){ (float)(px + soff), (float)(py + soff),
                                      (float)pw, (float)ph },
                         corner / fminf(pw, ph), 8, shadow);

    // Frame background and border.
    DrawRectangleRounded(outer, corner / fminf(pw, ph), 8,
                         UI2RAY(g_ui_theme.panel_bg));
    UiColor border_ac = { g_ui_theme.accent.r, g_ui_theme.accent.g,
                          g_ui_theme.accent.b, 220 };
    Color border = focused ? UI2RAY(border_ac) : UI2RAY(g_ui_theme.panel_border);
    DrawRectangleRoundedLinesEx(outer, corner / fminf(pw, ph), 8, 1.0f, border);

    // Header bar.
    if (header_h > 0) {
        Rectangle header = { (float)px + 1.0f, (float)py + 1.0f,
                             (float)pw - 2.0f, (float)header_h };
        DrawRectangleRounded(header, corner / fminf(pw, ph), 8,
                             UI2RAY(g_ui_theme.pane_header_bg));

        Session *ss = leaf->leaf.session;
        const char *pcwd = session_cwd(ss);
        const char *label = pcwd ? pcwd : "";
        const char *slash = strrchr(label, '/');
        if (slash && slash[1]) label = slash + 1;

        char primary[128];
        snprintf(primary, sizeof(primary), "%s", label);

        float text_y = py + 1.0f + (header_h - font_size) / 2.0f;
        float text_x = px + 8.0f * scale;

        // Status dot.
        int status_r = (int)(3.5f * scale);
        if (status_r < 2) status_r = 2;
        Color dot = UI2RAY(g_ui_theme.pane_status_idle);
        int exit_st = session_exit_status(ss);
        if (exit_st >= 0) dot = UI2RAY(g_ui_theme.pane_status_error);
        if (session_child_alive(ss)) dot = UI2RAY(g_ui_theme.pane_status_running);
        DrawCircle((int)(text_x + status_r), (int)(text_y + font_size / 2.0f),
                   (float)status_r, dot);
        text_x += status_r * 2.0f + 6.0f * scale;

        // Primary label.
        Vector2 prim_sz = MeasureTextEx(font, primary, (float)font_size, 0);
        if (prim_sz.x > 0 && text_x + prim_sz.x < px + pw - 8.0f * scale) {
            DrawTextEx(font, primary, (Vector2){ text_x, text_y },
                       (float)font_size, 0,
                       UI2RAY(g_ui_theme.pane_header_text));
            text_x += prim_sz.x + 8.0f * scale;
        }

        // Branch detail.
        char branch[64] = {0};
        cached_git_branch(pcwd ? pcwd : "", branch, sizeof(branch), now_ms);
        if (branch[0]) {
            char detail[128];
            snprintf(detail, sizeof(detail), "(%s)", branch);
            Vector2 det_sz = MeasureTextEx(font, detail, (float)font_size, 0);
            if (text_x + det_sz.x < px + pw - 8.0f * scale) {
                DrawTextEx(font, detail, (Vector2){ text_x, text_y },
                           (float)font_size, 0,
                           UI2RAY(g_ui_theme.pane_header_detail));
            }
        }
    }

    // Scissor to inner content rect and render the terminal inside it.
    BeginScissorMode(ix, iy, iw, ih);
    DrawRectangle(ix, iy, iw, ih, UI2RAY(g_ui_theme.terminal_bg));
    render_terminal(rs, ri, rc, font, bold_font,
                    cell_width, cell_height, font_size, pad,
                    iw, ih, lsb_ptr, terminal, pi,
                    kitty_renderer, ix, iy, cfg, now_ms, pane_id, scale);
    EndScissorMode();
}

// ---------------------------------------------------------------------------
// Build info
// ---------------------------------------------------------------------------

// Log compile-time build info from libghostty-vt so we can quickly tell
// whether the library was built with SIMD, and in which optimization mode.
static void log_build_info(void)
{
    bool simd = false;
    ghostty_build_info(GHOSTTY_BUILD_INFO_SIMD, &simd);

    GhosttyOptimizeMode opt = GHOSTTY_OPTIMIZE_DEBUG;
    ghostty_build_info(GHOSTTY_BUILD_INFO_OPTIMIZE, &opt);

    const char *opt_str;
    switch (opt) {
    case GHOSTTY_OPTIMIZE_DEBUG:        opt_str = "Debug";        break;
    case GHOSTTY_OPTIMIZE_RELEASE_SAFE: opt_str = "ReleaseSafe";  break;
    case GHOSTTY_OPTIMIZE_RELEASE_SMALL: opt_str = "ReleaseSmall"; break;
    case GHOSTTY_OPTIMIZE_RELEASE_FAST: opt_str = "ReleaseFast";  break;
    default:                            opt_str = "Unknown";       break;
    }

    TraceLog(LOG_INFO, "ghostty-vt: simd:     %s", simd ? "enabled" : "disabled");
    TraceLog(LOG_INFO, "ghostty-vt: optimize: %s", opt_str);
}

// ---------------------------------------------------------------------------
// System callbacks (process-global, set once at startup)
// ---------------------------------------------------------------------------

// decode_png — decodes raw PNG data into RGBA pixels using Raylib's
// stb_image-based decoder.  The output buffer is allocated through the
// provided GhosttyAllocator so the library can free it later.
static bool decode_png(void *userdata,
                       const GhosttyAllocator *allocator,
                       const uint8_t *data,
                       size_t data_len,
                       GhosttySysImage *out)
{
    (void)userdata;

    // Raylib's LoadImageFromMemory decodes the PNG via stb_image.
    Image img = LoadImageFromMemory(".png", data, (int)data_len);
    if (img.data == NULL) return false;

    // Convert to uncompressed R8G8B8A8 so we have a known pixel layout.
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    const size_t pixel_len = (size_t)img.width * (size_t)img.height * 4;
    uint8_t *pixels = ghostty_alloc(allocator, pixel_len);
    if (!pixels) {
        UnloadImage(img);
        return false;
    }
    memcpy(pixels, img.data, pixel_len);
    UnloadImage(img);

    out->width    = (uint32_t)img.width;
    out->height   = (uint32_t)img.height;
    out->data     = pixels;
    out->data_len = pixel_len;
    return true;
}

// ---------------------------------------------------------------------------
// Effects callbacks
// ---------------------------------------------------------------------------

// (EffectsContext, effect callbacks, register_session_effects, and
// update_session_effects moved to ui_effects.c — included above.)

// Reload the terminal font at the given *logical* size (applying the current
// content scale via load_terminal_font), then recompute cell metrics, the grid,
// and the pty winsize. Shared by font-size changes (settings/zoom) and
// content-scale changes (window dragged to a differently-scaled monitor).
static bool rebuild_terminal_font(Font *font, Font *bold_font, int font_size,
                                  int *cell_width, int *cell_height,
                                  uint16_t *term_cols, uint16_t *term_rows,
                                  int term_area_w, int pad,
                                  TermEngine *te, int pty_fd)
{
    int new_cell_width = 0;
    int new_cell_height = 0;
    Font new_font = load_terminal_font(font_size, &new_cell_width, &new_cell_height);
    if (new_font.texture.id == 0)
        return false;

    UnloadFont(*font);
    *font = new_font;
    GuiSetFont(*font);              // keep RayGUI on the freshly reloaded font

    // Reload bold variant at the same logical size.
    if (bold_font && bold_font->texture.id != 0)
        UnloadFont(*bold_font);
    if (bold_font) {
        Vector2 dpi_scale = fangs_content_scale();
        int font_size_px = (int)(font_size * dpi_scale.y);
        if (font_size_px < 1) font_size_px = 1;
        int bold_cp_count = 0;
        const int *bold_cps = terminal_font_codepoints(&bold_cp_count);
        *bold_font = LoadFontFromMemory(".ttf", font_jetbrains_mono_bold,
                           (int)sizeof(font_jetbrains_mono_bold), font_size_px,
                           (int *)bold_cps, bold_cp_count);
        if (bold_font->texture.id != 0)
            SetTextureFilter(bold_font->texture, TEXTURE_FILTER_BILINEAR);
    }

    *cell_width = new_cell_width;
    *cell_height = new_cell_height;

    compute_terminal_grid(term_area_w, pad, *cell_width, *cell_height,
                          term_cols, term_rows);
    term_engine_resize(te, *term_cols, *term_rows, *cell_width, *cell_height);
    if (pty_fd >= 0)
        pty_set_winsize(pty_fd, *term_cols, *term_rows, *cell_width, *cell_height);
    return true;
}

// (declared in ui_effects.h)

static bool apply_config(const AppConfig *cfg,
                         Font *font, Font *bold_font,
                         int *font_size,
                         int *cell_width,
                         int *cell_height,
                         uint16_t *term_cols,
                         uint16_t *term_rows,
                         int term_area_w,
                         int pad,
                         Session *s)
{
    TermEngine *te = (TermEngine *)session_engine(s);
    int pty_fd = session_pty_fd(s);
    if (!te || pty_fd < 0) return false;

    if (cfg->font_size != *font_size) {
        if (!rebuild_terminal_font(font, bold_font, cfg->font_size, cell_width, cell_height,
                                   term_cols, term_rows, term_area_w, pad, te, pty_fd))
            return false;
        *font_size = cfg->font_size;
    }

    update_session_effects(s, *term_cols, *term_rows, *cell_width, *cell_height);
    return true;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// (declared in ui_effects.h)

// Sink for pty_read: feed drained PTY bytes through the cmdblocks OSC-133
// observer, which forwards every byte to the VT engine unchanged while tracking
// command boundaries for the block overlay.
static void feed_engine(void *userdata, const uint8_t *data, size_t len)
{
    TermEngine *te = (TermEngine *)userdata;
    if (g_cmdblocks)
        cmdblocks_feed(g_cmdblocks, te, data, len);
    else
        term_engine_write(te, data, len);
}

// Resolve the AI key: FANGS_API_KEY env wins over the config file value.
static const char *resolve_api_key(const AppConfig *cfg)
{
    const char *env = getenv("FANGS_API_KEY");
    if (env && env[0])
        return env;
    return cfg->api_key;
}

static void open_sidebar_for_cmdblock_action(const CmdBlockAction *action)
{
    if (!action || action->action != CB_ACTION_ASK_AI)
        return;

    char ctx_buf[16384];
    ai_block_build_context(action->command,
                           action->output ? action->output : "",
                           action->exit_code,
                           ctx_buf, (int)sizeof(ctx_buf));
    char *redacted = redact_secrets(ctx_buf);
    if (redacted)
        ui_sidebar_set_oneshot_context(redacted);
    else
        ui_sidebar_set_oneshot_context(strdup(ctx_buf));

    ui_sidebar_prefill(ai_block_default_question(action->exit_code));
    ui_sidebar_open_focused();
}

static void cmdblock_action_free(CmdBlockAction *action)
{
    if (!action)
        return;
    if (action->output) {
        free(action->output);
        action->output = NULL;
    }
    action->output_len = 0;
}

// Build and launch a streaming AI request for `prompt`, using redacted recent
// terminal output as context. Returns NULL if there's no key or setup fails.
// block_context (§15): when non-NULL, it is the already-redacted command-block
// context and REPLACES the scrollback dump for this send. NULL → normal chat,
// which captures the last ~120 redacted lines.
static AiStream *start_ai_request(TermEngine *te, const AppConfig *cfg,
                                  const char *prompt, const char *block_context)
{
    char *ctx = block_context ? NULL : context_build(te, 120, 8192);  // redacted
    const char *context_text = block_context ? block_context
                             : (ctx ? ctx : "(none)");
    const char *sys =
        "You are a terminal assistant embedded in the user's terminal. Recent "
        "terminal output is provided as context. Answer concisely. If you "
        "recommend a command, put ONLY that command on a single line inside one "
        "```sh fenced block.";

    size_t ulen = strlen(context_text) + strlen(prompt) + 64;
    char *user = malloc(ulen);
    snprintf(user, ulen, "Recent terminal output:\n%s\n\nQuestion: %s",
             context_text, prompt);

    // system + recent conversation history + the augmented current question.
    enum { MAX_TURNS = 10 };
    AiMessage msgs[MAX_TURNS + 2];
    int n = 0;
    msgs[n++] = (AiMessage){ "system", sys };

    // History = prior sidebar messages, excluding the current question (the
    // last one, just pushed) and any MSG_SYSTEM notices. Capped to MAX_TURNS.
    int count = ui_sidebar_count();
    int hist_end = count > 0 ? count - 1 : 0;
    int begin = hist_end - MAX_TURNS;
    if (begin < 0) begin = 0;
    for (int i = begin; i < hist_end; i++) {
        MsgRole r = ui_sidebar_role(i);
        if (r == MSG_SYSTEM) continue;
        const char *text = ui_sidebar_text(i);
        if (!text[0]) continue;
        msgs[n++] = (AiMessage){ r == MSG_USER ? "user" : "assistant", text };
    }
    msgs[n++] = (AiMessage){ "user", user };

    AiConfig aic = {
        .provider = cfg->provider,
        .endpoint = cfg->endpoint,
        .model = cfg->model,
        .api_key = resolve_api_key(cfg),
        .max_tokens = cfg->max_tokens,
        .stream = true,
        .ollama_num_ctx = cfg->ollama_num_ctx,
        .ollama_num_gpu = cfg->ollama_num_gpu,
        .ollama_num_thread = cfg->ollama_num_thread,
        .ollama_num_batch = cfg->ollama_num_batch,
    };
    AiStream *s = ai_stream_start(&aic, msgs, n);
    free(ctx);
    free(user);
    return s;
}

// Inline (Ctrl+Space) request: a strict "one bare command" prompt with a small
// context budget. Returns NULL if there's no key or setup fails.
static AiStream *start_inline_request(TermEngine *te, const AppConfig *cfg,
                                      const char *prompt)
{
    char *ctx = context_build(te, 40, 4096);
    const char *sys =
        "You translate the user's request into a single shell command for their "
        "shell. Output ONLY the command on one line. No explanation, no markdown, "
        "no backticks. Recent terminal output is provided for context. If unsure, "
        "output the closest single command.";

    size_t ulen = (ctx ? strlen(ctx) : 0) + strlen(prompt) + 64;
    char *user = malloc(ulen);
    snprintf(user, ulen, "Recent terminal output:\n%s\n\nRequest: %s",
             ctx ? ctx : "(none)", prompt);

    AiMessage msgs[2] = {
        { "system", sys },
        { "user", user },
    };
    AiConfig aic = {
        .provider = cfg->provider,
        .endpoint = cfg->endpoint,
        .model = cfg->model,
        .api_key = resolve_api_key(cfg),
        .max_tokens = cfg->max_tokens,
        .stream = true,
        .ollama_num_ctx = cfg->ollama_num_ctx,
        .ollama_num_gpu = cfg->ollama_num_gpu,
        .ollama_num_thread = cfg->ollama_num_thread,
        .ollama_num_batch = cfg->ollama_num_batch,
    };
    AiStream *s = ai_stream_start(&aic, msgs, 2);
    free(ctx);
    free(user);
    return s;
}

// Pixel position just below the terminal cursor, for anchoring the inline prompt.
static void cursor_pixel(GhosttyRenderState rs, int cell_width, int cell_height,
                         int origin_x, int origin_y, int pad, int *px, int *py)
{
    uint16_t cx = 0, cy = 0;
    ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
    ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
    *px = origin_x + pad + (int)cx * cell_width;
    *py = origin_y + pad + ((int)cy + 1) * cell_height;
}

// Mix two colors (t in 0..1), for deriving widget shades from a theme.
static Color color_mix(Color a, Color b, float t)
{
    return (Color){
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255,
    };
}

// Style RayGUI (settings modal + sidebar/inline widgets) to match the active
// terminal theme, so the in-app UI isn't a light panel over a dark terminal.
// Derives shades from the theme's bg/fg/blue, so it works for dark and light.
static void apply_gui_style(const Theme *t)
{
    Color bg     = (Color){t->bg.r, t->bg.g, t->bg.b, 255};
    Color fg     = (Color){t->fg.r, t->fg.g, t->fg.b, 255};
    Color accent = (Color){t->ansi[4].r, t->ansi[4].g, t->ansi[4].b, 255};  // blue
    Color border = color_mix(bg, fg, 0.30f);
    Color base   = color_mix(bg, fg, 0.08f);
    Color base_f = color_mix(bg, accent, 0.14f);   // hover: faint tint
    Color base_p = color_mix(bg, accent, 0.18f);   // focused/editing: mostly dark — focus is shown by the accent border, not a heavy fill
    Color dim    = color_mix(bg, fg, 0.45f);

    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(bg));
    GuiSetStyle(DEFAULT, LINE_COLOR,       ColorToInt(border));

    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, ColorToInt(border));
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL,   ColorToInt(base));
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL,   ColorToInt(fg));

    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, ColorToInt(accent));
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED,   ColorToInt(base_f));
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED,   ColorToInt(fg));

    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, ColorToInt(accent));
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED,   ColorToInt(base_p));
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED,   ColorToInt(fg));   // readable while typing (was accent-on-accent)

    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, ColorToInt(border));
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED,   ColorToInt(base));
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED,   ColorToInt(dim));
}

// Apply config to all sessions in the active tab. Used by end-of-frame font
// zoom and settings-save so every pane picks up theme/font changes.
// Returns true if all sessions applied successfully.
static bool apply_config_all_sessions(const AppConfig *cfg,
                                       Font *font, Font *bold_font,
                                       int *font_size,
                                       int *cell_width, int *cell_height,
                                       uint16_t *term_cols, uint16_t *term_rows,
                                       int term_area_w, int pad)
{
    if (app.n_tabs <= 0 || app.active < 0) return false;
    Tab *tab = &app.tabs[app.active];
    if (!tab || !tab->root) return false;
    PaneNode *leaves[64];
    int n = 0;
    pane_collect_leaves(tab->root, leaves, 64, &n);
    bool ok = true;
    for (int i = 0; i < n; i++) {
        Session *s = leaves[i]->leaf.session;
        if (!apply_config(cfg, font, bold_font, font_size, cell_width, cell_height,
                          term_cols, term_rows, term_area_w, pad, s))
            ok = false;
    }
    return ok;
}

static void drain_char_queue(void)
{
    while (GetCharPressed() != 0) { }
}

static UiPaletteSelection palette_selection_none(void)
{
    return (UiPaletteSelection){
        .type = UI_PALETTE_SELECTION_NONE,
        .action_id = FANGS_ACTION_NONE,
        .workflow_index = -1,
        .tab_index = -1,
    };
}

// Feed the currently open tabs into the command palette so they're
// fuzzy-matchable alongside actions and workflows. Same label precedence as
// the rail (name > title > cwd) — see workspace_rail_build's comment.
static void refresh_palette_workspaces(void)
{
    WorkspacePaletteEntry entries[FANGS_MAX_TABS];
    int n = 0;
    const char *home = getenv("HOME");

    for (int i = 0; i < app.n_tabs && i < FANGS_MAX_TABS; i++) {
        Tab *tt = &app.tabs[i];
        entries[n].tab_index = i;
        entries[n].label[0] = '\0';

        if (tt->name[0]) {
            snprintf(entries[n].label, sizeof(entries[n].label), "%s", tt->name);
        } else {
            Session *rep = (tt->focused && tt->focused->kind == PANE_LEAF)
                ? tt->focused->leaf.session : NULL;
            if (!rep)
                rep = tab_first_leaf_session(tt);
            const char *title = rep
                ? cmdblocks_title((CmdBlocks *)session_cmdblocks(rep)) : "";
            if (title && title[0]) {
                snprintf(entries[n].label, sizeof(entries[n].label), "%s", title);
            } else {
                const char *cwd = rep ? session_cwd(rep) : "";
                workspace_cwd_label(cwd, home, entries[n].label,
                                    (int)sizeof(entries[n].label));
            }
        }
        n++;
    }

    ui_palette_set_workspaces(entries, n);
}

// Types cfg->workspace_command + Enter into a freshly created worktree
// workspace's shell, if configured. Only called from interactive worktree
// creation (palette action, Option/Alt-click +, row context menu) — not
// `fangs ctl new --worktree`, which already has its own explicit --run.
static void maybe_auto_launch(Session *ns, const AppConfig *cfg)
{
    if (!ns || !cfg->workspace_command[0])
        return;
    int fd = session_pty_fd(ns);
    if (fd < 0)
        return;
    pty_write(fd, cfg->workspace_command, strlen(cfg->workspace_command));
    pty_write(fd, "\n", 1);
}

// Save the current tab list (cwd + name) so it can be restored on next
// launch. Called once per frame only when g_session_dirty, plus once more
// as a shutdown safety net. Failures are silent (best-effort persistence,
// same as the rest of the rail's non-critical state).
static void persist_session_if_dirty(const AppConfig *cfg)
{
    if (!g_session_dirty)
        return;
    g_session_dirty = false;
    if (!cfg->restore_session)
        return;

    WorkspaceSessionState state;
    memset(&state, 0, sizeof(state));
    state.count = app.n_tabs < WORKSPACE_SESSION_MAX_TABS
        ? app.n_tabs : WORKSPACE_SESSION_MAX_TABS;
    state.active = (app.active >= 0 && app.active < state.count) ? app.active : 0;

    for (int i = 0; i < state.count; i++) {
        Tab *tt = &app.tabs[i];
        Session *rep = tt->focused ? tt->focused->leaf.session : tab_first_leaf_session(tt);
        const char *cwd = rep ? session_cwd(rep) : "";
        snprintf(state.tabs[i].cwd, sizeof(state.tabs[i].cwd), "%s", cwd ? cwd : "");
        snprintf(state.tabs[i].name, sizeof(state.tabs[i].name), "%s", tt->name);
        state.tabs[i].color_tag = tt->color_tag;
    }

    char session_path[4096];
    if (workspace_session_default_path(session_path, sizeof(session_path)))
        workspace_session_save(session_path, &state);
}

static void workflow_path_from_config_path(const char *config_path,
                                           char *out, int out_size)
{
    if (!out || out_size <= 0)
        return;
    out[0] = '\0';
    if (!config_path || config_path[0] == '\0') {
        snprintf(out, (size_t)out_size, "workflows");
        return;
    }

    const char *slash = strrchr(config_path, '/');
    if (!slash) {
        snprintf(out, (size_t)out_size, "workflows");
        return;
    }

    int dir_len = (int)(slash - config_path);
    if (dir_len < 1)
        dir_len = 1;
    if (dir_len >= out_size)
        dir_len = out_size - 1;
    memcpy(out, config_path, (size_t)dir_len);
    out[dir_len] = '\0';
    snprintf(out + strlen(out), (size_t)(out_size - (int)strlen(out)),
             "%sworkflows", out[dir_len - 1] == '/' ? "" : "/");
}

static bool find_project_workflow_path(const char *cwd, char *out, int out_size)
{
    if (!cwd || !cwd[0] || !out || out_size <= 0)
        return false;

    char cur[4096];
    snprintf(cur, sizeof(cur), "%s", cwd);

    for (;;) {
        char candidate[4096];
        snprintf(candidate, sizeof(candidate), "%s/.fangs/workflows", cur);
        if (access(candidate, R_OK) == 0) {
            snprintf(out, (size_t)out_size, "%s", candidate);
            return true;
        }
        snprintf(candidate, sizeof(candidate), "%s/.fangs/workflows.ini", cur);
        if (access(candidate, R_OK) == 0) {
            snprintf(out, (size_t)out_size, "%s", candidate);
            return true;
        }

        char *slash = strrchr(cur, '/');
        if (!slash || slash == cur)
            break;
        *slash = '\0';
    }

    return false;
}

static bool find_project_root_for_save(const char *cwd, char *out, int out_size)
{
    if (!cwd || !cwd[0] || !out || out_size <= 0)
        return false;

    char cur[4096];
    snprintf(cur, sizeof(cur), "%s", cwd);

    for (;;) {
        char marker[4096];
        snprintf(marker, sizeof(marker), "%s/.fangs", cur);
        if (access(marker, F_OK) == 0) {
            snprintf(out, (size_t)out_size, "%s", cur);
            return true;
        }
        snprintf(marker, sizeof(marker), "%s/.git", cur);
        if (access(marker, F_OK) == 0) {
            snprintf(out, (size_t)out_size, "%s", cur);
            return true;
        }

        char *slash = strrchr(cur, '/');
        if (!slash || slash == cur)
            break;
        *slash = '\0';
    }

    snprintf(out, (size_t)out_size, "%s", cwd);
    return true;
}

static bool project_workflow_save_path(const char *cwd, char *out, int out_size)
{
    char root[4096];
    if (!find_project_root_for_save(cwd, root, (int)sizeof(root)))
        return false;
    snprintf(out, (size_t)out_size, "%s/.fangs/workflows", root);
    return true;
}

static void refresh_palette_workflows(WorkflowRegistry *workflows,
                                      const char *config_path,
                                      const char *cwd)
{
    if (!workflows)
        return;

    workflows_init(workflows);

    char global_path[4096];
    workflow_path_from_config_path(config_path, global_path, (int)sizeof(global_path));
    if (!workflows_load_file(workflows, global_path)) {
        fprintf(stderr, "warning: failed to load workflows at %s\n", global_path);
        toast_push(TOAST_WARN, "Failed to load global workflows.");
    }

    char project_path[4096];
    if (find_project_workflow_path(cwd, project_path, (int)sizeof(project_path))) {
        if (!workflows_load_file(workflows, project_path)) {
            fprintf(stderr, "warning: failed to load workflows at %s\n", project_path);
            toast_push(TOAST_WARN, "Failed to load project workflows.");
        }
    }

    ui_palette_set_workflows(workflows);
}

static void stage_workflow_command(const WorkflowRegistry *workflows,
                                   UiPaletteSelection selection,
                                   int pty_fd)
{
    const Workflow *workflow = workflows_get(workflows, selection.workflow_index);
    if (!workflow || workflow->command[0] == '\0') {
        toast_push(TOAST_WARN, "Workflow command is unavailable.");
        return;
    }
    pty_write(pty_fd, workflow->command, strlen(workflow->command));
    toast_push(TOAST_INFO, "Workflow command staged.");
}

static void stage_command_text(const char *command, int pty_fd)
{
    if (!command || !command[0])
        return;
    pty_write(pty_fd, command, strlen(command));
    toast_push(TOAST_INFO, "Workflow command staged.");
}

static void handle_workflow_selection(const WorkflowRegistry *workflows,
                                      UiPaletteSelection selection,
                                      int pty_fd)
{
    const Workflow *workflow = workflows_get(workflows, selection.workflow_index);
    if (!workflow) {
        toast_push(TOAST_WARN, "Workflow command is unavailable.");
        return;
    }

    WorkflowVar vars[WORKFLOW_VAR_MAX];
    int var_count = workflows_collect_vars(workflow->command, vars, WORKFLOW_VAR_MAX);
    if (var_count > 0) {
        if (!ui_workflow_prompt_open(workflow))
            toast_push(TOAST_WARN, "Could not open workflow prompt.");
        return;
    }

    stage_workflow_command(workflows, selection, pty_fd);
}

static void save_latest_command_block_as_workflow(TermEngine *te,
                                                  uint16_t term_rows,
                                                  const char *cwd,
                                                  const char *config_path,
                                                  WorkflowRegistry *workflows)
{
    CmdBlockAction latest_action = {0};
    if (!cmdblocks_latest_action(g_cmdblocks, te, (int)term_rows, &latest_action)) {
        toast_push(TOAST_INFO, "No command block to save.");
        return;
    }

    if (latest_action.command[0] == '\0') {
        cmdblock_action_free(&latest_action);
        toast_push(TOAST_WARN, "Latest command block has no command.");
        return;
    }

    char path[4096];
    if (!project_workflow_save_path(cwd, path, (int)sizeof(path))) {
        cmdblock_action_free(&latest_action);
        toast_push(TOAST_WARN, "Could not resolve project workflow path.");
        return;
    }

    char id[WORKFLOW_ID_MAX];
    if (!workflows_append_saved_command(path, latest_action.command,
                                        id, (int)sizeof(id))) {
        cmdblock_action_free(&latest_action);
        toast_push(TOAST_WARN, "Failed to save workflow.");
        return;
    }

    refresh_palette_workflows(workflows, config_path, cwd);
    char msg[160];
    snprintf(msg, sizeof(msg), "Saved runbook: %s", id);
    toast_push(TOAST_INFO, msg);
    cmdblock_action_free(&latest_action);
}

static void open_inline_at_cursor(GhosttyRenderState render_state,
                                  int cell_width, int cell_height,
                                  int term_origin_x, int term_origin_y,
                                  int pad)
{
    int icx = 0, icy = 0;
    cursor_pixel(render_state, cell_width, cell_height,
                 term_origin_x, term_origin_y, pad, &icx, &icy);
    ui_inline_open(icx, icy);
}

static void ask_ai_about_latest_block(TermEngine *te, uint16_t term_rows)
{
    CmdBlockAction latest_action = {0};
    if (cmdblocks_latest_action(g_cmdblocks, te, (int)term_rows, &latest_action)) {
        open_sidebar_for_cmdblock_action(&latest_action);
        cmdblock_action_free(&latest_action);
    } else {
        toast_push(TOAST_INFO, "No command block to ask about.");
    }
}

static bool save_font_size_action(AppConfig *cfg, const char *config_path,
                                  int new_size, bool *apply_saved_config)
{
    if (new_size < FANGS_MIN_FONT_SIZE) new_size = FANGS_MIN_FONT_SIZE;
    if (new_size > FANGS_MAX_FONT_SIZE) new_size = FANGS_MAX_FONT_SIZE;
    if (new_size == cfg->font_size)
        return false;

    cfg->font_size = new_size;
    if (!config_save(cfg, config_path)) {
        fprintf(stderr, "warning: failed to save config at %s\n", config_path);
        toast_push(TOAST_WARN, "Failed to save config (font zoom).");
    }
    if (apply_saved_config)
        *apply_saved_config = true;
    return true;
}

static Session *sync_runtime_for_action(TermEngine **te, int *pty_fd,
                                        pid_t *child, bool *child_exited,
                                        GhosttyTerminal *terminal,
                                        GhosttyRenderState *render_state,
                                        GhosttyRenderStateRowIterator *row_iter,
                                        GhosttyRenderStateRowCells *row_cells,
                                        GhosttyKittyGraphicsPlacementIterator *placement_iter,
                                        GhosttyKeyEncoder *key_encoder,
                                        GhosttyKeyEvent *key_event,
                                        GhosttyMouseEncoder *mouse_encoder,
                                        GhosttyMouseEvent *mouse_event)
{
    return sync_active_runtime(te, pty_fd, child, child_exited,
                               terminal, render_state, row_iter, row_cells,
                               placement_iter, key_encoder, key_event,
                               mouse_encoder, mouse_event);
}

static void execute_host_action(FangsActionId action,
                                TermEngine **te, int *pty_fd,
                                pid_t *child, bool *child_exited,
                                GhosttyTerminal *terminal,
                                GhosttyRenderState *render_state,
                                GhosttyRenderStateRowIterator *row_iter,
                                GhosttyRenderStateRowCells *row_cells,
                                GhosttyKittyGraphicsPlacementIterator *placement_iter,
                                GhosttyKeyEncoder *key_encoder,
                                GhosttyKeyEvent *key_event,
                                GhosttyMouseEncoder *mouse_encoder,
                                GhosttyMouseEvent *mouse_event,
                                AppConfig *cfg, const char *config_path,
                                WorkflowRegistry *palette_workflows,
                                int cell_width, int cell_height, int pad,
                                uint16_t term_cols, uint16_t term_rows,
                                int term_origin_x, int term_origin_y,
                                int term_area_w,
                                uint64_t now_ms,
                                bool *apply_saved_config,
                                int *prev_term_area_w)
{
    switch (action) {
    case FANGS_ACTION_OPEN_COMMAND_PALETTE:
        refresh_palette_workspaces();
        ui_palette_open();
        break;
    case FANGS_ACTION_OPEN_SETTINGS:
        if (!ui_settings_open())
            ui_settings_toggle();
        break;
    case FANGS_ACTION_TOGGLE_SIDEBAR:
        ui_sidebar_toggle();
        ui_sidebar_focus(ui_sidebar_visible());
        break;
    case FANGS_ACTION_TOGGLE_WORKSPACE_RAIL:
        cfg->workspace_rail = !cfg->workspace_rail;
        g_rail_collapsed = false;
        config_save(cfg, config_path);
        if (prev_term_area_w)
            *prev_term_area_w = -1;
        break;
    case FANGS_ACTION_RENAME_WORKSPACE:
        if (app.n_tabs > 0 && app.active >= 0)
            ui_rename_prompt_open(app.active, app.tabs[app.active].name);
        break;
    case FANGS_ACTION_NEW_WORKTREE_WORKSPACE: {
        if (app.n_tabs >= FANGS_MAX_TABS) {
            toast_push(TOAST_WARN, "Maximum workspaces reached");
            break;
        }
        Session *cur = sync_runtime_for_action(te, pty_fd, child, child_exited,
                                               terminal, render_state, row_iter,
                                               row_cells, placement_iter,
                                               key_encoder, key_event,
                                               mouse_encoder, mouse_event);
        if (!cur) break;
        WorkspaceWorktreeResult wtr;
        memset(&wtr, 0, sizeof(wtr));
        if (workspace_worktree_create(session_cwd(cur), &wtr)) {
            Session *ns = app_add_tab_named(term_cols, term_rows,
                                            cell_width, cell_height,
                                            cfg->scrollback, wtr.path,
                                            wtr.branch,
                                            cfg->kitty_images,
                                            cfg->kitty_image_storage_mb,
                                            te, pty_fd, child, child_exited);
            if (!ns) {
                // Best-effort clean up the worktree we just created.
                workspace_worktree_remove_created(&wtr);
                toast_push(TOAST_WARN, "Failed to open workspace for worktree");
            } else {
                toast_push(TOAST_INFO, "Created worktree");
                maybe_auto_launch(ns, cfg);
            }
        } else {
            toast_push(TOAST_WARN, wtr.error);
        }
        sync_runtime_for_action(te, pty_fd, child, child_exited,
                                terminal, render_state, row_iter, row_cells,
                                placement_iter, key_encoder, key_event,
                                mouse_encoder, mouse_event);
        if (prev_term_area_w)
            *prev_term_area_w = -1;
        break;
    }
    case FANGS_ACTION_ATTENTION_INBOX: {
        collect_rail_inputs(now_ms);

        // Snapshot recent events once, to look up "how long has this pane
        // been waiting" for each candidate below (newest-first; first
        // match per pane_id is its most recent event).
        WorkspaceStatusEvent event_cache[HISTORY_EVENT_CACHE];
        int event_count = workspace_status_events(&g_workspace_status,
                                                   event_cache, HISTORY_EVENT_CACHE);

        typedef struct {
            int tab_index;
            uint64_t pane_id;
            WorkspaceAttention level;
            uint64_t at_ms;   // 0 if no matching event found (sorts last)
        } InboxCandidate;

        InboxCandidate cand[WORKSPACE_RAIL_MAX_TABS];
        int cn = 0;
        for (int i = 0; i < app.n_tabs && cn < WORKSPACE_RAIL_MAX_TABS; i++) {
            uint64_t pid = tab_attention_id(&app.tabs[i]);
            WorkspaceAttention lvl = workspace_status_level(&g_workspace_status, pid);
            if (lvl == WORKSPACE_ATTENTION_NONE)
                continue;
            uint64_t at_ms = 0;
            for (int ei = 0; ei < event_count; ei++) {
                if (event_cache[ei].pane_id == pid) {
                    at_ms = event_cache[ei].at_ms;
                    break;
                }
            }
            cand[cn].tab_index = i;
            cand[cn].pane_id = pid;
            cand[cn].level = lvl;
            cand[cn].at_ms = at_ms;
            cn++;
        }

        // Sort worst-first: severity descending, then oldest-waiting first
        // (smaller at_ms first; unknown age sorts last within its tier).
        for (int a = 0; a < cn - 1; a++) {
            for (int b = a + 1; b < cn; b++) {
                bool swap = false;
                if (cand[b].level > cand[a].level) {
                    swap = true;
                } else if (cand[b].level == cand[a].level) {
                    uint64_t ta = cand[a].at_ms ? cand[a].at_ms : UINT64_MAX;
                    uint64_t tb = cand[b].at_ms ? cand[b].at_ms : UINT64_MAX;
                    if (tb < ta) swap = true;
                }
                if (swap) {
                    InboxCandidate tmp = cand[a];
                    cand[a] = cand[b];
                    cand[b] = tmp;
                }
            }
        }

        if (cn == 0) {
            toast_push(TOAST_INFO, "Nothing needs attention");
            break;
        }

        UiMenuItem iitems[WORKSPACE_RAIL_MAX_TABS];
        memset(iitems, 0, sizeof(iitems));
        g_inbox_pane_count = 0;
        for (int i = 0; i < cn; i++) {
            Tab *tt = &app.tabs[cand[i].tab_index];
            const char *lbl = tt->name[0] ? tt->name : g_rail_inputs.tab_labels[cand[i].tab_index];
            const char *text = workspace_status_text(&g_workspace_status, cand[i].pane_id);
            snprintf(iitems[i].label, sizeof(iitems[i].label), "%s: %s",
                     lbl, (text && text[0]) ? text : "needs attention");
            iitems[i].tag = RAIL_MENU_INBOX_BASE + i;
            iitems[i].tint = (cand[i].level == WORKSPACE_ATTENTION_ERROR)
                ? UI_COLOR_INLINE_ERROR
                : (cand[i].level == WORKSPACE_ATTENTION_WARN)
                    ? UI_COLOR_TEXT
                    : UI_COLOR_SUBTITLE;
            iitems[i].separator = false;
            g_inbox_pane_cache[i] = cand[i].pane_id;
            g_inbox_pane_count++;
        }

        int ix = GetScreenWidth() / 2;
        int iy = GetScreenHeight() / 2;
        ui_menu_open(&g_rail_menu, iitems, cn, ix, iy);
        ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
        break;
    }
    case FANGS_ACTION_CLEANUP_WORKTREES: {
        Session *cur = sync_runtime_for_action(te, pty_fd, child, child_exited,
                                               terminal, render_state, row_iter,
                                               row_cells, placement_iter,
                                               key_encoder, key_event,
                                               mouse_encoder, mouse_event);
        if (!cur) break;

        char repo_root[WORKTREE_PATH_MAX];
        if (!workspace_worktree_repo_root(session_cwd(cur), repo_root, sizeof(repo_root))) {
            toast_push(TOAST_WARN, "Not inside a git repository");
            break;
        }

        // Exclude every currently-open pane cwd, on top of the clean-worktree
        // check inside find_cleanup_candidates. A pane in a worktree
        // subdirectory still blocks removing that worktree.
        const char *exclude[OPEN_WORKTREE_EXCLUDE_MAX];
        int exclude_count = collect_open_session_cwds(exclude, OPEN_WORKTREE_EXCLUDE_MAX);

        g_cleanup_candidate_count = workspace_worktree_find_cleanup_candidates(
            repo_root, exclude, exclude_count, g_cleanup_candidates, WORKTREE_CLEANUP_MAX);
        snprintf(g_cleanup_repo_root, sizeof(g_cleanup_repo_root), "%s", repo_root);
        // Confirming acts on exactly what's listed below (leave room for
        // the separator + "Confirm" items) -- never more than was shown.
        if (g_cleanup_candidate_count > UI_MENU_MAX_ITEMS - 2)
            g_cleanup_candidate_count = UI_MENU_MAX_ITEMS - 2;

        if (g_cleanup_candidate_count == 0) {
            toast_push(TOAST_INFO, "No worktrees to clean up");
            break;
        }

        UiMenuItem citems[UI_MENU_MAX_ITEMS];
        memset(citems, 0, sizeof(citems));
        int cc = 0;
        // Leave room for the separator + "Confirm" items.
        for (int i = 0; i < g_cleanup_candidate_count
             && cc < UI_MENU_MAX_ITEMS - 2; i++) {
            snprintf(citems[cc].label, sizeof(citems[cc].label),
                     "%s (merged, clean)", g_cleanup_candidates[i].branch);
            citems[cc].tag = -1;  // informational only in v1, not clickable
            citems[cc].tint = UI_COLOR_SUBTITLE;
            citems[cc].separator = false;
            cc++;
        }
        citems[cc].separator = true;
        citems[cc].tag = -1;
        cc++;
        snprintf(citems[cc].label, sizeof(citems[cc].label),
                 "Confirm: remove %d worktree%s", g_cleanup_candidate_count,
                 g_cleanup_candidate_count == 1 ? "" : "s");
        citems[cc].tag = RAIL_MENU_CLEANUP_CONFIRM;
        citems[cc].tint = UI_COLOR_INLINE_ERROR;
        citems[cc].separator = false;
        cc++;

        int mx = GetScreenWidth() / 2;
        int my = GetScreenHeight() / 2;
        ui_menu_open(&g_rail_menu, citems, cc, mx, my);
        ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
        break;
    }
    case FANGS_ACTION_BROADCAST_COMMAND:
        ui_broadcast_prompt_open();
        break;
    case FANGS_ACTION_INLINE_COMMAND:
        if (!ui_inline_active())
            open_inline_at_cursor(*render_state, cell_width, cell_height,
                                  term_origin_x, term_origin_y, pad);
        break;
    case FANGS_ACTION_ASK_LATEST_BLOCK:
        ask_ai_about_latest_block(*te, term_rows);
        break;
    case FANGS_ACTION_SAVE_LATEST_BLOCK_WORKFLOW: {
        Session *cur = sync_runtime_for_action(te, pty_fd, child, child_exited,
                                               terminal, render_state, row_iter,
                                               row_cells, placement_iter,
                                               key_encoder, key_event,
                                               mouse_encoder, mouse_event);
        save_latest_command_block_as_workflow(*te, term_rows, session_cwd(cur),
                                              config_path, palette_workflows);
        break;
    }
    case FANGS_ACTION_FIND:
        g_search_active = true;
        break;
    case FANGS_ACTION_COPY_SELECTION:
        sel_copy_to_clipboard();
        break;
    case FANGS_ACTION_PASTE:
        do_paste(*pty_fd, *terminal);
        break;
    case FANGS_ACTION_NEW_TAB: {
        Session *cur = sync_runtime_for_action(te, pty_fd, child, child_exited,
                                               terminal, render_state, row_iter,
                                               row_cells, placement_iter,
                                               key_encoder, key_event,
                                               mouse_encoder, mouse_event);
        app_add_tab(term_cols, term_rows, cell_width, cell_height,
                    cfg->scrollback, session_cwd(cur),
                    cfg->kitty_images, cfg->kitty_image_storage_mb,
                    te, pty_fd, child, child_exited);
        sync_runtime_for_action(te, pty_fd, child, child_exited,
                                terminal, render_state, row_iter, row_cells,
                                placement_iter, key_encoder, key_event,
                                mouse_encoder, mouse_event);
        if (prev_term_area_w)
            *prev_term_area_w = -1;
        break;
    }
    case FANGS_ACTION_CLOSE_PANE:
        app_close_active();
        sync_runtime_for_action(te, pty_fd, child, child_exited,
                                terminal, render_state, row_iter, row_cells,
                                placement_iter, key_encoder, key_event,
                                mouse_encoder, mouse_event);
        if (prev_term_area_w)
            *prev_term_area_w = -1;
        break;
    case FANGS_ACTION_SPLIT_RIGHT:
    case FANGS_ACTION_SPLIT_DOWN: {
        Session *cur = sync_runtime_for_action(te, pty_fd, child, child_exited,
                                               terminal, render_state, row_iter,
                                               row_cells, placement_iter,
                                               key_encoder, key_event,
                                               mouse_encoder, mouse_event);
        app_split_focused(action == FANGS_ACTION_SPLIT_DOWN ? PANE_VSPLIT : PANE_HSPLIT,
                          term_cols, term_rows, cell_width, cell_height,
                          cfg->scrollback, session_cwd(cur),
                          cfg->kitty_images, cfg->kitty_image_storage_mb,
                          te, pty_fd, child, child_exited);
        sync_runtime_for_action(te, pty_fd, child, child_exited,
                                terminal, render_state, row_iter, row_cells,
                                placement_iter, key_encoder, key_event,
                                mouse_encoder, mouse_event);
        if (prev_term_area_w)
            *prev_term_area_w = -1;
        break;
    }
    case FANGS_ACTION_FOCUS_LEFT:
    case FANGS_ACTION_FOCUS_RIGHT:
    case FANGS_ACTION_FOCUS_UP:
    case FANGS_ACTION_FOCUS_DOWN:
        if (app.n_tabs > 0) {
            int dx = 0, dy = 0;
            if (action == FANGS_ACTION_FOCUS_LEFT) dx = -1;
            if (action == FANGS_ACTION_FOCUS_RIGHT) dx = 1;
            if (action == FANGS_ACTION_FOCUS_UP) dy = -1;
            if (action == FANGS_ACTION_FOCUS_DOWN) dy = 1;
            Tab *t = &app.tabs[app.active];
            PaneNode *nf = pane_focus_move(t->root, t->focused, dx, dy);
            if (nf) {
                t->focused = nf;
                sync_runtime_for_action(te, pty_fd, child, child_exited,
                                        terminal, render_state, row_iter, row_cells,
                                        placement_iter, key_encoder, key_event,
                                        mouse_encoder, mouse_event);
            }
        }
        break;
    case FANGS_ACTION_FONT_INCREASE:
        save_font_size_action(cfg, config_path, cfg->font_size + 1, apply_saved_config);
        break;
    case FANGS_ACTION_FONT_DECREASE:
        save_font_size_action(cfg, config_path, cfg->font_size - 1, apply_saved_config);
        break;
    case FANGS_ACTION_FONT_RESET:
        save_font_size_action(cfg, config_path, FANGS_DEFAULT_FONT_SIZE, apply_saved_config);
        break;
    case FANGS_ACTION_NONE:
    default:
        break;
    }

    (void)term_area_w;
}

// ---------------------------------------------------------------------------
// Remote control — execute one parsed request and return a response JSON
// string (caller must free).  Called from the frame loop after
// sync_active_runtime so te / pty_fd reflect the active session.
// ---------------------------------------------------------------------------
static char *remote_execute(const RemoteRequest *req,
                            TermEngine *te, int pty_fd,
                            const AppConfig *cfg, const char *config_path,
                            uint16_t term_cols, uint16_t term_rows,
                            int cell_width, int cell_height)
{
    switch (req->cmd) {
    case REMOTE_CMD_LIST: {
        cJSON *tabs = cJSON_CreateArray();
        for (int ti = 0; ti < app.n_tabs; ti++) {
            cJSON *t = cJSON_CreateObject();
            cJSON_AddNumberToObject(t, "index", ti);
            cJSON_AddBoolToObject(t, "active", ti == app.active);
            cJSON_AddStringToObject(t, "name", app.tabs[ti].name);

            // Collect leaf sessions
            PaneNode *ll[WORKSPACE_RAIL_MAX_PANES];
            int ln = 0;
            pane_collect_leaves(app.tabs[ti].root, ll,
                                WORKSPACE_RAIL_MAX_PANES, &ln);
            cJSON *panes_arr = cJSON_CreateArray();
            int port_buf[64];
            for (int pi = 0; pi < ln; pi++) {
                if (ll[pi]->kind != PANE_LEAF) continue;
                Session *ss = ll[pi]->leaf.session;
                cJSON *p = cJSON_CreateObject();
                cJSON_AddNumberToObject(p, "id",
                    (double)(uint64_t)(uintptr_t)ss);
                cJSON_AddStringToObject(p, "cwd",
                    session_cwd(ss) ? session_cwd(ss) : "");
                int np = session_ports(ss, port_buf, 64);
                if (np > 0) {
                    cJSON *prts = cJSON_CreateArray();
                    for (int pi2 = 0; pi2 < np; pi2++)
                        cJSON_AddItemToArray(prts, cJSON_CreateNumber(port_buf[pi2]));
                    cJSON_AddItemToObject(p, "ports", prts);
                }
                cJSON_AddItemToArray(panes_arr, p);
            }
            cJSON_AddItemToObject(t, "panes", panes_arr);
            cJSON_AddItemToArray(tabs, t);
        }
        return remote_proto_ok_obj(req->id, tabs);
    }

    case REMOTE_CMD_NEW: {
        if (app.n_tabs >= FANGS_MAX_TABS)
            return remote_proto_error(req->id, "max workspaces reached");
        const char *cwd = req->cwd[0] ? req->cwd : NULL;
        Session *ns = NULL;
        if (req->worktree) {
            WorkspaceWorktreeResult wtr;
            memset(&wtr, 0, sizeof(wtr));
            if (!workspace_worktree_create(cwd, &wtr))
                return remote_proto_error(req->id, wtr.error);
            ns = app_add_tab_named(term_cols, term_rows,
                                   cell_width, cell_height,
                                   cfg->scrollback, wtr.path, wtr.branch,
                                   cfg->kitty_images,
                                   cfg->kitty_image_storage_mb,
                                   NULL, NULL, NULL, NULL);
            if (!ns) {
                workspace_worktree_remove_created(&wtr);
                return remote_proto_error(req->id,
                    "failed to open workspace for worktree");
            }
        } else {
            ns = app_add_tab(term_cols, term_rows,
                             cell_width, cell_height,
                             cfg->scrollback, cwd,
                             cfg->kitty_images,
                             cfg->kitty_image_storage_mb,
                             NULL, NULL, NULL, NULL);
        }
        if (!ns)
            return remote_proto_error(req->id, "failed to create session");

        // If run text is provided, inject it after the shell starts.
        if (req->run[0] && ns) {
            int pfd = session_pty_fd(ns);
            if (pfd >= 0) {
                pty_write(pfd, req->run, strlen(req->run));
                pty_write(pfd, "\n", 1);
            }
        }
        // Apply name if provided.
        if (req->name[0] && app.n_tabs > 0) {
            Tab *last = &app.tabs[app.n_tabs - 1];
            snprintf(last->name, sizeof(last->name), "%s", req->name);
        }
        (void)config_path;
        return remote_proto_ok(req->id);
    }

    case REMOTE_CMD_FOCUS: {
        int idx = req->index;
        if (idx < 0 || idx >= app.n_tabs)
            return remote_proto_error(req->id, "invalid tab index");
        if (idx != app.active)
            (void)app_switch_tab(idx, NULL, NULL, NULL, NULL);
        // Focus specific pane if given.
        if (req->pane >= 0 && app.n_tabs > 0) {
            Tab *ft = &app.tabs[app.active];
            PaneNode *fl[WORKSPACE_RAIL_MAX_PANES];
            int fn = 0;
            pane_collect_leaves(ft->root, fl, WORKSPACE_RAIL_MAX_PANES, &fn);
            for (int pi = 0; pi < fn; pi++) {
                if (fl[pi]->kind != PANE_LEAF) continue;
                uint64_t pid = (uint64_t)(uintptr_t)fl[pi]->leaf.session;
                if ((int64_t)pid == req->pane) {
                    ft->focused = fl[pi];
                    break;
                }
            }
        }
        return remote_proto_ok(req->id);
    }

    case REMOTE_CMD_RENAME: {
        if (app.n_tabs <= 0 || app.active < 0)
            return remote_proto_error(req->id, "no active workspace");
        int rename_idx = (req->index >= 0 && req->index < app.n_tabs) ? req->index : app.active;
        snprintf(app.tabs[rename_idx].name, sizeof(app.tabs[rename_idx].name),
                 "%s", req->name);
        return remote_proto_ok(req->id);
    }

    case REMOTE_CMD_SEND: {
        if (!cfg->remote_api_send)
            return remote_proto_error(req->id, "send disabled by config");
        if (pty_fd < 0)
            return remote_proto_error(req->id, "no active pty");
        pty_write(pty_fd, req->text, strlen(req->text));
        return remote_proto_ok(req->id);
    }

    case REMOTE_CMD_READ: {
        if (!te)
            return remote_proto_error(req->id, "no active terminal");
        char *dump = term_engine_dump_text(te);
        if (!dump)
            return remote_proto_error(req->id, "dump failed");
        cJSON *fields = cJSON_CreateObject();
        cJSON_AddStringToObject(fields, "text", dump);
        free(dump);
        return remote_proto_ok_obj(req->id, fields);
    }

    case REMOTE_CMD_RING: {
        if (app.n_tabs <= 0 || app.active < 0)
            return remote_proto_error(req->id, "no active workspace");
        Tab *rt = &app.tabs[app.active];
        PaneNode *rl[WORKSPACE_RAIL_MAX_PANES];
        int rn = 0;
        pane_collect_leaves(rt->root, rl, WORKSPACE_RAIL_MAX_PANES, &rn);
        for (int pi = 0; pi < rn; pi++) {
            if (rl[pi]->kind != PANE_LEAF) continue;
            uint64_t pid = (uint64_t)(uintptr_t)rl[pi]->leaf.session;
            workspace_status_note_notify(&g_workspace_status, pid,
                                         rl[pi] == rt->focused,
                                         req->message[0] ? req->message : "ring");
        }
        return remote_proto_ok(req->id);
    }

    case REMOTE_CMD_NONE:
    default:
        return remote_proto_error(req->id, "unknown command");
    }
}

// Send a response to a remote request parsed from a line.
static void dispatch_remote_line(const char *line,
                                 TermEngine *te, int pty_fd,
                                 const AppConfig *cfg, const char *config_path,
                                 uint16_t term_cols, uint16_t term_rows,
                                 int cell_width, int cell_height)
{
    RemoteRequest req;
    char err[256];
    if (!remote_proto_parse(line, &req, err, (int)sizeof(err))) {
        char *resp = remote_proto_error(0, err);
        remote_api_respond(g_remote_api, resp);
        free(resp);
        return;
    }
    char *resp = remote_execute(&req, te, pty_fd, cfg, config_path,
                                term_cols, term_rows,
                                cell_width, cell_height);
    if (resp) {
        remote_api_respond(g_remote_api, resp);
        free(resp);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
static void draw_gutter_hints(PaneRectEntry *entries, int count, int pane_gap,
                              float scale, int mouse_x, int mouse_y)
{
    const int hit_dist = (int)(4.0f * scale);
    const int handle_len = (int)(24.0f * scale);
    Color hint = UI2RAY(g_ui_theme.gutter_hover);

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            PaneRectEntry *a = &entries[i];
            PaneRectEntry *b = &entries[j];

            // Vertical split: a on the left, b on the right.
            if (a->y == b->y && a->h == b->h &&
                (b->x == a->x + a->w + pane_gap || a->x == b->x + b->w + pane_gap)) {
                int edge_x = (a->x < b->x)
                    ? a->x + a->w + pane_gap / 2
                    : b->x + b->w + pane_gap / 2;
                int mid_y = a->y + a->h / 2;
                if (abs(mouse_x - edge_x) <= hit_dist &&
                    mouse_y >= a->y && mouse_y <= a->y + a->h) {
                    DrawLine(edge_x, mid_y - handle_len / 2,
                             edge_x, mid_y + handle_len / 2, hint);
                }
            }

            // Horizontal split: a on top, b on bottom.
            if (a->x == b->x && a->w == b->w &&
                (b->y == a->y + a->h + pane_gap || a->y == b->y + b->h + pane_gap)) {
                int edge_y = (a->y < b->y)
                    ? a->y + a->h + pane_gap / 2
                    : b->y + b->h + pane_gap / 2;
                int mid_x = a->x + a->w / 2;
                if (abs(mouse_y - edge_y) <= hit_dist &&
                    mouse_x >= a->x && mouse_x <= a->x + a->w) {
                    DrawLine(mid_x - handle_len / 2, edge_y,
                             mid_x + handle_len / 2, edge_y, hint);
                }
            }
        }
    }
}


// (declared in ui_effects.h; no forward declaration needed.)

int main(int argc, char **argv)
{
    log_build_info();

    // Tier 1 of docs/crash-resilience-plan.md: log a marker + backtrace on
    // crash so "does Fangs actually crash in daily use" is data, not a
    // guess. Diagnostic only -- doesn't change how the process terminates.
    {
        char crash_log_path[1024];
        snprintf(crash_log_path, sizeof(crash_log_path), "%s/crash.log",
                 config_default_app_dir());
        crash_log_install(crash_log_path);
    }

    AppConfig cfg;
    const char *config_path = config_default_path();
    if (!config_load(&cfg, config_path)) {
        fprintf(stderr, "warning: failed to load config at %s; using defaults\n", config_path);
        toast_push(TOAST_WARN, "Failed to load config; using defaults.");
    }
    pty_set_tmux_wrap(cfg.tmux_wrap);

    // ---- CLI mode: "fangs ctl <subcommand> [args]" --------------------------
    if (argc >= 3 && strcmp(argv[1], "ctl") == 0) {
        const char *sock_dir = config_default_app_dir();
        char sock_path[1024];
        snprintf(sock_path, sizeof(sock_path), "%s/remote.sock", sock_dir);

        // Build JSON request from structured args.
        cJSON *req = cJSON_CreateObject();
        const char *sub = argv[2];

        if (strcmp(sub, "list") == 0) {
            cJSON_AddStringToObject(req, "cmd", "list");

        } else if (strcmp(sub, "new") == 0) {
            cJSON_AddStringToObject(req, "cmd", "new");
            for (int ai = 3; ai + 1 < argc; ai++) {
                if (strcmp(argv[ai], "--worktree") == 0)
                    cJSON_AddStringToObject(req, "worktree", argv[++ai]);
                else if (strcmp(argv[ai], "--name") == 0)
                    cJSON_AddStringToObject(req, "name", argv[++ai]);
                else if (strcmp(argv[ai], "--cwd") == 0)
                    cJSON_AddStringToObject(req, "cwd", argv[++ai]);
                else if (strcmp(argv[ai], "--run") == 0)
                    cJSON_AddStringToObject(req, "run", argv[++ai]);
            }

        } else if (strcmp(sub, "focus") == 0) {
            cJSON_AddStringToObject(req, "cmd", "focus");
            if (argc > 3)
                cJSON_AddNumberToObject(req, "index", atoi(argv[3]));
            for (int ai = 4; ai + 1 < argc; ai++) {
                if (strcmp(argv[ai], "--pane") == 0)
                    cJSON_AddNumberToObject(req, "pane", atoll(argv[++ai]));
            }

        } else if (strcmp(sub, "rename") == 0) {
            cJSON_AddStringToObject(req, "cmd", "rename");
            if (argc > 3)
                cJSON_AddStringToObject(req, "name", argv[3]);
            for (int ai = 4; ai + 1 < argc; ai++) {
                if (strcmp(argv[ai], "--index") == 0)
                    cJSON_AddNumberToObject(req, "index", atoi(argv[++ai]));
            }

        } else if (strcmp(sub, "send") == 0) {
            cJSON_AddStringToObject(req, "cmd", "send");
            if (argc > 3)
                cJSON_AddStringToObject(req, "text", argv[3]);
            for (int ai = 4; ai + 1 < argc; ai++) {
                if (strcmp(argv[ai], "--lines") == 0)
                    cJSON_AddNumberToObject(req, "lines", atoi(argv[++ai]));
            }

        } else if (strcmp(sub, "read") == 0) {
            cJSON_AddStringToObject(req, "cmd", "read");
            for (int ai = 3; ai + 1 < argc; ai++) {
                if (strcmp(argv[ai], "--lines") == 0)
                    cJSON_AddNumberToObject(req, "lines", atoi(argv[++ai]));
            }

        } else if (strcmp(sub, "ring") == 0) {
            cJSON_AddStringToObject(req, "cmd", "ring");
            if (argc > 3)
                cJSON_AddStringToObject(req, "message", argv[3]);

        } else {
            fprintf(stderr, "usage: fangs ctl <command> [args]\n\n");
            fprintf(stderr, "commands:\n");
            fprintf(stderr, "  list                        list workspaces\n");
            fprintf(stderr, "  new [--worktree P] [--name N] [--cwd D] [--run C]\n");
            fprintf(stderr, "  focus INDEX [--pane ID]\n");
            fprintf(stderr, "  rename NAME [--index INDEX]\n");
            fprintf(stderr, "  send TEXT [--lines N]\n");
            fprintf(stderr, "  read [--lines N]\n");
            fprintf(stderr, "  ring [MESSAGE]\n");
            fprintf(stderr, "  help                        show this message\n");
            cJSON_Delete(req);
            return 1;
        }

        // Serialize to JSON string for sending.
        char *json = cJSON_PrintUnformatted(req);
        cJSON_Delete(req);
        if (!json) {
            fprintf(stderr, "error: failed to build JSON request\n");
            return 1;
        }

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            fprintf(stderr, "error: cannot create socket\n");
            free(json);
            return 1;
        }
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);

        struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, (struct sockaddr *)&addr,
                    (socklen_t)sizeof(addr)) < 0) {
            fprintf(stderr, "error: cannot connect to Fangs socket at %s "
                    "(is Fangs running with remote_api enabled?)\n", sock_path);
            close(fd);
            free(json);
            return 1;
        }
        // Send JSON + newline.
        size_t jlen = strlen(json);
        ssize_t sent = write(fd, json, jlen);
        if (sent > 0) write(fd, "\n", 1);
        free(json);
        // Read response line.
        char resp[8192];
        ssize_t n = read(fd, resp, sizeof(resp) - 1);
        if (n > 0) {
            resp[n] = '\0';
            // Strip trailing newline for clean output.
            while (n > 0 && (resp[n-1] == '\n' || resp[n-1] == '\r'))
                resp[--n] = '\0';
            printf("%s\n", resp);
        } else {
            fprintf(stderr, "error: no response from Fangs\n");
            close(fd);
            return 1;
        }
        close(fd);
        return 0;
    }

    int font_size = cfg.font_size;

    // Enable HiDPI *before* creating the window so raylib can set up the
    // framebuffer at the native display resolution.
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);

    int initial_window_w = clamp_window_dimension(cfg.window_width, 800, FANGS_MIN_WINDOW_W);
    int initial_window_h = clamp_window_dimension(cfg.window_height, 600, FANGS_MIN_WINDOW_H);

    // Initialize window
    InitWindow(initial_window_w, initial_window_h, "Fangs");
    restore_window_position_if_visible(&cfg);
#ifndef __APPLE__
    set_linux_window_icon();
#endif
    desktop_notify_startup();
    // raylib's default exit key is ESC. A terminal must pass ESC straight
    // through to the child (vim normal mode, cancelling prompts, every TUI),
    // and the settings modal needs ESC to dismiss itself — not kill the app.
    // Disable the exit key so the loop only ends on the window close button.
    SetExitKey(KEY_NULL);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    // Left uncapped: raylib's own SetTargetFPS pacing sleeps blindly via
    // WaitTime() (clock-based, no idea whether input arrived mid-sleep), which
    // is what let a fast click's press+release land in the same sleep and get
    // coalesced away by GLFW's mouse-button callback. All frame pacing is done
    // by us instead, via the glfwWaitEventsTimeout() call at the end of the
    // main loop, which waits for a real event (waking near-instantly on input)
    // or a bounded timeout — see FANGS_IDLE_FPS/FANGS_ACTIVE_FPS.
    SetTargetFPS(0);

    // Start the remote API server if enabled in config.
    if (cfg.remote_api) {
        char err[256];
        const char *sd = config_default_app_dir();
        g_remote_api = remote_api_start(sd, getpid(), err, (int)sizeof(err));
        if (!g_remote_api)
            fprintf(stderr, "warning: remote_api start failed: %s\n", err);
    }

    // Process-global curl init (paired with ai_global_cleanup at exit). Done
    // here, before any worker thread exists, since it isn't thread-safe.
    ai_global_init();

    // App handles: sessions via tabs+panes, AI streams, accumulated data.
    int exit_code = 0;
    AiStream *active_stream = NULL;   // in-flight sidebar AI request
    AiStream *inline_stream = NULL;   // in-flight inline (Ctrl+Space) request
    char inline_answer[8192] = "";    // accumulates the inline command reply
    Font mono_font = {0};
    Font bold_font = {0};
    KittyImageRenderer *kitty_renderer = kitty_image_renderer_create();
    if (!kitty_renderer) {
        fprintf(stderr, "failed to create kitty image renderer\n");
        toast_push(TOAST_ERROR, "Failed to create image renderer.");
        exit_code = 1;
        goto cleanup;
    }

    // g_cmdblocks is set by sync_active_session() per-frame; prime it NULL.
    g_cmdblocks = NULL;

    int cell_width = 0;
    int cell_height = 0;
    mono_font = load_terminal_font(font_size, &cell_width, &cell_height);
    if (mono_font.texture.id == 0) {
        fprintf(stderr, "LoadFontFromMemory failed\n");
        toast_push(TOAST_ERROR, "Failed to load terminal font.");
        exit_code = 1;
        goto cleanup;
    }

    // Load the bold variant for SGR bold rendering (§E4).
    {
        Vector2 dpi_scale = fangs_content_scale();
        int font_size_px = (int)(font_size * dpi_scale.y);
        if (font_size_px < 1) font_size_px = 1;
        int bold_cp_count = 0;
        const int *bold_cps = terminal_font_codepoints(&bold_cp_count);
        bold_font = LoadFontFromMemory(".ttf", font_jetbrains_mono_bold,
                           (int)sizeof(font_jetbrains_mono_bold), font_size_px,
                           (int *)bold_cps, bold_cp_count);
        if (bold_font.texture.id != 0)
            SetTextureFilter(bold_font.texture, TEXTURE_FILTER_BILINEAR);
    }

    // Use the terminal font for all RayGUI widgets + AI panels, so the AI
    // features match the rest of the terminal instead of raylib's bitmap font.
    GuiSetFont(mono_font);

    // Content scale (HiDPI). Tracked so the font + UI rescale live when the
    // window is dragged to a differently-scaled monitor.
    float applied_scale = fangs_content_scale().y;

    // Small padding from window edges, in pixels.  Passed to render_terminal()
    // and handle_mouse() so all layout uses a single value.
    const int pad = 4;
    const int sidebar_width = 380;   // logical px; the UI renders in logical space
    const int min_terminal_w = 320;  // (crispness comes from the 2x font texture)

    Layout lo = layout_compute_with_rail(GetScreenWidth(), GetScreenHeight(),
                                         cfg.workspace_rail, cfg.workspace_rail_width, 56,
                                         ui_sidebar_visible(), sidebar_width,
                                         pad, min_terminal_w);
    int term_area_w = lo.terminal.w;

    uint16_t term_cols = 0;
    uint16_t term_rows = 0;
    compute_terminal_grid(term_area_w, pad, cell_width, cell_height,
                          &term_cols, &term_rows);

    // Install the PNG decoder (process-global, before any terminal exists)
    // so the Kitty graphics protocol can accept PNG images.
    ghostty_sys_set(GHOSTTY_SYS_OPT_DECODE_PNG, (const void *)decode_png);

    // Session restore: load the last-saved tab list (cwd + name), filter out
    // any whose directory no longer exists, and remap the active index
    // through that filter. Disabled or missing/empty -> today's unchanged
    // single-tab startup (first_cwd stays NULL).
    WorkspaceSessionState restore_state;
    memset(&restore_state, 0, sizeof(restore_state));
    WorkspaceSessionTab restore_valid[WORKSPACE_SESSION_MAX_TABS];
    int restore_valid_count = 0;
    int restore_active = 0;
    int restore_skipped = 0;
    if (cfg.restore_session) {
        char session_path[4096];
        if (workspace_session_default_path(session_path, sizeof(session_path))
            && workspace_session_load(session_path, &restore_state)) {
            for (int i = 0; i < restore_state.count; i++) {
                if (access(restore_state.tabs[i].cwd, F_OK) == 0) {
                    if (i == restore_state.active)
                        restore_active = restore_valid_count;
                    restore_valid[restore_valid_count++] = restore_state.tabs[i];
                } else {
                    restore_skipped++;
                }
            }
        }
    }
    const char *first_cwd = restore_valid_count > 0 ? restore_valid[0].cwd : NULL;

    // Initialise the first tab/session via the App (§16.5).
    Session *s = app_init_first_tab(term_cols, term_rows, cell_width, cell_height,
                                     cfg.scrollback,
                                     cfg.kitty_images, cfg.kitty_image_storage_mb,
                                     first_cwd);
    if (!s) {
        fprintf(stderr, "failed to create initial session\n");
        toast_push(TOAST_ERROR, "Failed to create terminal session.");
        exit_code = 1;
        goto cleanup;
    }
    register_session_effects(s);
    if (restore_valid_count > 0 && restore_valid[0].name[0])
        snprintf(app.tabs[0].name, sizeof(app.tabs[0].name), "%s", restore_valid[0].name);
    if (restore_valid_count > 0)
        app.tabs[0].color_tag = restore_valid[0].color_tag;
    // Cover the case where the whole session is just this one tab and the
    // user never adds/closes/renames another: still capture its final cwd
    // (which may drift via `cd`) on exit instead of only persisting on
    // multi-tab mutations.
    g_session_dirty = true;

    // Borrow handles for the per-frame input/render code via the active session.
    // sync_active_session() is called each frame to keep these current across
    // tab switches and split changes.
    TermEngine *te = NULL;
    int pty_fd = -1;
    pid_t child = -1;
    bool child_exited = true;
    sync_active_session(&te, &pty_fd, &child, &child_exited);

    // Restore any remaining tabs, then move focus to the persisted active tab.
    for (int i = 1; i < restore_valid_count; i++) {
        Session *rs = app_add_tab(term_cols, term_rows, cell_width, cell_height,
                                  cfg.scrollback, restore_valid[i].cwd,
                                  cfg.kitty_images, cfg.kitty_image_storage_mb,
                                  &te, &pty_fd, &child, &child_exited);
        if (rs && restore_valid[i].name[0])
            snprintf(app.tabs[app.n_tabs - 1].name,
                    sizeof(app.tabs[app.n_tabs - 1].name), "%s", restore_valid[i].name);
        if (rs)
            app.tabs[app.n_tabs - 1].color_tag = restore_valid[i].color_tag;
    }
    if (restore_valid_count > 1) {
        int want_active = (restore_active >= 0 && restore_active < app.n_tabs) ? restore_active : 0;
        if (want_active != app.active)
            app_switch_tab(want_active, &te, &pty_fd, &child, &child_exited);
    }
    if (restore_skipped > 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%d workspace%s skipped - directory no longer exists",
                restore_skipped, restore_skipped == 1 ? "" : "s");
        toast_push(TOAST_WARN, msg);
    }

    // Init workspace attention model for the rail.
    if (!g_workspace_status_inited) {
        workspace_status_init(&g_workspace_status);
        g_workspace_status_inited = true;
    }
    if (!g_git_status_sampler) {
        g_git_status_sampler = workspace_git_status_start();
        if (!g_git_status_sampler)
            fprintf(stderr, "warning: workspace git-status sampler unavailable\n");
    }

    GhosttyTerminal terminal = term_engine_terminal(te);
    GhosttyRenderState render_state = term_engine_render_state(te);
    GhosttyRenderStateRowIterator row_iter = term_engine_row_iter(te);
    GhosttyRenderStateRowCells row_cells = term_engine_row_cells(te);
    GhosttyKittyGraphicsPlacementIterator placement_iter = term_engine_placement_iter(te);
    GhosttyKeyEncoder key_encoder = term_engine_key_encoder(te);
    GhosttyKeyEvent key_event = term_engine_key_event(te);
    GhosttyMouseEncoder mouse_encoder = term_engine_mouse_encoder(te);
    GhosttyMouseEvent mouse_event = term_engine_mouse_event(te);

    bool child_reaped = false;
    int child_exit_status = -1;

    // Track window size so we only recalculate the grid on actual changes.
    int prev_width = GetScreenWidth();
    int prev_height = GetScreenHeight();
    int prev_term_area_w = term_area_w;
    char applied_theme[32] = "";   // engine-applied theme name; re-apply on change

    // Track focus state so we only send focus events on transitions.
    // Initialize from the actual window state to avoid a spurious
    // focus-lost event on startup.
    bool prev_focused = IsWindowFocused();

    // Scrollbar drag state — when the user clicks and drags the
    // scrollbar thumb we continuously reposition the viewport.
    bool scrollbar_dragging = false;

    const char *phase3_smoke_report_path = getenv("FANGS_PHASE3_SMOKE_REPORT");
    const char *phase3_smoke_screenshot_path = getenv("FANGS_PHASE3_SMOKE_SCREENSHOT");
    bool phase3_smoke = (phase3_smoke_report_path && phase3_smoke_report_path[0] != '\0')
                     || (phase3_smoke_screenshot_path && phase3_smoke_screenshot_path[0] != '\0');
    bool phase3_smoke_started = false;
    int phase3_smoke_frames = 0;

    // Command-block visual smoke: feed canned OSC-133 marks + output so the
    // block overlay can be screenshotted deterministically without a shell
    // that has the integration snippet. Dev/CI only; gated by the env var.
    const char *blocks_smoke_path = getenv("FANGS_BLOCKS_SMOKE_SCREENSHOT");
    bool blocks_smoke = (blocks_smoke_path && blocks_smoke_path[0] != '\0');
    bool blocks_smoke_started = false;
    int  blocks_smoke_frames = 0;

    // Kitty image visual smoke: feed a tiny inline PNG through Ghostty's
    // Kitty graphics parser, render a few frames, then export a screenshot.
    const char *kitty_smoke_path = getenv("FANGS_KITTY_SMOKE_SCREENSHOT");
    bool kitty_smoke = (kitty_smoke_path && kitty_smoke_path[0] != '\0');
    bool kitty_smoke_started = false;
    int  kitty_smoke_frames = 0;
    int frame_count = 0;
    uint64_t last_activity_ms = 0;   // set on the first frame, below
    // SetTargetFPS is uncapped (see InitWindow above), so raylib's own
    // GetFrameTime()/CORE.Time.frame no longer includes any pacing wait and
    // reads near-zero every frame — wrong delta for time-based animation
    // (toast fade, sidebar scroll-follow). Track wall-clock time ourselves
    // instead and pass that to anything that used to call GetFrameTime().
    double last_frame_sec = 0.0;   // set on the first frame, below
    WorkflowRegistry palette_workflows;
    workflows_init(&palette_workflows);
    ui_palette_set_workflows(&palette_workflows);
    UiPaletteSelection pending_palette_selection = palette_selection_none();

    // Each frame: handle resize → read pty → process input → render.
    while (!WindowShouldClose()) {
        frame_count++;

        struct timespec _now_ts;
        clock_gettime(CLOCK_MONOTONIC, &_now_ts);
        uint64_t now_ms = (uint64_t)_now_ts.tv_sec * 1000 + (uint64_t)_now_ts.tv_nsec / 1000000;
        double now_sec = (double)_now_ts.tv_sec + (double)_now_ts.tv_nsec / 1.0e9;
        if (frame_count == 1) { last_activity_ms = now_ms; last_frame_sec = now_sec; }
        double frame_dt_sec = now_sec - last_frame_sec;
        last_frame_sec = now_sec;

        sync_active_runtime(&te, &pty_fd, &child, &child_exited,
                            &terminal, &render_state, &row_iter, &row_cells,
                            &placement_iter, &key_encoder, &key_event,
                            &mouse_encoder, &mouse_event);
        if (phase3_smoke && !phase3_smoke_started) {
            if (!ui_sidebar_visible())
                ui_sidebar_toggle();
            ui_sidebar_push(MSG_USER, "phase3 smoke prompt");
            ui_sidebar_push(MSG_SYSTEM, "(AI not wired yet - Phase 4)");
            // Visual toast smoke: exercise the polished toast renderer in the
            // screenshot so placement and styling can be inspected headlessly.
            toast_push(TOAST_INFO,  "Toast smoke: info notification");
            toast_push(TOAST_WARN,  "Toast smoke: warning notification");
            toast_push(TOAST_ERROR, "Toast smoke: error notification");
            // FANGS_SMOKE_FOCUS opens the input in edit mode so the headless
            // smoke can exercise GuiTextBox's edit path — a regression guard for
            // the narrow-sidebar SIGBUS. Default stays unfocused (PTY passthrough).
            ui_sidebar_focus(getenv("FANGS_SMOKE_FOCUS") != NULL);
            if (getenv("FANGS_SMOKE_SETTINGS") != NULL)
                ui_settings_toggle();
            phase3_smoke_started = true;
        }

        if (blocks_smoke && !blocks_smoke_started) {
            // Full A/B/C/D protocol per command, exactly as the shell snippet
            // emits (docs/shell-integration.md): A=prompt start, B=prompt end,
            // C=exec (output begins), D=done. C is what lets the engine's
            // select_output identify the output region for "copy output".
            static const char canned[] =
                "\x1b[2J\x1b[3J\x1b[H"
                "\x1b]133;A\x1b\\$ \x1b]133;B\x1b\\npm run build\r\n\x1b]133;C\x1b\\  vite v5.4 building for production...\r\n  built in 1.21s\r\n\x1b]133;D;0\x1b\\"
                "\x1b]133;A\x1b\\$ \x1b]133;B\x1b\\cargo test\r\n\x1b]133;C\x1b\\  error[E0382]: borrow of moved value: `cfg`\r\n  test result: FAILED. 1 passed; 1 failed\r\n\x1b]133;D;101\x1b\\"
                "\x1b]133;A\x1b\\$ \x1b]133;B\x1b\\git status\r\n\x1b]133;C\x1b\\  On branch main\r\n  nothing to commit, working tree clean\r\n\x1b]133;D;0\x1b\\"
                "\x1b]133;A\x1b\\$ \x1b]133;B\x1b\\";
            cmdblocks_feed(g_cmdblocks, te, (const uint8_t *)canned, sizeof(canned) - 1);
            blocks_smoke_started = true;
        }

        if (kitty_smoke && !kitty_smoke_started) {
            static const char canned[] =
                "\x1b[2J\x1b[3J\x1b[H"
                "Kitty image smoke\r\n"
                "\x1b_Gf=100,a=T,s=2,v=2;iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAFElEQVR4nGP8z8Dwn4GBgYGJAQoAHxcCAsuzUSwAAAAASUVORK5CYII=\x1b\\"
                "\r\n";
            cmdblocks_feed(g_cmdblocks, te, (const uint8_t *)canned, sizeof(canned) - 1);
            kitty_smoke_started = true;
        }

        // Config changes from the settings modal or the font-zoom chord are
        // applied once at the end of the frame via apply_config(); declared here
        // so the zoom chord (below, before handle_input) can request it.
        bool apply_saved_config = false;

        bool ctrl_down  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        bool shift_down = IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT);
        bool cmd_down   = IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER);
        bool alt_down   = IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT);

        // Idle-frame-rate activity signal: any real input this frame.
        // GetKeyPressed() is otherwise unused in this file, so draining its
        // queue here has no side effects on input handling elsewhere.
        bool _any_key_pressed = false;
        while (GetKeyPressed() != 0) _any_key_pressed = true;
        Vector2 _mouse_delta = GetMouseDelta();
        if (_any_key_pressed || GetMouseWheelMove() != 0.0f
            || _mouse_delta.x != 0.0f || _mouse_delta.y != 0.0f
            || IsMouseButtonDown(MOUSE_BUTTON_LEFT)
            || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)
            || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            last_activity_ms = now_ms;
        }

        if (pending_palette_selection.type == UI_PALETTE_SELECTION_ACTION) {
            execute_host_action(pending_palette_selection.action_id,
                                &te, &pty_fd, &child, &child_exited,
                                &terminal, &render_state, &row_iter, &row_cells,
                                &placement_iter, &key_encoder, &key_event,
                                &mouse_encoder, &mouse_event,
                                &cfg, config_path, &palette_workflows,
                                cell_width, cell_height, pad,
                                term_cols, term_rows,
                                lo.terminal.x, lo.terminal.y, term_area_w,
                                now_ms,
                                &apply_saved_config,
                                &prev_term_area_w);
            pending_palette_selection = palette_selection_none();
        } else if (pending_palette_selection.type == UI_PALETTE_SELECTION_WORKFLOW) {
            handle_workflow_selection(&palette_workflows, pending_palette_selection, pty_fd);
            pending_palette_selection = palette_selection_none();
        } else if (pending_palette_selection.type == UI_PALETTE_SELECTION_WORKSPACE) {
            int tab_idx = pending_palette_selection.tab_index;
            if (tab_idx >= 0 && tab_idx < app.n_tabs) {
                app_switch_tab(tab_idx, &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
            }
            pending_palette_selection = palette_selection_none();
        }

        bool palette_chord_consumed = false;
        if (IsKeyPressed(KEY_P) && (cmd_down || (ctrl_down && shift_down))
            && !ui_settings_open() && !ui_inline_active()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                   &terminal, &render_state, &row_iter,
                                                   &row_cells, &placement_iter,
                                                   &key_encoder, &key_event,
                                                   &mouse_encoder, &mouse_event);
            refresh_palette_workflows(&palette_workflows, config_path, session_cwd(cur));
            refresh_palette_workspaces();
            ui_palette_open();
            ui_sidebar_focus(false);
            g_search_active = false;
            palette_chord_consumed = true;
            drain_char_queue();
        }

        // Intercept settings shortcut before handle_input() can forward the
        // comma key to the PTY. Accept Super+, on macOS and Ctrl+, elsewhere.
        bool settings_shortcut_consumed = false;
        if (IsKeyPressed(KEY_COMMA)
            && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)
                || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            ui_settings_toggle();
            settings_shortcut_consumed = true;
            drain_char_queue();
        }

        bool sidebar_chord_consumed = false;
        bool sidebar_chord = IsKeyPressed(KEY_B)
            && ((IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER))
                || ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                    && (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))));
        if (sidebar_chord && !ui_settings_open() && !ui_palette_is_open()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            ui_sidebar_toggle();
            ui_sidebar_focus(ui_sidebar_visible());
            sidebar_chord_consumed = true;
            drain_char_queue();
        }

        // Inline AI: Ctrl+Space opens a floating prompt anchored at the cursor.
        bool inline_chord = IsKeyPressed(KEY_SPACE)
            && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
        if (inline_chord && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            open_inline_at_cursor(render_state, cell_width, cell_height,
                                  lo.terminal.x, lo.terminal.y, pad);
            drain_char_queue();
        }

        bool over_rail_resize_handle = g_rail_resizing
            || (lo.rail_visible && !ui_menu_active(&g_rail_menu) && g_drag_from < 0
                && GetMouseX() >= lo.rail.x + lo.rail.w - RAIL_RESIZE_HANDLE_PAD
                && GetMouseX() <= lo.rail.x + lo.rail.w + RAIL_RESIZE_HANDLE_PAD
                && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h);

        int mouse_cursor = MOUSE_CURSOR_DEFAULT;
        if (over_rail_resize_handle) {
            mouse_cursor = MOUSE_CURSOR_RESIZE_EW;
        } else if (!ui_settings_open() && !ui_inline_active() && !ui_palette_is_open()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && GetMouseX() >= lo.terminal.x && GetMouseX() < lo.terminal.x + term_area_w
            && GetMouseY() >= lo.terminal.y && GetMouseY() < lo.terminal.y + lo.terminal.h) {
            int ucc = (GetMouseX() - lo.terminal.x - pad) / cell_width;
            int ucr = (GetMouseY() - lo.terminal.y - pad) / cell_height;
            Session *ref_session = NULL;
            if (app.n_tabs > 0) {
                PaneNode *fl = app.tabs[app.active].focused;
                if (fl && fl->kind == PANE_LEAF) ref_session = fl->leaf.session;
            }
            const char *ref_cwd = ref_session ? session_cwd(ref_session) : NULL;
            char hover_url[2048];
            mouse_cursor = (url_at(ucr, ucc, hover_url, (int)sizeof(hover_url))
                            || file_ref_at(ucr, ucc, ref_cwd, hover_url, (int)sizeof(hover_url)))
                ? MOUSE_CURSOR_POINTING_HAND
                : MOUSE_CURSOR_IBEAM;
        }
        SetMouseCursor(mouse_cursor);

        // Tab/split/focus chords: Cmd on macOS, Ctrl+Shift on Linux. Plain
        // Ctrl+<key> must reach the shell (Ctrl+D EOF, Ctrl+W word-erase,
        // Ctrl+T transpose), so these never trigger on bare Ctrl — matching the
        // copy/paste chords above.
        bool tab_chord  = cmd_down || (ctrl_down && shift_down);

        // Command-block navigation: Cmd/Ctrl + Up/Down jumps between command
        // prompts (Warp-style). Intercepted before handle_input so the arrow
        // keys aren't also forwarded to the child shell.
        bool block_nav_consumed = false;
        if ((cmd_down || ctrl_down) && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !g_search_active && !ui_sidebar_focused()) {
            if (IsKeyPressed(KEY_UP))   block_nav_consumed = cmdblocks_navigate(g_cmdblocks, te, -1);
            if (IsKeyPressed(KEY_DOWN)) block_nav_consumed = cmdblocks_navigate(g_cmdblocks, te, +1);
        }

        // Ask AI about the latest finished command block: keyboard equivalent
        // of the hover "Ask AI" button, using the same §15 action path.
        // Cmd+Shift+/ (macOS) / Ctrl+Shift+/ (Linux) — tab_chord already encodes
        // that split, so requiring shift on top yields both. (Super+Shift on
        // Linux would be swallowed by the window manager.)
        bool ask_last_command_consumed = false;
        if (tab_chord && shift_down && IsKeyPressed(KEY_SLASH)
            && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !g_search_active) {
            ask_ai_about_latest_block(te, term_rows);
            ask_last_command_consumed = true;
            drain_char_queue();
        }

        // Clipboard: Ctrl+Shift+C/V (Linux) or Cmd+C/V (macOS); Shift+Insert pastes.
        // Intercept BEFORE handle_input so Ctrl+Shift+C never reaches the pty as ^C.
        bool clipboard_consumed = false;
        if (((ctrl_down && shift_down) || cmd_down) && IsKeyPressed(KEY_C)) {
            sel_copy_to_clipboard();
            clipboard_consumed = true;
        }
        // Only paste into the PTY when no on-screen text field is capturing
        // input — otherwise the settings field, AI sidebar input, and inline
        // prompt (all raygui GuiTextBox, which now handles paste itself) would
        // be bypassed and the paste would land in the terminal behind them.
        bool text_field_capturing = ui_settings_open() || ui_inline_active()
            || ui_palette_is_open() || ui_workflow_prompt_active() || ui_rename_prompt_active() || ui_broadcast_prompt_active() || ui_menu_active(&g_rail_menu)
            || ui_sidebar_focused() || g_search_active;
        if ((((ctrl_down && shift_down) || cmd_down) && IsKeyPressed(KEY_V))
            || (shift_down && IsKeyPressed(KEY_INSERT))) {
            if (!text_field_capturing) {
                do_paste(pty_fd, terminal);
                clipboard_consumed = true;
                drain_char_queue();
            }
        }

        // Find overlay: Ctrl+F / Cmd+F toggles; while open it captures typing.
        bool search_consumed = false;
        if ((ctrl_down || cmd_down) && IsKeyPressed(KEY_F)
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            g_search_active = !g_search_active;
            if (!g_search_active) g_search_query[0] = '\0';
            search_consumed = true;
            drain_char_queue();
        }
        if (g_search_active) {
            if (IsKeyPressed(KEY_ESCAPE)) { g_search_active = false; g_search_query[0] = '\0'; }
            else {
                search_input();
                // Explicit, one-shot cross-workspace scan — never run this
                // per-keystroke/per-frame, only on an intentional Enter.
                if (IsKeyPressed(KEY_ENTER) && g_search_query[0] != '\0')
                    perform_cross_workspace_search(g_search_query);
            }
        }

        // Font zoom: Ctrl/Cmd + '='/'+' grows, '-' shrinks, '0' resets to the
        // config default. Intercepted before handle_input so '='/'-' aren't also
        // forwarded to the shell. Mutates cfg.font_size, persists, and lets the
        // end-of-frame apply_config() reload the font + reflow the grid + pty.
        bool zoom_consumed = false;
        if ((ctrl_down || cmd_down) && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !g_search_active && !ui_sidebar_focused()) {
            int new_size = cfg.font_size;
            if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
                new_size += 1;
            else if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
                new_size -= 1;
            else if (IsKeyPressed(KEY_ZERO) || IsKeyPressed(KEY_KP_0))
                new_size = FANGS_DEFAULT_FONT_SIZE;
            if (new_size < FANGS_MIN_FONT_SIZE) new_size = FANGS_MIN_FONT_SIZE;
            if (new_size > FANGS_MAX_FONT_SIZE) new_size = FANGS_MAX_FONT_SIZE;
            if (save_font_size_action(&cfg, config_path, new_size, &apply_saved_config)) {
                zoom_consumed = true;
                drain_char_queue();  // drain '='/'-'/'+' text events
            }
        }

        // Tab and split operations: Cmd+… (macOS) / Ctrl+Shift+… (Linux).
        // Intercepted before handle_input so these key events never reach the
        // shell — and gated on tab_chord so bare Ctrl+<key> still does.
        if (tab_chord && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !g_search_active && !ui_sidebar_focused()) {

            // --- New tab: Cmd/Ctrl+T ---
            if (IsKeyPressed(KEY_T)) {
                Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                       &terminal, &render_state, &row_iter,
                                                       &row_cells, &placement_iter,
                                                       &key_encoder, &key_event,
                                                       &mouse_encoder, &mouse_event);
                app_add_tab(term_cols, term_rows, cell_width, cell_height,
                            cfg.scrollback, session_cwd(cur),
                            cfg.kitty_images, cfg.kitty_image_storage_mb,
                            &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
            }

            // --- Close focused pane / active tab: Cmd/Ctrl+W ---
            if (IsKeyPressed(KEY_W)) {
                app_close_active();
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
            }

            // --- Tab switch: Cmd/Ctrl+1..9 ---
            int tab_idx = -1;
                 if (IsKeyPressed(KEY_ONE))   tab_idx = 0;
            else if (IsKeyPressed(KEY_TWO))   tab_idx = 1;
            else if (IsKeyPressed(KEY_THREE)) tab_idx = 2;
            else if (IsKeyPressed(KEY_FOUR))  tab_idx = 3;
            else if (IsKeyPressed(KEY_FIVE))  tab_idx = 4;
            else if (IsKeyPressed(KEY_SIX))   tab_idx = 5;
            else if (IsKeyPressed(KEY_SEVEN)) tab_idx = 6;
            else if (IsKeyPressed(KEY_EIGHT)) tab_idx = 7;
            else if (IsKeyPressed(KEY_NINE))  tab_idx = 8;
            if (tab_idx >= 0) {
                app_switch_tab(tab_idx, &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
            }

            // --- Split D. Direction picker: on macOS Shift = vertical
            // (Cmd+D horizontal, Cmd+Shift+D vertical, per spec §16.8); on
            // Linux Shift is already part of the chord, so Alt = vertical
            // (Ctrl+Shift+D horizontal, Ctrl+Shift+Alt+D vertical). ---
            if (IsKeyPressed(KEY_D)) {
                bool vertical = cmd_down ? shift_down : alt_down;
                Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                       &terminal, &render_state, &row_iter,
                                                       &row_cells, &placement_iter,
                                                       &key_encoder, &key_event,
                                                       &mouse_encoder, &mouse_event);
                app_split_focused(vertical ? PANE_VSPLIT : PANE_HSPLIT,
                                  term_cols, term_rows, cell_width, cell_height,
                                  cfg.scrollback, session_cwd(cur),
                                  cfg.kitty_images, cfg.kitty_image_storage_mb,
                                  &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
            }

            // --- Prev/next workspace: Cmd+Shift+[ / ] (macOS) /
            //     Ctrl+Shift+[ / ] (Linux) ---
            if (shift_down && app.n_tabs > 1
                && (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_RIGHT_BRACKET))) {
                int dir = IsKeyPressed(KEY_RIGHT_BRACKET) ? 1 : -1;
                int next = (app.active + dir + app.n_tabs) % app.n_tabs;
                app_switch_tab(next, &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
            }

            // --- Jump to the most severe unread pane: Cmd+Shift+U
            //     (Ctrl+Shift+U on Linux). Resolved before drawing. ---
            if (shift_down && IsKeyPressed(KEY_U)) {
                uint64_t ids[WORKSPACE_STATUS_MAX_PANES];
                int n = 0;
                for (int ti = 0; ti < app.n_tabs; ti++) {
                    if (!app.tabs[ti].root)
                        continue;
                    PaneNode *jl[WORKSPACE_RAIL_MAX_PANES];
                    int jn = 0;
                    pane_collect_leaves(app.tabs[ti].root, jl,
                                        WORKSPACE_RAIL_MAX_PANES, &jn);
                    for (int pj = 0; pj < jn && n < WORKSPACE_STATUS_MAX_PANES; pj++) {
                        if (jl[pj]->kind != PANE_LEAF)
                            continue;
                        ids[n++] = pane_id_for_session(jl[pj]->leaf.session);
                    }
                }
                g_jump_request = workspace_status_top_pane(&g_workspace_status, ids, n);
            }

            // --- Rename workspace: Cmd+Shift+R (Ctrl+Shift+R on Linux) ---
            if (shift_down && IsKeyPressed(KEY_R)
                && app.n_tabs > 0 && app.active >= 0) {
                ui_rename_prompt_open(app.active, app.tabs[app.active].name);
                drain_char_queue();
            }

            // --- Toggle workspace rail: Cmd+Shift+E (Ctrl+Shift+E on Linux) ---
            if (shift_down && IsKeyPressed(KEY_E)) {
                cfg.workspace_rail = !cfg.workspace_rail;
                g_rail_collapsed = false;
                config_save(&cfg, config_path);
                prev_term_area_w = -1;
                drain_char_queue();
            }
        }

        // --- Pane focus move: Cmd+Opt+Arrow (macOS) / Ctrl+Shift+Arrow (Linux).
        // Only when the active tab has more than one pane, so single-pane users
        // keep Ctrl+Shift+Arrow / modified arrows for their TUIs (§16.8). ---
        bool focus_chord = (cmd_down && alt_down) || (ctrl_down && shift_down);
        if (focus_chord && app.n_tabs > 0
            && pane_count_leaves(app.tabs[app.active].root) > 1
            && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !g_search_active && !ui_sidebar_focused()) {
            int dx = 0, dy = 0;
            if (IsKeyPressed(KEY_LEFT))       dx = -1;
            else if (IsKeyPressed(KEY_RIGHT)) dx = 1;
            else if (IsKeyPressed(KEY_UP))    dy = -1;
            else if (IsKeyPressed(KEY_DOWN))  dy = 1;
            if (dx != 0 || dy != 0) {
                Tab *t = &app.tabs[app.active];
                PaneNode *nf = pane_focus_move(t->root, t->focused, dx, dy);
                if (nf) {
                    t->focused = nf;
                    sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                            &terminal, &render_state, &row_iter,
                                            &row_cells, &placement_iter,
                                            &key_encoder, &key_event,
                                            &mouse_encoder, &mouse_event);
                }
                drain_char_queue();
            }
        }

        // Recalculate grid dimensions when the window or split layout changes.
        // We update both the ghostty terminal (so it reflows text) and the
        // pty's winsize (so the child shell knows about the new size and
        // can send SIGWINCH to its foreground process group).
        int w = GetScreenWidth();
        int h = GetScreenHeight();

        // Re-detect content scale: if the window moved to a monitor with a
        // different scale, rebuild the font (and reflow) so font_size stays a
        // consistent on-screen size. Scale only drives font-texture resolution;
        // the UI and layout are sized in logical px (see the 1.0f passed below).
        float ui_scale = fangs_content_scale().y;
        int pane_gap = (int)(6.0f * applied_scale);
        if (pane_gap < 0) pane_gap = 0;
        if (pane_gap > 32) pane_gap = 32;
        if (ui_scale != applied_scale) {
            if (rebuild_terminal_font(&mono_font, &bold_font, font_size, &cell_width, &cell_height,
                                      &term_cols, &term_rows, term_area_w, pad, te, pty_fd)) {
                applied_scale = ui_scale;
                prev_term_area_w = -1;   // force the grid/winsize resync below
            } else {
                ui_scale = applied_scale;   // reload failed; keep the current font
            }
        }

        int base_rail_width = g_rail_collapsed ? WORKSPACE_RAIL_COLLAPSED_WIDTH : cfg.workspace_rail_width;
        bool rail_animating = false;
        int effective_rail_width;
        if (g_rail_resizing) {
            effective_rail_width = g_rail_drag_width;
            g_rail_width_anim = (float)g_rail_drag_width; // keep in sync so release doesn't jump
        } else {
            if (g_rail_width_anim < 0.0f) g_rail_width_anim = (float)base_rail_width; // first frame
            float target = (float)base_rail_width;
            if (fabsf(target - g_rail_width_anim) > 0.5f) {
                g_rail_width_anim += (target - g_rail_width_anim) *
                    fminf(1.0f, (float)frame_dt_sec * RAIL_ANIM_SPEED);
                rail_animating = true;
            } else {
                g_rail_width_anim = target;
            }
            effective_rail_width = (int)(g_rail_width_anim + 0.5f);
        }
        lo = layout_compute_with_rail(w, h, cfg.workspace_rail, effective_rail_width, 56,
                                      ui_sidebar_visible(), sidebar_width,
                                      pad, min_terminal_w);
        term_area_w = lo.terminal.w;
        // While the rail's resize handle is held, only the *visual* rect
        // above (lo.terminal, which the mouse drives live) tracks the drag —
        // the terminal engine/pty grid is deliberately left alone until the
        // drag commits (g_rail_resizing goes false, below). Re-flowing the
        // real grid on every intermediate frame is what made any
        // already-wrapped long line walk the prompt further down the screen
        // with each pixel of drag: each resize's reflow is individually
        // correct, but firing it dozens of times per gesture made even a
        // modest final width change look like a runaway one.
        if (!g_rail_resizing && !rail_animating
            && (w != prev_width || h != prev_height || term_area_w != prev_term_area_w)) {
            last_activity_ms = now_ms;   // resize counts as activity
            // Fallback/default grid (whole-area size) — used for sizing brand
            // new tabs/sessions elsewhere and as the value if the active tab
            // has no pane tree yet. Each existing pane is then resized to its
            // own rect below, which overrides term_cols/term_rows with the
            // focused leaf's actual (possibly smaller) grid.
            compute_terminal_grid(term_area_w, pad, cell_width, cell_height,
                                  &term_cols, &term_rows);
            Tab *tab = &app.tabs[app.active];
            resize_pane_leaves_to_fit(tab, lo.terminal.x, lo.terminal.y,
                                      lo.terminal.w, lo.terminal.h,
                                      pane_gap,
                                      pad, cell_width, cell_height,
                                      &term_cols, &term_rows);
            prev_width = w;
            prev_height = h;
            prev_term_area_w = term_area_w;
        }

        // Active tab's current pane rects — used both to focus-follow a
        // click into a different pane (below) and, later this frame, to
        // scope mouse coordinate math to whichever pane ends up focused
        // instead of the union of all panes (§16.4).
        PaneRectEntry active_pane_rects[64];
        PaneRectCollector active_pane_collector = {
            .entries = active_pane_rects, .count = 0, .capacity = 64,
        };
        if (app.n_tabs > 0 && app.tabs[app.active].root) {
            layout_compute_panes(app.tabs[app.active].root,
                                 lo.terminal.x, lo.terminal.y,
                                 lo.terminal.w, lo.terminal.h,
                                 pane_gap,
                                 pane_rect_collect_cb, &active_pane_collector);
        }

        // --- Click-to-focus: clicking inside a non-focused pane switches
        // focus to it before this frame's runtime sync (below), so the
        // click's own selection/mouse-forwarding targets the pane the user
        // actually clicked rather than whatever was focused before. ---
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && app.n_tabs > 0
            && !ui_settings_open() && !ui_inline_active()
            && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active()
            && !ui_menu_active(&g_rail_menu) && !ui_sidebar_focused()
            && GetMouseX() >= lo.terminal.x && GetMouseX() < lo.terminal.x + lo.terminal.w
            && GetMouseY() >= lo.terminal.y && GetMouseY() < lo.terminal.y + lo.terminal.h) {
            Tab *click_tab = &app.tabs[app.active];
            int mx = GetMouseX(), my = GetMouseY();
            for (int i = 0; i < active_pane_collector.count; i++) {
                PaneRectEntry *r = &active_pane_collector.entries[i];
                if (mx >= r->x && mx < r->x + r->w && my >= r->y && my < r->y + r->h) {
                    if (r->leaf != click_tab->focused)
                        click_tab->focused = r->leaf;
                    break;
                }
            }
        }

        // Drain PTY output from ALL sessions in the active tab and feed their
        // VT engines. In blocks-smoke mode we ignore the real shell so the
        // canned content renders without interleaving.
        Session *active_session = sync_active_runtime(&te, &pty_fd, &child, &child_exited,
                                                      &terminal, &render_state, &row_iter,
                                                      &row_cells, &placement_iter,
                                                      &key_encoder, &key_event,
                                                      &mouse_encoder, &mouse_event);
        if (!active_session)
            break;   // no sessions left — exit the app

        bool any_pty_activity = false;
        if (!blocks_smoke) {
            if (app.n_tabs > 0 && app.active >= 0) {
                Tab *active_tab = &app.tabs[app.active];

                // Clear workspace attention when focus changes to a pane.
                if (active_tab->focused && active_tab->focused->kind == PANE_LEAF) {
                    uint64_t focused_id = pane_id_for_session(active_tab->focused->leaf.session);
                    if (focused_id != g_last_focused_pane_id) {
                        workspace_status_clear(&g_workspace_status, focused_id);
                        g_last_focused_pane_id = focused_id;
                    }
                }

                uint64_t live_ids[WORKSPACE_STATUS_MAX_PANES];
                int live_count = 0;
                WorkspaceGitStatusTarget git_targets[WORKSPACE_GIT_STATUS_MAX_TARGETS];
                int git_target_count = 0;
                bool window_focused_now = IsWindowFocused();

                for (int ti = 0; ti < app.n_tabs; ti++) {
                    Tab *tab = &app.tabs[ti];
                    if (!tab->root)
                        continue;

                    PaneNode *leaves[WORKSPACE_RAIL_MAX_PANES];
                    int n_leaves = 0;
                    pane_collect_leaves(tab->root, leaves, WORKSPACE_RAIL_MAX_PANES, &n_leaves);
                    for (int i = 0; i < n_leaves; i++) {
                        if (leaves[i]->kind != PANE_LEAF)
                            continue;

                        Session *ss = leaves[i]->leaf.session;
                        bool active_pane = (ti == app.active && leaves[i] == active_tab->focused);
                        bool focused = window_focused_now && active_pane;
                        uint64_t pane_id = pane_id_for_session(ss);
                        if (live_count < WORKSPACE_STATUS_MAX_PANES)
                            live_ids[live_count++] = pane_id;
                        if (git_target_count < WORKSPACE_GIT_STATUS_MAX_TARGETS) {
                            git_targets[git_target_count].pane_id = pane_id;
                            snprintf(git_targets[git_target_count].cwd,
                                     sizeof(git_targets[git_target_count].cwd),
                                     "%s", session_cwd(ss) ? session_cwd(ss) : "");
                            git_target_count++;
                        }

                        // Feed PTY and detect background output activity.
                        SessionFeedStats stats = session_feed_pty_stats(ss);
                        if (stats.bytes_read > 0)
                            any_pty_activity = true;
                        workspace_status_note_output_at(&g_workspace_status, pane_id,
                                                        focused, stats.bytes_read, now_ms);

                        if ((stats.eof || stats.error) && !focused) {
                            session_reap(ss);
                            workspace_status_note_child_exit(&g_workspace_status, pane_id,
                                                             focused, session_exit_status(ss));
                        }

                        // Detect new command completions in background panes.
                        CmdBlocks *cb = (CmdBlocks *)session_cmdblocks(ss);
                        unsigned long seq = cmdblocks_completion_seq(cb);
                        if (seq > 0) {
                            unsigned long prev = pane_update_completion_seq(ss, seq);
                            if (prev != seq) {
                                int code = cmdblocks_latest_exit_code(cb);
                                workspace_status_note_command(&g_workspace_status, pane_id,
                                                              focused, code);
                                session_ports_clear(ss);
                            }
                        }

                        // Detect agent notifications (BEL / OSC 9 / OSC 777) —
                        // the channels Claude Code and friends ring when they
                        // need input.
                        unsigned long nseq = cmdblocks_notify_seq(cb);
                        if (nseq > 0) {
                            unsigned long nprev = pane_update_notify_seq(ss, nseq);
                            if (nprev != nseq) {
                                workspace_status_note_notify(&g_workspace_status, pane_id,
                                                             focused,
                                                             cmdblocks_notify_text(cb));
                                ui_workspace_rail_set_ring_pulse(&g_rail_view, 1.0f);
                                if (!window_focused_now) {
                                    char ws_label[128];
                                    const char *ctitle = cmdblocks_title(cb);
                                    if (ctitle && ctitle[0]) {
                                        snprintf(ws_label, sizeof(ws_label), "%s", ctitle);
                                    } else {
                                        const char *cwd = session_cwd(ss);
                                        if (cwd && cwd[0]) {
                                            workspace_cwd_label(cwd, getenv("HOME") ? getenv("HOME") : "",
                                                                ws_label, (int)sizeof(ws_label));
                                        } else {
                                            snprintf(ws_label, sizeof(ws_label), "shell");
                                        }
                                    }
                                    desktop_notify_agent_ring(ws_label,
                                                             cmdblocks_notify_text(cb));
                                }
                            }
                        }
                    }
                }

                workspace_status_prune(&g_workspace_status, live_ids, live_count);
                pane_prune_completion_seen(live_ids, live_count);
                scrollbar_state_prune();
                workspace_git_status_set_targets(g_git_status_sampler,
                                                 git_targets, git_target_count);
            }
        }
        if (any_pty_activity) last_activity_ms = now_ms;

        // Send focus in/out events when the window focus state changes,
        // but only if the application has enabled focus reporting
        // (DECSET 1004).  Sending CSI I / CSI O unconditionally would
        // inject unexpected escape sequences into shells that never
        // asked for them.
        bool focused = IsWindowFocused();
        if (focused != prev_focused) {
            if (focused) last_activity_ms = now_ms;   // focus-gained: wake up
            // Window gained focus: clear the active pane's attention since
            // the user can now see it directly.
            if (focused && app.n_tabs > 0 && app.active >= 0) {
                Tab *tab = &app.tabs[app.active];
                if (tab->focused && tab->focused->kind == PANE_LEAF) {
                    uint64_t id = pane_id_for_session(tab->focused->leaf.session);
                    workspace_status_clear(&g_workspace_status, id);
                    g_last_focused_pane_id = id;
                }
            }

            bool focus_mode = false;
            if (!child_exited
                && ghostty_terminal_mode_get(terminal,
                       GHOSTTY_MODE_FOCUS_EVENT, &focus_mode) == GHOSTTY_SUCCESS
                && focus_mode) {
                GhosttyFocusEvent focus_event = focused
                    ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST;
                char focus_buf[8];
                size_t focus_written = 0;
                GhosttyResult focus_res = ghostty_focus_encode(
                    focus_event, focus_buf, sizeof(focus_buf), &focus_written);
                if (focus_res == GHOSTTY_SUCCESS && focus_written > 0)
                    pty_write(pty_fd, focus_buf, focus_written);
            }
            prev_focused = focused;
        }

        // Reap the child if exited so the exit status is available for the
        // banner and the auto-close check below.
        if (!session_child_alive(active_session))
            session_reap(active_session);
        child_exit_status = session_exit_status(active_session);

        // A clean shell exit (the user typed `exit` or pressed Ctrl-D) should
        // close the window, just like any other terminal. Only an abnormal
        // exit — a non-zero status or a signal — keeps the window open with
        // the banner below so the user can scroll back and inspect output.
        if (!session_child_alive(active_session) && child_exit_status == 0)
            break;

        // Mouse coordinate math below targets whichever pane is focused, not
        // the union of every pane in the tab — otherwise a split's column/row
        // math (and therefore selection and pty mouse-forwarding) is computed
        // against the wrong origin/width once there's more than one pane
        // (§16.4). Falls back to the whole terminal area when the focused
        // leaf isn't in the collector (e.g. no pane tree yet).
        Rect focused_pane_rect = lo.terminal;
        if (app.n_tabs > 0) {
            PaneNode *focused_leaf = app.tabs[app.active].focused;
            for (int i = 0; i < active_pane_collector.count; i++) {
                if (active_pane_collector.entries[i].leaf == focused_leaf) {
                    focused_pane_rect.x = active_pane_collector.entries[i].x;
                    focused_pane_rect.y = active_pane_collector.entries[i].y;
                    focused_pane_rect.w = active_pane_collector.entries[i].w;
                    focused_pane_rect.h = active_pane_collector.entries[i].h;
                    break;
                }
            }
        }

        bool mouse_in_terminal =
            GetMouseX() >= focused_pane_rect.x
            && GetMouseX() < focused_pane_rect.x + focused_pane_rect.w
            && GetMouseY() >= focused_pane_rect.y
            && GetMouseY() < focused_pane_rect.y + focused_pane_rect.h;

        if (ui_sidebar_visible() && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && mouse_in_terminal) {
            ui_sidebar_focus(false);
        }

        // Handle scrollbar drag-to-scroll before mouse forwarding so
        // clicks on the scrollbar region don't leak into terminal apps
        // (e.g. vim, tmux) as spurious mouse events.
        bool scrollbar_consumed = false;
        if (!ui_settings_open() && !ui_palette_is_open()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            scrollbar_consumed = handle_scrollbar(terminal, render_state,
                                                   &scrollbar_dragging,
                                                   focused_pane_rect.x, focused_pane_rect.y,
                                                   focused_pane_rect.w, focused_pane_rect.h,
                                                   applied_scale);
        }

        // Host text selection (click-drag) when the app isn't grabbing the mouse
        // (or Shift is held to force it). Consumes the drag so it isn't also
        // forwarded to the pty as mouse events.
        bool mouse_tracking = false;
        ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);
        bool can_select = (!mouse_tracking || shift_down)
                          && !ui_settings_open() && !ui_inline_active()
                          && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu);
        bool selection_consumed = false;
        // Ctrl/Cmd+click on a URL opens it (handled before starting a selection).
        if ((ctrl_down || cmd_down) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && mouse_in_terminal && !ui_palette_is_open()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)) {
            int ucc = (GetMouseX() - focused_pane_rect.x - pad) / cell_width;
            int ucr = (GetMouseY() - focused_pane_rect.y - pad) / cell_height;
            Session *ref_session = NULL;
            if (app.n_tabs > 0) {
                PaneNode *fl = app.tabs[app.active].focused;
                if (fl && fl->kind == PANE_LEAF) ref_session = fl->leaf.session;
            }
            const char *ref_cwd = ref_session ? session_cwd(ref_session) : NULL;
            char url[2048];
            if (url_at(ucr, ucc, url, (int)sizeof(url))
                || file_ref_at(ucr, ucc, ref_cwd, url, (int)sizeof(url))) {
                open_url(url);
                selection_consumed = true;
            }
        }
        if (!selection_consumed && can_select && !scrollbar_consumed && mouse_in_terminal) {
            int cc = (GetMouseX() - focused_pane_rect.x - pad) / cell_width;
            int cr = (GetMouseY() - focused_pane_rect.y - pad) / cell_height;
            if (cc < 0) cc = 0;
            if (cr < 0) cr = 0;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                g_sel.sr = g_sel.er = cr; g_sel.sc = g_sel.ec = cc;
                g_sel.dragging = true; g_sel.active = true;
                selection_consumed = true;
            } else if (g_sel.dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                g_sel.er = cr; g_sel.ec = cc;
                selection_consumed = true;
            }
        }
        if (g_sel.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            g_sel.dragging = false;
            if (g_sel.sr == g_sel.er && g_sel.sc == g_sel.ec)
                g_sel.active = false;   // a plain click clears the selection
            else
                sel_copy_to_clipboard();  // copy-on-select, like most terminals
            selection_consumed = true;
        }

        // Forward keyboard/mouse input only while the child is alive and no UI
        // element is capturing keys. Sidebar visibility alone does not block.
        if (!ui_inline_active() && !ui_palette_is_open()
            && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
            && !inline_chord && !palette_chord_consumed && !clipboard_consumed
            && !g_search_active && !search_consumed && !block_nav_consumed
            && !zoom_consumed
            && !ask_last_command_consumed
            && ui_sidebar_allows_pty_input(child_exited, ui_settings_open(),
                lo.sidebar_visible, ui_sidebar_focused(),
                settings_shortcut_consumed, sidebar_chord_consumed)) {
            handle_input(pty_fd, key_encoder, key_event, terminal);
            if (!scrollbar_consumed && !selection_consumed && mouse_in_terminal)
                handle_mouse(pty_fd, mouse_encoder, mouse_event, terminal,
                             cell_width, cell_height, pad,
                             focused_pane_rect.x, focused_pane_rect.y,
                             focused_pane_rect.w, focused_pane_rect.h);
        }

        // Apply the color theme when it changes (e.g. on Save). Setting the
        // palette/default colors is a mutating call, so do it once per change.
        // Apply to EVERY session in EVERY tab — not just the active pane — so
        // background panes/tabs don't keep a stale palette (§16 × E3).
        if (strcmp(cfg.theme, applied_theme) != 0) {
            Theme th = theme_resolve(cfg.theme);
            for (int ti = 0; ti < app.n_tabs; ti++) {
                PaneNode *tl[64];
                int tln = 0;
                pane_collect_leaves(app.tabs[ti].root, tl, 64, &tln);
                for (int li = 0; li < tln; li++) {
                    TermEngine *lte = (TermEngine *)session_engine(tl[li]->leaf.session);
                    if (lte) term_engine_apply_theme(lte, &th);
                }
            }
            apply_gui_style(&th);
            ui_theme_derive(&th);      // -> also updates g_ui_theme
            snprintf(applied_theme, sizeof(applied_theme), "%s", cfg.theme);
        }

        // Snapshot the terminal state into the render state for every session in
        // the active tab.  Each leaf needs its own snapshots before we draw,
        // otherwise stale render states produce visual flicker / blank panes (§16.4).
        {
            Tab *rtab = &app.tabs[app.active];
            PaneNode *rleaves[64];
            int rn_leaves = 0;
            pane_collect_leaves(rtab->root, rleaves, 64, &rn_leaves);
            for (int li = 0; li < rn_leaves; li++) {
                Session *ls = rleaves[li]->leaf.session;
                if (ls) term_engine_update((TermEngine *)session_engine(ls));
            }
        }

        // Drain any AI tokens the worker thread produced since last frame and
        // append them to the streaming assistant message. All on the main
        // thread — the worker only ever touches its own mutex-guarded buffer.
        if (active_stream) {
            char delta[2048];
            bool is_reason = false, stream_done = false, stream_ok = false;
            while (ai_stream_poll(active_stream, delta, (int)sizeof(delta),
                                  &is_reason, &stream_done, &stream_ok) > 0)
                ui_sidebar_append_assistant(delta, is_reason);
            if (stream_done) {
                ui_sidebar_end_assistant();
                if (!stream_ok)
                    ui_sidebar_push(MSG_SYSTEM, ai_stream_error(active_stream));
                ai_stream_free(active_stream);
                active_stream = NULL;
            }
        }

        // Drain the inline (Ctrl+Space) request: accumulate the answer, then on
        // completion stage the sanitised single command at the prompt.
        if (inline_stream) {
            char delta[1024];
            bool ir = false, idone = false, iok = false;
            while (ai_stream_poll(inline_stream, delta, (int)sizeof(delta),
                                  &ir, &idone, &iok) > 0) {
                if (!ir) {   // answer only; inline ignores reasoning
                    size_t cur = strlen(inline_answer);
                    snprintf(inline_answer + cur, sizeof(inline_answer) - cur, "%s", delta);
                }
            }
            if (idone) {
                if (iok) {
                    char cmd[1024];
                    if (inline_sanitize_command(inline_answer, cmd, (int)sizeof(cmd)))
                        pty_write(pty_fd, cmd, strlen(cmd));   // staged, NO newline
                    ui_inline_cancel();
                } else {
                    ui_inline_set_error(ai_stream_error(inline_stream));
                }
                ai_stream_free(inline_stream);
                inline_stream = NULL;
            }
        }

        // Drain remote API requests (if any). Each line is a JSON-RPC-style
        // request; dispatch against the active runtime handles.
        if (g_remote_api) {
            char *lines[16];
            int nlines = remote_api_poll(g_remote_api, lines, 16);
            for (int ri = 0; ri < nlines; ri++) {
                dispatch_remote_line(lines[ri], te, pty_fd,
                                     &cfg, config_path,
                                     (uint16_t)term_cols, (uint16_t)term_rows,
                                     cell_width, cell_height);
            }
        }

        Theme theme = theme_resolve(cfg.theme);
        Color win_bg = { theme.bg.r, theme.bg.g, theme.bg.b, 255 };

        // Query scrollbar state for the renderer.
        GhosttyTerminalScrollbar scrollbar = {0};
        GhosttyTerminalScrollbar *scrollbar_ptr = NULL;
        if (ghostty_terminal_get(terminal, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                                 &scrollbar) == GHOSTTY_SUCCESS)
            scrollbar_ptr = &scrollbar;

        // ----------------------------------------------------------------
        // Workspace rail resize-handle drag. Runs before the drag-to-reorder
        // and click blocks below and sets g_rail_resizing so they never also
        // interpret the same press as a tab switch. Dragging the handle past
        // WORKSPACE_RAIL_HIDE_THRESHOLD and releasing soft-collapses the rail
        // to a thin WORKSPACE_RAIL_COLLAPSED_WIDTH sliver — never a literal
        // zero-width/hidden rail — so the handle is always still there to
        // drag back open (VS Code/cmux-style sidebar collapse).
        // ----------------------------------------------------------------
        if (lo.rail_visible && !ui_menu_active(&g_rail_menu) && g_drag_from < 0
            && !g_rail_resizing) {
            int handle_x = lo.rail.x + lo.rail.w;
            bool over_handle = GetMouseX() >= handle_x - RAIL_RESIZE_HANDLE_PAD
                             && GetMouseX() <= handle_x + RAIL_RESIZE_HANDLE_PAD
                             && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h;
            if (over_handle && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                g_rail_resizing = true;
                g_rail_drag_width = lo.rail.w;
                g_rail_was_collapsed_on_press = g_rail_collapsed;
                g_rail_resize_press_x = GetMouseX();
            }
        }

        if (g_rail_resizing) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                int candidate = GetMouseX() - lo.rail.x;
                if (candidate < WORKSPACE_RAIL_COLLAPSED_WIDTH) candidate = WORKSPACE_RAIL_COLLAPSED_WIDTH;
                if (candidate > WORKSPACE_RAIL_MAX_WIDTH) candidate = WORKSPACE_RAIL_MAX_WIDTH;
                if (candidate != g_rail_drag_width) {
                    g_rail_drag_width = candidate;
                    // Note: no prev_term_area_w reset here — the terminal
                    // grid/pty resize is gated on !g_rail_resizing above and
                    // only fires once, after the drag commits below.
                }
            } else {
                // Left button released (or its state was otherwise lost, e.g.
                // focus loss mid-drag) — commit the resize or soft-collapse.
                bool no_real_drag = abs(GetMouseX() - g_rail_resize_press_x)
                                   < RAIL_RESIZE_CLICK_SLOP_PX;
                if (g_rail_was_collapsed_on_press && no_real_drag) {
                    // Plain click on the collapsed sliver: reopen instead of
                    // re-collapsing (which was otherwise a silent no-op).
                    g_rail_collapsed = false;
                } else if (g_rail_drag_width < WORKSPACE_RAIL_HIDE_THRESHOLD) {
                    g_rail_collapsed = true;
                } else {
                    int committed = g_rail_drag_width;
                    if (committed < WORKSPACE_RAIL_MIN_WIDTH) committed = WORKSPACE_RAIL_MIN_WIDTH;
                    if (committed > WORKSPACE_RAIL_MAX_WIDTH) committed = WORKSPACE_RAIL_MAX_WIDTH;
                    cfg.workspace_rail_width = committed;
                    g_rail_collapsed = false;
                    config_save(&cfg, config_path);
                }
                g_rail_resizing = false;
                prev_term_area_w = -1;
                drain_char_queue();
            }
        }

        // ----------------------------------------------------------------
        // Drag-to-reorder tracking (runs every frame the rail is visible).
        // Monitors left-press on tab rows, vertical movement to enter drag
        // mode, and release to complete the reorder.  The actual drag-state
        // globals are propagated into the rail view for drawing below.
        // ----------------------------------------------------------------
        if (lo.rail_visible && !ui_menu_active(&g_rail_menu) && !g_rail_resizing) {
            int rail_mx = GetMouseX();
            int rail_my = GetMouseY();
            bool in_rail = (rail_mx >= lo.rail.x && rail_mx < lo.rail.x + lo.rail.w
                            && rail_my >= lo.rail.y && rail_my < lo.rail.y + lo.rail.h);

            // Left-press inside the rail: check for tab-row press to start
            // candidate tracking.  We build the model once here to resolve
            // geometry; if the hit is not a tab switch we clear the candidate.
            if (in_rail && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_drag_from < 0) {
                build_rail_view(&lo, now_ms, font_size);
                WorkspaceRailAction act = workspace_rail_hit(&g_rail_view,
                                                             rail_mx, rail_my);
                if (act.type == WORKSPACE_RAIL_ACTION_SWITCH_TAB
                    && act.index >= 0 && act.index < app.n_tabs) {
                    g_drag_candidate = act.index;
                    g_drag_press_y = rail_my;
                } else {
                    g_drag_candidate = -1;
                }
            }

            // While the left button is held and we have a candidate, check
            // for vertical movement past the 6 px threshold to enter drag.
            if (g_drag_candidate >= 0 && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                if (g_drag_from < 0 && abs(rail_my - g_drag_press_y) >= 6) {
                    g_drag_from = g_drag_candidate;
                }
                if (g_drag_from >= 0) {
                    // Rebuild layout each frame to keep drop index accurate.
                    build_rail_view(&lo, now_ms, font_size);
                    g_drag_slot = workspace_rail_drop_index(&g_rail_view, rail_my, font_size);
                }
            }

            // Release while dragging: reorder tabs.
            if (g_drag_from >= 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                if (g_drag_slot >= 0 && g_drag_slot != g_drag_from
                    && g_drag_slot != g_drag_from + 1) {
                    Tab dragged = app.tabs[g_drag_from];
                    int dest = g_drag_slot;
                    // Adjust destination if it shifted due to removal.
                    if (dest > g_drag_from) dest--;
                    int src = g_drag_from;
                    if (dest < src) {
                        memmove(&app.tabs[dest + 1], &app.tabs[dest],
                                (size_t)(src - dest) * sizeof(Tab));
                    } else if (dest > src) {
                        memmove(&app.tabs[src], &app.tabs[src + 1],
                                (size_t)(dest - src) * sizeof(Tab));
                    }
                    app.tabs[dest] = dragged;
                    // Update app.active to follow the moved tab.
                    if (app.active == g_drag_from)
                        app.active = dest;
                    else if (app.active > g_drag_from && app.active <= dest)
                        app.active--;
                    else if (app.active < g_drag_from && app.active >= dest)
                        app.active++;
                    // Sync runtime for the new active tab.
                    sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                            &terminal, &render_state, &row_iter,
                                            &row_cells, &placement_iter,
                                            &key_encoder, &key_event,
                                            &mouse_encoder, &mouse_event);
                    prev_term_area_w = -1;
                    drain_char_queue();
                    g_session_dirty = true;
                }
                g_drag_from = -1;
                g_drag_slot = -1;
                g_drag_candidate = -1;
            }

            // Cancel drag on any other mouse button press.
            if (g_drag_from >= 0
                && (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
                    || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))) {
                g_drag_from = -1;
                g_drag_slot = -1;
                g_drag_candidate = -1;
            }

            // Clear candidate if mouse button released before drag activated.
            if (g_drag_candidate >= 0 && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                g_drag_candidate = -1;
            }
        }

        // ----------------------------------------------------------------
        // Workspace rail clicks (before BeginDrawing so state changes like
        // tab switches apply before this frame's draw). Uses the same
        // build/layout/hit model as drawing, so click targets can't drift
        // from what's on screen. Clicks inside the rail never reach the
        // terminal: pane rects start to the right of the rail.
        // ----------------------------------------------------------------
        if (lo.rail_visible && g_drag_from < 0 && !g_rail_resizing
            && !ui_menu_active(&g_rail_menu)
            && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && GetMouseX() >= lo.rail.x && GetMouseX() < lo.rail.x + lo.rail.w
            && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h) {
            build_rail_view(&lo, now_ms, font_size);
            WorkspaceRailAction act = workspace_rail_hit(&g_rail_view,
                                                         GetMouseX(), GetMouseY());

            switch (act.type) {
            case WORKSPACE_RAIL_ACTION_SWITCH_TAB:
                if (act.index >= 0 && act.index < app.n_tabs
                    && act.index != app.active) {
                    app_switch_tab(act.index, &te, &pty_fd, &child, &child_exited);
                    sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                            &terminal, &render_state, &row_iter,
                                            &row_cells, &placement_iter,
                                            &key_encoder, &key_event,
                                            &mouse_encoder, &mouse_event);
                    prev_term_area_w = -1;
                    drain_char_queue();
                }
                break;
            case WORKSPACE_RAIL_ACTION_FOCUS_PANE: {
                Tab *atab = &app.tabs[app.active];
                if (atab->root && act.pane_id != 0) {
                    PaneNode *aleaves[WORKSPACE_RAIL_MAX_PANES];
                    int anl = 0;
                    pane_collect_leaves(atab->root, aleaves,
                                        WORKSPACE_RAIL_MAX_PANES, &anl);
                    for (int pj = 0; pj < anl; pj++) {
                        if (aleaves[pj]->kind != PANE_LEAF)
                            continue;
                        if (pane_id_for_session(aleaves[pj]->leaf.session) != act.pane_id)
                            continue;
                        if (aleaves[pj] != atab->focused) {
                            atab->focused = aleaves[pj];
                            sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                    &terminal, &render_state, &row_iter,
                                                    &row_cells, &placement_iter,
                                                    &key_encoder, &key_event,
                                                    &mouse_encoder, &mouse_event);
                            drain_char_queue();
                        }
                        break;
                    }
                }
                break;
            }
            case WORKSPACE_RAIL_ACTION_NEW_TAB: {
                // Option/Alt-click → worktree workspace; normal click → same-dir.
                bool alt_held = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
                if (alt_held) {
                    if (app.n_tabs >= FANGS_MAX_TABS) {
                        toast_push(TOAST_WARN, "Maximum workspaces reached");
                        break;
                    }
                    Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                           &terminal, &render_state, &row_iter,
                                                           &row_cells, &placement_iter,
                                                           &key_encoder, &key_event,
                                                           &mouse_encoder, &mouse_event);
                    if (!cur) break;
                    WorkspaceWorktreeResult wtr;
                    memset(&wtr, 0, sizeof(wtr));
                    if (workspace_worktree_create(session_cwd(cur), &wtr)) {
                        Session *ns = app_add_tab_named(term_cols, term_rows,
                                                        cell_width, cell_height,
                                                        cfg.scrollback, wtr.path,
                                                        wtr.branch,
                                                        cfg.kitty_images,
                                                        cfg.kitty_image_storage_mb,
                                                        &te, &pty_fd, &child, &child_exited);
                        if (!ns) {
                            workspace_worktree_remove_created(&wtr);
                            toast_push(TOAST_WARN, "Failed to open workspace for worktree");
                        } else {
                            toast_push(TOAST_INFO, "Created worktree");
                            maybe_auto_launch(ns, &cfg);
                        }
                    } else {
                        toast_push(TOAST_WARN, wtr.error);
                    }
                } else {
                    Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                           &terminal, &render_state, &row_iter,
                                                           &row_cells, &placement_iter,
                                                           &key_encoder, &key_event,
                                                           &mouse_encoder, &mouse_event);
                    app_add_tab(term_cols, term_rows, cell_width, cell_height,
                                cfg.scrollback, session_cwd(cur),
                                cfg.kitty_images, cfg.kitty_image_storage_mb,
                                &te, &pty_fd, &child, &child_exited);
                }
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
                break;
            }
            case WORKSPACE_RAIL_ACTION_OPEN_PORT: {
                char url[64];
                snprintf(url, sizeof(url), "http://localhost:%d", act.port);
                open_url(url);
                break;
            }
            case WORKSPACE_RAIL_ACTION_JUMP_ATTENTION:
                g_jump_request = act.pane_id;
                break;
            case WORKSPACE_RAIL_ACTION_VIEW_DIFF: {
                Session *rep = session_for_pane_id(act.pane_id);
                perform_view_changes(rep ? session_cwd(rep) : NULL);
                break;
            }
            case WORKSPACE_RAIL_ACTION_HISTORY: {
                // Snapshot events for the popover before opening.
                g_history_event_count = workspace_status_events(
                    &g_workspace_status, g_history_event_cache,
                    HISTORY_EVENT_CACHE);
                g_history_last_seen = g_workspace_status.event_count;

                UiMenuItem hitems[HISTORY_EVENT_CACHE + 2];
                memset(hitems, 0, sizeof(hitems));
                int hc = 0;
                // Leave room for the separator + "Clear history" items.
                for (int hi = 0; hi < g_history_event_count
                     && hc < UI_MENU_MAX_ITEMS - 2; hi++) {
                    WorkspaceStatusEvent *ev = &g_history_event_cache[hi];
                    snprintf(hitems[hc].label, sizeof(hitems[hc].label),
                             "%s", ev->text);
                    // Label with workspace prefix for context: find the tab
                    // that contains the ringing pane (it may not be the tab's
                    // current attention representative).
                    for (int j = 0; j < app.n_tabs; j++) {
                        if (!tab_contains_pane(&app.tabs[j], ev->pane_id))
                            continue;
                        const char *lbl = app.tabs[j].name[0]
                            ? app.tabs[j].name : g_rail_inputs.tab_labels[j];
                        snprintf(hitems[hc].label, sizeof(hitems[hc].label),
                                 "%s: %s", lbl, ev->text);
                        break;
                    }
                    hitems[hc].tag = 200 + hi;   // event index
                    hitems[hc].tint = (ev->level == WORKSPACE_ATTENTION_ERROR)
                        ? UI_COLOR_INLINE_ERROR
                        : (ev->level == WORKSPACE_ATTENTION_WARN)
                            ? UI_COLOR_TEXT
                            : UI_COLOR_SUBTITLE;
                    hitems[hc].separator = false;
                    hc++;
                }
                if (hc > 0) {
                    hitems[hc].separator = true;
                    hitems[hc].tag = -1;
                    hc++;
                }
                snprintf(hitems[hc].label, sizeof(hitems[hc].label),
                         "Clear history");
                hitems[hc].tag = RAIL_MENU_HISTORY_CLEAR;
                hitems[hc].tint = UI_COLOR_INLINE_ERROR;
                hitems[hc].separator = false;
                hc++;

                int bell_x = GetMouseX();
                int bell_y = GetMouseY();
                ui_menu_open(&g_rail_menu, hitems, hc, bell_x, bell_y);
                ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
                break;
            }
            case WORKSPACE_RAIL_ACTION_COLLAPSE_RAIL:
                g_rail_collapsed = true;
                prev_term_area_w = -1;
                drain_char_queue();
                break;
            case WORKSPACE_RAIL_ACTION_SPLIT_RIGHT:
            case WORKSPACE_RAIL_ACTION_SPLIT_DOWN: {
                Session *cur = sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                                       &terminal, &render_state, &row_iter,
                                                       &row_cells, &placement_iter,
                                                       &key_encoder, &key_event,
                                                       &mouse_encoder, &mouse_event);
                app_split_focused(act.type == WORKSPACE_RAIL_ACTION_SPLIT_DOWN
                                  ? PANE_VSPLIT : PANE_HSPLIT,
                                  term_cols, term_rows, cell_width, cell_height,
                                  cfg.scrollback, session_cwd(cur),
                                  cfg.kitty_images, cfg.kitty_image_storage_mb,
                                  &te, &pty_fd, &child, &child_exited);
                sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                        &terminal, &render_state, &row_iter,
                                        &row_cells, &placement_iter,
                                        &key_encoder, &key_event,
                                        &mouse_encoder, &mouse_event);
                prev_term_area_w = -1;
                drain_char_queue();
                break;
            }
            case WORKSPACE_RAIL_ACTION_NONE:
            default:
                break;
            }
        }

        // ----------------------------------------------------------------
        // Right-click context menu on the workspace rail.
        // Opens a popover with actions for the clicked row.
        // ----------------------------------------------------------------
        if (lo.rail_visible && g_drag_from < 0 && !g_rail_resizing
            && !ui_menu_active(&g_rail_menu)
            && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
            && GetMouseX() >= lo.rail.x && GetMouseX() < lo.rail.x + lo.rail.w
            && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h) {
            build_rail_view(&lo, now_ms, font_size);

            // Find which row was clicked.
            int hit_tab = -1;
            int hit_pane = -1;
            int mx = GetMouseX(), my = GetMouseY();
            for (int i = 0; i < g_rail_view.tab_count; i++) {
                if (my >= g_rail_view.tabs[i].y
                    && my < g_rail_view.tabs[i].y + g_rail_view.tabs[i].h)
                    { hit_tab = i; break; }
            }
            if (hit_tab < 0) {
                for (int i = 0; i < g_rail_view.pane_count; i++) {
                    if (my >= g_rail_view.panes[i].y
                        && my < g_rail_view.panes[i].y + g_rail_view.panes[i].h)
                        { hit_pane = i; break; }
                }
            }

            UiMenuItem mitems[8];
            memset(mitems, 0, sizeof(mitems));
            int mc = 0;
            if (hit_tab >= 0) {
                g_rail_context_tab = hit_tab;
                g_rail_context_is_pane = false;
                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Rename...");
                mitems[mc].tag = RAIL_MENU_RENAME;
                mitems[mc].tint = UI_COLOR_TEXT;
                mitems[mc].separator = false;
                mc++;
                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "New Worktree Here");
                mitems[mc].tag = RAIL_MENU_WORKTREE;
                mitems[mc].tint = UI_COLOR_TEXT;
                mitems[mc].separator = false;
                mc++;

                // PR/review handoff -- only offered when the row's cwd is
                // inside a git repo with a resolvable base branch.
                g_pr_repo_root[0] = '\0';
                g_pr_base_branch[0] = '\0';
                Tab *ct = &app.tabs[hit_tab];
                Session *rep = ct->focused
                    ? ct->focused->leaf.session
                    : tab_first_leaf_session(ct);
                const char *cwd = rep ? session_cwd(rep) : NULL;
                if (cwd && cwd[0]
                    && workspace_worktree_repo_root(cwd, g_pr_repo_root, sizeof(g_pr_repo_root))
                    && workspace_worktree_default_branch(g_pr_repo_root, g_pr_base_branch, sizeof(g_pr_base_branch))) {
                    snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Create Pull Request");
                    mitems[mc].tag = RAIL_MENU_PR_CREATE;
                    mitems[mc].tint = UI_COLOR_TEXT;
                    mitems[mc].separator = false;
                    mc++;
                    snprintf(mitems[mc].label, sizeof(mitems[mc].label),
                             "View Diff vs %s", g_pr_base_branch);
                    mitems[mc].tag = RAIL_MENU_PR_DIFF;
                    mitems[mc].tint = UI_COLOR_TEXT;
                    mitems[mc].separator = false;
                    mc++;
                }

                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Color...");
                mitems[mc].tag = RAIL_MENU_COLOR;
                mitems[mc].tint = UI_COLOR_TEXT;
                mitems[mc].separator = false;
                mc++;

                if (g_rail_view.tabs[hit_tab].git_changed_count > 0) {
                    snprintf(mitems[mc].label, sizeof(mitems[mc].label), "View Changes...");
                    mitems[mc].tag = RAIL_MENU_DIFF;
                    mitems[mc].tint = UI_COLOR_TEXT;
                    mitems[mc].separator = false;
                    mc++;
                }

                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Close Workspace");
                mitems[mc].tag = RAIL_MENU_CLOSE;
                mitems[mc].tint = UI_COLOR_INLINE_ERROR;
                mitems[mc].separator = false;
                mc++;
            } else if (hit_pane >= 0) {
                g_rail_context_pane = g_rail_view.panes[hit_pane].id;
                g_rail_context_is_pane = true;
                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Focus");
                mitems[mc].tag = RAIL_MENU_FOCUS;
                mitems[mc].tint = UI_COLOR_TEXT;
                mitems[mc].separator = false;
                mc++;
                if (g_rail_view.panes[hit_pane].git_changed_count > 0) {
                    snprintf(mitems[mc].label, sizeof(mitems[mc].label), "View Changes...");
                    mitems[mc].tag = RAIL_MENU_DIFF;
                    mitems[mc].tint = UI_COLOR_TEXT;
                    mitems[mc].separator = false;
                    mc++;
                }
                snprintf(mitems[mc].label, sizeof(mitems[mc].label), "Close Pane");
                mitems[mc].tag = RAIL_MENU_CLOSE;
                mitems[mc].tint = UI_COLOR_INLINE_ERROR;
                mitems[mc].separator = false;
                mc++;
            }
            if (mc > 0) {
                ui_menu_open(&g_rail_menu, mitems, mc, mx, my);
                ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
            }
        }

        // ----------------------------------------------------------------
        // Middle-click armed close on the workspace rail.
        // First middle-click on a tab row arms it for 2 s; a second
        // middle-click within the window closes the tab.
        // ----------------------------------------------------------------
        if (lo.rail_visible && g_drag_from < 0 && !g_rail_resizing
            && !ui_menu_active(&g_rail_menu)
            && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)
            && GetMouseX() >= lo.rail.x && GetMouseX() < lo.rail.x + lo.rail.w
            && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h) {
            build_rail_view(&lo, now_ms, font_size);

            // Find the clicked tab row.
            int mmx = GetMouseX(), mmy = GetMouseY();
            int clicked_tab = -1;
            for (int i = 0; i < g_rail_view.tab_count; i++) {
                if (mmy >= g_rail_view.tabs[i].y
                    && mmy < g_rail_view.tabs[i].y + g_rail_view.tabs[i].h)
                    { clicked_tab = i; break; }
            }

            if (clicked_tab >= 0 && clicked_tab < app.n_tabs) {
                uint64_t tid = tab_stable_id(&app.tabs[clicked_tab]);
                if (tid != 0 && g_armed_pane_id == tid
                    && g_armed_deadline_ms > now_ms) {
                    // Already armed — close it.
                    app_close_tab(clicked_tab);
                    disarm_armed_close();
                    sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                            &terminal, &render_state, &row_iter,
                                            &row_cells, &placement_iter,
                                            &key_encoder, &key_event,
                                            &mouse_encoder, &mouse_event);
                    prev_term_area_w = -1;
                    drain_char_queue();
                    toast_push(TOAST_INFO, "Workspace closed");
                } else {
                    // First middle-click: arm.
                    g_armed_pane_id = tid;
                    g_armed_deadline_ms = now_ms + 2000;
                }
            }
        }

        // Disarm armed-close on any click outside the rail.
        if (g_armed_pane_id != 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
            && !(GetMouseX() >= lo.rail.x && GetMouseX() < lo.rail.x + lo.rail.w
                 && GetMouseY() >= lo.rail.y && GetMouseY() < lo.rail.y + lo.rail.h)) {
            disarm_armed_close();
        }

        // ----------------------------------------------------------------
        // Menu action execution: process a click on the context menu or
        // history popover.  Handles all RAIL_MENU_* and history-event tags.
        // ----------------------------------------------------------------
        if (g_rail_menu.open) {
            // Esc closes the menu (handled here since the menu has no
            // dedicated draw handler for key events in isolation).
            if (IsKeyPressed(KEY_ESCAPE)) {
                ui_menu_close(&g_rail_menu);
            }

            // Left-click on an item.
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int hit = ui_menu_hit(&g_rail_menu, GetMouseX(), GetMouseY());
                if (hit < 0) {
                    // Click outside the menu closes it.
                    ui_menu_close(&g_rail_menu);
                } else {
                    int tag = g_rail_menu.items[hit].tag;
                    ui_menu_close(&g_rail_menu);

                    switch (tag) {
                    case RAIL_MENU_RENAME:
                        if (g_rail_context_tab >= 0
                            && g_rail_context_tab < app.n_tabs) {
                            ui_rename_prompt_open(
                                g_rail_context_tab,
                                app.tabs[g_rail_context_tab].name);
                        }
                        break;
                    case RAIL_MENU_WORKTREE:
                        if (g_rail_context_tab >= 0
                            && g_rail_context_tab < app.n_tabs
                            && app.n_tabs < FANGS_MAX_TABS) {
                            Tab *ct = &app.tabs[g_rail_context_tab];
                            Session *rep = ct->focused
                                ? ct->focused->leaf.session
                                : tab_first_leaf_session(ct);
                            const char *cwd = rep ? session_cwd(rep) : NULL;
                            if (cwd && cwd[0]) {
                                WorkspaceWorktreeResult wtr;
                                memset(&wtr, 0, sizeof(wtr));
                                if (workspace_worktree_create(cwd, &wtr)) {
                                    Session *ns = app_add_tab_named(
                                        term_cols, term_rows,
                                        cell_width, cell_height,
                                        cfg.scrollback, wtr.path,
                                        wtr.branch,
                                        cfg.kitty_images,
                                        cfg.kitty_image_storage_mb,
                                        &te, &pty_fd, &child, &child_exited);
                                    if (!ns) {
                                        workspace_worktree_remove_created(&wtr);
                                        toast_push(TOAST_WARN,
                                            "Failed to open workspace for worktree");
                                    } else {
                                        toast_push(TOAST_INFO,
                                            "Created worktree");
                                        maybe_auto_launch(ns, &cfg);
                                    }
                                } else {
                                    toast_push(TOAST_WARN, wtr.error);
                                }
                                sync_runtime_for_action(
                                    &te, &pty_fd, &child, &child_exited,
                                    &terminal, &render_state, &row_iter,
                                    &row_cells, &placement_iter,
                                    &key_encoder, &key_event,
                                    &mouse_encoder, &mouse_event);
                                prev_term_area_w = -1;
                                drain_char_queue();
                            }
                        }
                        break;
                    case RAIL_MENU_PR_CREATE:
                    case RAIL_MENU_PR_DIFF: {
                        if (g_rail_context_tab >= 0
                            && g_rail_context_tab < app.n_tabs
                            && g_pr_base_branch[0]) {
                            Session *target = app_switch_tab(
                                g_rail_context_tab, &te, &pty_fd,
                                &child, &child_exited);
                            if (target) {
                                char cmd[160];
                                if (tag == RAIL_MENU_PR_CREATE) {
                                    snprintf(cmd, sizeof(cmd),
                                            "gh pr create --fill --base %s",
                                            g_pr_base_branch);
                                } else {
                                    snprintf(cmd, sizeof(cmd),
                                            "git diff %s...HEAD",
                                            g_pr_base_branch);
                                }
                                pty_write(pty_fd, cmd, strlen(cmd));
                                toast_push(TOAST_INFO, tag == RAIL_MENU_PR_CREATE
                                    ? "Staged gh pr create -- press Enter to run"
                                    : "Staged git diff -- press Enter to run");
                                drain_char_queue();
                            }
                            prev_term_area_w = -1;
                        }
                        break;
                    }
                    case RAIL_MENU_CLOSE:
                        if (!g_rail_context_is_pane) {
                            if (g_rail_context_tab >= 0
                                && g_rail_context_tab < app.n_tabs) {
                                app_close_tab(g_rail_context_tab);
                                sync_runtime_for_action(
                                    &te, &pty_fd, &child, &child_exited,
                                    &terminal, &render_state, &row_iter,
                                    &row_cells, &placement_iter,
                                    &key_encoder, &key_event,
                                    &mouse_encoder, &mouse_event);
                                prev_term_area_w = -1;
                                drain_char_queue();
                            }
                        } else {
                            if (g_rail_context_pane != 0) {
                                app_close_pane(g_rail_context_pane);
                                sync_runtime_for_action(
                                    &te, &pty_fd, &child, &child_exited,
                                    &terminal, &render_state, &row_iter,
                                    &row_cells, &placement_iter,
                                    &key_encoder, &key_event,
                                    &mouse_encoder, &mouse_event);
                                prev_term_area_w = -1;
                                drain_char_queue();
                            }
                        }
                        break;
                    case RAIL_MENU_FOCUS: {
                        Tab *atab = &app.tabs[app.active];
                        if (atab->root && g_rail_context_pane != 0) {
                            PaneNode *aleaves[WORKSPACE_RAIL_MAX_PANES];
                            int anl = 0;
                            pane_collect_leaves(atab->root, aleaves,
                                                WORKSPACE_RAIL_MAX_PANES, &anl);
                            for (int pj = 0; pj < anl; pj++) {
                                if (aleaves[pj]->kind != PANE_LEAF) continue;
                                if (pane_id_for_session(aleaves[pj]->leaf.session)
                                    != g_rail_context_pane) continue;
                                atab->focused = aleaves[pj];
                                sync_runtime_for_action(
                                    &te, &pty_fd, &child, &child_exited,
                                    &terminal, &render_state, &row_iter,
                                    &row_cells, &placement_iter,
                                    &key_encoder, &key_event,
                                    &mouse_encoder, &mouse_event);
                                drain_char_queue();
                                break;
                            }
                        }
                        break;
                    }
                    case RAIL_MENU_HISTORY_CLEAR:
                        workspace_status_events_clear(&g_workspace_status);
                        g_history_last_seen = 0;
                        break;
                    case RAIL_MENU_CLEANUP_CONFIRM: {
                        int removed = 0;
                        bool closed_a_tab = false;
                        const char *exclude[OPEN_WORKTREE_EXCLUDE_MAX];
                        int exclude_count = collect_open_session_cwds(
                            exclude, OPEN_WORKTREE_EXCLUDE_MAX);
                        for (int i = 0; i < g_cleanup_candidate_count; i++) {
                            if (!workspace_worktree_cleanup(g_cleanup_repo_root,
                                                            exclude,
                                                            exclude_count,
                                                            &g_cleanup_candidates[i]))
                                continue;
                            removed++;
                            // Defensive: close any tab still pointing at the
                            // removed path. Shouldn't happen -- the scan
                            // excluded every open tab's cwd -- but never
                            // leave a tab pointing at a deleted directory.
                            for (int ti = app.n_tabs - 1; ti >= 0; ti--) {
                                Tab *tt = &app.tabs[ti];
                                Session *rep = tt->focused
                                    ? tt->focused->leaf.session
                                    : tab_first_leaf_session(tt);
                                const char *cwd = rep ? session_cwd(rep) : NULL;
                                if (cwd && strcmp(cwd, g_cleanup_candidates[i].path) == 0) {
                                    app_close_tab(ti);
                                    closed_a_tab = true;
                                    break;
                                }
                            }
                        }
                        g_cleanup_candidate_count = 0;
                        if (closed_a_tab) {
                            sync_runtime_for_action(
                                &te, &pty_fd, &child, &child_exited,
                                &terminal, &render_state, &row_iter,
                                &row_cells, &placement_iter,
                                &key_encoder, &key_event,
                                &mouse_encoder, &mouse_event);
                            prev_term_area_w = -1;
                            drain_char_queue();
                        }
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Removed %d worktree%s",
                                removed, removed == 1 ? "" : "s");
                        toast_push(removed > 0 ? TOAST_INFO : TOAST_WARN, msg);
                        break;
                    }
                    case RAIL_MENU_COLOR: {
                        if (g_rail_context_tab < 0 || g_rail_context_tab >= app.n_tabs)
                            break;
                        UiMenuItem citems[WORKSPACE_RAIL_COLOR_TAG_COUNT + 1];
                        memset(citems, 0, sizeof(citems));
                        snprintf(citems[0].label, sizeof(citems[0].label), "None");
                        citems[0].tag = RAIL_MENU_COLOR_BASE;
                        citems[0].tint = UI_COLOR_SUBTITLE;
                        for (int ci = 0; ci < WORKSPACE_RAIL_COLOR_TAG_COUNT; ci++) {
                            snprintf(citems[ci + 1].label, sizeof(citems[ci + 1].label),
                                    "%s", WORKSPACE_RAIL_COLOR_TAG_NAMES[ci]);
                            citems[ci + 1].tag = RAIL_MENU_COLOR_BASE + ci + 1;
                            citems[ci + 1].tint = WORKSPACE_RAIL_COLOR_TAG_COLORS[ci];
                        }
                        ui_menu_open(&g_rail_menu, citems, WORKSPACE_RAIL_COLOR_TAG_COUNT + 1,
                                    GetMouseX(), GetMouseY());
                        ui_menu_layout(&g_rail_menu, GetScreenWidth(), GetScreenHeight());
                        break;
                    }
                    case RAIL_MENU_DIFF: {
                        Session *rep = g_rail_context_is_pane
                            ? session_for_pane_id(g_rail_context_pane)
                            : (g_rail_context_tab >= 0 && g_rail_context_tab < app.n_tabs
                               ? tab_first_leaf_session(&app.tabs[g_rail_context_tab])
                               : NULL);
                        perform_view_changes(rep ? session_cwd(rep) : NULL);
                        break;
                    }
                    default:
                        // Color-tag submenu choice (tag = RAIL_MENU_COLOR_BASE
                        // + choice; 0 = None, 1..COUNT = palette index).
                        if (tag >= RAIL_MENU_COLOR_BASE
                            && tag <= RAIL_MENU_COLOR_BASE + WORKSPACE_RAIL_COLOR_TAG_COUNT
                            && g_rail_context_tab >= 0 && g_rail_context_tab < app.n_tabs) {
                            app.tabs[g_rail_context_tab].color_tag = tag - RAIL_MENU_COLOR_BASE;
                            g_session_dirty = true;
                        }
                        // History event jump (tag = 200 + event_index).
                        if (tag >= 200 && tag < 200 + g_history_event_count) {
                            int ei = tag - 200;
                            if (ei >= 0 && ei < g_history_event_count) {
                                uint64_t target = g_history_event_cache[ei].pane_id;
                                if (target != 0)
                                    g_jump_request = target;
                            }
                        }
                        // Attention Inbox jump (tag = RAIL_MENU_INBOX_BASE + index).
                        if (tag >= RAIL_MENU_INBOX_BASE
                            && tag < RAIL_MENU_INBOX_BASE + g_inbox_pane_count) {
                            int ii = tag - RAIL_MENU_INBOX_BASE;
                            if (ii >= 0 && ii < g_inbox_pane_count) {
                                uint64_t target = g_inbox_pane_cache[ii];
                                if (target != 0)
                                    g_jump_request = target;
                            }
                        }
                        // Cross-workspace search result jump
                        // (tag = RAIL_MENU_SEARCH_BASE + index).
                        if (tag >= RAIL_MENU_SEARCH_BASE
                            && tag < RAIL_MENU_SEARCH_BASE + g_search_result_count) {
                            int si = tag - RAIL_MENU_SEARCH_BASE;
                            if (si >= 0 && si < g_search_result_count) {
                                uint64_t target = g_search_result_pane_cache[si];
                                if (target != 0)
                                    g_jump_request = target;
                            }
                        }
                        // Diff-review result click: copy the file's path
                        // (tag = RAIL_MENU_DIFF_BASE + index).
                        if (tag >= RAIL_MENU_DIFF_BASE
                            && tag < RAIL_MENU_DIFF_BASE + g_diff_result_count) {
                            int di = tag - RAIL_MENU_DIFF_BASE;
                            if (di >= 0 && di < g_diff_result_count) {
                                SetClipboardText(g_diff_result_path_cache[di]);
                                toast_push(TOAST_INFO, "Copied path to clipboard");
                            }
                        }
                        break;
                    }
                }
            }
        }

        // ----------------------------------------------------------------
        // Resolve a pending jump-to-unread request (Cmd+Shift+U or a
        // notification-strip click): switch to the tab owning the target
        // pane and focus it. Attention clears through the normal
        // focus-change path at the top of the next frame.
        // ----------------------------------------------------------------
        if (g_jump_request != 0) {
            uint64_t target = g_jump_request;
            g_jump_request = 0;
            bool found = false;
            for (int ti = 0; ti < app.n_tabs && !found; ti++) {
                Tab *tt = &app.tabs[ti];
                if (!tt->root)
                    continue;
                PaneNode *jleaves[WORKSPACE_RAIL_MAX_PANES];
                int jn = 0;
                pane_collect_leaves(tt->root, jleaves, WORKSPACE_RAIL_MAX_PANES, &jn);
                for (int pj = 0; pj < jn; pj++) {
                    if (jleaves[pj]->kind != PANE_LEAF)
                        continue;
                    if (pane_id_for_session(jleaves[pj]->leaf.session) != target)
                        continue;
                    if (ti != app.active)
                        app_switch_tab(ti, &te, &pty_fd, &child, &child_exited);
                    app.tabs[app.active].focused = jleaves[pj];
                    sync_runtime_for_action(&te, &pty_fd, &child, &child_exited,
                                            &terminal, &render_state, &row_iter,
                                            &row_cells, &placement_iter,
                                            &key_encoder, &key_event,
                                            &mouse_encoder, &mouse_event);
                    prev_term_area_w = -1;
                    drain_char_queue();
                    found = true;
                    break;
                }
            }
        }

        kitty_image_renderer_begin_frame(kitty_renderer);

        BeginDrawing();
        ClearBackground(win_bg);

        // Draw workspace rail inside the drawing block.
        if (lo.rail_visible) {
            build_rail_view(&lo, now_ms, font_size);
            ui_workspace_rail_draw(mono_font, &g_rail_view,
                                   GetMouseX(), GetMouseY(),
                                   (float)frame_dt_sec, font_size);

            // Hover-preview dwell tracking: which row (if any) the mouse is
            // resting over, and for how long.
            bool hov_is_pane = false;
            int hov_idx = workspace_rail_row_at(&g_rail_view, GetMouseX(), GetMouseY(),
                                                &hov_is_pane);
            uint64_t hov_id = (hov_idx >= 0)
                ? (hov_is_pane ? g_rail_view.panes[hov_idx].id : g_rail_view.tabs[hov_idx].id)
                : 0;

            if (hov_id != g_hover_row_id) {
                g_hover_row_id = hov_id;
                g_hover_since_ms = now_ms;
                g_hover_preview[0] = '\0';
            }

            if (hov_id != 0 && (now_ms - g_hover_since_ms) >= FANGS_HOVER_DWELL_MS) {
                // Compute the preview text once per hover-in, not every
                // frame — a full scrollback dump isn't free.
                if (g_hover_preview[0] == '\0') {
                    Session *hs = session_for_pane_id(hov_id);
                    char *preview = hs
                        ? context_build((TermEngine *)session_engine(hs), 3, 200)
                        : NULL;
                    if (preview && preview[0]) {
                        snprintf(g_hover_preview, sizeof(g_hover_preview), "%s", preview);
                    } else {
                        // Mark "computed, nothing to show" so we don't retry
                        // context_build() every frame while still hovering.
                        snprintf(g_hover_preview, sizeof(g_hover_preview), " ");
                    }
                    free(preview);
                }

                // An idle prompt's scrollback tail is mostly whitespace/prompt
                // glyphs — skip the popup entirely rather than show a near-
                // empty box for it.
                int preview_nonspace = 0;
                for (const char *p = g_hover_preview; *p; p++) {
                    if (!isspace((unsigned char)*p)) preview_nonspace++;
                }
                bool has_preview = preview_nonspace >= FANGS_HOVER_PREVIEW_MIN_CHARS;

                if (has_preview) {
                    const WorkspaceRailRow *hrow = hov_is_pane
                        ? &g_rail_view.panes[hov_idx] : &g_rail_view.tabs[hov_idx];
                    int hpx = lo.rail.x + lo.rail.w + 8;
                    int hpw = 280;
                    if (hpx + hpw > GetScreenWidth() - 4)
                        hpx = GetScreenWidth() - hpw - 4;
                    int hpy = hrow->y;
                    int hph = 78;
                    if (hpy + hph > GetScreenHeight() - 4)
                        hpy = GetScreenHeight() - hph - 4;

                    DrawRectangle(hpx, hpy, hpw, hph, UI2RAY(g_ui_theme.inline_bg));
                    DrawRectangleLines(hpx, hpy, hpw, hph, UI2RAY(g_ui_theme.panel_border));
                    char hline[160];
                    snprintf(hline, sizeof(hline), "%s%s%s", hrow->label,
                            hrow->branch[0] ? "  " : "", hrow->branch);
                    DrawTextEx(mono_font, hline, (Vector2){(float)hpx + 8, (float)hpy + 6},
                              14.0f, 0, UI2RAY(g_ui_theme.text));
                    DrawTextEx(mono_font, g_hover_preview,
                              (Vector2){(float)hpx + 8, (float)hpy + 26},
                              12.0f, 0, UI2RAY(g_ui_theme.subtitle));
                }
            }
        }

        Tab *tab = &app.tabs[app.active];
        PaneNode *leaves[64];
        int n_leaves = 0;
        pane_collect_leaves(tab->root, leaves, 64, &n_leaves);

        // Allocate enough space for all leaf rects.
        PaneRectEntry pane_rects[64];
        PaneRectCollector collector = {
            .entries = pane_rects,
            .count = 0,
            .capacity = 64,
        };
        layout_compute_panes(tab->root,
                             lo.terminal.x, lo.terminal.y,
                             lo.terminal.w, lo.terminal.h,
                             pane_gap,
                             pane_rect_collect_cb, &collector);

        for (int i = 0; i < collector.count; i++) {
            PaneNode *leaf = collector.entries[i].leaf;
            int px = collector.entries[i].x;
            int py = collector.entries[i].y;
            int pw = collector.entries[i].w;
            int ph = collector.entries[i].h;

            if (pw < 1 || ph < 1) continue;

            Session *ss = leaf->leaf.session;
            TermEngine *lte = (TermEngine *)session_engine(ss);
            if (!lte) continue;

            // Borrow render handles from this session's term_engine, exactly
            // as the main loop does for the active session (§16.4).
            GhosttyTerminal lterm = term_engine_terminal(lte);
            GhosttyRenderState lrs = term_engine_render_state(lte);
            GhosttyRenderStateRowIterator lri = term_engine_row_iter(lte);
            GhosttyRenderStateRowCells lrc = term_engine_row_cells(lte);
            GhosttyKittyGraphicsPlacementIterator lpi = term_engine_placement_iter(lte);

            // Query scrollbar state.
            GhosttyTerminalScrollbar lsb = {0};
            GhosttyTerminalScrollbar *lsb_ptr = NULL;
            if (ghostty_terminal_get(lterm, GHOSTTY_TERMINAL_DATA_SCROLLBAR,
                                     &lsb) == GHOSTTY_SUCCESS)
                lsb_ptr = &lsb;

            bool focused = (leaf == tab->focused);
            int header_h = (ph >= (int)(48.0f * applied_scale))
                             ? (int)(24.0f * applied_scale)
                             : 0;
            int inner_rows = 0;
            uint64_t pane_id = pane_id_for_session(ss);
            draw_pane_chrome_and_content(
                leaf, px, py, pw, ph, header_h, focused, applied_scale,
                mono_font, bold_font, cell_width, cell_height, font_size, pad,
                lterm, lrs, lri, lrc, lpi, kitty_renderer, lsb_ptr, &cfg, now_ms,
                &inner_rows, pane_id);

            // Command-block overlay for the focused pane only. The chrome
            // wrapper scissored to the inner content rect during draw; restore
            // that clip here so the overlay cannot spill into the header or
            // frame border.
            if (focused && g_cmdblocks) {
                int ix = px + 1;
                int iy = py + header_h + 1;
                int iw = pw - 2;
                int ih = ph - header_h - 2;
                if (iw < 1) iw = 1;
                if (ih < 1) ih = 1;
                int lpane_term_area_w = iw;

                CmdBlockAction cb_action = {0};
                bool block_click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
                    && GetMouseX() >= px && GetMouseX() < px + pw
                    && GetMouseY() >= py && GetMouseY() < py + ph
                    && !ui_settings_open() && !ui_inline_active()
                    && !ui_palette_is_open() && !ui_workflow_prompt_active() && !ui_rename_prompt_active() && !ui_broadcast_prompt_active() && !ui_menu_active(&g_rail_menu)
                    && !ui_sidebar_focused();
                BeginScissorMode(ix, iy, iw, ih);
                if (cmdblocks_draw(g_cmdblocks, te, mono_font, &theme,
                                   cell_width, cell_height, font_size,
                                   pad, lpane_term_area_w, inner_rows,
                                   GetMouseX(), GetMouseY(), block_click,
                                   &cb_action)) {
                    g_sel.active = false;
                    g_sel.dragging = false;

                    if (cb_action.action == CB_ACTION_ASK_AI) {
                        open_sidebar_for_cmdblock_action(&cb_action);
                        cmdblock_action_free(&cb_action);
                    }
                }
                EndScissorMode();
            }
        }

        draw_gutter_hints(pane_rects, collector.count, pane_gap, applied_scale,
                          GetMouseX(), GetMouseY());

        if (g_search_active) {
            int matches = draw_search_highlights(lo.terminal.x, lo.terminal.y,
                                                 pad, cell_width, cell_height);
            draw_search_box(mono_font, lo.terminal.x, term_area_w, matches);
        }

        if (ui_sidebar_visible() && lo.sidebar_visible) {
            DrawLine(lo.sidebar.x, 0, lo.sidebar.x, lo.sidebar.h,
                     UI2RAY(g_ui_theme.sidebar_separator));
            // E5: tell the sidebar whether a key is configured so it can show
            // the first-run setup card instead of a dead input.
            const char *cur_key = resolve_api_key(&cfg);
            ui_sidebar_set_has_key(cur_key && cur_key[0]);
            // out_prompt carries just the question now; any §15 block context
            // is taken separately below, so this buffer stays small.
            char submitted[1024] = "";
            char run_cmd[1024] = "";
            // The UI renders in logical space (crispness from the 2x font
            // texture), so widgets size at scale 1.0 to match the terminal.
            if (ui_sidebar_draw(mono_font, lo.sidebar, submitted, (int)sizeof(submitted),
                                run_cmd, (int)sizeof(run_cmd), 1.0f, (float)frame_dt_sec)) {
                // A new question interrupts any in-flight stream.
                if (active_stream) {
                    ai_stream_cancel(active_stream);
                    ai_stream_free(active_stream);
                    active_stream = NULL;
                    ui_sidebar_end_assistant();
                }
                ui_sidebar_push(MSG_USER, submitted);
                // §15: redacted command-block context (NULL for a normal turn).
                // Sent to the model in place of the scrollback dump, not shown
                // in the chat bubble. We own it and must free it.
                char *block_ctx = ui_sidebar_take_oneshot_context();
                const char *key = resolve_api_key(&cfg);
                if (!key || !key[0]) {
                    ui_sidebar_push(MSG_SYSTEM,
                        "No API key. Set FANGS_API_KEY or add one in Ctrl+, settings.");
                } else {
                    active_stream = start_ai_request(te, &cfg, submitted, block_ctx);
                    if (active_stream)
                        ui_sidebar_begin_assistant();
                    else
                        ui_sidebar_push(MSG_SYSTEM, "Failed to start the AI request.");
                }
                free(block_ctx);
            }
            // Run button: stage the command at the prompt — NO trailing newline,
            // the user reviews and presses Enter themselves.
            if (run_cmd[0])
                pty_write(pty_fd, run_cmd, strlen(run_cmd));
        }

        // Inline AI prompt: floating over the terminal, below the settings modal.
        ui_inline_draw(mono_font, 1.0f);
        const char *inline_prompt = ui_inline_take_prompt();
        if (inline_prompt) {
            const char *ikey = resolve_api_key(&cfg);
            if (!ikey || !ikey[0]) {
                ui_inline_set_error("No API key (set FANGS_API_KEY)");
            } else {
                inline_answer[0] = '\0';
                inline_stream = start_inline_request(te, &cfg, inline_prompt);
                if (inline_stream)
                    ui_inline_set_waiting("thinking…");
                else
                    ui_inline_set_error("failed to start request");
            }
        }

        // Show a banner when the child process has exited so the user
        // knows the shell is gone (they can still scroll / inspect output).
        if (child_exited) {
            char exit_msg[128];
            if (child_exit_status >= 0)
                snprintf(exit_msg, sizeof(exit_msg),
                         "[process exited with status %d]", child_exit_status);
            else
                snprintf(exit_msg, sizeof(exit_msg), "[process exited]");

            Vector2 msg_size = MeasureTextEx(mono_font, exit_msg, font_size, 0);
            int screen_w = GetScreenWidth();
            int screen_h = GetScreenHeight();
            int banner_h = (int)msg_size.y + 8;
            DrawRectangle(0, screen_h - banner_h, screen_w, banner_h,
                          UI2RAY(g_ui_theme.exit_banner_bg));
            DrawTextEx(mono_font, exit_msg,
                       (Vector2){(screen_w - msg_size.x) / 2,
                                 screen_h - banner_h + 4},
                       font_size, 0, UI2RAY(g_ui_theme.exit_banner_text));
        }

        // Advance toast timers each frame.
        toast_tick(frame_dt_sec);

        // Flush the session-state file if the tab list changed this frame.
        persist_session_if_dirty(&cfg);

        if (ui_settings_open()) {
            bool saved = false;
            ui_settings_draw(&cfg, &saved, 1.0f);
            if (saved) {
                if (!config_save(&cfg, config_path)) {
                    fprintf(stderr, "warning: failed to save config at %s\n", config_path);
                    toast_push(TOAST_WARN, "Failed to save config.");
                } else {
                    apply_saved_config = true;
                    toast_push(TOAST_INFO, "Settings saved.");
                }
            }
        }

        {
            UiPaletteSelection palette_selection = palette_selection_none();
            if (ui_palette_draw(mono_font, 1.0f, &palette_selection))
                pending_palette_selection = palette_selection;
        }

        ui_workflow_prompt_draw(mono_font, 1.0f);
        const char *workflow_command = ui_workflow_prompt_take_command();
        if (workflow_command)
            stage_command_text(workflow_command, pty_fd);

        // Workspace rename prompt: draw, then apply an accepted name.
        ui_rename_prompt_draw(mono_font, 1.0f);
        {
            int rename_tab = -1;
            char rename_name[RENAME_PROMPT_NAME_MAX];
            if (ui_rename_prompt_take(&rename_tab, rename_name,
                                      (int)sizeof(rename_name))
                && rename_tab >= 0 && rename_tab < app.n_tabs) {
                snprintf(app.tabs[rename_tab].name,
                         sizeof(app.tabs[rename_tab].name), "%s", rename_name);
                g_session_dirty = true;
            }
        }

        // Broadcast prompt: draw, then send the accepted text to every
        // live session's PTY across every open workspace.
        ui_broadcast_prompt_draw(mono_font, 1.0f);
        {
            char broadcast_text[BROADCAST_PROMPT_TEXT_MAX];
            if (ui_broadcast_prompt_take(broadcast_text, (int)sizeof(broadcast_text))
                && broadcast_text[0] != '\0') {
                int sent = 0;
                for (int ti = 0; ti < app.n_tabs; ti++) {
                    PaneNode *bleaves[WORKSPACE_RAIL_MAX_PANES];
                    int bn = 0;
                    pane_collect_leaves(app.tabs[ti].root, bleaves,
                                        WORKSPACE_RAIL_MAX_PANES, &bn);
                    for (int bi = 0; bi < bn; bi++) {
                        if (bleaves[bi]->kind != PANE_LEAF)
                            continue;
                        Session *bs = bleaves[bi]->leaf.session;
                        if (!bs || !session_child_alive(bs))
                            continue;
                        pty_write(session_pty_fd(bs), broadcast_text,
                                 strlen(broadcast_text));
                        pty_write(session_pty_fd(bs), "\r", 1);
                        sent++;
                    }
                }
                char toast_msg[64];
                snprintf(toast_msg, sizeof(toast_msg),
                         "Sent to %d workspace%s", sent, sent == 1 ? "" : "s");
                toast_push(TOAST_INFO, toast_msg);
            }
        }

        // Draw toast notifications (polished rounded cards, top-right).
        toast_draw(mono_font, applied_scale, frame_dt_sec);

        // Draw context menu / history popover on top of everything.
        if (g_rail_menu.open) {
            ui_menu_draw(mono_font, &g_rail_menu, GetMouseX(), GetMouseY(),
                         &g_ui_theme);
        }

        // Idle-aware frame pacing: any open UI surface, in-flight AI stream,
        // in-progress drag, or visible toast counts as activity, same as
        // input/PTY/resize/focus above. Skipped entirely during headless
        // smoke runs so their frame-count-based timing is untouched.
        if (ui_settings_open() || ui_palette_is_open() || ui_workflow_prompt_active()
            || ui_rename_prompt_active() || ui_broadcast_prompt_active() || ui_menu_active(&g_rail_menu)
            || ui_inline_active() || g_search_active
            || active_stream || inline_stream
            || g_drag_from >= 0 || toast_count() > 0) {
            last_activity_ms = now_ms;
        }
        EndDrawing();

        // Opt-in frame-timing diagnostic: FANGS_PERF=1 logs REAL wall-clock fps.
        if (getenv("FANGS_PERF")) {
            static struct timespec last = {0}; static double acc = 0, worst = 0; static int n = 0;
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            if (last.tv_sec != 0) {
                double dt = (now.tv_sec - last.tv_sec) * 1000.0
                          + (now.tv_nsec - last.tv_nsec) / 1.0e6;
                acc += dt; n++;
                if (dt > worst) worst = dt;
                if (n >= 60) {
                    fprintf(stderr, "[PERF] real avg=%.1fms (=%.0f fps) worst=%.1fms settings_open=%d\n",
                            acc / n, 1000.0 / (acc / n), worst, ui_settings_open());
                    acc = 0; n = 0; worst = 0;
                }
            }
            last = now;
        }

        // Free transient image textures after EndDrawing() flushes commands.
        kitty_image_renderer_end_frame(kitty_renderer);

        if (blocks_smoke && blocks_smoke_started) {
            blocks_smoke_frames++;
            if (blocks_smoke_frames >= 3) {
                if (!export_screen_image(blocks_smoke_path))
                    exit_code = 1;
                break;
            }
        }

        if (kitty_smoke && kitty_smoke_started) {
            kitty_smoke_frames++;
            if (kitty_smoke_frames >= 3) {
                if (!export_screen_image(kitty_smoke_path))
                    exit_code = 1;
                break;
            }
        }

        if (phase3_smoke && phase3_smoke_started) {
            phase3_smoke_frames++;
            if (phase3_smoke_frames >= 2) {
                bool screenshot_written = false;
                if (phase3_smoke_screenshot_path && phase3_smoke_screenshot_path[0] != '\0')
                    screenshot_written = export_screen_image(phase3_smoke_screenshot_path);

                bool report_written = write_phase3_smoke_report(
                    phase3_smoke_report_path, lo, term_area_w,
                    term_cols, term_rows, screenshot_written);

                if ((phase3_smoke_report_path && phase3_smoke_report_path[0] != '\0' && !report_written)
                    || (phase3_smoke_screenshot_path && phase3_smoke_screenshot_path[0] != '\0' && !screenshot_written)) {
                    exit_code = 1;
                }
                break;
            }
        }

        if (apply_saved_config) {
            if (!apply_config_all_sessions(&cfg, &mono_font, &bold_font, &font_size,
                                            &cell_width, &cell_height,
                                            &term_cols, &term_rows, term_area_w, pad)) {
                fprintf(stderr, "warning: failed to apply config\n");
                toast_push(TOAST_WARN, "Failed to apply config.");
            } else {
                prev_width = GetScreenWidth();
                prev_height = GetScreenHeight();
                prev_term_area_w = term_area_w;
            }
        }

        // All frame pacing lives here now (SetTargetFPS is uncapped — see
        // InitWindow above): block until GLFW actually has an event to
        // deliver, waking near-instantly on real input, or after the target
        // interval elapses, to bound staleness of PTY output/animations.
        // This runs every frame, not just while idle — raylib's own
        // SetTargetFPS pacing sleeps blindly via WaitTime() regardless of the
        // target value, so even the normal 60fps gap (16.7ms) was wide enough
        // to occasionally coalesce a fast click's press+release together;
        // only waking on the actual event (rather than napping through any
        // fixed window) closes that race for good. Skipped during headless
        // smoke runs, matching every other pacing check in this loop.
        if (!phase3_smoke && !blocks_smoke && !kitty_smoke) {
            bool idle = (now_ms - last_activity_ms) >= FANGS_IDLE_TIMEOUT_MS;
            glfwWaitEventsTimeout(idle ? (1.0 / FANGS_IDLE_FPS) : (1.0 / FANGS_ACTIVE_FPS));
        }
    }

cleanup:
    // Join any in-flight AI worker before tearing down (and before curl's global
    // cleanup). Safe even if active_stream is NULL.
    if (active_stream) {
        ai_stream_cancel(active_stream);
        ai_stream_free(active_stream);
        active_stream = NULL;
    }
    if (inline_stream) {
        ai_stream_cancel(inline_stream);
        ai_stream_free(inline_stream);
        inline_stream = NULL;
    }
    if (mono_font.texture.id != 0)
        UnloadFont(mono_font);
    if (bold_font.texture.id != 0)
        UnloadFont(bold_font);
    if (g_remote_api) {
        remote_api_stop(g_remote_api);
        g_remote_api = NULL;
    }
    if (g_git_status_sampler) {
        workspace_git_status_stop(g_git_status_sampler);
        g_git_status_sampler = NULL;
    }
    kitty_image_renderer_destroy(kitty_renderer);
    if (!save_window_geometry(&cfg, config_path))
        fprintf(stderr, "warning: failed to save window geometry at %s\n", config_path);
    persist_session_if_dirty(&cfg);  // final flush safety net
    CloseWindow();
    // Destroy all tabs/sessions — this closes PTY fds, kills children,
    // destroys term engines, and frees userdata via session_destroy().
    app_destroy_all();
    g_cmdblocks = NULL;
    ai_global_cleanup();
    return exit_code;
}
