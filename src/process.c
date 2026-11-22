#include "process.h"

#include <stdlib.h>

#include "balance.h"
#include "rules.h"
#include "sync.h"

void process_assign_zones(Process *procs, int R, int T) {
    for (int i = 0; i < T; i++) {
        procs[i].row_begin = (int)((long)R * i / T);
        procs[i].row_end   = (int)((long)R * (i + 1) / T);
    }
}

void process_init_all(Process *procs, World *w, int T) {
    for (int i = 0; i < T; i++) {
        procs[i].id    = i;
        procs[i].world = w;
        procs[i].up    = i > 0     ? &procs[i - 1] : NULL;
        procs[i].down  = i < T - 1 ? &procs[i + 1] : NULL;
        procs[i].animals_per_row = calloc(w->R ? w->R : 1, sizeof(uint8_t));
        atomic_init(&procs[i].progress, 0);
    }
    process_assign_zones(procs, w->R, T);

    // Initial animals-per-row of each zone, from the loaded grid.
    for (int i = 0; i < T; i++) {
        for (int r = procs[i].row_begin; r < procs[i].row_end; r++) {
            int count = 0;
            for (int c = 0; c < w->C; c++) {
                uint8_t t = cell_at(w->buf[0], w, r, c)->type;
                if (t == RABBIT || t == FOX) count++;
            }
            procs[i].animals_per_row[r] = count > 255 ? 255 : (uint8_t)count;
        }
    }
}

void process_destroy_all(Process *procs, int T) {
    for (int i = 0; i < T; i++) free(procs[i].animals_per_row);
}

void *process_worker_main(void *arg) {
    Process *p = arg;
    World   *w = p->world;
    int total  = 2 * w->p.n_gen;

    // Sub-step s: even = rabbits (A->B), odd = foxes (B->A).
    for (int s = 0; s < total; s++) {
        sync_wait_neighbours(p, s);

        Cell *src = w->buf[s & 1];
        Cell *dst = w->buf[(s + 1) & 1];
        if ((s & 1) == 0) rabbit_substep(p, s / 2, src, dst);
        else              fox_substep(p, s / 2, src, dst);

        sync_publish(p, s + 1);

        // TODO: every rebalance_every generations -> barrier + balance_rebalance
    }
    return NULL;
}

int process_run_all(Process *procs, int T) {
    for (int i = 0; i < T; i++)
        if (pthread_create(&procs[i].thread, NULL, process_worker_main, &procs[i]) != 0)
            return -1;
    for (int i = 0; i < T; i++)
        pthread_join(procs[i].thread, NULL);
    return 0;
}
