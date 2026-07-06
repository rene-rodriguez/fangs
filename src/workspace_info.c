#include "workspace_info.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void workspace_cwd_label(const char *cwd, const char *home, char *out, int out_size)
{
    if (!cwd || !*cwd || out_size <= 0) {
        if (out_size > 0) out[0] = '\0';
        return;
    }

    if (home && strcmp(cwd, home) == 0) {
        snprintf(out, out_size, "~");
        return;
    }

    if (strcmp(cwd, "/") == 0) {
        snprintf(out, out_size, "/");
        return;
    }

    /* Find the last '/' */
    const char *last = strrchr(cwd, '/');
    if (last && *(last + 1)) {
        snprintf(out, out_size, "%s", last + 1);
    } else {
        snprintf(out, out_size, "%s", cwd);
    }
}

bool workspace_git_branch(const char *cwd, char *out, int out_size)
{
    if (!cwd || !*cwd || out_size <= 0) {
        if (out_size > 0) out[0] = '\0';
        return false;
    }

    char dotgit[4096];
    char resolved[4096];
    char head[4096];
    struct stat st;

    /* Walk upward from cwd looking for .git */
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", cwd);

    while (1) {
        snprintf(dotgit, sizeof(dotgit), "%s/.git", dir);

        if (stat(dotgit, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                /* .git is a directory — read .git/HEAD directly */
                snprintf(head, sizeof(head), "%s/HEAD", dotgit);
                break;
            } else if (S_ISREG(st.st_mode)) {
                /* .git is a file — parse gitdir: <path> */
                FILE *f = fopen(dotgit, "r");
                if (!f) goto fail;
                char line[4096];
                if (!fgets(line, (int)sizeof(line), f)) {
                    fclose(f);
                    goto fail;
                }
                fclose(f);

                if (strncmp(line, "gitdir: ", 8) != 0) goto fail;
                /* Trim trailing whitespace */
                char *nl = strchr(line, '\n');
                if (nl) *nl = '\0';
                char *cr = strchr(line, '\r');
                if (cr) *cr = '\0';

                /* Handle relative gitdir paths by resolving relative to dir */
                const char *gitdir_path = line + 8;
                if (gitdir_path[0] == '/') {
                    snprintf(resolved, sizeof(resolved), "%s", gitdir_path);
                } else {
                    snprintf(resolved, sizeof(resolved), "%s/%s", dir, gitdir_path);
                }

                snprintf(head, sizeof(head), "%s/HEAD", resolved);
                break;
            }
        }

        /* Walk up to parent */
        char *parent_end = strrchr(dir, '/');
        if (!parent_end || parent_end == dir) {
            /* Reached root without finding .git */
            goto fail;
        }
        *parent_end = '\0';
    }

    /* Read HEAD */
    FILE *f = fopen(head, "r");
    if (!f) goto fail;
    char content[4096];
    if (!fgets(content, (int)sizeof(content), f)) {
        fclose(f);
        goto fail;
    }
    fclose(f);

    /* Trim trailing whitespace */
    size_t len = strlen(content);
    while (len > 0 && (content[len - 1] == '\n' || content[len - 1] == '\r')) {
        content[--len] = '\0';
    }

    if (strncmp(content, "ref: refs/heads/", 16) == 0) {
        snprintf(out, out_size, "%s", content + 16);
        return true;
    }

    /* Detached HEAD — show first 7 characters of commit hash */
    if (len >= 7) {
        snprintf(out, out_size, "%.7s", content);
        return true;
    }

fail:
    if (out_size > 0) out[0] = '\0';
    return false;
}
