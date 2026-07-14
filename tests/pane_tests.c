// pane tree unit tests: leaf creation, split, close, navigation, ratio.
// Pure — no window or Session dependency (we mock Session* as opaque).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pane.h"
#include "session.h"

// We treat Session* as an opaque token for testing. The pane tree never
// dereferences the pointer itself — only passes it through to leaf nodes.
// We use small integers cast through intptr_t so we can track which session
// ends up in which leaf.
#define mock_session(n)  ((Session *)(intptr_t)(n))

// Stub session_destroy so the test binary links without a real session.o.
// The mocked Session pointers are never actually allocated, so we just
// no-op here (the real session_destroy would crash on them too).
void session_destroy(Session *s) { (void)s; }

static int failures = 0;
static int total = 0;

#define CHECK(cond, msg) do {                                          \
    total++;                                                           \
    if (!(cond)) {                                                     \
        fprintf(stderr, "FAIL [%d] %s\n", total, msg);                 \
        failures++;                                                    \
    } else {                                                           \
        fprintf(stderr, "ok   [%d] %s\n", total, msg);                 \
    }                                                                  \
} while(0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_create_leaf(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    CHECK(l != NULL, "pane_leaf returns non-NULL");
    CHECK(l->kind == PANE_LEAF, "leaf kind is PANE_LEAF");
    CHECK(l->parent == NULL, "leaf parent is NULL");
    CHECK(l->leaf.session == mock_session(1), "leaf session is correct");
    pane_destroy(l);
}

static void test_split_leaf(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);

    CHECK(r != NULL, "split returns non-NULL");
    CHECK(r->kind == PANE_HSPLIT, "root is now HSPLIT");
    CHECK(r->split.left->kind == PANE_LEAF, "left child is leaf");
    CHECK(r->split.right->kind == PANE_LEAF, "right child is leaf");
    CHECK(r->split.left->leaf.session == mock_session(1), "left leaf has original session");
    CHECK(r->split.right->leaf.session == mock_session(2), "right leaf has new session");
    CHECK(r->split.left->parent == r, "left child parent is root");
    CHECK(r->split.right->parent == r, "right child parent is root");
    CHECK(pane_count_leaves(r) == 2, "2 leaves after split");
    CHECK(pane_count_splits(r) == 1, "1 split node");

    pane_destroy(r);
}

static void test_split_vsplit(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_VSPLIT, mock_session(2), 0.3f);

    CHECK(r != NULL, "vsplit returns non-NULL");
    CHECK(r->kind == PANE_VSPLIT, "root is VSPLIT");
    CHECK(pane_count_leaves(r) == 2, "2 leaves after vsplit");

    pane_destroy(r);
}

static void test_nested_split(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);

    // Split the left leaf again.
    PaneNode *left = r->split.left;
    PaneNode *r2 = pane_split(r, left, PANE_VSPLIT, mock_session(3), 0.4f);

    CHECK(r2 != NULL, "nested split returns non-NULL");
    CHECK(pane_count_leaves(r2) == 3, "3 leaves after nested split");
    CHECK(pane_count_splits(r2) == 2, "2 split nodes after nested split");

    // Find all three sessions.
    int count[4] = {0};
    PaneNode *l1 = pane_first_leaf(r2);
    if (l1) count[(intptr_t)l1->leaf.session]++;
    PaneNode *l2 = pane_next_leaf(r2, l1);
    if (l2) count[(intptr_t)l2->leaf.session]++;
    PaneNode *l3 = pane_next_leaf(r2, l2);
    if (l3) count[(intptr_t)l3->leaf.session]++;

    CHECK(count[1] == 1, "session 1 present once");
    CHECK(count[2] == 1, "session 2 present once");
    CHECK(count[3] == 1, "session 3 present once");

    pane_destroy(r2);
}

static void test_close_leaf(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);

    PaneNode *new_focus = NULL;
    PaneNode *root = pane_close(r, r->split.left, &new_focus);

    CHECK(root != NULL, "root survives close");
    CHECK(root->kind == PANE_LEAF, "root is now a leaf after close");
    CHECK(root->leaf.session == mock_session(2), "remaining session is session 2");
    CHECK(new_focus == root, "new_focus is the remaining leaf");
    CHECK(pane_count_leaves(root) == 1, "1 leaf after close");

    pane_destroy(root);
}

static void test_close_last_leaf(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *new_focus = NULL;
    PaneNode *root = pane_close(l, l, &new_focus);

    CHECK(root == NULL, "closing last leaf returns NULL");
    CHECK(new_focus == NULL, "new_focus is NULL for last leaf");
    // No pane_destroy needed — pane_close already freed it.
}

static void test_close_right_leaf(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);

    PaneNode *new_focus = NULL;
    PaneNode *root = pane_close(r, r->split.right, &new_focus);

    CHECK(root != NULL, "close right leaf: root exists");
    CHECK(root->kind == PANE_LEAF, "close right leaf: root is leaf");
    CHECK(root->leaf.session == mock_session(1), "close right leaf: session 1 remains");
    CHECK(new_focus == root, "close right leaf: focus is session 1");

    pane_destroy(root);
}

