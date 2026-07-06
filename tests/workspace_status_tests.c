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

int main(void)
{
    test_background_output_marks_info();
    test_active_output_does_not_mark_unread();
    test_failed_background_command_marks_warn();
    test_focus_clears_one_pane();
    test_tab_aggregate_and_notification();
    test_prune_removes_missing();
    test_remove_single_pane();
    return failures ? 1 : 0;
}
