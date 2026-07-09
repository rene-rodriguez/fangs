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

// Resolve the repository root for `cwd` (`git rev-parse --show-toplevel`).
// Returns false (and empties out) if cwd isn't inside a git repo.
bool workspace_worktree_repo_root(const char *cwd, char *out, int out_size);

#define WORKTREE_CLEANUP_MAX 32

typedef struct {
    char path[WORKTREE_PATH_MAX];
    char branch[WORKTREE_NAME_MAX];
} WorkspaceWorktreeCandidate;

// Find worktrees under <repo_root>/.worktrees/ eligible for cleanup: branch
// fully merged into the repo's default branch AND `git status --porcelain`
// is empty (no uncommitted changes). Never includes a path present in
// exclude_paths (the caller's currently-open tab cwds) even if otherwise
// eligible. Returns the number of candidates written to `out` (<= max).
// Returns 0 (never negative) if the default branch can't be resolved or the
// repo has no eligible worktrees — never guesses at what's "safe" to delete.
int workspace_worktree_find_cleanup_candidates(const char *repo_root,
                                               const char *const *exclude_paths,
                                               int exclude_count,
                                               WorkspaceWorktreeCandidate *out,
                                               int max);

// Remove a cleanup candidate after re-checking that it is still under
// <repo_root>/.worktrees/, still not open, still merged, and still clean.
// Uses non-force git removal/deletion so a concurrent dirty/unmerged change
// fails closed instead of deleting work.
bool workspace_worktree_cleanup(const char *repo_root,
                                const char *const *exclude_paths,
                                int exclude_count,
                                const WorkspaceWorktreeCandidate *candidate);

#endif
