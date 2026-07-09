#include "workspace_git_status.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_INT(actual, expected) do { \
    int a_ = (actual), e_ = (expected); \
    if (a_ != e_) { \
        fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, e_, a_); \
        failures++; \
    } \
} while (0)

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

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

static bool write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return false;
    fputs(text, f);
    fclose(f);
    return true;
}

static bool make_repo(char *root, int root_size, const char *name)
{
    snprintf(root, (size_t)root_size, "/tmp/fangs-git-status-%s-XXXXXX", name);
    if (!mkdtemp(root))
        return false;

    if (run_git(root, "init", "-b", "main", NULL) != 0)
        return false;
    if (run_git(root, "config", "user.email", "fangs@example.test", NULL) != 0)
        return false;
    if (run_git(root, "config", "user.name", "Fangs Test", NULL) != 0)
        return false;

    char tracked[512];
    snprintf(tracked, sizeof(tracked), "%s/tracked.txt", root);
    if (!write_file(tracked, "clean\n"))
        return false;
    if (run_git(root, "add", "tracked.txt", NULL, NULL) != 0)
        return false;
    return run_git(root, "commit", "-m", "init", NULL) == 0;
}

static void remove_repo(const char *root)
{
    const char *prefix = "/tmp/fangs-git-status-";
    if (!root || strncmp(root, prefix, strlen(prefix)) != 0)
        return;
    pid_t pid = fork();
    if (pid == 0) {
        execlp("rm", "rm", "-rf", root, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
}

static void test_count_path_reports_dirty_file_count(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    EXPECT_TRUE(make_repo(root, sizeof(root), "count"));

    EXPECT_INT(workspace_git_status_count_path(root), 0);

    char tracked[512];
    snprintf(tracked, sizeof(tracked), "%s/tracked.txt", root);
    EXPECT_TRUE(write_file(tracked, "changed\n"));

    char untracked[512];
    snprintf(untracked, sizeof(untracked), "%s/untracked.txt", root);
    EXPECT_TRUE(write_file(untracked, "new\n"));

    EXPECT_INT(workspace_git_status_count_path(root), 2);
    remove_repo(root);
}

static void test_count_path_rejects_non_repo(void)
{
    EXPECT_INT(workspace_git_status_count_path("/tmp"), -1);
}

static void test_sampler_publishes_latest_counts(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    EXPECT_TRUE(make_repo(root, sizeof(root), "sampler"));

    char untracked[512];
    snprintf(untracked, sizeof(untracked), "%s/agent-output.txt", root);
    EXPECT_TRUE(write_file(untracked, "new\n"));

    WorkspaceGitStatusSampler *sampler = workspace_git_status_start();
    EXPECT_TRUE(sampler != NULL);
    if (!sampler)
        return;

    WorkspaceGitStatusTarget target = {
        .pane_id = 42,
    };
    snprintf(target.cwd, sizeof(target.cwd), "%s", root);
    workspace_git_status_set_targets(sampler, &target, 1);

    int got = -1;
    for (int i = 0; i < 100; i++) {
        got = workspace_git_status_count_for(sampler, 42);
        if (got == 1)
            break;
        usleep(20000);
    }

    EXPECT_INT(got, 1);
    workspace_git_status_stop(sampler);
    remove_repo(root);
}

static void test_sampler_sums_unique_worktrees(void)
{
    if (run_git(NULL, "--version", NULL, NULL, NULL) != 0)
        return;

    char root[512];
    EXPECT_TRUE(make_repo(root, sizeof(root), "unique"));

    char subdir[512];
    snprintf(subdir, sizeof(subdir), "%s/src", root);
    mkdir(subdir, 0700);

    char untracked[512];
    snprintf(untracked, sizeof(untracked), "%s/agent-output.txt", root);
    EXPECT_TRUE(write_file(untracked, "new\n"));

    WorkspaceGitStatusSampler *sampler = workspace_git_status_start();
    EXPECT_TRUE(sampler != NULL);
    if (!sampler)
        return;

    WorkspaceGitStatusTarget targets[2] = {
        { .pane_id = 101 },
        { .pane_id = 102 },
    };
    snprintf(targets[0].cwd, sizeof(targets[0].cwd), "%s", root);
    snprintf(targets[1].cwd, sizeof(targets[1].cwd), "%s", subdir);
    workspace_git_status_set_targets(sampler, targets, 2);

    uint64_t ids[2] = { 101, 102 };
    int got = -1;
    for (int i = 0; i < 100; i++) {
        got = workspace_git_status_sum_unique_for(sampler, ids, 2);
        if (got == 1)
            break;
        usleep(20000);
    }

    EXPECT_INT(workspace_git_status_count_for(sampler, 101), 1);
    EXPECT_INT(workspace_git_status_count_for(sampler, 102), 1);
    EXPECT_INT(got, 1);
    workspace_git_status_stop(sampler);
    remove_repo(root);
}

int main(void)
{
    test_count_path_reports_dirty_file_count();
    test_count_path_rejects_non_repo();
    test_sampler_publishes_latest_counts();
    test_sampler_sums_unique_worktrees();
    return failures ? 1 : 0;
}
