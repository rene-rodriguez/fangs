#ifndef FANGS_WORKSPACE_STATUS_H
#define FANGS_WORKSPACE_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    WORKSPACE_ATTENTION_NONE = 0,
    WORKSPACE_ATTENTION_INFO,
    WORKSPACE_ATTENTION_WARN,
    WORKSPACE_ATTENTION_ERROR,
} WorkspaceAttention;

#define WORKSPACE_STATUS_MAX_PANES 128

typedef struct {
    /* Per-pane attention levels */
    uint64_t pane_ids[WORKSPACE_STATUS_MAX_PANES];
    WorkspaceAttention levels[WORKSPACE_STATUS_MAX_PANES];
    char texts[WORKSPACE_STATUS_MAX_PANES][128];
    int count;
    /* Running byte counter for output detection */
    size_t pane_bytes[WORKSPACE_STATUS_MAX_PANES];
} WorkspaceStatus;

void workspace_status_init(WorkspaceStatus *st);

/* Record output activity on a pane.
 * focused: whether this pane is currently the focused/active pane.
 * bytes_read: number of bytes read this frame. */
void workspace_status_note_output(WorkspaceStatus *st, uint64_t pane_id,
                                  bool focused, size_t bytes_read);

/* Record a command completion on a pane.
 * focused: whether this pane is currently focused.
 * exit_code: the command's exit code. */
void workspace_status_note_command(WorkspaceStatus *st, uint64_t pane_id,
                                   bool focused, int exit_code);

/* Record a child exit on a pane. */
void workspace_status_note_child_exit(WorkspaceStatus *st, uint64_t pane_id,
                                      bool focused, int exit_code);

/* Clear attention for a specific pane (e.g. when focused). */
void workspace_status_clear(WorkspaceStatus *st, uint64_t pane_id);

/* Get the attention level for a single pane. */
WorkspaceAttention workspace_status_level(const WorkspaceStatus *st, uint64_t pane_id);

/* Get the notification text for a single pane (empty string if none). */
const char *workspace_status_text(const WorkspaceStatus *st, uint64_t pane_id);

/* Get the highest attention level among the given pane IDs. */
WorkspaceAttention workspace_status_highest(const WorkspaceStatus *st,
                                            const uint64_t *pane_ids, int n);

/* Build a single notification string from the given pane IDs.
 * Picks the most severe unread event's text. */
void workspace_status_notification(const WorkspaceStatus *st,
                                   const uint64_t *pane_ids, int n,
                                   char *out, int out_size);

/* Prune any entries whose pane_id is not in the keep set. */
void workspace_status_prune(WorkspaceStatus *st, const uint64_t *keep_ids, int n);

/* Remove a single pane by ID. */
void workspace_status_remove(WorkspaceStatus *st, uint64_t pane_id);

#endif
