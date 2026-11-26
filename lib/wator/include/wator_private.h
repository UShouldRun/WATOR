/**
 * @file wator_private.h
 * @brief Wa-Tor internals: concrete structs and private prototypes
 *
 * Includes the public API, then defines the data layouts (params, world,
 * wator, process) and declares every internal function. Not part of the
 * public interface: only translation units of the library and its tests
 * include this header.
 */
#ifndef __WATOR_PRIVATE_H__
#define __WATOR_PRIVATE_H__

#include "wator.h"

#include <pthread.h>
#include <stdatomic.h>

/* ====================# DATA LAYOUT #==================== */

// One grid cell type: 0..3, 2 bits. The grid packs 4 cells per byte, so a
// 4x4 block is 4 bytes (one byte per block row).
enum wator_type {
  WATOR_EMPTY  = 0,
  WATOR_ROCK   = 1,
  WATOR_RABBIT = 2,
  WATOR_FOX    = 3,
};

typedef enum   wator_type    WatorType;

typedef struct wator_params  WatorParams;
typedef struct wator_world   WatorWorld;
typedef struct wator_process WatorProcess;

struct wator_params {
  i32
    s_gen_proc_rabbits,  // generations until a rabbit can procreate
    s_gen_proc_foxes,    // generations until a fox can procreate
    s_gen_food_foxes,    // generations before a fox starves
    s_gen,               // generations to simulate
    s_threads,           // requested worker threads
    s_rebalance_every;   // generations between load-balancing barriers
};

// One side of the double buffer: everything a sub-step reads (src) or
// writes (dst). Sub-step s reads world[s & 1] and writes world[(s + 1) & 1].
struct wator_world {
  /*
    4x4-block grid, stored block-major, s_blocks * 16 cells (edge blocks
    padded), 2 bits per cell: s_blocks * 4 bytes. Cell (r, c) has the
    cell index (see __wator_world_cell_index)
      i = (r / 4 * s_blocks_row + c / 4) * 16 + (r % 4) * 4 + c % 4
    and lives in grid byte i / 4 (= block * 4 + r % 4, one byte per block
    row) at bit shift 2 * (i % 4) (= 2 * (c % 4)). Access through
    __wator_world_grid_get / __wator_world_grid_set.
  */
  u8
    * grid;      // WATOR_* per cell, 4 cells per byte

  // Ages, block-major, one u16 per cell: indexed by the cell index i.
  u16
    * proc_age,  // breeding age of the animal in the cell
    * food_age;  // starvation age of the animal in the cell
};

// struct wator is opaque in wator.h; this is its concrete layout.
struct wator {
  i32
    s_rows,  // grid rows    (stored; <= s_cols)
    s_cols;  // grid columns (stored)

  /*
    The input had rows > cols, so the grid is stored transposed: input
    cell (x, y) lives at stored (y, x), and the input's dimensions are
    (s_cols, s_rows). Load and print undo it; nothing else sees input
    coordinates.
    Why: in the block-major layout a block-row (4 rows x s_cols) is one
    contiguous run. Keeping rows <= cols makes those runs the long side:
    the inner loops over blocks are long sequential streams for the
    prefetcher, and the outer loop over block-rows is the short one, so
    there is less loop overhead and fewer jumps between block-rows.
    The rules must still number neighbours clockwise from
    north *in input coordinates*: stored W, S, E, N.
  */
  bool transposed;

  WatorParams params;
  WatorWorld  world[2];  // double buffer, see struct wator_world

  /*
    Claim bits, block-major like grid: two cells per byte, one nibble
    each, s_blocks * 8 bytes. The nibble holds one bit per neighbour
    direction that chose to move into the cell (and the eat bit for the
    fox sub-step). Written in pass 1 and read in pass 2 of the same
    sub-step, then cleared: not double-buffered. Cell (r, c) is in byte
      (r / 4 * s_blocks_row + c / 4) * 8 + (r % 4) * 2 + (c % 4) / 2
    at nibble shift 4 * (c % 2).
  */
  u8* claims;

  void* mem;  // single allocation backing every array above; free this

  /*
    TODO(refactor): add the two trees (quadtree.h), one leaf per 4x4 block:
      QuadTree* occupancy;    // mask tree: leaf = type mask, internal = animals
      QuadTree* claims_tree;  // count tree: claimed cells per block
    QuadTree (lib/quadtree) is not implemented yet.
  */
};

