#include "kitty_images.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_INT(actual, expected) do { \
    int a__ = (actual); \
    int e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %s == %d, got %d\n", \
                __FILE__, __LINE__, #actual, e__, a__); \
        failures++; \
    } \
} while (0)

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_cache_key_equality_requires_same_image_metadata(void)
{
    KittyImageCacheKey a = {
        .image_id = 7,
        .image_handle = (const void *)0x1234,
        .data_ptr = (const void *)0x5678,
        .data_len = 16,
        .width = 2,
        .height = 2,
        .format = GHOSTTY_KITTY_IMAGE_FORMAT_RGBA,
    };
    KittyImageCacheKey b = a;
    EXPECT_TRUE(kitty_image_cache_key_equal(&a, &b));

    b.data_len = 12;
    EXPECT_TRUE(!kitty_image_cache_key_equal(&a, &b));

    b = a;
    b.format = GHOSTTY_KITTY_IMAGE_FORMAT_RGB;
    EXPECT_TRUE(!kitty_image_cache_key_equal(&a, &b));
}

static void test_convert_rgb_to_rgba(void)
{
    const uint8_t src[] = { 10, 20, 30, 40, 50, 60 };
    uint8_t dst[8] = {0};

    EXPECT_TRUE(kitty_image_decode_to_rgba(src, sizeof(src),
        GHOSTTY_KITTY_IMAGE_FORMAT_RGB, 2, 1, dst, sizeof(dst)));

    const uint8_t expected[] = { 10, 20, 30, 255, 40, 50, 60, 255 };
    EXPECT_TRUE(memcmp(dst, expected, sizeof(expected)) == 0);
}

static void test_convert_gray_to_rgba(void)
{
    const uint8_t src[] = { 9, 200 };
    uint8_t dst[8] = {0};

    EXPECT_TRUE(kitty_image_decode_to_rgba(src, sizeof(src),
        GHOSTTY_KITTY_IMAGE_FORMAT_GRAY, 2, 1, dst, sizeof(dst)));

    const uint8_t expected[] = { 9, 9, 9, 255, 200, 200, 200, 255 };
    EXPECT_TRUE(memcmp(dst, expected, sizeof(expected)) == 0);
}

static void test_convert_gray_alpha_to_rgba(void)
{
    const uint8_t src[] = { 7, 80, 210, 128 };
    uint8_t dst[8] = {0};

    EXPECT_TRUE(kitty_image_decode_to_rgba(src, sizeof(src),
        GHOSTTY_KITTY_IMAGE_FORMAT_GRAY_ALPHA, 2, 1, dst, sizeof(dst)));

    const uint8_t expected[] = { 7, 7, 7, 80, 210, 210, 210, 128 };
    EXPECT_TRUE(memcmp(dst, expected, sizeof(expected)) == 0);
}

static void test_layer_draw_points_match_ghostty_z_buckets(void)
{
    EXPECT_INT(kitty_image_layer_draw_point(GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_BG),
               KITTY_IMAGE_DRAW_BEFORE_BACKGROUNDS);
    EXPECT_INT(kitty_image_layer_draw_point(GHOSTTY_KITTY_PLACEMENT_LAYER_BELOW_TEXT),
               KITTY_IMAGE_DRAW_BEFORE_TEXT);
    EXPECT_INT(kitty_image_layer_draw_point(GHOSTTY_KITTY_PLACEMENT_LAYER_ABOVE_TEXT),
               KITTY_IMAGE_DRAW_AFTER_TEXT);
}

static void test_dest_rect_includes_pane_origin_padding_and_offsets(void)
{
    KittyImageRect rect = kitty_image_dest_rect(
        120, 40,   // pane origin
        6,         // terminal pad
        9, 18,     // cell size
        3, 4,      // viewport column/row
        5, 2,      // placement columns/rows
        7, 11);    // pixel offsets

    EXPECT_INT(rect.x, 120 + 6 + 3 * 9 + 7);
    EXPECT_INT(rect.y, 40 + 6 + 4 * 18 + 11);
    EXPECT_INT(rect.w, 5 * 9);
    EXPECT_INT(rect.h, 2 * 18);
}

static void test_dest_rect_handles_negative_viewport_position(void)
{
    KittyImageRect rect = kitty_image_dest_rect(
        20, 30,
        4,
        10, 20,
        -2, -1,
        3, 4,
        0, 0);

    EXPECT_INT(rect.x, 20 + 4 - 20);
    EXPECT_INT(rect.y, 30 + 4 - 20);
    EXPECT_INT(rect.w, 30);
    EXPECT_INT(rect.h, 80);
}

int main(void)
{
    test_cache_key_equality_requires_same_image_metadata();
    test_convert_rgb_to_rgba();
    test_convert_gray_to_rgba();
    test_convert_gray_alpha_to_rgba();
    test_layer_draw_points_match_ghostty_z_buckets();
    test_dest_rect_includes_pane_origin_padding_and_offsets();
    test_dest_rect_handles_negative_viewport_position();

    if (failures != 0) {
        fprintf(stderr, "%d kitty image test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
