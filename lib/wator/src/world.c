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

    Cell* c = __wator_cell_at(w->buf[0], s_cols, x, y);
    if (c->type != WATOR_EMPTY)
      goto fail;  // two objects on the same cell

    if      (strcmp(name, "ROCK")   == 0) c->type = WATOR_ROCK;
    else if (strcmp(name, "RABBIT") == 0) c->type = WATOR_RABBIT;
    else if (strcmp(name, "FOX")    == 0) c->type = WATOR_FOX;
    else
      goto fail;
    // proc_age and food_age start at 0 (buffers are zeroed).
  }
  return true;

fail:
  __world_free_bufs(w);
  return false;
}

// ====================# PRIVATE #====================

// Allocates both double-buffer grids (zeroed) for an s_rows x s_cols world.
bool __world_alloc_bufs(World* w, const i32 s_rows, const i32 s_cols) {
  const i64 s_grid = (i64) s_rows * s_cols;
  w->s_rows = s_rows;
  w->s_cols = s_cols;
  w->buf[0] = calloc(s_grid ? s_grid : 1, sizeof(Cell));
  w->buf[1] = calloc(s_grid ? s_grid : 1, sizeof(Cell));

  return w->buf[0] != NULL && w->buf[1] != NULL;
}

void __world_free_bufs(World* w) {
  free(w->buf[0]);
  free(w->buf[1]);
  w->buf[0] = w->buf[1] = NULL;
}
