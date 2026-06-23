#include "cmdblocks_osc.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
    long a__ = (long)(actual); long e__ = (long)(expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %ld, got %ld\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

// Collect every mark in one buffer with a fresh parser.
static int collect(const char *s, CbHit *out, int max)
{
    CbParser p; cb_parser_reset(&p);
    size_t pos = 0, len = strlen(s);
    int n = 0;
    CbHit h;
    while (n < max && cb_parse_next(&p, (const uint8_t *)s, len, &pos, &h))
        out[n++] = h;
    return n;
}

// ST-terminated marks (ESC \) — the form our zsh/bash snippets emit.
static void test_st_terminated(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;A\x1b\\$ ls\r\n\x1b]133;D;0\x1b\\", h, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(h[0].mark, CB_MARK_PROMPT);
    EXPECT_EQ(h[1].mark, CB_MARK_DONE);
    EXPECT_EQ(h[1].code, 0);
}

// BEL-terminated marks (\a = 0x07) — the other legal OSC terminator.
// (Use \a not \x07: a \x escape greedily eats following hex digits, so
// "\x07cmd" would parse as 0x7c + "md".)
static void test_bel_terminated(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;A\acmd\x1b]133;D;1\a", h, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(h[0].mark, CB_MARK_PROMPT);
    EXPECT_EQ(h[1].mark, CB_MARK_DONE);
    EXPECT_EQ(h[1].code, 1);
}

// Non-zero exit code with multiple digits.
static void test_exit_code_multidigit(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;D;130\x1b\\", h, 8);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(h[0].mark, CB_MARK_DONE);
    EXPECT_EQ(h[0].code, 130);
}

// D with no code → unknown (-1). A;aid=... extra params still parse as PROMPT.
static void test_done_no_code_and_params(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;D\x1b\\\x1b]133;A;aid=42\x1b\\", h, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(h[0].mark, CB_MARK_DONE);
    EXPECT_EQ(h[0].code, -1);
    EXPECT_EQ(h[1].mark, CB_MARK_PROMPT);
}

// B and C are recognized too.
static void test_b_and_c(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;B\x1b\\\x1b]133;C\x1b\\", h, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(h[0].mark, CB_MARK_CMD);
    EXPECT_EQ(h[1].mark, CB_MARK_EXEC);
}

// Unrelated OSCs (window title OSC 0, hyperlink OSC 8) must be ignored.
static void test_ignores_other_osc(void)
{
    CbHit h[8];
    int n = collect("\x1b]0;my title\x07plain text\x1b]8;;http://x\x1b\\", h, 8);
    EXPECT_EQ(n, 0);
}

// The `end` offset must point just past the terminator (host flushes to it).
static void test_end_offset(void)
{
    const char *s = "ab\x1b]133;A\x1b\\cd";
    CbHit h[4];
    int n = collect(s, h, 4);
    EXPECT_EQ(n, 1);
    // "ab" = 2, "\x1b]133;A" = 7, "\x1b\\" = 2  → terminator ends at index 11.
    EXPECT_EQ(h[0].end, 11);
}

// A single mark split across two feed chunks must still be recognized.
static void test_split_across_chunks(void)
{
    CbParser p; cb_parser_reset(&p);
    const char *c1 = "output\x1b]133;D";
    const char *c2 = ";0\x1b\\next";
    CbHit h; size_t pos;

    pos = 0;
    EXPECT_TRUE(!cb_parse_next(&p, (const uint8_t *)c1, strlen(c1), &pos, &h));

    pos = 0;
    EXPECT_TRUE(cb_parse_next(&p, (const uint8_t *)c2, strlen(c2), &pos, &h));
    EXPECT_EQ(h.mark, CB_MARK_DONE);
    EXPECT_EQ(h.code, 0);
}

// A real precmd burst: D for the finished command immediately followed by A
// for the next prompt, in one chunk — the common interactive case.
static void test_done_then_prompt_burst(void)
{
    CbHit h[8];
    int n = collect("\x1b]133;D;0\x1b\\\x1b]133;A\x1b\\", h, 8);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(h[0].mark, CB_MARK_DONE);
    EXPECT_EQ(h[1].mark, CB_MARK_PROMPT);
}

int main(void)
{
    test_st_terminated();
    test_bel_terminated();
    test_exit_code_multidigit();
    test_done_no_code_and_params();
    test_b_and_c();
    test_ignores_other_osc();
    test_end_offset();
    test_split_across_chunks();
    test_done_then_prompt_burst();

    if (failures) {
        fprintf(stderr, "%d cmdblocks_osc test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
