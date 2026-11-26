// TODO(refactor): port the tests to Wator / block-major grid; details below
// the includes.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
  TODO(refactor): the tests still use the old layout and must be ported:
   - World -> Wator, w->p -> params, __world_alloc_bufs -> __wator_alloc.
   - cell reads __wator_cell_at(w->buf[0], ...)->type become
     __wator_world_grid_get(w->world[0].grid, __wator_world_cell_index(...)), and
     ages world[0].proc_age / food_age at the same block-major index.
   - the random-world oracle test compares memcmp on Cell buffers and checks
     animals_per_row: keep the pull kernel's reference step (row-major Cell
     is fine inside the test), convert to/from the block-major world for the
     comparison, and check the occupancy tree instead of animals_per_row
     (QuadTree, lib/quadtree, is not implemented yet: it needs its own tests).
   - __rules_*_substep now take WatorWorld* src / dst.
   - add cases: rows > cols input (transposed load/print round-trip and the
     same result as the untransposed run), and R, C not multiples of 4.
*/
#include "wator_private.h"

/* ====================# WORLD #==================== */

static const char* EXAMPLE =
    "2 4 3 6 5 5 9\n"
    "ROCK 0 0\n"
    "RABBIT 0 2\n"
    "FOX 0 4\n"
    "FOX 1 0\n"
    "FOX 1 4\n"
    "ROCK 2 4\n"
    "RABBIT 3 0\n"
    "RABBIT 4 0\n"
    "FOX 4 4\n";

// Same objects, row-major order, S_GEN printed as 0.
static const char* EXPECTED =
    "2 4 3 0 5 5 9\n"
    "ROCK 0 0\n"
    "RABBIT 0 2\n"
    "FOX 0 4\n"
    "FOX 1 0\n"
    "FOX 1 4\n"
    "ROCK 2 4\n"
    "RABBIT 3 0\n"
    "RABBIT 4 0\n"
    "FOX 4 4\n";

// After the 6 generations of EXAMPLE, the world must look like this.
static const char* FINAL_5X5 =
    "2 4 3 0 5 5 5\n"
    "ROCK 0 0\n"
    "ROCK 2 4\n"
    "RABBIT 3 0\n"
    "FOX 4 0\n"
    "FOX 4 3\n";

static bool __test_load_str(World* w, const char* s) {
  FILE* f = fmemopen((void*) s, strlen(s), "r");
  assert(f);

  bool ok = wator_load(w, f);
  fclose(f);

  return ok;
}

static char* __test_print_to_str(const World* w) {
  char* buf = NULL;
  size_t len = 0;
  FILE* out = open_memstream(&buf, &len);
  assert(out);

  wator_print(w, out);
  fclose(out);

  return buf;
}

/* ====================# TESTS #==================== */

static void test_world_roundtrip(void) {
  printf("world: round-trip load + print\n");

  World* w = wator_create();
  assert(w);
  assert(__test_load_str(w, EXAMPLE));
  assert(w->s_rows == 5 && w->s_cols == 5);
  assert(w->p.s_gen_proc_rabbits == 2 && w->p.s_gen_proc_foxes == 4);
  assert(w->p.s_gen_food_foxes == 3 && w->p.s_gen == 6);
  assert(__wator_cell_at(w->buf[0], w->s_cols, 0, 2)->type == WATOR_RABBIT);
  assert(__wator_cell_at(w->buf[0], w->s_cols, 2, 4)->type == WATOR_ROCK);
  assert(__wator_cell_at(w->buf[0], w->s_cols, 4, 4)->type == WATOR_FOX);

  char* out = __test_print_to_str(w);
  assert(strcmp(out, EXPECTED) == 0);
  free(out);

  assert(wator_destroy(w));
}

static void test_world_invalid(void) {
  printf("world: reject malformed input\n");

  World* w = wator_create();
  assert(w);
  assert(!__test_load_str(w, ""));
  assert(!__test_load_str(w, "1 1 1 1 2 2 1\nROCK 5 0\n"));         // out of bounds
  assert(!__test_load_str(w, "1 1 1 1 2 2 1\nTREE 0 0\n"));         // unknown object
  assert(!__test_load_str(w, "1 1 1 1 2 2 2\nROCK 0 0\nFOX 0 0\n"));  // same cell
  assert(!__test_load_str(w, "1 1 1 1 2 2 3\nROCK 0 0\n"));         // truncated

  assert(wator_destroy(w));
}

