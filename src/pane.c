#include "pane.h"
#include "session.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static PaneNode *pane_next_leaf_rec(PaneNode *n, PaneNode *cur, bool *found);
static bool pane_contains_node(const PaneNode *root, const PaneNode *target);

static PaneNode *node_alloc(PaneKind kind)
{
    PaneNode *n = (PaneNode *)calloc(1, sizeof(PaneNode));
    if (n)
        n->kind = kind;
    return n;
}

static void replace_child(PaneNode *parent, PaneNode *old, PaneNode *new)
{
    if (!parent)
        return;
    if (parent->split.left == old)
        parent->split.left = new;
    else if (parent->split.right == old)
        parent->split.right = new;
    if (new)
        new->parent = parent;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

PaneNode *pane_leaf(Session *s)
{
    PaneNode *n = node_alloc(PANE_LEAF);
    if (n)
        n->leaf.session = s;
    return n;
}

PaneNode *pane_split(PaneNode *root, PaneNode *focused, PaneKind dir,
                     Session *new_leaf, float ratio)
{
    if (!root || !focused || focused->kind != PANE_LEAF || !new_leaf)
        return root;
    if (dir != PANE_HSPLIT && dir != PANE_VSPLIT)
        return root;
    if (!pane_contains_node(root, focused))
        return root;

    if (ratio < 0.15f) ratio = 0.15f;
    if (ratio > 0.85f) ratio = 0.85f;

    // Create the new split node.
    PaneNode *split = node_alloc(dir);
    if (!split)
        return root;
    split->split.ratio = ratio;

    // The focused leaf becomes split->left; the new session is split->right.
    split->split.left  = focused;
    split->split.right = pane_leaf(new_leaf);
    if (!split->split.right) {
        free(split);
        return root;
    }
    split->split.right->parent = split;

    // Save focused's parent BEFORE we reparent it (otherwise the check below
    // would always see a parent and self-replace in replace_child).
    PaneNode *focused_parent = focused->parent;
    focused->parent = split;

    // Replace focused with split in the parent (or root).
    if (focused_parent) {
        replace_child(focused_parent, focused, split);
    } else {
        // Splitting the root.
        root = split;
    }

    return root;
}

PaneNode *pane_close(PaneNode *root, PaneNode *leaf, PaneNode **new_focus)
{
    if (!root || !leaf || leaf->kind != PANE_LEAF)
        return root;
    if (!pane_contains_node(root, leaf))
        return root;

    PaneNode *parent = leaf->parent;

    // No parent → root is the only leaf. Destroy and return NULL.
    if (!parent) {
        session_destroy(leaf->leaf.session);
        free(leaf);
        if (new_focus)
            *new_focus = NULL;
        return NULL;
    }

    // Determine the sibling (the child that is *not* leaf).
    PaneNode *sibling = parent->split.left == leaf
                            ? parent->split.right
                            : parent->split.left;

    // Grandparent.
    PaneNode *gp = parent->parent;

    // Detach sibling and re-parent it to gp.
    if (sibling)
        sibling->parent = gp;

    if (gp) {
        replace_child(gp, parent, sibling);
    } else {
        // Parent was root; sibling becomes the new root.
        root = sibling;
    }

    // Destroy the leaf and the split node.
    session_destroy(leaf->leaf.session);
    free(leaf);
    free(parent);

    // Set new_focus to the sibling (or its first leaf if it's a split).
    if (new_focus) {
        if (sibling && sibling->kind == PANE_LEAF)
            *new_focus = sibling;
        else if (sibling)
            *new_focus = pane_first_leaf(sibling);
        else
            *new_focus = NULL;
    }

    return root;
}

// ---------------------------------------------------------------------------
// Focus movement
// ---------------------------------------------------------------------------

static bool pane_contains_node(const PaneNode *root, const PaneNode *target)
{
    if (!root || !target)
        return false;
    if (root == target)
        return true;
    if (root->kind == PANE_LEAF)
        return false;
    return pane_contains_node(root->split.left, target)
        || pane_contains_node(root->split.right, target);
}

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
} PaneFocusRect;

// Directional focus is based on the split tree's normalized geometry. This
// keeps movement aligned across matching nested splits instead of blindly
// choosing the first/last leaf in the neighboring subtree.
typedef struct {
    const PaneNode *cur;
    PaneFocusRect cur_rect;
    int dx;
    int dy;
    PaneNode *best;
    bool best_overlaps;
    float best_primary;
    float best_overlap;
    float best_center_distance;
} PaneFocusSearch;

static float pane_minf(float a, float b) { return a < b ? a : b; }
static float pane_maxf(float a, float b) { return a > b ? a : b; }
static float pane_absf(float v) { return v < 0.0f ? -v : v; }

static float pane_overlap(float a0, float a1, float b0, float b1)
{
    float overlap = pane_minf(a1, b1) - pane_maxf(a0, b0);
    return overlap > 0.0f ? overlap : 0.0f;
}

static float pane_center(float a, float b)
{
    return (a + b) * 0.5f;
}

