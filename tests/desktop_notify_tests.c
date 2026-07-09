// Tests for desktop_notify AppleScript escaping.
// Does not require Notification Center permissions and does not invoke
// osascript.
#include "desktop_notify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_FALSE(expr) do { if ((expr)) { fprintf(stderr, "FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static void test_applescript_escape(void)
{
    char out[256];
    EXPECT_TRUE(desktop_notify_escape_applescript("say \"hi\" \\ done", out, sizeof(out)));
    EXPECT_STR(out, "say \\\"hi\\\" \\\\ done");
}

static void test_empty_message_is_allowed(void)
{
    char out[8];
    EXPECT_TRUE(desktop_notify_escape_applescript("", out, sizeof(out)));
    EXPECT_STR(out, "");
}

static void test_escape_nul_input(void)
{
    char out[8];
    EXPECT_TRUE(desktop_notify_escape_applescript(NULL, out, sizeof(out)));
    EXPECT_STR(out, "");
}

static void test_escape_no_special_chars(void)
{
    char out[64];
    EXPECT_TRUE(desktop_notify_escape_applescript("hello world", out, sizeof(out)));
    EXPECT_STR(out, "hello world");
}

static void test_escape_truncation(void)
{
    char out[4];
    EXPECT_TRUE(desktop_notify_escape_applescript("abc", out, sizeof(out)));
    EXPECT_STR(out, "abc");
    // "ab\" would need 4 bytes + NUL -> truncation
    EXPECT_FALSE(desktop_notify_escape_applescript("ab\"", out, sizeof(out)));
}

static void test_agent_ring_never_crashes(void)
{
    // Should not crash on any platform; we don't assert the return value
    // since osascript may not be available in test environments.
    desktop_notify_agent_ring("test-workspace", "hello");
    desktop_notify_agent_ring("test-workspace", NULL);
    desktop_notify_agent_ring(NULL, "hello");
    desktop_notify_agent_ring(NULL, NULL);
}

int main(void)
{
    // desktop_notify_agent_ring() forks a real osascript unless this is set —
    // see the matching guard in src/desktop_notify.c.
    setenv("FANGS_TEST_NO_NOTIFY", "1", 1);

    test_applescript_escape();
    test_empty_message_is_allowed();
    test_escape_nul_input();
    test_escape_no_special_chars();
    test_escape_truncation();
    test_agent_ring_never_crashes();
    return failures ? 1 : 0;
}