static void test_simulation_sequential(void) {
  printf("simulation: sequential run matches the course example\n");

  World* w = wator_create();
  assert(w);
  assert(__test_load_str(w, EXAMPLE));
  assert(wator_run(w, 1));

  char* out = __test_print_to_str(w);
  assert(strcmp(out, FINAL_5X5) == 0);
  free(out);

  assert(wator_destroy(w));
}

static void test_simulation_parallel(void) {
  printf("simulation: parallel run matches the course example\n");

  World* w = wator_create();
  assert(w);
  assert(__test_load_str(w, EXAMPLE));
  assert(wator_run(w, 2));

  char* out = __test_print_to_str(w);
  assert(strcmp(out, FINAL_5X5) == 0);
  free(out);

  assert(wator_destroy(w));
}

// Independent "push" reference: every rabbit picks its destination from
// src, movers compete for the destination cell, the oldest wins.
static void __test_ref_rabbit_step(const World* w, const i32 gen, const Cell* src, Cell* dst) {
  static const i32 dr[4] = { -1, 0, 1, 0 };
  static const i32 dc[4] = {  0, 1, 0, -1 };
  i32 PR = w->p.s_gen_proc_rabbits;
  i64 s_grid = (i64) w->s_rows * w->s_cols;

  i32* best = malloc((size_t) s_grid * sizeof(i32));
  assert(best);
  for (i64 i = 0; i < s_grid; i++) {
    best[i] = -1;
    dst[i] = (Cell){ WATOR_EMPTY, 0, 0 };
  }

  for (i32 r = 0; r < w->s_rows; r++)
    for (i32 c = 0; c < w->s_cols; c++) {
      i64 i = (i64) r * w->s_cols + c;
      Cell cur = src[i];
      if (cur.type == WATOR_ROCK || cur.type == WATOR_FOX) {
        dst[i] = cur;
        continue;
      }
      if (cur.type != WATOR_RABBIT)
        continue;

      i64 opts[4];
      i32 k = 0;
      for (i32 d = 0; d < 4; d++) {
        i32 rr = r + dr[d], cc = c + dc[d];
        if (rr < 0 || rr >= w->s_rows || cc < 0 || cc >= w->s_cols)
          continue;
        if (src[(i64) rr * w->s_cols + cc].type == WATOR_EMPTY)
          opts[k++] = (i64) rr * w->s_cols + cc;
      }
      if (!k) {
        dst[i] = (Cell){ WATOR_RABBIT, cur.proc_age < 255 ? cur.proc_age + 1 : 255, 0 };
        continue;
      }
      i64 d = opts[(gen + r + c) % k];
      if (cur.proc_age >= PR)
        dst[i] = (Cell){ WATOR_RABBIT, 0, 0 };  // baby left behind
      // Compete with the age the rabbit will have after moving.
      i32 age = cur.proc_age >= PR ? 0 : cur.proc_age + 1;
      if (age > best[d])
        best[d] = age;
    }
  for (i64 i = 0; i < s_grid; i++)
    if (best[i] >= 0)
      dst[i] = (Cell){ WATOR_RABBIT, (u8) best[i], 0 };
  free(best);
}

static void __test_random_world(World* w, const i32 density) {
  for (i32 i = 0; i < w->s_rows * w->s_cols; i++) {
    i32 x = rand() % 100;
    Cell c = { WATOR_EMPTY, 0, 0 };
    if (x < density) {
      i32 t = rand() % 4;
      if (t == 0) {
        c.type = WATOR_ROCK;
      } else if (t == 3) {
        c.type = WATOR_FOX;
        c.proc_age = rand() % 4;
        c.food_age = rand() % 3;
      } else {
        c.type = WATOR_RABBIT;
        c.proc_age = rand() % (w->p.s_gen_proc_rabbits + 3);
      }
    }
    w->buf[0][i] = c;
  }
}

