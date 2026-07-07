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

static void test_tab_attention_contributes_to_notification(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 42, .label = "hidden", .branch = "main", .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_command(&st, 42, false, 5);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, NULL, 0, &st, 0);

    EXPECT_INT(view.tabs[0].attention, WORKSPACE_ATTENTION_WARN);
    EXPECT_TRUE(strstr(view.notification, "exit 5") != NULL);
}

static void test_title_overrides_label(void)
{
    WorkspaceRailInput tabs[2] = {
        { .id = 1, .label = "fangs", .branch = "main", .title = "✳ fixing tests", .active = 1 },
        { .id = 2, .label = "api",   .branch = "dev",  .title = "",               .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 2, NULL, 0, &st, 0);

    // Non-ASCII is stripped for display (the UI font atlas is basic Latin).
    EXPECT_STR(view.tabs[0].label, "fixing tests");
    EXPECT_STR(view.tabs[1].label, "api");
}

static void test_custom_name_overrides_title_and_label(void)
{
    WorkspaceRailInput tabs[3] = {
        { .id = 1, .label = "fangs", .branch = "main", .title = "agent title",
          .name = "auth refactor", .active = 1 },
        { .id = 2, .label = "fangs", .branch = "main", .title = "agent title",
          .name = "", .active = 0 },
        { .id = 3, .label = "fangs", .branch = "main", .title = "agent title",
          .name = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 3, NULL, 0, &st, 0);

    EXPECT_STR(view.tabs[0].label, "auth refactor");
    EXPECT_STR(view.tabs[1].label, "agent title");   // empty name falls back
    EXPECT_STR(view.tabs[2].label, "agent title");   // NULL name falls back
}

static void test_single_pane_hides_pane_section(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "a", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput one_pane[1] = {
        { .id = 2, .label = "a", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput two_panes[2] = {
        { .id = 2, .label = "a", .branch = NULL, .active = 1 },
        { .id = 3, .label = "b", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, one_pane, 1, &st, 0);
    EXPECT_INT(view.show_panes, 0);

    workspace_rail_build(&view, tabs, 1, two_panes, 2, &st, 0);
    EXPECT_INT(view.show_panes, 1);
}

static void test_layout_positions(void)
{
    WorkspaceRailInput tabs[2] = {
        { .id = 1, .label = "a", .branch = NULL, .active = 1 },
        { .id = 2, .label = "b", .branch = NULL, .active = 0 },
    };
    WorkspaceRailInput panes[2] = {
        { .id = 3, .label = "a", .branch = NULL, .active = 1 },
        { .id = 4, .label = "b", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 2, panes, 2, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // No notification: tabs start right below the header.
    EXPECT_INT(view.notif_h, 0);
    EXPECT_INT(view.tabs[0].y, WORKSPACE_RAIL_HEADER_H);
    EXPECT_INT(view.tabs[1].y, WORKSPACE_RAIL_HEADER_H + WORKSPACE_RAIL_ROW_H);

    // Splits section sits after the tab rows; panes follow it.
    EXPECT_INT(view.section_y, WORKSPACE_RAIL_HEADER_H + 2 * WORKSPACE_RAIL_ROW_H);
    EXPECT_INT(view.panes[0].y, view.section_y + WORKSPACE_RAIL_SECTION_H);

    // Footer is pinned to the bottom.
    EXPECT_INT(view.footer_y, 800 - WORKSPACE_RAIL_FOOTER_H);

    // With an unread event the notification strip pushes rows down.
    workspace_status_note_command(&st, 2, false, 1);
    workspace_rail_build(&view, tabs, 2, panes, 2, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);
    EXPECT_INT(view.notif_h, WORKSPACE_RAIL_NOTIF_H);
    EXPECT_INT(view.tabs[0].y, WORKSPACE_RAIL_HEADER_H + WORKSPACE_RAIL_NOTIF_H);
}

static void test_hit_targets(void)
{
    WorkspaceRailInput tabs[2] = {
        { .id = 1, .label = "act", .branch = NULL, .active = 1 },
        { .id = 2, .label = "b", .branch = NULL, .active = 0 },
    };
    WorkspaceRailInput panes[2] = {
        { .id = 3, .label = "act", .branch = NULL, .active = 1 },
        { .id = 4, .label = "b", .branch = NULL, .active = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);
    workspace_status_note_notify(&st, 2, false, "needs input");

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 2, panes, 2, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // Outside the rail.
    WorkspaceRailAction act = workspace_rail_hit(&view, 300, 100);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_NONE);

    // "+" button.
    act = workspace_rail_hit(&view, view.plus_x + 1, view.plus_y + 1);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_NEW_TAB);

    // Notification strip jumps to the offending pane.
    act = workspace_rail_hit(&view, 10, view.notif_y + 1);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_JUMP_ATTENTION);
    EXPECT_TRUE(act.pane_id == 2);

    // Second tab row.
    act = workspace_rail_hit(&view, 10, view.tabs[1].y + 1);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_SWITCH_TAB);
    EXPECT_INT(act.index, 1);

    // Second pane row.
    act = workspace_rail_hit(&view, 10, view.panes[1].y + 1);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_FOCUS_PANE);
    EXPECT_INT(act.index, 1);
    EXPECT_TRUE(act.pane_id == 4);

    // Empty space below the rows.
    act = workspace_rail_hit(&view, 10, view.panes[1].y + view.panes[1].h + 5);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_NONE);
}

static void test_hit_skips_hidden_pane_section(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "act", .branch = NULL, .active = 1 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 2, .label = "act", .branch = NULL, .active = 1 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 1, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // Where the pane row would be, but the section is hidden.
    WorkspaceRailAction act = workspace_rail_hit(&view, 10,
        view.tabs[0].y + view.tabs[0].h + 5);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_NONE);
}

static void test_ports_copy_from_inputs(void)
{
    // Port data should flow from inputs to rows without modification.
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "dev", .branch = NULL, .active = 1,
          .ports = {3000, 5173, 0}, .port_count = 2 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, NULL, 0, &st, 0);

    EXPECT_INT(view.tabs[0].port_count, 2);
    EXPECT_INT(view.tabs[0].ports[0], 3000);
    EXPECT_INT(view.tabs[0].ports[1], 5173);
    EXPECT_INT(view.tabs[0].ports[2], 0);
}

