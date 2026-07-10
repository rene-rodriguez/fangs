// crash_log — best-effort crash telemetry. See crash_log.h.
#include "crash_log.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CRASH_LOG_PATH_MAX 1024
#define CRASH_BACKTRACE_MAX 64

static char g_crash_log_path[CRASH_LOG_PATH_MAX];

static const int CRASH_SIGNALS[] = { SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE };
#define CRASH_SIGNAL_COUNT (int)(sizeof(CRASH_SIGNALS) / sizeof(CRASH_SIGNALS[0]))

// The handler below must stick to async-signal-safe calls only (open, write,
// close, time, raise, backtrace, backtrace_symbols_fd) — no malloc, no
// snprintf/printf, no backtrace_symbols (which does malloc). These two
// helpers build the log line by hand instead.
static int append_uint(char *buf, int cap, int len, unsigned long v)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (n > 0 && len < cap)
        buf[len++] = tmp[--n];
    return len;
}

static int append_str(char *buf, int cap, int len, const char *s)
{
    while (*s && len < cap)
        buf[len++] = *s++;
    return len;
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    default:      return "unknown";
    }
}

static void crash_log_handler(int sig)
{
    if (g_crash_log_path[0] != '\0') {
        int fd = open(g_crash_log_path, O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd >= 0) {
            char line[256];
            int len = 0;
            len = append_str(line, (int)sizeof(line), len, "[fangs] crashed: ");
            len = append_str(line, (int)sizeof(line), len, signal_name(sig));
            len = append_str(line, (int)sizeof(line), len, " (signal ");
            len = append_uint(line, (int)sizeof(line), len, (unsigned long)sig);
            len = append_str(line, (int)sizeof(line), len, ") at epoch ");
            len = append_uint(line, (int)sizeof(line), len, (unsigned long)time(NULL));
            len = append_str(line, (int)sizeof(line), len, "\n");
            ssize_t unused = write(fd, line, (size_t)len);
            (void)unused;

            void *frames[CRASH_BACKTRACE_MAX];
            int nframes = backtrace(frames, CRASH_BACKTRACE_MAX);
            backtrace_symbols_fd(frames, nframes, fd);

            unused = write(fd, "\n", 1);
            (void)unused;
            close(fd);
        }
    }

    // SA_RESETHAND (set in crash_log_install) already restored the default
    // disposition for this signal before invoking us, so re-raising here
    // terminates the process exactly as it would have without this handler.
    raise(sig);
}

void crash_log_install(const char *log_path)
{
    if (log_path)
        snprintf(g_crash_log_path, sizeof(g_crash_log_path), "%s", log_path);
    else
        g_crash_log_path[0] = '\0';

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = crash_log_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND; // avoid recursing if the handler itself crashes

    for (int i = 0; i < CRASH_SIGNAL_COUNT; i++)
        sigaction(CRASH_SIGNALS[i], &sa, NULL);
}
