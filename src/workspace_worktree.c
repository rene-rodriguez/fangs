// workspace_worktree — git worktree creation for isolated agent workspaces.
//
// All git invocations use fork/execvp with argv arrays, never system() or
// shell string concatenation.
#include "workspace_worktree.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// Execute a git command that returns output on stdout.
// Returns 0 on success with output in buf (null-terminated, trimmed).
// Returns -1 on fork/exec failure, or the exit status on non-zero exit.
// argv must have up to 7 elements (a1..a6) followed by a NULL sentinel.
static int git_capture(const char *repo, const char *const *argv,
                       char *buf, int buf_size)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: redirect stdout to pipe, exec git.
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        if (repo) {
            execlp("git", "git", "-C", repo,
                   argv[0], argv[1], argv[2], argv[3],
                   argv[4], argv[5], argv[6], (char *)NULL);
        } else {
            execlp("git", "git",
                   argv[0], argv[1], argv[2], argv[3],
                   argv[4], argv[5], argv[6], (char *)NULL);
        }
        _exit(127);
    }

    // Parent: read output.
    close(pipefd[1]);
    int total = 0;
    ssize_t n;
    while ((n = read(pipefd[0], buf + total, buf_size - total - 1)) > 0) {
        total += (int)n;
        if (total >= buf_size - 1) break;
    }
    close(pipefd[0]);
    buf[total] = '\0';

    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            // Trim trailing newline/whitespace.
            while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r' || buf[total - 1] == ' '))
                buf[--total] = '\0';
            return 0;
        }
        return code;
    }
    return -1;
}

// Execute a git command checking only exit status.
// Returns 0 on success, non-zero exit status, or -1 on fork failure.
// argv must have up to 7 elements (a1..a6) followed by a NULL sentinel.
static int git_run(const char *repo, const char *const *argv)
{
    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        if (repo) {
            execlp("git", "git", "-C", repo,
                   argv[0], argv[1], argv[2], argv[3],
                   argv[4], argv[5], argv[6], (char *)NULL);
        } else {
            execlp("git", "git",
                   argv[0], argv[1], argv[2], argv[3],
                   argv[4], argv[5], argv[6], (char *)NULL);
        }
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

// 4-arg git_run helper.
static int git_run_4(const char *repo, const char *a1, const char *a2,
                     const char *a3, const char *a4)
{
    const char *argv[7] = { a1, a2, a3, a4, NULL, NULL, NULL };
    return git_run(repo, argv);
}

// 5-arg git_run helper.
static int git_run_5(const char *repo, const char *a1, const char *a2,
                     const char *a3, const char *a4, const char *a5)
{
    const char *argv[7] = { a1, a2, a3, a4, a5, NULL, NULL };
    return git_run(repo, argv);
}

// 6-arg git_run helper.
static int git_run_6(const char *repo, const char *a1, const char *a2,
                     const char *a3, const char *a4, const char *a5,
                     const char *a6)
{
    const char *argv[7] = { a1, a2, a3, a4, a5, a6, NULL };
    return git_run(repo, argv);
}

// 4-arg git_capture helper.
static int git_capture_4(const char *repo, const char *a1, const char *a2,
                         const char *a3, const char *a4,
                         char *buf, int buf_size)
{
    const char *argv[7] = { a1, a2, a3, a4, NULL, NULL, NULL };
    return git_capture(repo, argv, buf, buf_size);
}

// Check if a character is valid in a worktree/branch name.
static bool is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-';
}

// True for characters that should become a single separator.
static bool is_separator(char c)
{
    return c == '-' || c == '/' || c == '\\' || c == ' ' || c == '\t';
}

bool workspace_worktree_sanitize_name(const char *input, char *out, int out_size)
{
    if (!input || !out || out_size <= 0) return false;

    int wi = 0;
    bool last_sep = true; // also trims leading separators

    for (const char *p = input; *p && wi < out_size - 1; p++) {
        unsigned char c = (unsigned char)*p;
        if (!is_separator((char)c) && is_name_char((char)c)) {
            out[wi++] = (char)c;
            last_sep = false;
        } else {
            if (!last_sep) {
                out[wi++] = '-';
                last_sep = true;
            }
        }
    }

    // Trim trailing separator.
    while (wi > 0 && out[wi - 1] == '-')
        wi--;
    out[wi] = '\0';

    return wi > 0;
}

