#include "redact.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); const char *e__ = (expected); \
    if (strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

static bool contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

static void test_redacts_known_key_shapes(void)
{
    char *r;

    r = redact_secrets("here is sk-ABCDEF0123456789XYZ done");
    EXPECT_TRUE(!contains(r, "sk-ABCDEF0123456789XYZ"));
    EXPECT_TRUE(contains(r, "<redacted>"));
    EXPECT_TRUE(contains(r, "here is") && contains(r, "done"));   // surrounding text kept
    free(r);

    r = redact_secrets("aws AKIAIOSFODNN7EXAMPLE end");
    EXPECT_TRUE(!contains(r, "AKIAIOSFODNN7EXAMPLE"));
    free(r);

    r = redact_secrets("Authorization: Bearer abcdef0123456789TOKEN");
    EXPECT_TRUE(!contains(r, "abcdef0123456789TOKEN"));
    EXPECT_TRUE(contains(r, "Bearer"));        // header name preserved
    free(r);
}

static void test_redacts_key_value_assignments(void)
{
    char *r;

    r = redact_secrets("export API_KEY=supersecretvalue123");
    EXPECT_TRUE(!contains(r, "supersecretvalue123"));
    EXPECT_TRUE(contains(r, "API_KEY="));      // key kept, value scrubbed
    free(r);

    r = redact_secrets("password=hunter2");
    EXPECT_TRUE(!contains(r, "hunter2"));
    free(r);

    r = redact_secrets("the token: ghp_0123456789abcdefXYZ");
    EXPECT_TRUE(!contains(r, "ghp_0123456789abcdefXYZ"));
    free(r);
}

static void test_keeps_ordinary_output(void)
{
    char *r = redact_secrets("ls -la /usr/local/bin && echo done");
    EXPECT_STR(r, "ls -la /usr/local/bin && echo done");
    free(r);
}

static void test_text_tail_lines_and_bytes(void)
{
    char *r = text_tail("l1\nl2\nl3\nl4\nl5\n", 2, 0);
    EXPECT_STR(r, "l4\nl5\n");
    free(r);

    r = text_tail("only one line", 5, 0);
    EXPECT_STR(r, "only one line");
    free(r);

    // Byte cap aligns to a line boundary (drops the partial first line).
    r = text_tail("aaaa\nbbbb\ncccc\n", 0, 6);
    EXPECT_STR(r, "cccc\n");
    free(r);
}

int main(void)
{
    test_redacts_known_key_shapes();
    test_redacts_key_value_assignments();
    test_keeps_ordinary_output();
    test_text_tail_lines_and_bytes();

    if (failures) {
        fprintf(stderr, "%d redact test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