static bool pane_find_leaf_rect(const PaneNode *root, const PaneNode *target,
                                PaneFocusRect rect, PaneFocusRect *out)
{
    if (!root || !target || !out)
        return false;
    if (root == target && root->kind == PANE_LEAF) {
        *out = rect;
        return true;
    }
    if (root->kind == PANE_LEAF)
        return false;

    if (root->kind == PANE_HSPLIT) {
        float mid = rect.x0 + (rect.x1 - rect.x0) * root->split.ratio;
        PaneFocusRect left = { rect.x0, rect.y0, mid, rect.y1 };
        PaneFocusRect right = { mid, rect.y0, rect.x1, rect.y1 };
        return pane_find_leaf_rect(root->split.left, target, left, out)
            || pane_find_leaf_rect(root->split.right, target, right, out);
    }

    if (root->kind == PANE_VSPLIT) {
        float mid = rect.y0 + (rect.y1 - rect.y0) * root->split.ratio;
        PaneFocusRect top = { rect.x0, rect.y0, rect.x1, mid };
        PaneFocusRect bottom = { rect.x0, mid, rect.x1, rect.y1 };
        return pane_find_leaf_rect(root->split.left, target, top, out)
            || pane_find_leaf_rect(root->split.right, target, bottom, out);
    }

    return false;
}

static bool pane_focus_candidate_better(const PaneFocusSearch *s,
                                        bool overlaps, float primary,
                                        float overlap, float center_distance)
{
    const float eps = 0.000001f;
    if (!s->best)
        return true;
    if (overlaps != s->best_overlaps)
        return overlaps;
    if (pane_absf(primary - s->best_primary) > eps)
        return primary < s->best_primary;
    if (overlaps && pane_absf(overlap - s->best_overlap) > eps)
        return overlap > s->best_overlap;
    return center_distance < s->best_center_distance;
}

static void pane_focus_visit(PaneNode *root, PaneFocusRect rect,
                             PaneFocusSearch *search)
{
    if (!root || !search)
        return;

    if (root->kind == PANE_LEAF) {
        if (root == search->cur)
            return;

        const float eps = 0.000001f;
        float primary = 0.0f;
        float overlap = 0.0f;
        float center_distance = 0.0f;

        if (search->dx > 0) {
            if (rect.x0 < search->cur_rect.x1 - eps)
                return;
            primary = rect.x0 - search->cur_rect.x1;
            overlap = pane_overlap(rect.y0, rect.y1,
                                   search->cur_rect.y0, search->cur_rect.y1);
            center_distance = pane_absf(pane_center(rect.y0, rect.y1)
                                      - pane_center(search->cur_rect.y0,
                                                    search->cur_rect.y1));
        } else if (search->dx < 0) {
            if (rect.x1 > search->cur_rect.x0 + eps)
                return;
            primary = search->cur_rect.x0 - rect.x1;
            overlap = pane_overlap(rect.y0, rect.y1,
                                   search->cur_rect.y0, search->cur_rect.y1);
            center_distance = pane_absf(pane_center(rect.y0, rect.y1)
                                      - pane_center(search->cur_rect.y0,
                                                    search->cur_rect.y1));
        } else if (search->dy > 0) {
            if (rect.y0 < search->cur_rect.y1 - eps)
                return;
            primary = rect.y0 - search->cur_rect.y1;
            overlap = pane_overlap(rect.x0, rect.x1,
                                   search->cur_rect.x0, search->cur_rect.x1);
            center_distance = pane_absf(pane_center(rect.x0, rect.x1)
                                      - pane_center(search->cur_rect.x0,
                                                    search->cur_rect.x1));
        } else if (search->dy < 0) {
            if (rect.y1 > search->cur_rect.y0 + eps)
                return;
            primary = search->cur_rect.y0 - rect.y1;
            overlap = pane_overlap(rect.x0, rect.x1,
                                   search->cur_rect.x0, search->cur_rect.x1);
            center_distance = pane_absf(pane_center(rect.x0, rect.x1)
                                      - pane_center(search->cur_rect.x0,
                                                    search->cur_rect.x1));
        } else {
            return;
        }

        bool overlaps = overlap > eps;
        if (pane_focus_candidate_better(search, overlaps, primary, overlap,
                                        center_distance)) {
            search->best = root;
            search->best_overlaps = overlaps;
            search->best_primary = primary;
            search->best_overlap = overlap;
            search->best_center_distance = center_distance;
        }
        return;
    }

    if (root->kind == PANE_HSPLIT) {
        float mid = rect.x0 + (rect.x1 - rect.x0) * root->split.ratio;
        pane_focus_visit(root->split.left,
                         (PaneFocusRect){ rect.x0, rect.y0, mid, rect.y1 },
                         search);
        pane_focus_visit(root->split.right,
                         (PaneFocusRect){ mid, rect.y0, rect.x1, rect.y1 },
                         search);
    } else if (root->kind == PANE_VSPLIT) {
        float mid = rect.y0 + (rect.y1 - rect.y0) * root->split.ratio;
        pane_focus_visit(root->split.left,
                         (PaneFocusRect){ rect.x0, rect.y0, rect.x1, mid },
                         search);
        pane_focus_visit(root->split.right,
                         (PaneFocusRect){ rect.x0, mid, rect.x1, rect.y1 },
                         search);
    }
}

