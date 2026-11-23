#define _POSIX_C_SOURCE 200809L

#include "wator_private.h"

#include <sched.h>

// Window rule: to run sub-step s, each neighbour must have completed at
// least s sub-steps (so the cells I read are final). The neighbour applies
// the same rule to me, so it cannot overwrite what I am reading before I
// finish. Net effect: neighbours differ by at most one sub-step, and zones
// k apart can drift up to k sub-steps apart.
// ====================# PUBLIC #====================

void __sync_wait(const Process* p, const i32 substep) {
  const Process* nb[2] = { p->up, p->down };

  for (i32 i = 0; i < 2; i++) {
    if (!nb[i])
      continue;
    while (atomic_load_explicit(&nb[i]->progress, memory_order_acquire) < substep)
      sched_yield();  // TODO: try pure spin / condvar and compare
  }
}

// Marks `substep` sub-steps as done for p.
void __sync_publish(Process* p, const i32 substep) {
  atomic_store_explicit(&p->progress, substep, memory_order_release);
}
