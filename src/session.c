#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#include "cmdblocks.h"
#include "pty.h"
#include "term_engine.h"

struct Session {
    TermEngine *te;
    int  pty_fd;
    pid_t child;
    bool child_alive;
    int  cell_w;
    int  cell_h;
    uint16_t cols;
    uint16_t rows;
    char cwd[1024];
    int  max_scrollback;
};

Session *session_create(uint16_t cols, uint16_t rows, int cell_w, int cell_h,
                        int max_scrollback, const char *cwd)
{
    Session *s = (Session *)calloc(1, sizeof(Session));
    if (!s)
        return NULL;

    s->cols = cols;
    s->rows = rows;
    s->cell_w = cell_w;
    s->cell_h = cell_h;
    s->max_scrollback = max_scrollback;

    if (cwd && cwd[0])
        snprintf(s->cwd, sizeof(s->cwd), "%s", cwd);
    else
        snprintf(s->cwd, sizeof(s->cwd), "%s", getenv("HOME") ? getenv("HOME") : "/");

    s->te = term_engine_create(cols, rows, cell_w, cell_h, max_scrollback);
    if (!s->te) {
        free(s);
        return NULL;
    }

    s->pty_fd = pty_spawn(&s->child, cols, rows, cell_w, cell_h);
    if (s->pty_fd < 0) {
        term_engine_destroy(s->te);
        free(s);
        return NULL;
    }
    s->child_alive = true;

    return s;
}

void session_destroy(Session *s)
{
    if (!s)
        return;

    // Close PTY to signal the child.
    if (s->pty_fd >= 0) {
        close(s->pty_fd);
        s->pty_fd = -1;
    }

    // Reap child.
    if (s->child > 0) {
        int status = 0;
        waitpid(s->child, &status, WNOHANG);
        s->child = -1;
        s->child_alive = false;
    }

    cmdblocks_reset();
    term_engine_destroy(s->te);
    free(s);
}

void session_feed_pty(Session *s)
{
    if (!s || s->pty_fd < 0 || !s->child_alive)
        return;

    uint8_t buf[65536];
    ssize_t n = read(s->pty_fd, buf, sizeof(buf));
    if (n > 0)
        cmdblocks_feed(s->te, buf, (size_t)n);
    else if (n == 0)
        s->child_alive = false;
    // n < 0: EAGAIN is fine; other errors mark child dead.
    else if (errno != EAGAIN && errno != EINTR)
        s->child_alive = false;
}

void session_resize(Session *s, uint16_t cols, uint16_t rows,
                    int cell_w, int cell_h)
{
    if (!s)
        return;

    s->cols = cols;
    s->rows = rows;
    s->cell_w = cell_w;
    s->cell_h = cell_h;

    term_engine_resize(s->te, cols, rows, cell_w, cell_h);
    pty_set_winsize(s->pty_fd, cols, rows, cell_w, cell_h);
}

int session_pty_fd(const Session *s)
{
    return s ? s->pty_fd : -1;
}

void *session_engine(Session *s)
{
    return s ? (void *)s->te : NULL;
}

bool session_child_alive(const Session *s)
{
    return s && s->child_alive;
}

const char *session_cwd(const Session *s)
{
    return s ? s->cwd : "";
}

bool session_reap(Session *s)
{
    if (!s || s->child <= 0)
        return false;
    int status = 0;
    if (waitpid(s->child, &status, WNOHANG) == s->child) {
        s->child_alive = false;
        s->child = -1;
        return true;
    }
    return false;
}

bool session_respawn(Session *s, const char *cwd)
{
    if (!s)
        return false;

    if (s->child > 0) {
        close(s->pty_fd);
        int status = 0;
        waitpid(s->child, &status, WNOHANG);
        s->child = -1;
    }

    const char *dir = cwd ? cwd : s->cwd;
    s->pty_fd = -1;
    s->child_alive = false;

    s->pty_fd = pty_spawn(&s->child, s->cols, s->rows, s->cell_w, s->cell_h);
    if (s->pty_fd < 0)
        return false;

    s->child_alive = true;
    if (dir && dir[0])
        snprintf(s->cwd, sizeof(s->cwd), "%s", dir);
    return true;
}
