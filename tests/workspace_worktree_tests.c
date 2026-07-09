#include "workspace_worktree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_FALSE(expr) do { if ((expr)) { fprintf(stderr, "FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static int run_git(const char *repo, const char *a, const char *b,
                   const char *c, const char *d)
{
    pid_t pid = fork();
    if (pid == 0) {
        if (repo)
            execlp("git", "git", "-C", repo, a, b, c, d, (char *)NULL);
        else
            execlp("git", "git", a, b, c, d, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 127;
}

static void test_sanitize_name(void)
{
    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_sanitize_name("feature/agent ui", out, sizeof(out)));
    EXPECT_STR(out, "feature-agent-ui");
    EXPECT_TRUE(workspace_worktree_sanitize_name("---main---", out, sizeof(out)));
    EXPECT_STR(out, "main");
    EXPECT_FALSE(workspace_worktree_sanitize_name("///", out, sizeof(out)));
}

static void test_candidate_generation(void)
{
    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_candidate("main", 0, out, sizeof(out)));
    EXPECT_STR(out, "main-agent");
    EXPECT_TRUE(workspace_worktree_candidate("feature/agent-ui", 2, out, sizeof(out)));
    EXPECT_STR(out, "feature-agent-ui-agent-2");
    EXPECT_TRUE(workspace_worktree_candidate("", 0, out, sizeof(out)));
    EXPECT_STR(out, "worktree-agent");
}

static void test_create_rejects_non_repo(void)
{
    WorkspaceWorktreeResult r;
    EXPECT_FALSE(workspace_worktree_create("/tmp", &r));
    EXPECT_TRUE(r.error[0] != '\0');
}

static void test_create_local_worktree(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult r;
    EXPECT_TRUE(workspace_worktree_create(root, &r));
    EXPECT_STR(r.branch, "main-agent");
    EXPECT_TRUE(strstr(r.path, "/.worktrees/main-agent") != NULL);

    char dotgit[1024];
    snprintf(dotgit, sizeof(dotgit), "%s/.git", r.path);
    struct stat st;
    EXPECT_TRUE(stat(dotgit, &st) == 0);
}

static void test_create_uses_spec_suffix_for_second_worktree(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-suffix-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult first;
    WorkspaceWorktreeResult second;
    EXPECT_TRUE(workspace_worktree_create(root, &first));
    EXPECT_TRUE(workspace_worktree_create(root, &second));
    EXPECT_STR(first.branch, "main-agent");
    EXPECT_STR(second.branch, "main-agent-2");
    EXPECT_TRUE(strstr(second.path, "/.worktrees/main-agent-2") != NULL);
}

// Commit an empty file named `name` on whatever branch is currently checked
// out in `path` (repo root or a worktree path).
static void commit_file(const char *path, const char *name)
{
    char full[1024];
    snprintf(full, sizeof(full), "%s/%s", path, name);
    FILE *f = fopen(full, "w");
    if (f) {
        fputs("x\n", f);
        fclose(f);
    }
    run_git(path, "add", "-A", NULL, NULL);
    run_git(path, "commit", "-m", "add file", NULL);
}

static bool has_candidate(const WorkspaceWorktreeCandidate *c, int n,
                          const char *branch)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(c[i].branch, branch) == 0)
            return true;
    }
    return false;
}

static bool branch_exists(const char *root, const char *branch)
{
    char ref[600];
    snprintf(ref, sizeof(ref), "refs/heads/%s", branch);
    return run_git(root, "show-ref", "--verify", "--quiet", ref) == 0;
}

