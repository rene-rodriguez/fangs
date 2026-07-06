#include "workspace_status.h"

#include <stdio.h>
#include <string.h>

static int find_pane(const WorkspaceStatus *st, uint64_t pane_id)
{
    for (int i = 0; i < st->count; i++) {
        if (st->pane_ids[i] == pane_id) return i;
    }
    return -1;
}

static int find_or_add(WorkspaceStatus *st, uint64_t pane_id)
{
    int idx = find_pane(st, pane_id);
    if (idx >= 0) return idx;
    if (st->count >= WORKSPACE_STATUS_MAX_PANES) return -1;
    idx = st->count++;
    st->pane_ids[idx] = pane_id;
    st->levels[idx] = WORKSPACE_ATTENTION_NONE;
    st->texts[idx][0] = '\0';
    st->pane_bytes[idx] = 0;
    return idx;
}

void workspace_status_init(WorkspaceStatus *st)
{
    st->count = 0;
}

void workspace_status_note_output(WorkspaceStatus *st, uint64_t pane_id,
                                  bool focused, size_t bytes_read)
{
    if (bytes_read == 0) return;
    int idx = find_or_add(st, pane_id);
    if (idx < 0) return;

    st->pane_bytes[idx] += bytes_read;

    /* Active pane output does not create unread attention */
    if (focused) return;

    /* Only set info if current level is lower */
    if (st->levels[idx] < WORKSPACE_ATTENTION_INFO) {
        st->levels[idx] = WORKSPACE_ATTENTION_INFO;
        /* Preserve existing text unless it's a higher severity */
        if (st->texts[idx][0] == '\0') {
            snprintf(st->texts[idx], sizeof(st->texts[idx]), "new output");
        }
    }
}

void workspace_status_note_command(WorkspaceStatus *st, uint64_t pane_id,
                                   bool focused, int exit_code)
{
    int idx = find_or_add(st, pane_id);
    if (idx < 0) return;

    if (focused) return; /* Active pane commands don't create unread */

    if (exit_code != 0 && st->levels[idx] < WORKSPACE_ATTENTION_WARN) {
        st->levels[idx] = WORKSPACE_ATTENTION_WARN;
        snprintf(st->texts[idx], sizeof(st->texts[idx]), "exit %d", exit_code);
    }
}

void workspace_status_note_child_exit(WorkspaceStatus *st, uint64_t pane_id,
                                      bool focused, int exit_code)
{
    int idx = find_or_add(st, pane_id);
    if (idx < 0) return;

    if (focused) return;

    st->levels[idx] = WORKSPACE_ATTENTION_ERROR;
    snprintf(st->texts[idx], sizeof(st->texts[idx]),
             "process exited: %d", exit_code);
}

void workspace_status_clear(WorkspaceStatus *st, uint64_t pane_id)
{
    int idx = find_pane(st, pane_id);
    if (idx < 0) return;
    st->levels[idx] = WORKSPACE_ATTENTION_NONE;
    st->texts[idx][0] = '\0';
}

WorkspaceAttention workspace_status_level(const WorkspaceStatus *st, uint64_t pane_id)
{
    int idx = find_pane(st, pane_id);
    if (idx < 0) return WORKSPACE_ATTENTION_NONE;
    return st->levels[idx];
}

const char *workspace_status_text(const WorkspaceStatus *st, uint64_t pane_id)
{
    int idx = find_pane(st, pane_id);
    if (idx < 0) return "";
    return st->texts[idx];
}

WorkspaceAttention workspace_status_highest(const WorkspaceStatus *st,
                                            const uint64_t *pane_ids, int n)
{
    WorkspaceAttention highest = WORKSPACE_ATTENTION_NONE;
    for (int i = 0; i < n; i++) {
        WorkspaceAttention level = workspace_status_level(st, pane_ids[i]);
        if (level > highest) highest = level;
    }
    return highest;
}

void workspace_status_notification(const WorkspaceStatus *st,
                                   const uint64_t *pane_ids, int n,
                                   char *out, int out_size)
{
    if (out_size <= 0) return;

    /* Find the most severe unread event among the given panes */
    WorkspaceAttention highest = WORKSPACE_ATTENTION_NONE;
    int best_idx = -1;

    for (int i = 0; i < n; i++) {
        int idx = find_pane(st, pane_ids[i]);
        if (idx < 0) continue;
        if (st->levels[idx] > highest) {
            highest = st->levels[idx];
            best_idx = idx;
        }
    }

    if (best_idx >= 0 && st->texts[best_idx][0]) {
        snprintf(out, out_size, "%s", st->texts[best_idx]);
    } else {
        out[0] = '\0';
    }
}

void workspace_status_prune(WorkspaceStatus *st, const uint64_t *keep_ids, int n)
{
    int write_idx = 0;
    for (int i = 0; i < st->count; i++) {
        bool keep = false;
        for (int j = 0; j < n; j++) {
            if (st->pane_ids[i] == keep_ids[j]) {
                keep = true;
                break;
            }
        }
        if (keep) {
            if (write_idx != i) {
                st->pane_ids[write_idx] = st->pane_ids[i];
                st->levels[write_idx] = st->levels[i];
                st->texts[write_idx][0] = '\0';
                memcpy(st->texts[write_idx], st->texts[i], sizeof(st->texts[i]));
                st->pane_bytes[write_idx] = st->pane_bytes[i];
            }
            write_idx++;
        }
    }
    st->count = write_idx;
}

void workspace_status_remove(WorkspaceStatus *st, uint64_t pane_id)
{
    int idx = find_pane(st, pane_id);
    if (idx < 0) return;
    /* Shift remaining entries */
    for (int i = idx; i < st->count - 1; i++) {
        st->pane_ids[i] = st->pane_ids[i + 1];
        st->levels[i] = st->levels[i + 1];
        memcpy(st->texts[i], st->texts[i + 1], sizeof(st->texts[i]));
        st->pane_bytes[i] = st->pane_bytes[i + 1];
    }
    st->count--;
}