bool workspace_worktree_candidate(const char *current_branch, int suffix,
                                  char *out, int out_size)
{
    if (!out || out_size <= 0) return false;

    char sanitized[WORKTREE_NAME_MAX];
    if (!current_branch || current_branch[0] == '\0') {
        snprintf(sanitized, sizeof(sanitized), "worktree-agent");
    } else if (strcmp(current_branch, "main") == 0 || strcmp(current_branch, "master") == 0) {
        snprintf(sanitized, sizeof(sanitized), "%s-agent", current_branch);
    } else {
        if (!workspace_worktree_sanitize_name(current_branch, sanitized,
                                              (int)sizeof(sanitized))) {
            return false;
        }
        // Append -agent
        int len = (int)strlen(sanitized);
        if (len + 7 < (int)sizeof(sanitized)) {
            snprintf(sanitized + len, sizeof(sanitized) - (size_t)len, "-agent");
        } else {
            return false;
        }
    }

    // Cap base name at 64 bytes before the numeric suffix.
    int base_len = (int)strlen(sanitized);
    if (base_len > 64) {
        sanitized[64] = '\0';
        base_len = 64;
    }

    if (suffix == 0) {
        snprintf(out, out_size, "%s", sanitized);
    } else {
        snprintf(out, out_size, "%s-%d", sanitized, suffix);
    }

    return out[0] != '\0';
}

bool workspace_worktree_repo_root(const char *cwd, char *out, int out_size)
{
    if (!out || out_size <= 0)
        return false;
    out[0] = '\0';
    if (!cwd || !cwd[0])
        return false;

    return git_capture_4(cwd, "rev-parse", "--show-toplevel", NULL, NULL,
                         out, out_size) == 0;
}

bool workspace_worktree_create(const char *cwd, WorkspaceWorktreeResult *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    if (!cwd || !cwd[0]) {
        snprintf(out->error, sizeof(out->error), "No working directory");
        return false;
    }

    // Resolve repository root.
    char root[WORKTREE_PATH_MAX];
    int ret = git_capture_4(cwd, "rev-parse", "--show-toplevel", NULL, NULL,
                            root, (int)sizeof(root));
    if (ret != 0) {
        snprintf(out->error, sizeof(out->error),
                 "Not inside a git repository");
        return false;
    }
    snprintf(out->repo_root, sizeof(out->repo_root), "%s", root);

    // Get current branch.
    char branch[WORKTREE_NAME_MAX];
    ret = git_capture_4(cwd, "branch", "--show-current", NULL, NULL,
                        branch, (int)sizeof(branch));
    if (ret != 0) branch[0] = '\0';

    // Generate unique candidate name.
    char candidate[WORKTREE_NAME_MAX];
    bool found = false;
    for (int attempt = 0; attempt < 1000; attempt++) {
        int suffix = attempt == 0 ? 0 : attempt + 1;
        if (!workspace_worktree_candidate(branch, suffix, candidate,
                                          (int)sizeof(candidate))) {
            snprintf(out->error, sizeof(out->error),
                     "Could not generate branch name");
            return false;
        }

        // Check if path exists.
        char wt_path[WORKTREE_PATH_MAX];
        snprintf(wt_path, sizeof(wt_path), "%s/.worktrees/%s", root, candidate);

        struct stat st;
        bool path_exists = (stat(wt_path, &st) == 0);

        // Check if branch ref exists.
        char ref[WORKTREE_NAME_MAX + 32];
        snprintf(ref, sizeof(ref), "refs/heads/%s", candidate);
        bool ref_exists = (git_run_5(root, "show-ref", "--verify", "--quiet",
                                     ref, NULL) == 0);

        if (!path_exists && !ref_exists) {
            snprintf(out->path, sizeof(out->path), "%s", wt_path);
            snprintf(out->branch, sizeof(out->branch), "%s", candidate);
            found = true;
            break;
        }
    }

    if (!found) {
        snprintf(out->error, sizeof(out->error),
                 "Could not generate unique branch name after 1000 attempts");
        return false;
    }

    // Create .worktrees directory if needed.
    char worktrees_dir[WORKTREE_PATH_MAX];
    snprintf(worktrees_dir, sizeof(worktrees_dir), "%s/.worktrees", root);
    mkdir(worktrees_dir, 0775);

    // Run git worktree add.
    ret = git_run_6(root, "worktree", "add", "-b", out->branch, out->path,
                    "HEAD");
    if (ret != 0) {
        snprintf(out->error, sizeof(out->error),
                 "Could not create worktree: git exited %d", ret);
        return false;
    }

    // Best-effort append to .git/info/exclude.
    char exclude_path[WORKTREE_PATH_MAX];
    snprintf(exclude_path, sizeof(exclude_path), "%s/.git/info/exclude", root);

    FILE *f = fopen(exclude_path, "r");
    if (f) {
        bool found_line = false;
        char line[1024];
        while (fgets(line, (int)sizeof(line), f)) {
            // Trim trailing newline.
            size_t ln = strlen(line);
            while (ln > 0 && (line[ln - 1] == '\n' || line[ln - 1] == '\r'))
                line[--ln] = '\0';
            if (strcmp(line, ".worktrees/") == 0) {
                found_line = true;
                break;
            }
        }
        fclose(f);

        if (!found_line) {
            f = fopen(exclude_path, "a");
            if (f) {
                fprintf(f, ".worktrees/\n");
                fclose(f);
            }
        }
    }

    return true;
}

