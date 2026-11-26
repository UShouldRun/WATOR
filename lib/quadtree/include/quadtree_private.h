/**
 * @file quadtree_private.h
 * @brief QuadTree internals: private index helpers
 *
 * Includes the public API, then defines the index helpers. Only
 * translation units of the library and its tests include this header.
 */
#ifndef __QUADTREE_PRIVATE_H__
#define __QUADTREE_PRIVATE_H__

#include "quadtree.h"

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
