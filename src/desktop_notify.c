// desktop_notify — macOS desktop notification helper for agent rings.
//
// On macOS, fires a notification via forked osascript.  On other platforms
// the notification function compiles to a no-op that returns false.
#include "desktop_notify.h"

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <stdlib.h>
#endif

bool desktop_notify_escape_applescript(const char *input, char *out, int out_size)
{
    if (!out || out_size <= 0) return false;
    if (!input) input = "";

    int wi = 0;
    for (const char *p = input; *p; p++) {
        if (*p == '\\' || *p == '"') {
            if (wi + 2 > out_size - 1) {
                out[wi] = '\0';
                return false;
            }
            out[wi++] = '\\';
            out[wi++] = *p;
        } else {
            if (wi + 1 > out_size - 1) {
                out[wi] = '\0';
                return false;
            }
            out[wi++] = *p;
        }
    }
    out[wi] = '\0';
    return true;
}

bool desktop_notify_agent_ring(const char *workspace, const char *message)
{
#ifndef __APPLE__
    (void)workspace;
    (void)message;
    return false;
#else
    if (!message || !message[0])
        message = "needs attention";
    if (!workspace || !workspace[0])
        workspace = "shell";

    char esc_workspace[256];
    char esc_message[1024];
    if (!desktop_notify_escape_applescript(workspace, esc_workspace,
                                           (int)sizeof(esc_workspace)))
        return false;
    if (!desktop_notify_escape_applescript(message, esc_message,
                                           (int)sizeof(esc_message)))
        return false;

    char script[2048];
    int n = snprintf(script, sizeof(script),
        "display notification \"%s\" with title \"Fangs\" subtitle \"%s\"",
        esc_message, esc_workspace);
    if (n < 0 || (size_t)n >= sizeof(script))
        return false;

    // Double-fork so the child is reparented to init and we don't block.
    pid_t pid1 = fork();
    if (pid1 < 0) return false;
    if (pid1 > 0) {
        // Parent: reap the first child immediately.
        int status;
        waitpid(pid1, &status, 0);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    // First child: fork again and exit.
    pid_t pid2 = fork();
    if (pid2 < 0)
        _exit(1);
    if (pid2 > 0)
        _exit(0);

    // Second child (reparented to init): exec osascript.
    // Restore default signal handling, close unnecessary fds.
    setsid();

    // Redirect stdin to /dev/null.
    FILE *null_in = fopen("/dev/null", "r");
    if (null_in) {
        dup2(fileno(null_in), STDIN_FILENO);
        fclose(null_in);
    }

    execlp("osascript", "osascript", "-e", script, (char *)NULL);
    _exit(127);
#endif
}
