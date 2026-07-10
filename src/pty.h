// PTY plumbing — spawn a shell in a pseudo-terminal and pump bytes.
// Deliberately engine-agnostic: pty_read hands bytes to a sink callback so
// this module knows nothing about libghostty-vt (keeps the seam clean).
#ifndef FANGS_PTY_H
#define FANGS_PTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    PTY_READ_OK,     // drained (or nothing available right now)
    PTY_READ_EOF,    // child closed its end
    PTY_READ_ERROR,  // real read error
} PtyReadResult;

// Tier 2 of docs/crash-resilience-plan.md: when enabled, new shells are
// spawned inside a named `tmux new-session -A` instead of directly, so a
// Fangs crash (which SIGHUPs the shell same as any terminal) no longer kills
// the wrapped shell/agent -- it keeps running in tmux's own server and can
// be reattached to (`tmux attach -t <name>`) after Fangs restarts. Off by
// default; falls back to a plain shell if `tmux` isn't on PATH. Call once at
// startup from the loaded config -- affects every pty_spawn() call after it.
void pty_set_tmux_wrap(bool enabled);

// Receives bytes drained from the pty master fd.
typedef void (*PtySink)(void *userdata, const uint8_t *data, size_t len);

// Spawn the user's shell ($SHELL → passwd → /bin/sh) in a new pty.
// Returns the non-blocking master fd (>= 0) and stores the child pid in
// *child_out, or -1 on failure. If `cwd` is non-NULL and non-empty, the child
// chdir()s there before exec (used so a new tab/pane opens in the focused
// pane's directory); NULL inherits the parent's working directory.
int  pty_spawn(pid_t *child_out, uint16_t cols, uint16_t rows,
               int cell_width, int cell_height, const char *cwd);

// Best-effort write to the pty master fd (handles EINTR/partial/EAGAIN).
void pty_write(int pty_fd, const char *buf, size_t len);

// Drain all currently-available output, delivering it via sink().
PtyReadResult pty_read(int pty_fd, PtySink sink, void *userdata);

// Push a new window size to the pty (rows/cols + pixel dims) so the child
// gets SIGWINCH.
void pty_set_winsize(int pty_fd, uint16_t cols, uint16_t rows,
                     int cell_width, int cell_height);

#endif // FANGS_PTY_H
