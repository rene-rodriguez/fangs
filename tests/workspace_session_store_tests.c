#include "workspace_session_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_FALSE(expr) do { \
    if ((expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_INT(actual, expected) do { \
    int a__ = (actual); \
    int e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); \
    const char *e__ = (expected); \
    if (strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "fopen(%s): %s\n", path, strerror(errno));
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static char *temp_session_path(void)
{
    char templ[] = "/tmp/fangs-session-test-XXXXXX";
    char *dir = mkdtemp(templ);
    if (!dir) {
        fprintf(stderr, "mkdtemp: %s\n", strerror(errno));
        exit(2);
    }

    size_t len = strlen(dir) + strlen("/session.json") + 1;
    char *path = malloc(len);
    if (!path) {
        fprintf(stderr, "malloc failed\n");
        exit(2);
    }
    snprintf(path, len, "%s/session.json", dir);
    return path;
}

static void test_load_missing_file_returns_false_and_zeroes_state(void)
{
    WorkspaceSessionState state;
    memset(&state, 0xAA, sizeof(state));  // poison, to check load zeroes it

    EXPECT_FALSE(workspace_session_load("/tmp/fangs-session-does-not-exist.json", &state));
    EXPECT_INT(state.count, 0);
}

static void test_load_malformed_json_returns_false(void)
{
    char *path = temp_session_path();
    write_file(path, "{ not valid json ");

    WorkspaceSessionState state;
    EXPECT_FALSE(workspace_session_load(path, &state));
    EXPECT_INT(state.count, 0);

    free(path);
}

static void test_load_empty_tabs_array_returns_false(void)
{
    char *path = temp_session_path();
    write_file(path, "{\"active\":0,\"tabs\":[]}");

    WorkspaceSessionState state;
    EXPECT_FALSE(workspace_session_load(path, &state));
    EXPECT_INT(state.count, 0);

    free(path);
}

static void test_save_and_load_round_trips(void)
{
    char *path = temp_session_path();

    WorkspaceSessionState state;
    memset(&state, 0, sizeof(state));
    snprintf(state.tabs[0].cwd, sizeof(state.tabs[0].cwd), "/tmp/repo-one");
    snprintf(state.tabs[0].name, sizeof(state.tabs[0].name), "main");
    snprintf(state.tabs[1].cwd, sizeof(state.tabs[1].cwd), "/tmp/repo-one/.worktrees/fix-auth");
    // tabs[1].name intentionally left empty (automatic label)
    state.count = 2;
    state.active = 1;

    EXPECT_TRUE(workspace_session_save(path, &state));

    WorkspaceSessionState loaded;
    EXPECT_TRUE(workspace_session_load(path, &loaded));
    EXPECT_INT(loaded.count, 2);
    EXPECT_INT(loaded.active, 1);
    EXPECT_STR(loaded.tabs[0].cwd, "/tmp/repo-one");
    EXPECT_STR(loaded.tabs[0].name, "main");
    EXPECT_STR(loaded.tabs[1].cwd, "/tmp/repo-one/.worktrees/fix-auth");
    EXPECT_STR(loaded.tabs[1].name, "");

    free(path);
}

static void test_load_truncates_at_max_tabs_and_clamps_active(void)
{
    char *path = temp_session_path();

    char json[4096];
    int off = snprintf(json, sizeof(json), "{\"active\":50,\"tabs\":[");
    for (int i = 0; i < WORKSPACE_SESSION_MAX_TABS + 3; i++) {
        off += snprintf(json + off, sizeof(json) - (size_t)off,
                        "%s{\"cwd\":\"/tmp/repo-%d\",\"name\":\"\"}",
                        i == 0 ? "" : ",", i);
    }
    snprintf(json + off, sizeof(json) - (size_t)off, "]}");
    write_file(path, json);

    WorkspaceSessionState state;
    EXPECT_TRUE(workspace_session_load(path, &state));
    EXPECT_INT(state.count, WORKSPACE_SESSION_MAX_TABS);
    EXPECT_INT(state.active, 0);  // out-of-range active clamps to 0

    free(path);
}

static void test_load_skips_entries_missing_cwd(void)
{
    char *path = temp_session_path();
    write_file(path,
        "{\"active\":0,\"tabs\":["
        "{\"name\":\"no-cwd\"},"
        "{\"cwd\":\"\",\"name\":\"empty-cwd\"},"
        "{\"cwd\":\"/tmp/repo-valid\",\"name\":\"ok\"}"
        "]}");

    WorkspaceSessionState state;
    EXPECT_TRUE(workspace_session_load(path, &state));
    EXPECT_INT(state.count, 1);
    EXPECT_STR(state.tabs[0].cwd, "/tmp/repo-valid");

    free(path);
}

static void test_default_path_ends_with_session_json(void)
{
    char buf[4096];
    EXPECT_TRUE(workspace_session_default_path(buf, sizeof(buf)));
    size_t len = strlen(buf);
    EXPECT_TRUE(len > strlen("session.json"));
    EXPECT_STR(buf + len - strlen("session.json"), "session.json");
}

int main(void)
{
    test_load_missing_file_returns_false_and_zeroes_state();
    test_load_malformed_json_returns_false();
    test_load_empty_tabs_array_returns_false();
    test_save_and_load_round_trips();
    test_load_truncates_at_max_tabs_and_clamps_active();
    test_load_skips_entries_missing_cwd();
    test_default_path_ends_with_session_json();

    if (failures != 0) {
        fprintf(stderr, "%d workspace session store test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
