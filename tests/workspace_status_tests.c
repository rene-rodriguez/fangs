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

static void test_last_output_ms_tracks_most_recent_output(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);

    EXPECT_INT((int)workspace_status_last_output_ms(&st, 10), 0);

    workspace_status_note_output_at(&st, 10, false, 42, 1000);
    EXPECT_INT((int)workspace_status_last_output_ms(&st, 10), 1000);

    workspace_status_note_output_at(&st, 10, false, 7, 4200);
    EXPECT_INT((int)workspace_status_last_output_ms(&st, 10), 4200);

    EXPECT_INT((int)workspace_status_last_output_ms(&st, 999), 0);
}

/* --- Event ring buffer tests --- */

static void test_event_appended_on_notify(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 10, false, "help");
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    EXPECT_INT(n, 1);
    EXPECT_INT((int)evs[0].pane_id, 10);
    EXPECT_INT((int)evs[0].level, WORKSPACE_ATTENTION_WARN);
    EXPECT_STR(evs[0].text, "help");
}

static void test_event_appended_on_command_fail(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_command(&st, 20, false, 3);
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    EXPECT_INT(n, 1);
    EXPECT_INT((int)evs[0].pane_id, 20);
    EXPECT_INT((int)evs[0].level, WORKSPACE_ATTENTION_WARN);
    EXPECT_TRUE(strstr(evs[0].text, "exit 3") != NULL);
}

static void test_event_appended_on_child_exit(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_child_exit(&st, 30, false, 9);
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    EXPECT_INT(n, 1);
    EXPECT_INT((int)evs[0].pane_id, 30);
    EXPECT_INT((int)evs[0].level, WORKSPACE_ATTENTION_ERROR);
    EXPECT_TRUE(strstr(evs[0].text, "process exited") != NULL);
}

static void test_output_does_not_push_event(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_output(&st, 10, false, 1);
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    EXPECT_INT(n, 0);
}

static void test_event_newest_first(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 1, false, "first");
    workspace_status_note_notify(&st, 2, false, "second");
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    EXPECT_INT(n, 2);
    EXPECT_STR(evs[0].text, "second");
    EXPECT_STR(evs[1].text, "first");
}

static void test_event_ring_wraps(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    // Fill past event max
    for (int i = 0; i < WORKSPACE_STATUS_EVENT_MAX + 5; i++) {
        char label[8];
        snprintf(label, sizeof(label), "e%d", i);
        workspace_status_note_notify(&st, 100 + i, false, label);
    }
    WorkspaceStatusEvent evs[4];
    int n = workspace_status_events(&st, evs, 4);
    // Newest first, limited by max size
    EXPECT_INT(n, 4);
    EXPECT_STR(evs[0].text, "e36");  // 32 + 5 - 1 = index 36
    EXPECT_INT(workspace_status_unseen(&st, 0), WORKSPACE_STATUS_EVENT_MAX);
}

static void test_unseen_count(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 1, false, "a");
    workspace_status_note_notify(&st, 2, false, "b");
    EXPECT_INT(workspace_status_unseen(&st, 0), 2);
    EXPECT_INT(workspace_status_unseen(&st, 2), 0);
    EXPECT_INT(workspace_status_unseen(&st, 1), 1);
    workspace_status_note_notify(&st, 3, false, "c");
    EXPECT_INT(workspace_status_unseen(&st, 2), 1);
}

static void test_events_clear_resets(void)
{
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 1, false, "x");
    workspace_status_events_clear(&st);
    WorkspaceStatusEvent evs[4];
    EXPECT_INT(workspace_status_events(&st, evs, 4), 0);
    EXPECT_INT(workspace_status_unseen(&st, 0), 0);
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
    test_last_output_ms_tracks_most_recent_output();
    test_event_appended_on_notify();
    test_event_appended_on_command_fail();
    test_event_appended_on_child_exit();
    test_output_does_not_push_event();
    test_event_newest_first();
    test_event_ring_wraps();
    test_unseen_count();
    test_events_clear_resets();
    return failures ? 1 : 0;
}