PaneNode *pane_focus_move(const PaneNode *root, const PaneNode *cur,
                          int dx, int dy)
{
    if (!root || !cur || cur->kind != PANE_LEAF || !pane_contains_node(root, cur))
        return (PaneNode *)cur;

    bool horizontal = dx != 0;
    bool vertical = !horizontal && dy != 0;
    if (!horizontal && !vertical)
        return (PaneNode *)cur;

    PaneFocusRect root_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
    PaneFocusRect cur_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!pane_find_leaf_rect(root, cur, root_rect, &cur_rect))
        return (PaneNode *)cur;

    PaneFocusSearch search = {
        .cur = cur,
        .cur_rect = cur_rect,
        .dx = horizontal ? dx : 0,
        .dy = vertical ? dy : 0,
        .best = NULL,
        .best_overlaps = false,
        .best_primary = 0.0f,
        .best_overlap = 0.0f,
        .best_center_distance = 0.0f,
    };
    pane_focus_visit((PaneNode *)root, root_rect, &search);
    return search.best ? search.best : (PaneNode *)cur;
}

PaneNode *pane_at_pos(PaneNode *root, int x, int y,
                      void (*leaf_rect)(const PaneNode *, int *x, int *y,
                                        int *w, int *h))
{
    if (!root || !leaf_rect)
        return NULL;

    if (root->kind == PANE_LEAF) {
        int lx = 0, ly = 0, lw = 0, lh = 0;
        leaf_rect(root, &lx, &ly, &lw, &lh);
        if (x >= lx && x < lx + lw && y >= ly && y < ly + lh)
            return root;
        return NULL;
    }

    PaneNode *left = pane_at_pos(root->split.left, x, y, leaf_rect);
    if (left)
        return left;
    PaneNode *right = pane_at_pos(root->split.right, x, y, leaf_rect);
    if (right)
        return right;
    return NULL;
}

// ---------------------------------------------------------------------------
// Ratio
// ---------------------------------------------------------------------------

void pane_set_ratio(PaneNode *split, float ratio)
{
    if (!split || (split->kind != PANE_HSPLIT && split->kind != PANE_VSPLIT))
        return;
    if (ratio < 0.15f) ratio = 0.15f;
    if (ratio > 0.85f) ratio = 0.85f;
    split->split.ratio = ratio;
}

// ---------------------------------------------------------------------------
// Tree walkers
// ---------------------------------------------------------------------------

void pane_destroy(PaneNode *root)
{
    if (!root)
        return;

    if (root->kind == PANE_LEAF) {
        session_destroy(root->leaf.session);
    } else {
        pane_destroy(root->split.left);
        pane_destroy(root->split.right);
    }
    free(root);
}

int pane_count_leaves(const PaneNode *root)
{
    if (!root)
        return 0;
    if (root->kind == PANE_LEAF)
        return 1;
    return pane_count_leaves(root->split.left)
         + pane_count_leaves(root->split.right);
}

int pane_count_splits(const PaneNode *root)
{
    if (!root || root->kind == PANE_LEAF)
        return 0;
    return 1
         + pane_count_splits(root->split.left)
         + pane_count_splits(root->split.right);
}

PaneNode *pane_first_leaf(PaneNode *root)
{
    if (!root)
        return NULL;
    if (root->kind == PANE_LEAF)
        return root;
    PaneNode *l = pane_first_leaf(root->split.left);
    if (l)
        return l;
    return pane_first_leaf(root->split.right);
}

// Internal recursive helper.
static PaneNode *pane_next_leaf_rec(PaneNode *n, PaneNode *cur, bool *found)
{
    if (!n)
        return NULL;
    if (n->kind == PANE_LEAF) {
        if (*found)
            return n;
        if (n == cur)
            *found = true;
        return NULL;
    }
    PaneNode *l = pane_next_leaf_rec(n->split.left, cur, found);
    if (l)
        return l;
    return pane_next_leaf_rec(n->split.right, cur, found);
}

PaneNode *pane_next_leaf(PaneNode *root, PaneNode *cur)
{
    if (!root || !cur)
        return NULL;
    // Simple linear scan (tree is small; acceptable).
    // Returns the leaf after cur in a depth-first left-to-right traversal.
    bool found = false;
    return pane_next_leaf_rec(root, cur, &found);
}

void pane_collect_leaves(PaneNode *root, PaneNode **out, int max, int *n)
{
    if (!root || !out || !n)
        return;
    if (*n >= max)
        return;
    if (root->kind == PANE_LEAF) {
        out[(*n)++] = root;
    } else {
        pane_collect_leaves(root->split.left,  out, max, n);
        pane_collect_leaves(root->split.right, out, max, n);
    }
}
