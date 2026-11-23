#include "sequential.h"

#include <stdlib.h>

#include "process.h"
#include "rules.h"

// A single Process owning every row: no neighbours, so no synchronisation is
// needed and the shared rule code can be reused unchanged.
int sequential_run(World *w) {
    Process *p = calloc(1, sizeof(Process));
    if (!p) return -1;
    process_init_all(p, w, 1);

    for (int g = 0; g < w->p.n_gen; g++) {
        rabbit_substep(p, g, w->buf[0], w->buf[1]);   // A -> B
        fox_substep(p, g, w->buf[1], w->buf[0]);      // B -> A
    }

    process_destroy_all(p, 1);
    free(p);
    return 0;
}
