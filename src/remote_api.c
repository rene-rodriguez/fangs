#include "remote_api.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define REMOTE_API_LINE_MAX 8192
#define REMOTE_API_OUTBOX_MAX 64

struct RemoteApi {
    // Worker thread writes request lines here; main thread reads them.
    char inbox[REMOTE_API_INBOX_MAX][REMOTE_API_LINE_MAX];
    volatile int inbox_head;   // worker writes at head % INBOX_MAX
    volatile int inbox_tail;   // main reads at tail % INBOX_MAX

    // Main thread writes response JSON here; worker reads and sends.
    char outbox[REMOTE_API_OUTBOX_MAX][REMOTE_API_LINE_MAX];
    volatile int outbox_head;  // main writes at head % OUTBOX_MAX
    volatile int outbox_tail;  // worker reads at tail % OUTBOX_MAX

    pthread_mutex_t mu;
    pthread_t thread;
    volatile int thread_started;

    volatile int stop;         // signal from main to worker
    volatile int running;      // set by worker once socket is listening
    volatile int client_connected;

    int server_fd;
    char socket_path[512];
    char symlink_path[512];
};

// Forward declare worker function.
static void *worker(void *arg);

RemoteApi *remote_api_start(const char *socket_dir, pid_t pid,
                            char *error, int err_size)
{
    RemoteApi *ra = calloc(1, sizeof(*ra));
    if (!ra) {
        if (error) snprintf(error, err_size, "calloc failed");
        return NULL;
    }
    pthread_mutex_init(&ra->mu, NULL);

    // Build socket paths.
    snprintf(ra->socket_path, sizeof(ra->socket_path),
             "%s/remote-%d.sock", socket_dir, (int)pid);
    snprintf(ra->symlink_path, sizeof(ra->symlink_path),
             "%s/remote.sock", socket_dir);

    ra->server_fd = -1;
    ra->stop = 0;
    ra->running = 0;
    ra->client_connected = 0;

    if (pthread_create(&ra->thread, NULL, worker, ra) != 0) {
        if (error) snprintf(error, err_size, "pthread_create failed");
        pthread_mutex_destroy(&ra->mu);
        free(ra);
        return NULL;
    }
    ra->thread_started = 1;

    // Busy-wait a tiny bit for the worker to bind (so the caller gets
    // immediate feedback). This is the same pattern as the frame loop
    // checking running before draining.
    for (int i = 0; i < 500; i++) {
        if (ra->running)
            break;
        usleep(2000); // 2 ms
    }

    if (!ra->running) {
        if (error) snprintf(error, err_size, "socket bind failed (check if fangs is already running)");
        remote_api_stop(ra);
        return NULL;
    }

    return ra;
}

void remote_api_stop(RemoteApi *ra)
{
    if (!ra) return;

    ra->stop = 1;

    // Close server fd to break accept().
    if (ra->server_fd >= 0) {
        close(ra->server_fd);
        ra->server_fd = -1;
    }

    if (ra->thread_started) {
        pthread_join(ra->thread, NULL);
        ra->thread_started = 0;
    }

    // Unlink socket and symlink.
    unlink(ra->socket_path);
    unlink(ra->symlink_path);

    pthread_mutex_destroy(&ra->mu);
    free(ra);
}

int remote_api_poll(RemoteApi *ra, char *lines[], int max_lines)
{
    if (!ra || max_lines <= 0) return 0;

    int count = 0;
    pthread_mutex_lock(&ra->mu);
    while (ra->inbox_tail != ra->inbox_head && count < max_lines) {
        int idx = ra->inbox_tail % REMOTE_API_INBOX_MAX;
        lines[count] = ra->inbox[idx];
        count++;
        ra->inbox_tail++;
    }
    pthread_mutex_unlock(&ra->mu);
    return count;
}

void remote_api_respond(RemoteApi *ra, const char *json)
{
    if (!ra || !json) return;

    pthread_mutex_lock(&ra->mu);
    int next = (ra->outbox_head + 1) % REMOTE_API_OUTBOX_MAX;
    // Silently drop if outbox full.
    if (next != ra->outbox_tail) {
        int idx = ra->outbox_head % REMOTE_API_OUTBOX_MAX;
        snprintf(ra->outbox[idx], sizeof(ra->outbox[idx]), "%s", json);
        ra->outbox_head = next;
    }
    pthread_mutex_unlock(&ra->mu);
}