bool workspace_worktree_remove_created(const WorkspaceWorktreeResult *created)
{
    if (!created || !created->repo_root[0] || !created->path[0])
        return false;

    const char *argv[7] = { "worktree", "remove", "--force", created->path, NULL, NULL, NULL };
    int ret = git_run(created->repo_root, argv);
    return ret == 0;
}

// Resolve the repo's default branch: prefer the remote's HEAD symlink
// (origin/main, origin/master, ...), falling back to a local "main" or
// "master" branch if no remote is configured. Empty string if neither
// resolves — callers must treat that as "nothing is safely mergeable into."
static void resolve_default_branch(const char *root, char *out, int out_size)
{
    out[0] = '\0';

    char ref[WORKTREE_NAME_MAX];
    const char *argv[7] = { "symbolic-ref", "--short", "refs/remotes/origin/HEAD",
                            NULL, NULL, NULL, NULL };
    if (git_capture(root, argv, ref, (int)sizeof(ref)) == 0 && ref[0]) {
        const char *slash = strchr(ref, '/');
        snprintf(out, (size_t)out_size, "%s", slash ? slash + 1 : ref);
        return;
    }

    if (git_run_4(root, "show-ref", "--verify", "--quiet", "refs/heads/main") == 0) {
        snprintf(out, (size_t)out_size, "main");
        return;
    }
    if (git_run_4(root, "show-ref", "--verify", "--quiet", "refs/heads/master") == 0) {
        snprintf(out, (size_t)out_size, "master");
        return;
    }
}

static size_t path_trimmed_len(const char *path)
{
    size_t len = path ? strlen(path) : 0;
    while (len > 1 && path[len - 1] == '/')
        len--;
    return len;
}

static bool path_equal_trimmed(const char *a, const char *b)
{
    size_t alen = path_trimmed_len(a);
    size_t blen = path_trimmed_len(b);
    return alen == blen && alen > 0 && strncmp(a, b, alen) == 0;
}

static bool path_is_descendant_of(const char *base, const char *path)
{
    size_t base_len = path_trimmed_len(base);
    size_t path_len = path_trimmed_len(path);
    return base_len > 0 && path_len > base_len
        && strncmp(path, base, base_len) == 0
        && path[base_len] == '/';
}

static bool path_same_or_descendant_of(const char *base, const char *path)
{
    return path_equal_trimmed(base, path) || path_is_descendant_of(base, path);
}

static bool path_under_repo_worktrees(const char *repo_root, const char *path)
{
    char worktrees_dir[WORKTREE_PATH_MAX];
    int n = snprintf(worktrees_dir, sizeof(worktrees_dir),
                     "%s/.worktrees", repo_root);
    if (n < 0 || n >= (int)sizeof(worktrees_dir))
        return false;
    return path_is_descendant_of(worktrees_dir, path);
}

// True if `branch` is fully merged into `default_branch`.
static bool branch_is_merged(const char *root, const char *default_branch,
                             const char *branch)
{
    if (!default_branch || !default_branch[0] || !branch || !branch[0])
        return false;

    char branch_ref[WORKTREE_NAME_MAX + 32];
    char default_ref[WORKTREE_NAME_MAX + 32];
    int bn = snprintf(branch_ref, sizeof(branch_ref), "refs/heads/%s", branch);
    int dn = snprintf(default_ref, sizeof(default_ref), "refs/heads/%s",
                      default_branch);
    if (bn < 0 || bn >= (int)sizeof(branch_ref)
        || dn < 0 || dn >= (int)sizeof(default_ref))
        return false;

    return git_run_4(root, "merge-base", "--is-ancestor",
                     branch_ref, default_ref) == 0;
}

// True if `path`'s working tree has no uncommitted changes.
static bool worktree_is_clean(const char *path)
{
    char status[4096];
    if (git_capture_4(path, "status", "--porcelain", NULL, NULL,
                      status, (int)sizeof(status)) != 0) {
        return false;  // can't verify -> not eligible
    }
    return status[0] == '\0';
}

