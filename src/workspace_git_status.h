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

typedef struct {
    uint64_t pane_id;
    char cwd[WORKSPACE_GIT_STATUS_PATH_MAX];
} WorkspaceGitStatusTarget;

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

#endif // FANGS_WORKSPACE_GIT_STATUS_H