bool remote_api_running(const RemoteApi *ra)
{
    return ra && ra->running;
}

bool remote_api_has_client(const RemoteApi *ra)
{
    return ra && ra->client_connected;
}

// ---- Worker thread ----

// Wait for a response in the outbox (or until stop). Returns a pointer to the
// response string in outbox[idx] (valid under lock). Caller must unlock.
static const char *wait_for_response(RemoteApi *ra)
{
    for (int i = 0; i < 5000; i++) {
        if (ra->stop) return NULL;
        if (ra->outbox_tail != ra->outbox_head) {
            int idx = ra->outbox_tail % REMOTE_API_OUTBOX_MAX;
            return ra->outbox[idx];
        }
        usleep(2000); // 2 ms
    }
    return NULL; // timeout
}

static void handle_client(RemoteApi *ra, int client_fd)
{
    char buf[REMOTE_API_LINE_MAX];
    size_t pos = 0;

    while (!ra->stop) {
        // --- Read one line (delimited by \n) ---
        pos = 0;
        while (!ra->stop) {
            char ch;
            ssize_t n = read(client_fd, &ch, 1);
            if (n <= 0)
                goto done; // closed or error

            if (ch == '\n') {
                buf[pos] = '\0';
                break;
            }
            if (pos < sizeof(buf) - 1)
                buf[pos++] = ch;
            // else: character silently dropped (line too long)
        }
        if (ra->stop) break;
        if (pos == 0) continue; // skip blank lines

        // --- Push to inbox ---
        pthread_mutex_lock(&ra->mu);
        int next = (ra->inbox_head + 1) % REMOTE_API_INBOX_MAX;
        if (next != ra->inbox_tail) {
            int idx = ra->inbox_head % REMOTE_API_INBOX_MAX;
            snprintf(ra->inbox[idx], sizeof(ra->inbox[idx]), "%s", buf);
            ra->inbox_head = next;
        } else {
            // Inbox full; drop the request.
            pthread_mutex_unlock(&ra->mu);
            continue;
        }
        pthread_mutex_unlock(&ra->mu);

        // --- Wait for the main thread to process and respond ---
        pthread_mutex_lock(&ra->mu);
        const char *resp = wait_for_response(ra);
        if (resp) {
            size_t rlen = strlen(resp);
            ra->outbox_tail++; // consume the response
            pthread_mutex_unlock(&ra->mu);

            // Write response + newline to client.
            size_t off = 0;
            while (off < rlen) {
                ssize_t w = write(client_fd, resp + off, rlen - off);
                if (w <= 0) goto done;
                off += w;
            }
            write(client_fd, "\n", 1);
        } else {
            pthread_mutex_unlock(&ra->mu);
            if (ra->stop) break;
        }
    }

done:
    close(client_fd);
}

static void *worker(void *arg)
{
    RemoteApi *ra = (RemoteApi *)arg;

    // Create Unix domain socket.
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        ra->stop = 1;
        return NULL;
    }
    ra->server_fd = fd;

    // Ensure clean state before bind.
    unlink(ra->socket_path);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ra->socket_path);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        // Let the "running" check below see failure.
        close(fd);
        ra->server_fd = -1;
        ra->stop = 1;
        return NULL;
    }

    // Set socket permissions to 0600.
    chmod(ra->socket_path, 0600);

    // Create/update symlink remote.sock -> remote-<pid>.sock.
    unlink(ra->symlink_path);
    symlink(ra->socket_path, ra->symlink_path);

    // Mark running before listen so the main thread knows we're live.
    ra->running = 1;

    if (listen(fd, 1) < 0) {
        ra->running = 0;
        close(fd);
        ra->server_fd = -1;
        ra->stop = 1;
        return NULL;
    }

    // Accept loop: serve one client at a time.
    while (!ra->stop) {
        struct sockaddr_un client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (ra->stop) break;
            continue;
        }
        ra->client_connected = 1;
        handle_client(ra, client_fd);
        ra->client_connected = 0;
    }

    ra->running = 0;
    close(fd);
    ra->server_fd = -1;
    return NULL;
}
