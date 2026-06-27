#include "ui_sidebar_model.h"

#include <stdio.h>

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

#define EXPECT_FLOAT_BETWEEN(actual, min, max) do { \
    float a__ = (actual); \
    if (!(a__ > (min) && a__ < (max))) { \
        fprintf(stderr, "FAIL %s:%d: expected %s between %.3f and %.3f, got %.3f\n", \
                __FILE__, __LINE__, #actual, (double)(min), (double)(max), (double)a__); \
        failures++; \
    } \
} while (0)

#define EXPECT_FLOAT_EQ(actual, expected) do { \
    float a__ = (actual); \
    float e__ = (expected); \
    float d__ = a__ > e__ ? a__ - e__ : e__ - a__; \
    if (d__ > 0.001f) { \
        fprintf(stderr, "FAIL %s:%d: expected %.3f, got %.3f\n", \
                __FILE__, __LINE__, (double)e__, (double)a__); \
        failures++; \
    } \
} while (0)

static void test_enter_before_textbox_focus_change_submits_text(void)
{
    EXPECT_TRUE(ui_sidebar_should_submit(true, false, "explain this error"));
}

static void test_send_button_submits_text(void)
{
    EXPECT_TRUE(ui_sidebar_should_submit(false, true, "explain this error"));
}

static void test_empty_input_never_submits(void)
{
    EXPECT_FALSE(ui_sidebar_should_submit(true, false, ""));
    EXPECT_FALSE(ui_sidebar_should_submit(false, true, ""));
}

static void test_no_trigger_does_not_submit(void)
{
    EXPECT_FALSE(ui_sidebar_should_submit(false, false, "draft"));
}

static void test_sidebar_visible_unfocused_allows_pty_input(void)
{
    EXPECT_TRUE(ui_sidebar_allows_pty_input(false, false, true, false, false, false));
}

static void test_sidebar_focused_blocks_pty_input(void)
{
    EXPECT_FALSE(ui_sidebar_allows_pty_input(false, false, true, true, false, false));
}

static void test_modal_shortcuts_and_child_exit_block_pty_input(void)
{
    EXPECT_FALSE(ui_sidebar_allows_pty_input(true, false, false, false, false, false));
    EXPECT_FALSE(ui_sidebar_allows_pty_input(false, true, false, false, false, false));
    EXPECT_FALSE(ui_sidebar_allows_pty_input(false, false, false, false, true, false));
    EXPECT_FALSE(ui_sidebar_allows_pty_input(false, false, false, false, false, true));
}

static void test_wheel_up_latches_manual_scroll(void)
{
    bool user_scrolled_up = false;
    float offset = ui_sidebar_apply_wheel_scroll(100.0f, 1.0f, 32.0f,
                                                 100.0f, &user_scrolled_up);

    EXPECT_FLOAT_EQ(offset, 68.0f);
    EXPECT_TRUE(user_scrolled_up);
}

static void test_wheel_down_to_bottom_resumes_auto_follow(void)
{
    bool user_scrolled_up = true;
    float offset = ui_sidebar_apply_wheel_scroll(68.0f, -10.0f, 32.0f,
                                                 100.0f, &user_scrolled_up);

    EXPECT_FLOAT_EQ(offset, 100.0f);
    EXPECT_FALSE(user_scrolled_up);
}

static void test_stream_follow_smoothly_moves_toward_bottom(void)
{
    float offset = ui_sidebar_smooth_follow_scroll(0.0f, 100.0f,
                                                   false, 1.0f / 60.0f);

    EXPECT_FLOAT_BETWEEN(offset, 0.0f, 100.0f);
}

static void test_stream_follow_yields_to_manual_scroll(void)
{
    float offset = ui_sidebar_smooth_follow_scroll(20.0f, 100.0f,
                                                   true, 1.0f / 60.0f);

    EXPECT_FLOAT_EQ(offset, 20.0f);
}

int main(void)
{
    test_enter_before_textbox_focus_change_submits_text();
    test_send_button_submits_text();
    test_empty_input_never_submits();
    test_no_trigger_does_not_submit();
    test_sidebar_visible_unfocused_allows_pty_input();
    test_sidebar_focused_blocks_pty_input();
    test_modal_shortcuts_and_child_exit_block_pty_input();
    test_wheel_up_latches_manual_scroll();
    test_wheel_down_to_bottom_resumes_auto_follow();
    test_stream_follow_smoothly_moves_toward_bottom();
    test_stream_follow_yields_to_manual_scroll();

    if (failures != 0) {
        fprintf(stderr, "%d sidebar model test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
