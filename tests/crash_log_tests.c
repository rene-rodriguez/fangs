#include "crash_log.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static bool read_whole_file(const char *path, char *out, size_t out_size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    size_t n = fread(out, 1, out_size - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

// Forks a child that installs the crash handler and deliberately crashes
// with `sig`, then checks: (1) the child still dies from that signal, same
// as it would without our handler (SA_RESETHAND did its job), and (2) the
// log file recorded the crash.
static void run_crash_case(const char *log_path, int sig, const char *expect_name)
{
    unlink(log_path);

    pid_t pid = fork();
    EXPECT_TRUE(pid >= 0);
    if (pid == 0) {
        crash_log_install(log_path);
        raise(sig);
        _exit(111); // unreachable if the handler correctly re-raises
    }

    int status = 0;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));
    if (WIFSIGNALED(status))
        EXPECT_TRUE(WTERMSIG(status) == sig);

    char contents[4096] = "";
    EXPECT_TRUE(read_whole_file(log_path, contents, sizeof(contents)));
    EXPECT_TRUE(strstr(contents, "crashed") != NULL);
    EXPECT_TRUE(strstr(contents, expect_name) != NULL);

    unlink(log_path);
}

static void test_segv_logs_and_still_terminates(void)
{
    run_crash_case("/tmp/fangs-crash-log-segv-test.log", SIGSEGV, "SIGSEGV");
}

static void test_abrt_logs_and_still_terminates(void)
{
    run_crash_case("/tmp/fangs-crash-log-abrt-test.log", SIGABRT, "SIGABRT");
}

static void test_disabled_when_no_path_set(void)
{
    const char *log_path = "/tmp/fangs-crash-log-disabled-test.log";
    unlink(log_path);

    pid_t pid = fork();
    EXPECT_TRUE(pid >= 0);
    if (pid == 0) {
        crash_log_install(NULL);
        raise(SIGSEGV);
        _exit(111);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);

    FILE *f = fopen(log_path, "r");
    EXPECT_TRUE(f == NULL); // nothing should have been written
    if (f) fclose(f);
}

int main(void)
{
    test_segv_logs_and_still_terminates();
    test_abrt_logs_and_still_terminates();
    test_disabled_when_no_path_set();
    return failures ? 1 : 0;
}
