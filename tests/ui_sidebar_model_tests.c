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

int main(void)
{
    test_enter_before_textbox_focus_change_submits_text();
    test_send_button_submits_text();
    test_empty_input_never_submits();
    test_no_trigger_does_not_submit();
    test_sidebar_visible_unfocused_allows_pty_input();
    test_sidebar_focused_blocks_pty_input();
    test_modal_shortcuts_and_child_exit_block_pty_input();

    if (failures != 0) {
        fprintf(stderr, "%d sidebar model test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
