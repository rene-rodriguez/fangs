#include "sse.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_STR(actual, expected) do { \
    const char *a__ = (actual); const char *e__ = (expected); \
    if (strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

typedef struct { char reason[2048]; char answer[2048]; } Acc;

static void on_delta(void *ud, const char *text, bool is_reasoning)
{
    Acc *a = ud;
    char *dst = is_reasoning ? a->reason : a->answer;
    size_t cur = strlen(dst);
    snprintf(dst + cur, 2048 - cur, "%s", text);
}

static void test_basic_content(void)
{
    Acc a = {{0}, {0}};
    SseParser *p = sse_parser_new();
    const char *in =
        "data: {\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\", world\"}}]}\n"
        "data: [DONE]\n";
    sse_parser_feed(p, in, strlen(in), on_delta, &a);
    EXPECT_STR(a.answer, "Hello, world");
    EXPECT_STR(a.reason, "");
    sse_parser_free(p);
}

static void test_partial_line_across_feeds(void)
{
    Acc a = {{0}, {0}};
    SseParser *p = sse_parser_new();
    // A single SSE line split across two feed() calls (arbitrary byte boundary).
    const char *c1 = "data: {\"choices\":[{\"delta\":{\"content\":\"Hel";
    const char *c2 = "lo\"}}]}\n";
    sse_parser_feed(p, c1, strlen(c1), on_delta, &a);
    EXPECT_STR(a.answer, "");                 // nothing complete yet
    sse_parser_feed(p, c2, strlen(c2), on_delta, &a);
    EXPECT_STR(a.answer, "Hello");
    sse_parser_free(p);
}

static void test_reasoning_then_content(void)
{
    Acc a = {{0}, {0}};
    SseParser *p = sse_parser_new();
    const char *in =
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think\"}}]}\r\n"
        "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"ing\"}}]}\r\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\r\n";
    sse_parser_feed(p, in, strlen(in), on_delta, &a);
    EXPECT_STR(a.reason, "thinking");
    EXPECT_STR(a.answer, "answer");
    sse_parser_free(p);
}

static void test_ignores_non_data_and_blank(void)
{
    Acc a = {{0}, {0}};
    SseParser *p = sse_parser_new();
    const char *in =
        ": keep-alive comment\n"
        "\n"
        "event: message\n"
        "data: {\"choices\":[{\"delta\":{\"content\":\"X\"}}]}\n";
    sse_parser_feed(p, in, strlen(in), on_delta, &a);
    EXPECT_STR(a.answer, "X");
    sse_parser_free(p);
}

// Anthropic Messages stream: content_block_delta events with text_delta /
// thinking_delta, interleaved with non-content events that must be ignored.
static void test_anthropic_text_and_thinking(void)
{
    Acc a = {{0}, {0}};
    SseParser *p = sse_parser_new();
    const char *in =
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_1\"}}\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hmm\"}}\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"text_delta\",\"text\":\" there\"}}\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n";
    sse_parser_feed(p, in, strlen(in), on_delta, &a);
    EXPECT_STR(a.reason, "hmm");
    EXPECT_STR(a.answer, "Hello there");
    sse_parser_free(p);
}

int main(void)
{
    test_basic_content();
    test_partial_line_across_feeds();
    test_reasoning_then_content();
    test_ignores_non_data_and_blank();
    test_anthropic_text_and_thinking();

    if (failures) {
        fprintf(stderr, "%d sse test failure(s)\n", failures);
        return 1;
    }
    return 0;
}