static void test_port_chip_layout_computed(void)
{
    // After layout, port chips should have non-zero rects within the row.
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "dev", .branch = NULL, .active = 1,
          .ports = {3000, 5173, 0}, .port_count = 2 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, NULL, 0, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // Port chips should be positioned inside the row with non-zero size.
    EXPECT_TRUE(view.tabs[0].port_w[0] > 0);
    EXPECT_TRUE(view.tabs[0].port_w[1] > 0);
    EXPECT_INT(view.tabs[0].port_w[2], 0);   // no third chip
    EXPECT_TRUE(view.tabs[0].port_h > 0);
    // Chips should be right-aligned: port_x[0] > port_x[1] (rendered right-to-left but
    // the array is in original order; rightmost = first chip from right = last index)
    // Actually: row_layout_port_chips iterates right to left, so port_x[1] (5173) is placed
    // first (rightmost), then port_x[0] (3000) is placed to its left.
    EXPECT_TRUE(view.tabs[0].port_x[1] > view.tabs[0].port_x[0]);
}

static void test_port_chip_hit(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "dev", .branch = NULL, .active = 1,
          .ports = {3000, 5173, 0}, .port_count = 2 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 2, .label = "p1", .branch = NULL, .active = 1,
          .ports = {0, 0, 0}, .port_count = 0 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 1, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // Click on the first port chip (3000).
    int cx = view.tabs[0].port_x[0] + 2;
    int cy = view.tabs[0].port_y + 2;
    WorkspaceRailAction act = workspace_rail_hit(&view, cx, cy);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_OPEN_PORT);
    EXPECT_INT(act.port, 3000);
}

static void test_row_hit_still_switches_tab_when_ports_present(void)
{
    // Clicking on the row label area (left side) should switch tab, not open port.
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "dev", .branch = NULL, .active = 1,
          .ports = {3000, 5173, 0}, .port_count = 2 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, NULL, 0, &st, 0);
    workspace_rail_layout(&view, 0, 0, 260, 800);

    // Click far left of the row (label area) — should switch tab, not open port.
    WorkspaceRailAction act = workspace_rail_hit(&view, 5, view.tabs[0].y + 2);
    EXPECT_INT(act.type, WORKSPACE_RAIL_ACTION_SWITCH_TAB);
}

static void test_working_flag_propagates_to_rows(void)
{
    WorkspaceRailInput tabs[1] = {
        { .id = 1, .label = "agent", .branch = "main", .active = 1, .working = 1 },
    };
    WorkspaceRailInput panes[1] = {
        { .id = 2, .label = "agent", .branch = "main", .active = 1, .working = 1 },
    };
    WorkspaceStatus st;
    workspace_status_init(&st);

    WorkspaceRailView view;
    workspace_rail_build(&view, tabs, 1, panes, 1, &st, 0);

    EXPECT_INT(view.tabs[0].working, 1);
    EXPECT_INT(view.panes[0].working, 1);
}

int main(void)
{
    test_row_ordering();
    test_active_markers();
    test_compact_mode();
    test_notification_text();
    test_attention_from_status();
    test_tab_attention_contributes_to_notification();
    test_title_overrides_label();
    test_custom_name_overrides_title_and_label();
    test_single_pane_hides_pane_section();
    test_layout_positions();
    test_hit_targets();
    test_hit_skips_hidden_pane_section();
    test_working_flag_propagates_to_rows();
    test_ports_copy_from_inputs();
    test_port_chip_layout_computed();
    test_port_chip_hit();
    test_row_hit_still_switches_tab_when_ports_present();
    return failures ? 1 : 0;
}
