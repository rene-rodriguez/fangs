// ui_palette_model — Pure command-palette filtering and selection state.
#ifndef FANGS_UI_PALETTE_MODEL_H
#define FANGS_UI_PALETTE_MODEL_H

#include <stdbool.h>

#include "action_registry.h"
#include "workflows.h"

#define UI_PALETTE_QUERY_MAX   128
#define UI_PALETTE_MATCHES_MAX 64

typedef struct {
    int type;
    FangsActionId action_id;
    int workflow_index;
} UiPaletteEntry;

typedef struct {
    int type;
    FangsActionId action_id;
    int workflow_index;
} UiPaletteSelection;

enum {
    UI_PALETTE_ENTRY_ACTION = 1,
    UI_PALETTE_ENTRY_WORKFLOW = 2,
};

enum {
    UI_PALETTE_SELECTION_NONE = 0,
    UI_PALETTE_SELECTION_ACTION = 1,
    UI_PALETTE_SELECTION_WORKFLOW = 2,
};

typedef struct {
    bool open;
    char query[UI_PALETTE_QUERY_MAX];
    int selected;
    int match_count;
    UiPaletteEntry matches[UI_PALETTE_MATCHES_MAX];
    const WorkflowRegistry *workflows;
} UiPaletteModel;

void ui_palette_model_init(UiPaletteModel *m);
void ui_palette_model_open(UiPaletteModel *m);
void ui_palette_model_close(UiPaletteModel *m);
bool ui_palette_model_is_open(const UiPaletteModel *m);
void ui_palette_model_set_workflows(UiPaletteModel *m,
                                    const WorkflowRegistry *workflows);

const char *ui_palette_model_query(const UiPaletteModel *m);
void ui_palette_model_set_query(UiPaletteModel *m, const char *query);

int ui_palette_model_match_count(const UiPaletteModel *m);
UiPaletteEntry ui_palette_model_match_entry_at(const UiPaletteModel *m, int index);
const FangsAction *ui_palette_model_match_at(const UiPaletteModel *m, int index);
const Workflow *ui_palette_model_match_workflow_at(const UiPaletteModel *m, int index);

int ui_palette_model_selected(const UiPaletteModel *m);
void ui_palette_model_move(UiPaletteModel *m, int delta);
FangsActionId ui_palette_model_accept(UiPaletteModel *m);
UiPaletteSelection ui_palette_model_accept_selection(UiPaletteModel *m);

#endif // FANGS_UI_PALETTE_MODEL_H
