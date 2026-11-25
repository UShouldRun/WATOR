#include "wator_private.h"

#include <stdlib.h>
#include <string.h>

// ====================# PUBLIC #====================

/**
 * @brief Creates a world without buffers: s_rows = s_cols = 0, zeroed params.
 * @return a new world, or NULL on allocation failure.
 */
World* wator_world_create(void) {
  World* w = calloc(1, sizeof(struct world));
  if (w == NULL)
    return NULL;

  return w;
}

/**
 * @brief Frees the buffers (if any) and the world.
 * @return false if w was NULL.
 */
bool wator_world_destroy(World* w) {
  if (w == NULL)
    return false;

  __world_free_bufs(w);
  free(w);

  return true;
}

/**
 * @brief Runs p.s_gen generations: sequentially when s_threads <= 1,
 *        otherwise on s_threads pthreads (clamped so that every zone keeps
 *        at least two rows, since the rules read two rows into neighbouring
 *        zones and sync only covers immediate neighbours).
 * @return false on allocation or thread-creation failure.
 */
bool wator_world_run(World* w, const i32 s_threads) {
  i32 T = s_threads;
  if (T > w->s_rows / 2)
    T = w->s_rows / 2;

  return __process_run(w, T);
}

/**
 * @brief Prints buf[0] in the input format: params, s_gen printed as 0,
 *        then the final state, N the object count, listed row-major.
 */
void wator_world_print(const World* w, FILE* out) {
  // Reads buf[0] while the pull kernel is the running path; flips to
  // the block-major grid when the bucket kernel lands.
  const Cell* g = w->buf[0];
  const i64 s_grid = (i64) w->s_rows * w->s_cols;

  i64 n_objects = 0;
  for (i64 i = 0; i < s_grid; i++)
    if (g[i].type != WATOR_EMPTY)
      n_objects++;

  fprintf(out, "%d %d %d 0 %d %d %lld\n",
          w->p.s_gen_proc_rabbits, w->p.s_gen_proc_foxes, w->p.s_gen_food_foxes,
          w->s_rows, w->s_cols, (long long) n_objects);

  static const char* const names[] = {
    [WATOR_ROCK]   = "ROCK",
    [WATOR_RABBIT] = "RABBIT",
    [WATOR_FOX]    = "FOX",
  };
  for (i32 r = 0; r < w->s_rows; r++)
    for (i32 c = 0; c < w->s_cols; c++) {
      u8 t = g[(i64) r * w->s_cols + c].type;
      if (t != WATOR_EMPTY)
        fprintf(out, "%s %d %d\n", names[t], r, c);
    }
}

/**
 * @brief Parses parameters + initial entities from in into buf[0].
 *
 * First line: S_GEN_PROC_RABBITS S_GEN_PROC_FOXES S_GEN_FOOD_FOXES
 * S_GEN S_ROWS S_COLS S_OBJECTS, then one OBJECT X Y line per object,
 * OBJECT in {ROCK, RABBIT, FOX}.
 *
 * @return false on malformed input or allocation failure, freeing the
 *         buffers in that case.
 */
bool wator_world_load(World* w, FILE* in) {
  Params* p = &w->p;
  i32 s_rows, s_cols, s_objects;

  if (fscanf(in, "%d %d %d %d %d %d %d",
             &p->s_gen_proc_rabbits, &p->s_gen_proc_foxes, &p->s_gen_food_foxes,
             &p->s_gen, &s_rows, &s_cols, &s_objects) != 7)
    return false;

  if (s_rows <= 0 || s_cols <= 0 || s_objects < 0 || p->s_gen < 0)
    return false;

  if (!__world_alloc_bufs(w, s_rows, s_cols))
    return false;

  for (i32 i = 0; i < s_objects; i++) {
    char name[16];
    i32 x, y;

    if (fscanf(in, "%15s %d %d", name, &x, &y) != 3)
      goto fail;

    if (x < 0 || x >= s_rows || y < 0 || y >= s_cols)
      goto fail;

    Cell* cell = __wator_cell_at(w->buf[0], s_cols, x, y);
    if (cell->type != WATOR_EMPTY)
      goto fail;  // two objects on the same cell

    u8 type;
    if      (strcmp(name, "ROCK")   == 0) type = WATOR_ROCK;
    else if (strcmp(name, "RABBIT") == 0) type = WATOR_RABBIT;
    else if (strcmp(name, "FOX")    == 0) type = WATOR_FOX;
    else
      goto fail;

    cell->type = type;
    // proc_age and food_age start at 0 (buffers are zeroed).

    u8* g = __wator_grid_at(&w->grid, __wator_s_blocks_row(w), x, y);
    if (*g != WATOR_EMPTY)
      goto fail;  // two objects on the same cell

    *g = type;
    // Ages start at 0 (arrays are zeroed).
  }

  return true;

fail:
  __world_free_bufs(w);
  return false;
}

// ====================# PRIVATE #====================

// Allocates the grids and ages for an s_rows x s_cols world (grid and
// claims block-major, zeroed; ages zeroed; pull-kernel bufs too).
bool __world_alloc_bufs(World* w, const i32 s_rows, const i32 s_cols) {
  const i64 s_grid = (i64) s_rows * s_cols;
  const i32 s_blocks_row = (s_cols + 3) / 4;
  const i32 s_blocks_col = (s_rows + 3) / 4;
  const u32 s_blocks = (u32) ((i64) s_blocks_col * s_blocks_row);

  w->s_rows = s_rows;
  w->s_cols = s_cols;

  w->grid.s_grid = s_blocks;
  w->claims.s_grid = s_blocks;

  w->grid.grid = calloc(s_grid ? s_grid : 1, sizeof(u8));
  w->claims.claims = calloc((s_grid + 1) / 2 ? (s_grid + 1) / 2 : 1, sizeof(u8));

  w->proc_age = calloc(s_grid ? s_grid : 1, sizeof(u16));
  w->food_age = calloc(s_grid ? s_grid : 1, sizeof(u16));

  // The pull kernel still runs on its row-major Cell layout.
  w->buf[0] = calloc(s_grid ? s_grid : 1, sizeof(Cell));
  w->buf[1] = calloc(s_grid ? s_grid : 1, sizeof(Cell));

  return w->grid.grid != NULL && w->claims.claims != NULL
      && w->proc_age != NULL && w->food_age != NULL
      && w->buf[0] != NULL && w->buf[1] != NULL;
}

void __world_free_bufs(World* w) {
  free(w->buf[0]);
  free(w->buf[1]);
  w->buf[0] = w->buf[1] = NULL;

  free(w->proc_age);
  free(w->food_age);
  w->proc_age = w->food_age = NULL;

  free(w->grid.grid);
  free(w->claims.claims);
  w->grid.grid = w->claims.claims = NULL;
}
