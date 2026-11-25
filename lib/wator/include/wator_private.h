/**
 * @file wator_private.h
 * @brief Wa-Tor internals: concrete structs and private prototypes
 *
 * Includes the public API, then defines the data layouts (cell, params,
 * world, process) and declares every internal function. Not part of the
 * public interface: only translation units of the library and its tests
 * include this header.
 */
#ifndef __WATOR_PRIVATE_H__
#define __WATOR_PRIVATE_H__

#include "wator.h"

#include <pthread.h>
#include <stdatomic.h>

// ====================# DATA LAYOUT #====================

// One grid cell type: 0..3. One byte per cell in the grid array;
// the packed variant packs a 4x4 block into one u32 (2 bits/cell).
typedef enum wator_cell_type {
  WATOR_EMPTY  = 0,
  WATOR_ROCK   = 1,
  WATOR_RABBIT = 2,
  WATOR_FOX    = 3,
} CellType;

typedef struct params {
  i32
    s_gen_proc_rabbits,  // generations until a rabbit can procreate
    s_gen_proc_foxes,    // generations until a fox can procreate
    s_gen_food_foxes,    // generations before a fox starves
    s_gen,               // generations to simulate
    s_threads,           // requested worker threads
    s_rebalance_every;   // generations between load-balancing barriers
} Params;

// Pull-kernel layout: one packed Cell per cell of a row-major grid.
// The bucket rewrite moves this into rules.c; kept here while both
// kernels coexist.
typedef struct cell {
  u8
    type,      // WATOR_*
    proc_age;  // generations since last reproduction

  u16
    food_age;  // generations since last meal (foxes only)
} Cell;

#define CELL_EMPTY (Cell){ WATOR_EMPTY, 0, 0 }

// Cell at row r, column c of a row-major grid of s_cols columns.
static inline Cell* __wator_cell_at(Cell* g, const i32 s_cols, const i32 r, const i32 c) {
  return &g[(i64) r * s_cols + c];
}

// 4x4-block grid: s_grid blocks, stored block-major. Block (br, bc) is
// the contiguous run blocks[(br * s_blocks_row + bc) * 16]; inside it
// the 4 rows are consecutive. Cell (r, c) lives at
//   (r / 4 * s_blocks_row + c / 4) * 16 + (r % 4) * 4 + c % 4
// u32 s_grid + u8* grid
typedef struct world_grid {
  u32
    s_grid;  // 4x4 block count (u8* grid: one byte per cell)

  u8* grid;  // WATOR_* per cell
} WorldGrid;

// Claim bits, block-major like WorldGrid: two cells per byte, one
// nibble each; the nibble holds one bit per neighbour direction that
// chose to move into the cell (and the eat bit for the fox sub-step).
// u32 s_grid + u8* claims
typedef struct world_grid_claims {
  u32
    s_grid;  // 4x4 block count

  u8* claims;  // two cells per byte, one nibble each
} WorldGridClaims;

// struct world is opaque in wator.h; this is its concrete layout.
// The ages are their own arrays: pass 1 scans cells without needing
// them, and the pull kernel keeps its Cell layout inside rules.c.
struct world {
  WorldGrid       grid;   // cells, block-major
  WorldGridClaims claims;  // claim bits, block-major

  i32
    s_rows,  // grid rows
    s_cols;  // grid columns

  u16
    *proc_age,  // breeding age of the animal in the cell
    *food_age;  // starvation age of the animal in the cell

  Cell* buf[2];  // pull-kernel double buffer, row-major

  Params p;
};

// A worker thread: owns rows [row_begin, row_end) of the grid and is the
// only thread that writes there. 1-D strips; add col_* fields for 2-D.
typedef struct process {
  i32
    id,
    row_begin,
    row_end;

  pthread_t thread;

  struct world* world;

  // Sub-steps completed: 2 * gen + (0 = start, 1 = rabbits done).
  // See __sync_wait for the window rule neighbours must respect.
  _Atomic i32 progress;

  struct process* up,
                * down;  // neighbouring workers, NULL when none

  // Animals (rabbits + foxes) per row; rows with 0 are skipped.
  // Size = s_rows.
  u8* animals_per_row;
} Process;

// ====================# GRID (BLOCK-MAJOR) #====================

// Blocks per block-row (s_cols rounded up to 4).
static inline i32 __wator_s_blocks_row(const struct world* w) {
  return (w->s_cols + 3) / 4;
}

// Grid cell at (r, c) in a block-major WorldGrid.
static inline u8* __wator_grid_at(const WorldGrid* g, const i32 s_blocks_row, const i32 r, const i32 c) {
  const i64 i_block = (i64) (r >> 2) * s_blocks_row + (c >> 2);
  return &g->grid[i_block * 16 + (r & 3) * 4 + (c & 3)];
}

// ====================# WORLD #====================

// Allocates the grids and ages for an s_rows x s_cols world (grid and
// claims block-major, zeroed; ages zeroed; pull-kernel bufs too).
bool __world_alloc_bufs    (World* w, const i32 s_rows, const i32 s_cols);
void __world_free_bufs     (World* w);

// ====================# PROCESS #====================

// Runs the whole simulation: sequentially on the calling thread when
// s_threads <= 1, otherwise on s_threads pthreads. False on failure.
bool __process_run             (World* w, const i32 s_threads);
// Sets up s_threads workers over w: links neighbours, allocates
// animals_per_row and fills it from the loaded grid.
bool __process_init_all        (Process* procs, World* w, const i32 s_threads);
void __process_destroy_all     (Process* procs, const i32 s_threads);
// (Re)assigns contiguous row strips; also the hook for 2-D blocks later.
void __process_assign_zones    (Process* procs, const i32 s_rows, const i32 s_threads);

// ====================# RULES #====================

// One sub-step of the given species: reads src, writes dst, touching only
// the zone owned by p (reads may reach the neighbours' boundary rows).
// `gen` is the generation G used by the (G + X + Y) mod P selection rule.
void __rules_rabbit_substep    (Process* p, const i32 gen, const Cell* src, Cell* dst);
void __rules_fox_substep       (Process* p, const i32 gen, const Cell* src, Cell* dst);

// ====================# SYNC #====================

// Blocks until it is safe for p to run sub-step number `substep`
// (0-based; sub-step s reads buf[s & 1] and writes buf[(s + 1) & 1]).
void __sync_wait               (const Process* p, const i32 substep);
// Marks `substep` sub-steps as done for p.
void __sync_publish            (Process* p, const i32 substep);

// ====================# BALANCE #====================

// Total animals in p's zone, from animals_per_row.
i64  __balance_count_animals   (const Process* p);
// Barrier hook: rebalance zones when the load is badly skewed.
void __balance_rebalance       (Process* procs, const i32 s_threads);

#endif // !__WATOR_PRIVATE_H__
