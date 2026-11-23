#include <stdio.h>
#include <stdlib.h>

#include "process.h"
#include "sequential.h"
#include "world.h"

// Usage: ./sim [n_threads] < input > output
// n_threads <= 1 (the default) runs the simulation sequentially.
int main(int argc, char **argv) {
    World w = {0};
    w.p.n_threads       = argc > 1 ? atoi(argv[1]) : 1;
    w.p.rebalance_every = 50;

    if (w.p.n_threads < 1) {
        fprintf(stderr, "usage: %s [n_threads] < input\n", argv[0]);
        return 1;
    }
    if (world_load(&w, stdin) != 0) {
        fprintf(stderr, "failed to load world\n");
        return 1;
    }

    // Every zone needs >= 2 rows: the rabbit/fox rules read 2 rows into the
    // neighbouring zones, and sync only checks the immediate neighbours.
    int T = w.p.n_threads;
    if (T > w.R / 2) T = w.R / 2;

    if (T <= 1) {
        if (sequential_run(&w) != 0) {
            fprintf(stderr, "sequential run failed\n");
            return 1;
        }
    } else {
        Process *procs = calloc(T, sizeof(Process));
        if (!procs) return 1;
        process_init_all(procs, &w, T);
        if (process_run_all(procs, T) != 0) {
            fprintf(stderr, "failed to start threads\n");
            return 1;
        }
        process_destroy_all(procs, T);
        free(procs);
    }

    // After 2 sub-steps per generation the result is back in buf[0].
    world_print(&w, 0, stdout);

    world_free(&w);
    return 0;
}
