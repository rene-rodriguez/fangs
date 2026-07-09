// workspace_session_store — Persists the open-tab list (cwd + name) to a
// small JSON file so it can be restored across app restarts. File I/O only
// (no App/raylib types), same tier as config.c: synchronous reads/writes on
// the main thread, gated by the [workspace] restore_session config key.
#ifndef FANGS_WORKSPACE_SESSION_STORE_H
#define FANGS_WORKSPACE_SESSION_STORE_H

#include <stdbool.h>

#define WORKSPACE_SESSION_MAX_TABS 9  // mirror FANGS_MAX_TABS
#define WORKSPACE_SESSION_CWD_MAX  1024
#define WORKSPACE_SESSION_NAME_MAX 64

typedef struct {
    char cwd[WORKSPACE_SESSION_CWD_MAX];
    char name[WORKSPACE_SESSION_NAME_MAX];
} WorkspaceSessionTab;

typedef struct {
    WorkspaceSessionTab tabs[WORKSPACE_SESSION_MAX_TABS];
    int count;
    int active;
} WorkspaceSessionState;

// Fills `buf` with the default session-state path (next to config.ini, in
// the same app dir as config_default_app_dir()). Returns false if `buf` is
// too small.
bool workspace_session_default_path(char *buf, int buf_size);

// Writes `state` to `path` as JSON (0600, like config_save). Returns false
// on any I/O error; a partial/failed write never leaves a corrupt file
// readable by workspace_session_load (write-to-temp-then-rename).
bool workspace_session_save(const char *path, const WorkspaceSessionState *state);

// Reads `state` from `path`. Returns false (and zeroes *out) if the file is
// missing, unreadable, or not valid JSON in the expected shape — callers
// should treat that as "nothing to restore", not a fatal error.
bool workspace_session_load(const char *path, WorkspaceSessionState *out);

#endif // FANGS_WORKSPACE_SESSION_STORE_H
