#ifndef FANGS_WORKSPACE_WORKTREE_H
#define FANGS_WORKSPACE_WORKTREE_H

#include <stdbool.h>

#define WORKTREE_NAME_MAX 96
#define WORKTREE_PATH_MAX 4096
#define WORKTREE_ERROR_MAX 256

typedef struct {
    char repo_root[WORKTREE_PATH_MAX];
    char branch[WORKTREE_NAME_MAX];
    char path[WORKTREE_PATH_MAX];
    char error[WORKTREE_ERROR_MAX];
} WorkspaceWorktreeResult;

bool workspace_worktree_sanitize_name(const char *input, char *out, int out_size);
bool workspace_worktree_candidate(const char *current_branch, int suffix, char *out, int out_size);
bool workspace_worktree_create(const char *cwd, WorkspaceWorktreeResult *out);
bool workspace_worktree_remove_created(const WorkspaceWorktreeResult *created);

#endif
