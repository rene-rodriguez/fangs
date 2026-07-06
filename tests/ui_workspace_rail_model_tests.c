#include "ui_workspace_rail_model.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_INT(actual, expected) do { \
    int a=(actual), e=(expected); if (a != e) { \
        fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); \
        failures++; \
    } \
} while (0)
#define EXPECT_STR(actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected '%s' got '%s'\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        failures++; \
    } \
} while (0)
#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static void test_row_ordering(void)
{
    // Two tabs, two panes — verify rows are in input order.
    WorkspaceRailInput tabs[2] = {
        { .id = 10, .label = "fangs", .branch = "main", .active = 0 },
        { .id = 20, .label = "other", .branch = "dev", .active = 1 },
    };
    WorkspaceRailInput panes[2] = {
        { .id = 11, .label = "src",   .branch = "main", .active = 1 },
        { .id = 12, .label = "tests", .branch = "main", .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 2, panes, 2, &st, 0);

    EXPECT_INT(view.tab_count, 2);
    EXPECT_INT(view.panes[0].id, 11);
    EXPECT_INT(view.panes[1].id, 12);
    EXPECT_STR(view.tabs[0].label, "fangs");
    EXPECT_STR(view.tabs[1].label, "other");
    EXPECT_STR(view.tabs[0].branch, "main");
    EXPECT_STR(view.tabs[1].branch, "dev");
}

static void test_active_markers(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "a", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput panes[2] = {
        { .id = 2, .label = "b", .branch = NULL, .active = 1 },
        { .id = 3, .label = "c", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 2, &st, 0);

    EXPECT_INT(view.tabs[0].active, 1);
    EXPECT_INT(view.panes[0].active, 1);
    EXPECT_INT(view.panes[1].active, 0);
}

static void test_compact_mode(void)
{
    WorkspaceRailInput tabs[2] = {
        { .id = 1, .label = "fangs", .branch = "main", .active = 1 },
        { .id = 2, .label = "other", .branch = "dev",  .active = 0 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 3, .label = "src", .branch = "main", .active = 1 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 2, panes, 1, &st, 1);

    // Compact mode: labels should be numeric, branches empty.
    EXPECT_STR(view.tabs[0].label, "1");
    EXPECT_STR(view.tabs[1].label, "2");
    EXPECT_STR(view.panes[0].label, "1");
    EXPECT_STR(view.tabs[0].branch, "");
    EXPECT_STR(view.tabs[1].branch, "");
    EXPECT_STR(view.panes[0].branch, "");
    EXPECT_INT(view.compact, 1);
}

static void test_notification_text(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "a", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput panes[2] = {
        { .id = 10, .label = "p1", .branch = NULL, .active = 1 },
        { .id = 11, .label = "p2", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    // Pane 11 has a failed command in background.
    workspace_status_note_command(&st, 11, false, 1);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 2, &st, 0);

    EXPECT_INT(view.panes[1].attention, WORKSPACE_ATTENTION_WARN);
    EXPECT_TRUE(view.notification[0] != '\0');
}

static void test_attention_from_status(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "a", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 10, .label = "p1", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    // Background output on pane 10.
    workspace_status_note_output(&st, 10, false, 100);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 1, &st, 0);

    EXPECT_INT(view.panes[0].attention, WORKSPACE_ATTENTION_INFO);
}

int main(void)
{
    test_row_ordering();
    test_active_markers();
    test_compact_mode();
    test_notification_text();
    test_attention_from_status();
    return failures ? 1 : 0;
}
