// desktop_notify — desktop notification helper for agent rings.
//
// On macOS, fires a notification via UNUserNotificationCenter (falling back to
// forked osascript). On Linux, fires via forked notify-send (libnotify),
// exec'd directly with no shell involved. On any other platform, or if the
// platform notifier binary/API is unavailable, this is a harmless no-op.
#include "desktop_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
// Implemented in desktop_notify_mac.m. UNUserNotificationCenter posts under
// Fangs' own bundle identity, so a click activates Fangs instead of Script
// Editor (osascript's `display notification` is permanently misattributed to
// Script Editor for click-through — no AppleScript string fixes that).
// Both return/no-op harmlessly when the process has no bundle identity
// (e.g. running the raw build/fangs binary instead of a packaged .app), so
// desktop_notify_agent_ring() below falls back to osascript in that case.
bool desktop_notify_native_ring(const char *workspace, const char *message);
void desktop_notify_native_startup(void);
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
    // Tests link this file directly and call this function with real
    // messages; without this guard every ctest run pops a real notification
    // since nothing here is mocked.
    if (getenv("FANGS_TEST_NO_NOTIFY"))
        return true;

    if (!message || !message[0])
        message = "needs attention";
    if (!workspace || !workspace[0])
        workspace = "shell";

#ifdef __APPLE__
    if (desktop_notify_native_ring(workspace, message))
        return true;

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
#else
    // notify-send takes the title/body as separate argv entries (no shell in
    // between), so no AppleScript-style escaping is needed here.
    char title[300];
    snprintf(title, sizeof(title), "Fangs \xe2\x80\x94 %s", workspace);
#endif

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

    // Second child (reparented to init): exec the platform notifier.
    // Restore default signal handling, close unnecessary fds.
    setsid();

    // Redirect stdin to /dev/null.
    FILE *null_in = fopen("/dev/null", "r");
    if (null_in) {
        dup2(fileno(null_in), STDIN_FILENO);
        fclose(null_in);
    }

#ifdef __APPLE__
    execlp("osascript", "osascript", "-e", script, (char *)NULL);
#else
    // Silently no-ops (via the exec failure path below) if notify-send isn't
    // installed — not every Linux setup has libnotify or a notification
    // daemon running.
    execlp("notify-send", "notify-send", "-a", "Fangs", "-i", "utilities-terminal",
          title, message, (char *)NULL);
#endif
    _exit(127);
}

void desktop_notify_startup(void)
{
#ifdef __APPLE__
    desktop_notify_native_startup();
#endif
}
