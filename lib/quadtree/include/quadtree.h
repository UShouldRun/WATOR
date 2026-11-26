/**
 * @file quadtree.h
 * @brief Quad tree over a grid of 4x4 blocks (Public API)
 *
 * A complete quad tree stored as a flat array of u32, one word per node.
 * Its leaves are the blocks of a block-major grid, addressed by
 * (block_row, block_col); the tree is padded to a power-of-two square of
 * leaves (2^depth x 2^depth), padded leaves stay 0.
 *
 * * QuadTree:
 * - What: an index of the non-empty blocks of a grid: per-block leaf words
 *   with the counts of every subtree above them.
 * - Words: an internal node holds a count, the sum of the counts below it;
 *   a node with 0 has nothing below it. A leaf
 *   holds a caller-defined word, used in one of two ways:
 *     count tree: the leaf is a count too (Wa-Tor's claims tree: claimed
 *                 cells per block). Update with quadtree_add.
 *     mask tree:  the leaf is any word (Wa-Tor's occupancy tree: the 4x4
 *                 type mask) and the counts above it are kept by the
 *                 caller (animals per subtree). Update the leaf with
 *                 quadtree_leaf_set and the counts with quadtree_path_add.
 * - When: created once per grid, cleared (count trees) or kept up to date
 *   (mask trees) between sub-steps, destroyed with the grid.
 * - Time Complexity: O(1) per leaf read or store, O(depth) per add;
 *   clear is O(s_nodes).
 * - Concurrency: quadtree_add and quadtree_path_add are atomic (relaxed
 *   fetch_add) and may run from several workers at once. quadtree_leaf_set
 *   is a plain store: only the worker owning the block may call it.
 *
 * The struct is public so a caller can hold a QuadTree in place (by value,
 * e.g. inside Wator) instead of behind a pointer; its fields are read-only
 * to the caller, and all updates go through this API.
 */
#ifndef __QUADTREE_H__
#define __QUADTREE_H__

#include "utils.h"

typedef struct quadtree QuadTree;

// Deepest tree supported: 2^16 x 2^16 leaves (a 262144 x 262144 grid).
#define QUADTREE_MAX_DEPTH 16

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

/* ====================# QUADTREE #==================== */

/*
  Creates a zeroed tree whose leaves cover s_block_rows x s_block_cols
  blocks. NULL on allocation failure or when the grid needs more than
  QUADTREE_MAX_DEPTH levels.
*/
QuadTree* quadtree_create      (const u32 s_block_rows, const u32 s_block_cols);
bool      quadtree_destroy     (QuadTree* tree);

// Zeroes every node (one memset): the reset of a count tree per sub-step.
void      quadtree_clear       (QuadTree* tree);

// Root word: the total count of the tree.
u32       quadtree_total       (const QuadTree* tree);

/* ====================# LEAF #==================== */

// Leaf word of block (br, bc).
u32       quadtree_leaf_get    (const QuadTree* tree, const u32 br, const u32 bc);
// Stores the leaf word of block (br, bc); counts above it are untouched.
void      quadtree_leaf_set    (QuadTree* tree, const u32 br, const u32 bc, const u32 word);

/*
  Count tree: adds delta to the leaf of (br, bc) and to every ancestor,
  atomically. Returns the leaf's previous value, so a caller can tell a
  0 -> 1 transition.
*/
u32       quadtree_add         (QuadTree* tree, const u32 br, const u32 bc, const i32 delta);
// Mask tree: adds delta to every ancestor of (br, bc), not the leaf; atomic.
void      quadtree_path_add    (QuadTree* tree, const u32 br, const u32 bc, const i32 delta);

#endif // !__QUADTREE_H__
