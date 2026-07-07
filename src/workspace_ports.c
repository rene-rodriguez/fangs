// workspace_ports — Pure chunk-safe dev-server port scanner for PTY output.
//
// Scans raw PTY byte streams for host:port announcements.  A match can
// straddle feed chunks, so the tail of each chunk is preserved in a carry
// buffer.  Ports whose digit span reaches end-of-buffer (EOS) are pre-
// confirmed when they have at least 3 digits (the minimum for a real
// ephemeral port); shorter digit spans at EOS are held as pending and must be
// completed by a subsequent feed to be reported.
#include "workspace_ports.h"

#include <ctype.h>
#include <string.h>

void workspace_ports_reset(WorkspacePortScanner *sc)
{
    sc->count = 0;
    sc->carry_len = 0;
    sc->feed_seq = 0;
    sc->pending_port = -1;
    sc->pending_len = 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_digit(unsigned char c)  { return c >= '0' && c <= '9'; }
static bool is_alpha(unsigned char c)  {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static bool valid_boundary(unsigned char c) { return !is_digit(c) && !is_alpha(c); }

static unsigned char to_lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 0x20) : c;
}

// ---------------------------------------------------------------------------
// Port list management
// ---------------------------------------------------------------------------

static int find_port(const WorkspacePortScanner *sc, int port)
{
    for (int i = 0; i < sc->count; i++)
        if (sc->ports[i] == port) return i;
    return -1;
}

static void evict_oldest(WorkspacePortScanner *sc)
{
    if (sc->count <= 0) return;
    int oldest = 0;
    for (int i = 1; i < sc->count; i++)
        if (sc->last_seen[i] < sc->last_seen[oldest])
            oldest = i;
    sc->count--;
    for (int i = oldest; i < sc->count; i++) {
        sc->ports[i] = sc->ports[i + 1];
        sc->last_seen[i] = sc->last_seen[i + 1];
    }
}

static void add_port(WorkspacePortScanner *sc, int port)
{
    sc->feed_seq++;
    int idx = find_port(sc, port);
    if (idx >= 0) {
        sc->last_seen[idx] = sc->feed_seq;
        return;
    }
    if (sc->count >= WORKSPACE_PORTS_MAX)
        evict_oldest(sc);
    idx = sc->count;
    sc->ports[idx] = port;
    sc->last_seen[idx] = sc->feed_seq;
    sc->count++;
}

// ---------------------------------------------------------------------------
// Host matcher (case-insensitive)
// ---------------------------------------------------------------------------

// Returns host length if bytes at `pos` match a recognised host literal
// followed by ':' and at least one digit.  Sets *dstart/*dend to the digit
// span (both 0 if the ':' has no following digits).
static int match_host_port(const unsigned char *buf, size_t pos, size_t blen,
                           size_t *dstart, size_t *dend)
{
    *dstart = 0;
    *dend = 0;

    static const char *hosts[] = { "127.0.0.1", "localhost", "0.0.0.0", "[::1]" };
    static const int hlen[] = { 9, 9, 7, 5 };

    for (int h = 0; h < 4; h++) {
        if (pos + (size_t)hlen[h] > blen) continue;
        bool match = true;
        for (int i = 0; i < hlen[h]; i++) {
            if (to_lower(buf[pos + i]) != (unsigned char)hosts[h][i]) {
                match = false;
                break;
            }
        }
        if (!match) continue;

        size_t ci = pos + hlen[h];          // position of expected ':'
        if (ci >= blen || buf[ci] != ':') continue;

        size_t ds = ci + 1;
        if (ds >= blen) return hlen[h];     // ':' but no digits (yet)
        size_t de = ds;
        while (de < blen && is_digit(buf[de])) de++;
        if (de == ds) return hlen[h];       // ':' followed by non-digit
        *dstart = ds;
        *dend  = de;
        return hlen[h];
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Feed
// ---------------------------------------------------------------------------

void workspace_ports_feed(WorkspacePortScanner *sc, const uint8_t *data,
                          size_t len)
{
    if (!sc || !data || len == 0) return;

    // Combine carry (previous tail) and new data into one scan buffer.
    unsigned char buf[65536 + 32];
    size_t blen = 0;
    if (sc->carry_len > 0) {
        memcpy(buf, sc->carry, sc->carry_len);
        blen = sc->carry_len;
    }
    size_t to_copy = len;
    if (blen + to_copy > sizeof(buf))
        to_copy = sizeof(buf) - blen;
    memcpy(buf + blen, data, to_copy);
    blen += to_copy;

    // Reset pending — the combined scan re-evaluates everything below.
    sc->pending_port = -1;
    sc->pending_len  = 0;

    // Scan for host:port patterns.
    size_t dstart, dend;
    for (size_t i = 0; i < blen; i++) {
        int hl = match_host_port(buf, i, blen, &dstart, &dend);
        if (hl == 0) continue;
        if (dstart == 0) continue;           // ':' with no digits yet

        // Accumulate port.
        int port = 0;
        for (size_t j = dstart; j < dend; j++)
            port = port * 10 + (buf[j] - '0');
        if (port < 1 || port > 65535) continue;

        // Check boundary after the digit span.
        if (dend >= blen) {
            // EOS — accept immediately if >= 3 digits (real ports are
            // 1024-65535, so 3-5 digits).  Shorter spans may be the
            // first half of a chunk-split port and are held pending.
            int ndigits = (int)(dend - dstart);
            if (ndigits >= 3) {
                add_port(sc, port);
            } else {
                sc->pending_port = port;
                sc->pending_len  = ndigits;
            }
            continue;
        }

        if (!valid_boundary(buf[dend])) continue;
        add_port(sc, port);
    }

    // Save the tail of the combined buffer as carry.
    int keep = (int)blen;
    int max_carry = (int)sizeof(sc->carry) - 1;
    if (keep > max_carry) {
        int excess = keep - max_carry;
        memmove(buf, buf + excess, max_carry);
        keep = max_carry;
    }
    if (keep > 0)
        memcpy(sc->carry, buf, keep);
    sc->carry_len = keep;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

int workspace_ports_get(const WorkspacePortScanner *sc, int *out, int max)
{
    if (!sc || !out || max <= 0) return 0;

    int n = sc->count < max ? sc->count : max;
    for (int i = 0; i < n; i++)
        out[i] = sc->ports[i];

    // Insertion sort (tiny array).
    for (int i = 1; i < n; i++) {
        int key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j] > key) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

void workspace_ports_clear(WorkspacePortScanner *sc)
{
    sc->count = 0;
    sc->pending_port = -1;
    sc->pending_len = 0;
}
