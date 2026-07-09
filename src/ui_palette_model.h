// ui_palette_model — Pure command-palette filtering and selection state.
#ifndef FANGS_UI_PALETTE_MODEL_H
#define FANGS_UI_PALETTE_MODEL_H

#include <stdbool.h>

#include "action_registry.h"
#include "workflows.h"

#define UI_PALETTE_QUERY_MAX   128
#define UI_PALETTE_MATCHES_MAX 64
#define UI_PALETTE_WORKSPACES_MAX 9  // mirror FANGS_MAX_TABS

typedef struct {
    int type;
    FangsActionId action_id;
    int workflow_index;
    int workspace_index;  // index into UiPaletteModel.workspaces
} UiPaletteEntry;

typedef struct {
    int type;
    FangsActionId action_id;
    int workflow_index;
    int tab_index;        // app.tabs[] index, for UI_PALETTE_SELECTION_WORKSPACE
} UiPaletteSelection;

enum {
    UI_PALETTE_ENTRY_ACTION = 1,
    UI_PALETTE_ENTRY_WORKFLOW = 2,
    UI_PALETTE_ENTRY_WORKSPACE = 3,
};

enum {
    UI_PALETTE_SELECTION_NONE = 0,
    UI_PALETTE_SELECTION_ACTION = 1,
    UI_PALETTE_SELECTION_WORKFLOW = 2,
    UI_PALETTE_SELECTION_WORKSPACE = 3,
};

// One open workspace, as offered to the palette by the host. Copied by
// value on ui_palette_model_set_workspaces (tiny array, and tab identity /
// order can change between palette opens, unlike the workflow registry).
typedef struct {
    int  tab_index;   // app.tabs[] index to switch to on accept
    char label[64];   // display label (name > title > cwd, host-resolved)
} WorkspacePaletteEntry;

typedef struct {
    bool open;
    char query[UI_PALETTE_QUERY_MAX];
    int selected;
    int match_count;
    UiPaletteEntry matches[UI_PALETTE_MATCHES_MAX];
    const WorkflowRegistry *workflows;
    WorkspacePaletteEntry workspaces[UI_PALETTE_WORKSPACES_MAX];
    int workspace_count;
} UiPaletteModel;

void ui_palette_model_init(UiPaletteModel *m);
void ui_palette_model_open(UiPaletteModel *m);
void ui_palette_model_close(UiPaletteModel *m);
bool ui_palette_model_is_open(const UiPaletteModel *m);
void ui_palette_model_set_workflows(UiPaletteModel *m,
                                    const WorkflowRegistry *workflows);
void ui_palette_model_set_workspaces(UiPaletteModel *m,
                                     const WorkspacePaletteEntry *entries,
                                     int count);

const char *ui_palette_model_query(const UiPaletteModel *m);
void ui_palette_model_set_query(UiPaletteModel *m, const char *query);

int ui_palette_model_match_count(const UiPaletteModel *m);
UiPaletteEntry ui_palette_model_match_entry_at(const UiPaletteModel *m, int index);
const FangsAction *ui_palette_model_match_at(const UiPaletteModel *m, int index);
const Workflow *ui_palette_model_match_workflow_at(const UiPaletteModel *m, int index);
WorkspacePaletteEntry ui_palette_model_match_workspace_at(const UiPaletteModel *m, int index);

int ui_palette_model_selected(const UiPaletteModel *m);
void ui_palette_model_move(UiPaletteModel *m, int delta);
FangsActionId ui_palette_model_accept(UiPaletteModel *m);
UiPaletteSelection ui_palette_model_accept_selection(UiPaletteModel *m);

#endif // FANGS_UI_PALETTE_MODEL_H
