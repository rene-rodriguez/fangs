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

    fprintf(stderr, "\n%d / %d passed, %d failed\n",
            total - failures, total, failures);
    return failures > 0 ? 1 : 0;
}