// A worker thread: owns rows [row_begin, row_end) of the grid and is the
// only thread that writes there. 1-D strips; add col_* fields for 2-D.
struct wator_process {
  i32
    id,
    row_begin,
    row_end;

  pthread_t thread;

  Wator* wator;  // shared by every worker

  // Sub-steps completed: 2 * gen + (0 = start, 1 = rabbits done).
  // See __wator_process_sync_wait for the window rule neighbours must respect.
  _Atomic i32 progress;

  struct wator_process
    * up,
    * down;  // neighbouring workers, NULL when none
};

/* ====================# GRID (BLOCK-MAJOR) #==================== */

// Blocks per block-row (s_cols rounded up to 4).
static inline i32 __wator_s_blocks_row(const Wator* wator) {
  return (wator->s_cols + 3) / 4;
}

// Block-major cell index of (row, col): indexes the ages directly; the
// grid (2 bits/cell) and the claims (4 bits/cell) derive their byte from it.
static inline i64 __wator_world_cell_index(const i32 s_blocks_row, const i32 row, const i32 col) {
  const i64
    block_index = (i64)(row >> 2) * s_blocks_row + (col >> 2),
    block_coord = (row & 3) * 4 + (col & 3);
  return block_index * 16 + block_coord;
}

// WATOR_* type of the cell with cell index i.
static inline u8 __wator_world_grid_get(const u8* grid, const i64 i) {
  return (grid[i >> 2] >> ((i & 3) * 2)) & 3;
}

/*
  Sets the type of the cell with cell index i. A plain read-modify-write of
  the byte: the byte's 4 cells are one block row, so with block-row-aligned
  zones only the owning worker ever writes it.
*/
static inline void __wator_world_grid_set(u8* grid, const i64 i, const u8 type) {
  const u8 shift = (u8)((i & 3) * 2);
  grid[i >> 2] = (u8)((grid[i >> 2] & ~(3u << shift)) | ((u32)type << shift));
}

/* ====================# WORLD #==================== */

// Allocates, in one block, both worlds' grids and ages and the claims for
// an s_rows x s_cols world, all zeroed (block-major, edge blocks padded).
bool __wator_alloc       (Wator* wator, const i32 s_rows, const i32 s_cols);
void __wator_free        (Wator* wator);

/* ====================# PROCESS #==================== */

// Runs the whole simulation: sequentially on the calling thread when
// s_threads <= 1, otherwise on s_threads pthreads. False on failure.
bool __wator_process_run             (Wator* wator, const i32 s_threads);
// Sets up s_threads workers over wator: links neighbours, assigns zones.
bool __wator_process_init_all        (WatorProcess* procs, Wator* wator, const i32 s_threads);
void __wator_process_destroy_all     (WatorProcess* procs, const i32 s_threads);
// (Re)assigns contiguous row strips; also the hook for 2-D blocks later.
void __wator_process_assign_zones    (WatorProcess* procs, const i32 s_rows, const i32 s_threads);

/* ====================# RULES #==================== */

/*
  One sub-step of the given species: reads src, writes dst, touching only
  the zone owned by p (reads may reach the neighbours' boundary rows).
  `gen` is the generation G used by the (G + X + Y) mod P selection rule.
*/
void __wator_process_rabbit_substep    (WatorProcess* p, const i32 gen, const WatorWorld* src, WatorWorld* dst);
void __wator_process_fox_substep       (WatorProcess* p, const i32 gen, const WatorWorld* src, WatorWorld* dst);

/* ====================# SYNC #==================== */

// Blocks until it is safe for p to run sub-step number `substep`
// (0-based; sub-step s reads world[s & 1] and writes world[(s + 1) & 1]).
void __wator_process_sync_wait               (const WatorProcess* p, const i32 substep);
// Marks `substep` sub-steps as done for p.
void __wator_process_sync_publish            (WatorProcess* p, const i32 substep);

/* ====================# BALANCE #==================== */

// Total animals in p's zone.
i64  __wator_process_count_animals   (const WatorProcess* p);
// Barrier hook: rebalance zones when the load is badly skewed.
void __wator_process_rebalance       (WatorProcess* procs, const i32 s_threads);

#endif // !__WATOR_PRIVATE_H__
