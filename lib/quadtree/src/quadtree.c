// TODO(quadtree): not implemented yet; every function below is a stub with
// the behaviour to implement in its TODO. The API is quadtree.h.

#include "quadtree_private.h"

#include <stdlib.h>

/* ====================# QUADTREE #==================== */

QuadTree* quadtree_create(const u32 s_block_rows, const u32 s_block_cols) {
  /*
    TODO(quadtree): depth = smallest d with 2^d >= max(rows, cols), fail
    above QUADTREE_MAX_DEPTH; s_nodes = (4^(depth + 1) - 1) / 3; calloc the
    struct and the nodes (one block is fine), fill the fields.
  */
  (void) s_block_rows;
  (void) s_block_cols;
  return NULL;
}

bool quadtree_destroy(QuadTree* tree) {
  // TODO(quadtree): false on NULL; free nodes and struct.
  (void) tree;
  return false;
}

void quadtree_clear(QuadTree* tree) {
  // TODO(quadtree): memset(nodes, 0, s_nodes * sizeof(u32)).
  (void) tree;
}

u32 quadtree_total(const QuadTree* tree) {
  // TODO(quadtree): nodes[0].
  (void) tree;
  return 0;
}

/* ====================# LEAF #==================== */

u32 quadtree_leaf_get(const QuadTree* tree, const u32 br, const u32 bc) {
  // TODO(quadtree): nodes[__quadtree_leaf_index(tree, br, bc)].
  (void) tree;
  (void) br;
  (void) bc;
  return 0;
}

void quadtree_leaf_set(QuadTree* tree, const u32 br, const u32 bc, const u32 word) {
  // TODO(quadtree): plain store into the leaf; ancestors untouched.
  (void) tree;
  (void) br;
  (void) bc;
  (void) word;
}

u32 quadtree_add(QuadTree* tree, const u32 br, const u32 bc, const i32 delta) {
  /*
    TODO(quadtree): relaxed __atomic_fetch_add on the leaf (keep its old
    value to return), then on each ancestor: i = (i - 1) / 4 up to 0.
  */
  (void) tree;
  (void) br;
  (void) bc;
  (void) delta;
  return 0;
}

void quadtree_path_add(QuadTree* tree, const u32 br, const u32 bc, const i32 delta) {
  // TODO(quadtree): as quadtree_add, starting at the leaf's parent.
  (void) tree;
  (void) br;
  (void) bc;
  (void) delta;
}

/* ====================# ITER #==================== */

void quadtree_iter_init(QuadTreeIter* it, const QuadTree* tree, const u32 br_begin, const u32 br_end) {
  // TODO(quadtree): store tree and range; push the root (node 0, child 0).
  (void) it;
  (void) tree;
  (void) br_begin;
  (void) br_end;
}

bool quadtree_iter_next(QuadTreeIter* it, u32* br, u32* bc, u32* word) {
  /*
    TODO(quadtree): iterative DFS on it->node / it->child. Skip a child
    whose word is 0 or whose block-row span [row0, row0 + 2^(depth - l))
    misses [br_begin, br_end); at the leaf level skip padded leaves
    (br >= s_block_rows or bc >= s_block_cols) and yield (br, bc, word)
    from the Morton code. Return false when the stack empties.
  */
  (void) it;
  (void) br;
  (void) bc;
  (void) word;
  return false;
}
