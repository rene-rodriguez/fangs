// workspace_git_status — background git dirty-count sampler for rail badges.
#include "workspace_git_status.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    uint64_t pane_id;
    int count;
    char root[WORKSPACE_GIT_STATUS_PATH_MAX];
} WorkspaceGitStatusEntry;

struct WorkspaceGitStatusSampler {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t thread;
    int thread_started;
    int stop;

    unsigned long generation;
    unsigned long sampled_generation;
    int interval_ms;

    WorkspaceGitStatusTarget targets[WORKSPACE_GIT_STATUS_MAX_TARGETS];
    int target_count;

    WorkspaceGitStatusEntry entries[WORKSPACE_GIT_STATUS_MAX_TARGETS];
    int entry_count;
};

static void deadline_after_ms(int ms, struct timespec *ts)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static int git_capture_raw_args(const char *cwd, const char *const *argv,
                                char **out, size_t *out_len)
{
    if (!cwd || !cwd[0] || !out || !out_len)
        return -1;
    *out = NULL;
    *out_len = 0;

    int pipefd[2];
    if (pipe(pipefd) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        setenv("GIT_OPTIONAL_LOCKS", "0", 1);

        execlp("git", "git", "-C", cwd,
               argv[0], argv[1], argv[2], argv[3],
               (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    char tmp[4096];
    size_t len = 0;
    size_t cap = 0;
    char *buf = NULL;

    for (;;) {
        ssize_t n = read(pipefd[0], tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return -1;
        }
        if (n == 0)
            break;
        if (len + (size_t)n > cap) {
            size_t next = cap ? cap * 2 : 8192;
            while (next < len + (size_t)n)
                next *= 2;
            char *nb = realloc(buf, next);
            if (!nb) {
                free(buf);
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return -1;
            }
            buf = nb;
            cap = next;
        }
        memcpy(buf + len, tmp, (size_t)n);
        len += (size_t)n;
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(buf);
        return -1;
    }

    *out = buf;
    *out_len = len;
    return 0;
}

static int git_capture_status(const char *cwd, char **out, size_t *out_len)
{
    const char *argv[4] = {
        "status", "--porcelain=v1", "-z", "--untracked-files=all",
    };
    return git_capture_raw_args(cwd, argv, out, out_len);
}

static bool workspace_git_status_root_path(const char *cwd,
                                           char *out, int out_size)
{
    if (!out || out_size <= 0)
        return false;
    out[0] = '\0';

    const char *argv[4] = {
        "rev-parse", "--show-toplevel", NULL, NULL,
    };
    char *buf = NULL;
    size_t len = 0;
    if (git_capture_raw_args(cwd, argv, &buf, &len) != 0)
        return false;

    while (len > 0 && (buf[len - 1] == '\0' || buf[len - 1] == '\n'
                       || buf[len - 1] == '\r' || buf[len - 1] == ' '))
        len--;

    if (len == 0 || len >= (size_t)out_size) {
        free(buf);
        return false;
    }

    memcpy(out, buf, len);
    out[len] = '\0';
    free(buf);
    return true;
}

static int count_porcelain_z(const char *buf, size_t len)
{
    int count = 0;
    size_t i = 0;

    while (i + 3 <= len) {
        char x = buf[i];
        char y = buf[i + 1];
        if (buf[i + 2] != ' ')
            break;

        bool has_second_path = (x == 'R' || x == 'C' || y == 'R' || y == 'C');
        i += 3;
        while (i < len && buf[i] != '\0')
            i++;
        if (i < len)
            i++;

        if (has_second_path) {
            while (i < len && buf[i] != '\0')
                i++;
            if (i < len)
                i++;
        }

        count++;
    }

    return count;
}

int workspace_git_status_count_path(const char *cwd)
{
    char *buf = NULL;
    size_t len = 0;
    if (git_capture_status(cwd, &buf, &len) != 0)
        return -1;
    int count = count_porcelain_z(buf, len);
    free(buf);
    return count;
}

static bool targets_equal(const WorkspaceGitStatusSampler *sampler,
                          const WorkspaceGitStatusTarget *targets,
                          int target_count)
{
    if (target_count > WORKSPACE_GIT_STATUS_MAX_TARGETS)
        target_count = WORKSPACE_GIT_STATUS_MAX_TARGETS;
    if (sampler->target_count != target_count)
        return false;
    for (int i = 0; i < target_count; i++) {
        if (sampler->targets[i].pane_id != targets[i].pane_id)
            return false;
        if (strcmp(sampler->targets[i].cwd, targets[i].cwd) != 0)
            return false;
    }
    return true;
}

static void publish_counts(WorkspaceGitStatusSampler *sampler,
                           const WorkspaceGitStatusTarget *targets,
                           const int *counts, const char roots[][WORKSPACE_GIT_STATUS_PATH_MAX],
                           int target_count)
{
    sampler->entry_count = 0;
    for (int i = 0; i < target_count && i < WORKSPACE_GIT_STATUS_MAX_TARGETS; i++) {
        if (counts[i] < 0 || targets[i].pane_id == 0)
            continue;
        int out = sampler->entry_count++;
        sampler->entries[out].pane_id = targets[i].pane_id;
        sampler->entries[out].count = counts[i];
        snprintf(sampler->entries[out].root, sizeof(sampler->entries[out].root),
                 "%s", roots[i]);
    }
}

static void *sampler_worker(void *arg)
{
    WorkspaceGitStatusSampler *sampler = (WorkspaceGitStatusSampler *)arg;
    WorkspaceGitStatusTarget *targets =
        calloc(WORKSPACE_GIT_STATUS_MAX_TARGETS, sizeof(*targets));
    int *counts = calloc(WORKSPACE_GIT_STATUS_MAX_TARGETS, sizeof(*counts));
    char (*roots)[WORKSPACE_GIT_STATUS_PATH_MAX] =
        calloc(WORKSPACE_GIT_STATUS_MAX_TARGETS, sizeof(*roots));
    char (*sampled_roots)[WORKSPACE_GIT_STATUS_PATH_MAX] =
        calloc(WORKSPACE_GIT_STATUS_MAX_TARGETS, sizeof(*sampled_roots));
    int *sampled_counts =
        calloc(WORKSPACE_GIT_STATUS_MAX_TARGETS, sizeof(*sampled_counts));

    if (!targets || !counts || !roots || !sampled_roots || !sampled_counts)
        goto done;

    for (;;) {
        int target_count = 0;

        pthread_mutex_lock(&sampler->mu);
        while (!sampler->stop
               && sampler->sampled_generation == sampler->generation) {
            struct timespec ts;
            deadline_after_ms(sampler->interval_ms, &ts);
            int rc = pthread_cond_timedwait(&sampler->cv, &sampler->mu, &ts);
            if (rc == ETIMEDOUT)
                break;
        }

        if (sampler->stop) {
            pthread_mutex_unlock(&sampler->mu);
            break;
        }

        sampler->sampled_generation = sampler->generation;
        target_count = sampler->target_count;
        for (int i = 0; i < target_count; i++)
            targets[i] = sampler->targets[i];
        pthread_mutex_unlock(&sampler->mu);

        int sampled_count = 0;

        for (int i = 0; i < target_count; i++) {
            counts[i] = -1;
            roots[i][0] = '\0';

            if (!workspace_git_status_root_path(targets[i].cwd,
                                                roots[i], (int)sizeof(roots[i])))
                continue;

            int found = -1;
            for (int j = 0; j < sampled_count; j++) {
                if (strcmp(sampled_roots[j], roots[i]) == 0) {
                    found = j;
                    break;
                }
            }

            if (found >= 0) {
                counts[i] = sampled_counts[found];
                continue;
            }

            counts[i] = workspace_git_status_count_path(roots[i]);
            if (sampled_count < WORKSPACE_GIT_STATUS_MAX_TARGETS) {
                snprintf(sampled_roots[sampled_count],
                         sizeof(sampled_roots[sampled_count]), "%s", roots[i]);
                sampled_counts[sampled_count] = counts[i];
                sampled_count++;
            }
        }

        pthread_mutex_lock(&sampler->mu);
            if (!sampler->stop)
                publish_counts(sampler, targets, counts, roots, target_count);
            pthread_mutex_unlock(&sampler->mu);
    }

done:
    free(sampled_counts);
    free(sampled_roots);
    free(roots);
    free(counts);
    free(targets);
    return NULL;
}

WorkspaceGitStatusSampler *workspace_git_status_start(void)
{
    WorkspaceGitStatusSampler *sampler = calloc(1, sizeof(*sampler));
    if (!sampler)
        return NULL;

    pthread_mutex_init(&sampler->mu, NULL);
    pthread_cond_init(&sampler->cv, NULL);
    sampler->interval_ms = WORKSPACE_GIT_STATUS_INTERVAL_MS;
    sampler->generation = 1;
    sampler->sampled_generation = 0;

    if (pthread_create(&sampler->thread, NULL, sampler_worker, sampler) != 0) {
        pthread_cond_destroy(&sampler->cv);
        pthread_mutex_destroy(&sampler->mu);
        free(sampler);
        return NULL;
    }

    sampler->thread_started = 1;
    return sampler;
}

void workspace_git_status_stop(WorkspaceGitStatusSampler *sampler)
{
    if (!sampler)
        return;

    pthread_mutex_lock(&sampler->mu);
    sampler->stop = 1;
    pthread_cond_signal(&sampler->cv);
    pthread_mutex_unlock(&sampler->mu);

    if (sampler->thread_started)
        pthread_join(sampler->thread, NULL);

    pthread_cond_destroy(&sampler->cv);
    pthread_mutex_destroy(&sampler->mu);
    free(sampler);
}

void workspace_git_status_set_targets(WorkspaceGitStatusSampler *sampler,
                                      const WorkspaceGitStatusTarget *targets,
                                      int target_count)
{
    if (!sampler)
        return;
    if (target_count < 0)
        target_count = 0;
    if (target_count > WORKSPACE_GIT_STATUS_MAX_TARGETS)
        target_count = WORKSPACE_GIT_STATUS_MAX_TARGETS;

    pthread_mutex_lock(&sampler->mu);
    if ((target_count == 0 || targets)
        && !targets_equal(sampler, targets, target_count)) {
        sampler->target_count = target_count;
        for (int i = 0; i < target_count; i++) {
            sampler->targets[i].pane_id = targets[i].pane_id;
            snprintf(sampler->targets[i].cwd, sizeof(sampler->targets[i].cwd),
                     "%s", targets[i].cwd);
        }
        sampler->generation++;
        pthread_cond_signal(&sampler->cv);
    }
    pthread_mutex_unlock(&sampler->mu);
}

int workspace_git_status_count_for(WorkspaceGitStatusSampler *sampler,
                                   uint64_t pane_id)
{
    if (!sampler || pane_id == 0)
        return 0;

    int count = 0;
    pthread_mutex_lock(&sampler->mu);
    for (int i = 0; i < sampler->entry_count; i++) {
        if (sampler->entries[i].pane_id == pane_id) {
            count = sampler->entries[i].count;
            break;
        }
    }
    pthread_mutex_unlock(&sampler->mu);
    return count;
}

int workspace_git_status_sum_unique_for(WorkspaceGitStatusSampler *sampler,
                                        const uint64_t *pane_ids,
                                        int pane_count)
{
    if (!sampler || !pane_ids || pane_count <= 0)
        return 0;

    int total = 0;
    char seen[WORKSPACE_GIT_STATUS_MAX_TARGETS][WORKSPACE_GIT_STATUS_PATH_MAX];
    int seen_count = 0;

    pthread_mutex_lock(&sampler->mu);
    for (int p = 0; p < pane_count; p++) {
        if (pane_ids[p] == 0)
            continue;

        const WorkspaceGitStatusEntry *entry = NULL;
        for (int i = 0; i < sampler->entry_count; i++) {
            if (sampler->entries[i].pane_id == pane_ids[p]) {
                entry = &sampler->entries[i];
                break;
            }
        }
        if (!entry || entry->count <= 0)
            continue;

        bool already_seen = false;
        for (int i = 0; i < seen_count; i++) {
            if (strcmp(seen[i], entry->root) == 0) {
                already_seen = true;
                break;
            }
        }
        if (already_seen)
            continue;

        total += entry->count;
        if (seen_count < WORKSPACE_GIT_STATUS_MAX_TARGETS) {
            snprintf(seen[seen_count], sizeof(seen[seen_count]), "%s", entry->root);
            seen_count++;
        }
    }
    pthread_mutex_unlock(&sampler->mu);
    return total;
}