static void test_set_ratio(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);

    CHECK(r->split.ratio >= 0.49f && r->split.ratio <= 0.51f,
           "default ratio is ~0.5");

    pane_set_ratio(r, 0.75f);
    CHECK(r->split.ratio >= 0.74f && r->split.ratio <= 0.76f,
           "ratio set to 0.75");

    // Clamping.
    pane_set_ratio(r, 0.0f);
    CHECK(r->split.ratio >= 0.14f, "ratio clamped to min ~0.15");

    pane_set_ratio(r, 1.0f);
    CHECK(r->split.ratio <= 0.86f, "ratio clamped to max ~0.85");

    pane_destroy(r);
}

static void test_count_functions(void)
{
    CHECK(pane_count_leaves(NULL) == 0, "count_leaves(NULL) == 0");
    CHECK(pane_count_splits(NULL) == 0, "count_splits(NULL) == 0");

    PaneNode *l = pane_leaf(mock_session(1));
    CHECK(pane_count_leaves(l) == 1, "single leaf count");
    CHECK(pane_count_splits(l) == 0, "single split count");
    pane_destroy(l);
}

static void test_first_leaf(void)
{
    CHECK(pane_first_leaf(NULL) == NULL, "first_leaf(NULL) == NULL");

    PaneNode *l = pane_leaf(mock_session(42));
    CHECK(pane_first_leaf(l) == l, "first_leaf of single leaf is itself");
    pane_destroy(l);
}

static void test_split_rejects_invalid_kind(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, (PaneKind)99, mock_session(2), 0.5f);

    CHECK(r == l, "invalid split kind leaves root unchanged");
    CHECK(r->kind == PANE_LEAF, "invalid split kind keeps leaf intact");
    CHECK(r->leaf.session == mock_session(1), "invalid split kind keeps original session");
    CHECK(pane_count_leaves(r) == 1, "invalid split kind does not add a leaf");

    pane_destroy(r);
}

static void test_split_rejects_leaf_outside_root(void)
{
    PaneNode *root = pane_leaf(mock_session(1));
    PaneNode *foreign = pane_leaf(mock_session(2));
    PaneNode *r = pane_split(root, foreign, PANE_HSPLIT, mock_session(3), 0.5f);

    CHECK(r == root, "split with foreign leaf leaves root unchanged");
    CHECK(root->kind == PANE_LEAF, "split with foreign leaf keeps root a leaf");
    CHECK(root->parent == NULL, "split with foreign leaf does not reparent root");
    CHECK(foreign->kind == PANE_LEAF, "split with foreign leaf keeps foreign leaf intact");
    CHECK(foreign->parent == NULL, "split with foreign leaf does not reparent foreign leaf");
    CHECK(pane_count_leaves(root) == 1, "split with foreign leaf does not add root leaves");

    pane_destroy(root);
    pane_destroy(foreign);
}

static void test_close_rejects_leaf_outside_root(void)
{
    PaneNode *root = pane_leaf(mock_session(1));
    PaneNode *foreign = pane_leaf(mock_session(2));
    PaneNode *sentinel = (PaneNode *)(intptr_t)0x1;
    PaneNode *new_focus = sentinel;
    PaneNode *r = pane_close(root, foreign, &new_focus);

    CHECK(r == root, "close with foreign leaf leaves root unchanged");
    CHECK(new_focus == sentinel, "close with foreign leaf leaves focus output unchanged");
    CHECK(root->kind == PANE_LEAF, "close with foreign leaf keeps root a leaf");

    pane_destroy(root);
    if (r == root)
        pane_destroy(foreign);
}

static void test_focus_move_horizontal_split(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);
    PaneNode *left = r->split.left;
    PaneNode *right = r->split.right;

    CHECK(pane_focus_move(r, left, 1, 0) == right, "focus right crosses HSPLIT");
    CHECK(pane_focus_move(r, right, -1, 0) == left, "focus left crosses HSPLIT");
    CHECK(pane_focus_move(r, left, -1, 0) == left, "focus left with no neighbor stays put");

    pane_destroy(r);
}

static void test_focus_move_vertical_split(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_VSPLIT, mock_session(2), 0.5f);
    PaneNode *top = r->split.left;
    PaneNode *bottom = r->split.right;

    CHECK(pane_focus_move(r, top, 0, 1) == bottom, "focus down crosses VSPLIT");
    CHECK(pane_focus_move(r, bottom, 0, -1) == top, "focus up crosses VSPLIT");
    CHECK(pane_focus_move(r, top, 0, -1) == top, "focus up with no neighbor stays put");

    pane_destroy(r);
}

