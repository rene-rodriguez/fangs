// action_registry — Stable IDs and display metadata for Fangs host actions.
#ifndef FANGS_ACTION_REGISTRY_H
#define FANGS_ACTION_REGISTRY_H

typedef enum {
    FANGS_ACTION_NONE = 0,
    FANGS_ACTION_OPEN_COMMAND_PALETTE,
    FANGS_ACTION_OPEN_SETTINGS,
    FANGS_ACTION_TOGGLE_SIDEBAR,
    FANGS_ACTION_INLINE_COMMAND,
    FANGS_ACTION_ASK_LATEST_BLOCK,
    FANGS_ACTION_SAVE_LATEST_BLOCK_WORKFLOW,
    FANGS_ACTION_FIND,
    FANGS_ACTION_COPY_SELECTION,
    FANGS_ACTION_PASTE,
    FANGS_ACTION_NEW_TAB,
    FANGS_ACTION_CLOSE_PANE,
    FANGS_ACTION_SPLIT_RIGHT,
    FANGS_ACTION_SPLIT_DOWN,
    FANGS_ACTION_FOCUS_LEFT,
    FANGS_ACTION_FOCUS_RIGHT,
    FANGS_ACTION_FOCUS_UP,
    FANGS_ACTION_FOCUS_DOWN,
    FANGS_ACTION_FONT_INCREASE,
    FANGS_ACTION_FONT_DECREASE,
    FANGS_ACTION_FONT_RESET,
} FangsActionId;

typedef struct {
    FangsActionId id;
    const char   *name;      // Stable machine-readable ID.
    const char   *label;     // Human-readable command palette label.
    const char   *detail;    // Short description for filtering and UI detail.
    const char   *shortcut;  // Display-only shortcut hint.
} FangsAction;

const FangsAction *action_registry_all(int *out_count);
const FangsAction *action_registry_find(FangsActionId id);

#endif // FANGS_ACTION_REGISTRY_H
