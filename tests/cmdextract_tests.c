#include "cmdextract.h"

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

static void test_fenced_with_language_tag(void)
{
    char out[256];
    bool ok = command_extract("Try this:\n```sh\nls -la /tmp\n```\nthat's it", out, sizeof(out));
    EXPECT_TRUE(ok);
    EXPECT_STR(out, "ls -la /tmp");
}

static void test_inline_fence(void)
{
    char out[256];
    bool ok = command_extract("run ```pwd``` now", out, sizeof(out));
    EXPECT_TRUE(ok);
    EXPECT_STR(out, "pwd");
}

static void test_multiline_block_rejected(void)
{
    char out[256];
    // SAFETY: multi-line blocks must NOT be offered as a one-press Run command.
    bool ok = command_extract("```sh\ncd /tmp\nrm -rf junk\n```", out, sizeof(out));
    EXPECT_FALSE(ok);
    EXPECT_STR(out, "");
}

static void test_no_fence(void)
{
    char out[256];
    EXPECT_FALSE(command_extract("just prose, no code here", out, sizeof(out)));
    EXPECT_STR(out, "");
}

static void test_empty_fence(void)
{
    char out[256];
    EXPECT_FALSE(command_extract("```\n\n```", out, sizeof(out)));
}

int main(void)
{
    test_fenced_with_language_tag();
    test_inline_fence();
    test_multiline_block_rejected();
    test_no_fence();
    test_empty_fence();

    if (failures) {
        fprintf(stderr, "%d cmdextract test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
