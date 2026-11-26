/**
 * @file quadtree_private.h
 * @brief QuadTree internals: concrete struct and private helpers
 *
 * Includes the public API, then defines the node layout and declares the
 * index helpers. Only translation units of the library and its tests
 * include this header.
 */
#ifndef __QUADTREE_PRIVATE_H__
#define __QUADTREE_PRIVATE_H__

#include "quadtree.h"

/* ====================# DATA LAYOUT #==================== */

/*
  Flat complete quad tree, root first, level by level: level l holds 4^l
  nodes starting at index (4^l - 1) / 3. The children of node i are
  4i + 1 .. 4i + 4, its parent (i - 1) / 4. The leaves are level `depth`,
  2^depth x 2^depth of them, in Z (Morton) order: the four children of a
  node are four consecutive leaves, and leaf (br, bc) is at
    leaf_base + morton(br, bc),  leaf_base = (4^depth - 1) / 3
  s_nodes = (4^(depth + 1) - 1) / 3 words in all.
*/
struct quadtree {
  u32
    s_block_rows,  // leaves in use: block rows of the grid
    s_block_cols,  // leaves in use: block columns of the grid
    depth,         // leaf level; 2^depth >= max(s_block_rows, s_block_cols)
    s_nodes;       // node count, root included

  u32* nodes;      // leaf: caller word; internal: count of its subtree
};

/* ====================# INDEX #==================== */

// Index of the first leaf: (4^depth - 1) / 3.
static inline u32 __quadtree_leaf_base(const QuadTree* tree) {
  return (u32)(((1ull << (2 * tree->depth)) - 1) / 3);
}

// Interleaves the bits of br (odd positions) and bc (even positions).
static inline u32 __quadtree_morton(const u32 br, const u32 bc) {
  u32 code = 0;
  for (u32 bit = 0; bit < QUADTREE_MAX_DEPTH; bit++)
    code |= ((br >> bit) & 1u) << (2 * bit + 1)
         |  ((bc >> bit) & 1u) << (2 * bit);
  return code;
}

// Node index of leaf (br, bc).
static inline u32 __quadtree_leaf_index(const QuadTree* tree, const u32 br, const u32 bc) {
  return __quadtree_leaf_base(tree) + __quadtree_morton(br, bc);
}

#endif // !__QUADTREE_PRIVATE_H__
