// workspace_ports_tests — Pure model tests for the port scanner.
#include "workspace_ports.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_INT(actual, expected) do { \
    int a=(actual), e=(expected); if (a != e) { \
        fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); \
        failures++; \
    } \
} while (0)
#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)
#define EXPECT_PORTS(sc, ...) do { \
    int expected[] = { __VA_ARGS__ }; \
    int n = (int)(sizeof(expected) / sizeof(expected[0])); \
    int got[WORKSPACE_PORTS_MAX]; \
    int gn = workspace_ports_get(sc, got, WORKSPACE_PORTS_MAX); \
    if (gn != n) { \
        fprintf(stderr, "FAIL %s:%d: expected %d ports, got %d\n", __FILE__, __LINE__, n, gn); \
        failures++; \
    } else { \
        for (int i_ = 0; i_ < n; i_++) { \
            if (got[i_] != expected[i_]) { \
                fprintf(stderr, "FAIL %s:%d: port[%d] = %d, expected %d\n", __FILE__, __LINE__, i_, got[i_], expected[i_]); \
                failures++; \
            } \
        } \
    } \
} while (0)
#define EXPECT_EMPTY(sc) do { \
    int got[WORKSPACE_PORTS_MAX]; \
    int gn = workspace_ports_get(sc, got, WORKSPACE_PORTS_MAX); \
    if (gn != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected empty, got %d ports\n", __FILE__, __LINE__, gn); \
        failures++; \
    } \
} while (0)

static void test_localhost_port(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    const uint8_t *data = (const uint8_t *)"  Local:   http://localhost:5173/";
    workspace_ports_feed(&sc, data, strlen((const char *)data));
    EXPECT_PORTS(&sc, 5173);
}

static void test_loopback_ip_ports(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"listening on 127.0.0.1:8080", 28);
    EXPECT_PORTS(&sc, 8080);

    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"0.0.0.0:3000", 12);
    EXPECT_PORTS(&sc, 3000);

    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"[::1]:9229", 10);
    EXPECT_PORTS(&sc, 9229);
}

static void test_boundary_rejections(void)
{
    // Port must end at non-digit or end-of-stream.
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:5173abc", 17);
    EXPECT_EMPTY(&sc);

    // Port 0 is invalid.
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:0", 11);
    EXPECT_EMPTY(&sc);

    // Port > 65535 is invalid.
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:70000", 15);
    EXPECT_EMPTY(&sc);

    // Missing port.
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:", 10);
    EXPECT_EMPTY(&sc);
}

static void test_unknown_hosts_rejected(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"10.0.0.5:443", 12);
    EXPECT_EMPTY(&sc);

    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"host.docker.internal:4000", 25);
    EXPECT_EMPTY(&sc);
}

static void test_case_insensitive_host(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"LocalHost:4321", 14);
    EXPECT_PORTS(&sc, 4321);
}

static void test_chunk_split_middle_of_port(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:51", 12);
    EXPECT_EMPTY(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"73 ready", 8);
    EXPECT_PORTS(&sc, 5173);
}

static void test_chunk_split_in_host(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"local", 5);
    EXPECT_EMPTY(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"host:5173", 9);
    EXPECT_PORTS(&sc, 5173);
}

static void test_dedupe(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3000 and 127.0.0.1:3000", 34);
    EXPECT_PORTS(&sc, 3000);
}

static void test_lru_cap(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    // Feed 7 distinct ports; only 6 should survive, the oldest evicted.
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3000", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3001", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3002", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3003", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3004", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3005", 14);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3006", 14);
    int got[WORKSPACE_PORTS_MAX];
    int gn = workspace_ports_get(&sc, got, WORKSPACE_PORTS_MAX);
    EXPECT_INT(gn, 6);
    // 3000 should be evicted (oldest), 3001-3006 should survive.
    for (int i = 0; i < gn; i++) {
        EXPECT_TRUE(got[i] >= 3001 && got[i] <= 3006);
    }
}

static void test_clear_empties(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:5173", 14);
    EXPECT_PORTS(&sc, 5173);
    workspace_ports_clear(&sc);
    EXPECT_EMPTY(&sc);
}

static void test_get_ascending(void)
{
    WorkspacePortScanner sc;
    workspace_ports_reset(&sc);
    workspace_ports_feed(&sc, (const uint8_t *)"localhost:3001 localhost:3000 localhost:3002", 44);
    int got[WORKSPACE_PORTS_MAX];
    int gn = workspace_ports_get(&sc, got, WORKSPACE_PORTS_MAX);
    EXPECT_INT(gn, 3);
    EXPECT_INT(got[0], 3000);
    EXPECT_INT(got[1], 3001);
    EXPECT_INT(got[2], 3002);
}

int main(void)
{
    test_localhost_port();
    test_loopback_ip_ports();
    test_boundary_rejections();
    test_unknown_hosts_rejected();
    test_case_insensitive_host();
    test_chunk_split_middle_of_port();
    test_chunk_split_in_host();
    test_dedupe();
    test_lru_cap();
    test_clear_empties();
    test_get_ascending();
    return failures ? 1 : 0;
}
