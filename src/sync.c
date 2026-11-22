#define _POSIX_C_SOURCE 200809L
#include "sync.h"

#include <sched.h>

// Window rule: to run sub-step s, each neighbour must have completed at
// least s sub-steps (so the cells I read are final). The neighbour applies
// the same rule to me, so it cannot overwrite what I am reading before I
// finish. Net effect: neighbours differ by at most one sub-step, and zones
// k apart can drift up to k sub-steps apart.
void sync_wait_neighbours(const Process *p, int substep) {
    const Process *nb[2] = { p->up, p->down };
    for (int i = 0; i < 2; i++) {
        if (!nb[i]) continue;
        while (atomic_load_explicit(&nb[i]->progress, memory_order_acquire) < substep)
            sched_yield();   // TODO: try pure spin / condvar and compare
    }
}

void sync_publish(Process *p, int completed) {
    atomic_store_explicit(&p->progress, completed, memory_order_release);
}