static void test_cleanup_candidates_merged_dirty_and_unmerged(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-cleanup-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    // merged: branch fully merged back into main, clean working tree.
    WorkspaceWorktreeResult merged;
    EXPECT_TRUE(workspace_worktree_create(root, &merged));
    commit_file(merged.path, "merged.txt");
    EXPECT_TRUE(run_git(root, "merge", merged.branch, NULL, NULL) == 0);

    // dirty: merged branch, but an uncommitted change in the worktree.
    WorkspaceWorktreeResult dirty;
    EXPECT_TRUE(workspace_worktree_create(root, &dirty));
    commit_file(dirty.path, "dirty.txt");
    EXPECT_TRUE(run_git(root, "merge", dirty.branch, NULL, NULL) == 0);
    char dirty_scratch[1024];
    snprintf(dirty_scratch, sizeof(dirty_scratch), "%s/scratch.txt", dirty.path);
    FILE *df = fopen(dirty_scratch, "w");
    if (df) { fputs("uncommitted\n", df); fclose(df); }

    // unmerged: branch has a commit that was never merged back.
    WorkspaceWorktreeResult unmerged;
    EXPECT_TRUE(workspace_worktree_create(root, &unmerged));
    commit_file(unmerged.path, "unmerged.txt");

    WorkspaceWorktreeCandidate cand[WORKTREE_CLEANUP_MAX];
    int n = workspace_worktree_find_cleanup_candidates(root, NULL, 0, cand, WORKTREE_CLEANUP_MAX);

    EXPECT_TRUE(has_candidate(cand, n, merged.branch));
    EXPECT_FALSE(has_candidate(cand, n, dirty.branch));
    EXPECT_FALSE(has_candidate(cand, n, unmerged.branch));

    // Excluding the merged worktree's own path removes it from the list.
    const char *exclude[1] = { merged.path };
    int n2 = workspace_worktree_find_cleanup_candidates(root, exclude, 1, cand, WORKTREE_CLEANUP_MAX);
    EXPECT_FALSE(has_candidate(cand, n2, merged.branch));

    // Cleanup actually removes the worktree directory and the branch.
    WorkspaceWorktreeCandidate to_remove;
    snprintf(to_remove.path, sizeof(to_remove.path), "%s", merged.path);
    snprintf(to_remove.branch, sizeof(to_remove.branch), "%s", merged.branch);
    EXPECT_TRUE(workspace_worktree_cleanup(root, NULL, 0, &to_remove));

    struct stat st;
    EXPECT_FALSE(stat(merged.path, &st) == 0);
    char ref[600];
    snprintf(ref, sizeof(ref), "refs/heads/%s", merged.branch);
    EXPECT_TRUE(run_git(root, "show-ref", "--verify", "--quiet", ref) != 0);
}

static void test_cleanup_excludes_open_subdirectory(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-subdir-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult merged;
    EXPECT_TRUE(workspace_worktree_create(root, &merged));
    commit_file(merged.path, "merged.txt");
    EXPECT_TRUE(run_git(root, "merge", merged.branch, NULL, NULL) == 0);

    char subdir[1024];
    snprintf(subdir, sizeof(subdir), "%s/nested", merged.path);
    mkdir(subdir, 0700);

    WorkspaceWorktreeCandidate cand[WORKTREE_CLEANUP_MAX];
    const char *exclude[1] = { subdir };
    int n = workspace_worktree_find_cleanup_candidates(root, exclude, 1,
                                                       cand, WORKTREE_CLEANUP_MAX);
    EXPECT_FALSE(has_candidate(cand, n, merged.branch));

    WorkspaceWorktreeCandidate blocked;
    snprintf(blocked.path, sizeof(blocked.path), "%s", merged.path);
    snprintf(blocked.branch, sizeof(blocked.branch), "%s", merged.branch);
    EXPECT_FALSE(workspace_worktree_cleanup(root, exclude, 1, &blocked));

    struct stat st;
    EXPECT_TRUE(stat(merged.path, &st) == 0);
    EXPECT_TRUE(branch_exists(root, merged.branch));
}

static void test_cleanup_ignores_worktree_outside_repo_worktrees_dir(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-prefix-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    char outside_parent[512];
    snprintf(outside_parent, sizeof(outside_parent),
             "/tmp/fangs-external-worktrees-%ld", (long)getpid());
    mkdir(outside_parent, 0700);
    char outside_worktrees[768];
    snprintf(outside_worktrees, sizeof(outside_worktrees),
             "%s/.worktrees", outside_parent);
    mkdir(outside_worktrees, 0700);
    char outside_path[1024];
    snprintf(outside_path, sizeof(outside_path),
             "%s/external-agent", outside_worktrees);

    EXPECT_TRUE(run_git(root, "branch", "external-agent", NULL, NULL) == 0);
    EXPECT_TRUE(run_git(root, "worktree", "add", outside_path, "external-agent") == 0);
    commit_file(outside_path, "external.txt");
    EXPECT_TRUE(run_git(root, "merge", "external-agent", NULL, NULL) == 0);

    WorkspaceWorktreeCandidate cand[WORKTREE_CLEANUP_MAX];
    int n = workspace_worktree_find_cleanup_candidates(root, NULL, 0,
                                                       cand, WORKTREE_CLEANUP_MAX);
    EXPECT_FALSE(has_candidate(cand, n, "external-agent"));
}

