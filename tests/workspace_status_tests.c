#include "workspace_status.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_INT(actual, expected) do { int a=(actual), e=(expected); if (a != e) { fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s', got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static void test_background_output_marks_info(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 12);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_INFO);
}

static void test_active_output_does_not_mark_unread(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, true, 12);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
}

static void test_failed_background_command_marks_warn(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_command(&st, 10, false, 2);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    const char *text = workspace_status_text(&st, 10);
    EXPECT_TRUE(strstr(text, "exit 2") != NULL);
}

static void test_background_notify_marks_warn_with_text(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 12);
    workspace_status_note_notify(&st, 10, false, "Claude needs your input");
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    EXPECT_STR(workspace_status_text(&st, 10), "Claude needs your input");
}

static void test_notify_empty_text_uses_default(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 10, false, "");
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    EXPECT_STR(workspace_status_text(&st, 10), "needs attention");
}

static void test_focused_notify_does_not_mark(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 10, true, "ping");
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
}

static void test_command_warning_does_not_downgrade_exit_error(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_child_exit(&st, 10, false, 9);
    workspace_status_note_command(&st, 10, false, 2);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_ERROR);
    EXPECT_TRUE(strstr(workspace_status_text(&st, 10), "process exited") != NULL);
}

static void test_focus_clears_one_pane(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_output(&st, 20, false, 1);
    workspace_status_clear(&st, 10);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
    EXPECT_INT(workspace_status_level(&st, 20), WORKSPACE_ATTENTION_INFO);
}

static void test_tab_aggregate_and_notification(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    uint64_t panes[] = { 10, 20 };
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_command(&st, 20, false, 7);
    EXPECT_INT(workspace_status_highest(&st, panes, 2), WORKSPACE_ATTENTION_WARN);
    char out[128];
    workspace_status_notification(&st, panes, 2, out, (int)sizeof(out));
    EXPECT_TRUE(strstr(out, "exit 7") != NULL);
}

static void test_prune_removes_missing(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_output(&st, 20, false, 1);
    workspace_status_note_output(&st, 30, false, 1);
    uint64_t keep[] = { 10, 30 };
    workspace_status_prune(&st, keep, 2);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_INFO);
    EXPECT_INT(workspace_status_level(&st, 20), WORKSPACE_ATTENTION_NONE);
    EXPECT_INT(workspace_status_level(&st, 30), WORKSPACE_ATTENTION_INFO);
}

static void test_remove_single_pane(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 1);
    workspace_status_note_output(&st, 20, false, 1);
    workspace_status_remove(&st, 10);
    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
    EXPECT_INT(workspace_status_level(&st, 20), WORKSPACE_ATTENTION_INFO);
}

static void test_active_pane_can_be_marked_when_caller_says_not_focused(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_notify(&st, 10, false, "approve?");

    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_WARN);
    EXPECT_TRUE(strstr(workspace_status_text(&st, 10), "approve?") != NULL);
}

static void test_output_marks_working_without_unread_when_focused(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_output_at(&st, 10, true, 42, 1000);

    EXPECT_INT(workspace_status_level(&st, 10), WORKSPACE_ATTENTION_NONE);
    EXPECT_TRUE(workspace_status_is_working_at(&st, 10, 1500));
}

static void test_working_expires_after_silence(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    workspace_status_note_output_at(&st, 10, false, 42, 1000);

    EXPECT_TRUE(workspace_status_is_working_at(&st, 10, 2999));
    EXPECT_TRUE(!workspace_status_is_working_at(&st, 10, 3001));
}

int main(void)
{
    test_background_output_marks_info();
    test_active_output_does_not_mark_unread();
    test_failed_background_command_marks_warn();
    test_background_notify_marks_warn_with_text();
    test_notify_empty_text_uses_default();
    test_focused_notify_does_not_mark();
    test_command_warning_does_not_downgrade_exit_error();
    test_focus_clears_one_pane();
    test_tab_aggregate_and_notification();
    test_prune_removes_missing();
    test_remove_single_pane();
    test_active_pane_can_be_marked_when_caller_says_not_focused();
    test_output_marks_working_without_unread_when_focused();
    test_working_expires_after_silence();
    return failures ? 1 : 0;
}
