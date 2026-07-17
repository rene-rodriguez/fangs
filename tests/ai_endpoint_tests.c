#include "ai_endpoint.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_URL(provider, configured, expected) do { \
    char out[512]; \
    bool ok__ = ai_endpoint_resolve((provider), (configured), out, sizeof(out)); \
    if (!ok__ || strcmp(out, (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: %s -> expected '%s', got '%s'\n", \
                __FILE__, __LINE__, (configured), (expected), ok__ ? out : "<error>"); \
        failures++; \
    } \
} while (0)

int main(void)
{
    EXPECT_URL("openai", "https://api.openai.com/v1",
               "https://api.openai.com/v1/chat/completions");
    EXPECT_URL("custom", "http://10.0.0.124:13305/v1",
               "http://10.0.0.124:13305/v1/chat/completions");
    EXPECT_URL("custom", "http://10.0.0.124:13305/api/v1/",
               "http://10.0.0.124:13305/api/v1/chat/completions");
    EXPECT_URL("custom", "http://localhost:8080",
               "http://localhost:8080/v1/chat/completions");
    EXPECT_URL("custom", "http://localhost:8080/v1/chat/completions",
               "http://localhost:8080/v1/chat/completions");
    EXPECT_URL("custom", "  http://localhost:8080/v1  ",
               "http://localhost:8080/v1/chat/completions");
    EXPECT_URL("custom", "http://localhost:8080/v1?tenant=local",
               "http://localhost:8080/v1/chat/completions?tenant=local");

    EXPECT_URL("anthropic", "https://api.anthropic.com",
               "https://api.anthropic.com/v1/messages");
    EXPECT_URL("anthropic", "https://api.anthropic.com/v1",
               "https://api.anthropic.com/v1/messages");
    EXPECT_URL("anthropic", "https://api.anthropic.com/v1/messages",
               "https://api.anthropic.com/v1/messages");

    EXPECT_URL("ollama", "http://localhost:11434",
               "http://localhost:11434/api/chat");
    EXPECT_URL("ollama", "http://localhost:11434/api",
               "http://localhost:11434/api/chat");
    EXPECT_URL("ollama", "http://localhost:11434/api/chat",
               "http://localhost:11434/api/chat");

    char tiny[8];
    if (ai_endpoint_resolve("custom", "http://localhost:8080/v1", tiny, sizeof(tiny))) {
        fprintf(stderr, "FAIL expected a too-small output buffer to be rejected\n");
        failures++;
    }
    if (ai_endpoint_resolve("custom", "", tiny, sizeof(tiny))) {
        fprintf(stderr, "FAIL expected an empty URL to be rejected\n");
        failures++;
    }

    if (failures == 0)
        printf("ai_endpoint_tests: ok\n");
    return failures ? 1 : 0;
}
