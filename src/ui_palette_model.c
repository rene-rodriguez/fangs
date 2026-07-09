#include "ui_palette_model.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool ascii_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || needle[0] == '\0')
        return true;
    if (!haystack)
        return false;

    for (const char *h = haystack; *h; h++) {
        const char *hp = h;
        const char *np = needle;
        while (*hp && *np
               && tolower((unsigned char)*hp) == tolower((unsigned char)*np)) {
            hp++;
            np++;
        }
        if (*np == '\0')
            return true;
    }

    return false;
}

static bool action_matches_token(const FangsAction *a, const char *token)
{
    return ascii_contains_ci(a->name, token)
        || ascii_contains_ci(a->label, token)
        || ascii_contains_ci(a->detail, token)
        || ascii_contains_ci(a->shortcut, token);
}

static bool action_matches_query(const FangsAction *a, const char *query)
{
    if (!query || query[0] == '\0')
        return true;

    char q[UI_PALETTE_QUERY_MAX];
    snprintf(q, sizeof(q), "%s", query);

    char *p = q;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        char *start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        if (!action_matches_token(a, start))
            return false;
    }

    return true;
}

static bool workflow_matches_token(const Workflow *w, const char *token)
{
    return ascii_contains_ci(w->id, token)
        || ascii_contains_ci(w->label, token)
        || ascii_contains_ci(w->detail, token)
        || ascii_contains_ci(w->command, token);
}

static bool workflow_matches_query(const Workflow *w, const char *query)
{
    if (!query || query[0] == '\0')
        return true;

    char q[UI_PALETTE_QUERY_MAX];
    snprintf(q, sizeof(q), "%s", query);

    char *p = q;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        char *start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        if (!workflow_matches_token(w, start))
            return false;
    }

    return true;
}

static bool workspace_matches_query(const WorkspacePaletteEntry *w, const char *query)
{
    if (!query || query[0] == '\0')
        return true;

    char q[UI_PALETTE_QUERY_MAX];
    snprintf(q, sizeof(q), "%s", query);

    char *p = q;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        char *start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p) {
            *p = '\0';
            p++;
        }

        if (!ascii_contains_ci(w->label, start))
            return false;
    }

    return true;
}

static void recompute(UiPaletteModel *m)
{
    if (!m)
        return;

    m->match_count = 0;
    int count = 0;
    const FangsAction *actions = action_registry_all(&count);
    for (int i = 0; i < count && m->match_count < UI_PALETTE_MATCHES_MAX; i++) {
        if (action_matches_query(&actions[i], m->query)) {
            m->matches[m->match_count++] = (UiPaletteEntry){
                .type = UI_PALETTE_ENTRY_ACTION,
                .action_id = actions[i].id,
                .workflow_index = -1,
                .workspace_index = -1,
            };
        }
    }

    if (m->workflows) {
        int workflow_count = workflows_count(m->workflows);
        for (int i = 0; i < workflow_count && m->match_count < UI_PALETTE_MATCHES_MAX; i++) {
            const Workflow *w = workflows_get(m->workflows, i);
            if (w && workflow_matches_query(w, m->query)) {
                m->matches[m->match_count++] = (UiPaletteEntry){
                    .type = UI_PALETTE_ENTRY_WORKFLOW,
                    .action_id = FANGS_ACTION_NONE,
                    .workflow_index = i,
                    .workspace_index = -1,
                };
            }
        }
    }

    for (int i = 0; i < m->workspace_count && m->match_count < UI_PALETTE_MATCHES_MAX; i++) {
        if (workspace_matches_query(&m->workspaces[i], m->query)) {
            m->matches[m->match_count++] = (UiPaletteEntry){
                .type = UI_PALETTE_ENTRY_WORKSPACE,
                .action_id = FANGS_ACTION_NONE,
                .workflow_index = -1,
                .workspace_index = i,
            };
        }
    }

    if (m->match_count == 0) {
        m->selected = 0;
    } else if (m->selected >= m->match_count) {
        m->selected = m->match_count - 1;
    } else if (m->selected < 0) {
        m->selected = 0;
    }
}

void ui_palette_model_init(UiPaletteModel *m)
{
    if (!m)
        return;
    memset(m, 0, sizeof(*m));
    recompute(m);
}

void ui_palette_model_open(UiPaletteModel *m)
{
    if (!m)
        return;
    m->open = true;
    m->query[0] = '\0';
    m->selected = 0;
    recompute(m);
}

void ui_palette_model_close(UiPaletteModel *m)
{
    if (!m)
        return;
    m->open = false;
    m->query[0] = '\0';
    m->selected = 0;
    recompute(m);
}

bool ui_palette_model_is_open(const UiPaletteModel *m)
{
    return m ? m->open : false;
}

void ui_palette_model_set_workflows(UiPaletteModel *m,
                                    const WorkflowRegistry *workflows)
{
    if (!m)
        return;
    m->workflows = workflows;
    m->selected = 0;
    recompute(m);
}