// Runs the real rabbit sub-step zone by zone and compares every generation
// with the reference, over many random worlds and thread counts.
static void test_rabbit_zones_match_reference(void) {
  printf("simulation: rabbit zones match the push reference\n");

  srand(12345);
  for (i32 iter = 0; iter < 3000; iter++) {
    World* w = wator_create();
    assert(w);
    i32 s_rows = 1 + rand() % 14, s_cols = 1 + rand() % 12;
    assert(__world_alloc_bufs(w, s_rows, s_cols));
    w->p.s_gen_proc_rabbits = 1 + rand() % 4;
    __test_random_world(w, (i32[]){ 5, 15, 40, 80 }[rand() % 4]);

    i32 s_threads = 1 + rand() % (s_rows / 2 > 0 ? s_rows / 2 : 1);  // zones have >= 2 rows
    Process procs[16] = {0};
    assert(__wator_process_init_all(procs, w, s_threads));

    Cell* ref = malloc((i64) s_rows * s_cols * sizeof(Cell));
    assert(ref);
    for (i32 gen = 0; gen < 6; gen++) {
      __test_ref_rabbit_step(w, gen, w->buf[0], ref);
      for (i32 t = 0; t < s_threads; t++)
        __wator_process_rabbit_substep(&procs[t], gen, w->buf[0], w->buf[1]);
      assert(memcmp(ref, w->buf[1], (i64) s_rows * s_cols * sizeof(Cell)) == 0);

      // animals_per_row must describe the new grid
      for (i32 t = 0; t < s_threads; t++)
        for (i32 r = procs[t].row_begin; r < procs[t].row_end; r++) {
          i32 n_animals = 0;
          for (i32 c = 0; c < s_cols; c++) {
            u8 ty = w->buf[1][(i64) r * s_cols + c].type;
            n_animals += ty == WATOR_RABBIT || ty == WATOR_FOX;
          }
          assert(procs[t].animals_per_row[r] == n_animals);
        }
      memcpy(w->buf[0], w->buf[1], (i64) s_rows * s_cols * sizeof(Cell));
    }
    free(ref);
    __wator_process_destroy_all(procs, s_threads);
    assert(wator_destroy(w));
  }
}

/*
  Regression: when two rabbits move to the same cell, the winner is the one
  with the older age *after* the move. A rabbit that is procreating resets to
  0, so it loses to a younger rabbit that is not procreating.
*/
static void test_rabbit_conflict_post_move_age(void) {
  printf("simulation: procreating rabbit competes with its post-move age\n");

  /*
    2x3 world:
      A . B        A = rabbit age 1 (procreating), B = rabbit age 0.
      * . *        Rocks block every other neighbour, so both A and B can
                   only move to the centre cell (0, 1).
  */
  World* w = wator_create();
  assert(w);
  assert(__test_load_str(w,
      "1 1 1 1 2 3 4\n"
      "RABBIT 0 0\n"
      "RABBIT 0 2\n"
      "ROCK 1 0\n"
      "ROCK 1 2\n"));

  __wator_cell_at(w->buf[0], w->s_cols, 0, 0)->proc_age = 1;

  assert(wator_run(w, 1));

  // B (0 -> 1) wins the centre; A resets to 0 and leaves a baby at (0, 0).
  Cell centre = *__wator_cell_at(w->buf[0], w->s_cols, 0, 1);
  Cell baby   = *__wator_cell_at(w->buf[0], w->s_cols, 0, 0);
  Cell east   = *__wator_cell_at(w->buf[0], w->s_cols, 0, 2);
  assert(centre.type == WATOR_RABBIT && centre.proc_age == 1);
  assert(baby.type == WATOR_RABBIT && baby.proc_age == 0);
  assert(east.type == WATOR_EMPTY);

  assert(wator_destroy(w));
}

/* ====================# MAIN #==================== */

int main(void) {
  test_world_roundtrip();
  test_world_invalid();
  test_simulation_sequential();
  test_simulation_parallel();
  test_rabbit_conflict_post_move_age();
  test_rabbit_zones_match_reference();

  puts("wator tests passed");
  return 0;
}