static bool path_excluded(const char *path, const char *const *exclude_paths,
                          int exclude_count)
{
    for (int i = 0; i < exclude_count; i++) {
        if (exclude_paths[i] && path_same_or_descendant_of(path, exclude_paths[i]))
            return true;
    }
    return false;
}

static bool cleanup_candidate_is_safe(const char *repo_root,
                                      const char *default_branch,
                                      const char *path,
                                      const char *branch,
                                      const char *const *exclude_paths,
                                      int exclude_count)
{
    return path && path[0] && branch && branch[0]
        && strcmp(branch, default_branch) != 0
        && path_under_repo_worktrees(repo_root, path)
        && !path_excluded(path, exclude_paths, exclude_count)
        && branch_is_merged(repo_root, default_branch, branch)
        && worktree_is_clean(path);
}

int workspace_worktree_find_cleanup_candidates(const char *repo_root,
                                               const char *const *exclude_paths,
                                               int exclude_count,
                                               WorkspaceWorktreeCandidate *out,
                                               int max)
{
    if (!repo_root || !repo_root[0] || !out || max <= 0)
        return 0;

    char root[WORKTREE_PATH_MAX];
    if (!workspace_worktree_repo_root(repo_root, root, (int)sizeof(root)))
        return 0;

    char default_branch[WORKTREE_NAME_MAX];
    resolve_default_branch(root, default_branch, (int)sizeof(default_branch));
    if (!default_branch[0])
        return 0;

    char list[16384];
    const char *list_argv[7] = { "worktree", "list", "--porcelain", NULL, NULL, NULL, NULL };
    if (git_capture(root, list_argv, list, (int)sizeof(list)) != 0)
        return 0;

    int n = 0;
    char cur_path[WORKTREE_PATH_MAX] = "";
    char cur_branch[WORKTREE_NAME_MAX] = "";

    // git worktree list --porcelain emits blocks separated by blank lines,
    // but strtok_r collapses consecutive delimiters and never yields an
    // empty token for that blank line — so a block boundary can only be
    // detected by the *next* "worktree " line (or end of input). Flush the
    // previous block right before starting a new one, then flush once more
    // after the loop for the final block.
    char *saveptr = NULL;
    for (char *line = strtok_r(list, "\n", &saveptr); line;
         line = strtok_r(NULL, "\n", &saveptr)) {
        if (strncmp(line, "worktree ", 9) == 0) {
            if (n < max
                && cleanup_candidate_is_safe(root, default_branch,
                                             cur_path, cur_branch,
                                             exclude_paths, exclude_count)) {
                snprintf(out[n].path, sizeof(out[n].path), "%s", cur_path);
                snprintf(out[n].branch, sizeof(out[n].branch), "%s", cur_branch);
                n++;
            }
            snprintf(cur_path, sizeof(cur_path), "%s", line + 9);
            cur_branch[0] = '\0';
        } else if (strncmp(line, "branch refs/heads/", 18) == 0) {
            snprintf(cur_branch, sizeof(cur_branch), "%s", line + 18);
        }
        // Any other line (HEAD <sha>, detached) is ignored.
    }
    if (n < max
        && cleanup_candidate_is_safe(root, default_branch,
                                     cur_path, cur_branch,
                                     exclude_paths, exclude_count)) {
        snprintf(out[n].path, sizeof(out[n].path), "%s", cur_path);
        snprintf(out[n].branch, sizeof(out[n].branch), "%s", cur_branch);
        n++;
    }

    return n;
}

bool workspace_worktree_cleanup(const char *repo_root,
                                const char *const *exclude_paths,
                                int exclude_count,
                                const WorkspaceWorktreeCandidate *candidate)
{
    if (!repo_root || !repo_root[0] || !candidate || !candidate->path[0]
        || !candidate->branch[0])
        return false;

    char root[WORKTREE_PATH_MAX];
    if (!workspace_worktree_repo_root(repo_root, root, (int)sizeof(root)))
        return false;

    char default_branch[WORKTREE_NAME_MAX];
    resolve_default_branch(root, default_branch, (int)sizeof(default_branch));
    if (!default_branch[0])
        return false;

    if (!cleanup_candidate_is_safe(root, default_branch,
                                   candidate->path, candidate->branch,
                                   exclude_paths, exclude_count))
        return false;

    const char *remove_argv[7] = { "worktree", "remove", candidate->path,
                                   NULL, NULL, NULL, NULL };
    if (git_run(root, remove_argv) != 0)
        return false;

    const char *branch_argv[7] = { "branch", "-d", candidate->branch,
                                   NULL, NULL, NULL, NULL };
    git_run(root, branch_argv);  // best-effort: the worktree is already removed.

    return true;
}
