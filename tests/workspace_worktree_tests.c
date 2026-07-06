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

int main(void)
{
    test_sanitize_name();
    test_candidate_generation();
    test_create_rejects_non_repo();
    test_create_local_worktree();
    return failures ? 1 : 0;
}
