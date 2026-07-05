#include "term_engine.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ghostty/vt.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_U64(actual, expected) do { \
    uint64_t a__ = (actual); \
    uint64_t e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == %llu, got %llu\n", \
                __FILE__, __LINE__, #actual, \
                (unsigned long long)e__, (unsigned long long)a__); \
        failures++; \
    } \
} while (0)

static uint64_t kitty_storage_limit(TermEngine *te)
{
    uint64_t limit = 0;
    EXPECT_TRUE(ghostty_terminal_get(term_engine_terminal(te),
        GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_STORAGE_LIMIT, &limit) == GHOSTTY_SUCCESS);
    return limit;
}

static bool kitty_medium(TermEngine *te, GhosttyTerminalData data)
{
    bool enabled = false;
    EXPECT_TRUE(ghostty_terminal_get(term_engine_terminal(te), data, &enabled) == GHOSTTY_SUCCESS);
    return enabled;
}

static void test_kitty_images_enabled_uses_configured_storage(void)
{
    TermEngine *te = term_engine_create(80, 24, 8, 16, 1000, true, 96);
    EXPECT_TRUE(te != NULL);
    if (!te)
        return;

    EXPECT_U64(kitty_storage_limit(te), 96ULL * 1024ULL * 1024ULL);
    EXPECT_TRUE(kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_FILE));
    EXPECT_TRUE(kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_TEMP_FILE));
    EXPECT_TRUE(kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_SHARED_MEM));

    term_engine_destroy(te);
}

static void test_kitty_images_disabled_sets_zero_storage_and_media_off(void)
{
    TermEngine *te = term_engine_create(80, 24, 8, 16, 1000, false, 64);
    EXPECT_TRUE(te != NULL);
    if (!te)
        return;

    EXPECT_U64(kitty_storage_limit(te), 0);
    EXPECT_TRUE(!kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_FILE));
    EXPECT_TRUE(!kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_TEMP_FILE));
    EXPECT_TRUE(!kitty_medium(te, GHOSTTY_TERMINAL_DATA_KITTY_IMAGE_MEDIUM_SHARED_MEM));

    term_engine_destroy(te);
}

int main(void)
{
    test_kitty_images_enabled_uses_configured_storage();
    test_kitty_images_disabled_sets_zero_storage_and_media_off();

    if (failures != 0) {
        fprintf(stderr, "%d term engine test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
