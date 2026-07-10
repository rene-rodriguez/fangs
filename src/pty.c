#include "pty.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pwd.h>

#if defined(__APPLE__)
#include <util.h>
#else
// glibc declares forkpty() in <pty.h>, but the project ships its own "pty.h"
// which shadows the system header on the -Isrc angle-bracket search path, so
// `#include <pty.h>` would pull in our header (and never declare forkpty).
// Declare the prototype directly — the BSD/glibc signature is stable.
#include <termios.h>
extern int forkpty(int *__amaster, char *__name,
                   const struct termios *__termp, const struct winsize *__winp);
#endif

static bool g_tmux_wrap = false;

void pty_set_tmux_wrap(bool enabled)
{
    g_tmux_wrap = enabled;
}

// Builds a tmux session name from cwd's last path component, sanitized to
// the character set tmux session names tolerate everywhere, plus the
// spawning pid for uniqueness (multiple panes can share a cwd). Purely for
// human recognizability when running `tmux ls` later -- Fangs never
// auto-reattaches by this name.
static void tmux_session_name(const char *cwd, pid_t pid, char *out, size_t out_size)
{
    const char *base = "fangs";
    if (cwd && cwd[0]) {
        const char *slash = strrchr(cwd, '/');
        base = slash ? slash + 1 : cwd;
        if (base[0] == '\0')
            base = "fangs";
    }

    char safe[64];
    size_t si = 0;
    for (size_t i = 0; base[i] != '\0' && si + 1 < sizeof(safe); i++) {
        unsigned char ch = (unsigned char)base[i];
        safe[si++] = (isalnum(ch) || ch == '-' || ch == '_') ? (char)ch : '-';
    }
    safe[si] = '\0';
    if (safe[0] == '\0')
        snprintf(safe, sizeof(safe), "fangs");

    snprintf(out, out_size, "fangs-%s-%d", safe, (int)pid);
}

int pty_spawn(pid_t *child_out, uint16_t cols, uint16_t rows,
              int cell_width, int cell_height, const char *cwd)
{
    int pty_fd;
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = (unsigned short)(cols * cell_width),
        .ws_ypixel = (unsigned short)(rows * cell_height),
    };

    // forkpty() = openpty + fork + login_tty.
    pid_t child = forkpty(&pty_fd, NULL, NULL, &ws);
    if (child < 0) {
        perror("forkpty");
        return -1;
    }
    if (child == 0) {
        const char *shell = getenv("SHELL");
        if (!shell || shell[0] == '\0') {
            struct passwd *pw = getpwuid(getuid());
            if (pw && pw->pw_shell && pw->pw_shell[0] != '\0')
                shell = pw->pw_shell;
            else
                shell = "/bin/sh";
        }
        const char *shell_name = strrchr(shell, '/');
        shell_name = shell_name ? shell_name + 1 : shell;

        // Start a LOGIN shell: argv[0] prefixed with '-' is the POSIX convention
        // that tells the shell to source the user's login profile
        // (~/.zprofile/.zshrc, ~/.bash_profile, …). This is how Terminal.app,
        // iTerm and Ghostty recover the user's full PATH: a GUI app launched from
        // Finder/Dock inherits only launchd's minimal PATH (/usr/bin:/bin:…), so
        // without a login shell, Homebrew (/opt/homebrew/bin), ~/.local/bin and
        // other profile-added entries would be missing.
        char login_argv0[256];
        snprintf(login_argv0, sizeof(login_argv0), "-%s", shell_name);

        // Open in the requested directory (a new tab/pane inherits the focused
        // pane's cwd). Ignore failure — fall back to the inherited cwd.
        if (cwd && cwd[0])
            (void)(chdir(cwd) == 0);

        setenv("TERM", "xterm-256color", 1);

        if (g_tmux_wrap) {
            char session_name[128];
            tmux_session_name(cwd, getpid(), session_name, sizeof(session_name));

            // "-l" asks the shell for login behavior the same way the
            // leading '-' in login_argv0 does above (tmux runs this string
            // as a shell command, not exec's argv[0], so the '-' convention
            // doesn't apply here).
            char login_cmd[512];
            snprintf(login_cmd, sizeof(login_cmd), "%s -l", shell);

            execlp("tmux", "tmux", "new-session", "-A", "-s", session_name,
                  login_cmd, (char *)NULL);
            // tmux isn't installed (or exec failed for some other reason) --
            // fall through to the plain shell below.
        }

        execl(shell, login_argv0, (char *)NULL);
        _exit(127); // execl only returns on error
    }

    // Parent: non-blocking master so reads return EAGAIN instead of stalling.
    int flags = fcntl(pty_fd, F_GETFL);
    if (flags < 0 || fcntl(pty_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl O_NONBLOCK");
        close(pty_fd);
        return -1;
    }

    *child_out = child;
    return pty_fd;
}

void pty_write(int pty_fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(pty_fd, buf, len);
        if (n > 0) {
            buf += n;
            len -= (size_t)n;
        } else if (n < 0) {
            if (errno == EINTR)
                continue;
            break; // EAGAIN or real error — drop the remainder
        }
    }
}

PtyReadResult pty_read(int pty_fd, PtySink sink, void *userdata)
{
    uint8_t buf[4096];
    for (;;) {
        ssize_t n = read(pty_fd, buf, sizeof(buf));
        if (n > 0) {
            sink(userdata, buf, (size_t)n);
        } else if (n == 0) {
            return PTY_READ_EOF;
        } else {
            if (errno == EAGAIN)
                return PTY_READ_OK;
            if (errno == EINTR)
                continue;
            if (errno == EIO) // Linux: slave close often reports EIO not EOF
                return PTY_READ_EOF;
            perror("pty read");
            return PTY_READ_ERROR;
        }
    }
}

void pty_set_winsize(int pty_fd, uint16_t cols, uint16_t rows,
                     int cell_width, int cell_height)
{
    struct winsize ws = {
        .ws_row = rows,
        .ws_col = cols,
        .ws_xpixel = (unsigned short)(cols * cell_width),
        .ws_ypixel = (unsigned short)(rows * cell_height),
    };
    ioctl(pty_fd, TIOCSWINSZ, &ws);
}
