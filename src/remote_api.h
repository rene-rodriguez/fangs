#ifndef FANGS_REMOTE_API_H
#define FANGS_REMOTE_API_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define REMOTE_API_INBOX_MAX 16

typedef struct RemoteApi RemoteApi;

// Start the Unix socket worker thread. Listens on
// <socket_dir>/remote-<pid>.sock and symlinks remote.sock -> remote-<pid>.sock.
// Returns NULL on failure; error is filled if non-NULL.
RemoteApi *remote_api_start(const char *socket_dir, pid_t pid,
                            char *error, int err_size);

// Graceful stop: signal the worker, join the thread, unlink socket+symlink.
void remote_api_stop(RemoteApi *ra);

// Dequeue pending request lines from the inbox (main thread).
// Returns the number of lines written; each is a NUL-terminated JSON string.
// Call at most once per frame.
int remote_api_poll(RemoteApi *ra, char *lines[], int max_lines);

// Enqueue a response JSON string (main thread). The string is copied.
// The worker thread will send it to the connected client.
void remote_api_respond(RemoteApi *ra, const char *json);

// True while the socket is listening / a client is connected.
bool remote_api_running(const RemoteApi *ra);

// True if a client is currently connected.
bool remote_api_has_client(const RemoteApi *ra);

#endif // FANGS_REMOTE_API_H
