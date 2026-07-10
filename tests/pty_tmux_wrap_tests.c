#include "pty.h"

#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static bool tmux_available(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); }
        execlp("tmux", "tmux", "-V", (char *)NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Polls pty_read() until `needle` shows up in the accumulated output or
// `timeout_ms` elapses. Returns the accumulated bytes read (NUL-terminated).
static void collect_output(int pty_fd, const char *needle, int timeout_ms,
                          char *out, size_t out_size)
{
    size_t len = 0;
    out[0] = '\0';
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000
                         + (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms > timeout_ms)
            return;

        char buf[4096];
        ssize_t n = read(pty_fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            size_t copy = (size_t)n;
            if (len + copy >= out_size)
                copy = out_size - 1 - len;
            memcpy(out + len, buf, copy);
            len += copy;
            out[len] = '\0';
            if (needle && strstr(out, needle))
                return;
        } else {
            usleep(20000);
        }
    }
}

// The pty echoes typed input back verbatim before the shell even runs it,
// so the stop/assertion needle must only be able to appear in the shell's
// *expanded* output, never in the raw command text. "%s" only becomes
// "none" or a real path once printf actually substitutes ${TMUX:-none} --
// the literal source line (as echoed) still reads "RESULT=[%s]".
static const char *TMUX_PROBE_CMD = "printf 'RESULT=[%s]\\n' \"${TMUX:-none}\"\n";

static void test_plain_shell_when_disabled(void)
{
    setenv("SHELL", "/bin/sh", 1);
    pty_set_tmux_wrap(false);

    pid_t child = 0;
    int fd = pty_spawn(&child, 80, 24, 8, 16, "/tmp");
    EXPECT_TRUE(fd >= 0);
    if (fd < 0)
        return;

    write(fd, TMUX_PROBE_CMD, strlen(TMUX_PROBE_CMD));

    char out[8192];
    collect_output(fd, "RESULT=[none]", 2000, out, sizeof(out));
    EXPECT_TRUE(strstr(out, "RESULT=[none]") != NULL);

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(fd);
}

static void test_tmux_wrap_puts_session_inside_tmux(void)
{
    if (!tmux_available()) {
        fprintf(stderr, "SKIP: tmux not installed, skipping tmux_wrap test\n");
        return;
    }

    setenv("SHELL", "/bin/sh", 1);
    pty_set_tmux_wrap(true);

    pid_t child = 0;
    int fd = pty_spawn(&child, 80, 24, 8, 16, "/tmp");
    EXPECT_TRUE(fd >= 0);
    if (fd < 0)
        return;

    write(fd, TMUX_PROBE_CMD, strlen(TMUX_PROBE_CMD));

    char out[8192];
    // tmux sets $TMUX to "<socket-path>,<server-pid>,<session-index>" for
    // every command running inside a session -- always starts with '/'.
    collect_output(fd, "RESULT=[/", 3000, out, sizeof(out));
    EXPECT_TRUE(strstr(out, "RESULT=[none]") == NULL);
    EXPECT_TRUE(strstr(out, "RESULT=[/") != NULL);

    char kill_cmd[256];
    snprintf(kill_cmd, sizeof(kill_cmd), "tmux kill-session -t fangs-tmp-%d", (int)child);
    system(kill_cmd);

    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(fd);

    pty_set_tmux_wrap(false);
}

int main(void)
{
    test_plain_shell_when_disabled();
    test_tmux_wrap_puts_session_inside_tmux();
    return failures ? 1 : 0;
}
