// workspace_git_status — background git dirty-count sampler for rail badges.
//
// The UI thread submits the current pane id/cwd targets and reads the latest
// sampled counts. The worker thread is the only place that runs git; it
// publishes results under a mutex so render/model code never blocks on git.
#ifndef FANGS_WORKSPACE_GIT_STATUS_H
#define FANGS_WORKSPACE_GIT_STATUS_H

#include <stdint.h>

#define WORKSPACE_GIT_STATUS_MAX_TARGETS 64
#define WORKSPACE_GIT_STATUS_PATH_MAX 4096
#define WORKSPACE_GIT_STATUS_INTERVAL_MS 2000
#define WORKSPACE_GIT_STATUS_MAX_FILES 24

typedef struct {
    uint64_t pane_id;
    char cwd[WORKSPACE_GIT_STATUS_PATH_MAX];
} WorkspaceGitStatusTarget;

typedef struct {
    char path[WORKSPACE_GIT_STATUS_PATH_MAX];
    char status[3];     // e.g. "M ", "A ", "D ", "??", "R "
    int  insertions;    // -1 if unknown (untracked / binary)
    int  deletions;     // -1 if unknown
} WorkspaceGitFileChange;

typedef struct WorkspaceGitStatusSampler WorkspaceGitStatusSampler;

// Return the number of changed/untracked files in cwd's git worktree.
// Returns -1 when cwd is not in a git worktree or git fails.
int workspace_git_status_count_path(const char *cwd);

WorkspaceGitStatusSampler *workspace_git_status_start(void);
void workspace_git_status_stop(WorkspaceGitStatusSampler *sampler);

// Replace the target set. The sampler copies the array immediately.
void workspace_git_status_set_targets(WorkspaceGitStatusSampler *sampler,
                                      const WorkspaceGitStatusTarget *targets,
                                      int target_count);

// Latest count for pane_id. Unknown, clean, and non-git panes all read as 0
// because the rail only needs to render positive badges.
int workspace_git_status_count_for(WorkspaceGitStatusSampler *sampler,
                                   uint64_t pane_id);

// Sum positive counts for pane_ids, counting each git worktree once even when
// several panes point at the same repo from different subdirectories.
int workspace_git_status_sum_unique_for(WorkspaceGitStatusSampler *sampler,
                                        const uint64_t *pane_ids,
                                        int pane_count);

// One-shot, foreground call — NOT sampled/threaded. Lists changed files
// (status + insertion/deletion counts where known) for cwd's git worktree.
// Callers must only invoke this in response to an explicit user action
// (e.g. a rail badge/menu click), never per-frame or per-keystroke, matching
// the same performance guardrail as the cross-workspace scrollback search.
// Returns the number of entries written to `out` (<= max_out); the true
// total (which may exceed max_out) is written to *out_total when non-NULL.
// Returns 0 (and *out_total = 0) when cwd is not in a git worktree or clean.
int workspace_git_status_list_changes(const char *cwd,
                                      WorkspaceGitFileChange *out, int max_out,
                                      int *out_total);

#endif // FANGS_WORKSPACE_GIT_STATUS_H
