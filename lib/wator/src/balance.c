#include "wator_private.h"

// ====================# PUBLIC #====================

// Total animals in p's zone, from animals_per_row.
i64 __balance_count_animals(const Process* p) {
  i64 total = 0;

  for (i32 r = p->row_begin; r < p->row_end; r++)
    total += p->animals_per_row[r];

  return total;
}

// Barrier hook, called every s_rebalance_every generations: count animals
// per worker and, if the imbalance is large, resize the zones and move the
// animals_per_row data.
void __balance_rebalance(Process* procs, const i32 s_threads) {
  // TODO: barrier across all workers, compare __balance_count_animals()
  //       per worker, and if the discrepancy is large shift the zone
  //       boundaries (row_begin / row_end), moving animals_per_row data
  //       and keeping the buffers consistent with the new ownership.
  (void) procs;
  (void) s_threads;
}