void ui_palette_model_set_workspaces(UiPaletteModel *m,
                                     const WorkspacePaletteEntry *entries,
                                     int count)
{
    if (!m)
        return;
    if (count < 0)
        count = 0;
    if (count > UI_PALETTE_WORKSPACES_MAX)
        count = UI_PALETTE_WORKSPACES_MAX;
    if (count > 0 && entries)
        memcpy(m->workspaces, entries, (size_t)count * sizeof(*entries));
    m->workspace_count = count;
    m->selected = 0;
    recompute(m);
}

const char *ui_palette_model_query(const UiPaletteModel *m)
{
    return m ? m->query : "";
}

void ui_palette_model_set_query(UiPaletteModel *m, const char *query)
{
    if (!m)
        return;
    snprintf(m->query, sizeof(m->query), "%s", query ? query : "");
    m->selected = 0;
    recompute(m);
}

int ui_palette_model_match_count(const UiPaletteModel *m)
{
    return m ? m->match_count : 0;
}

const FangsAction *ui_palette_model_match_at(const UiPaletteModel *m, int index)
{
    if (!m || index < 0 || index >= m->match_count)
        return NULL;
    if (m->matches[index].type != UI_PALETTE_ENTRY_ACTION)
        return NULL;
    return action_registry_find(m->matches[index].action_id);
}

UiPaletteEntry ui_palette_model_match_entry_at(const UiPaletteModel *m, int index)
{
    UiPaletteEntry empty = {
        .type = 0,
        .action_id = FANGS_ACTION_NONE,
        .workflow_index = -1,
    };
    if (!m || index < 0 || index >= m->match_count)
        return empty;
    return m->matches[index];
}

const Workflow *ui_palette_model_match_workflow_at(const UiPaletteModel *m, int index)
{
    if (!m || index < 0 || index >= m->match_count)
        return NULL;
    UiPaletteEntry entry = m->matches[index];
    if (entry.type != UI_PALETTE_ENTRY_WORKFLOW || !m->workflows)
        return NULL;
    return workflows_get(m->workflows, entry.workflow_index);
}

WorkspacePaletteEntry ui_palette_model_match_workspace_at(const UiPaletteModel *m, int index)
{
    WorkspacePaletteEntry empty = { .tab_index = -1, .label = "" };
    if (!m || index < 0 || index >= m->match_count)
        return empty;
    UiPaletteEntry entry = m->matches[index];
    if (entry.type != UI_PALETTE_ENTRY_WORKSPACE
        || entry.workspace_index < 0 || entry.workspace_index >= m->workspace_count)
        return empty;
    return m->workspaces[entry.workspace_index];
}

int ui_palette_model_selected(const UiPaletteModel *m)
{
    return m ? m->selected : 0;
}

void ui_palette_model_move(UiPaletteModel *m, int delta)
{
    if (!m || m->match_count == 0)
        return;
    int next = m->selected + delta;
    while (next < 0)
        next += m->match_count;
    while (next >= m->match_count)
        next -= m->match_count;
    m->selected = next;
}

FangsActionId ui_palette_model_accept(UiPaletteModel *m)
{
    UiPaletteSelection selection = ui_palette_model_accept_selection(m);
    if (selection.type != UI_PALETTE_SELECTION_ACTION)
        return FANGS_ACTION_NONE;
    return selection.action_id;
}

UiPaletteSelection ui_palette_model_accept_selection(UiPaletteModel *m)
{
    UiPaletteSelection empty = {
        .type = UI_PALETTE_SELECTION_NONE,
        .action_id = FANGS_ACTION_NONE,
        .workflow_index = -1,
        .tab_index = -1,
    };
    if (!m || m->match_count == 0)
        return empty;

    UiPaletteEntry entry = ui_palette_model_match_entry_at(m, m->selected);
    if (entry.type == UI_PALETTE_ENTRY_ACTION) {
        const FangsAction *a = action_registry_find(entry.action_id);
        if (!a)
            return empty;
        ui_palette_model_close(m);
        return (UiPaletteSelection){
            .type = UI_PALETTE_SELECTION_ACTION,
            .action_id = a->id,
            .workflow_index = -1,
            .tab_index = -1,
        };
    }

    if (entry.type == UI_PALETTE_ENTRY_WORKFLOW) {
        if (!m->workflows || !workflows_get(m->workflows, entry.workflow_index))
            return empty;
        ui_palette_model_close(m);
        return (UiPaletteSelection){
            .type = UI_PALETTE_SELECTION_WORKFLOW,
            .action_id = FANGS_ACTION_NONE,
            .workflow_index = entry.workflow_index,
            .tab_index = -1,
        };
    }

    if (entry.type == UI_PALETTE_ENTRY_WORKSPACE) {
        if (entry.workspace_index < 0 || entry.workspace_index >= m->workspace_count)
            return empty;
        int tab_index = m->workspaces[entry.workspace_index].tab_index;
        ui_palette_model_close(m);
        return (UiPaletteSelection){
            .type = UI_PALETTE_SELECTION_WORKSPACE,
            .action_id = FANGS_ACTION_NONE,
            .workflow_index = -1,
            .tab_index = tab_index,
        };
    }

    return empty;
}