static void test_focus_move_uses_nearest_matching_ancestor(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);
    PaneNode *left = r->split.left;
    PaneNode *r2 = pane_split(r, left, PANE_VSPLIT, mock_session(3), 0.5f);

    PaneNode *top_left = r2->split.left->split.left;
    PaneNode *bottom_left = r2->split.left->split.right;
    PaneNode *right = r2->split.right;

    CHECK(pane_focus_move(r2, top_left, 1, 0) == right,
          "focus right from nested left pane crosses ancestor HSPLIT");
    CHECK(pane_focus_move(r2, bottom_left, 1, 0) == right,
          "focus right from nested lower left pane crosses ancestor HSPLIT");

    pane_destroy(r2);
}

static void test_focus_move_preserves_parallel_position_across_matching_splits(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *root = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);
    root = pane_split(root, root->split.left, PANE_VSPLIT, mock_session(3), 0.5f);
    root = pane_split(root, root->split.right, PANE_VSPLIT, mock_session(4), 0.5f);

    PaneNode *top_left = root->split.left->split.left;
    PaneNode *bottom_left = root->split.left->split.right;
    PaneNode *top_right = root->split.right->split.left;
    PaneNode *bottom_right = root->split.right->split.right;

    CHECK(pane_focus_move(root, top_left, 1, 0) == top_right,
          "focus right from top-left lands on top-right");
    CHECK(pane_focus_move(root, bottom_left, 1, 0) == bottom_right,
          "focus right from bottom-left lands on bottom-right");
    CHECK(pane_focus_move(root, top_right, -1, 0) == top_left,
          "focus left from top-right lands on top-left");
    CHECK(pane_focus_move(root, bottom_right, -1, 0) == bottom_left,
          "focus left from bottom-right lands on bottom-left");

    pane_destroy(root);
}

static void rect_for_session(const PaneNode *n, int *x, int *y, int *w, int *h)
{
    if (!n || n->kind != PANE_LEAF)
        return;

    switch ((intptr_t)n->leaf.session) {
    case 1: *x = 0;  *y = 0;  *w = 50; *h = 50; break;
    case 2: *x = 50; *y = 0;  *w = 50; *h = 100; break;
    case 3: *x = 0;  *y = 50; *w = 50; *h = 50; break;
    default: *x = 0; *y = 0; *w = 0; *h = 0; break;
    }
}

static void test_pane_at_pos_checks_single_leaf_bounds(void)
{
    PaneNode *l = pane_leaf(mock_session(1));

    CHECK(pane_at_pos(l, 10, 10, rect_for_session) == l,
          "pane_at_pos finds single leaf when point is inside");
    CHECK(pane_at_pos(l, 80, 80, rect_for_session) == NULL,
          "pane_at_pos ignores single leaf when point is outside");

    pane_destroy(l);
}

static void test_pane_at_pos_searches_nested_leaf_rects(void)
{
    PaneNode *l = pane_leaf(mock_session(1));
    PaneNode *r = pane_split(l, l, PANE_HSPLIT, mock_session(2), 0.5f);
    PaneNode *r2 = pane_split(r, r->split.left, PANE_VSPLIT, mock_session(3), 0.5f);
    PaneNode *hit_top_left = pane_at_pos(r2, 10, 10, rect_for_session);
    PaneNode *hit_bottom_left = pane_at_pos(r2, 10, 70, rect_for_session);
    PaneNode *hit_right = pane_at_pos(r2, 70, 10, rect_for_session);

    CHECK(hit_top_left && hit_top_left->leaf.session == mock_session(1),
          "pane_at_pos finds nested top-left leaf");
    CHECK(hit_bottom_left && hit_bottom_left->leaf.session == mock_session(3),
          "pane_at_pos finds nested bottom-left leaf");
    CHECK(hit_right && hit_right->leaf.session == mock_session(2),
          "pane_at_pos finds right leaf");
    CHECK(pane_at_pos(r2, 150, 150, rect_for_session) == NULL,
          "pane_at_pos returns NULL outside all leaves");

    pane_destroy(r2);
}

int main(void)
{
    fprintf(stderr, "pane_tests:\n");
    test_create_leaf();
    test_split_leaf();
    test_split_vsplit();
    test_nested_split();
    test_close_leaf();
    test_close_last_leaf();
    test_close_right_leaf();
    test_set_ratio();
    test_count_functions();
    test_first_leaf();
    test_split_rejects_invalid_kind();
    test_split_rejects_leaf_outside_root();
    test_close_rejects_leaf_outside_root();
    test_focus_move_horizontal_split();
    test_focus_move_vertical_split();
    test_focus_move_uses_nearest_matching_ancestor();
    test_focus_move_preserves_parallel_position_across_matching_splits();
    test_pane_at_pos_checks_single_leaf_bounds();
    test_pane_at_pos_searches_nested_leaf_rects();

    fprintf(stderr, "\n%d / %d passed, %d failed\n",
            total - failures, total, failures);
    return failures > 0 ? 1 : 0;
}
