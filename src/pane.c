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

static PaneNode *pane_edge_leaf(PaneNode *root, bool leading_edge)
{
    PaneNode *n = root;
    while (n && n->kind != PANE_LEAF)
        n = leading_edge ? n->split.left : n->split.right;
    return n;
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

    PaneNode *node = (PaneNode *)cur;
    while (node && node->parent) {
        PaneNode *parent = node->parent;
        if (horizontal && parent->kind == PANE_HSPLIT) {
            if (dx > 0 && parent->split.left == node)
                return pane_edge_leaf(parent->split.right, true);
            if (dx < 0 && parent->split.right == node)
                return pane_edge_leaf(parent->split.left, false);
        } else if (vertical && parent->kind == PANE_VSPLIT) {
            if (dy > 0 && parent->split.left == node)
                return pane_edge_leaf(parent->split.right, true);
            if (dy < 0 && parent->split.right == node)
                return pane_edge_leaf(parent->split.left, false);
        }
        node = parent;
    }

    return (PaneNode *)cur;
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
