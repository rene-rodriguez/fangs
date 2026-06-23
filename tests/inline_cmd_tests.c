#include "inline_cmd.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); const char *e__ = (expected); \
    if (strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

static void test_bare_command(void)
{
    char out[256];
    EXPECT_TRUE(inline_sanitize_command("git reset --soft HEAD~1", out, sizeof(out)));
    EXPECT_STR(out, "git reset --soft HEAD~1");
}

static void test_strips_fence(void)
{
    char out[256];
    EXPECT_TRUE(inline_sanitize_command("```sh\nls -la /tmp\n```", out, sizeof(out)));
    EXPECT_STR(out, "ls -la /tmp");
}

static void test_strips_inline_backticks(void)
{
    char out[256];
    EXPECT_TRUE(inline_sanitize_command("`pwd`", out, sizeof(out)));
    EXPECT_STR(out, "pwd");
}

static void test_strips_prompt_marker(void)
{
    char out[256];
    EXPECT_TRUE(inline_sanitize_command("$ docker ps -a", out, sizeof(out)));
    EXPECT_STR(out, "docker ps -a");
}

static void test_skips_blank_and_trims(void)
{
    char out[256];
    EXPECT_TRUE(inline_sanitize_command("   \n  echo hi  \n", out, sizeof(out)));
    EXPECT_STR(out, "echo hi");
}

static void test_empty_and_fence_only(void)
{
    char out[256];
    EXPECT_FALSE(inline_sanitize_command("", out, sizeof(out)));
    EXPECT_FALSE(inline_sanitize_command("```\n\n```", out, sizeof(out)));
}

int main(void)
{
    test_bare_command();
    test_strips_fence();
    test_strips_inline_backticks();
    test_strips_prompt_marker();
    test_skips_blank_and_trims();
    test_empty_and_fence_only();

    if (failures) {
        fprintf(stderr, "%d inline_cmd test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
