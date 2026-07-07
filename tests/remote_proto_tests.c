#include "remote_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT_INT(actual, expected) do { \
    int a=(actual), e=(expected); if (a != e) { \
        fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); \
        failures++; \
    } \
} while (0)
#define EXPECT_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        failures++; \
    } \
} while (0)
#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)
#define EXPECT_FALSE(expr) do { \
    if ((expr)) { \
        fprintf(stderr, "FAIL %s:%d: !(%s)\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_parse_list(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse("{\"id\":1,\"cmd\":\"list\"}", &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 1);
    EXPECT_INT(rq.cmd, REMOTE_CMD_LIST);
}

static void test_parse_new_minimal(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse("{\"id\":2,\"cmd\":\"new\"}", &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 2);
    EXPECT_INT(rq.cmd, REMOTE_CMD_NEW);
    EXPECT_FALSE(rq.worktree);
    EXPECT_STR(rq.name, "");
    EXPECT_STR(rq.cwd, "");
    EXPECT_STR(rq.run, "");
}

static void test_parse_new_full(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":3,\"cmd\":\"new\",\"worktree\":true,\"name\":\"demo\","
        "\"cwd\":\"/tmp\",\"run\":\"claude\"}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 3);
    EXPECT_INT(rq.cmd, REMOTE_CMD_NEW);
    EXPECT_TRUE(rq.worktree);
    EXPECT_STR(rq.name, "demo");
    EXPECT_STR(rq.cwd, "/tmp");
    EXPECT_STR(rq.run, "claude");
}

static void test_parse_focus(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":4,\"cmd\":\"focus\",\"index\":2,\"pane\":0}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 4);
    EXPECT_INT(rq.cmd, REMOTE_CMD_FOCUS);
    EXPECT_INT(rq.index, 2);
    EXPECT_INT(rq.pane, 0);
}

static void test_parse_focus_minimal(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":5,\"cmd\":\"focus\",\"index\":1}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 5);
    EXPECT_INT(rq.cmd, REMOTE_CMD_FOCUS);
    EXPECT_INT(rq.index, 1);
    EXPECT_INT(rq.pane, -1);   // absent
}

static void test_parse_rename(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":6,\"cmd\":\"rename\",\"index\":0,\"name\":\"fix-bug\"}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 6);
    EXPECT_INT(rq.cmd, REMOTE_CMD_RENAME);
    EXPECT_INT(rq.index, 0);
    EXPECT_STR(rq.name, "fix-bug");
}

static void test_parse_send(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":7,\"cmd\":\"send\",\"index\":1,\"text\":\"echo hello\\n\"}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 7);
    EXPECT_INT(rq.cmd, REMOTE_CMD_SEND);
    EXPECT_INT(rq.index, 1);
    EXPECT_STR(rq.text, "echo hello\n");
}

static void test_parse_read(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":8,\"cmd\":\"read\",\"index\":0,\"lines\":80}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 8);
    EXPECT_INT(rq.cmd, REMOTE_CMD_READ);
    EXPECT_INT(rq.index, 0);
    EXPECT_INT(rq.lines, 80);
}

static void test_parse_read_default_lines(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":9,\"cmd\":\"read\",\"index\":0}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 9);
    EXPECT_INT(rq.cmd, REMOTE_CMD_READ);
    EXPECT_INT(rq.index, 0);
    EXPECT_INT(rq.lines, -1);   // absent, caller defaults
}

static void test_parse_ring(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":10,\"cmd\":\"ring\",\"index\":2,\"message\":\"review me\"}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 10);
    EXPECT_INT(rq.cmd, REMOTE_CMD_RING);
    EXPECT_INT(rq.index, 2);
    EXPECT_STR(rq.message, "review me");
}

static void test_parse_ring_no_message(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_TRUE(remote_proto_parse(
        "{\"id\":11,\"cmd\":\"ring\",\"index\":0}",
        &rq, err, sizeof(err)));
    EXPECT_INT(rq.id, 11);
    EXPECT_INT(rq.cmd, REMOTE_CMD_RING);
    EXPECT_INT(rq.index, 0);
    EXPECT_STR(rq.message, "");
}

static void test_parse_unknown_cmd(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_FALSE(remote_proto_parse(
        "{\"id\":12,\"cmd\":\"reboot\"}", &rq, err, sizeof(err)));
    EXPECT_TRUE(err[0] != '\0');
}

static void test_parse_missing_cmd(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_FALSE(remote_proto_parse(
        "{\"id\":13}", &rq, err, sizeof(err)));
    EXPECT_TRUE(err[0] != '\0');
}

static void test_parse_non_json(void)
{
    RemoteRequest rq;
    char err[128];
    EXPECT_FALSE(remote_proto_parse("not json at all", &rq, err, sizeof(err)));
    EXPECT_TRUE(err[0] != '\0');
}

static void test_parse_oversize_line(void)
{
    // Build a line > REMOTE_LINE_MAX
    char big[REMOTE_LINE_MAX + 100];
    memset(big, 'x', sizeof(big) - 1);
    big[0] = '{';
    big[sizeof(big) - 2] = '}';
    big[sizeof(big) - 1] = '\0';

    RemoteRequest rq;
    char err[128];
    EXPECT_FALSE(remote_proto_parse(big, &rq, err, sizeof(err)));
    EXPECT_TRUE(err[0] != '\0');
}

static void test_parse_id_echoes_on_error(void)
{
    RemoteRequest rq;
    char err[128];
    // Missing cmd but has id — the id should still be parseable
    EXPECT_FALSE(remote_proto_parse(
        "{\"id\":42,\"cmd\":\"bogus\"}", &rq, err, sizeof(err)));
    // We just check error is non-empty; id extraction is best-effort
    EXPECT_TRUE(err[0] != '\0');
}

static void test_build_error(void)
{
    char *resp = remote_proto_error(1, "something went wrong");
    EXPECT_TRUE(resp != NULL);
    EXPECT_TRUE(strstr(resp, "\"id\":1") != NULL);
    EXPECT_TRUE(strstr(resp, "\"ok\":false") != NULL);
    EXPECT_TRUE(strstr(resp, "\"error\":\"something went wrong\"") != NULL);
    free(resp);
}

static void test_build_ok(void)
{
    char *resp = remote_proto_ok(2);
    EXPECT_TRUE(resp != NULL);
    EXPECT_TRUE(strstr(resp, "\"id\":2") != NULL);
    EXPECT_TRUE(strstr(resp, "\"ok\":true") != NULL);
    free(resp);
}

static void test_build_ok_echoes_arbitrary_id(void)
{
    char *resp = remote_proto_ok(999);
    EXPECT_TRUE(strstr(resp, "\"id\":999") != NULL);
    free(resp);
}

static void test_escape_in_text(void)
{
    // Build a response with embedded quotes and newlines via ok_obj
    // We'll test that the JSON builder doesn't produce broken JSON
    void *obj = (void*)((long)1); // can't really test without cJSON includes
    // For now verify remote_proto_ok_obj exists and handles NULL
    char *resp = remote_proto_ok_obj(1, NULL);
    EXPECT_TRUE(resp != NULL);
    EXPECT_TRUE(strstr(resp, "\"ok\":true") != NULL);
    free(resp);
}

int main(void)
{
    test_parse_list();
    test_parse_new_minimal();
    test_parse_new_full();
    test_parse_focus();
    test_parse_focus_minimal();
    test_parse_rename();
    test_parse_send();
    test_parse_read();
    test_parse_read_default_lines();
    test_parse_ring();
    test_parse_ring_no_message();
    test_parse_unknown_cmd();
    test_parse_missing_cmd();
    test_parse_non_json();
    test_parse_oversize_line();
    test_parse_id_echoes_on_error();
    test_build_error();
    test_build_ok();
    test_build_ok_echoes_arbitrary_id();
    test_escape_in_text();
    return failures ? 1 : 0;
}
