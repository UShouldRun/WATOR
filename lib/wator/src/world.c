#include "wator_private.h"

#include <stdlib.h>
#include <string.h>

// ====================# PUBLIC #====================

/**
 * @brief Creates a world without buffers: s_rows = s_cols = 0, zeroed params.
 * @return a new world, or NULL on allocation failure.
 */
Wator* wator_create(void) {
  return (Wator*)calloc(1, sizeof(struct wator));
}

/**
 * @brief Frees the buffers (if any) and the world.
 * @return false if wator was NULL.
 */
bool wator_destroy(Wator* wator) {
  if (wator == NULL)
    return false;

  __wator_free(wator);
  free(wator);

  return true;
}

/**
 * @brief Runs params.s_gen generations: sequentially when s_threads <= 1,
 *        otherwise on s_threads pthreads (clamped so that every zone keeps
 *        at least two rows, since the rules read two rows into neighbouring
 *        zones and sync only covers immediate neighbours).
 * @return false on allocation or thread-creation failure.
 */
bool wator_run(Wator* wator, const i32 s_threads) {
  i32 T = s_threads;
  if (T > wator->s_rows / 2)
    T = wator->s_rows / 2;

  return __wator_process_run(wator, T);
}

/**
 * @brief Prints world[0] in the input format: params, s_gen printed as 0,
 *        then the final state, N the object count, listed row-major in
 *        input coordinates (the transposition is undone here).
 *
 * A run is 2 * s_gen sub-steps, an even count, so the final state is
 * always in world[0].
 */
void wator_print(const Wator* wator, FILE* out) {
  const WatorParams* p = &wator->params;
  const u8* grid = wator->world[0].grid;
  const i32 s_blocks_row = __wator_s_blocks_row(wator);

  // Input dimensions.
  const i32
    s_rows = wator->transposed ? wator->s_cols : wator->s_rows,
    s_cols = wator->transposed ? wator->s_rows : wator->s_cols;

  i64 n_objects = 0;
  for (i32 r = 0; r < wator->s_rows; r++)
    for (i32 c = 0; c < wator->s_cols; c++)
      if (__wator_world_grid_get(grid, __wator_world_cell_index(s_blocks_row, r, c)) != WATOR_EMPTY)
        n_objects++;

  fprintf(out, "%d %d %d 0 %d %d %lld\n",
          p->s_gen_proc_rabbits, p->s_gen_proc_foxes, p->s_gen_food_foxes,
          s_rows, s_cols, (long long) n_objects);

  static const char* const names[] = {
    [WATOR_ROCK]   = "ROCK",
    [WATOR_RABBIT] = "RABBIT",
    [WATOR_FOX]    = "FOX",
  };

  for (i32 x = 0; x < s_rows; x++)
    for (i32 y = 0; y < s_cols; y++) {
      const u8 t = __wator_world_grid_get(grid, wator->transposed
        ? __wator_world_cell_index(s_blocks_row, y, x)
        : __wator_world_cell_index(s_blocks_row, x, y));

      if (t != WATOR_EMPTY)
        fprintf(out, "%s %d %d\n", names[t], x, y);
    }
}

/**
 * @brief Parses parameters + initial entities from in into world[0].
 *
 * First line: S_GEN_PROC_RABBITS S_GEN_PROC_FOXES S_GEN_FOOD_FOXES
 * S_GEN S_ROWS S_COLS S_OBJECTS, then one OBJECT X Y line per object,
 * OBJECT in {ROCK, RABBIT, FOX}. When S_ROWS > S_COLS the grid is stored
 * transposed: (X, Y) goes to stored (Y, X).
 *
 * @return false on malformed input or allocation failure, freeing the
 *         buffers in that case.
 */