static void test_cleanup_refuses_stale_dirty_candidate(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-stale-dirty-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult merged;
    EXPECT_TRUE(workspace_worktree_create(root, &merged));
    commit_file(merged.path, "merged.txt");
    EXPECT_TRUE(run_git(root, "merge", merged.branch, NULL, NULL) == 0);

    WorkspaceWorktreeCandidate stale;
    snprintf(stale.path, sizeof(stale.path), "%s", merged.path);
    snprintf(stale.branch, sizeof(stale.branch), "%s", merged.branch);

    char scratch[1024];
    snprintf(scratch, sizeof(scratch), "%s/scratch.txt", merged.path);
    FILE *sf = fopen(scratch, "w");
    if (sf) { fputs("dirty\n", sf); fclose(sf); }

    EXPECT_FALSE(workspace_worktree_cleanup(root, NULL, 0, &stale));

    struct stat st;
    EXPECT_TRUE(stat(merged.path, &st) == 0);
    EXPECT_TRUE(branch_exists(root, merged.branch));
}

static void test_cleanup_refuses_stale_unmerged_candidate(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-stale-unmerged-test-%ld", (long)getpid());
    mkdir(root, 0700);

    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    WorkspaceWorktreeResult merged;
    EXPECT_TRUE(workspace_worktree_create(root, &merged));
    commit_file(merged.path, "merged.txt");
    EXPECT_TRUE(run_git(root, "merge", merged.branch, NULL, NULL) == 0);

    WorkspaceWorktreeCandidate stale;
    snprintf(stale.path, sizeof(stale.path), "%s", merged.path);
    snprintf(stale.branch, sizeof(stale.branch), "%s", merged.branch);

    commit_file(merged.path, "unmerged-after-confirm.txt");

    EXPECT_FALSE(workspace_worktree_cleanup(root, NULL, 0, &stale));

    struct stat st;
    EXPECT_TRUE(stat(merged.path, &st) == 0);
    EXPECT_TRUE(branch_exists(root, merged.branch));
}

static void test_default_branch_prefers_origin_head(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char remote[512];
    snprintf(remote, sizeof(remote), "/tmp/fangs-worktree-default-branch-remote-%ld", (long)getpid());
    mkdir(remote, 0700);
    EXPECT_TRUE(run_git(remote, "init", "-b", "trunk", NULL) == 0);
    EXPECT_TRUE(run_git(remote, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(remote, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(remote, "commit", "--allow-empty", "-m", "init") == 0);

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-default-branch-test-%ld", (long)getpid());
    EXPECT_TRUE(run_git(NULL, "clone", remote, root, NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);

    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_default_branch(root, out, sizeof(out)));
    EXPECT_STR(out, "trunk");
}

static void test_default_branch_falls_back_to_main(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-worktree-default-branch-main-test-%ld", (long)getpid());
    mkdir(root, 0700);
    EXPECT_TRUE(run_git(root, "init", "-b", "main", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.email", "fangs@example.test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "config", "user.name", "Fangs Test", NULL) == 0);
    EXPECT_TRUE(run_git(root, "commit", "--allow-empty", "-m", "init") == 0);

    char out[WORKTREE_NAME_MAX];
    EXPECT_TRUE(workspace_worktree_default_branch(root, out, sizeof(out)));
    EXPECT_STR(out, "main");
}

static void test_default_branch_rejects_non_repo(void)
{
    char out[WORKTREE_NAME_MAX];
    EXPECT_FALSE(workspace_worktree_default_branch("/tmp", out, sizeof(out)));
    EXPECT_STR(out, "");
}

int main(void)
{
    test_sanitize_name();
    test_candidate_generation();
    test_create_rejects_non_repo();
    test_create_local_worktree();
    test_create_uses_spec_suffix_for_second_worktree();
    test_cleanup_candidates_merged_dirty_and_unmerged();
    test_cleanup_excludes_open_subdirectory();
    test_cleanup_ignores_worktree_outside_repo_worktrees_dir();
    test_cleanup_refuses_stale_dirty_candidate();
    test_cleanup_refuses_stale_unmerged_candidate();
    test_default_branch_prefers_origin_head();
    test_default_branch_falls_back_to_main();
    test_default_branch_rejects_non_repo();
    return failures ? 1 : 0;
}
