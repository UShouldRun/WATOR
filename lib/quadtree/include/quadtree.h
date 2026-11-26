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
 * - What: an index of the non-empty blocks of a grid, so a walk visits
 *   occupied blocks and skips empty subtrees without touching them.
 * - Words: an internal node holds a count, the sum of the counts below it;
 *   a node with 0 has nothing below it and the descent skips it. A leaf
 *   holds a caller-defined word, used in one of two ways:
 *     count tree: the leaf is a count too (Wa-Tor's claims tree: claimed
 *                 cells per block). Update with quadtree_add.
 *     mask tree:  the leaf is any word (Wa-Tor's occupancy tree: the 4x4
 *                 type mask) and the counts above it are kept by the
 *                 caller (animals per subtree). Update the leaf with
 *                 quadtree_leaf_set and the counts with quadtree_path_add.
 * - When: created once per grid, cleared (count trees) or kept up to date
 *   (mask trees) between sub-steps, destroyed with the grid.
 * - Time Complexity: O(depth) per update; a walk is O(A * depth) for A
 *   visited leaves; clear is O(s_nodes).
 * - Concurrency: quadtree_add and quadtree_path_add are atomic (relaxed
 *   fetch_add) and may run from several workers at once. quadtree_leaf_set
 *   is a plain store: only the worker owning the block may call it. Walks
 *   may run concurrently with each other, not with writers.
 *
 * QuadTree is opaque to the caller; all access goes through this API.
 */
#ifndef __QUADTREE_H__
#define __QUADTREE_H__

#include "utils.h"

typedef struct quadtree      QuadTree;
typedef struct quadtree_iter QuadTreeIter;

// Deepest tree supported: 2^16 x 2^16 leaves (a 262144 x 262144 grid).
#define QUADTREE_MAX_DEPTH 16

/*
  Walk state: an explicit stack of the descent, so a walk needs no
  allocation and no callback. Fields are private; set up with
  quadtree_iter_init, advance with quadtree_iter_next.
*/
struct quadtree_iter {
  const QuadTree* tree;

  u32
    br_begin,  // block-row range of the walk: [br_begin, br_end)
    br_end,
    depth;     // current stack depth

  u32
    node [QUADTREE_MAX_DEPTH + 1],  // node index at each level
    child[QUADTREE_MAX_DEPTH + 1];  // next child (0..4) to visit at each level
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

/* ====================# ITER #==================== */

/*
  Starts a walk over the leaves in block-rows [br_begin, br_end): a worker
  passes its zone. Subtrees with count 0, and subtrees outside the range,
  are skipped without being visited.
*/
void      quadtree_iter_init   (QuadTreeIter* it, const QuadTree* tree, const u32 br_begin, const u32 br_end);

/*
  Next non-zero leaf of the walk, in Z order: writes its block (br, bc) and
  its word, returns false when the walk is over. A mask tree may yield a
  leaf whose word is non-zero but holds nothing the caller wants (rocks
  only, under an ancestor counting animals); the caller filters.
*/
bool      quadtree_iter_next   (QuadTreeIter* it, u32* br, u32* bc, u32* word);

#endif // !__QUADTREE_H__
