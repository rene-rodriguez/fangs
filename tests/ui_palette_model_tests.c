#include "ui_palette_model.h"
#include "workflows.h"

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

#define EXPECT_INT_EQ(actual, expected) do { \
    int a__ = (actual); \
    int e__ = (expected); \
    if (a__ != e__) { \
        fprintf(stderr, "FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, e__, a__); \
        failures++; \
    } \
} while (0)

static bool has_action(const UiPaletteModel *m, FangsActionId id)
{
    for (int i = 0; i < ui_palette_model_match_count(m); i++) {
        const FangsAction *a = ui_palette_model_match_at(m, i);
        if (a && a->id == id)
            return true;
    }
    return false;
}

static void test_open_resets_query_and_shows_actions(void)
{
    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_set_query(&m, "split");
    ui_palette_model_move(&m, 1);

    ui_palette_model_open(&m);

    EXPECT_TRUE(ui_palette_model_is_open(&m));
    EXPECT_INT_EQ(ui_palette_model_selected(&m), 0);
    EXPECT_TRUE(ui_palette_model_query(&m)[0] == '\0');
    EXPECT_TRUE(ui_palette_model_match_count(&m) >= 12);
}

static void test_query_filters_by_label_name_and_detail(void)
{
    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_open(&m);

    ui_palette_model_set_query(&m, "split");

    EXPECT_TRUE(has_action(&m, FANGS_ACTION_SPLIT_RIGHT));
    EXPECT_TRUE(has_action(&m, FANGS_ACTION_SPLIT_DOWN));
    EXPECT_FALSE(has_action(&m, FANGS_ACTION_OPEN_SETTINGS));

    ui_palette_model_set_query(&m, "latest block");

    EXPECT_TRUE(has_action(&m, FANGS_ACTION_ASK_LATEST_BLOCK));
    EXPECT_FALSE(has_action(&m, FANGS_ACTION_SPLIT_RIGHT));
}

static void test_selection_wraps_across_matches(void)
{
    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_open(&m);
    ui_palette_model_set_query(&m, "split");

    int count = ui_palette_model_match_count(&m);
    EXPECT_TRUE(count >= 2);

    ui_palette_model_move(&m, -1);
    EXPECT_INT_EQ(ui_palette_model_selected(&m), count - 1);

    ui_palette_model_move(&m, 1);
    EXPECT_INT_EQ(ui_palette_model_selected(&m), 0);
}

static void test_accept_returns_selected_action_and_closes(void)
{
    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_open(&m);
    ui_palette_model_set_query(&m, "settings");

    FangsActionId accepted = ui_palette_model_accept(&m);

    EXPECT_INT_EQ((int)accepted, (int)FANGS_ACTION_OPEN_SETTINGS);
    EXPECT_FALSE(ui_palette_model_is_open(&m));
}

static void test_empty_result_accepts_nothing(void)
{
    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_open(&m);
    ui_palette_model_set_query(&m, "zzzzzz-no-match");

    EXPECT_INT_EQ(ui_palette_model_match_count(&m), 0);
    EXPECT_INT_EQ((int)ui_palette_model_accept(&m), (int)FANGS_ACTION_NONE);
    EXPECT_TRUE(ui_palette_model_is_open(&m));
}

static void test_workflows_are_filterable_palette_entries(void)
{
    WorkflowRegistry reg;
    workflows_init(&reg);
    workflows_add(&reg, "test", "Run Tests",
                  "cmake --build build && ctest --test-dir build --output-on-failure",
                  "Build and run tests");

    UiPaletteModel m;
    ui_palette_model_init(&m);
    ui_palette_model_set_workflows(&m, &reg);
    ui_palette_model_open(&m);
    ui_palette_model_set_query(&m, "run tests");

    EXPECT_INT_EQ(ui_palette_model_match_count(&m), 1);

    UiPaletteEntry entry = ui_palette_model_match_entry_at(&m, 0);
    EXPECT_INT_EQ((int)entry.type, (int)UI_PALETTE_ENTRY_WORKFLOW);
    EXPECT_INT_EQ(entry.workflow_index, 0);

    UiPaletteSelection selection = ui_palette_model_accept_selection(&m);
    EXPECT_INT_EQ((int)selection.type, (int)UI_PALETTE_SELECTION_WORKFLOW);
    EXPECT_INT_EQ(selection.workflow_index, 0);
    EXPECT_FALSE(ui_palette_model_is_open(&m));
}

int main(void)
{
    test_open_resets_query_and_shows_actions();
    test_query_filters_by_label_name_and_detail();
    test_selection_wraps_across_matches();
    test_accept_returns_selected_action_and_closes();
    test_empty_result_accepts_nothing();
    test_workflows_are_filterable_palette_entries();

    if (failures != 0) {
        fprintf(stderr, "%d palette model test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
