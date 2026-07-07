#ifndef FANGS_REMOTE_PROTO_H
#define FANGS_REMOTE_PROTO_H

#include <stdbool.h>
#include <stddef.h>

#define REMOTE_LINE_MAX 8192

typedef enum {
    REMOTE_CMD_NONE,
    REMOTE_CMD_LIST,
    REMOTE_CMD_NEW,
    REMOTE_CMD_FOCUS,
    REMOTE_CMD_RENAME,
    REMOTE_CMD_SEND,
    REMOTE_CMD_READ,
    REMOTE_CMD_RING,
} RemoteCmd;

typedef struct {
    long id;
    RemoteCmd cmd;
    int index;       // -1 when absent
    int pane;        // -1 when absent
    int lines;       // -1 when absent
    bool worktree;
    char cwd[4096];
    char name[64];
    char run[512];
    char text[4096];
    char message[128];
} RemoteRequest;

// Parse a single JSON-line request. Returns true on success.
// On parse error fills err (if non-NULL) and returns false.
bool remote_proto_parse(const char *line, RemoteRequest *out,
                        char *err, int err_size);

// Build response JSON strings (caller must free the returned pointer).
char *remote_proto_error(long id, const char *msg);
char *remote_proto_ok(long id);
// Takes ownership of fields (a cJSON object). fields may be NULL.
char *remote_proto_ok_obj(long id, void *fields);

#endif // FANGS_REMOTE_PROTO_H
