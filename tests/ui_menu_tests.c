// ui_menu tests — pure model only, no raylib.

#include "ui_menu.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); failures++; } } while (0)
#define EXPECT_INT(actual, expected) do { int a=(actual), e=(expected); if (a != e) { fprintf(stderr, "FAIL %s:%d: expected %d got %d\n", __FILE__, __LINE__, e, a); failures++; } } while (0)
#define EXPECT_STR(actual, expected) do { if (strcmp((actual), (expected)) != 0) { fprintf(stderr, "FAIL %s:%d: expected '%s', got '%s'\n", __FILE__, __LINE__, (expected), (actual)); failures++; } } while (0)

static void test_open_sets_state(void)
{
    UiMenuItem items[] = {
        { .label = "One", .tag = 1, .tint = { 255, 255, 255, 255 } },
        { .label = "Two", .tag = 2, .tint = { 255, 255, 255, 255 } },
    };
    UiMenu m;
    memset(&m, 0, sizeof(m));
    ui_menu_open(&m, items, 2, 100, 200);
    EXPECT_TRUE(m.open);
    EXPECT_INT(m.count, 2);
    EXPECT_STR(m.items[0].label, "One");
    EXPECT_STR(m.items[1].label, "Two");
    EXPECT_INT(m.x, 100);
    EXPECT_INT(m.y, 200);
    ui_menu_close(&m);
    EXPECT_TRUE(!m.open);
}

static void test_hit_inside_menu(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    UiMenuItem items[] = {
        { .label = "Alpha", .tag = 10, .tint = { 255, 255, 255, 255 } },
        { .label = "Beta",  .tag = 20, .tint = { 255, 255, 255, 255 } },
        { .label = "Gamma", .tag = 30, .tint = { 255, 255, 255, 255 } },
    };
    ui_menu_open(&m, items, 3, 50, 50);
    ui_menu_layout(&m, 800, 600);

    // Hit each item by computing its expected rect.
    for (int i = 0; i < 3; i++) {
        int iy = m.y + UI_MENU_PAD + i * UI_MENU_ITEM_H;
        int idx = ui_menu_hit(&m, m.x + 4, iy + UI_MENU_ITEM_H / 2);
        EXPECT_INT(idx, i);
    }
}

static void test_hit_outside_returns_minus_one(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    UiMenuItem items[] = {
        { .label = "X", .tag = 1, .tint = { 255, 255, 255, 255 } },
    };
    ui_menu_open(&m, items, 1, 100, 100);
    ui_menu_layout(&m, 800, 600);
    EXPECT_INT(ui_menu_hit(&m, 0, 0), -1);          // outside left
    EXPECT_INT(ui_menu_hit(&m, 999, 999), -1);       // outside far
    EXPECT_INT(ui_menu_hit(&m, m.x + 4, m.y - 2), -1); // above
}

static void test_separator_not_hittable(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    UiMenuItem items[] = {
        { .label = "A", .tag = 1, .tint = { 255, 255, 255, 255 } },
        { .separator = true },
        { .label = "B", .tag = 2, .tint = { 255, 255, 255, 255 } },
    };
    ui_menu_open(&m, items, 3, 50, 50);
    ui_menu_layout(&m, 800, 600);

    // Hit where separator would be.
    int sep_y = m.y + UI_MENU_PAD + 1 * UI_MENU_ITEM_H;
    int idx = ui_menu_hit(&m, m.x + 4, sep_y + UI_MENU_ITEM_H / 2);
    EXPECT_INT(idx, -1);

    // Item below separator still hittable.
    int b_y = m.y + UI_MENU_PAD + 2 * UI_MENU_ITEM_H;
    idx = ui_menu_hit(&m, m.x + 4, b_y + UI_MENU_ITEM_H / 2);
    EXPECT_INT(idx, 2);
}

static void test_layout_clamps_right_edge(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    UiMenuItem items[] = {
        { .label = "Wide", .tag = 1, .tint = { 255, 255, 255, 255 } },
    };
    // Anchor near right edge.
    ui_menu_open(&m, items, 1, 790, 50);
    ui_menu_layout(&m, 800, 600);
    // Menu should be clamped so right edge <= 800.
    EXPECT_TRUE(m.x + m.w <= 800);
}

static void test_layout_clamps_bottom_edge(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    UiMenuItem items[10];
    for (int i = 0; i < 10; i++) {
        snprintf(items[i].label, sizeof(items[i].label), "Item %d", i);
        items[i].tag = i;
        items[i].tint = (UiColor){ 255, 255, 255, 255 };
    }
    ui_menu_open(&m, items, 10, 50, 590);
    ui_menu_layout(&m, 600, 600);
    // Menu should be clamped so bottom edge <= 600.
    EXPECT_TRUE(m.y + m.h <= 600);
}

static void test_closed_menu_returns_minus_one(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    EXPECT_INT(ui_menu_hit(&m, 100, 100), -1);
    EXPECT_TRUE(!ui_menu_active(&m));
}

static void test_menu_active(void)
{
    UiMenu m;
    memset(&m, 0, sizeof(m));
    EXPECT_TRUE(!ui_menu_active(&m));
    UiMenuItem items[] = {
        { .label = "X", .tag = 1, .tint = { 255, 255, 255, 255 } },
    };
    ui_menu_open(&m, items, 1, 0, 0);
    EXPECT_TRUE(ui_menu_active(&m));
    ui_menu_close(&m);
    EXPECT_TRUE(!ui_menu_active(&m));
}

int main(void)
{
    test_open_sets_state();
    test_hit_inside_menu();
    test_hit_outside_returns_minus_one();
    test_separator_not_hittable();
    test_layout_clamps_right_edge();
    test_layout_clamps_bottom_edge();
    test_closed_menu_returns_minus_one();
    test_menu_active();
    if (failures)
        fprintf(stderr, "%d test(s) FAILED\n", failures);
    else
        printf("ALL ui_menu_tests PASSED\n");
    return failures ? 1 : 0;
}
