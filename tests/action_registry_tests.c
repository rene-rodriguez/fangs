#include "action_registry.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define EXPECT_STR_EQ(actual, expected) do { \
    const char *a__ = (actual); \
    const char *e__ = (expected); \
    if (!a__ || strcmp(a__, e__) != 0) { \
        fprintf(stderr, "FAIL %s:%d: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, e__, a__ ? a__ : "(null)"); \
        failures++; \
    } \
} while (0)

static void test_registry_exposes_existing_core_actions(void)
{
    const FangsAction *settings = action_registry_find(FANGS_ACTION_OPEN_SETTINGS);
    const FangsAction *sidebar  = action_registry_find(FANGS_ACTION_TOGGLE_SIDEBAR);
    const FangsAction *latest   = action_registry_find(FANGS_ACTION_ASK_LATEST_BLOCK);
    const FangsAction *split    = action_registry_find(FANGS_ACTION_SPLIT_RIGHT);

    EXPECT_TRUE(settings != NULL);
    EXPECT_TRUE(sidebar != NULL);
    EXPECT_TRUE(latest != NULL);
    EXPECT_TRUE(split != NULL);

    EXPECT_STR_EQ(settings->name, "settings.open");
    EXPECT_STR_EQ(settings->label, "Open Settings");
    EXPECT_STR_EQ(sidebar->name, "ai.toggle_sidebar");
    EXPECT_STR_EQ(latest->name, "blocks.ask_latest");
    EXPECT_STR_EQ(split->name, "pane.split_right");
}

static void test_registry_ids_are_unique_and_described(void)
{
    int count = 0;
    const FangsAction *actions = action_registry_all(&count);

    EXPECT_TRUE(actions != NULL);
    EXPECT_TRUE(count >= 12);

    for (int i = 0; i < count; i++) {
        EXPECT_TRUE(actions[i].id != FANGS_ACTION_NONE);
        EXPECT_TRUE(actions[i].name && actions[i].name[0]);
        EXPECT_TRUE(actions[i].label && actions[i].label[0]);
        EXPECT_TRUE(actions[i].detail && actions[i].detail[0]);

        for (int j = i + 1; j < count; j++) {
            if (actions[i].id == actions[j].id) {
                fprintf(stderr, "FAIL duplicate id at %d and %d\n", i, j);
                failures++;
            }
            if (strcmp(actions[i].name, actions[j].name) == 0) {
                fprintf(stderr, "FAIL duplicate name \"%s\"\n", actions[i].name);
                failures++;
            }
        }
    }
}

static void test_find_unknown_action_returns_null(void)
{
    EXPECT_TRUE(action_registry_find(FANGS_ACTION_NONE) == NULL);
    EXPECT_TRUE(action_registry_find((FangsActionId)9999) == NULL);
}

int main(void)
{
    test_registry_exposes_existing_core_actions();
    test_registry_ids_are_unique_and_described();
    test_find_unknown_action_returns_null();

    if (failures != 0) {
        fprintf(stderr, "%d action registry test failure(s)\n", failures);
        return 1;
    }

    return 0;
}
