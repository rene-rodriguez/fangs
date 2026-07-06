#include "workspace_info.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected '%s', got '%s'\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        failures++; \
    } \
} while (0)

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_FALSE(expr) do { \
    if ((expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void write_file(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(2); }
    fputs(body, f);
    fclose(f);
}

static void test_cwd_label(void)
{
    char out[128];
    workspace_cwd_label("/Users/rene/src/fangs", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "fangs");
    workspace_cwd_label("/Users/rene", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "~");
    workspace_cwd_label("/", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "/");
    workspace_cwd_label("", "/Users/rene", out, (int)sizeof(out));
    EXPECT_STR(out, "");
}

static void test_git_branch_directory(void)
{
    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-info-%ld", (long)getpid());
    mkdir(root, 0700);
    char git[512], sub[512], head[512];
    snprintf(git, sizeof(git), "%s/.git", root);
    snprintf(sub, sizeof(sub), "%s/src", root);
    snprintf(head, sizeof(head), "%s/HEAD", git);
    mkdir(git, 0700);
    mkdir(sub, 0700);
    write_file(head, "ref: refs/heads/main\n");

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(sub, out, (int)sizeof(out)));
    EXPECT_STR(out, "main");
}

static void test_git_detached_head(void)
{
    char root[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-detached-%ld", (long)getpid());
    mkdir(root, 0700);
    char git[512], head[512];
    snprintf(git, sizeof(git), "%s/.git", root);
    snprintf(head, sizeof(head), "%s/HEAD", git);
    mkdir(git, 0700);
    write_file(head, "0123456789abcdef0123456789abcdef01234567\n");

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(root, out, (int)sizeof(out)));
    EXPECT_STR(out, "0123456");
}

static void test_git_file_pointer(void)
{
    char root[512], real_git[512], head[512], dotgit[512];
    snprintf(root, sizeof(root), "/tmp/fangs-workspace-file-%ld", (long)getpid());
    snprintf(real_git, sizeof(real_git), "/tmp/fangs-workspace-real-git-%ld", (long)getpid());
    mkdir(root, 0700);
    mkdir(real_git, 0700);
    snprintf(head, sizeof(head), "%s/HEAD", real_git);
    snprintf(dotgit, sizeof(dotgit), "%s/.git", root);
    write_file(head, "ref: refs/heads/worktree\n");
    char body[1024];
    snprintf(body, sizeof(body), "gitdir: %s\n", real_git);
    write_file(dotgit, body);

    char out[128] = "";
    EXPECT_TRUE(workspace_git_branch(root, out, (int)sizeof(out)));
    EXPECT_STR(out, "worktree");
}

static void test_no_repo(void)
{
    char out[128] = "unchanged";
    EXPECT_FALSE(workspace_git_branch("/tmp", out, (int)sizeof(out)));
    EXPECT_STR(out, "");
}

int main(void)
{
    test_cwd_label();
    test_git_branch_directory();
    test_git_detached_head();
    test_git_file_pointer();
    test_no_repo();
    return failures ? 1 : 0;
}
