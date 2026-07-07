// workspace_ports — Pure chunk-safe dev-server port scanner for PTY output.
//
// A WorkspacePortScanner feeds on raw PTY bytes and reports high-confidence
// localhost port announcements. It mirrors CbParser's design: a match can
// straddle feed chunks, so a small carry buffer preserves the tail; no
// allocations, no syscalls.
#ifndef FANGS_WORKSPACE_PORTS_H
#define FANGS_WORKSPACE_PORTS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define WORKSPACE_PORTS_MAX 6

typedef struct {
    int  ports[WORKSPACE_PORTS_MAX];        // discovered ports (ascending after get)
    uint64_t last_seen[WORKSPACE_PORTS_MAX]; // feed sequence, for LRU eviction
    int  count;
    char carry[32];                          // tail of previous chunk (match may straddle)
    int  carry_len;
    uint64_t feed_seq;
    int  pending_port;                       // port accepted at EOS in last feed (-1=none)
    int  pending_len;                        // number of digits in pending_port
} WorkspacePortScanner;

// Reset all state (same as zero-init).
void workspace_ports_reset(WorkspacePortScanner *sc);

// Feed raw PTY bytes. Scans for "localhost:<port>", "127.0.0.1:<port>",
// "0.0.0.0:<port>", and "[::1]:<port>" patterns (case-insensitive on host).
// Port must be 1–65535 with a non-digit boundary after it.
void workspace_ports_feed(WorkspacePortScanner *sc, const uint8_t *data, size_t len);

// Copy discovered ports into `out` (ascending, up to `max` entries).
// Returns the number of ports written.
int  workspace_ports_get(const WorkspacePortScanner *sc, int *out, int max);

// Clear all port claims (e.g. on command completion).
void workspace_ports_clear(WorkspacePortScanner *sc);

#endif // FANGS_WORKSPACE_PORTS_H
