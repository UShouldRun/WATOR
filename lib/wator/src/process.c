// TODO(refactor): port to the new interface: Process -> WatorProcess,
// World -> Wator, w->p -> params, buf/Cell -> world[2]/WatorWorld; drop
// animals_per_row; run the two-pass sub-step; block-row-aligned zones.

#include "wator_private.h"

#include <stdlib.h>

// ====================# PRIVATE #====================

// Thread body: runs every sub-step of the simulation.
static void* __wator_process_worker_main(void* arg) {
  Process* p = arg;
  World* w = p->world;
  i32 s_total = 2 * w->p.s_gen;

  // Sub-step s: even = rabbits (A -> B), odd = foxes (B -> A).
  for (i32 s = 0; s < s_total; s++) {
    __wator_process_sync_wait(p, s);

    // TODO(refactor): src/dst are &w->world[s & 1] / &w->world[(s + 1) & 1]
    // (WatorWorld*), and the sub-step becomes the two passes: pass 1,
    // publish the pass-1 progress, wait the neighbours' pass 1, pass 2,
    // publish the sub-step (see report, "Synchronisation"). Clear the claims
    // and the claims tree before pass 1.
    Cell* src = w->buf[s & 1];
    Cell* dst = w->buf[(s + 1) & 1];

    if ((s & 1) == 0)
      __wator_process_rabbit_substep(p, s >> 1, src, dst);
    else
      __wator_process_fox_substep(p, s >> 1, src, dst);

    __wator_process_sync_publish(p, s + 1);

    // TODO: every s_rebalance_every generations -> barrier + __wator_process_rebalance
  }

  return NULL;
}

// (Re)assigns contiguous row strips; also the hook for 2-D blocks later.
//
// TODO: align strip boundaries to block-rows (multiples of 4 rows).
// The grid is block-major: a boundary in the middle of a 4x4 block puts two
// workers on the same 16-byte block (false sharing on grid and claims) and
// on the same quad-tree leaf (its counts would then need atomics).
// With aligned strips the worker cap is ceil(s_rows / 4) instead of
// s_rows / 2 (see wator_run); 200x200 still allows 50 workers.
// s_rows is the shorter side (inputs with rows > cols are stored
// transposed), so the cap is set by min(R, C), as it already was for
// wide inputs before the transposition.
void __wator_process_assign_zones(Process* procs, const i32 s_rows, const i32 s_threads) {
  for (i32 i = 0; i < s_threads; i++) {
    procs[i].row_begin = (i32) ((i64) s_rows * i / s_threads);
    procs[i].row_end   = (i32) ((i64) s_rows * (i + 1) / s_threads);
  }
}

// Sets up s_threads workers over w: links neighbours, allocates
// animals_per_row and fills it from the loaded grid.
// TODO(refactor): procs[i].world -> procs[i].wator; drop animals_per_row
// (alloc, free and the initial count below): the occupancy tree replaces
// it and is built once from world[0].grid after load (or in alloc/load).
// The cell read must use __wator_world_grid_get, not __wator_cell_at / buf[0].
bool __wator_process_init_all(Process* procs, World* w, const i32 s_threads) {
  for (i32 i = 0; i < s_threads; i++) {
    procs[i].id    = i;
    procs[i].world = w;
    procs[i].up    = i > 0 ? &procs[i - 1] : NULL;
    procs[i].down  = i < s_threads - 1 ? &procs[i + 1] : NULL;

    procs[i].animals_per_row = (u8*)calloc(w->s_rows ? w->s_rows : 1, sizeof(u8));
    if (procs[i].animals_per_row == NULL) {
      for (i32 i = 0; i < s_threads; i++)
        free(procs[i].animals_per_row);

      return false;
    }

    atomic_init(&procs[i].progress, 0);
  }
  __wator_process_assign_zones(procs, w->s_rows, s_threads);

  // Initial animals-per-row of each zone, from the loaded grid.
  for (i32 i = 0; i < s_threads; i++)
    for (i32 row = procs[i].row_begin; row < procs[i].row_end; row++) {
      i32 n_animals = 0;

      for (i32 col = 0; col < w->s_cols; col++) {
        u8 t = __wator_cell_at(w->buf[0], w->s_cols, row, col)->type;

        if (t == WATOR_RABBIT || t == WATOR_FOX)
          n_animals++;
      }

      procs[i].animals_per_row[row] = n_animals > 255 ? 255 : (u8)n_animals;
    }

  return true;
}

// TODO(refactor): nothing left to free per worker once animals_per_row is
// gone; keep the hook if the quad trees end up per worker.
void __wator_process_destroy_all(Process* procs, const i32 s_threads) {
  for (i32 i = 0; i < s_threads; i++)
    free(procs[i].animals_per_row);
}

// Runs the whole simulation: sequentially on the calling thread when
// s_threads <= 1, otherwise on s_threads pthreads.
bool __wator_process_run(World* w, const i32 s_threads) {
  Process* procs = (Process*)calloc(s_threads, sizeof(Process));
  if (procs == NULL)
    return false;

  if (!__wator_process_init_all(procs, w, s_threads)) {
    free(procs);
    return false;
  }

  bool ok = true;
  i32 n_created = 0;
  if (s_threads == 1) {
    // No neighbours: run the whole simulation on the calling thread.
    __wator_process_worker_main(&procs[0]);

  } else {
    for (i32 i = 0; i < s_threads; i++) {
      i32 err = pthread_create(
        &procs[i].thread, NULL,
        __wator_process_worker_main, &procs[i]
      );

      if (err != 0) {
        ok = false;
        break;
      }

      n_created = i + 1;
    }

    // If a create failed, the workers that did start are left running: they
    // are in sync with each other and will finish the simulation on their
    // own. We report the failure without waiting for them (their results
    // are not trusted), so we join nothing on the failure path.
    if (ok)
      for (i32 i = 0; i < n_created; i++)
        pthread_join(procs[i].thread, NULL);
  }

  __wator_process_destroy_all(procs, s_threads);
  free(procs);

  return ok;
}