bool wator_load(Wator* wator, FILE* in) {
  WatorParams* p = &wator->params;
  i32 s_rows, s_cols, s_objects;

  if (fscanf(in, "%d %d %d %d %d %d %d",
             &p->s_gen_proc_rabbits, &p->s_gen_proc_foxes, &p->s_gen_food_foxes,
             &p->s_gen, &s_rows, &s_cols, &s_objects) != 7)
    return false;

  if (s_rows <= 0 || s_cols <= 0 || s_objects < 0 || p->s_gen < 0)
    return false;

  wator->transposed = s_rows > s_cols;

  if (wator->transposed
        ? !__wator_alloc(wator, s_cols, s_rows)
        : !__wator_alloc(wator, s_rows, s_cols))
    return false;

  u8* const grid = wator->world[0].grid;
  const i32 s_blocks_row = __wator_s_blocks_row(wator);

  for (i32 i = 0; i < s_objects; i++) {
    char name[16];
    i32 x, y;

    if (fscanf(in, "%15s %d %d", name, &x, &y) != 3)
      goto fail;

    if (x < 0 || x >= s_rows || y < 0 || y >= s_cols)
      goto fail;

    u8 type;
    if      (strcmp(name, "ROCK")   == 0) type = WATOR_ROCK;
    else if (strcmp(name, "RABBIT") == 0) type = WATOR_RABBIT;
    else if (strcmp(name, "FOX")    == 0) type = WATOR_FOX;
    else
      goto fail;

    const i64 i_cell = wator->transposed
      ? __wator_world_cell_index(s_blocks_row, y, x)
      : __wator_world_cell_index(s_blocks_row, x, y);

    if (__wator_world_grid_get(grid, i_cell) != WATOR_EMPTY)
      goto fail;  // two objects on the same cell

    __wator_world_grid_set(grid, i_cell, type);
    // Ages start at 0 (arrays are zeroed).
  }

  return true;

fail:
  __wator_free(wator);
  return false;
}

// ====================# PRIVATE #====================

// Allocates both worlds and the claims for an s_rows x s_cols world in one
// zeroed block (wator->mem), all block-major with padded edge blocks:
// per world, grid s_blocks * 4 bytes (2 bits/cell), proc_age and food_age
// s_blocks * 16 u16 each; claims s_blocks * 8 bytes (4 bits/cell).
bool __wator_alloc(Wator* wator, const i32 s_rows, const i32 s_cols) {
  const u64 s_blocks = (u64)((s_rows + 3) / 4) * ((s_cols + 3) / 4);

  const u64 s_mem =
    2 * s_blocks * (4 * sizeof(u8) + 16 * sizeof(u16) + 16 * sizeof(u16)) + /* grid + proc_age + food_age memory*/
    s_blocks * 8 * sizeof(u8);       /* claims memory */

  wator->mem = (void*)calloc(1, s_mem);
  if (wator->mem == NULL) {
    wator->world[0] = (WatorWorld){ 0 };
    wator->world[1] = (WatorWorld){ 0 };
    wator->claims   = NULL;
    return false;
  }

  const u64 
    proc_age_offset      = s_blocks * 4 * sizeof(u8),
    food_age_offset      = s_blocks * (4 * sizeof(u8) + 16 * sizeof(u16)),
    second_buffer_offset = s_blocks * (4 * sizeof(u8) + 16 * sizeof(u16) + 16 * sizeof(u16)),
    claims_offset        = 2 * s_blocks * (4 * sizeof(u8) + 16 * sizeof(u16) + 16 * sizeof(u16));

  wator->s_rows = s_rows;
  wator->s_cols = s_cols;

  wator->world[0].grid     = &((u8*)wator->mem)[0];
  wator->world[0].proc_age = (u16*)&((u8*)wator->mem)[proc_age_offset];
  wator->world[0].food_age = (u16*)&((u8*)wator->mem)[food_age_offset];

  wator->world[1].grid     = &((u8*)wator->mem)[second_buffer_offset];
  wator->world[1].proc_age = (u16*)&((u8*)wator->mem)[second_buffer_offset + proc_age_offset];
  wator->world[1].food_age = (u16*)&((u8*)wator->mem)[second_buffer_offset + food_age_offset];
  
  wator->claims = &((u8*)wator->mem)[claims_offset];

  return true;
}

// Frees the block behind every array and clears the pointers, so a second
// call (load's fail path, then wator_destroy) is harmless.
void __wator_free(Wator* wator) {
  wator->world[0] = (WatorWorld){ 0 };
  wator->world[1] = (WatorWorld){ 0 };
  wator->claims = NULL;

  if (wator->mem != NULL)
    free(wator->mem);

  wator->mem = NULL;
}
